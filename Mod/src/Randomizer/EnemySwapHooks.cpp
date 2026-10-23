#include "Randomizer/EnemySwapHooks.h"

#include "Multiplayer/main.hpp"
#include "Randomizer/CoinRando.h"
#include "Randomizer/EnemySwapDebug.h"
#include "Randomizer/EnemySwapRando.h"
#include "Randomizer/Randomizer.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/Collision/PartsConnectorUtil.h"
#include "al/Library/Draw/GraphicsSystemInfo.h"
#include "al/Library/Event/EventFlowUtil.h"
#include "al/Library/Joint/JointControllerKeeper.h"
#include "al/Library/Joint/JointSpringControllerHolder.h"
#include "al/Library/KeyPose/KeyPoseKeeper.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/LiveActor/LiveActorInfo.h"
#include "al/Library/LiveActor/LiveActorKeeper.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Placement/PlacementFunction.h"
#include "al/Library/Placement/PlacementInfo.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Project/Draw/GpuMemAllocator.h"
#include "al/Project/Joint/KeyPose.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Scene/StageScene.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameSystem.h"
#include "game/Util/PlayerHackFunction.h"
#include "game/Util/PlayerUtil.h"
#include "game/Util/SensorMsgFunction.h"
#include "hk/hook/Trampoline.h"
#include "hk/svc/api.h"
#include "hk/svc/cpu.h"
#include "sead/heap/seadHeapMgr.h"
#include "sead/prim/seadDelegate.h"

#include <cstring>

// No header exists for these internal, undocumented classes; used only as
// opaque typed pointers for the hooks below (see their comments).
class DemoStateHackFirst;
class IUsePlayerHack;
class IUsePlayerPuppet;
class PeachOnKoopaShoulder;
class PeachOnKoopaAnimInfo;

// Enemy/Capture Swap runtime: intercepts placed-object creation and spawns a
// replacement entity on the original placement.
//
// How: al::createPlacementActorFromFactory builds an ActorInitInfo whose
// placementInfo points at the placement being created, resolves the actor
// class via al::getClassName(initInfo) and, inside the actor's init, the
// resource archive via al::tryGetObjectName(initInfo). All of those readers
// pull the names straight out of the placement BYML (read-only), so the swap
// works by scoping the creation call and answering the ActorInitInfo-based
// name queries with the replacement's names while the scope is active. Links
// and transforms stay vanilla; target-specific initialization arguments are
// supplied only by narrowly scoped compatibility hooks. Every eligible
// replacement uses the full target actor identity; NPC source dialogue data
// is neutralized separately.
namespace EnemySwapHooks {

    namespace DB = Randomizer::EnemySwapDB;

    // OnlyBalloon flows show their text on proximity and never need the player
    // to interact with the resident. They are safe cosmetic chaos candidates;
    // every other authored event can drive a quest, reward, or cutscene and is
    // kept vanilla.
    static bool requiresNpcInteraction(const al::PlacementInfo& placementInfo) {
        const char* eventName = nullptr;
        if (!placementInfo.getPlacementIter().tryGetStringByKey(&eventName, "EventName"))
            return false;
        return eventName && std::strcmp(eventName, "None") != 0
            && std::strstr(eventName, "OnlyBalloon") == nullptr;
    }

    struct SwapScope {
        const al::PlacementInfo* placement = nullptr;
        const char* sourceObjectId = nullptr;
        const char* sourceUnitConfigName = nullptr;
        const char* objectName = nullptr;
        const char* displayName = nullptr;
        const char* className = nullptr;
        const char* modelName = nullptr;
        bool replaceActorIdentity = false;
    };
    static SwapScope sScope;
    // Bounded so a stage full of swaps cannot flood the log with one line per
    // early player-state query; the first few are enough to identify the
    // pattern.
    static s32 sPlayerStateInitGuardCount = 0;

    // Keep the first few runtime decisions in the normal debug log. This is
    // deliberately bounded: a stage can create thousands of placement
    // actors, and diagnostics must not become another source of heap
    // pressure. The trace makes the three failure points distinguishable:
    // lookup, memory admission, and identity scoping.
    static u32 sFactoryTraceCount = 0;
    static u32 sEnemyFactoryTraceCount = 0;
    static u32 sIdentityTraceCount = 0;
    static u32 sReplacementResultTraceCount = 0;
    static u32 sKoopaCaptureTraceCount = 0;
    // Bounded for a different reason than the counters above: the empty-slot
    // guard below lives in an animation getter every actor reaches, and what
    // is worth knowing is which actor first arrived there with nothing
    // playing, not how many frames it stayed that way.
    static u32 sEmptySklAnimSlotCount = 0;
    static char sTraceStage[128] = {};

    static void resetTraceCountersForStage(const char* stage) {
        const char* key = stage ? stage : "";
        if (std::strcmp(sTraceStage, key) == 0)
            return;
        std::strncpy(sTraceStage, key, sizeof(sTraceStage) - 1);
        sTraceStage[sizeof(sTraceStage) - 1] = '\0';
        sFactoryTraceCount = 0;
        sEnemyFactoryTraceCount = 0;
        sIdentityTraceCount = 0;
        sReplacementResultTraceCount = 0;
        sKoopaCaptureTraceCount = 0;
        sEmptySklAnimSlotCount = 0;
    }

    static void traceScopedIdentity(const char* query) {
        if (sIdentityTraceCount >= 64)
            return;
        Randomizer::logPush(
            "enemy swap identity hook: query=%s object=%s display=%s "
            "class=%s model=%s",
            query ? query : "?",
            sScope.objectName ? sScope.objectName : "?",
            sScope.displayName ? sScope.displayName : "?",
            sScope.className ? sScope.className : "?",
            sScope.modelName ? sScope.modelName : "?");
        sIdentityTraceCount++;
    }

    static void enableForeignDonsukePlayerReaction(
        al::LiveActor* actor, const DB::Entity* replacement) {
        if (!actor || !replacement || !replacement->className
            || std::strcmp(replacement->className, "Donsuke") != 0)
            return;

        // Donsuke normally receives MoveArea/SearchArea links from its native
        // placement. Foreign placements have neither. In 1.3.0 the byte at
        // +0x132 is the actor's explicit "track player without SearchArea"
        // flag (read by Donsuke::tryCalcPlayerPos). Enabling it after init lets
        // the Ogre notice and attack Mario without fabricating an AreaObj.
        reinterpret_cast<u8*>(actor)[0x132] = 1;
    }

    // A Koopa replacement has no usable per-body-part depth-shadow map
    // catalogue.  Keep the actor identity after its synchronous creation
    // scope ends, because Koopa's HackerDepthShadowMapCtrl is updated later
    // while the player is capturing it.
    static al::LiveActor* sKoopaWithoutDepthShadowMap = nullptr;

    // These guards intentionally survive ordinary actor creation, but must be
    // cleared when the scene is initialized again.  A same-stage reload does
    // not change the current-stage name, so the old function-local state would
    // otherwise make the first replacement in the reloaded scene look like a
    // duplicate spawn.
    static char sLastKoopaStage[128] = {};
    static bool sKoopaSpawnedThisStage = false;
    static char sLastFukankunStage[128] = {};
    static bool sFukankunSpawnedThisStage = false;
    struct KoopaReplacementRecord {
        al::LiveActor* actor = nullptr;
        sead::Vector3f spawnTrans = {};
        bool captureActive = false;
        bool respawnPending = false;
        bool deathApplied = false;
        u8 respawnDelayFrames = 0;
    };

    // DemoStateHackFirst is long-lived and its normal implementation only
    // exposes the actor through an internal parent pointer. Track the Koopa
    // actors that were actually created by this swap hook so the special
    // first-capture workaround does not affect native Koopa placements.
    static KoopaReplacementRecord sKoopaReplacementActors[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1] = {};
    static s32 sKoopaReplacementActorCount = 0;

    static KoopaReplacementRecord* findKoopaReplacementRecord(
        const al::LiveActor* actor) {
        if (!actor)
            return nullptr;
        for (s32 i = 0; i < sKoopaReplacementActorCount; i++)
            if (sKoopaReplacementActors[i].actor == actor)
                return &sKoopaReplacementActors[i];
        return nullptr;
    }

    static void rememberKoopaReplacementActor(al::LiveActor* actor) {
        if (!actor)
            return;
        if (findKoopaReplacementRecord(actor))
            return;
        if (sKoopaReplacementActorCount >= DB::sPlacementCount)
            return;
        KoopaReplacementRecord& record = sKoopaReplacementActors[sKoopaReplacementActorCount++];
        record.actor = actor;
        // The actor pose is already in the stage's world space after zone
        // transforms and initAfterPlacement have run. It is therefore a more
        // reliable respawn point than reading the authored PlacementInfo,
        // which may still be expressed in a zone-local coordinate system.
        record.spawnTrans = al::getTrans(actor);
        Randomizer::logPush(
            "enemy swap Koopa replacement tracked: actor=%p x=%.1f y=%.1f "
            "z=%.1f count=%d",
            actor, record.spawnTrans.x, record.spawnTrans.y,
            record.spawnTrans.z, sKoopaReplacementActorCount);
    }

    static bool isKoopaReplacementActor(const al::LiveActor* actor) {
        return findKoopaReplacementRecord(actor) != nullptr;
    }

    static bool isScoped(const al::PlacementInfo& placementInfo);

    static bool isScoped(const al::ActorInitInfo& initInfo) {
        return initInfo.placementInfo
            && (initInfo.placementInfo == sScope.placement
                || isScoped(*initInfo.placementInfo));
    }

    static bool isScoped(const al::PlacementInfo& placementInfo) {
        if (!sScope.placement)
            return false;
        if (sScope.placement == &placementInfo)
            return true;
        if (!sScope.sourceObjectId || !sScope.sourceUnitConfigName)
            return false;
        const char* objectId = nullptr;
        const char* unitConfigName = nullptr;
        if (!placementInfo.getPlacementIter().tryGetStringByKey(
                &objectId, "Id")
            || !placementInfo.getPlacementIter().tryGetStringByKey(
                &unitConfigName, "UnitConfigName"))
            return false;
        return objectId && unitConfigName
            && std::strcmp(objectId, sScope.sourceObjectId) == 0
            && std::strcmp(unitConfigName, sScope.sourceUnitConfigName) == 0;
    }

    static bool replacesActorIdentity(const al::ActorInitInfo& initInfo) {
        return isScoped(initInfo) && sScope.replaceActorIdentity;
    }

    static bool replacesActorIdentity(const al::PlacementInfo& placementInfo) {
        return isScoped(placementInfo) && sScope.replaceActorIdentity;
    }

    static bool isScopedReplacement(const al::ActorInitInfo& initInfo, const char* name) {
        return replacesActorIdentity(initInfo) && name && sScope.objectName
            && std::strcmp(sScope.objectName, name) == 0;
    }

    static const char* getReplacementBrosRuntimeName(
        const Randomizer::EnemySwapDB::Entity* replacement) {
        if (replacement && replacement->name
            && std::strcmp(replacement->name, "FireBrosPossessed") == 0)
            return "FireBros";
        if (replacement && replacement->name
            && std::strcmp(replacement->name, "HammerBrosPossessed") == 0)
            return "HammerBros";
        return replacement ? replacement->name : nullptr;
    }

    static const char* getReplacementClassName(
        const Randomizer::EnemySwapDB::Entity* replacement) {
        if (replacement && replacement->name
            && (std::strcmp(replacement->name, "FireBrosPossessed") == 0
                || std::strcmp(replacement->name, "HammerBrosPossessed") == 0))
            return getReplacementBrosRuntimeName(replacement);
        return replacement ? replacement->className : nullptr;
    }

    // Koopa is instantiated from ObjectData/Koopa, but its native capture
    // placement explicitly selects the KoopaHack model.  That model owns the
    // Hack-suffix init data, including InitCollider.  Treating its object and
    // model names as interchangeable left a swapped Bowser without a
    // collider, and Koopa::updateCollider then crashed in isCollidedGround.
    static const char* getReplacementModelName(
        const Randomizer::EnemySwapDB::Entity* replacement) {
        if (replacement && replacement->name
            && std::strcmp(replacement->name, "Koopa") == 0)
            return "KoopaHack";
        return getReplacementBrosRuntimeName(replacement);
    }

    // Fire/Hammer Bros are named *Possessed in authored UnitConfigName
    // fields, but their factory-facing object/resource identity is the
    // archive name without that suffix. Their concrete init functions use
    // that same archive name in initActorWithArchiveName(), so keep all
    // object-name readers consistent with the class/model identity while a
    // replacement is being constructed.
    static const char* getReplacementObjectName(
        const Randomizer::EnemySwapDB::Entity* replacement) {
        return getReplacementBrosRuntimeName(replacement);
    }

    // ActorFactory passes DisplayName to the concrete actor constructor. The
    // *Possessed UnitConfigName is an authored placement key, not the runtime
    // Bros actor identity, so use the canonical archive name here as well.
    static const char* getReplacementDisplayName(
        const Randomizer::EnemySwapDB::Entity* replacement) {
        return getReplacementBrosRuntimeName(replacement);
    }

    static bool isScopedNpcReplacement(const al::ActorInitInfo& initInfo) {
        return isScoped(initInfo) && sScope.className
            && std::strcmp(sScope.className, "TalkNpc") == 0;
    }

    // Yukimaru::init() calls rs::initEventFlowFromPlacementInfo(this, info,
    // "TalkNpc") the same way a real TalkNpc does, then unconditionally
    // starts the "Init" flow action. On a foreign placement the source
    // placement's own EventName (Args stay vanilla) points at some other
    // actor's flow chart, which has no "Init" entry -- EventFlowChart::
    // findEntryNode then dereferences a nonexistent node and crashes at
    // scene init. Same failure mode and same fix as the NPC EventName case
    // below, just for a non-TalkNpc class that is event-flow-driven anyway.
    static bool isScopedYukimaruReplacement(const al::ActorInitInfo& initInfo) {
        return isScoped(initInfo) && sScope.className
            && std::strcmp(sScope.className, "Yukimaru") == 0;
    }

    static bool isScopedFireBrosReplacement(
        const al::ActorInitInfo& initInfo) {
        return isScopedReplacement(initInfo, "FireBros");
    }

    static bool isScopedKoopaReplacement(const al::ActorInitInfo& initInfo) {
        return isScoped(initInfo) && sScope.className
            && std::strcmp(sScope.className, "Koopa") == 0;
    }

    static bool isScopedWanwanReplacement(
        const al::ActorInitInfo& initInfo) {
        return isScoped(initInfo) && sScope.className
            && std::strcmp(sScope.className, "Wanwan") == 0;
    }

    // The three placement keys rs::getItemType resolves into one shared item
    // enum. Which of those values an actor can honor is not shared: every
    // actor writes its own switch over the enum, and most cover only the
    // handful of items their native placements ever author.
    static bool isItemTypeArg(const char* key) {
        return std::strcmp(key, "ItemType") == 0
            || std::strcmp(key, "ItemTypeNoShine") == 0
            || std::strcmp(key, "ItemType2D3D") == 0;
    }

    // No ActorInitInfo overload: al::validateDepthShadowMap/
    // invalidateDepthShadowMap below take only (LiveActor*, char const*), so
    // they can't be checked with isScoped(initInfo) like the hooks above.
    // They only need to be correct while Koopa::init() is synchronously
    // running inside the scoped createPlacementActorFromFactory call, so
    // checking sScope directly (without an initInfo to match against) is
    // sufficient here.
    static bool isKoopaReplacementActive() {
        return sScope.placement && sScope.className
            && std::strcmp(sScope.className, "Koopa") == 0;
    }

    // al::CameraDirector::createObjectCamera resolves a camera ticket from the
    // PLACEMENT's own PlacementId, which a swap does not rewrite. On a foreign
    // placement no ticket exists, tryFindParamResource fails, and a null class
    // name is handed to createCameraFromFactory, which null-derefs in
    // al::isEqualString. Only active while a replacement is being constructed.
    static bool isCameraTicketReplacementActive() {
        return sScope.placement && sScope.replaceActorIdentity;
    }

    // HomeChair's BindInit path calls rs::forcePutOnPuppetCapWithEffect.  If
    // the cap was visible when the bind started, its hit-reaction walks the
    // cap model's sub-actors with the unchecked indexed getSubActor overload.
    // A model assembled from independently randomized outfit/cap state can
    // legitimately have no keeper (or a sparse keeper), and the native four-
    // instruction getter then dereferences null.  Keep the guard scoped to
    // this cap-return operation: valid keepers retain the exact native path;
    // a missing entry uses the root cap actor as the visual reaction target,
    // allowing the cap return and chair bind to complete without dropping
    // either feature.
    static s32 sForcePutOnPuppetCapDepth = 0;
    static u32 sForcePutOnPuppetCapFallbackCount = 0;

    HkTrampoline<void, IUsePlayerPuppet*> forcePutOnPuppetCapWithEffectHook = hk::hook::trampoline([](IUsePlayerPuppet* puppet) -> void {
        sForcePutOnPuppetCapDepth++;
        forcePutOnPuppetCapWithEffectHook.orig(puppet);
        sForcePutOnPuppetCapDepth--;
    });

    HkTrampoline<al::LiveActor*, const al::LiveActor*, s32>
        getSubActorByIndexHook = hk::hook::trampoline(
            [](const al::LiveActor* actor, s32 index) -> al::LiveActor* {
                if (sForcePutOnPuppetCapDepth > 0 && actor) {
                    al::SubActorKeeper* keeper = actor->getSubActorKeeper();
                    const bool missingEntry = !keeper || index < 0
                        || index >= keeper->getCurActorCount()
                        || !keeper->mBuffer || !keeper->getActorInfo(index)
                        || !keeper->getActorInfo(index)->subActor;
                    if (missingEntry) {
                        if (sForcePutOnPuppetCapFallbackCount < 8) {
                            Randomizer::logPush(
                                "cap return missing sub-actor: actor=%p name=%s "
                                "index=%d count=%d; using root",
                                actor, actor->mName ? actor->mName : "?", index,
                                keeper ? keeper->getCurActorCount() : 0);
                            sForcePutOnPuppetCapFallbackCount++;
                        }
                        return const_cast<al::LiveActor*>(actor);
                    }
                }
                return getSubActorByIndexHook.orig(actor, index);
            });

    // Some prop classes assume their authored placement selected the same
    // concrete pose keeper as their model. Full-actor swapping deliberately
    // breaks that assumption: FrailBox writes a quaternion during init, and
    // SignBoardDanger can write Euler Y and a quaternion later while wobbling.
    // The native setters dereference the requested direct-storage pointer
    // without checking it. Every pose keeper does, however, implement the
    // representation-neutral update virtuals. Use those only when direct
    // storage is unavailable; native actors keep the original fast path.
    HkTrampoline<void, al::LiveActor*, const sead::Quatf&> setQuatHook = hk::hook::trampoline(
        [](al::LiveActor* actor, const sead::Quatf& quat) -> void {
            if (actor && !al::tryGetQuatPtr(actor)) {
                al::updatePoseQuat(actor, quat);
                return;
            }
            setQuatHook.orig(actor, quat);
        });

    HkTrampoline<void, al::LiveActor*, f32> setRotateYHook = hk::hook::trampoline([](al::LiveActor* actor, f32 y) -> void {
        if (actor && !al::getRotatePtr(actor)) {
            sead::Vector3f rotate = al::getRotate(actor);
            rotate.y = y;
            al::updatePoseRotate(actor, rotate);
            return;
        }
        setRotateYHook.orig(actor, y);
    });

    HkTrampoline<void*, void*, const char*, const void*, const char*, int, const void*>
        createCameraFromFactoryHook = hk::hook::trampoline(
            [](void* director, const char* className, const void* placementId,
                const char* cameraName, int type, const void* mtx) -> void* {
                if (!className && isCameraTicketReplacementActive())
                    className = "";
                return createCameraFromFactoryHook.orig(
                    director, className, placementId, cameraName, type, mtx);
            });

    // Squirrel projects 150 units toward gravity during initAfterPlacement
    // and assumes the ray always finds collision. Full-actor swaps can place
    // one over open space or farther above the floor. On a miss, Triangle's
    // CollisionParts stays null; attachMtxConnectorToCollisionParts then
    // derives 0x58 as a matrix pointer and MtxConnector::clear faults while
    // dereferencing it. Keep the failed connector detached and restore the
    // authored placement after the native routine returns. Its base pose is
    // still valid, so the squirrel remains present and can attach normally
    // if its runaway state later reaches real ground.
    static al::LiveActor* sSquirrelInitAfterPlacementActor = nullptr;
    static al::MtxConnector* sSquirrelMissedGroundConnector = nullptr;
    static u32 sSquirrelMissedGroundCount = 0;
    // These game-private helpers are present in the main executable but are
    // intentionally absent from the linked symbol set. Keep the calls
    // offset-based, matching MoonHooks' resetPosition wrapper. The offsets
    // come from ActorMovementFunction and PartsConnectorUtil respectively.
    static constexpr uintptr_t kResetPositionOffset = 0x008e28b8;
    static constexpr uintptr_t kSetConnectorBaseQuatTransOffset = 0x00857fd4;

    static void restoreSquirrelPlacementPose(
        al::LiveActor* actor, const sead::Vector3f& trans,
        al::MtxConnector* connector) {
        using ResetPositionFn = void (*)(al::LiveActor*, const sead::Vector3f&);
        using SetConnectorBaseFn = void (*)(
            const sead::Quatf&, const sead::Vector3f&, al::MtxConnector*);
        const uintptr_t main = hk::ro::getMainModule()->range().start();
        reinterpret_cast<ResetPositionFn>(
            main + kResetPositionOffset)(actor, trans);
        reinterpret_cast<SetConnectorBaseFn>(
            main + kSetConnectorBaseQuatTransOffset)(
            al::getQuat(actor), trans, connector);
    }

    HkTrampoline<void, al::MtxConnector*, const al::CollisionParts*>
        attachMtxConnectorToCollisionPartsHook = hk::hook::trampoline(
            [](al::MtxConnector* connector,
                const al::CollisionParts* collisionParts) -> void {
                if (sSquirrelInitAfterPlacementActor && !collisionParts) {
                    sSquirrelMissedGroundConnector = connector;
                    return;
                }
                attachMtxConnectorToCollisionPartsHook.orig(
                    connector, collisionParts);
            });

    HkTrampoline<void, al::LiveActor*> squirrelInitAfterPlacementHook = hk::hook::trampoline([](al::LiveActor* actor) -> void {
        const sead::Vector3f authoredTrans = al::getTrans(actor);
        al::LiveActor* previousActor = sSquirrelInitAfterPlacementActor;
        al::MtxConnector* previousMissedConnector = sSquirrelMissedGroundConnector;
        sSquirrelInitAfterPlacementActor = actor;
        sSquirrelMissedGroundConnector = nullptr;

        squirrelInitAfterPlacementHook.orig(actor);

        al::MtxConnector* missedConnector = sSquirrelMissedGroundConnector;
        if (missedConnector) {
            restoreSquirrelPlacementPose(
                actor, authoredTrans, missedConnector);
            if (sSquirrelMissedGroundCount < 16) {
                Randomizer::logPush(
                    "enemy swap Squirrel has no ground in attach range: "
                    "actor=%p x=%.1f y=%.1f z=%.1f; keeping placement",
                    actor, authoredTrans.x, authoredTrans.y,
                    authoredTrans.z);
                sSquirrelMissedGroundCount++;
            }
        }

        sSquirrelInitAfterPlacementActor = previousActor;
        sSquirrelMissedGroundConnector = previousMissedConnector;
    });

    static const char* getCurrentStageName() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return nullptr;
        HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
        if (!seq->mGameDataHolderAccessor)
            return nullptr;
        return GameDataFunction::getCurrentStageName(seq->mGameDataHolderAccessor);
    }

    static s16 getCurrentScenario() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return -1;
        HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
        GameDataHolderAccessor accessor = seq->mGameDataHolderAccessor;
        if (!accessor.mData || !accessor.mData->getGameDataFile())
            return -1;
        // The free GameDataFunction wrapper is declared in the headers but is
        // not exported by the supported game build. This field is the value
        // returned by that wrapper and is public in the target ABI.
        return accessor.mData->getGameDataFile()->mScenarioNoPlacement;
    }

    // Stage key for the swap DB: the zone stage name for zone-placed objects
    // (the map generator records instances under their own stage file), the
    // current stage otherwise.
    static const char* resolvePlacementStage(const al::PlacementInfo& placementInfo) {
        // PlacementFileName is attached to the object record and is the
        // authoritative physical placement file. Prefer it over the zone
        // iterator: the latter may describe the containing logical stage and
        // is not sufficient when embedded zones reuse an object ID.
        const char* placementFileName = nullptr;
        if (placementInfo.getPlacementIter().tryGetStringByKey(
                &placementFileName, "PlacementFileName")
            && placementFileName && placementFileName[0])
            return placementFileName;
        // A few embedded-zone records carry PlacementFileName on the zone
        // iterator instead of the object iterator. Read both forms before
        // falling back to the zone UnitConfigName/current stage. Without
        // this, the actor is created normally but never reaches the generated
        // stage/object lookup and every inspector override appears inert.
        if (placementInfo.getZoneIter().tryGetStringByKey(
                &placementFileName, "PlacementFileName")
            && placementFileName && placementFileName[0])
            return placementFileName;
        const char* zoneName = nullptr;
        if (placementInfo.getZoneIter().tryGetStringByKey(&zoneName, "UnitConfigName")
            && zoneName && zoneName[0])
            return zoneName;
        return getCurrentStageName();
    }

    // The assignment solver permits one Chaos-assigned Koopa per logical
    // stage. Keep this runtime guard for inspector overrides and stale
    // assignment data: Koopa's per-scope sub-actor state cannot safely track
    // multiple sequential creations in one loaded stage. Returns true (and
    // marks Koopa as spawned) only for the first call after a stage change.
    static bool tryClaimKoopaSpawnForStage() {
        const char* stage = getCurrentStageName();
        if (!stage)
            return true;
        if (std::strcmp(stage, sLastKoopaStage) != 0) {
            std::strncpy(sLastKoopaStage, stage, sizeof(sLastKoopaStage) - 1);
            sLastKoopaStage[sizeof(sLastKoopaStage) - 1] = '\0';
            sKoopaSpawnedThisStage = false;
        }
        if (sKoopaSpawnedThisStage)
            return false;
        sKoopaSpawnedThisStage = true;
        return true;
    }

    static bool tryClaimFukankunSpawnForStage() {
        const char* stage = getCurrentStageName();
        if (!stage)
            return true;
        if (std::strcmp(stage, sLastFukankunStage) != 0) {
            std::strncpy(sLastFukankunStage, stage, sizeof(sLastFukankunStage) - 1);
            sLastFukankunStage[sizeof(sLastFukankunStage) - 1] = '\0';
            sFukankunSpawnedThisStage = false;
        }
        if (sFukankunSpawnedThisStage)
            return false;
        sFukankunSpawnedThisStage = true;
        return true;
    }

    struct HeapSnapshot {
        u32 freeBytes;
        u32 maxAllocatableBytes;
    };

    struct GpuPoolSnapshot {
        u32 usedBytes;
        u32 capacityBytes;
        bool valid;
    };

    struct GpuAdmissionFailure {
        const char* poolName = nullptr;
        u32 requestBytes = 0;
        u32 remainingBytes = 0;
    };

    static HeapSnapshot snapshotHeap(sead::Heap* heap) {
        if (!heap)
            return { 0, 0 };
        return {
            (u32)heap->getFreeSize(),
            (u32)heap->getMaxAllocatableSize(8),
        };
    }

    static al::GpuMemAllocator::Block* findGpuPool(
        al::GpuMemAllocator* allocator, const char* name) {
        if (!allocator || !name)
            return nullptr;
        for (s32 i = 0; i < allocator->mBlocks.size(); i++) {
            al::GpuMemAllocator::Block* block = allocator->mBlocks[i];
            if (block && std::strcmp(block->name.cstr(), name) == 0)
                return block;
        }
        return nullptr;
    }

    static GpuPoolSnapshot snapshotGpuPool(
        const al::ActorInitInfo& initInfo, const char* name) {
        al::GraphicsSystemInfo* graphics = initInfo.actorSceneInfo.graphicsSystemInfo;
        al::GpuMemAllocator::Block* block = graphics
            ? findGpuPool(graphics->mGpuMemAllocator, name)
            : nullptr;
        if (!block || block->usedSize < 0 || block->memorySize <= 0)
            return { 0, 0, false };
        return {
            (u32)block->usedSize,
            (u32)block->memorySize,
            true,
        };
    }

    static u32 remainingGpuBytes(const GpuPoolSnapshot& snapshot) {
        return snapshot.valid && snapshot.capacityBytes > snapshot.usedBytes
            ? snapshot.capacityBytes - snapshot.usedBytes
            : 0;
    }

    static constexpr u32 kRuntimeResourceReserve = 4 * 1024 * 1024;
    // Headroom demanded before a *new* archive is admitted. The world resource
    // heap is filled asynchronously: al::ArchiveEntry::load decompresses on
    // FileLoadThread, and the stage's own vanilla placements queue their
    // archives there too. getFreeSize() therefore reports space that is
    // already promised to loads still in flight. Spending it down to the
    // ordinary 4 MiB reserve left CityWorldHomeStage with 55 MB of pool
    // archives charged against a heap reporting 300 KiB free, and the next
    // decompression aborted inside sead::ExpHeap::tryAlloc on FileLoadThread.
    // A repeat instance adds no archive at all and keeps the smaller reserve.
    static constexpr u32 kRuntimeResourceArchiveReserve = 16 * 1024 * 1024;
    // Hard ceiling on the archive bytes one stage may add on top of vanilla.
    // The free-size check alone cannot bound this: early in a stage load
    // nothing has landed yet, so every admission sees a nearly empty heap and
    // the reserve is measured against space the stage has not claimed yet.
    // 12 MiB is roughly 25 distinct pool archives at the p50 size of 0.42 MB,
    // which is more variety than a single stage can show at once. Raise it
    // only with measured per-stage headroom (enemy_swap_stage_budgets.csv).
    static constexpr u64 kMaxStageArchiveBytes = 12ull * 1024 * 1024;
    static constexpr u32 kRuntimeSceneReserve = 1 * 1024 * 1024;
    // ModelUBO must still be able to construct large vanilla actors after the
    // last randomized placement. WorldMapEarth/WorldMapRoute are the concrete
    // failure case. Shader options need a smaller, independent reserve.
    static constexpr u32 kRuntimeModelUboReserve = 8 * 1024 * 1024;
    static constexpr u32 kRuntimeShaderOptionUboReserve = 2 * 1024 * 1024;
    static s32 sCreateDepth = 0;
    // True while a placement the audit decided on is being constructed. The
    // audit's actor counters only mean anything for its own placements --
    // classifyCurrentCase reads zero as "suppressed" and N as "the target was
    // built N times" -- so an ordinary placement must not be reported. Actors
    // spawned underneath a decided create still are, as that parent's nested
    // children (enemyRandoPlan.md section 8.3).
    static char sMemoryStage[128] = {};
    static s16 sMemoryScenario = -1;
    static bool sMemoryStageBlocked = false;
    // Resource archives load/decompress on FileLoadThread. Several factory
    // calls can therefore admit unique archives before the heap snapshots
    // reflect any of those allocations. Keep a stage-local cumulative charge
    // so individually safe admissions cannot collectively overcommit the
    // world resource heap.
    static u64 sMemoryAdmittedResourceBytes = 0;
    // Tightest world-resource-heap reading seen since the stage key was set.
    // Admission judges against this rather than the live value: the heap
    // rebounds as loads complete and free blocks coalesce, and a rebound is
    // not evidence that the space is spendable.
    static u32 sMemoryResourceFloorFree = 0;
    static u32 sMemoryResourceFloorMax = 0;
    static u16 sMemoryEntityCounts[DB::sEntityCount > 0 ? DB::sEntityCount : 1] = {};
    static bool sMemoryResources[DB::sMemoryResourceCount > 0
            ? DB::sMemoryResourceCount
            : 1]
        = {};
    // Costs learned from the allocator's monotonic counters survive stage
    // changes. A later occurrence can therefore use the largest cost actually
    // observed for this target instead of trusting an underestimate.
    static u32 sObservedModelUboBytes[DB::sEntityCount > 0
            ? DB::sEntityCount
            : 1]
        = {};
    static u32 sObservedShaderOptionUboBytes[DB::sEntityCount > 0
            ? DB::sEntityCount
            : 1]
        = {};
    // A forced capture is a constructive witness used by the logic solver.
    // Runtime admission may fall back to vanilla for an optional swap, but it
    // must not spend the GPU capacity already promised to a witness that has
    // not been constructed yet.
    static bool sGpuWitnessCreated[DB::sPlacementCount > 0
            ? DB::sPlacementCount
            : 1]
        = {};

    static void resetMemoryReservation(const char* stage, s16 scenario) {
        const char* key = stage ? stage : "";
        if (std::strcmp(sMemoryStage, key) == 0
            && sMemoryScenario == scenario)
            return;
        std::strncpy(sMemoryStage, key, sizeof(sMemoryStage) - 1);
        sMemoryStage[sizeof(sMemoryStage) - 1] = '\0';
        sMemoryScenario = scenario;
        sMemoryStageBlocked = false;
        sMemoryAdmittedResourceBytes = 0;
        sMemoryResourceFloorFree = 0;
        sMemoryResourceFloorMax = 0;
        std::memset(sMemoryEntityCounts, 0, sizeof(sMemoryEntityCounts));
        std::memset(sMemoryResources, 0, sizeof(sMemoryResources));
    }

    // Fold a reading into the stage's running floor and hand back the floor.
    // Zero means "never sampled" for the floor and "unavailable" for a
    // reading, so neither value may pull the other down.
    static u32 updateFloor(u32* floor, u32 current) {
        if (current != 0 && (*floor == 0 || current < *floor))
            *floor = current;
        return *floor != 0 ? *floor : current;
    }

    // Never index the entity table with a value that has not been checked.
    // A garbage index reached the logging calls below and handed VSNPrintf a
    // wild pointer for %s, which crashed inside stage init -- one bad index
    // became an unrelated-looking crash in al::initPlacementByStageInfo.
    static const char* safeEntityName(s16 entityIndex) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount)
            return "<invalid>";
        const char* name = DB::sEntities[entityIndex].name;
        return name ? name : "<unnamed>";
    }

    static bool checkGpuPoolAdmission(
        const GpuPoolSnapshot& pool, const char* poolName, u64 targetBytes,
        u32 reserveBytes, GpuAdmissionFailure* failure) {
        const u32 remaining = remainingGpuBytes(pool);
        // GpuMemAllocator aligns the used offset after performing its own
        // capacity comparison. Include the maximum 0x100-byte padding here so
        // its off-by-alignment ordering cannot carry the final allocation past
        // the end of the arena.
        const u64 required = (u64)targetBytes + reserveBytes + 0xff;
        if (pool.valid && required <= remaining)
            return true;
        if (failure) {
            failure->poolName = poolName;
            failure->requestBytes = targetBytes > 0xffffffffull
                ? 0xffffffffu
                : (u32)targetBytes;
            failure->remainingBytes = remaining;
        }
        return false;
    }

    struct PendingGpuWitnessCost {
        u64 modelBytes = 0;
        u64 shaderBytes = 0;
        u32 currentModelBytes = 0;
        u32 currentShaderBytes = 0;
        u32 count = 0;
        bool currentIsWitness = false;
    };

    static bool getEffectiveGpuCost(
        s16 entityIndex, u32* modelBytes, u32* shaderBytes) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount)
            return false;
        const DB::EntityMemoryCost* cost = EnemySwapRando::getMemoryCost(entityIndex);
        if (!cost || !cost->modelUboBytes || !cost->shaderOptionUboBytes)
            return false;
        if (modelBytes) {
            *modelBytes = sObservedModelUboBytes[entityIndex]
                    > cost->modelUboBytes
                ? sObservedModelUboBytes[entityIndex]
                : cost->modelUboBytes;
        }
        if (shaderBytes) {
            *shaderBytes = sObservedShaderOptionUboBytes[entityIndex]
                    > cost->shaderOptionUboBytes
                ? sObservedShaderOptionUboBytes[entityIndex]
                : cost->shaderOptionUboBytes;
        }
        return true;
    }

    static bool calculatePendingGpuWitnessCost(
        s32 currentPlacementIndex, PendingGpuWitnessCost* pending) {
        if (!pending)
            return false;
        *pending = {};
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (sGpuWitnessCreated[i]
                || !EnemySwapRando::isCaptureGateWitnessPlacement(i))
                continue;
            u32 modelBytes = 0;
            u32 shaderBytes = 0;
            if (!getEffectiveGpuCost(
                    EnemySwapRando::getResolvedEntityIndex(i),
                    &modelBytes, &shaderBytes))
                return false;
            pending->modelBytes += modelBytes;
            pending->shaderBytes += shaderBytes;
            pending->count++;
            if (i == currentPlacementIndex) {
                pending->currentModelBytes = modelBytes;
                pending->currentShaderBytes = shaderBytes;
                pending->currentIsWitness = true;
            }
        }
        return true;
    }

    static bool admitGpuReplacement(
        const al::ActorInitInfo& initInfo, const char* stage,
        s32 placementIndex, s16 entityIndex, bool reserveLogicWitnesses,
        GpuAdmissionFailure* failure) {
        u32 modelBytes = 0;
        u32 shaderBytes = 0;
        if (!getEffectiveGpuCost(entityIndex, &modelBytes, &shaderBytes))
            return false;

        u64 admittedModelBytes = modelBytes;
        u64 admittedShaderBytes = shaderBytes;
        PendingGpuWitnessCost pending = {};
        if (reserveLogicWitnesses) {
            if (!calculatePendingGpuWitnessCost(placementIndex, &pending))
                return false;
            admittedModelBytes += pending.modelBytes;
            admittedShaderBytes += pending.shaderBytes;
            // The current witness is already part of the pending total. Swap
            // its assigned estimate for the target actually being admitted;
            // generated assignments cancel exactly, while an inspector target
            // is still charged at its own cost.
            if (pending.currentIsWitness) {
                admittedModelBytes -= pending.currentModelBytes;
                admittedShaderBytes -= pending.currentShaderBytes;
            }
        }
        const GpuPoolSnapshot model = snapshotGpuPool(initInfo, "ModelUBO");
        const GpuPoolSnapshot shader = snapshotGpuPool(initInfo, "ShaderOptionUBO");
        if (!checkGpuPoolAdmission(
                model, "ModelUBO", admittedModelBytes,
                kRuntimeModelUboReserve,
                failure)
            || !checkGpuPoolAdmission(
                shader, "ShaderOptionUBO", admittedShaderBytes,
                kRuntimeShaderOptionUboReserve, failure)) {
            const char* poolName = failure && failure->poolName
                ? failure->poolName
                : "GPU";
            const u32 request = failure ? failure->requestBytes : 0;
            const u32 remaining = failure ? failure->remainingBytes : 0;
            Randomizer::logPush(
                "enemy swap GPU admission rejected: stage=%s target=%s "
                "pool=%s total_need=%u remaining=%u reserve=%u "
                "pending_witnesses=%d current_witness=%d",
                stage ? stage : "?", safeEntityName(entityIndex), poolName,
                request, remaining,
                std::strcmp(poolName, "ModelUBO") == 0
                    ? kRuntimeModelUboReserve
                    : kRuntimeShaderOptionUboReserve,
                reserveLogicWitnesses ? pending.count : 0,
                pending.currentIsWitness ? 1 : 0);
            return false;
        }
        return true;
    }

    static void observeGpuCostAfterCreation(
        const al::ActorInitInfo& initInfo, const char* stage,
        const char* objectId, s16 entityIndex,
        const GpuPoolSnapshot& beforeModel,
        const GpuPoolSnapshot& beforeShader) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount)
            return;
        const GpuPoolSnapshot model = snapshotGpuPool(initInfo, "ModelUBO");
        const GpuPoolSnapshot shader = snapshotGpuPool(initInfo, "ShaderOptionUBO");
        const u32 modelCost = beforeModel.valid && model.valid
                && model.usedBytes > beforeModel.usedBytes
            ? model.usedBytes - beforeModel.usedBytes
            : 0;
        const u32 shaderCost = beforeShader.valid && shader.valid
                && shader.usedBytes > beforeShader.usedBytes
            ? shader.usedBytes - beforeShader.usedBytes
            : 0;
        if (modelCost > sObservedModelUboBytes[entityIndex])
            sObservedModelUboBytes[entityIndex] = modelCost;
        if (shaderCost > sObservedShaderOptionUboBytes[entityIndex])
            sObservedShaderOptionUboBytes[entityIndex] = shaderCost;
        (void)stage;
        (void)objectId;
    }

    static bool admitMemoryReplacement(
        const char* stage, s16 scenario, s32 placementIndex, s16 entityIndex) {
        resetMemoryReservation(stage, scenario);
        if (sMemoryStageBlocked) {
            Randomizer::logPush(
                "enemy swap memory admission skipped: stage=%s is blocked",
                stage ? stage : "?");
            return false;
        }
        // One line per stage naming the heap state admission is judging
        // against. Every rejection path below already logs, but a run where
        // nothing swaps at all is the case where you need to see the inputs
        // rather than a hundred identical refusals.
        static char sReportedStage[64] = {};
        if (std::strcmp(sReportedStage, stage ? stage : "") != 0) {
            std::strncpy(sReportedStage, stage ? stage : "",
                sizeof(sReportedStage) - 1);
            sReportedStage[sizeof(sReportedStage) - 1] = '\0';
            sead::Heap* world = al::getWorldResourceHeap();
            sead::Heap* sceneNow = al::getSceneHeap();
            const HeapSnapshot w = snapshotHeap(world);
            const HeapSnapshot c = snapshotHeap(sceneNow);
            Randomizer::logPush(
                "enemy swap admission heaps: stage=%s world=%s free=%u max=%u "
                "scene=%s free=%u max=%u reserve=%u/%u/%u cap=%u",
                stage ? stage : "?", world ? "ok" : "NULL",
                w.freeBytes, w.maxAllocatableBytes,
                sceneNow ? "ok" : "NULL", c.freeBytes, c.maxAllocatableBytes,
                kRuntimeResourceReserve, kRuntimeResourceArchiveReserve,
                kRuntimeSceneReserve, (u32)kMaxStageArchiveBytes);
        }

        if (entityIndex < 0 || entityIndex >= DB::sEntityCount) {
            Randomizer::logPush(
                "enemy swap memory admission rejected: entity index %d out of "
                "range stage=%s",
                entityIndex, stage ? stage : "?");
            return false;
        }

        const DB::EntityMemoryCost* cost = EnemySwapRando::getMemoryCost(entityIndex);
        if (!cost || (!cost->measured && cost->confidence == 2)
            || cost->firstInstanceBytes == 0
            || cost->additionalInstanceBytes == 0
            || cost->generatorPeakBytes == 0
            || cost->resourceKeyCount == 0) {
            Randomizer::logPush(
                "enemy swap memory admission rejected: unknown target=%d stage=%s",
                entityIndex, stage ? stage : "?");
            return false;
        }
        if (!EnemySwapRando::checkMemoryBudgetForReplacement(
                placementIndex, entityIndex)) {
            Randomizer::logPush(
                "enemy swap memory admission rejected by assignment budget: "
                "stage=%s target=%s",
                stage ? stage : "?", DB::sEntities[entityIndex].name);
            return false;
        }

        u32 newResourceBytes = 0;
        for (u16 offset = 0; offset < cost->resourceKeyCount; offset++) {
            const u16 refIndex = cost->resourceKeyOffset + offset;
            if (refIndex >= DB::sMemoryResourceRefCount)
                return false;
            const u16 resourceIndex = DB::sMemoryResourceRefs[refIndex].resource;
            if (resourceIndex >= DB::sMemoryResourceCount)
                return false;
            if (!sMemoryResources[resourceIndex])
                newResourceBytes += DB::sMemoryResources[resourceIndex].bytes;
        }
        const bool generator = std::strcmp(
                                   DB::sEntities[entityIndex].category, "generator")
            == 0;
        const u32 instanceBytes = generator
            ? cost->generatorPeakBytes
            : (sMemoryEntityCounts[entityIndex] == 0
                      ? cost->firstInstanceBytes
                      : cost->additionalInstanceBytes);
        // The world resource heap, not the scene resource heap. Archives are
        // charged to the former; the latter is a fixed 512 KiB that never
        // holds one, and the memory audit measured it sitting at a constant
        // 409,168 bytes free in every context it sampled.
        //
        // That made this admission check unsatisfiable: kRuntimeResourceReserve
        // alone is 4 MiB, so the comparison below was true even for an entity
        // costing zero, and every generated replacement was silently rejected.
        // Only inspector overrides -- which skip admission -- ever appeared
        // in game. Same defect the report importer had, fixed there first.
        sead::Heap* resourceHeap = al::getWorldResourceHeap();
        sead::Heap* sceneHeap = al::getSceneHeap();
        const HeapSnapshot resource = snapshotHeap(resourceHeap);
        const HeapSnapshot scene = snapshotHeap(sceneHeap);
        // Some supported heap implementations report a valid free size but
        // return zero for the contiguous-allocation query while their frame
        // state is being advanced. Treat that one field as unavailable rather
        // than interpreting it as a zero-sized heap; the free-size check still
        // enforces the calibrated reserve. A missing heap remains a hard
        // failure.
        const bool resourceMaxKnown = resource.maxAllocatableBytes != 0;
        const bool sceneMaxKnown = scene.maxAllocatableBytes != 0;
        // A heap we cannot query is not a heap with nothing left in it.
        // snapshotHeap returns {0, 0} for a null pointer, so without this
        // distinction an unavailable world resource heap reads as totally
        // exhausted and rejects every replacement in the stage -- silently,
        // and for the rest of the stage once the latch below trips.
        //
        // When it is unavailable the archive term is skipped rather than
        // failed: the audit measured every archive in the pool summing to
        // 192 MB against a 320 MB worst-case headroom, so that term cannot be
        // the binding constraint, and dropping it costs far less than failing
        // closed on everything. The scene term stays hard -- that heap is
        // always present during creation, and it is the one that binds.
        const bool resourceKnown = resourceHeap != nullptr;
        const u32 resourceFree = resourceKnown
            ? updateFloor(&sMemoryResourceFloorFree, resource.freeBytes)
            : 0;
        const u32 resourceMax = resourceKnown && resourceMaxKnown
            ? updateFloor(&sMemoryResourceFloorMax, resource.maxAllocatableBytes)
            : 0;
        // Only carry the cumulative archive charge into a check that adds a
        // new unique resource. Repeated instances add no asynchronous archive
        // work and need only preserve the ordinary runtime reserve.
        const u64 resourceNeed64 = newResourceBytes
            ? sMemoryAdmittedResourceBytes + newResourceBytes
                + kRuntimeResourceArchiveReserve
            : kRuntimeResourceReserve;
        const u32 resourceNeed = resourceNeed64 > 0xffffffffull
            ? 0xffffffffu
            : (u32)resourceNeed64;
        // The stage's own archive allowance, independent of any heap reading.
        // A forced capture is exempt for the same reason the GPU path reserves
        // for one: the logic solver proved the route through that capture, so
        // refusing it hands the player a gate they cannot open. Its bytes are
        // still charged, so an optional swap after it sees the true total.
        const bool witness = EnemySwapRando::isCaptureGateWitnessPlacement(placementIndex);
        if (newResourceBytes && !witness
            && sMemoryAdmittedResourceBytes + newResourceBytes
                > kMaxStageArchiveBytes) {
            Randomizer::logPush(
                "enemy swap memory admission rejected by stage archive cap: "
                "stage=%s target=%s admitted=%u new=%u cap=%u",
                stage ? stage : "?", DB::sEntities[entityIndex].name,
                (u32)sMemoryAdmittedResourceBytes, newResourceBytes,
                (u32)kMaxStageArchiveBytes);
            return false;
        }
        const bool resourceTooSmall = resourceKnown
            && (resourceNeed64 > resourceFree
                || (resourceMax != 0 && resourceNeed64 > resourceMax));
        if (!sceneHeap || resourceTooSmall
            || instanceBytes + kRuntimeSceneReserve > scene.freeBytes
            || (sceneMaxKnown
                && instanceBytes + kRuntimeSceneReserve
                    > scene.maxAllocatableBytes)) {
            Randomizer::logPush(
                "enemy swap memory admission rejected: stage=%s target=%s "
                "resource need=%u free=%u max=%u scene need=%u free=%u max=%u",
                stage ? stage : "?", DB::sEntities[entityIndex].name,
                resourceNeed, resourceFree, resourceMax,
                instanceBytes + kRuntimeSceneReserve,
                scene.freeBytes, scene.maxAllocatableBytes);
            return false;
        }
        for (u16 offset = 0; offset < cost->resourceKeyCount; offset++) {
            const u16 refIndex = cost->resourceKeyOffset + offset;
            const u16 resourceIndex = DB::sMemoryResourceRefs[refIndex].resource;
            sMemoryResources[resourceIndex] = true;
        }
        sMemoryAdmittedResourceBytes += newResourceBytes;
        sMemoryEntityCounts[entityIndex]++;
        return true;
    }

    static void observeMemoryAfterCreation(
        const char* stage, const char* objectId, const char* source,
        const char* target, const char* category,
        const HeapSnapshot& beforeResource, const HeapSnapshot& beforeScene,
        const HeapSnapshot& beforeStationed,
        al::LiveActor* actor) {
        const HeapSnapshot resource = snapshotHeap(al::getWorldResourceHeap());
        const HeapSnapshot scene = snapshotHeap(al::getSceneHeap());
        (void)stage;
        (void)objectId;
        (void)source;
        (void)target;
        (void)category;
        (void)beforeResource;
        (void)beforeScene;
        (void)beforeStationed;
        (void)actor;
        const bool resourceMaxKnown = resource.maxAllocatableBytes != 0;
        const bool sceneMaxKnown = scene.maxAllocatableBytes != 0;
        // The reading taken right after a creation is the one that has seen
        // the most of this stage's loading, so it is the most useful floor the
        // next admission can be held to.
        updateFloor(&sMemoryResourceFloorFree, resource.freeBytes);
        updateFloor(&sMemoryResourceFloorMax, resource.maxAllocatableBytes);
        // Same distinction as in admitMemoryReplacement: only latch on a heap
        // we could actually read. Latching on an unavailable one would block
        // every remaining placement in the stage on no evidence at all.
        const bool resourceReadable = al::getWorldResourceHeap() != nullptr;
        if (target && ((resourceReadable && (!resource.freeBytes || (resourceMaxKnown && resource.maxAllocatableBytes < kRuntimeResourceReserve))) || !scene.freeBytes || (sceneMaxKnown && scene.maxAllocatableBytes < kRuntimeSceneReserve))) {
            sMemoryStageBlocked = true;
            Randomizer::logPush(
                "enemy swap memory admission stopped further replacements: "
                "stage=%s resource=%u/%u scene=%u/%u",
                stage ? stage : "?", resource.freeBytes,
                resource.maxAllocatableBytes, scene.freeBytes,
                scene.maxAllocatableBytes);
        }
    }

    void resetStageState() {
        // StageScene::init can be called with the same stage/scenario during a
        // reload.  Reset all state that is scoped to the currently loaded
        // scene rather than relying on the stage key changing.
        sScope = {};
        sCreateDepth = 0;
        sMemoryStage[0] = '\0';
        sMemoryScenario = -1;
        sMemoryStageBlocked = false;
        sMemoryAdmittedResourceBytes = 0;
        sMemoryResourceFloorFree = 0;
        sMemoryResourceFloorMax = 0;
        std::memset(sMemoryEntityCounts, 0, sizeof(sMemoryEntityCounts));
        std::memset(sMemoryResources, 0, sizeof(sMemoryResources));
        std::memset(sGpuWitnessCreated, 0, sizeof(sGpuWitnessCreated));

        sLastKoopaStage[0] = '\0';
        sKoopaSpawnedThisStage = false;
        sLastFukankunStage[0] = '\0';
        sFukankunSpawnedThisStage = false;
        sKoopaWithoutDepthShadowMap = nullptr;
        std::memset(
            sKoopaReplacementActors, 0,
            sizeof(sKoopaReplacementActors));
        sKoopaReplacementActorCount = 0;

        sTraceStage[0] = '\0';
        sFactoryTraceCount = 0;
        sEnemyFactoryTraceCount = 0;
        sIdentityTraceCount = 0;
        sReplacementResultTraceCount = 0;
        sKoopaCaptureTraceCount = 0;
        sEmptySklAnimSlotCount = 0;
        sSquirrelInitAfterPlacementActor = nullptr;
        sSquirrelMissedGroundConnector = nullptr;
        sSquirrelMissedGroundCount = 0;
    }
} // namespace EnemySwapHooks

namespace ProgressionHooks {
    void onPlacementActorCreated(al::LiveActor* actor, const al::PlacementInfo* placementInfo);
}

namespace EnemySwapHooks {

    HkTrampoline<al::LiveActor*, const al::ActorInitInfo&, const al::PlacementInfo*> createPlacementActorHook = hk::hook::trampoline([](const al::ActorInitInfo& initInfo, const al::PlacementInfo* placementInfo) -> al::LiveActor* {
        // The Multiplayer capture pool needs to know which capture classes this
        // stage places, and this is the one trampoline on this function - see the
        // sync bridge note in Multiplayer/main.hpp. No-op unless connected.
        mpOnCreatePlacementActor(placementInfo);

        CoinRando::Swap coinSwap = {};
        bool coinSwapActive = false;
        const Randomizer::EnemySwapDB::Entity* replacement = nullptr;
        const Randomizer::EnemySwapDB::Placement* sourcePlacement = nullptr;
        EnemySwapRando::SwapMode replacementMode = EnemySwapRando::SwapMode::FullActor;
        s32 placementIndex = -1;
        const char* placementStage = nullptr;
        const char* placementObjectId = nullptr;
        const char* placementUnitConfigName = nullptr;
        bool placementStageKnown = false;
        const Randomizer::EnemySwapDB::Entity* requestedReplacement = nullptr;
        s16 replacementEntityIndex = -1;
        bool ambiguousManualOverride = false;
        bool suppressPlacement = false;
        if (placementInfo) {
            const char* objId = nullptr;
            const char* unitConfigName = nullptr;
            placementInfo->getPlacementIter().tryGetStringByKey(&objId, "Id");
            placementInfo->getPlacementIter().tryGetStringByKey(&unitConfigName, "UnitConfigName");
            placementObjectId = objId;
            placementUnitConfigName = unitConfigName;
            coinSwapActive = CoinRando::resolveSwap(
                *placementInfo, getCurrentStageName(), getCurrentScenario(),
                &coinSwap);
            if (objId && unitConfigName) {
                const char* stage = resolvePlacementStage(*placementInfo);
                placementStage = stage;
                if (stage) {
                    placementStageKnown = EnemySwapRando::isKnownPlacementStage(stage);
                    sourcePlacement = EnemySwapRando::findPlacementForRuntime(
                        stage, objId, unitConfigName, &placementIndex);
                    // Some zone loaders expose a placement-file key that
                    // is neither the extracted physical key nor its
                    // StageMap spelling. Retry with the live current
                    // stage before treating the placement as vanilla.
                    // The object ID + UnitConfigName checks in the DB
                    // helper keep this fallback deterministic.
                    const char* currentStage = getCurrentStageName();
                    if (!sourcePlacement && !placementStageKnown && currentStage
                        && std::strcmp(currentStage, stage) != 0) {
                        sourcePlacement = EnemySwapRando::findPlacementForRuntime(
                            currentStage, objId, unitConfigName,
                            &placementIndex);
                        if (sourcePlacement)
                            placementStage = currentStage;
                    }
                    if (!sourcePlacement
                        || !EnemySwapRando::sourceEntityMatchesUnitConfig(
                            sourcePlacement->entity, unitConfigName))
                        placementIndex = -1;
                    replacement = EnemySwapRando::findReplacement(
                        placementStage, objId, unitConfigName,
                        &replacementMode, &placementIndex);
                    // PlacementFileName is usually the best physical key,
                    // but some copied zone records expose only a short zone
                    // name. Resolve an inspector override against the live
                    // current stage as well, and let it take precedence over
                    // a generated result selected from the less-specific
                    // zone key. Manual inspector state must not disappear
                    // merely because the loader changed that key.
                    if (replacementMode
                            != EnemySwapRando::SwapMode::ManualOverride
                        && currentStage && objId && unitConfigName
                        && EnemySwapDebug::hasAnyOverride()) {
                        s16 manualEntity = -1;
                        s32 manualPlacement = -1;
                        if (EnemySwapDebug::findRuntimeOverride(
                                currentStage,
                                EnemySwapDebug::resolveLogicStage(currentStage),
                                objId, unitConfigName, &manualEntity,
                                &manualPlacement)
                            && manualEntity >= 0
                            && manualEntity < DB::sEntityCount) {
                            replacement = &DB::sEntities[manualEntity];
                            replacementMode = EnemySwapRando::SwapMode::ManualOverride;
                            placementIndex = manualPlacement;
                            placementStage = currentStage;
                            if (manualPlacement >= 0
                                && manualPlacement < DB::sPlacementCount)
                                sourcePlacement = &DB::sPlacements[manualPlacement];
                        } else if (manualPlacement
                            == EnemySwapDebug::kAmbiguousRuntimeOverride) {
                            // A stale/generated result must not bypass an
                            // ambiguous pair table. The inspector overlay
                            // remains available so the user can clear or
                            // correct the conflicting pairs.
                            ambiguousManualOverride = true;
                            replacement = nullptr;
                            replacementMode = EnemySwapRando::SwapMode::FullActor;
                            placementIndex = -1;
                        }
                    }
                    if (!replacement
                        && replacementMode
                            != EnemySwapRando::SwapMode::Suppress
                        && !ambiguousManualOverride) {
                        if (!placementStageKnown && currentStage
                            && (!placementStage
                                || std::strcmp(currentStage,
                                       placementStage)
                                    != 0))
                            replacement = EnemySwapRando::findReplacement(
                                currentStage, objId, unitConfigName,
                                &replacementMode, &placementIndex);
                    }
                    requestedReplacement = replacement;
                    suppressPlacement = !replacement
                        && replacementMode
                            == EnemySwapRando::SwapMode::Suppress;
                }
                // Every guard below constrains a *seed*: keep quest NPCs
                // vanilla, one Koopa per stage, one Fukankun, and stay inside
                // the memory budget.
                const bool manualOverride = replacementMode
                    == EnemySwapRando::SwapMode::ManualOverride;
                // Interaction-driven TalkNpc placements include quest NPCs
                // (for example Cap Kingdom's treasure-hunt starter). Keep
                // them vanilla for generated assignments, but an explicit
                // inspector override is intentional and is allowed to target
                // any authored NPC placement.
                if (replacement && !manualOverride && sourcePlacement
                    && sourcePlacement->entity >= 0
                    && sourcePlacement->entity
                        < Randomizer::EnemySwapDB::sEntityCount
                    && std::strcmp(
                           Randomizer::EnemySwapDB::sEntities[sourcePlacement->entity].category,
                           "npc")
                        == 0
                    && requiresNpcInteraction(*placementInfo))
                    replacement = nullptr;
                // Generated assignments retain the one-Koopa-per-stage guard.
                // Explicit inspector overrides are intentionally unbounded;
                // clear the special actor pointer so a manual Koopa never
                // inherits state from an earlier generated replacement.
                if (replacement && replacement->className
                    && std::strcmp(replacement->className, "Koopa") == 0) {
                    if (manualOverride) {
                        sKoopaWithoutDepthShadowMap = nullptr;
                    } else if (!tryClaimKoopaSpawnForStage()) {
                        replacement = nullptr;
                    } else {
                        sKoopaWithoutDepthShadowMap = nullptr;
                    }
                }

                // Do the same for Fukankun to prevent potential FrameHeap
                // exhaustion or other crashes in generated assignments.
                if (replacement && !manualOverride && replacement->className
                    && std::strcmp(replacement->className, "Fukankun") == 0) {
                    if (!tryClaimFukankunSpawnForStage())
                        replacement = nullptr;
                }

                if (replacement) {
                    // The tables are inline so this pointer and sEntities are
                    // the same array in every translation unit. They were not
                    // always: while they had internal linkage this subtraction
                    // crossed two copies and produced a wild index, which then
                    // failed admission as an unknown entity and silently
                    // dropped every generated swap in the game. Verified here
                    // rather than assumed, because the failure mode is total
                    // and completely silent.
                    const ptrdiff_t rawIndex = replacement - DB::sEntities;
                    const bool indexSane = rawIndex >= 0 && rawIndex < DB::sEntityCount;
                    if (!indexSane)
                        Randomizer::logPush(
                            "enemy swap: replacement pointer is outside the "
                            "entity table (raw=%lld) stage=%s",
                            (long long)rawIndex, placementStage ? placementStage : "?");
                    replacementEntityIndex = indexSane ? (s16)rawIndex : (s16)-1;
                    if (!indexSane)
                        replacement = nullptr;
                }

                // The GPU arenas are process/graphics-system monotonic, not
                // stage heaps. Apply this guard to generated assignments,
                // inspector overrides, and audit targets alike. An audit
                // rejection is recorded as an allocation failure and suppresses
                // only that measured actor instead of crashing on a null pool.
                if (replacement && replacementEntityIndex < 0) {
                    const ptrdiff_t rawIndex = replacement - DB::sEntities;
                    if (rawIndex >= 0 && rawIndex < DB::sEntityCount)
                        replacementEntityIndex = (s16)rawIndex;
                }
                GpuAdmissionFailure gpuFailure = {};
                if (replacement
                    && !admitGpuReplacement(
                        initInfo, placementStage, placementIndex,
                        replacementEntityIndex, true,
                        &gpuFailure)) {
                    replacement = nullptr;
                    replacementEntityIndex = -1;
                }
                // A loaded scene may create actors from several embedded
                // placement files. Archive allocations all land in the same
                // world resource heap, so reservation state must use the host
                // scene key rather than reset whenever a zone key changes.
                const char* memoryAdmissionStage = getCurrentStageName();
                if (!memoryAdmissionStage)
                    memoryAdmissionStage = placementStage;
                if (replacement && !manualOverride
                    && (placementIndex < 0
                        || !admitMemoryReplacement(
                            memoryAdmissionStage, getCurrentScenario(),
                            placementIndex, replacementEntityIndex))) {
                    replacement = nullptr;
                    replacementEntityIndex = -1;
                }
            }
        }

        if (sCreateDepth == 0)
            resetTraceCountersForStage(placementStage);
        const bool traceFactory = sCreateDepth == 0 && sFactoryTraceCount < 16;
        const bool tracePlacementFactory = placementInfo
            && placementUnitConfigName && sEnemyFactoryTraceCount < 64;
        const bool traceEnemyFactory = sEnemyFactoryTraceCount < 64
            && (tracePlacementFactory || sourcePlacement
                || requestedReplacement || placementIndex >= 0);
        if (traceFactory || traceEnemyFactory) {
            const s16 overrideEntity = placementIndex >= 0
                ? EnemySwapDebug::getOverrideEntity(placementIndex)
                : -1;
            const char* sourceName = sourcePlacement
                    && sourcePlacement->entity >= 0
                    && sourcePlacement->entity < DB::sEntityCount
                ? DB::sEntities[sourcePlacement->entity].name
                : "unresolved";
            const char* overrideName = overrideEntity >= 0
                    && overrideEntity < DB::sEntityCount
                ? DB::sEntities[overrideEntity].name
                : "none";
            Randomizer::logPush(
                "enemy swap factory trace: stage=%s object=%s unit=%s "
                "placement=%d source=%s override=%s requested=%s admitted=%s "
                "manual=%d",
                placementStage ? placementStage : "?",
                placementObjectId ? placementObjectId : "?",
                placementUnitConfigName ? placementUnitConfigName : "?",
                placementIndex, sourceName, overrideName,
                requestedReplacement ? requestedReplacement->name : "none",
                replacement ? replacement->name : "none",
                replacementMode == EnemySwapRando::SwapMode::ManualOverride
                    ? 1
                    : 0);
            if (traceFactory)
                sFactoryTraceCount++;
            if (traceEnemyFactory)
                sEnemyFactoryTraceCount++;
        }

        const bool topLevel = sCreateDepth == 0;
        // Runtime admission only needs the post-create safety latch for an
        // actor that was actually replaced. Querying three heaps around every
        // vanilla actor is expensive and was never part of the release-mode
        // feature.
        const bool observeActorMemory = topLevel && replacement;
        const HeapSnapshot beforeResource = observeActorMemory
            ? snapshotHeap(al::getWorldResourceHeap())
            : HeapSnapshot { 0, 0 };
        const HeapSnapshot beforeScene = observeActorMemory
            ? snapshotHeap(al::getSceneHeap())
            : HeapSnapshot { 0, 0 };
        const GpuPoolSnapshot beforeModelUbo = replacement
            ? snapshotGpuPool(initInfo, "ModelUBO")
            : GpuPoolSnapshot { 0, 0, false };
        const GpuPoolSnapshot beforeShaderOptionUbo = replacement
            ? snapshotGpuPool(initInfo, "ShaderOptionUBO")
            : GpuPoolSnapshot { 0, 0, false };
        const HeapSnapshot beforeStationed = { 0, 0 };
        sCreateDepth++;
        const bool traceReplacementResult = replacement
            && sReplacementResultTraceCount < 32;
        al::LiveActor* actor = nullptr;
        // Nested creations belong to whichever parent is being measured, so
        // they are reported as nested rather than as the case's own actor.
        if (suppressPlacement) {
            // Suppression measures the stage without this actor, so the
            // original factory must not run at all. Generated prop deletion
            // uses the same safe factory boundary.
            actor = nullptr;
        } else if (!replacement && !coinSwapActive) {
            actor = createPlacementActorHook.orig(
                initInfo, placementInfo);
        } else {
            // Save/restore around the original call: an actor's init may
            // place further objects, which must not inherit this scope.
            const SwapScope previous = sScope;
            sScope.placement = placementInfo;
            sScope.sourceObjectId = placementObjectId;
            sScope.sourceUnitConfigName = placementUnitConfigName;
            sScope.objectName = coinSwapActive
                ? coinSwap.objectName
                : getReplacementObjectName(replacement);
            sScope.displayName = coinSwapActive
                ? coinSwap.displayName
                : getReplacementDisplayName(replacement);
            sScope.className = coinSwapActive
                ? coinSwap.className
                : getReplacementClassName(replacement);
            sScope.modelName = coinSwapActive
                ? coinSwap.modelName
                : getReplacementModelName(replacement);
            sScope.replaceActorIdentity = coinSwapActive
                || replacementMode == EnemySwapRando::SwapMode::FullActor
                || replacementMode == EnemySwapRando::SwapMode::ManualOverride;
            actor = createPlacementActorHook.orig(
                initInfo, placementInfo);
            sScope = previous;
        }
        sCreateDepth--;
        enableForeignDonsukePlayerReaction(actor, replacement);
        if (traceReplacementResult) {
            Randomizer::logPush(
                "enemy swap replacement result: stage=%s object=%s "
                "target=%s objectName=%s display=%s class=%s model=%s "
                "actor=%p dead=%d",
                placementStage ? placementStage : "?",
                placementObjectId ? placementObjectId : "?",
                replacement->name ? replacement->name : "?",
                getReplacementObjectName(replacement)
                    ? getReplacementObjectName(replacement)
                    : "?",
                getReplacementDisplayName(replacement)
                    ? getReplacementDisplayName(replacement)
                    : "?",
                replacement->className ? replacement->className : "?",
                getReplacementModelName(replacement)
                    ? getReplacementModelName(replacement)
                    : "?",
                actor, actor && al::isDead(actor) ? 1 : 0);
            sReplacementResultTraceCount++;
        }
        if (replacement && replacement->className
            && std::strcmp(replacement->className, "Koopa") == 0)
            rememberKoopaReplacementActor(actor);
        if (replacement)
            observeGpuCostAfterCreation(
                initInfo, placementStage, placementObjectId,
                replacementEntityIndex, beforeModelUbo,
                beforeShaderOptionUbo);
        if (actor && replacement && placementIndex >= 0
            && placementIndex < DB::sPlacementCount
            && EnemySwapRando::isCaptureGateWitnessPlacement(placementIndex))
            sGpuWitnessCreated[placementIndex] = true;
        if (observeActorMemory)
            observeMemoryAfterCreation(
                placementStage, placementObjectId,
                sourcePlacement && sourcePlacement->entity >= 0
                        && sourcePlacement->entity < DB::sEntityCount
                    ? DB::sEntities[sourcePlacement->entity].name
                    : nullptr,
                replacement ? replacement->name : nullptr,
                sourcePlacement && sourcePlacement->entity >= 0
                        && sourcePlacement->entity < DB::sEntityCount
                    ? DB::sEntities[sourcePlacement->entity].category
                    : nullptr,
                beforeResource, beforeScene, beforeStationed, actor);
        ProgressionHooks::onPlacementActorCreated(actor, placementInfo);
        mpOnPlacementActorCreated(actor, placementInfo);
        EnemySwapDebug::registerActor(actor, placementIndex);
        return actor;
    });

    // SphinxRide starts its PayCoin/Standby event flow synchronously from
    // init. A message-balloon node immediately asks rs::isPlayer2D about the
    // actor, but a foreign placement can be created before StageScene has put
    // a PlayerHolder in the actor's SceneInfo. The vanilla helper uses the
    // non-null-safe al::getPlayerActor path, tail-calls
    // PlayerHolder::tryGetPlayer with a null `this`, and faults at address 0.
    //
    // Treat that one early query as "not in 2D". This keeps the event-flow
    // executor running, so the replacement retains its dialogue, payment and
    // ride behavior once the player has been installed. Restrict the fallback
    // to synchronous replacement construction; ordinary player-state queries
    // continue through the original helper unchanged.
    HkTrampoline<bool, const al::LiveActor*> isPlayer2DHook = hk::hook::trampoline([](const al::LiveActor* actor) -> bool {
        if (!sScope.placement)
            return isPlayer2DHook.orig(actor);

        const al::PlayerHolder* playerHolder = actor && actor->mSceneInfo
            ? actor->mSceneInfo->playerHolder
            : nullptr;
        const bool playerReady = playerHolder
            && playerHolder->tryGetPlayer(0);
        if (playerReady)
            return isPlayer2DHook.orig(actor);

        if (sPlayerStateInitGuardCount < 16) {
            Randomizer::logPush(
                "enemy swap: deferred player 2D query for swapped %s "
                "(source %s actor=%p)",
                sScope.objectName ? sScope.objectName : "?",
                sScope.sourceUnitConfigName
                    ? sScope.sourceUnitConfigName
                    : "?",
                actor);
            sPlayerStateInitGuardCount++;
        }
        return false;
    });

    HkTrampoline<void, const char**, const al::ActorInitInfo&> getObjectNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> void {
        if (replacesActorIdentity(initInfo)) {
            traceScopedIdentity("getObjectName(ActorInitInfo)");
            *name = sScope.objectName;
            return;
        }
        getObjectNameHook.orig(name, initInfo);
    });

    HkTrampoline<bool, const char**, const al::ActorInitInfo&> tryGetObjectNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> bool {
        if (replacesActorIdentity(initInfo)) {
            traceScopedIdentity("tryGetObjectName(ActorInitInfo)");
            *name = sScope.objectName;
            return true;
        }
        return tryGetObjectNameHook.orig(name, initInfo);
    });

    // Some actor factories and placement helpers query the PlacementInfo
    // overloads directly. The ActorInitInfo overloads above cover the main
    // factory path, but leaving these readers vanilla makes the result depend
    // on which helper a target actor happens to call during init.
    HkTrampoline<void, const char**, const al::PlacementInfo&>
        getPlacementObjectNameHook = hk::hook::trampoline(
            [](const char** name, const al::PlacementInfo& placementInfo) -> void {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("getObjectName(PlacementInfo)");
                    *name = sScope.objectName;
                    return;
                }
                getPlacementObjectNameHook.orig(name, placementInfo);
            });

    HkTrampoline<bool, const char**, const al::PlacementInfo&>
        tryGetPlacementObjectNameHook = hk::hook::trampoline(
            [](const char** name, const al::PlacementInfo& placementInfo) -> bool {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("tryGetObjectName(PlacementInfo)");
                    *name = sScope.objectName;
                    return true;
                }
                return tryGetPlacementObjectNameHook.orig(name, placementInfo);
            });

    HkTrampoline<void, const char**, const al::ActorInitInfo&>
        getDisplayNameHook = hk::hook::trampoline(
            [](const char** name, const al::ActorInitInfo& initInfo) -> void {
                if (replacesActorIdentity(initInfo)) {
                    traceScopedIdentity("getDisplayName(ActorInitInfo)");
                    *name = sScope.displayName;
                    return;
                }
                getDisplayNameHook.orig(name, initInfo);
            });

    HkTrampoline<bool, const char**, const al::ActorInitInfo&>
        tryGetDisplayNameHook = hk::hook::trampoline(
            [](const char** name, const al::ActorInitInfo& initInfo) -> bool {
                if (replacesActorIdentity(initInfo)) {
                    traceScopedIdentity("tryGetDisplayName(ActorInitInfo)");
                    *name = sScope.displayName;
                    return sScope.displayName != nullptr;
                }
                return tryGetDisplayNameHook.orig(name, initInfo);
            });

    HkTrampoline<void, const char**, const al::PlacementInfo&>
        getPlacementDisplayNameHook = hk::hook::trampoline(
            [](const char** name,
                const al::PlacementInfo& placementInfo) -> void {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("getDisplayName(PlacementInfo)");
                    *name = sScope.displayName;
                    return;
                }
                getPlacementDisplayNameHook.orig(name, placementInfo);
            });

    HkTrampoline<bool, const char**, const al::PlacementInfo&>
        tryGetPlacementDisplayNameHook = hk::hook::trampoline(
            [](const char** name,
                const al::PlacementInfo& placementInfo) -> bool {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("tryGetDisplayName(PlacementInfo)");
                    *name = sScope.displayName;
                    return sScope.displayName != nullptr;
                }
                return tryGetPlacementDisplayNameHook.orig(
                    name, placementInfo);
            });

    HkTrampoline<void, const char**, const al::ActorInitInfo&> getClassNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> void {
        if (replacesActorIdentity(initInfo)) {
            traceScopedIdentity("getClassName(ActorInitInfo)");
            *name = sScope.className;
            return;
        }
        getClassNameHook.orig(name, initInfo);
    });

    HkTrampoline<bool, const char**, const al::ActorInitInfo&> tryGetClassNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> bool {
        if (replacesActorIdentity(initInfo)) {
            traceScopedIdentity("tryGetClassName(ActorInitInfo)");
            *name = sScope.className;
            return true;
        }
        return tryGetClassNameHook.orig(name, initInfo);
    });

    // getClassName(ActorInitInfo) and tryGetClassName(ActorInitInfo) are tiny
    // thunks into this full PlacementInfo implementation in the supported
    // 1.0.0 binary. Hooking the full implementation is important because a
    // direct call can bypass either thunk entry point.
    HkTrampoline<bool, const char**, const al::PlacementInfo&>
        tryGetPlacementClassNameHook = hk::hook::trampoline(
            [](const char** name, const al::PlacementInfo& placementInfo) -> bool {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("tryGetClassName(PlacementInfo)");
                    *name = sScope.className;
                    return true;
                }
                return tryGetPlacementClassNameHook.orig(name, placementInfo);
            });

    HkTrampoline<void, const char**, const al::PlacementInfo&>
        getPlacementClassNameHook = hk::hook::trampoline(
            [](const char** name, const al::PlacementInfo& placementInfo) -> void {
                if (replacesActorIdentity(placementInfo)) {
                    traceScopedIdentity("getClassName(PlacementInfo)");
                    *name = sScope.className;
                    return;
                }
                getPlacementClassNameHook.orig(name, placementInfo);
            });

    HkTrampoline<bool, const al::ActorInitInfo&, const char*>
        isClassNameHook = hk::hook::trampoline(
            [](const al::ActorInitInfo& initInfo, const char* name) -> bool {
                if (replacesActorIdentity(initInfo))
                    return name && sScope.className
                        && std::strcmp(sScope.className, name) == 0;
                return isClassNameHook.orig(initInfo, name);
            });

    HkTrampoline<bool, const al::PlacementInfo&, const char*>
        isPlacementClassNameHook = hk::hook::trampoline(
            [](const al::PlacementInfo& placementInfo, const char* name) -> bool {
                if (replacesActorIdentity(placementInfo))
                    return name && sScope.className
                        && std::strcmp(sScope.className, name) == 0;
                return isPlacementClassNameHook.orig(placementInfo, name);
            });

    // A source placement's ModelName belongs to its original actor, not the
    // replacement. Both ActorInitInfo and PlacementInfo overloads are needed:
    // MapParts actors (including Wanwan) query the latter during init.
    HkTrampoline<void, const char**, const al::ActorInitInfo&> getModelNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> void {
        if (isScoped(initInfo)) {
            traceScopedIdentity("getModelName(ActorInitInfo)");
            *name = sScope.modelName;
            return;
        }
        getModelNameHook.orig(name, initInfo);
    });

    HkTrampoline<bool, const char**, const al::ActorInitInfo&> tryGetModelNameHook = hk::hook::trampoline([](const char** name, const al::ActorInitInfo& initInfo) -> bool {
        if (isScoped(initInfo)) {
            traceScopedIdentity("tryGetModelName(ActorInitInfo)");
            *name = sScope.modelName;
            return true;
        }
        return tryGetModelNameHook.orig(name, initInfo);
    });

    HkTrampoline<void, const char**, const al::PlacementInfo&> getPlacementModelNameHook = hk::hook::trampoline([](const char** name, const al::PlacementInfo& placementInfo) -> void {
        if (isScoped(placementInfo)) {
            traceScopedIdentity("getModelName(PlacementInfo)");
            *name = sScope.modelName;
            return;
        }
        getPlacementModelNameHook.orig(name, placementInfo);
    });

    HkTrampoline<bool, const char**, const al::PlacementInfo&> tryGetPlacementModelNameHook = hk::hook::trampoline([](const char** name, const al::PlacementInfo& placementInfo) -> bool {
        if (isScoped(placementInfo)) {
            traceScopedIdentity("tryGetModelName(PlacementInfo)");
            *name = sScope.modelName;
            return true;
        }
        return tryGetPlacementModelNameHook.orig(name, placementInfo);
    });

    // Imomu normally follows an authored KeyMoveNext chain.  An enemy swap
    // placement has no such links, producing a one-pose keeper and leaving
    // Tropical Wiggler idle forever.  Give only replacement Imomu actors a
    // short, forward two-pose loop.  Real Imomu placements retain their
    // authored path (or deliberately-stationary one-pose behavior).
    HkTrampoline<al::KeyPoseKeeper*, const al::ActorInitInfo&> createKeyPoseKeeperHook = hk::hook::trampoline([](const al::ActorInitInfo& initInfo) -> al::KeyPoseKeeper* {
        al::KeyPoseKeeper* keeper = createKeyPoseKeeperHook.orig(initInfo);
        if (!keeper || !keeper->mKeyPoses || keeper->mKeyPoseCount != 1
            || !isScopedReplacement(initInfo, "Imomu"))
            return keeper;

        // KeyPose's constructor is not exported by the base game. The
        // original single pose is already fully initialized, so raw
        // storage plus byte copies produces two valid identical poses.
        u8* storage = new u8[sizeof(al::KeyPose) * 2];
        al::KeyPose* poses = reinterpret_cast<al::KeyPose*>(storage);
        std::memcpy(&poses[0], &keeper->mKeyPoses[0], sizeof(al::KeyPose));
        std::memcpy(&poses[1], &keeper->mKeyPoses[0], sizeof(al::KeyPose));

        // Inline equivalent of calcQuatFront. This overload is likewise
        // absent from the base game's exported symbol set.
        const sead::Quatf& quat = poses[0].mQuat;
        f32 forwardX = 2.0f * (quat.x * quat.z + quat.y * quat.w);
        f32 forwardZ = 1.0f - 2.0f * (quat.x * quat.x + quat.y * quat.y);
        if (forwardX * forwardX + forwardZ * forwardZ < 0.01f) {
            forwardX = 0.0f;
            forwardZ = 1.0f;
        }
        poses[1].mTrans.x += forwardX * 300.0f;
        poses[1].mTrans.z += forwardZ * 300.0f;

        delete[] reinterpret_cast<u8*>(keeper->mKeyPoses);
        keeper->mKeyPoses = poses;
        keeper->mKeyPoseCount = 2;
        return keeper;
    });

    // NPC residents have disjoint skeletons and animation sets. Even with a
    // full target TalkNpc actor, source placement material data can name an
    // animation the target does not have. Neutralize that source data at init:
    //   - skip the placement's VisAnim/MclAnim/MtpAnim material animations, and
    //     the material-anim preset (both reference the source model's anims), and
    //   - force the dialogue EventName to "None" (the generic TalkNpc event flow
    //     the game itself uses for eventless residents), so no source-specific
    //     event actions ever play. The wait/idle action stays vanilla.
    // Net: a swapped NPC keeps its new model, idles correctly, is effectively
    // silent, and never references a missing animation.
    HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&> setNpcMaterialAnimHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& initInfo) -> void {
        if (isScopedNpcReplacement(initInfo))
            return;
        setNpcMaterialAnimHook.orig(actor, initInfo);
    });

    // TalkNpcParam material-anim preset (rs::tryApplyNpcMaterialAnimPresetFromPlacementInfo),
    // called alongside setNpcMaterialAnimFromPlacementInfo; same foreign-model risk.
    HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&, const void*> applyNpcMaterialAnimPresetHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& initInfo, const void* param) -> void {
        if (isScopedNpcReplacement(initInfo))
            return;
        applyNpcMaterialAnimPresetHook.orig(actor, initInfo, param);
    });

    // A replacement NPC must not run the source resident's dialogue event,
    // which can reference source-only animations. Redirect only EventName to
    // the generic "None" flow; every other placement string is untouched.
    // Yukimaru shares the same failure mode (see isScopedYukimaruReplacement)
    // so it gets the same redirect.
    HkTrampoline<void, const char**, const al::ActorInitInfo&, const char*> getStringArgHook = hk::hook::trampoline([](const char** out, const al::ActorInitInfo& initInfo, const char* key) -> void {
        if (out && key
            && (isScopedNpcReplacement(initInfo)
                || isScopedYukimaruReplacement(initInfo))
            && std::strcmp(key, "EventName") == 0) {
            *out = "None";
            return;
        }
        getStringArgHook.orig(out, initInfo, key);
    });

    // al::getStringArg has a second, distinct overload that returns the
    // string directly instead of writing through an out-param.
    // rs::initEventFlowFromPlacementInfo (called by both TalkNpc and
    // Yukimaru's init()) calls this overload, not the one above -- without
    // this hook too, EventName still reads the foreign placement's real
    // value and the redirect above never applies, so the actor still starts
    // an event flow chart it doesn't have an "Init" entry for and crashes.
    HkTrampoline<const char*, const al::ActorInitInfo&, const char*> getStringArgValueHook = hk::hook::trampoline([](const al::ActorInitInfo& initInfo, const char* key) -> const char* {
        if (key
            && (isScopedNpcReplacement(initInfo)
                || isScopedYukimaruReplacement(initInfo))
            && std::strcmp(key, "EventName") == 0) {
            return "None";
        }
        const char* value = getStringArgValueHook.orig(initInfo, key);
        // TalkNpc::init passes EventWaitActionName straight to
        // al::isEqualSubString. Native NPC placements always author it, but a
        // foreign source such as AmiiboNpc does not, so the unguarded compare
        // receives null before the replacement actor has finished init.
        // "Wait" is the generic TalkNpc idle used by target NPC models; retain
        // an authored value when one exists and fill only the missing case.
        if (!value && key && isScopedNpcReplacement(initInfo)
            && std::strcmp(key, "EventWaitActionName") == 0) {
            return "Wait";
        }
        return value;
    });

    // Enemy cap arguments are actor-specific even though several actors call
    // them CapName.  A captured Goomba, for example, stores the localized
    // costume name ("construction helmet") there, while Tsukkun passes the
    // value straight to initChildActorWithArchiveNameNoPlacementInfo as an
    // ObjectData archive.  Letting a replacement Tsukkun inherit the Goomba
    // value makes Resource::tryGetByml build an init-file path from a resource
    // that does not exist and crashes during stage initialization.  Supply
    // Tsukkun's native child archive so it keeps its enemy cap and all of the
    // associated blow-down/capture behavior.  KuriboWing also reads CapName,
    // but its native placement data treats the cap as optional.  Do not pass a
    // foreign source actor's cap archive into a replacement KuriboWing; the
    // cap-free path is already fully supported by the actor.
    //
    // Wanwan also uses this argument through the suffix-less overload. Its
    // native cap is optional, but a foreign prop's CapName is not an actor
    // archive: passing it into initChildActorWithArchiveNameNoPlacementInfo
    // crashes while resolving InitActor metadata. Keep replacement Chain
    // Chomps cap-free, which their runtime paths already support.
    //
    // BrosHackBase::init also reads BrosCapType through tryGetStringArg.
    // Foreign placements such as Frog do not carry that target-specific
    // argument, which otherwise leaves a replacement Fire Bro without its
    // native cap setup. Supply the native Helmet value only when the source
    // has no authored value, so source-specific cap appearance remains intact.
    HkTrampoline<bool, const char**, const al::ActorInitInfo&, const char*>
        tryGetStringArgHook = hk::hook::trampoline(
            [](const char** out, const al::ActorInitInfo& initInfo,
                const char* key) -> bool {
                // A placement's ItemType family selects what the SOURCE actor
                // drops. rs::getItemType folds all three keys into one enum,
                // but each actor implements only the subset its own native
                // placements use, and the misses are not benign.
                // CactusMini::init handles Coin, LifeUpItem and Random and
                // passes a null item name to al::addItem for every other
                // value; ProjectItemDirector::declareItem hands that null
                // straight to strstr and crashes during stage init. The
                // CityWorldBushA bushes in CityWorldHomeStage author
                // ItemTypeNoShine=DotMarioCat, which is exactly such a value.
                // BlowObj::init has the same partial switch and returns from
                // init half-built instead. Treat the authored item as
                // source-specific tuning, like EventName and ShadowLength
                // elsewhere in this file: a replacement falls to the
                // ItemType==-1 path, which every target implements fully and
                // which still gives the actor its own default item. Moon-
                // carrying placements never reach here -- the swap map
                // excludes them -- so no Shine is dropped by this.
                if (out && key && isItemTypeArg(key)
                    && replacesActorIdentity(initInfo)) {
                    *out = nullptr;
                    return false;
                }
                if (out && key && isScopedReplacement(initInfo, "Tsukkun")
                    && std::strcmp(key, "CapName") == 0) {
                    *out = "EnemyCapTsukkun";
                    return true;
                }
                if (out && key
                    && isScopedReplacement(initInfo, "KuriboWing")
                    && std::strcmp(key, "CapName") == 0) {
                    *out = nullptr;
                    return false;
                }
                if (out && key && isScopedWanwanReplacement(initInfo)
                    && std::strcmp(key, "CapName") == 0) {
                    *out = nullptr;
                    return false;
                }
                if (out && key && isScopedFireBrosReplacement(initInfo)
                    && std::strcmp(key, "BrosCapType") == 0) {
                    if (tryGetStringArgHook.orig(out, initInfo, key))
                        return true;
                    *out = "Helmet";
                    return true;
                }
                return tryGetStringArgHook.orig(out, initInfo, key);
            });

    // ShadowLength is placement-specific tuning for the source NPC's shadow
    // masks. A replacement TalkNpc uses the target model's shadow setup, and
    // low-detail targets such as CityManLow legitimately have no shadow-mask
    // controller. Passing CapMan's authored value through makes TalkNpc::init
    // call the non-null-safe setShadowMaskDropLength on that missing
    // controller. Treat the foreign argument as absent so the target retains
    // its model-defined default shadow behavior.
    HkTrampoline<bool, s32*, const al::ActorInitInfo&, const char*>
        tryGetIntArgHook = hk::hook::trampoline(
            [](s32* out, const al::ActorInitInfo& initInfo,
                const char* key) -> bool {
                if (out && key && isScopedNpcReplacement(initInfo)
                    && std::strcmp(key, "ShadowLength") == 0) {
                    *out = -1;
                    return false;
                }
                return tryGetIntArgHook.orig(out, initInfo, key);
            });

    // TalkNpc::init treats IsReduceEyeSensor as a request to resize its "Eye"
    // sensor. That flag belongs to the source NPC placement, though, and can
    // survive an NPC-to-NPC swap into a low-detail resident such as CityManLow
    // that legitimately has no HitSensorKeeper. setSensorRadius then calls
    // HitSensorKeeper::getSensor through a null keeper during stage init. A
    // replacement must not inherit that source-model tuning flag, so report it
    // as present but disabled while constructing any replacement TalkNpc.
    //
    // Imomu defaults IsOnCap to true when the placement does not author the
    // argument. Its native placements override that default, but a foreign
    // enemy placement usually does not. The default path tries to construct
    // an EnemyCapSanta child whose archive was never requested for this
    // stage; findOrCreateActorResource then reaches Resource::tryGetByml with
    // a null resource and crashes during stage initialization. EnemyCap is an
    // optional capture-blocking hat throughout ImomuEnemyState, so replacement
    // Imomu actors use the cap-free variant.
    HkTrampoline<bool, bool*, const al::ActorInitInfo&, const char*>
        tryGetBoolArgHook = hk::hook::trampoline(
            [](bool* out, const al::ActorInitInfo& initInfo,
                const char* key) -> bool {
                // This hook is also the owner of the painting-unlock override
                // when chaos randomization is compiled in. Do not install a
                // second trampoline for this symbol: Hakkun does not relocate
                // a copied PC-relative branch when hooks are stacked.
                if (out && key && std::strcmp(key, "IsAfterClearOnly") == 0) {
                    *out = false;
                    return false;
                }
                if (out && key && isScopedNpcReplacement(initInfo)
                    && std::strcmp(key, "IsReduceEyeSensor") == 0) {
                    *out = false;
                    return true;
                }
                if (out && key && isScopedReplacement(initInfo, "Imomu")
                    && std::strcmp(key, "IsOnCap") == 0) {
                    *out = false;
                    return true;
                }
                return tryGetBoolArgHook.orig(out, initInfo, key);
            });

    // Crash safety net for the self-consistent NPC swap. Event flow first maps
    // its requested action through EventFlowDataHolder (for example, "Wait"
    // can become the source resident's placement-specific action), then starts
    // that mapped action and unconditionally reads its skeletal frame limit.
    // Checking the unconverted request is therefore insufficient: it can say
    // "Wait" exists while the converted source-only animation does not. Match
    // the engine's conversion before testing for the skeletal animation.
    HkTrampoline<void, al::LiveActor*, const al::IUseEventFlowData*, const char*>
        startEventActionAtRandomFrameHook = hk::hook::trampoline([](al::LiveActor* actor, const al::IUseEventFlowData* eventFlowData,
                                                                     const char* name) -> void {
            if (actor && eventFlowData && name) {
                al::StringTmp<128> actionName;
                al::makeEventActionName(&actionName, eventFlowData, name);
                if (!al::isSklAnimExist(actor, actionName.cstr()))
                    return;
            }
            startEventActionAtRandomFrameHook.orig(actor, eventFlowData, name);
        });

    // The same crash, one reader further along. al::EventFlowMovementWander is
    // the wandering variant of that Wait movement, and its exeWait never goes
    // through al::startEventActionAtRandomFrame: 120 frames into its own Wait
    // nerve it reads al::getSklAnimFrameMax(actor, 0) outright, so the
    // pre-check above cannot cover it. A replacement NPC reaches that read
    // with skeletal slot 0 still empty, because the "Wait" the movement asks
    // for is converted through the *source* placement's EventFlowDataHolder
    // and the converted name need not exist on the replacement model.
    //
    // al::AnimPlayerSkl::getSklAnimFrameMax(int) is the one member of its
    // family that walks the slot without testing it: getSklAnimFrame,
    // getSklAnimFrameRate and getPlayingSklAnimName all return early on an
    // empty slot (ida_context/Library/Anim/AnimPlayerSkl.md). That is exactly
    // why the two reads exeWait performs before this one come back 0 while
    // this one dereferences null. Restore the missing test at the actor-level
    // wrapper and answer with the same 0 its guarded siblings give.
    //
    // Deliberately unscoped: the guarded branch is reachable only in the state
    // that would otherwise fault, so no actor that animates today changes
    // behavior. exeWait's own arithmetic then leaves the actor in Wait -- a
    // fmodf by a zero frame max is NaN, and the unordered compare that follows
    // takes the keep-waiting path -- which is the right outcome for an actor
    // that has nothing to play.
    HkTrampoline<f32, const al::LiveActor*, s32> getSklAnimFrameMaxHook = hk::hook::trampoline(
        [](const al::LiveActor* actor, s32 index) -> f32 {
            if (actor && !al::isSklAnimPlaying(actor, index)) {
                if (sEmptySklAnimSlotCount < 8) {
                    const char* name = actor->getName();
                    Randomizer::logPush(
                        "enemy swap: skeletal slot %d is empty on '%s'; "
                        "returning frame max 0 instead of faulting",
                        index, name ? name : "?");
                    sEmptySklAnimSlotCount++;
                }
                return 0.0f;
            }
            return getSklAnimFrameMaxHook.orig(actor, index);
        });

    HkTrampoline<bool, const al::ActorInitInfo&, const char*> isObjectNameHook = hk::hook::trampoline([](const al::ActorInitInfo& initInfo, const char* name) -> bool {
        if (replacesActorIdentity(initInfo))
            return name && std::strcmp(sScope.objectName, name) == 0;
        return isObjectNameHook.orig(initInfo, name);
    });

    HkTrampoline<bool, const al::ActorInitInfo&, const char*> isObjectNameSubStrHook = hk::hook::trampoline([](const al::ActorInitInfo& initInfo, const char* name) -> bool {
        if (replacesActorIdentity(initInfo))
            return name && std::strstr(sScope.objectName, name) != nullptr;
        return isObjectNameSubStrHook.orig(initInfo, name);
    });

    HkTrampoline<bool, const al::PlacementInfo&, const char*>
        isPlacementObjectNameHook = hk::hook::trampoline(
            [](const al::PlacementInfo& placementInfo, const char* name) -> bool {
                if (replacesActorIdentity(placementInfo))
                    return name && std::strcmp(sScope.objectName, name) == 0;
                return isPlacementObjectNameHook.orig(placementInfo, name);
            });

    HkTrampoline<bool, const al::PlacementInfo&, const char*>
        isPlacementObjectNameSubStrHook = hk::hook::trampoline(
            [](const al::PlacementInfo& placementInfo, const char* name) -> bool {
                if (replacesActorIdentity(placementInfo))
                    return name && std::strstr(sScope.objectName, name) != nullptr;
                return isPlacementObjectNameSubStrHook.orig(placementInfo, name);
            });

    // Cactus and CactusMini send AttachCactusNeedle to Mario on contact.
    // Vanilla creates Mario's CactusMiniNeedle part only for stages that
    // explicitly request it. Swapped Cacti can appear in any stage, so ensure
    // the regular player-init path creates that part while enemy swapping is
    // enabled. With chaos disabled, preserve the stage-authored vanilla flag.
    HkTrampoline<bool, const PlayerInitInfo&> needCreateNoseNeedleHook = hk::hook::trampoline([](const PlayerInitInfo& initInfo) -> bool {
        return Randomizer::isEnemySwapRandoEnabled()
            || needCreateNoseNeedleHook.orig(initInfo);
    });

    // DemoStateHackFirst is the game's generic "first time this species has
    // ever been hacked" flourish, shared across capturable species (Frog
    // uses the same class). A species discriminator at offset 0x28 (0 = ?,
    // 1 = Frog, 2 = Koopa; cross-confirmed from both
    // DemoStateHackFirst::isEnableShowHackDemo and ::tryHackFirst) picks the
    // demo and, for Koopa specifically, gates it on being in Moon Kingdom
    // and drives an authored camera ticket plus rs::setFlagOnStartKoopaCapture.
    // That authored branch is unsafe for a foreign Enemy Swap placement, so
    // only tracked replacement Koopas bypass it. Native Koopa placements keep
    // the original final-escape behavior.
    static al::LiveActor* getDemoStateHackActor(
        const DemoStateHackFirst* self) {
        // DemoStateHackFirst inherits ActorStateBase; the parent actor is at
        // +0x18 in the supported 1.0.0 ABI (see DemoStateHackFirst.md).
        return self
            ? *reinterpret_cast<al::LiveActor* const*>(
                  reinterpret_cast<const char*>(self) + 0x18)
            : nullptr;
    }

    HkTrampoline<bool, const DemoStateHackFirst*> isEnableShowHackDemoHook = hk::hook::trampoline([](const DemoStateHackFirst* self) -> bool {
        if (self && *reinterpret_cast<const s32*>(reinterpret_cast<uintptr_t>(self) + 0x28) == 2
            && isKoopaReplacementActor(getDemoStateHackActor(self)))
            return false;
        return isEnableShowHackDemoHook.orig(self);
    });

    // isEnableShowHackDemo is consulted by both of the following methods,
    // but keep the replacement decision at their actual entry points too.
    // This covers builds/paths where a DemoStateHackFirst call is reached
    // through a cached state before the virtual helper observes the tracked
    // replacement actor. Returning false lets Koopa::receiveMsg continue to
    // its ordinary EnemyStateHackStart path instead of entering the
    // Moon/final-escape demo state.
    HkTrampoline<bool, DemoStateHackFirst*, const al::SensorMsg*>
        tryHackFirstDemoWaitHook = hk::hook::trampoline(
            [](DemoStateHackFirst* self, const al::SensorMsg* msg) -> bool {
                if (isKoopaReplacementActor(getDemoStateHackActor(self)))
                    return false;
                return tryHackFirstDemoWaitHook.orig(self, msg);
            });

    HkTrampoline<bool, DemoStateHackFirst*, IUsePlayerHack**,
        const al::SensorMsg*, al::HitSensor*, al::HitSensor*>
        tryHackFirstHook = hk::hook::trampoline(
            [](DemoStateHackFirst* self, IUsePlayerHack** hack,
                const al::SensorMsg* msg, al::HitSensor* target,
                al::HitSensor* source) -> bool {
                if (isKoopaReplacementActor(getDemoStateHackActor(self)))
                    return false;
                return tryHackFirstHook.orig(
                    self, hack, msg, target, source);
            });

    // The final-escape sequence stores a persistent StartKoopaCapture flag
    // in GameDataFile. Koopa::init() consults that flag before choosing its
    // ordinary hack-start path, so a stale flag would make a swapped Bowser
    // behave like the story-ending Bowser in unrelated stages. Keep the
    // authored flag and behavior for native Koopas, but mask both the read
    // and the write for replacement actors only.
    HkTrampoline<bool, const al::LiveActor*>
        isStartKoopaCaptureHook = hk::hook::trampoline(
            [](const al::LiveActor* actor) -> bool {
                if (isKoopaReplacementActive()
                    || isKoopaReplacementActor(actor))
                    return false;
                return isStartKoopaCaptureHook.orig(actor);
            });

    HkTrampoline<void, const al::LiveActor*>
        setFlagOnStartKoopaCaptureHook = hk::hook::trampoline(
            [](const al::LiveActor* actor) {
                if (isKoopaReplacementActive()
                    || isKoopaReplacementActor(actor))
                    return;
                setFlagOnStartKoopaCaptureHook.orig(actor);
            });

    // rs::isMsgHackInvalidEscape is declared by the SDK headers but is not
    // exported in the supported game build. Call its known 1.0.0 main-module
    // address directly, matching the other undeclared game helpers used by
    // this file, so the final link does not require an unavailable symbol.
    static bool isMsgHackInvalidEscapeRuntime(
        const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x00584cb0)(msg);
    }

    // The SDK declares these message predicates, but the supported game does
    // not export all of them to the mod link. Keep the Koopa lifecycle hooks
    // on the same known 1.0.0 main-module entry points used above.
    static bool isMsgStartHackRuntime(const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0058b97c)(msg);
    }

    static bool isMsgCancelHackRuntime(const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x005810e8)(msg);
    }

    static bool isMsgHackerDamageAndCancelRuntime(
        const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0056ce88)(msg);
    }

    static bool isMsgEnemyAttackRuntime(const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x008f9034)(msg);
    }

    static bool isMsgHammerBrosHammerEnemyAttackRuntime(
        const al::SensorMsg* msg) {
        if (!msg)
            return false;
        using Fn = bool (*)(const al::SensorMsg*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x00585420)(msg);
    }

    // rs::requestDamage is used by the native Bros capture path but is not
    // exported by the supported game build. Call its documented 1.0.0
    // main-module entry point directly, just like the message predicates
    // above, so the mod does not leave an unresolved SDK symbol at link time.
    static bool requestDamageRuntime(IUsePlayerHack* hack) {
        if (!hack)
            return false;
        using Fn = bool (*)(IUsePlayerHack*);
        return reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0056c978)(hack);
    }

    static void clearKoopaDeadSwitch(al::LiveActor* actor) {
        if (!actor)
            return;
        // Koopa::kill normally turns on the authored SwitchDeadOn. This
        // respawn is intentionally temporary, so clear that switch before
        // appearing the same actor again. Koopa::kill() passes its
        // IUseStageSwitch subobject (actor + 0x18), not the LiveActor base.
        // The helper is not exported by the supported build; this is its
        // documented 1.0.0 main-module offset.
        using Fn = bool (*)(al::IUseStageSwitch*);
        reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x00a279ec)(
            reinterpret_cast<al::IUseStageSwitch*>(
                reinterpret_cast<char*>(actor) + 0x18));
    }

    static void armKoopaRespawn(KoopaReplacementRecord* record) {
        if (!record || !record->actor)
            return;
        record->captureActive = false;
        // Both Koopa::receiveMsg and PlayerHackKeeper::endHack can observe
        // the same CancelHack. Do not reset the phase if the request has
        // already been queued by the other hook.
        if (!record->respawnPending) {
            record->respawnPending = true;
            record->deathApplied = false;
            record->respawnDelayFrames = 0;
            Randomizer::logPush(
                "enemy swap Koopa decapture queued: actor=%p x=%.1f "
                "y=%.1f z=%.1f",
                record->actor, record->spawnTrans.x, record->spawnTrans.y,
                record->spawnTrans.z);
        }
    }

    static void disarmKoopaRespawn(KoopaReplacementRecord* record) {
        if (!record)
            return;
        record->respawnPending = false;
        record->deathApplied = false;
        record->respawnDelayFrames = 0;
    }

    static void killKoopaReplacementActor(al::LiveActor* actor) {
        if (!actor)
            return;
        // Call Koopa::kill directly instead of relying on the foreign
        // placement's vtable slot. Koopa::kill invokes LiveActor::kill and
        // applies the authored SwitchDeadOn behavior.
        using Fn = void (*)(al::LiveActor*);
        reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0007e1c8)(actor);
        // Koopa::kill normally dispatches through LiveActor::kill to the
        // virtual dead transition. A replacement can, however, have a
        // partially authored child setup, and the dead flag alone is not the
        // visibility guarantee we need here. Always run Koopa's concrete
        // makeActorDead path once after kill: it hides the model, invalidates
        // sensors/collision, and is idempotent for an already-dead actor.
        reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0007e188)(actor);
    }

    static void appearKoopaReplacementActor(al::LiveActor* actor) {
        if (!actor)
            return;
        // Koopa::appear performs LiveActor::appear and then selects Koopa's
        // normal wait nerve. Calling the concrete method is important here:
        // LiveActor::appear alone would restore visibility without restoring
        // the Koopa state machine.
        using Fn = void (*)(al::LiveActor*);
        reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x0007e15c)(actor);
    }

    static const al::Nerve* getKoopaNerve(u32 offset) {
        return reinterpret_cast<const al::Nerve*>(
            hk::ro::getMainModule()->range().start() + offset);
    }

    static void setKoopaReplacementWaitNerve(al::LiveActor* actor) {
        if (!actor)
            return;
        // Koopa::initNerve registers Wait at the documented main-module data
        // address. Koopa::appear normally selects it, but reasserting it here
        // also covers a stage/demo tick that runs between appear and the next
        // Cappy collision.
        al::setNerve(actor, getKoopaNerve(0x01c6b430));
    }

    static IUsePlayerHack* forceKoopaReplacementStartHack(
        al::LiveActor* actor, const al::SensorMsg* msg,
        al::HitSensor* target, al::HitSensor* source) {
        if (!actor || !msg || !target || !source)
            return nullptr;
        void* hackStart = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(actor) + 0x158);
        if (!hackStart)
            return nullptr;

        // EnemyStateHackStart::tryStart is the same leaf used by
        // Koopa::receiveMsg. It performs rs::startHack, starts the normal
        // hack-start demo, and arms its own state nerve. Use it only as a
        // replacement-only recovery when the parent Koopa dispatch returned
        // false after the actor was already restored to Wait.
        using Fn = IUsePlayerHack* (*)(void*, const al::SensorMsg*, al::HitSensor*, al::HitSensor*);
        IUsePlayerHack* hack = reinterpret_cast<Fn>(
            hk::ro::getMainModule()->range().start() + 0x000fd754)(
            hackStart, msg, target, source);
        if (!hack)
            return nullptr;

        *reinterpret_cast<IUsePlayerHack**>(
            reinterpret_cast<char*>(actor) + 0x110)
            = hack;
        al::invalidateClipping(actor);
        al::invalidateOcclusionQuery(actor);
        rs::hideShadowHackCap(hack);
        al::setNerve(actor, getKoopaNerve(0x01c6b438));
        return hack;
    }

    static void resetKoopaReplacementCaptureState(al::LiveActor* actor) {
        if (!actor)
            return;

        // Koopa::receiveMsg stores the live IUsePlayerHack at +0x110. The
        // normal CancelHack branch clears it through rs::endHack, but the
        // stage-wide fallback also handles scripted exits that skip that
        // branch. Never let a stale pointer make the freshly appeared actor
        // look already captured.
        *reinterpret_cast<void**>(reinterpret_cast<char*>(actor) + 0x110) = nullptr;

        // KoopaHackStopCtrl is the object at +0x148. The native cancel path
        // resets its state before returning to KoopaNrvWait; mirror those two
        // stores for the fallback path as well. Both writes are part of the
        // supported 1.0.0 Koopa layout and are harmless when the controller
        // was never active.
        void* stopCtrl = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(actor) + 0x148);
        if (stopCtrl) {
            *reinterpret_cast<s32*>(stopCtrl) = -1;
            // 0xd4 is only 4-byte aligned in KoopaHackStopCtrl. Avoid an
            // unaligned 64-bit store on hardware while clearing the same
            // native u64 state used by Koopa's CancelHack branch.
            std::memset(reinterpret_cast<char*>(stopCtrl) + 0xd4, 0,
                sizeof(u64));
        }

        // A capture start intentionally invalidates every sensor and then
        // enables only the player-facing set. Reappearing the same actor does
        // not reset those per-sensor flags. Restore the *initial Wait* sensor
        // layout before the next Cappy hit: Koopa's authored init keeps Body
        // valid and invalidates player sensors until the hack starts. Leaving
        // the capture-time player sensors enabled makes Cappy address the
        // wrong receiver on the respawned actor.
        al::validateHitSensors(actor);
        al::invalidateHitSensorPlayerAll(actor);
        // Koopa receives Cappy's StartHack through its authored Body sensor.
        // Keep this explicit in addition to the keeper-wide reset: the
        // system-level dead transition and the per-sensor valid bit are
        // separate pieces of state in the 1.0.0 actor layout.
        al::validateHitSensor(actor, "Body");
        al::invalidateHitSensorPlayerAttackAll(actor);
        al::validateOcclusionQuery(actor);
        al::showSilhouetteModelIfHide(actor);
        al::startVisAnim(actor, "HackOff");
        setKoopaReplacementWaitNerve(actor);

        // LiveActor::makeActorAlive restores the actor's own system state, but
        // NerveStateBase::mIsDead belongs to the nested EnemyStateHackStart
        // object. A forced decapture can leave that byte set, which makes the
        // next StartHack message get ignored even though Koopa is in Wait.
        void* hackStart = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(actor) + 0x158);
        if (hackStart) {
            *reinterpret_cast<u8*>(reinterpret_cast<char*>(hackStart) + 0x10) = 0;
            // EnemyStateHackStart::kill() also clears its active
            // IUsePlayerHack/demo pointer at +0x20. A forced kill/appear can
            // bypass that nested kill path, so clear it before the next
            // StartHack message is delivered.
            std::memset(reinterpret_cast<char*>(hackStart) + 0x20, 0,
                sizeof(void*));
        }

        // LiveActor::makeActorDead also kills NerveStateBase children. Koopa's
        // concrete appear() restores the parent NerveKeeper, but it does not
        // call DemoStateHackFirst::appear(). Revive the state byte and clear
        // its one-shot wait counter so the ordinary post-respawn capture path
        // cannot inherit the dead/demo-complete state from the old life.
        void* demoState = *reinterpret_cast<void**>(
            reinterpret_cast<char*>(actor) + 0x160);
        if (demoState) {
            *reinterpret_cast<u8*>(reinterpret_cast<char*>(demoState) + 0x10) = 0;
            *reinterpret_cast<s32*>(reinterpret_cast<char*>(demoState) + 0x40) = 0;
        }

        // Koopa::exeWait establishes these on the first frame of its normal
        // nerve. Set them here as well because Cappy can hit the actor before
        // that movement tick after a delayed respawn.
        al::startAction(actor, "Wait");
        al::clearSklAnimInterpole(actor);
        *reinterpret_cast<u8*>(reinterpret_cast<char*>(actor) + 0x210) = 1;
    }

    static bool applyKoopaDeath(KoopaReplacementRecord* record) {
        if (!record || !record->respawnPending || !record->actor
            || record->deathApplied)
            return record && record->deathApplied;
        killKoopaReplacementActor(record->actor);
        if (!al::isDead(record->actor)) {
            // Keep the request pending if the actor's native kill path was
            // rejected by a transient hack/demo state. The stage tick will
            // retry on the next frame instead of starting the respawn timer
            // while Bowser is still visible.
            Randomizer::logPush(
                "enemy swap Koopa decapture death retry: actor=%p",
                record->actor);
            return false;
        }
        record->deathApplied = true;
        // Keep the actor dead long enough for the replacement to be
        // observable instead of killing and reappearing between two
        // rendered frames.
        record->respawnDelayFrames = 30;
        Randomizer::logPush(
            "enemy swap Koopa decapture death: actor=%p dead=%d",
            record->actor, al::isDead(record->actor));
        return true;
    }

    static void applyPendingKoopaDeaths() {
        for (s32 i = 0; i < sKoopaReplacementActorCount; i++)
            applyKoopaDeath(&sKoopaReplacementActors[i]);
    }

    static void reconcileKoopaCaptureState(StageScene* scene) {
        if (!scene)
            return;

        // The normal path is still handled by the message and escape hooks
        // below. This is a second, stage-wide observation point for paths
        // which clear PlayerHackKeeper::mHack without delivering a
        // CancelHack to Koopa (some scripted and transfer escapes do that).
        // Only actors which were positively observed entering a capture are
        // eligible, and an already-dead actor is never re-queued here.
        PlayerActorHakoniwa* player = (PlayerActorHakoniwa*)
            rs::getPlayerActor(scene);
        PlayerHackKeeper* keeper = player ? player->mHackKeeper : nullptr;
        al::LiveActor* activeHack = keeper ? keeper->getHack() : nullptr;
        for (s32 i = 0; i < sKoopaReplacementActorCount; i++) {
            KoopaReplacementRecord& record = sKoopaReplacementActors[i];
            if (!record.actor || record.respawnPending)
                continue;
            if (activeHack == record.actor) {
                // Also learn the capture state here. This keeps the fallback
                // independent of which StartHack message path the actor
                // used, including direct/scripted captures.
                record.captureActive = true;
                continue;
            }
            if (!record.captureActive)
                continue;
            if (al::isDead(record.actor)) {
                record.captureActive = false;
                continue;
            }

            Randomizer::logPush(
                "enemy swap Koopa decapture fallback: actor=%p active=%p",
                record.actor, activeHack);
            armKoopaRespawn(&record);
            applyKoopaDeath(&record);
        }
    }

    static void respawnPendingKoopas() {
        for (s32 i = 0; i < sKoopaReplacementActorCount; i++) {
            KoopaReplacementRecord& record = sKoopaReplacementActors[i];
            if (!record.respawnPending || !record.actor)
                continue;

            // Do the kill and appear on separate stage frames. The kill is
            // applied after the native cancel message/escape call returns, so
            // Koopa cannot overwrite the dead state while finishing its
            // CancelHack branch. Keep the actor allocated, kill it for a
            // visible interval, and only then reuse it.
            if (!record.deathApplied) {
                applyPendingKoopaDeaths();
                // The helper applies the death phase to every queued actor;
                // leave all of them dead until the next frame.
                break;
            }

            // Native stage/demo code can make an actor alive again while it
            // is still referenced by a hack transition. Reassert the
            // concrete dead transition until the delayed respawn begins so
            // Bowser cannot remain visible during this interval.
            if (!al::isDead(record.actor))
                killKoopaReplacementActor(record.actor);

            if (record.respawnDelayFrames > 0) {
                record.respawnDelayFrames--;
                continue;
            }

            record.respawnPending = false;
            record.deathApplied = false;
            clearKoopaDeadSwitch(record.actor);
            // Koopa::appear() calls LiveActor::makeActorAlive(), which calls
            // the game's native resetPosition() path. Re-apply the captured
            // world-space position when the pose keeper is available so a
            // zone-local placement cannot move the respawn to a different
            // origin. Avoid setQuat: the foreign Koopa can temporarily lack
            // the quaternion keeper during this transition.
            appearKoopaReplacementActor(record.actor);
            if (record.actor->getPoseKeeper())
                al::setTrans(record.actor, record.spawnTrans);
            resetKoopaReplacementCaptureState(record.actor);
            record.captureActive = false;
            Randomizer::logPush(
                "enemy swap Koopa decapture respawn: actor=%p x=%.1f "
                "y=%.1f z=%.1f",
                record.actor, record.spawnTrans.x, record.spawnTrans.y,
                record.spawnTrans.z);
        }
    }

    // Koopa::receiveMsg finishes its own hack state while handling
    // CancelHack, but it enters PlayerHackKeeper::endHack from inside that
    // handler. Arm the replacement before calling either original function;
    // otherwise the nested endHack hook runs before the old post-message code
    // marks the actor and misses the respawn entirely. Keep the real bool
    // return value as well: callers use it to decide whether the hack ended.
    HkTrampoline<bool, PlayerHackKeeper*, const HackEndParam*>
        playerHackEndHook = hk::hook::trampoline(
            [](PlayerHackKeeper* keeper, const HackEndParam* param) -> bool {
                KoopaReplacementRecord* record = keeper ? findKoopaReplacementRecord(keeper->getHack())
                                                        : nullptr;
                armKoopaRespawn(record);

                const bool result = playerHackEndHook.orig(keeper, param);
                // The request is processed by the outer escape hook or the
                // stage-control tick. Keep this hook focused on identifying a
                // successful endHack request; killing inside endHack would
                // still be inside Koopa's native message chain.
                if (!result)
                    disarmKoopaRespawn(record);
                return result;
            });

    // Koopa's native receiveMsg acknowledges HackInvalidEscape for every
    // instance. PlayerHackKeeper uses that message as a probe before sending
    // CancelHack, so the native response produces the rejection sound and
    // prevents a normal capture from being left. Native Koopa must retain
    // that behavior for the authored Moon Kingdom/final-escape sequence;
    // replacement Koopas are ordinary captures and must decline the probe so
    // the existing cancel path can run.
    HkTrampoline<bool, al::LiveActor*, const al::SensorMsg*,
        al::HitSensor*, al::HitSensor*>
        koopaReceiveMsgHook = hk::hook::trampoline(
            [](al::LiveActor* actor, const al::SensorMsg* msg,
                al::HitSensor* target, al::HitSensor* source) -> bool {
                KoopaReplacementRecord* record = findKoopaReplacementRecord(actor);
                const bool isStartHack = isMsgStartHackRuntime(msg);
                const bool isCancelHack = isMsgCancelHackRuntime(msg);
                if (record && isStartHack) {
                    // Stage/demo code can finish the respawn transition one
                    // tick after Koopa::appear(). Re-apply the ordinary Wait
                    // capture state at the message boundary as well, so a
                    // Cappy hit cannot arrive while the old hack-start state
                    // is still marked dead or the old sensor mask is active.
                    const void* activeHack = *reinterpret_cast<void**>(
                        reinterpret_cast<char*>(actor) + 0x110);
                    if (!activeHack)
                        resetKoopaReplacementCaptureState(actor);
                }
                // Native Koopa calls rs::endHack from inside its CancelHack
                // branch. Mark the actor before entering native code so the
                // nested PlayerHackKeeper::endHack hook can see it.
                if (record && isCancelHack)
                    armKoopaRespawn(record);

                // Native Koopa only handles its authored KoopaHackDamage
                // message while captured. Replacement Koopas are ordinary
                // captures, so mirror BrosHackBase's generic capture damage
                // path for enemy attacks and the standard hacker damage
                // message. Keep the pointer and record checks narrow so the
                // story/final-escape Koopa path remains completely native.
                if (record && msg) {
                    IUsePlayerHack* activeHack = *reinterpret_cast<IUsePlayerHack**>(
                        reinterpret_cast<char*>(actor) + 0x110);
                    const bool isEnemyDamage = isMsgHackerDamageAndCancelRuntime(msg)
                        || isMsgEnemyAttackRuntime(msg)
                        || isMsgHammerBrosHammerEnemyAttackRuntime(msg);
                    if (activeHack && isEnemyDamage) {
                        const bool damaged = requestDamageRuntime(activeHack);
                        if (damaged && source && target)
                            rs::requestHitReactionToAttacker(
                                msg, source, target);
                        return damaged;
                    }
                }

                bool result = (record && msg
                                  && isMsgHackInvalidEscapeRuntime(msg))
                    ? false
                    : koopaReceiveMsgHook.orig(
                          actor, msg, target, source);
                bool forcedStart = false;
                if (record && isStartHack) {
                    // A replacement can return true from a cached/demo
                    // branch without installing the ordinary
                    // PlayerHackKeeper object. Treat the pointer, rather
                    // than only the bool return, as the authoritative
                    // capture result. If it is absent, replay the native
                    // EnemyStateHackStart path so the respawned Bowser is a
                    // real capturable actor and not just a visible model.
                    const void* activeHack = *reinterpret_cast<void**>(
                        reinterpret_cast<char*>(actor) + 0x110);
                    if (!activeHack) {
                        if (forceKoopaReplacementStartHack(
                                actor, msg, target, source)) {
                            result = true;
                            forcedStart = true;
                            Randomizer::logPush(
                                "enemy swap Koopa StartHack forced: actor=%p",
                                actor);
                        } else {
                            // Do not mark a capture active when native code
                            // acknowledged the message but the replacement
                            // still has no hack object to own or release.
                            result = false;
                            if (sKoopaCaptureTraceCount < 32)
                                Randomizer::logPush(
                                    "enemy swap Koopa StartHack failed: "
                                    "actor=%p no active hack",
                                    actor);
                        }
                    }
                }
                if (record && isStartHack && sKoopaCaptureTraceCount < 32) {
                    const void* activeHack = *reinterpret_cast<void**>(
                        reinterpret_cast<char*>(actor) + 0x110);
                    const void* hackStart = *reinterpret_cast<void**>(
                        reinterpret_cast<char*>(actor) + 0x158);
                    const void* demoState = *reinterpret_cast<void**>(
                        reinterpret_cast<char*>(actor) + 0x160);
                    Randomizer::logPush(
                        "enemy swap Koopa StartHack: actor=%p result=%d "
                        "forced=%d dead=%d activeHack=%p hackDead=%d "
                        "demoDead=%d",
                        actor, result ? 1 : 0,
                        forcedStart ? 1 : 0, al::isDead(actor) ? 1 : 0,
                        activeHack,
                        hackStart
                            ? *reinterpret_cast<const u8*>(
                                  reinterpret_cast<const char*>(hackStart)
                                  + 0x10)
                            : 0xff,
                        demoState
                            ? *reinterpret_cast<const u8*>(
                                  reinterpret_cast<const char*>(demoState)
                                  + 0x10)
                            : 0xff);
                    sKoopaCaptureTraceCount++;
                }
                if (record && isCancelHack) {
                    if (result) {
                        // Koopa::receiveMsg has fully finished its native
                        // CancelHack branch at this point. Apply the death
                        // now so the actor is definitely gone before control
                        // returns to PlayerHackKeeper::tryEscapeHack.
                        applyKoopaDeath(record);
                    } else
                        disarmKoopaRespawn(record);
                }
                if (record && result && isStartHack)
                    record->captureActive = true;
                return result;
            });

    // PlayerHackKeeper::tryEscapeHack is the outermost operation for the
    // normal player-triggered decapture path. Its SDK declaration is missing
    // the native bool return, but the 1.0.0 caller tests W0 after this call.
    // Save the actor before native endHack clears the keeper, then queue only
    // after the native operation actually reports success.
    HkTrampoline<bool, PlayerHackKeeper*> playerTryEscapeHackHook = hk::hook::trampoline(
        [](PlayerHackKeeper* keeper) -> bool {
            al::LiveActor* actor = keeper ? keeper->getHack() : nullptr;
            const bool result = playerTryEscapeHackHook.orig(keeper);
            KoopaReplacementRecord* record = findKoopaReplacementRecord(actor);
            if (result) {
                armKoopaRespawn(record);
                // This is the outermost successful escape path. Apply
                // the native Koopa death here as a fallback even if the
                // message-dispatch hook did not observe CancelHack.
                applyKoopaDeath(record);
            } else
                disarmKoopaRespawn(record);
            return result;
        });

    // Koopa::init() calls al::validateDepthShadowMap(this, "Body", ...) very
    // early, then al::invalidateDepthShadowMap(this, "BodyHack"/
    // "GroundHack", ...). All three search a per-actor depth-shadow catalog
    // (DepthShadowMapCtrl::tryFindDepthShadowMapInfo); on a foreign
    // placement that catalog comes back effectively empty and the search
    // crashes inside al::isEqualString comparing against a null entry. This
    // is purely cosmetic per-bodypart shadow tuning -- Koopa still casts its
    // normal actor shadow without it -- so skipping these calls avoids the
    // crash without needing to know why the catalog is empty. Neither
    // function's return value is used by any caller in Koopa::init() (both
    // calls are bare statements), so returning null when skipped is safe.
    HkTrampoline<void*, al::LiveActor*, const char*> validateDepthShadowMapHook = hk::hook::trampoline([](al::LiveActor* actor, const char* name) -> void* {
        if (isKoopaReplacementActive() || actor == sKoopaWithoutDepthShadowMap)
            return nullptr;
        return validateDepthShadowMapHook.orig(actor, name);
    });

    HkTrampoline<void*, al::LiveActor*, const char*> invalidateDepthShadowMapHook = hk::hook::trampoline([](al::LiveActor* actor, const char* name) -> void* {
        if (isKoopaReplacementActive() || actor == sKoopaWithoutDepthShadowMap)
            return nullptr;
        return invalidateDepthShadowMapHook.orig(actor, name);
    });

    // Same DepthShadowMapCtrl::tryFindDepthShadowMapInfo crash as the two
    // hooks above (confirmed via disassembly: identical actor+0xD0 -> +8
    // catalog lookup), just a third function in the same family that's
    // called later in Koopa::init() -- this one from inside
    // HackerDepthShadowMapCtrl's constructor, for the "GroundHack" body
    // part. Same fix, same reasoning: the catalog is empty on a foreign
    // placement, skip the search entirely.
    HkTrampoline<void*, al::LiveActor*, const char*, bool> setEnableDepthShadowMapBottomGradationHook = hk::hook::trampoline([](al::LiveActor* actor, const char* name, bool enable) -> void* {
        if (isKoopaReplacementActive()) {
            sKoopaWithoutDepthShadowMap = actor;
            return nullptr;
        }
        return setEnableDepthShadowMapBottomGradationHook.orig(actor, name, enable);
    });

    // Koopa::control() calls HackerDepthShadowMapCtrl::update every frame.
    // The controller keeps the actor and the "GroundHack" map name from its
    // constructor, then calls al::getDepthShadowMapLength.  That lookup is
    // unsafe for a swapped Koopa because its foreign placement left the
    // map catalogue empty (the crash reported at al::isEqualString).  This
    // controller only adjusts cosmetic depth-shadow length, so bypass it for
    // the one actor whose catalogue was deliberately disabled above.
    // HackerDepthShadowMapCtrl's first field is its al::LiveActor*.
    static al::LiveActor* getHackerDepthShadowMapCtrlActor(const void* ctrl) {
        return ctrl ? *reinterpret_cast<al::LiveActor* const*>(ctrl) : nullptr;
    }

    HkTrampoline<void, void*, void*> hackerDepthShadowMapCtrlUpdateHook = hk::hook::trampoline([](void* ctrl, void* playerCollider) -> void {
        if (sKoopaWithoutDepthShadowMap
            && getHackerDepthShadowMapCtrlActor(ctrl) == sKoopaWithoutDepthShadowMap)
            return;
        hackerDepthShadowMapCtrlUpdateHook.orig(ctrl, playerCollider);
    });

    // Koopa::init() calls both of the two functions below twice each: once
    // legitimately for Koopa itself (whose resources resolve fine through
    // the normal swap hooks), and once for a sub-actor fetched via
    // al::getSubActor whose own archive isn't fully present on a foreign
    // placement -- that second call crashes inside
    // al::isExistModelResourceYaml. There's no argument-level way to tell
    // these two calls apart (both pass a valid al::LiveActor*), so this
    // remembers which actor pointer was seen FIRST for the current Koopa
    // creation and treats it as the legitimate one; any different actor seen
    // while the same scope is still active is assumed to be the crash-prone
    // sub-actor case.
    //
    // An earlier version of this fix unconditionally forced
    // calcInitFileSpringControlJointNum to 0 for both calls, which broke the
    // legitimate first call: it under-allocated
    // al::initJointControllerKeeper's capacity (sized from that count), and
    // JointSpringControllerHolder::init -- called right after with the real
    // resource, untouched by that first fix -- then overflowed the
    // under-sized controller array pushing the real controllers in.
    static al::LiveActor* sKoopaJointPrimaryActor = nullptr;
    static const al::PlacementInfo* sKoopaJointScopePlacement = nullptr;

    static bool isSecondaryKoopaJointActor(al::LiveActor* actor) {
        if (!isKoopaReplacementActive())
            return false;
        if (sScope.placement != sKoopaJointScopePlacement) {
            sKoopaJointScopePlacement = sScope.placement;
            sKoopaJointPrimaryActor = actor;
            return false;
        }
        return actor != sKoopaJointPrimaryActor;
    }

    // Confirmed via raw disassembly (not the pseudocode, which mislabeled
    // the first argument's type): called with exactly 2 real arguments
    // (LiveActor* actor in X0, const char* key in X1), no JointSpring-
    // ControllerHolder instance despite living in that class -- its purpose
    // is computing a count before any holder instance exists yet. Its own
    // body already has a graceful fallback (returns 0 if
    // al::isExistModelResourceYaml says the resource doesn't exist), so
    // returning 0 for the sub-actor case reaches that same documented result
    // by a safer path rather than skipping real behavior.
    HkTrampoline<s32, al::LiveActor*, const char*> calcInitFileSpringControlJointNumHook = hk::hook::trampoline([](al::LiveActor* actor, const char* name) -> s32 {
        if (isSecondaryKoopaJointActor(actor))
            return 0;
        return calcInitFileSpringControlJointNumHook.orig(actor, name);
    });

    // Unlike the count function above, this one really is an instance
    // method (confirmed via disassembly: Holder* this in X0, LiveActor* in
    // X1, const char* key in X2). Skipping it for the sub-actor case leaves
    // that sub-actor's JointSpringControllerHolder default-constructed with
    // zero controllers, which is exactly what it would end up with anyway
    // now that the count call above reports 0 controllers for the same case.
    HkTrampoline<void, al::JointSpringControllerHolder*, al::LiveActor*, const char*>
        jointSpringControllerHolderInitHook = hk::hook::trampoline([](al::JointSpringControllerHolder* holder, al::LiveActor* actor,
                                                                       const char* name) -> void {
            if (isSecondaryKoopaJointActor(actor))
                return;
            jointSpringControllerHolderInitHook.orig(holder, actor, name);
        });

    // Koopa::init() also calls al::initJointControllerKeeper(actor, count)
    // directly (once for itself with a real count, once for the same
    // sub-actor as above with whatever count the hook above now reports).
    // The crash here is one level deeper than a bad count: it's inside
    // JointControllerKeeper's own constructor, which dereferences a chain of
    // pointers hanging off the actor -- consistent with that sub-actor
    // itself being in the same incomplete state every other resource lookup
    // on it has hit. Nothing later in Koopa::init() registers additional
    // joint controllers onto this specific sub-actor (only onto Koopa's own
    // `this`), so leaving it without a JointControllerKeeper entirely is
    // safe here, matching the same "just don't touch the broken sub-actor"
    // approach as the two hooks above.
    //
    // Separately: for Koopa's own (primary) actor, this capacity is later
    // exhausted anyway -- al::registerJointController's "Spine" call (right
    // after JointSpringControllerHolder::init populates the counted spring
    // controllers) crashes inside JointControllerKeeper::pushBackController,
    // which unconditionally dereferences its free-list head with no null
    // check once that list is empty. Traced every controller push that
    // lands on this same keeper (JointSpringControllerHolder::init calls
    // al::initJointSpringController once per named YAML entry, which itself
    // calls pushBackController exactly once) and the accounting should fit
    // exactly in `count + 1` capacity -- so this one I can't fully explain:
    // either something else also pushes onto this keeper that I haven't
    // traced, or al::getJointIndex fails to resolve "Spine" on whatever
    // skeleton actually ends up loaded and corrupts state on the way in.
    // Padding the requested capacity is a pragmatic safety margin against
    // the crash mechanism (free-list exhaustion) rather than a fix for a
    // fully understood root cause like the other hooks in this file -- worth
    // flagging as the least certain of this batch.
    HkTrampoline<void, al::LiveActor*, s32> initJointControllerKeeperHook = hk::hook::trampoline([](al::LiveActor* actor, s32 count) -> void {
        if (isSecondaryKoopaJointActor(actor))
            return;
        if (isKoopaReplacementActive())
            count += 16;
        initJointControllerKeeperHook.orig(actor, count);
    });

    // Koopa::init() unconditionally creates a PeachOnKoopaShoulder sub-actor
    // (the ending-sequence Peach-riding-on-Bowser's-shoulder visual) via
    // al::PartsModel::initPartsSuffix -> al::registerSubActor ->
    // al::SubActorKeeper::registerSubActor -> sead::FrameHeap::tryAlloc,
    // which crashes on a foreign placement. Every later use of the resulting
    // pointer in Koopa (startCarried/validateAppearTiaraEyes/startDead) is
    // already null-guarded (`if (this+45) ...`/`if (a2) ...`), so skipping
    // creation entirely for a scoped Koopa replacement is safe -- the class
    // already treats "no Peach shoulder" as a normal, handled state. Unlike
    // the depth-shadow hooks above, this one does receive an ActorInitInfo,
    // so it uses the same isScoped(initInfo) pattern as the other hooks.
    HkTrampoline<PeachOnKoopaShoulder*, al::LiveActor*, const al::ActorInitInfo&,
        const PeachOnKoopaAnimInfo*, const void*>
        peachOnKoopaShoulderCreateHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& initInfo,
                                                                  const PeachOnKoopaAnimInfo* animInfo, const void* mtx) -> PeachOnKoopaShoulder* {
            if (isScopedKoopaReplacement(initInfo))
                return nullptr;
            return peachOnKoopaShoulderCreateHook.orig(actor, initInfo, animInfo, mtx);
        });

    // GraphicsSystemInfo gives ModelUBO a fixed 29 MiB arena and
    // ShaderOptionUBO a fixed 3 MiB arena. Both are monotonic. Chaos swapping
    // creates model combinations that the authored stage budget never had to
    // cover, so either pool can return a null GPUMemBlock. Neither ModelCtrl
    // nor ModelShader checks that result before initializeGfxMemoryPool.
    //
    // The reported WorldMapStage crash is specifically ModelUBO:
    // createModelAdditionalInfoUniformBlockAssign gets this UniformBlock's
    // backing storage from ModelUBO, and tryUpdateModelAdditionalInfoUbo
    // reaches setData_ with a null mapped destination after that allocation
    // fails. The old workaround enlarged only ShaderOptionUBO and therefore
    // could not affect that fault.
    //
    // Grow the arena when GraphicsSystemInfo creates it, before any address
    // from the block has been handed out. This preserves every randomized
    // model and shader; falling back to vanilla actors or dropping draw passes
    // would only hide the same capacity defect by removing visible features.
    static constexpr s32 kChaosModelUboHeadroom = 32 * 1024 * 1024;
    static constexpr s32 kChaosShaderOptionUboHeadroom = 8 * 1024 * 1024;
    HkTrampoline<void, al::GpuMemAllocator*, const char*, s32, sead::Heap*,
        s32, agl::MemoryAttribute>
        createGpuMemoryHook = hk::hook::trampoline(
            [](al::GpuMemAllocator* allocator, const char* name, s32 size,
                sead::Heap* heap, s32 alignment,
                agl::MemoryAttribute attribute) -> void {
                s32 headroom = 0;
                if (name && std::strcmp(name, "ModelUBO") == 0)
                    headroom = kChaosModelUboHeadroom;
                else if (name
                    && (std::strcmp(name, "ShaderOptionUBO") == 0
                        || std::strcmp(name, "ShaderOptionUBODebug") == 0))
                    headroom = kChaosShaderOptionUboHeadroom;
                if (headroom > 0 && size > 0
                    && size <= 0x7fffffff - headroom) {
                    size += headroom;
                }
                createGpuMemoryHook.orig(
                    allocator, name, size, heap, alignment, attribute);
            });

    void installHooks() {
        // Present in the game binary but absent from syms/main.sym.
        // ida_context/Project/Draw/GpuMemAllocator.md
        createGpuMemoryHook.installAtMainOffset(0x00a4fb20);
        createPlacementActorHook.installAtSym<"_ZN2al31createPlacementActorFromFactoryERKNS_13ActorInitInfoEPKNS_13PlacementInfoE">();
        tryGetClassNameHook.installAtSym<"_ZN2al15tryGetClassNameEPPKcRKNS_13ActorInitInfoE">();

        // ida_context/Npc/Squirrel.md and
        // ida_context/Library/Collision/PartsConnectorUtil.md. The helper
        // guard is active only inside Squirrel::initAfterPlacement.
        attachMtxConnectorToCollisionPartsHook.installAtMainOffset(0x00857fc4);
        squirrelInitAfterPlacementHook.installAtMainOffset(0x003db074);

        // ida_context/Library/Camera/CameraDirector.md
        // (al::CameraDirector::createCameraFromFactory(char const*,
        //  al::PlacementId const*, char const*, int, sead::Matrix34<float> const&))
        createCameraFromFactoryHook.installAtMainOffset(0x00831744);

        // Not in syms/main.sym; offsets from ida_context/Library/Placement/
        // PlacementFunction.md (ActorInitInfo and PlacementInfo overloads).
        getObjectNameHook.installAtMainOffset(0x0099e5cc);
        getPlacementObjectNameHook.installAtMainOffset(0x0099e628);
        tryGetPlacementObjectNameHook.installAtMainOffset(0x0099e688);
        tryGetObjectNameHook.installAtMainOffset(0x0099e6f4);
        // ActorFactory selects the creator by ClassName and passes DisplayName
        // to that creator. Keep both overload families scoped to the complete
        // replacement identity as well.
        getDisplayNameHook.installAtMainOffset(0x0099eb28);
        tryGetDisplayNameHook.installAtMainOffset(0x0099eb30);
        getPlacementDisplayNameHook.installAtMainOffset(0x0099eb38);
        tryGetPlacementDisplayNameHook.installAtMainOffset(0x0099eb3c);
        getClassNameHook.installAtMainOffset(0x0099ea88);
        tryGetPlacementClassNameHook.installAtMainOffset(0x0099e970);
        getPlacementClassNameHook.installAtMainOffset(0x0099ea90);
        isClassNameHook.installAtMainOffset(0x0099ea94);
        isPlacementClassNameHook.installAtMainOffset(0x0099eadc);
        isObjectNameHook.installAtMainOffset(0x0099e7c0);
        isPlacementObjectNameHook.installAtMainOffset(0x0099e82c);
        isObjectNameSubStrHook.installAtMainOffset(0x0099e894);
        isPlacementObjectNameSubStrHook.installAtMainOffset(0x0099e900);
        getModelNameHook.installAtMainOffset(0x009a31d8);
        getPlacementModelNameHook.installAtMainOffset(0x009a3270);
        tryGetPlacementModelNameHook.installAtMainOffset(0x009a3308);
        tryGetModelNameHook.installAtMainOffset(0x009a33ac);
        // ida_context/Library/KeyPose/KeyPoseKeeper.md
        createKeyPoseKeeperHook.installAtMainOffset(0x008afeb0);
        // ida_context/Util/PlayerPuppetFunction.md and
        // ida_context/Library/LiveActor/LiveActorFunction.md. The depth scope
        // keeps the indexed fallback exclusive to cap-return hit reactions.
        forcePutOnPuppetCapWithEffectHook.installAtMainOffset(0x0056ed64);
        getSubActorByIndexHook.installAtSym<
            "_ZN2al11getSubActorEPKNS_9LiveActorEi">();
        // ida_context/Library/LiveActor/ActorPoseUtil.md. Full-actor prop
        // replacements can inherit a different pose-keeper representation
        // from their source placement.
        setQuatHook.installAtMainOffset(0x008ef5fc);
        setRotateYHook.installAtMainOffset(0x008eee0c);
        // ida_context/Util/NpcAnimUtil.md (rs::setNpcMaterialAnimFromPlacementInfo)
        setNpcMaterialAnimHook.installAtMainOffset(0x00557bb4);
        // ida_context/Util/NpcAnimUtil.md (rs::tryApplyNpcMaterialAnimPresetFromPlacementInfo)
        applyNpcMaterialAnimPresetHook.installAtMainOffset(0x00557b54);
        // ida_context/Library/Placement/PlacementFunction.md
        // (al::getStringArg(char const**, al::ActorInitInfo const&, char const*))
        getStringArgHook.installAtMainOffset(0x009a08e8);
        // ida_context/Library/Placement/PlacementFunction.md
        // (al::getStringArg(al::ActorInitInfo const&, char const*) -- the
        // direct-return overload, distinct from the out-param one above)
        getStringArgValueHook.installAtMainOffset(0x009a0a58);
        // ida_context/Library/Placement/PlacementFunction.md
        // (al::tryGetStringArg(char const**, al::ActorInitInfo const&,
        // char const*))
        tryGetStringArgHook.installAtMainOffset(0x009a0b70);
        // TalkNpc::init reads the source placement's ShadowLength through the
        // int overload; replacement NPCs must retain the target model's
        // default when that source-only tuning is present.
        tryGetIntArgHook.installAtMainOffset(0x009a06d4);
        // TalkNpc's eye-sensor tuning and ImomuEnemyState's optional EnemyCap
        // creation use the bool overload of al::tryGetArg.
        tryGetBoolArgHook.installAtSym<
            "_ZN2al9tryGetArgEPbRKNS_13ActorInitInfoEPKc">();
        // ida_context/Demo/DemoStateHackFirst.md
        // (DemoStateHackFirst::isEnableShowHackDemo(void) const)
        isEnableShowHackDemoHook.installAtMainOffset(0x000d4840);
        // (DemoStateHackFirst::tryHackFirstDemoWait / tryHackFirst)
        tryHackFirstDemoWaitHook.installAtMainOffset(0x000d47ac);
        tryHackFirstHook.installAtMainOffset(0x000d4914);
        // ida_context/System/GameDataUtil.md
        // (rs::isStartKoopaCapture / rs::setFlagOnStartKoopaCapture)
        isStartKoopaCaptureHook.installAtMainOffset(0x005337b4);
        setFlagOnStartKoopaCaptureHook.installAtMainOffset(0x00533790);
        // ida_context/Boss/Koopa/Koopa.md
        // (Koopa::receiveMsg)
        koopaReceiveMsgHook.installAtMainOffset(0x0007ed3c);
        // ida_context/Player/PlayerHackKeeper.md
        // (PlayerHackKeeper::endHack)
        playerHackEndHook.installAtMainOffset(0x004491e0);
        // (PlayerHackKeeper::tryEscapeHack)
        playerTryEscapeHackHook.installAtMainOffset(0x004497c4);
        // ida_context/Library/Shadow/ActorShadowFunction.md
        // (al::validateDepthShadowMap(al::LiveActor*, char const*) /
        // al::invalidateDepthShadowMap(al::LiveActor*, char const*))
        validateDepthShadowMapHook.installAtMainOffset(0x00a13b1c);
        invalidateDepthShadowMapHook.installAtMainOffset(0x00a13c40);
        // ida_context/Library/Shadow/ActorShadowFunction.md
        // (al::setEnableDepthShadowMapBottomGradation(al::LiveActor const*,
        // char const*, bool))
        setEnableDepthShadowMapBottomGradationHook.installAtMainOffset(0x00a14a30);
        // ida_context/Enemy/HackerDepthShadowMapCtrl.md
        // (HackerDepthShadowMapCtrl::update(PlayerCollider*))
        hackerDepthShadowMapCtrlUpdateHook.installAtMainOffset(0x00110410);
        // ida_context/Library/Joint/JointSpringControllerHolder.md
        // (al::JointSpringControllerHolder::calcInitFileSpringControlJointNum
        // (al::LiveActor const*, char const*))
        calcInitFileSpringControlJointNumHook.installAtMainOffset(0x008af6a4);
        // ida_context/Library/Joint/JointSpringControllerHolder.md
        // (al::JointSpringControllerHolder::init(al::LiveActor*, char const*))
        jointSpringControllerHolderInitHook.installAtMainOffset(0x008aefe0);
        // ida_context/Library/Joint/JointControllerKeeper.md
        // (al::initJointControllerKeeper(al::LiveActor const*, int))
        initJointControllerKeeperHook.installAtMainOffset(0x008a86ac);
        // ida_context/Boss/Koopa/Koopa.md
        // (PeachOnKoopaShoulder::create(al::LiveActor*, al::ActorInitInfo
        // const&, PeachOnKoopaAnimInfo const*, sead::Matrix34<float> const*))
        peachOnKoopaShoulderCreateHook.installAtMainOffset(0x003ae0c8);
        // ida_context/Library/Event/EventFlowDataHolder.md
        // (al::startEventActionAtRandomFrame(al::LiveActor*, al::IUseEventFlowData const*, char const*))
        startEventActionAtRandomFrameHook.installAtMainOffset(0x0089015c);
        // ida_context/Library/LiveActor/ActorAnimFunction.md
        // (al::getSklAnimFrameMax(al::LiveActor const*, int) -- the wrapper
        // that resolves the model keeper's AnimPlayerSkl and tail-calls the
        // unguarded member, which is where the wander crash landed)
        getSklAnimFrameMaxHook.installAtMainOffset(0x008d3ea0);
        // ida_context/Util/PlayerUtil.md (rs::isNeedCreateNoseNeedle)
        needCreateNoseNeedleHook.installAtMainOffset(0x0056f3d0);
        // ida_context/Util/PlayerUtil.md (rs::isPlayer2D). SphinxRide's event
        // flow can reach it before the replacement actor's SceneInfo has a
        // PlayerHolder; the scoped hook above preserves the flow and answers
        // only that initialization-time query safely.
        isPlayer2DHook.installAtMainOffset(0x00570a1c);
        Randomizer::logPush(
            "enemy swap hooks installed: placement factory and identity readers");
    }

    void update(StageScene* scene) {
        reconcileKoopaCaptureState(scene);
        respawnPendingKoopas();
    }

} // namespace EnemySwapHooks
