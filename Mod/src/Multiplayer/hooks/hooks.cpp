#include "hk/gfx/ImGuiBackendNvn.h"
#include "hk/hook/Trampoline.h"

#include "al/Library/Bgm/BgmLineFunction.h"
#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Controller/PadRumbleDirector.h"
#include "al/Library/Controller/PadRumbleFunction.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/System/GameSystemInfo.h"

#include "agl/common/aglDrawContext.h"

#include "game/MapObj/CheckpointFlagWatcher.h"
#include "game/System/Application.h"
#include "game/System/GameConfigData.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameSystem.h"

#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/layouts/SpeedrunIcon.h"
#include "Multiplayer/logger.hpp"
#include "Multiplayer/main.hpp"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "Multiplayer/saveManager.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/server/DeltaTime.hpp"
#include "Scene/StageSceneStateModConfig.hpp"
#include "imgui.h"

static HkTrampoline<void, StageScene*> sceneKillHook = hk::hook::trampoline([](StageScene* scene) -> void {
    // this hook should prevent crashes on scene transitions
    gIsSceneAlive = false;

    Client::clearArrays();

    // Drops every pooled capture model without deleting it - they were built on
    // the scene heap this transition is about to free.
    PuppetCapturePool::onStageKill();

    sceneKillHook.orig(scene);
});

// NOTE: _ZN16HakoniwaSequence12exePlayStageEv is already hooked in src/main.cpp
// (see hakoniwaSequenceHook there). Installing a second HkTrampoline on the same
// symbol here will collide with that hook. This body is left intact for reference,
// but installMultiplayerHooks() below does NOT install it — the logic still needs
// to be merged into the existing hook in main.cpp before this can run.
// static HkTrampoline<void, HakoniwaSequence*> multiplayerHakoniwaSequenceHook = hk::hook::trampoline([](HakoniwaSequence* sequence) -> void {
//    StageScene* stageScene = (StageScene*)sequence->mCurrentScene;
//    SpeedrunIcon::sInstance->setHolder(sequence->mGameDataHolderAccessor);
//
//    static bool isCameraActive = false;
//
//    al::PlayerHolder* pHolder = al::getScenePlayerHolder(stageScene);
//    PlayerActorBase* playerBase = (PlayerActorBase*)al::tryGetPlayerActor(pHolder, 0);
//    auto* player = (PlayerActorHakoniwa*)al::tryGetPlayerActor(pHolder, 0);
//
//    if (!playerBase) {
//        multiplayerHakoniwaSequenceHook.orig(sequence);
//        return;
//    }
//
//    bool isYukimaru = !playerBase->getPlayerInfo();
//
//    isInGame = !stageScene->isPause();
//
//    Client::setStageInfo(sequence);
//
//    Client::update();
//
//    if (Client::shouldStopRumble() && player && !isYukimaru) {
//        auto* rumbleDirector = alPadRumbleFunction::getPadRumbleDirector(player);
//        if (rumbleDirector)
//            rumbleDirector->stopAllRumble();
//        Client::clearStopRumble();
//    }
//
//    if (gIsSceneAlive) {
//        updatePlayerInfo(GameDataHolderAccessor(stageScene), playerBase, isYukimaru);
//    }
//
//    if (SpeedrunIcon::sInstance) {
//            SpeedrunIcon::sInstance->tryStart();
//        } else {
//            SpeedrunIcon::sInstance->tryEnd();
//        }
//    }
//
//    stageScene->stageSceneLayout->updateCounterParts();
//
//    multiplayerHakoniwaSequenceHook.orig(sequence);
//});

// ===== LOGGING HOOK =====

void seadPrintHook(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    va_end(args);
}

HkReplaceVarArgs replaceSeadPrintHook = seadPrintHook;

// crash fix maybe
// _ZNK21CheckpointFlagWatcher21tryFindCheckpointFlagEPKc demangles to:
// CheckpointFlagWatcher::tryFindCheckpointFlag(const char*) const
static HkTrampoline<void*, const CheckpointFlagWatcher*, const char*> checkpointFlagWatcherHook = hk::hook::trampoline([](const CheckpointFlagWatcher* thisPtr, const char* p1) -> void* {
    if (!thisPtr || !p1)
        return nullptr;

    return checkpointFlagWatcherHook.orig(thisPtr, p1);
});

void installOtherHooks() {
    replaceSeadPrintHook.installAtSym<"_ZN4sead6system5PrintEPKcz">();

    sceneKillHook.installAtSym<"_ZN10StageScene4killEv">();

    checkpointFlagWatcherHook.installAtSym<"_ZNK21CheckpointFlagWatcher21tryFindCheckpointFlagEPKc">();
}