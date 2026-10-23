#include "Randomizer/TrackerBridge.h"

#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/HttpsClient.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/WorldMeta.h"

#include "Library/Base/StringUtil.h"
#include "Scene/StageScene.h"
#include "Sequence/HakoniwaSequence.h"
#include "System/GameSystem.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Thread/AsyncFunctorThread.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "helpers.hpp"
#include "sead/heap/seadExpHeap.h"
#include "sead/heap/seadHeapMgr.h"
#include "sead/thread/seadAtomic.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <nn/fs.h>
#include <nn/util.h>

namespace TrackerBridge {
    namespace {
        // Own dedicated heap for cloud-upload thread creation, sized generously
        // for that alone (a single CloudUploadThread needs roughly 33KB - see
        // its stack-size comment - plus ExpHeap bookkeeping overhead).
        //
        // This exists because sead::HeapMgr's "current heap" is a *scope*, not
        // a permanent handle - something pushes it during normal gameplay
        // (stage loading, actor updates), but the pause menu's own button-
        // press handling never needed to allocate, so nothing ever pushes one
        // there. Every allocation attempted from sendCloud() (which fires from
        // either the pause menu or the per-transition hook) found no current
        // heap at all, regardless of how small the request was shrunk to -
        // confirmed via TrackerBridgeDebugOverlay showing "no current heap" at
        // the exact moment of failure, not a real size/fragmentation limit.
        // Same problem, same fix Multiplayer already uses for its own gHeap.
        sead::Heap* sTrackerHeap = nullptr;
        constexpr size_t kTrackerHeapSize = 0x20000; // 128KB

        // Set via the "Tracker Live Testing" Mod Config menu entry (see
        // StageSceneStateModConfig), which owns its own Keyboard instance and is
        // only ever reached from a deliberate player action in the pause menu -
        // safe timing that's already proven to work there for Seed/Moon
        // Requirement. Do NOT open a Keyboard directly from here: notifyTransition
        // fires from inside changeNextStageHook's finish(), a hot per-transition
        // hook, and an earlier attempt to lazily prompt from that hook crashed
        // (Keyboard::Keyboard() dereferences Multiplayer's gHeap, which isn't
        // reliably valid at that point - confirmed via crash log, matches the
        // Dev's report that the Multiplayer reintegration is shaky).
        char sRoomCode[64] = {};

        // No hardcoded fallback - an unset room code is genuinely empty until
        // the player sets one (save-file creation wizard, or the in-game
        // Tracker Live Testing page). checkAndSendProgress()/sendCloud() both
        // skip uploading entirely while this is empty (see their own guards)
        // rather than silently spamming a room nobody asked for.
        const char* effectiveRoomCode() { return sRoomCode; }

        // Set from the cloud-upload background thread, read from the main
        // thread for display (Tracker Live Testing's status row) - a torn
        // read for one frame is a cosmetic non-issue, not worth a lock for.
        // Also set from sendCloud() itself (main thread) for failures that
        // never make it to a background thread at all (alloc failure, all
        // slots busy).
        char sCloudStatusText[128] = "Not attempted yet";

        // Last progress snapshot actually *dispatched* (see
        // checkAndSendProgress()) - the diff-check baseline for "did
        // anything change since the last attempt". File-scope (not
        // function-local) so TrackerBridgeDebugOverlay can show them
        // directly: a screenshot showing the right bits already computed
        // here, but not reflected on the web tracker, isolates the bug to
        // delivery/decode rather than game-state computation - and vice
        // versa.
        //
        // Deliberately NOT the same thing as "confirmed delivered" - see
        // sLastProgressSendOk below for why that distinction matters.
        char sLastMoons[128] = "";
        char sLastReqs[128] = "";
        char sLastCaptures[64] = "";
        // One character per AbilityId minus CapReturnJump, plus the terminator -
        // sized off the enum so appending an ability cannot quietly overflow it.
        char sLastAbilities[AbilityRando::AbilityId_Count + 1] = "";
        char sLastKingdomStatus[128] = "";

        // Whether the most recent progress dispatch is known to have
        // actually landed. Set from the cloud-upload background thread (see
        // sendToFirestore) - same torn-read tolerance as sCloudStatusText
        // above. Starts true so the very first-ever send (sLast* still all
        // empty, guaranteed to differ from real data) takes the normal
        // "something changed" path below rather than the retry-cooldown one.
        //
        // Exists because a rejected write (e.g. firestore.rules not yet
        // allowing a newly-added field) must not go unretried forever just
        // because nothing else in-game happens to change afterward - the
        // data that failed to send is still exactly what needs sending.
        bool sLastProgressSendOk = true;
        s32 sProgressRetryCooldown = 0;
        constexpr s32 kProgressRetryCooldownFrames = 180; // ~3s at 60fps

        void setCloudStatus(const char* fmt, ...) {
            va_list args;
            va_start(args, fmt);
            nn::util::VSNPrintf(sCloudStatusText, sizeof(sCloudStatusText), fmt, args);
            va_end(args);
        }

        // HTTPS straight to Firestore, no local relay process needed -
        // resolved browser-side by firebase-live-sync.js instead. A TLS
        // handshake over the real internet is much slower than a LAN round
        // trip, and notifyTransition fires from inside a hot per-transition
        // gameplay hook - doing this synchronously there would risk a
        // visible hitch every time a door is used. Uploads run on a small
        // background thread pool instead (see kCloudUploadSlots below); if
        // every slot is already busy (the player is transitioning faster
        // than uploads can complete), the event is queued rather than
        // dropped (see the ring-buffer comment further down) - only truly
        // overflowing that queue drops anything now.
        //
        // Fill in kFirestoreProjectId after creating the Firebase project -
        // same placeholder as firebase-live-sync.js's FIREBASE_CONFIG.projectId,
        // see the tracker README's "Cloud Live Tracking" section.
        constexpr const char* kFirestoreProjectId = "smo-randomizer-tracker";
        constexpr const char* kFirestoreHost = "firestore.googleapis.com";
        // Google's frontend serving infra routes by Host header/SNI rather than
        // destination IP (see HttpsClient.h), so this is just A resolved IP for
        // that infra, not specifically tied to this project - the same one
        // BTT-Studio-Skybox-Build's SkyboxHooks.cpp already uses successfully
        // for its own Firestore calls. May need refreshing if Google's frontend
        // IPs rotate out from under it.
        constexpr const char* kFirestoreIp = "108.177.122.95";

        // Progress (moons/captures/abilities) events reuse the same struct and
        // upload plumbing as door transitions - they just repurpose from/to/
        // debugArrivalDoorId to carry moons/captures/abilities snapshot
        // strings instead (see checkAndSendProgress()). A whole second
        // thread/queue/heap-budget just for this would duplicate everything
        // already proven to work for transitions, for data that doesn't even
        // need per-event delivery guarantees (a dropped progress snapshot is
        // superseded by the next changed frame's send, unlike a transition).
        enum class EventKind
        {
            Transition,
            Progress
        };

        struct CloudEvent {
            EventKind kind = EventKind::Transition;
            char roomCode[64];
            char exitId[128];
            char from[128];
            char to[128];
            char debugArrivalDoorId[128];
            // Progress-kind only: per-kingdom unlocked/peace/moon-rock flags
            // (see checkAndSendProgress()) - a dedicated field rather than a
            // 5th repurposed one, since every other field was already
            // claimed by moons/moonReqs/captures/abilities.
            char kingdomStatus[128];
        };

        void copyField(char* dst, size_t dstSize, const char* src) {
            std::strncpy(dst, src ? src : "", dstSize - 1);
            dst[dstSize - 1] = '\0';
        }

        // Small lock-free ring buffer for events that arrive while the
        // single upload slot is already busy - see kCloudUploadSlots below
        // for why there's only one slot. Without this, walking straight
        // through a short room (in one door, out the other, both real
        // shuffled transitions) fires two events well within one Firestore
        // TLS round-trip, and the second - the player's actual final resting
        // position - used to be silently dropped, leaving the live tracker
        // stuck showing the first hop as "current" forever. Each CloudEvent
        // is small (~576 bytes), so even this holds under 5KB - trivial next
        // to kTrackerHeapSize, nowhere near what caused the earlier heap
        // issues (those were 32KB+ background-thread stack allocations, not
        // this, and this is plain static storage, not heap-allocated at all).
        //
        // sead::CriticalSection isn't actually linkable on this target (same
        // class of SDK-declared-but-not-backed-by-a-real-import gap this
        // project has hit repeatedly elsewhere) - sead::Atomic is a plain
        // header-only wrapper around std::atomic instead, so a classic
        // single-producer (sendCloud() on the main thread) / single-consumer
        // (a CloudUploadThread's own background thread) lock-free ring
        // buffer works without needing any lock at all: mHead is only ever
        // written by the consumer, mTail only ever written by the producer,
        // each side only *reads* the other's index, and array size is
        // capacity+1 so head==tail unambiguously means empty.
        constexpr s32 kCloudQueueCapacity = 8;
        constexpr s32 kCloudQueueSize = kCloudQueueCapacity + 1;
        CloudEvent sCloudQueue[kCloudQueueSize];
        sead::Atomic<s32> sCloudQueueHead = 0;
        sead::Atomic<s32> sCloudQueueTail = 0;

        // Called only from the main thread (sendCloud()).
        void pushQueuedEvent(const CloudEvent& event) {
            const s32 tail = sCloudQueueTail.load();
            const s32 nextTail = (tail + 1) % kCloudQueueSize;
            if (nextTail == sCloudQueueHead.load())
                return; // full - drop this event rather than race the consumer to evict the oldest
            sCloudQueue[tail] = event;
            sCloudQueueTail.store(nextTail);
        }

        // Called only from a CloudUploadThread's own background thread, right
        // after it finishes uploading whatever it was last given.
        bool tryPopQueuedEvent(CloudEvent* out) {
            const s32 head = sCloudQueueHead.load();
            if (head == sCloudQueueTail.load())
                return false; // empty
            *out = sCloudQueue[head];
            sCloudQueueHead.store((head + 1) % kCloudQueueSize);
            return true;
        }

        void sendToFirestore(const CloudEvent& event) {
            char path[256];
            char body[0x400];
            const char* method;

            if (event.kind == EventKind::Progress) {
                // A single fixed document per room, overwritten in place
                // (PATCH with no updateMask replaces the whole document) -
                // late viewers just read the latest snapshot directly rather
                // than replaying an event history, and there's nothing to
                // grow unbounded the way an events log would.
                method = "PATCH";
                snprintf(
                    path, sizeof(path),
                    "/v1/projects/%s/databases/(default)/documents/rooms/%s/progress/state",
                    kFirestoreProjectId, event.roomCode);
                snprintf(
                    body, sizeof(body),
                    "{\"fields\":{\"moons\":{\"stringValue\":\"%s\"},\"captures\":{\"stringValue\":\"%s\"},"
                    "\"abilities\":{\"stringValue\":\"%s\"},\"moonReqs\":{\"stringValue\":\"%s\"},"
                    "\"kingdomStatus\":{\"stringValue\":\"%s\"}}}",
                    event.from, event.to, event.debugArrivalDoorId, event.exitId, event.kingdomStatus);
            } else {
                method = "POST";
                snprintf(
                    path, sizeof(path), "/v1/projects/%s/databases/(default)/documents/rooms/%s/events",
                    kFirestoreProjectId, event.roomCode);
                snprintf(
                    body, sizeof(body),
                    "{\"fields\":{\"exitId\":{\"stringValue\":\"%s\"},\"from\":{\"stringValue\":\"%s\"},"
                    "\"to\":{\"stringValue\":\"%s\"},\"debugArrivalDoorId\":{\"stringValue\":\"%s\"}}}",
                    event.exitId, event.from, event.to, event.debugArrivalDoorId);
            }

            char response[512];
            s32 statusCode = 0;
            const bool ok = https::httpsRequest(
                method, kFirestoreHost, 443, path, "Content-Type: application/json\r\n", body, response,
                sizeof(response), kFirestoreIp, &statusCode);
            if (event.kind == EventKind::Progress)
                sLastProgressSendOk = ok;

            if (ok)
                setCloudStatus("Uploaded OK (HTTP %d)", statusCode);
            else if (statusCode > 0)
                // Reached Firestore but it rejected the write - a wrong
                // project ID, unpublished/misconfigured security rules, or a
                // body shape mismatch all show up here. response holds
                // Firestore's own error JSON, truncated to fit the status line.
                setCloudStatus("Firestore rejected upload (HTTP %d): %.60s", statusCode, response);
            else
                // Never got a response at all - TCP/TLS handshake failure,
                // DNS/IP routing issue, or no internet route from the console.
                setCloudStatus("Upload failed: %.100s", response);
        }

        class CloudUploadThread : public al::AsyncFunctorThread {
        public:
            // 0x8000 (32KB): a 16KB stack genuinely overflowed doing this
            // exact mbedTLS TLS handshake (confirmed via crash log - SP had
            // run only ~3.5KB past the bottom of that region, so genuine peak
            // usage is around 19.5KB). 32KB keeps a real margin over that
            // (roughly 1.6x) without asking for more than this heap needs to
            // hold.
            CloudUploadThread()
                : al::AsyncFunctorThread(
                      "TrackerCloudUpload", al::FunctorV0M(this, &CloudUploadThread::threadFunction), 0, 0x8000,
                      sead::CoreId::cSub1) { }
            // Drains the queue before actually finishing (isDone() only
            // becomes true once this returns) - a burst of events queued
            // while the first upload was in flight gets sent back-to-back on
            // this same thread instead of waiting for a later trigger that
            // might never come (the player could just stop moving).
            void threadFunction() {
                sendToFirestore(event);
                while (tryPopQueuedEvent(&event))
                    sendToFirestore(event);
            }
            CloudEvent event;
        };

        // One slot: this heap is sized for exactly one CloudUploadThread at a
        // time (see kTrackerHeapSize) - only one upload can be in flight at
        // once. Extra events while it's busy get queued (see the ring-buffer
        // comment above) and drained on this same thread before it actually
        // finishes, rather than dropped.
        constexpr s32 kCloudUploadSlots = 1;
        CloudUploadThread* sCloudSlots[kCloudUploadSlots] = {};

        // Plain `new CloudUploadThread()` is NOT safe even with a null check
        // right after: the C++ standard says a non-nothrow operator new must
        // throw rather than return null, so Clang trusts that contract and
        // emits the constructor's vtable-pointer store with no null guard at
        // all - confirmed via disassembly (a Data Abort at address 0 landed
        // squarely on that unguarded store). This target's operator new
        // (hk_Znwm, see HeapSourceDynamic's DynamicHeapWrappers.cpp) breaks
        // that contract and returns null on failure instead, so only the
        // nothrow form - which the standard requires to be null-checked
        // before construction - actually works here.
        //
        // sead::ScopedCurrentHeapSetter forces sTrackerHeap as the "current
        // heap" for the whole construction, not just the outer allocation:
        // AsyncFunctorThread's constructor (a CloudUploadThread base class)
        // does its own internal allocation for the actual OS thread + its
        // stack, and that code has no way to know about sTrackerHeap unless
        // the ambient "current heap" is set to it too - see the sTrackerHeap
        // comment above for why ambient state can't be trusted otherwise.
        // If sTrackerHeap is null (initHeap() never ran or failed),
        // ScopedCurrentHeapSetter(nullptr) is a documented no-op, so this
        // just falls back to whatever ambient behavior existed before.
        void ensureCloudThreadsAllocated() {
            sead::ScopedCurrentHeapSetter setter(sTrackerHeap);
            for (s32 i = 0; i < kCloudUploadSlots; i++) {
                if (sCloudSlots[i])
                    continue;
                sCloudSlots[i] = new (std::nothrow) CloudUploadThread();
                if (!sCloudSlots[i]) {
                    if (sTrackerHeap) {
                        setCloudStatus(
                            "Upload thread alloc failed (heap: %zuB free, %zuB max block)",
                            sTrackerHeap->getFreeSize(), sTrackerHeap->getMaxAllocatableSize(8));
                    } else {
                        setCloudStatus("Upload thread alloc failed (no dedicated heap - initHeap() never ran)");
                    }
                    return; // stop trying further slots this call - same heap state, unlikely to help
                }
            }
        }

        // Shared by sendCloud() (transitions) and checkAndSendProgress()
        // (moons/captures/abilities) - both just build a CloudEvent and hand
        // it here to either start uploading immediately or queue behind
        // whatever's already in flight.
        void dispatchCloudEvent(const CloudEvent& newEvent) {
            ensureCloudThreadsAllocated();

            for (s32 i = 0; i < kCloudUploadSlots; i++) {
                if (!sCloudSlots[i])
                    continue; // this slot never got allocated - skip to whichever slots did
                if (!sCloudSlots[i]->isDone()) {
                    // Busy uploading a prior event - queue this one instead
                    // of dropping it (see kCloudQueueCapacity's comment:
                    // walking straight through a short room fires two real
                    // events faster than one Firestore round-trip completes,
                    // and the second one is the player's actual current
                    // position).
                    pushQueuedEvent(newEvent);
                    setCloudStatus("Upload busy, queued");
                    return;
                }

                sCloudSlots[i]->event = newEvent;
                sCloudSlots[i]->start();
                return;
            }
            // No slot was ever allocated at all - leave
            // ensureCloudThreadsAllocated's "alloc failed" status alone, it's
            // the more useful reason than anything generic here.
        }

        void sendCloud(const char* exitId, const char* fromStage, const char* toStage, const char* debugArrivalDoorId) {
            if (!sRoomCode[0])
                return; // no room set - nothing to upload to yet

            CloudEvent newEvent;
            newEvent.kind = EventKind::Transition;
            copyField(newEvent.roomCode, sizeof(newEvent.roomCode), effectiveRoomCode());
            copyField(newEvent.exitId, sizeof(newEvent.exitId), exitId);
            copyField(newEvent.from, sizeof(newEvent.from), fromStage);
            copyField(newEvent.to, sizeof(newEvent.to), toStage);
            copyField(newEvent.debugArrivalDoorId, sizeof(newEvent.debugArrivalDoorId), debugArrivalDoorId);
            dispatchCloudEvent(newEvent);
        }

        // Kingdoms shown on the web tracker's moon-count panel, in the exact
        // order its own KINGDOMS array uses (app.js) - the browser side
        // decodes the comma-separated moon string positionally against that
        // same array, so this order is load-bearing, not cosmetic. Cap and
        // Moon Kingdom are last there too (see app.js's comment on why).
        constexpr s32 kProgressWorldIds[] = {
            WorldMeta::WorldId_Cascade,
            WorldMeta::WorldId_Sand,
            WorldMeta::WorldId_Lake,
            WorldMeta::WorldId_Wooded,
            WorldMeta::WorldId_Lost,
            WorldMeta::WorldId_Metro,
            WorldMeta::WorldId_Snow,
            WorldMeta::WorldId_Seaside,
            WorldMeta::WorldId_Luncheon,
            WorldMeta::WorldId_Ruined,
            WorldMeta::WorldId_Bowsers,
            WorldMeta::WorldId_Moon,
            WorldMeta::WorldId_Cap,
        };
        constexpr s32 kProgressWorldCount = sizeof(kProgressWorldIds) / sizeof(kProgressWorldIds[0]);

        // Same lookup TrackerOverlay's HUD panel uses for the "X / req"
        // readout - kept here too rather than exported from there, since
        // it's a two-line lookup and TrackerOverlay is a HUD-drawing module,
        // not really the right place to hang a shared API off of.
        s32 getSavedRequirement(GameDataFile* file, s32 worldId) {
            if (const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, worldId))
                return entry->requirement.moonCount;
            return RequirementRando::getRequirement(worldId);
        }

        // Builds the progress snapshot strings (moons, moon requirements,
        // captures, abilities, per-kingdom unlocked/peace/moon-rock status)
        // and sends them (as a repurposed CloudEvent, see EventKind::Progress)
        // only when at least one changed since the last call - this runs
        // every frame, so the diff check is what keeps it from spamming
        // Firestore with identical writes while nothing is happening.
        void checkAndSendProgress() {
            if (!sRoomCode[0])
                return; // no room set - nothing to upload to yet, and no point computing a snapshot for it

            // getStageScene() checks system->mSequence's actual runtime type
            // name before treating it as a HakoniwaSequence (see helpers.cpp)
            // - an earlier version of this skipped straight to
            // GameSystemFunction::getGameSystem()/a blind static_cast, same
            // as TrackerOverlay.cpp never does. During the pause -> Options
            // -> Return to Main Menu transition, mSequence is no longer
            // actually a HakoniwaSequence (or is mid-teardown) for at least
            // one frame, so that blind cast read garbage that eventually
            // resolved to a null GameDataFile* and crashed on
            // getCurrentWorldId() - confirmed via a crash log resolving the
            // fault straight back to this function. getStageScene() returning
            // null here means gameplay isn't actually running (menu, title,
            // a mid-transition frame like this one), which is exactly when
            // there's nothing meaningful to report anyway.
            StageScene* scene = getStageScene();
            if (!scene)
                return;
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return;
            HakoniwaSequence* sequence = static_cast<HakoniwaSequence*>(system->mSequence);
            GameDataHolderAccessor accessor = sequence->mGameDataHolderAccessor;
            if (!accessor.mData)
                return;
            GameDataFile* file = accessor->getGameDataFile();
            if (!file)
                return;

            const s32 currentWorldId = file->getCurrentWorldId();
            const u32 visitedMask = Randomizer::getVisitedKingdoms();
            const KingdomProgress::KingdomProgressList* progressList = KingdomProgress::get(file);
            const s32 frontierIndex = KingdomProgress::getFirstIncompleteIndex(file);

            char moonsStr[128] = "";
            char reqsStr[128] = "";
            // One digit per kingdom, bits 4/2/1 = unlocked/peace/moon-rock -
            // see the comment below on why each bit is (or isn't) gated by
            // "visited" separately.
            char statusStr[128] = "";
            size_t pos = 0;
            size_t reqPos = 0;
            size_t statusPos = 0;
            for (s32 i = 0; i < kProgressWorldCount; i++) {
                const s32 worldId = kProgressWorldIds[i];
                const s32 have = file->getShineNum(worldId);
                const int written = snprintf(
                    moonsStr + pos, sizeof(moonsStr) - pos, i == 0 ? "%d" : ",%d", have);
                if (written > 0)
                    pos += static_cast<size_t>(written);

                // -1 for a kingdom the player hasn't visited yet, same
                // spoiler-prevention rule TrackerOverlay's own HUD panel
                // uses ("?" until visited) - the web side leaves whatever
                // it already has alone when it sees -1, rather than
                // clobbering it with a value the player hasn't earned yet.
                const bool visited = currentWorldId == worldId || (visitedMask & (1u << worldId)) != 0;
                const s32 req = visited ? getSavedRequirement(file, worldId) : -1;
                const int reqWritten = snprintf(
                    reqsStr + reqPos, sizeof(reqsStr) - reqPos, i == 0 ? "%d" : ",%d", req);
                if (reqWritten > 0)
                    reqPos += static_cast<size_t>(reqWritten);

                // Unlocked/reachable is NOT visited-gated - TrackerOverlay's
                // own HUD shows this for the current frontier kingdom before
                // the player has ever set foot there too (it's forward-
                // looking progression info the player is about to run into
                // anyway, not a spoiler the way an exact moon count is).
                //
                // requirementsSatisfied() means "you've collected THIS
                // kingdom's own moons and beaten its boss" - that's
                // completion (ready to move past it), a much later milestone
                // than "can I currently enter it", which is what a padlock
                // icon means. The right match for that is exactly
                // TrackerOverlay's own frontier logic: already completed, or
                // sitting at the current frontier slot right now.
                const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, worldId);
                const bool isFrontier = progressList && frontierIndex >= 0
                    && frontierIndex < KingdomProgress::cKingdomCount
                    && progressList->entries[frontierIndex].kingdomId == worldId;
                const bool unlocked = (entry && entry->completeStatus) || isFrontier;

                // Peace/moon-rock ARE visited-gated, matching TrackerOverlay's
                // own state-icon logic exactly (Bowser-head "unknown" marker
                // until visited) - these describe history at a specific
                // kingdom, not something the player is about to discover
                // regardless. Left at their default (not achieved) rather
                // than a sentinel when unvisited - harmless either way since
                // that's also every kingdom's untouched starting state.
                bool peace = false;
                bool rock = false;
                if (visited) {
                    const s32 peaceScenario = worldId >= 0 && worldId < WorldMeta::sWorldCount
                        ? Randomizer::LogicData::sPeaceScenarioByKingdom[worldId]
                        : -1;
                    peace = peaceScenario > 0 && file->getScenarioNo(worldId) >= peaceScenario;
                    rock = file->isOpenMoonRock(worldId);
                }
                const s32 statusDigit = (unlocked ? 4 : 0) | (peace ? 2 : 0) | (rock ? 1 : 0);
                const int statusWritten = snprintf(
                    statusStr + statusPos, sizeof(statusStr) - statusPos, i == 0 ? "%d" : ",%d", statusDigit);
                if (statusWritten > 0)
                    statusPos += static_cast<size_t>(statusWritten);
            }

            // Bit order = CaptureRando's own table order (getCapture(0..N)),
            // read directly from the live table rather than a hand-copied
            // list here, so it can never drift out of sync with it. The web
            // side keeps its own fixed copy of that same order to decode
            // positionally (see firebase-progress-sync.js).
            // isCaptureUnlocked() unconditionally returns true for every
            // capture the instant Capture Rando is off for this seed (it's a
            // logic-availability check, not a "have you done this" one) -
            // that would make the whole captures string constant-true from
            // frame one regardless of what the player actually rides, so
            // nothing would ever visibly change. Match TrackerOverlay's own
            // HUD panel instead: gallery presence (an actual "you've
            // captured this at least once" record) when captures are
            // locked, the base game's own hack-dictionary flag when they
            // aren't.
            const bool captureRandoEnabled = Randomizer::isCaptureRandoEnabled();
            char capturesStr[64] = "";
            const s32 captureCount = CaptureRando::getCaptureCount();
            s32 c = 0;
            for (; c < captureCount && c < static_cast<s32>(sizeof(capturesStr)) - 1; c++) {
                const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(c);
                const bool owned = captureRandoEnabled
                    ? CaptureRando::isCaptureInGallery(accessor, capture->hackName)
                    : file->isExistInHackDictionary(capture->hackName);
                capturesStr[c] = owned ? '1' : '0';
            }
            capturesStr[c] = '\0';

            // Bit order = AbilityId enum declaration order, skipping
            // CapReturnJump (the one AbilityId with no corresponding entry
            // on the web tracker's Abilities panel - it's always granted
            // alongside the starting cap throw, nothing to track).
            char abilitiesStr[AbilityRando::AbilityId_Count + 1] = "";
            s32 a = 0;
            for (s32 id = 0; id < AbilityRando::AbilityId_Count; id++) {
                if (id == AbilityRando::AbilityId_CapReturnJump)
                    continue;
                abilitiesStr[a++] = AbilityRando::isAbilityUnlocked(accessor, static_cast<AbilityRando::AbilityId>(id))
                    ? '1'
                    : '0';
            }
            abilitiesStr[a] = '\0';

            const bool sameAsLastDispatch = std::strcmp(moonsStr, sLastMoons) == 0
                && std::strcmp(reqsStr, sLastReqs) == 0 && std::strcmp(capturesStr, sLastCaptures) == 0
                && std::strcmp(abilitiesStr, sLastAbilities) == 0
                && std::strcmp(statusStr, sLastKingdomStatus) == 0;
            if (sameAsLastDispatch) {
                if (sLastProgressSendOk)
                    return; // already confirmed delivered, nothing to do
                // The last attempt at this exact data is known to have
                // failed (e.g. firestore.rules didn't allow a field yet) -
                // retry on a cooldown rather than either every single frame
                // (needless spam while genuinely down) or never again (the
                // bug this replaced - see sLastProgressSendOk's comment).
                if (sProgressRetryCooldown > 0) {
                    sProgressRetryCooldown--;
                    return;
                }
            }
            sProgressRetryCooldown = kProgressRetryCooldownFrames;

            copyField(sLastMoons, sizeof(sLastMoons), moonsStr);
            copyField(sLastReqs, sizeof(sLastReqs), reqsStr);
            copyField(sLastCaptures, sizeof(sLastCaptures), capturesStr);
            copyField(sLastAbilities, sizeof(sLastAbilities), abilitiesStr);
            copyField(sLastKingdomStatus, sizeof(sLastKingdomStatus), statusStr);

            CloudEvent newEvent;
            newEvent.kind = EventKind::Progress;
            copyField(newEvent.roomCode, sizeof(newEvent.roomCode), effectiveRoomCode());
            // exitId is unused for a transition event here - repurposed to
            // carry moonReqs instead (see sendToFirestore's Progress branch).
            copyField(newEvent.exitId, sizeof(newEvent.exitId), reqsStr);
            copyField(newEvent.from, sizeof(newEvent.from), moonsStr);
            copyField(newEvent.to, sizeof(newEvent.to), capturesStr);
            copyField(newEvent.debugArrivalDoorId, sizeof(newEvent.debugArrivalDoorId), abilitiesStr);
            copyField(newEvent.kingdomStatus, sizeof(newEvent.kingdomStatus), statusStr);
            dispatchCloudEvent(newEvent);
        }
    } // namespace

    void initHeap() {
        if (sTrackerHeap)
            return; // already done
        sTrackerHeap = sead::ExpHeap::create(
            kTrackerHeapSize, "TrackerBridgeHeap", al::getStationedHeap(), sizeof(void*),
            sead::Heap::cHeapDirection_Forward, true);
        if (sTrackerHeap)
            al::addNamedHeap(sTrackerHeap, "TrackerBridgeHeap");
    }

    void setRoomCode(const char* roomCode) {
        if (!roomCode || !roomCode[0])
            return;
        std::strncpy(sRoomCode, roomCode, sizeof(sRoomCode) - 1);
        sRoomCode[sizeof(sRoomCode) - 1] = '\0';
    }

    const char* getRoomCode() { return effectiveRoomCode(); }

    const char* getRoomCodeDisplayText() {
        if (!sRoomCode[0])
            return "(not set)";
        if (!Randomizer::isStreamerModeEnabled())
            return sRoomCode;

        // Masked in place of the real code so it's safe to show on stream;
        // the actual code is still readable via getRoomCode() for the
        // cloud-sync path, which never renders to screen.
        static char sMaskedRoomCode[sizeof(sRoomCode)] = {};
        const size_t len = std::strlen(sRoomCode);
        for (size_t i = 0; i < len; i++)
            sMaskedRoomCode[i] = '*';
        sMaskedRoomCode[len] = '\0';
        return sMaskedRoomCode;
    }

    // Per-slot persistence file, same directory/pattern BingoBoard.cpp
    // already uses for its own per-slot data (sd:/SMO-Randomizer/Saves/
    // Slot<N>/...) - see the .h comment for why this isn't part of
    // RandoSaveFile::Header instead. Raw sRoomCode bytes, no header/struct:
    // there's nothing here that needs versioning, and a missing/short/
    // corrupt file just fails the read, leaving the caller's existing value
    // alone.
    al::StringTmp<128> makeRoomCodePath(s32 slot, SaveRedirect::Category category) {
        const char* root = category == SaveRedirect::Category::Multiplayer
            ? "MPSaves"
            : category == SaveRedirect::Category::MultiplayerGuest ? "MPSaves/_guest"
                                                                   : "Saves";
        return al::StringTmp<128>("sd:/smo/Randomizer/%s/Slot%d/tracker_room_code.bin", root, slot);
    }

    void saveRoomCodeToSlot(s32 slot, SaveRedirect::Category category) {
        const auto path = makeRoomCodePath(slot, category);
        nn::fs::DeleteFile(path.cstr());
        if (nn::fs::CreateFile(path.cstr(), sizeof(sRoomCode)).IsFailure())
            return;

        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Write).IsFailure())
            return;
        nn::fs::WriteFile(handle, 0, sRoomCode, sizeof(sRoomCode),
            nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
        nn::fs::CloseFile(handle);
    }

    void loadRoomCodeFromSlot(s32 slot, SaveRedirect::Category category) {
        const auto path = makeRoomCodePath(slot, category);
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return; // no saved room code for this slot - leave the current value alone

        char buf[sizeof(sRoomCode)] = {};
        const bool ok = nn::fs::ReadFile(handle, 0, buf, sizeof(buf)).IsSuccess();
        nn::fs::CloseFile(handle);
        if (!ok)
            return;

        buf[sizeof(buf) - 1] = '\0'; // guard against a corrupt/truncated file
        if (buf[0])
            setRoomCode(buf);
    }

    const char* getCloudStatusText() { return sCloudStatusText; }

    const char* getHeapStatusText() {
        static char buf[128];
        if (!sTrackerHeap) {
            std::strncpy(buf, "No dedicated heap (initHeap() never ran or failed)", sizeof(buf) - 1);
            buf[sizeof(buf) - 1] = '\0';
            return buf;
        }
        nn::util::SNPrintf(
            buf, sizeof(buf), "free=%zuB  maxBlock=%zuB  total=%zuB", sTrackerHeap->getFreeSize(),
            sTrackerHeap->getMaxAllocatableSize(8), sTrackerHeap->getSize());
        return buf;
    }

    void notifyTransition(
        const char* exitId, const char* fromStage, const char* toStage, const char* debugArrivalDoorId) {
        sendCloud(exitId, fromStage, toStage, debugArrivalDoorId);
    }

    void updateProgress() { checkAndSendProgress(); }

    const char* getLastMoonsDebugText() { return sLastMoons; }
    const char* getLastMoonReqsDebugText() { return sLastReqs; }
    const char* getLastCapturesDebugText() { return sLastCaptures; }
    const char* getLastAbilitiesDebugText() { return sLastAbilities; }
    const char* getLastKingdomStatusDebugText() { return sLastKingdomStatus; }

} // namespace TrackerBridge
