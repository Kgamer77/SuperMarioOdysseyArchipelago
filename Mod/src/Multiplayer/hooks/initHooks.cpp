#include "hk/diag/diag.h"
#include "hk/hook/Trampoline.h"

#include "nn/fs/fs_mount.h"
#include "nn/nifm.h"
#include "nn/socket.h"

#include "sead/filedevice/nin/seadNinFileDeviceBaseNin.h"
#include "sead/filedevice/seadFileDeviceMgr.h"
#include "sead/heap/seadExpHeap.h"
#include "sead/heap/seadHeap.h"
#include "sead/heap/seadHeapMgr.h"

#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Placement/PlacementInfo.h"
#include "al/Library/System/SystemKit.h"

#include "game/Sequence/SequenceInitInfo.h"
#include "game/System/GameSystem.h"

#include "Imgui.hpp"
#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/sync/SyncedActor.h"
#include "Randomizer/TrackerBridge.h"

// static HkTrampoline<void, GameSystem*> gameSystemInit = hk::hook::trampoline([](GameSystem* gameSystem) -> void {
//     nn::nifm::Initialize();
//     nn::nifm::SubmitNetworkRequest();
//
//     while (nn::nifm::IsNetworkRequestOnHold()) {
//     }
//
//     nn::socket::Initialize(socketPool, socketPoolSize, socketAllocPoolSize, 0xE);
//     disableSocketInit.installAtSym<"_ZN2nn6socket10InitializeEPvmmi">();
//
//     Client::createInstance(gHeap);
//     SaveManager::createInstance(gHeap);
//
//     gameSystemInit.orig(gameSystem);
// });

static HkTrampoline<void, HakoniwaSequence*, al::SequenceInitInfo*> hakoniwaSequenceInitHook = hk::hook::trampoline(
    [](HakoniwaSequence* sequence, al::SequenceInitInfo* initInfo) -> void {
        hakoniwaSequenceInitHook.orig(sequence, initInfo);
        // was threadInit (hook for initializing client class)
        al::LayoutInitInfo lytInfo;

        al::initLayoutInitInfo(&lytInfo, sequence->mLayoutKit, 0, sequence->mAudioDirector,
            initInfo->mSystemInfo->layoutSystem, initInfo->mSystemInfo->messageSystem,
            initInfo->mSystemInfo->gamePadSystem);

        Client::instance()->init(lytInfo, sequence->mGameDataHolderAccessor);
    });

al::LiveActor* createPuppetActorFromFactory(const al::ActorInitInfo& initInfo) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    // initInfo here is the SCENE's own base ActorInitInfo (mpOnActorInitInfo
    // below, from al::initActorInitInfo's hook), reused across every puppet
    // slot in the loop that calls this - not a real per-object placement.
    // Its placementInfo pointer can be non-null but DANGLING (pointing at
    // whatever object the engine most recently placed during the real
    // stage-load pass, not something that stays valid) - confirmed via real
    // crash logs, resolved down to the exact faulting instruction, each
    // catching a different al:: function reading through it
    // (al::initActorSRT via tryGetTrans, al::initActorModelKeeper via
    // tryGetStringArg). There's no way to validate a pointer's liveness
    // from the pointer alone, so every al::init*/al::tryGet* call taking
    // initInfo downstream (PuppetActor::init and everything it calls) is a
    // potential crash site.
    //
    // Point placementInfo at a real, permanently-empty PlacementInfo
    // instead of nullptr, rather than stripping it entirely: a bare
    // nullptr fixed every call that null-checks placementInfo first (most
    // of them), but PlayerFunction::initCapModelActorDemo (PuppetCapActor.
    // cpp) does NOT null-check - it dereferences placementInfo
    // unconditionally, and a real crash confirmed this (Data Abort at
    // address 0x8 - ByamlIter::mRootNode's offset, i.e. a method called on
    // a null PlacementInfo*). A default-constructed PlacementInfo's own
    // ByamlIters are themselves empty (mData == nullptr), which is the
    // ordinary "key not present" case every ByamlIter::tryGetXxxByKey
    // already has to handle gracefully for real placed objects with
    // optional/absent keys - unlike the original dangling pointer, whose
    // mData was a real, misleading non-null address into stale memory.
    // Static so it's never freed and safely shared/reused across every
    // puppet - it's never written to, and a puppet's saved ActorInitInfo
    // copy (PuppetActor::mInitInfo) can outlive this call by the puppet's
    // entire lifetime (costume/capture refreshes reuse it later).
    static al::PlacementInfo sEmptyPlacementInfo;

    al::ActorInitInfo safeInitInfo = initInfo;
    safeInitInfo.placementInfo = &sEmptyPlacementInfo;

    PuppetActor* newActor = new PuppetActor("PuppetActor");

    if (Client::tryAddPuppet(newActor)) {
        PuppetStateGuard stateGuard;
        PuppetInfo* curInfo = Client::getLatestInfo();
        if (!curInfo) {
            hk::diag::logLine("[Factory] ERROR: Puppet Info is Null!");
            MpDebugLog::write("[Factory] destroying puppet=%p (no PuppetInfo, pre-init)", newActor);
            delete newActor;
            return nullptr;
        } else {
            // hk::diag::logLine("[Factory] Creating puppet for player: %s", curInfo->puppetName.cstr());

            // set puppet info first before calling init so we can get costume info from the
            // info
            newActor->initOnline(curInfo);
            newActor->init(safeInitInfo);

            // hk::diag::logLine("[Factory] Puppet initialized successfully for %s",
            // curInfo->puppetName.cstr());

            // Diagnostic for the recurring ModelDrawerBase::addModelToTable draw-
            // pipeline crash (Data Abort, address 0 - see memory.txt): this pointer
            // pair is what a future crash's X0/X1 registers can be matched against.
            // getModelKeeper() is null-checked since not every LiveActor subclass
            // guarantees one.
            al::LiveActor* curModel = newActor->getCurrentModel();
            al::ModelKeeper* modelKeeper = curModel ? curModel->getModelKeeper() : nullptr;
            MpDebugLog::write("[Factory] created puppet=%p model=%p modelCtrl=%p name=%s", newActor,
                curModel, modelKeeper ? modelKeeper->getModelCtrl() : nullptr, curInfo->puppetName.cstr());
        }
    } else {
        hk::diag::logLine("[Factory] ERROR: Failed to add puppet to client");
        MpDebugLog::write("[Factory] destroying puppet=%p (tryAddPuppet failed, pre-init)", newActor);
        delete newActor;
        return nullptr;
    }

    return newActor;
}

// al::initActorInitInfo is already trampolined by main.cpp's
// paintingNotifActorInitInfoHook (one trampoline per function -- see the note in
// Multiplayer/main.hpp), which calls this right after the original runs.
void mpOnActorInitInfo(al::ActorInitInfo* initInfo, al::Scene* scene) {
    if (!Client::isSocketActive())
        return;

    if (!scene || !al::isEqualString(scene->mName.cstr(), "StageScene"))
        return;

    // was stage init hook
    gIsSceneAlive = true;

    // Before the puppets: this both clears the previous stage's capture models
    // (their scene heap is gone) and takes the ActorInitInfo copy that any
    // capture model built later in this stage is constructed from.
    PuppetCapturePool::onStageInit(*initInfo);

    // Same reasoning as the pool's teardown above: the previous stage's synced
    // actors point into a scene heap that is gone. Forget them before anything
    // can hand one out again.
    SyncedActor::reset();

    Client::sendGameInfPacket(scene);

    for (s32 i = 0; i < (Client::getMaxPlayerCount() - 1); i++) {
        createPuppetActorFromFactory(*initInfo);
    }
}

#define MB(x) (x * 1024.f * 1024.f)

static HkTrampoline<void, al::SystemKit*, sead::Heap*> createHeap = hk::hook::trampoline(
    [](al::SystemKit* systemKit, sead::Heap* rootHeap) -> void {
        createHeap.orig(systemKit, rootHeap);

        gHeap = sead::ExpHeap::create(MB(0.5), "SMOOPlusHeap", al::getStationedHeap(), sizeof(void*),
            sead::Heap::cHeapDirection_Forward, true);
        al::addNamedHeap(gHeap, "SMOOPlusHeap");
        // Diagnostic for the known sceneStateModConfig leak (PauseMenuHooks.cpp)
        // - logging this once here lets a pulled mp_debug_*.log directly confirm
        // or rule out whether that leak's allocations land on this same heap,
        // instead of guessing before attempting a third fix.
        MpDebugLog::write("gHeap created: %p", gHeap);

        // Rides along here rather than installing its own hook on this same
        // symbol: HkTrampoline only supports one hook per function process-
        // wide, and this hook already owns it. This is the earliest point
        // with a guaranteed-valid heap to create TrackerBridge's own
        // dedicated heap from - see TrackerBridge.cpp's sTrackerHeap comment
        // for why it needs one at all.
        TrackerBridge::initHeap();
    });

void installInitHooks() {
    createHeap.installAtSym<"_ZN2al9SystemKit18createMemorySystemEPN4sead4HeapE">();
    hakoniwaSequenceInitHook.installAtSym<"_ZN16HakoniwaSequence4initERKN2al16SequenceInitInfoE">();
    // NO hook on al::initActorInitInfo here -- main.cpp already owns it and
    // calls mpOnActorInitInfo for us.
}
