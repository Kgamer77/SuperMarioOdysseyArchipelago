#include "Multiplayer/puppets/PuppetCapturePool.h"

#include "nn/os.h"

#include <atomic>
#include <cstring>

#include "hk/diag/diag.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "sead/heap/seadHeapMgr.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Placement/PlacementInfo.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/actors/PuppetHackActor.h"
#include "Multiplayer/algorithms/CaptureSyncTable.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/puppets/LateModelDrawer.h"
#include "helpers.hpp"

namespace PuppetCapturePool {

    // Capture models are built only after a real placed actor proves that the
    // archive is resident, then use the same shared executor/draw tables as
    // the ordinary puppet models. LateModelDrawer grows those tables before
    // registration and PuppetActor poses the leased actor from the remote
    // player's transform, so this is the normal feature path again.
    constexpr bool cEnableCaptureSync = true;

    namespace {

        constexpr s32 kNameLen = 0x20;

        // Placement notification is not guaranteed to run on the game thread,
        // while acquire/release/update and stage teardown do. The pool stores
        // raw scene-heap pointers, so even a short unsynchronised read during
        // onStageKill() can turn a harmless capture fallback into a use-after-
        // teardown. Keep this lock limited to pool metadata and the bounded
        // construction/lease operations; PuppetInfo has its separate guard.
        std::atomic_flag sPoolLock = ATOMIC_FLAG_INIT;

        class PoolGuard {
        public:
            PoolGuard() {
                while (sPoolLock.test_and_set(std::memory_order_acquire))
                    nn::os::YieldThread();
            }

            ~PoolGuard() { sPoolLock.clear(std::memory_order_release); }

            PoolGuard(const PoolGuard&) = delete;
            PoolGuard& operator=(const PoolGuard&) = delete;
        };

        // Everything here is trivially initialised on purpose: this file has no
        // static constructors to run, since nothing guarantees .init_array is
        // executed for the mod.
        struct TypeEntry {
            // The sync key, i.e. what PuppetInfo::curHack carries.
            char key[kNameLen];
            // The archive to build from, i.e. the class the stage actually placed.
            char archive[kNameLen];
            char suffix[kNameLen];
            CaptureSync::Build build;
            // Borrowed straight from the static capture table - see
            // CaptureSync::Entry::variants. Never copied, because the table
            // is constexpr and outlives every stage.
            const char* const* variants;
            s32 variantCount;
        };

        TypeEntry sTypes[kMaxTypes];
        // Parallel to sTypes[] - whether at least one actual model has been
        // built for this type (set by update(), never by
        // notifyRealActorCreated() itself - see the crash history in the
        // top-of-file comment for why).
        bool sTypeBuilt[kMaxTypes];
        // Parallel to sTypes[] - set by notifyRealActorCreated() the moment a
        // real actor of this type is observed; update() drains this into an
        // actual build on the main thread. Deliberately two separate flags
        // (not one) so a type that's pending-but-not-yet-built can't be
        // re-marked pending by a second real actor of the same class.
        bool sTypeBuildPending[kMaxTypes];
        // A failed resource/model construction is terminal for this stage.
        // Without this memo an invalid archive would be retried synchronously
        // from every capture request after the initial queued attempt.
        bool sTypeBuildFailed[kMaxTypes];
        s32 sTypeCount = 0;

        PuppetHackActor* sActors[kMaxActors];
        bool sInUse[kMaxActors];
        s32 sActorCount = 0;

        // A copy, not a pointer: the ActorInitInfo handed to the stage-init hook is
        // a stack temporary there, while its members all point at things that live
        // as long as the scene. Same reasoning as PuppetActor::mInitInfo.
        //
        // Allocated once on gHeap and overwritten each stage, so repeated stage
        // transitions do not leak.
        al::ActorInitInfo* sInitInfo = nullptr;
        bool sIsStageReady = false;

        // Set only for the duration of the endInit drain - see
        // isEarlyBuildPhase() and installHooks() at the bottom of this file.
        bool sEarlyBuildPhase = false;

        char sMissingTypes[kMaxTypes][kNameLen];
        s32 sMissingTypesCount = 0;
        char sNotPlacedTypes[kMaxTypes][kNameLen];
        s32 sNotPlacedTypesCount = 0;
        char sNoSpareTypes[kMaxTypes][kNameLen];
        s32 sNoSpareTypesCount = 0;

        // Family members this stage turned out not to have. A capture request
        // the pool cannot serve is retried EVERY FRAME by PuppetActor (which is
        // correct - the pool may finish building later), so without this an
        // anagram stage that does not spell the letter a peer is carrying would
        // run two archive lookups and write a log line sixty times a second.
        // Stage-scoped, like every other memo here.
        constexpr s32 kMaxMissingVariants = 16;
        char sMissingVariants[kMaxMissingVariants][kNameLen];
        s32 sMissingVariantsCount = 0;

        bool isKnownMissingVariant(const char* archive) {
            for (s32 i = 0; i < sMissingVariantsCount; i++) {
                if (al::isEqualString(sMissingVariants[i], archive))
                    return true;
            }
            return false;
        }

        void copyName(char* dst, const char* src) {
            std::strncpy(dst, src, kNameLen - 1);
            dst[kNameLen - 1] = '\0';
        }

        s32 findTypeIndex(const char* key) {
            if (!key || key[0] == '\0')
                return -1;

            for (s32 i = 0; i < sTypeCount; i++) {
                if (al::isEqualString(sTypes[i].key, key))
                    return i;
            }
            return -1;
        }

        const TypeEntry* findType(const char* key) {
            const s32 index = findTypeIndex(key);
            return index >= 0 ? &sTypes[index] : nullptr;
        }

        // Builds one PuppetHackActor for `type` on the scene heap and adds it
        // to the pool as available (not leased, not alive). Only ever called
        // from drainPendingBuilds(), i.e. from one of exactly two places:
        // the al::LiveActorKit::endInit hook (the ordinary case, still inside
        // scene initialisation) or update() on the MAIN thread (the fallback,
        // during gameplay). Either way notifyRealActorCreated() has already
        // confirmed - from wherever it was actually called, see its own
        // comment on why that's NOT safe to build from directly - that a real
        // placed enemy of the same class has finished construction, which is
        // what guarantees the archive is actually resident. The model is put
        // to sleep immediately after construction - it is only ever woken up
        // later, on a different frame, via acquire() followed by the owning
        // PuppetActor's model-holder registration. The actor is deliberately
        // kept asleep until that registration has completed.
        // See PuppetCapturePool.h for the mechanisms that crashed trying to
        // do this differently.
        // Does ObjectData/<name>.szs actually exist?
        //
        // A capture type's KEY and its ARCHIVE are allowed to differ, and not
        // every archive name we are handed resolves - "FireBrosPossessed" and
        // "KuriboPossessed" are placed CLASS names with no .szs behind them.
        // Asking the game first is the only safe order: al::initActorWithArchive
        // Name does the lookup internally and faults on a missing archive
        // rather than returning, so a check afterwards can never run (crash
        // reports 01788189008 / 01788189328, al::Resource::tryGetByml +0x240 on
        // the scene-init thread, reached through buildOne -> PuppetHackActor::
        // init).
        //
        // Offset taken from MoonHooks::moonArchiveExists, which has used it in
        // four places for some time. Duplicated rather than shared only to keep
        // this fix off a file that is being actively worked on upstream; if a
        // third caller appears it should move to a shared header.
        bool archiveExists(const char* archiveName) {
            if (!archiveName || !archiveName[0])
                return false;

            using Fn = bool (*)(const sead::SafeString&);
            static constexpr uintptr_t cIsExistArchiveOffset = 0x00897ee0;
            al::StringTmp<128> resourcePath("ObjectData/%s", archiveName);
            Fn fn = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cIsExistArchiveOffset);
            return fn(resourcePath);
        }

        bool isArchiveResident(const char* archiveName) {
            if (!archiveName || !archiveName[0])
                return false;

            using Fn = void* (*)(const sead::SafeString&);
            static constexpr uintptr_t cFindResourceOffset = 0x009cb4e0;
            al::StringTmp<128> resourcePath("ObjectData/%s", archiveName);
            Fn fn = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cFindResourceOffset);
            return fn(resourcePath) != nullptr;
        }

        bool buildOneArchive(const TypeEntry& type, const char* archive);

        // `wantArchive` is a specific family member to build, or nullptr for
        // "whatever this type resolves to". A named archive that is not
        // resident is NOT an error and does not fail the type: the caller
        // (acquire's variant path) simply does not get its preferred piece and
        // falls back to the one the type already has.
        bool buildOne(const TypeEntry& type, const char* wantArchive = nullptr) {
            if (type.build == CaptureSync::Build::Unsupported) {
                MpDebugLog::write("[CapturePool] type %s is Unsupported - not built, body fallback", type.key);
                return false;
            }

            if (wantArchive && wantArchive[0] != '\0') {
                if (!archiveExists(wantArchive) || !isArchiveResident(wantArchive)) {
                    MpDebugLog::write(
                        "[CapturePool] %s: variant archive %s not resident - keeping default",
                        type.key, wantArchive);
                    return false;
                }
                if (sActorCount >= kMaxActors) {
                    MpDebugLog::write(
                        "[CapturePool] actor cap (%d) reached, %s variant %s falls back",
                        kMaxActors, type.key, wantArchive);
                    return false;
                }
                return buildOneArchive(type, wantArchive);
            }

            // Resolved before anything is allocated. A type that resolves to
            // nothing is terminal: returning false marks it failed so it is not
            // retried every frame, acquire() misses, and the puppet keeps its
            // own body - the same outcome as a type the stage never placed.
            const char* archive = type.archive;
            if (!archive || archive[0] == '\0') {
                MpDebugLog::write("[CapturePool] no archive for key=%s - not built", type.key);
                return false;
            }

            bool onDisc = archiveExists(archive);
            bool resident = onDisc && isArchiveResident(archive);

            // A capture whose wire name covers a FAMILY of archives - the
            // anagram letters, the Fukuwarai face pieces, the two manholes -
            // names one of them as its archive and lists the whole family,
            // because which are loaded depends on the stage (which word, which
            // face). Residency is the only honest way to choose the default,
            // and this is where it is already being asked.
            for (s32 alt = 0; !resident && alt < type.variantCount; alt++) {
                const char* candidate = type.variants[alt];
                if (!candidate || !candidate[0])
                    continue;
                if (archiveExists(candidate) && isArchiveResident(candidate)) {
                    MpDebugLog::write("[CapturePool] %s: archive %s not resident, using %s",
                        type.key, archive, candidate);
                    archive = candidate;
                    onDisc = true;
                    resident = true;
                }
            }

            if (!resident) {
                MpDebugLog::write("[CapturePool] %s: archive %s not built (onDisc=%d resident=%d)",
                    type.key, archive, onDisc ? 1 : 0, resident ? 1 : 0);
                return false; // -> sTypeBuildFailed, puppet keeps its body
            }

            // Logged on every build so a play session produces a full audit of
            // which archive each capture actually resolved to, without needing
            // the game's file list to hand.
            MpDebugLog::write("[CapturePool] resolve key=%s class=%s -> archive=%s%s", type.key,
                type.archive, archive,
                al::isEqualString(archive, type.archive) ? "" : " (REMAPPED)");

            if (sActorCount >= kMaxActors) {
                MpDebugLog::write("[CapturePool] actor cap (%d) reached, %s falls back to body",
                    kMaxActors, type.key);
                return false;
            }

            return buildOneArchive(type, archive);
        }

        // The allocation half, split out so a variant can reach it without
        // re-running the default archive resolution above. Both callers have
        // already proved `archive` resident and checked the actor cap.
        bool buildOneArchive(const TypeEntry& type, const char* archive) {
            PuppetHackActor* actor = nullptr;
            {
                sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());
                actor = new PuppetHackActor(archive);
            }

            if (!actor) {
                hk::diag::logLine("[CapturePool] ERROR: allocation failed for %s", type.archive);
                return false;
            }

            // No PuppetInfo yet - nobody has leased this. Mirrors the state
            // release() leaves an idle actor in.
            actor->initOnline(nullptr, type.key, archive, type.suffix[0] ? type.suffix : nullptr);
            if (type.build == CaptureSync::Build::YoshiModel) {
                actor->initYoshi(*sInitInfo);
            } else {
                actor->init(*sInitInfo);
            }
            if (!al::isDead(actor))
                actor->makeActorDead();

            // init() bails without building anything when the archive name has
            // no real .szs behind it - a type key and its archive are allowed
            // to differ, and not every name that reaches us resolves. Such an
            // actor is inert (init() stops before any executor registration),
            // so it is simply not pooled: acquire() then finds nothing for this
            // type and the puppet keeps its own body, which is the same outcome
            // as a type nobody has placed. Deliberately not deleted - it costs
            // a few bytes of a scene heap that is about to be torn down anyway,
            // and freeing a half-initialised LiveActor is the riskier move.
            if (!actor->hasModel()) {
                MpDebugLog::write("[CapturePool] %s (archive %s) has no model - not pooled, body fallback",
                    type.key, archive);
                return false;
            }

            sActors[sActorCount] = actor;
            sInUse[sActorCount] = false;
            sActorCount++;

            al::ModelKeeper* keeper = actor->getModelKeeper();
            MpDebugLog::write("[CapturePool] built %s (archive %s), %d/%d, actor=%p modelCtrl=%p", type.key,
                archive, sActorCount, kMaxActors, actor, keeper ? keeper->getModelCtrl() : nullptr);
            return true;
        }

    } // namespace

    namespace {

        void resetStageState() {
            sIsStageReady = false;

            // Diagnostic for the recurring ModelDrawerBase::addModelToTable draw-
            // pipeline crash (Data Abort, address 0 - see memory.txt): this is
            // the scene-transition boundary where every pooled actor's pointer is
            // dropped on the assumption the scene heap already tore them down (and,
            // with them, deregistered their models from the engine's draw-request
            // table - see ExecuteRequestKeeper.h). Logging what's being dropped and
            // when lets a future crash log's sequence-number gap be checked against
            // this boundary.
            if (sActorCount > 0) {
                MpDebugLog::write("[CapturePool] onStageKill dropping %d actors", sActorCount);
                for (s32 i = 0; i < sActorCount; i++)
                    MpDebugLog::write("[CapturePool]   dropped[%d]=%p", i, sActors[i]);
            }

            // Deliberately no deletes: these were allocated on the scene heap, which is
            // freed wholesale on the transition. Deleting here would be a double free.
            for (s32 i = 0; i < kMaxActors; i++) {
                sActors[i] = nullptr;
                sInUse[i] = false;
            }
            sActorCount = 0;
            sTypeCount = 0;
            for (s32 i = 0; i < kMaxTypes; i++) {
                sTypeBuilt[i] = false;
                sTypeBuildPending[i] = false;
                sTypeBuildFailed[i] = false;
            }

            sMissingTypesCount = 0;
            sNotPlacedTypesCount = 0;
            sNoSpareTypesCount = 0;
            sMissingVariantsCount = 0;
        }

    } // namespace

    void onStageInit(const al::ActorInitInfo& initInfo) {
        PoolGuard guard;

        // A previous stage's actors died with its scene heap; drop them before
        // anything can hand one out again.
        resetStageState();
        LateModelDrawer::resetTrackedBuffers();

        if (!sInitInfo) {
            sInitInfo = new (gHeap) al::ActorInitInfo();
            if (!sInitInfo) {
                hk::diag::logLine("[CapturePool] ERROR: failed to allocate ActorInitInfo");
                return;
            }
        }

        *sInitInfo = initInfo;

        // This is the same scene-level ActorInitInfo that the ordinary puppet
        // factory receives. Its placementInfo is not a stable per-object
        // placement and may already be dangling by the time acquire() lazily
        // constructs a PuppetHackActor. The Mario/cap path sanitises its own copy
        // in createPuppetActorFromFactory, but capture models are built later from
        // this independent cache and therefore need the same protection here.
        //
        // Keep a real empty PlacementInfo rather than nullptr: vanilla actor/model
        // initialisers include both guarded and unconditional placement reads.
        // Function-local static initialization does not depend on this module's
        // .init_array and the object remains valid for every stage thereafter.
        static al::PlacementInfo sEmptyPlacementInfo;
        sInitInfo->placementInfo = &sEmptyPlacementInfo;
        sIsStageReady = true;

        MpDebugLog::write("[CapturePool] onStageInit finished. actors: %d/%d (live/max), types: %d/%d (reg/max)",
            sActorCount, kMaxActors, sTypeCount, kMaxTypes);
    }

    void onStageKill() {
        // The ride table holds scene-heap actor pointers too.
        CaptureSync::resetRidePlacements();

        PoolGuard guard;
        resetStageState();
    }

    void onPlacementActorCreated(const char* className, const char* placementModelName, const char* actorModelName) {
        if (!cEnableCaptureSync)
            return;

        PoolGuard guard;

        if (!sIsStageReady || !sInitInfo || !className)
            return;

        // A map-parts capture, by either of the two things that identify one: the
        // registry key its placement uses, or a HackMove* model. Kept after the
        // PossessedMapParts fix because it is the check that found it - the class
        // name a placement carries is a factory registry key, not the C++ class,
        // and nothing else would have told us they differ.
        const bool isMapParts = al::isEqualString(className, "PossessedMapParts")
            || al::isEqualString(className, "HackMapParts")
            || (placementModelName && std::strncmp(placementModelName, "HackMove", 8) == 0)
            || (actorModelName && std::strncmp(actorModelName, "HackMove", 8) == 0);

        const char* resolveModelName = actorModelName ? actorModelName : placementModelName;

        // Every row this placement matches, not only the best one - one placed
        // class can prove the residency of two different captures that no
        // property of the placement distinguishes (see
        // CaptureSync::findByPlacementAll). Small and fixed: no class carries
        // more than two today.
        constexpr s32 kMaxRowsPerPlacement = 4;
        const CaptureSync::Entry* entries[kMaxRowsPerPlacement] = {};
        const s32 entryCount
            = CaptureSync::findByPlacementAll(className, resolveModelName, entries,
                kMaxRowsPerPlacement);
        const CaptureSync::Entry* entry = entryCount > 0 ? entries[0] : nullptr;

        // De-duplicated per model, not once per session. The one-shot version
        // reported Tree and then went quiet, so Boulder, Cactus and Manhole -
        // the three that would have confirmed they share the key - never showed.
        if (isMapParts) {
            static char sLoggedMapParts[8][kNameLen];
            static s32 sLoggedMapPartsCount = 0;
            const char* tag = resolveModelName ? resolveModelName : className;
            bool logged = false;
            for (s32 i = 0; i < sLoggedMapPartsCount; i++) {
                if (al::isEqualString(sLoggedMapParts[i], tag)) {
                    logged = true;
                    break;
                }
            }
            if (!logged && sLoggedMapPartsCount < 8) {
                copyName(sLoggedMapParts[sLoggedMapPartsCount++], tag);
                MpDebugLog::write(
                    "[CapturePool] mapparts placement class=%s placementModel=%s actorModel=%s -> row=%s",
                    className, placementModelName ? placementModelName : "(none)",
                    actorModelName ? actorModelName : "(none)", entry ? entry->hackName : "(none)");
            }
        }

        if (!entry)
            return;

        for (s32 e = 0; e < entryCount; e++) {
            const CaptureSync::Entry* row = entries[e];

            s32 i = findTypeIndex(row->hackName);
            if (i < 0) {
                if (sTypeCount >= kMaxTypes) {
                    MpDebugLog::write("[CapturePool] type table full, dropping %s", className);
                    return;
                }

                i = sTypeCount;
                copyName(sTypes[i].key, row->hackName);
                if (row->archive) {
                    copyName(sTypes[i].archive, row->archive);
                } else if (actorModelName) {
                    copyName(sTypes[i].archive, actorModelName);
                } else {
                    copyName(sTypes[i].archive, className);
                }
                sTypes[i].build = row->build;

                // Pointers into the static, constexpr capture table, so they
                // outlive every stage - buildOne() reads them on another
                // thread, later.
                sTypes[i].variants = row->variants;
                sTypes[i].variantCount = row->variants ? row->variantCount : 0;

                if (row->suffix) {
                    copyName(sTypes[i].suffix, row->suffix);
                } else {
                    sTypes[i].suffix[0] = '\0';
                }

                sTypeBuilt[i] = false;
                sTypeBuildPending[i] = false;
                sTypeBuildFailed[i] = false;
                sTypeCount++;

                if (row->build == CaptureSync::Build::Unsupported) {
                    MpDebugLog::write("[CapturePool] registered unsupported type key=%s class=%s model=%s archive=%s",
                        row->hackName, row->placementClass, resolveModelName ? resolveModelName : "(none)", sTypes[i].archive);
                } else {
                    MpDebugLog::write("[CapturePool] registered type key=%s archive=%s (%d/%d)", row->hackName, sTypes[i].archive,
                        sTypeCount, kMaxTypes);
                }
            }

            if (sTypeBuilt[i] || sTypeBuildPending[i] || sTypeBuildFailed[i])
                continue;

            // DO NOT build here. This function is called synchronously from
            // mpOnPlacementActorCreated, which fires from deep inside
            // al::initPlacementByStageInfo - confirmed via a real crash log
            // (see memory.txt) to run on a background "Scene Initialization
            // Thread", not the main thread, mid-placement-sweep. Calling
            // buildOne() here means reentrantly starting a SECOND, unrelated
            // resource load/actor construction from inside the engine's own
            // placement/resource-loading callback chain - it crashed inside
            // al::Resource::tryGetByml, not because the archive wasn't
            // resident (it was - that part of the theory was right), but
            // because doing this mid-callback on that thread isn't safe.
            // Just mark it pending; update() (called from Client::update(),
            // main thread, once a real gameplay frame is ticking - which by
            // definition is after this entire placement sweep has finished)
            // does the actual build.
            sTypeBuildPending[i] = true;
            MpDebugLog::write(
                "[CapturePool] real actor for %s (archive %s) confirmed resident - build queued for main thread",
                row->hackName, sTypes[i].archive);
        }
    }

    namespace {

        // Turns every type notifyRealActorCreated() marked pending into an
        // actual model. `early` says which of the two construction paths
        // PuppetHackActor::init should take; see isEarlyBuildPhase().
        void drainPendingBuilds(bool early) {
            if (!cEnableCaptureSync)
                return;

            PoolGuard guard;

            if (!sIsStageReady)
                return;

            sEarlyBuildPhase = early;

            for (s32 i = 0; i < sTypeCount; i++) {
                if (!sTypeBuildPending[i] || sTypeBuilt[i])
                    continue;

                sTypeBuildPending[i] = false;
                if (buildOne(sTypes[i]))
                    sTypeBuilt[i] = true;
                else
                    sTypeBuildFailed[i] = true;
            }

            sEarlyBuildPhase = false;
        }

        // al::LiveActorKit::endInit(). The whole point of hooking here is the
        // order of what it does, straight from IDA:
        //
        //     CollisionDirector::endInit / ClippingDirector::endInit
        //     AreaObjDirector::endInit / CameraDirector::endInit
        //     ShadowDirector::endInit
        //     GraphicsSystemInfo::endInit
        //     <every placed actor>::initAfterPlacement
        //     GraphicsSystemInfo::initAfterPlacement   <- sizes depth shadow
        //     ExecuteDirector::createExecutorListTable <- sizes EVERY table
        //
        // The placement sweep is finished before this runs, and the tables
        // the engine's per-frame movement/draw passes use are created at the
        // very end of it. A model built on entry to this function is therefore
        // an ORDINARY model: its initExecutorUpdate/initExecutorDraw
        // registrations are counted by createExecutorListTable like any placed
        // actor's, so there is no table to grow, no drawer left without a
        // mModels array, and no depth-shadow entry sitting past the director's
        // counters. Every problem LateModelDrawer exists to repair is a
        // consequence of building AFTER this line, not of building at all.
        //
        // This is also why the build cannot simply move into
        // notifyRealActorCreated(): that fires from inside the placement
        // sweep's own resource-loading callback chain (crash #4 in the header).
        // Here the sweep has completed and nothing of ours is reentrant.
        constexpr uintptr_t cLiveActorKitEndInitOffset = 0x00910450;

        static HkTrampoline<void, void*> liveActorKitEndInitHook
            = hk::hook::trampoline([](void* liveActorKit) -> void {
                  drainPendingBuilds(true);
                  liveActorKitEndInitHook.orig(liveActorKit);
              });

    } // namespace

    void update() {
        // Fallback path only, now that endInit does the ordinary case: this
        // catches a type whose real actor first appears after scene init (a
        // sub-area load). Those builds are still late and still lean on
        // LateModelDrawer.
        drainPendingBuilds(false);
    }

    bool isEarlyBuildPhase() {
        return sEarlyBuildPhase;
    }

    void installHooks() {
        liveActorKitEndInitHook.installAtMainOffset(cLiveActorKitEndInitOffset);
    }

    PuppetHackActor* acquire(const char* hackType, s32 variant, PuppetInfo* info) {
        if (!cEnableCaptureSync)
            return nullptr;

        PoolGuard guard;

        if (!sIsStageReady || !sInitInfo || !hackType || hackType[0] == '\0')
            return nullptr;

        const s32 typeIndex = findTypeIndex(hackType);
        if (typeIndex < 0) {
            if (!CaptureSync::findByHackName(hackType)) {
                // De-duplicate "no table row" misses per name across the stage lifetime
                bool logged = false;
                for (int i = 0; i < sMissingTypesCount; i++) {
                    if (al::isEqualString(sMissingTypes[i], hackType)) {
                        logged = true;
                        break;
                    }
                }
                if (!logged && sMissingTypesCount < kMaxTypes) {
                    copyName(sMissingTypes[sMissingTypesCount++], hackType);
                    MpDebugLog::write("[CapturePool] no table row for hackName=%s", hackType);
                }
            } else {
                bool logged = false;
                for (int i = 0; i < sNotPlacedTypesCount; i++) {
                    if (al::isEqualString(sNotPlacedTypes[i], hackType)) {
                        logged = true;
                        break;
                    }
                }
                if (!logged && sNotPlacedTypesCount < kMaxTypes) {
                    copyName(sNotPlacedTypes[sNotPlacedTypesCount++], hackType);
                    MpDebugLog::write("[CapturePool] acquire miss for hackType=%s (not placed)", hackType);
                }
            }
            return nullptr;
        }

        // The exact family member the sender is riding, when it named one and
        // this stage has it. Null means "no preference": either an ordinary
        // one-archive capture, a peer that does not send variants, or a variant
        // whose archive this stage never loaded - all three want today's
        // behaviour, which is whatever model the type already has.
        const char* wantArchive = nullptr;
        if (variant >= 0) {
            const CaptureSync::Entry* entry = CaptureSync::findByHackName(hackType);
            wantArchive = CaptureSync::getVariantArchive(entry, variant);
        }

        auto tryLease = [&](const char* archive) -> PuppetHackActor* {
            for (s32 i = 0; i < sActorCount; i++) {
                if (sInUse[i] || !sActors[i] || !sActors[i]->isHackType(hackType))
                    continue;
                if (archive && !sActors[i]->isArchive(archive))
                    continue;

                sInUse[i] = true;
                sActors[i]->setInfo(info);
                // Do not manually edit executor slots here. The archive-native
                // initializer selected the capture's own update/draw lists;
                // overwriting a shared slot would hide another puppet.
                // prepareForLateRegistration() made the tables large enough,
                // so LiveActor's normal makeActorAlive/makeActorDead
                // registration is the safe path.
                // Leave the actor asleep until PuppetActor has registered it
                // in its model holder. This keeps the shared movement/draw
                // executors from observing a capture during registerModel().
                al::ModelKeeper* keeper = sActors[i]->getModelKeeper();
                MpDebugLog::write(
                    "[CapturePool] leased slot=%d actor=%p modelCtrl=%p hackType=%s archive=%s variant=%d (sleeping)",
                    i, sActors[i], keeper ? keeper->getModelCtrl() : nullptr, hackType,
                    sActors[i]->getArchiveName(), variant);

                return sActors[i];
            }
            return nullptr;
        };

        // The requested family member first, if one was asked for.
        if (wantArchive) {
            PuppetHackActor* exact = tryLease(wantArchive);
            if (exact)
                return exact;

            // None built yet, or all of them are already out on loan. Build
            // this one - the type has already proved itself eligible, and a
            // family member is exactly as safe to construct here as the extra
            // copy the shared-capture path below builds. A refusal (archive not
            // resident this stage, or the ceiling reached) is not a failure of
            // the TYPE, so sTypeBuildFailed is deliberately not set: the puppet
            // just drops through to the default model.
            if (sTypeBuilt[typeIndex] && !sTypeBuildFailed[typeIndex]
                && !isKnownMissingVariant(wantArchive)) {
                if (buildOne(sTypes[typeIndex], wantArchive)) {
                    PuppetHackActor* built = tryLease(wantArchive);
                    if (built)
                        return built;
                } else if (!isArchiveResident(wantArchive)
                    && sMissingVariantsCount < kMaxMissingVariants) {
                    // Only a residency miss is permanent for this stage. A
                    // refusal because the actor ceiling is full is not - a
                    // released model frees a slot - so that one stays
                    // retryable.
                    copyName(sMissingVariants[sMissingVariantsCount++], wantArchive);
                }
            }
        }

        PuppetHackActor* leased = tryLease(nullptr);
        if (leased)
            return leased;

        // A stage can have more than one remote player riding the same
        // capture. One shared actor cannot be drawn at two transforms, so
        // grow this type on demand up to the global pool ceiling. Construction
        // is still on the main thread (acquire is called from PuppetHolder's
        // main-thread update), and it happens only after the first real actor
        // made the archive eligible.
        if (sTypeBuilt[typeIndex] && !sTypeBuildFailed[typeIndex]
            && sActorCount < kMaxActors) {
            if (buildOne(sTypes[typeIndex])) {
                PuppetHackActor* extra = tryLease(nullptr);
                if (extra)
                    return extra;
            } else {
                sTypeBuildFailed[typeIndex] = true;
            }
        }

        // Nothing spare - either the real enemy of this type hasn't been
        // seen yet this stage (so notifyRealActorCreated() never fired for
        // it), the model could not be built, or the global actor ceiling was
        // reached. The caller falls back to showing the puppet's own body.
        bool logged = false;
        for (int i = 0; i < sNoSpareTypesCount; i++) {
            if (al::isEqualString(sNoSpareTypes[i], hackType)) {
                logged = true;
                break;
            }
        }
        if (!logged && sNoSpareTypesCount < kMaxTypes) {
            copyName(sNoSpareTypes[sNoSpareTypesCount++], hackType);
            MpDebugLog::write("[CapturePool] acquire miss for hackType=%s (no built+free model)", hackType);
        }
        return nullptr;
    }

    void release(PuppetHackActor* actor) {
        if (!actor)
            return;

        PoolGuard guard;

        for (s32 i = 0; i < sActorCount; i++) {
            if (sActors[i] != actor)
                continue;

            sInUse[i] = false;
            // Kept alive as an allocation, put to sleep as an actor: the next
            // puppet to ride this capture re-leases it instead of paying for a
            // second model.
            actor->setInfo(nullptr);
            actor->makeActorDead();
            MpDebugLog::write("[CapturePool] released slot=%d actor=%p", i, actor);
            return;
        }
    }

    s32 getLiveCount() {
        PoolGuard guard;
        return sActorCount;
    }

    s32 getLeasedCount() {
        PoolGuard guard;
        s32 count = 0;
        for (s32 i = 0; i < sActorCount; i++) {
            if (sInUse[i])
                count++;
        }
        return count;
    }

    s32 getTypeCount() {
        PoolGuard guard;
        return sTypeCount;
    }

} // namespace PuppetCapturePool
