#include "Archipelago/ArchipelagoCappyMessage.h"

#include "game/MapObj/CapMessageShowInfo.h"

#include "Archipelago/MessengerData.h"

const char16_t* Archipelago::lookupCappyMessageSubstitution(const char* label) {
    if (!label)
        return nullptr;
    // Cheap-first: vast majority of MSBT lookups are not for our label.
    // kArchipelagoCappyLabel starts with 'A'.
    if (label[0] != 'A')
        return nullptr;
    if (strcmp(label, kArchipelagoCappyLabel) != 0)
        return nullptr;

    return sSafeCappyBuffer.cstr();
}

bool Archipelago::tryShowCappyMessage(StageScene* curScene) {
    al::IUseSceneObjHolder* curObjHolder = (al::IUseSceneObjHolder*)curScene;

    // Stop MessageBuffer from being edited or read before ready
    // Start read prevention in buildCappyMessage
    // End read prevention at tryShow
    bool isActive = rs::isActiveCapMessage(curObjHolder);
    if (isActive) {
        sCappyMessageFrameTimer = 100;
    }

    if (sCappyMessageFrameTimer > 0 || sCappyBufferInUse || sCappyMessages[sCurrentCappyMessage].itemType == 255) {
        if (!isActive) {
            sCappyMessageFrameTimer -= 1;
            if (sCappyMessageFrameTimer <= 0)
                sCappyBufferInUse = false;
        }
        return false;
    }

    buildCappyMessage();

    if (rs::tryShowCapMessagePriorityLow(curObjHolder, kArchipelagoCappyLabel, 180, 0)) {
        // Reset the read message
        sead::FixedSafeString<64> debugStr = sead::FixedSafeString<64>();
        debugStr = "Current Cappy Message: Slot: ";
        debugStr.append(intToCstr(sCappyMessages[sCurrentCappyMessage].slotNameIndex));
        debugStr.append(" , Type: ");
        debugStr.append(intToCstr(sCappyMessages[sCurrentCappyMessage].itemType));
        debugStr.append(" , Item: ");
        debugStr.append(intToCstr(sCappyMessages[sCurrentCappyMessage].itemIndex));
        sCappyMessages[sCurrentCappyMessage] = { 255, 255, 255, false };
        sCurrentCappyMessage = (sCurrentCappyMessage + 1) % kCappyMessageQueueSize;
        // mNextCappyMessage = (mNextCappyMessage + 1) % kCappyMessageQueueSize;
        return true;
    }

    return false;
}

void Archipelago::buildCappyMessage() {
    if (sCappyBufferInUse)
        return;
    sCappyBufferInUse = true;
    cappyMessage curMessage = sCappyMessages[sCurrentCappyMessage];
    // const char* tags[] = {"<name>", "<item>", "<type>", ""};
    GameDataHolderAccessor accessor(sCurScene);

    bool hasName = !sSlotNames[curMessage.slotNameIndex].isEmpty();
    if (curMessage.itemType == 0) {
        sSafeCappyBuffer = u"Connected to Archipelago as ";
    } else {
        if (curMessage.isOutgoing) {
            sSafeCappyBuffer = u"Sent ";
        } else {
            if (hasName)
                sSafeCappyBuffer = u"Got ";
            else
                sSafeCappyBuffer = u"Found ";
        }

        if (curMessage.itemType < CappyMessageTypes::CappyRegionalCoin) {
            getColor(ProjectTextColors::Yellow, &sSafeCappyBuffer);

            appendUtf8ToUtf16(kingdoms[curMessage.itemIndex], &sSafeCappyBuffer);
        }

        else if (curMessage.itemType == CappyMessageTypes::CappyRegionalCoin) {
            // appendUtf8ToUtf16(kingdoms[curMessage.itemIndex], &sSafeCappyBuffer);
            sSafeCappyBuffer.append(getRegionalCoinIcon(curMessage.itemIndex));

        }

        else if (curMessage.itemType <= CappyMessageTypes::CappyCapture) {
            appendUtf8ToUtf16(sItemNames[curMessage.itemIndex].cstr(), &sSafeCappyBuffer);
        }

        else if (curMessage.itemType == CappyMessageTypes::CappyAbility) {
            appendUtf8ToUtf16(abilities[curMessage.itemIndex], &sSafeCappyBuffer);
        }

        else if (curMessage.itemType == CappyMessageTypes::CappyCoins) {
            appendUtf8ToUtf16(intToCstr(curMessage.itemIndex), &sSafeCappyBuffer);
            sSafeCappyBuffer.append(u" ");
            sSafeCappyBuffer.append(getMiscIcon(1));
        }

        sSafeCappyBuffer.append(u" ");
        getColor(ProjectTextColors::Yellow, &sSafeCappyBuffer);

        appendUtf8ToUtf16(SMOItems[curMessage.itemType], &sSafeCappyBuffer);
        getColor(ProjectTextColors::ResetColor, &sSafeCappyBuffer);

        if (hasName) {
            if (curMessage.isOutgoing)
                sSafeCappyBuffer.append(u" to ");
            else
                sSafeCappyBuffer.append(u" from ");
        }
    }

    if (hasName) {
        getColor(ProjectTextColors::Blue, &sSafeCappyBuffer);

        appendUtf8ToUtf16(sSlotNames[curMessage.slotNameIndex].cstr(), &sSafeCappyBuffer);
        getColor(ProjectTextColors::ResetColor, &sSafeCappyBuffer);
    }

    int replacedSpaceIndex = 0;
    for (int i = 0; i < sSafeCappyBuffer.calcLength(); i++) {
        if (sSafeCappyBuffer[i] == u' ') {
            replacedSpaceIndex = i;
        }
        if (i > 32) {
            sSafeCappyBuffer.getBuffer()[replacedSpaceIndex] = u'\n';
            break;
        }
    }
    sSafeCappyBuffer.append(u".");
    sSafeCappyBuffer.replaceChar(u'\xf777', u'\x0000');

    return;
}