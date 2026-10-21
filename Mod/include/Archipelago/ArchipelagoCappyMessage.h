#pragma once

#include <sead/container/seadPtrArray.h>
#include <sead/container/seadSafeArray.h>
#include <sead/prim/seadSafeString.h>

#include "Archipelago/Archipelago.h"

namespace Archipelago {
    enum CappyMessageTypes
    {
        CappyConnect = 0,
        CappyMoon = 1,
        CappyMultiMoon = 2,
        CappyRegionalCoin = 3,
        CappyCap = 4,
        CappyClothes = 5,
        CappySticker = 6,
        CappySouvenir = 7,
        CappyCapture = 8,
        CappyMoonRock = 9,
        CappyAbility = 10,
        CappyHealthUpgrade = 11,
        CappyWalletUpgrade = 12,
        CappyCoins = 13,
        CappySent = 14,
    };

    struct cappyMessage {
        u8 slotNameIndex;
        u8 itemType;
        u8 itemIndex;
        bool isOutgoing;
    };

    static constexpr int kCappyMessageQueueSize = 20;
    static constexpr const char* kArchipelagoCappyLabel = "ArchipelagoCappyMsg";

    static sead::SafeArray<cappyMessage, kCappyMessageQueueSize> sCappyMessages;
    static int sCurrentCappyMessage = 0;
    static int sCurrentCappyQueue = 0;
    static bool sIsCappyMessageActive = false;
    static int sCappyMessageFrameTimer = 0;
    static int sCappyCheckForMessageTimer = 0;

    static sead::WFixedSafeString<kArchipelagoNameSize * 3> sSafeCappyBuffer;
    static bool sCappyBufferInUse = false;

    inline void setCappySlotName(const char* name) {
        sSlotNames[148 + sCurrentCappyQueue] = name;
    }
    inline void setCappyItemName(const char* name) {
        sItemNames[148 + sCurrentCappyQueue] = name;
    };

    void enqueueCappyMessage(cappyMessage message);

    const char16_t* lookupCappyMessageSubstitution(const char* label);

    bool tryShowCappyMessage(StageScene* curScene);
    void buildCappyMessage();
} // namespace Archipelago