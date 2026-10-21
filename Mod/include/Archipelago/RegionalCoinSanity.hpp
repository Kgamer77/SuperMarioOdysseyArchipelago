#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "sead/prim/seadSafeString.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"

#include "Archipelago/Archipelago.h"
// #include "server/gamemode/GameModeManager.hpp"

// ===== Regional Coins =====
static bool isGotCoinCollectHook(GameDataFile* file, const al::PlacementId* placementId) {
    if (Archipelago::isArchipelagoActive()) {
        al::StringTmp<128> placeIdString;
        placementId->makeString(&placeIdString);
        return Archipelago::hasRegionalCoin(placeIdString.cstr());
    } else {
        return file->isGotCoinCollect(placementId);
    }
}

// Gets the relative world Id for CoinCollect archive name and Picture Font for ER
static int getCurrentWorldIdForCoinCollectHook(GameDataHolderAccessor accessor) {
    if (Archipelago::isArchipelagoActive() && Archipelago::getRelativeWorldCoinCollect() > -1) {
        return Archipelago::getRelativeWorldCoinCollect();
    }
    return GameDataFunction::getCurrentWorldId(accessor);
}

static int getCoinCollectCheckGotNumHook(GameDataHolderAccessor accessor) {
    if (Archipelago::isArchipelagoActive() && Archipelago::getRelativeWorldCoinCollect() > -1) {
        return Archipelago::getRelativeWorldCoinCollectCheckGotNum(accessor);
    }
    return GameDataFunction::getCoinCollectGotNum(accessor);
}

static int getCoinCollectNumMaxHook(GameDataHolderAccessor accessor) {
    if (Archipelago::isArchipelagoActive() && Archipelago::getRelativeWorldCoinCollect() > -1) {
        return accessor.mData->getCoinCollectNumMax(Archipelago::getRelativeWorldCoinCollect());
    }
    return GameDataFunction::getCoinCollectNumMax(accessor);
}

static HkTrampoline<int, GameDataHolderAccessor> getCoinCollectNumHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> int {
    if (Archipelago::isArchipelagoActive() && Archipelago::getRelativeWorldCoinCollect() > -1) {
        return Archipelago::getNumCoinCollect();
    }

    return getCoinCollectNumHook.orig(accessor);
});

static void useCoinCollectHook(GameDataHolderWriter writer, int amount) {
    if (!Archipelago::isArchipelagoActive()) {
        GameDataFunction::useCoinCollect(writer, amount);
    } else {
        return;
    }
}