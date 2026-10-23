#pragma once

#include <sead/container/seadPtrArray.h>
#include <sead/container/seadSafeArray.h>
#include <sead/prim/seadSafeString.h>

#include "Archipelago/Archipelago.h"

const char* Archipelago::getShineReplacementText() {
    std::string text;
    for (AP_NetworkItem item : sScoutedMoons) {
        if (item.location == getRecentShineUid()) {
            text.append(item.playerName);
            text.append("'s ");
            text.append(item.itemName);
        }
    }
    return text.c_str();
}

int Archipelago::getShineColor(Shine* curShine) {
    return sShineColors[curShine->mShineIdx];
}

const char16_t* Archipelago::getShopReplacementText(const char* fileName, const char* key) {
    return u"";
}