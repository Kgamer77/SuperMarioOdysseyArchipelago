#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "al/Library/LiveActor/ActorAreaFunction.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/System/GameDataFile.h"

#include "server/gamemode/GameModeManager.hpp"

// Helper function to check if either mode is active
static bool isRecoveryModeActive() {
    return (GameModeManager::instance()->isModeAndActive(GameMode::ARCHIPELAGO) && Archipelago::getConnectInitFlag());
}

static HkReplace<bool, MapLayout*> isCheckpointWarpAllowedHook = hk::hook::replace([](MapLayout* map) -> bool { return !isRecoveryModeActive(); });

// static HkTrampoline<bool, al::LiveActor*> freezeDeathAreaHook =
//     hk::hook::trampoline([](al::LiveActor* actor) -> bool {
//         bool isHakoniwa =
//             al::isEqualString(typeid(*actor).name(), typeid(PlayerActorHakoniwa).name());
//         if (!isRecoveryModeActive() || !isHakoniwa) {
//             return freezeDeathAreaHook.orig(actor);
//         }

//         if (al::isInAreaObj(actor, "DeathArea")) {
//             // Handle Freeze Tag mode
//             if (GameModeManager::instance()->isModeAndActive(GameMode::FREEZETAG)) {
//                 FreezeTagMode* mode = GameModeManager::instance()->getMode<FreezeTagMode>();
//                 if (!mode->isEndgameActive()) {
//                     mode->tryStartRecoveryEvent(false);
//                 }
//             }
//             // Handle Shine Thief mode
//             else if (GameModeManager::instance()->isModeAndActive(GameMode::SHINETHIEF)) {
//                 ShineThiefMode* mode = GameModeManager::instance()->getMode<ShineThiefMode>();
//                 if (!mode->isEndgameActive()) {
//                     mode->tryStartRecoveryEvent(false);
//                 }
//             }
//         }
//         return false;
//     });

static HkTrampoline<void, PlayerHitPointData*> playerHitPointDamageHook = hk::hook::trampoline([](PlayerHitPointData* hitData) -> void {
    if (!isRecoveryModeActive()) {
        playerHitPointDamageHook.orig(hitData);
    }
});

static HkTrampoline<bool, HackCap*> isEnableRescuePlayerHook = hk::hook::trampoline([](HackCap* hackCap) -> bool { return isRecoveryModeActive() ? true : isEnableRescuePlayerHook.orig(hackCap); });

// static HkTrampoline<void, HackCap*> hackCapStartRescuePlayerHook =
//     hk::hook::trampoline([](HackCap* hackCap) -> void {
//         hackCapStartRescuePlayerHook.orig(hackCap);

//         if (GameModeManager::instance()->isModeAndActive(GameMode::SHINETHIEF)) {
//             ShineThiefMode* mode = GameModeManager::instance()->getMode<ShineThiefMode>();
//             if (mode && mode->getInfo()->mIsPlayerHolder && mode->getInfo()->mIsRound &&
//                 !mode->isEndgameActive()) {
//                 mode->trySetPlayerHolderState(false);
//                 mode->placeShine();
//                 if (mode->getLayout())
//                     mode->getLayout()->showShineRespawned();
//                 mode->sendShineThiefPacket(ShineThiefUpdateType::FALLOFF);
//             }
//         }
//     });

// static HkTrampoline<bool, Shine*, al::SensorMsg*, al::HitSensor*, al::HitSensor*>
//     freezeMoonHitboxHook = hk::hook::trampoline(
//         [](Shine* shine, al::SensorMsg* msg, al::HitSensor* sourve, al::HitSensor* target) ->
//         bool {
//             if (GameModeManager::instance()->isMode(GameMode::FREEZETAG) ||
//                 GameModeManager::instance()->isMode(GameMode::SHINETHIEF)) {
//                 return true;
//             }
//             return freezeMoonHitboxHook.orig(shine, msg, sourve, target);
//         });