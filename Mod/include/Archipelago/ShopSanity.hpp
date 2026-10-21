#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "sead/prim/seadSafeString.h"

#include "al/Library/Message/MessageHolder.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/Util/ClothUtil.h"

#include "Archipelago/Archipelago.h"

// ===== Shop Items =====
static void buyItemHook(GameDataFile* file, const ShopItem::ItemInfo* itemInfo, bool isPrepoSave) {
    if (Archipelago::isArchipelagoActive()) {
        Archipelago::sendShopCheck(itemInfo);
        Archipelago::addItem(itemInfo);
    } else {
        // Send buy item packet here
        file->buyItem(itemInfo, isPrepoSave);
    }
}

static bool isBuyItemHook(GameDataHolderAccessor accessor, ShopItem::ItemInfo* itemInfo) {
    // Add a collected outfits, gifts, stickers based implementation similar to shinechecks
    if (Archipelago::isArchipelagoActive()) {
        return Archipelago::hasItem(itemInfo);
    } else {
        return rs::isBuyItem(accessor, itemInfo);
    }
}

static void wearCapHook(GameDataHolderWriter writer, const char* itemName) {
    if (!Archipelago::isArchipelagoActive()) {
        GameDataFunction::wearCap(writer, itemName);
    } else {
        return;
    }
}

static void wearCostumeHook(GameDataHolderWriter writer, const char* itemName) {
    if (!Archipelago::isArchipelagoActive()) {
        GameDataFunction::wearCostume(writer, itemName);
    } else {
        return;
    }
}

// ===== Shop Data Replacement =====
static const char16_t* getShopItemMessage(const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) {
    ;
    if (Archipelago::isArchipelagoActive()) {
        const char16_t* msg = Archipelago::getShopReplacementText(fileName, key);
        sead::WFixedSafeString<200> confirm;
        confirm = u"";
        confirm.append(msg);
        if (!confirm.isEmpty()) {
            return msg;
        }
    }
    // Default to base game text if no ap text exists
    return al::getSystemMessageString(messageSystem, fileName, key);
}