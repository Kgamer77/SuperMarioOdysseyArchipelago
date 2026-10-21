#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "game/Player/PlayerHackKeeper.h"
#include "game/System/GameDataFunction.h"
// #include "game/Util/Hack.h"

#include "Archipelago/Archipelago.h"
// #include "server/gamemode/GameModeManager.hpp"

// ===== Hack Data Replacement =====
// isExistInHackDictionary for capture tracking
static void onAddHack(GameDataHolderWriter writer, const char* hackName) {
    if (Archipelago::getCapturesFlag()) {
        // Client::setMessage(2, hackName);
        Archipelago::sendCaptureCheck(hackName);
        Archipelago::setIsRecordCapture(true);
    } else {
        GameDataFunction::addHackDictionary(writer, hackName);
    }
}

static void canEndHack(al::LiveActor* actor) {
    if (actor != nullptr) {
        ((PlayerHackKeeper*)actor)->endHackStartDemo(actor);
    }
}