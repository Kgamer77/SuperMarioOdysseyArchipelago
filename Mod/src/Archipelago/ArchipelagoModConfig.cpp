// #include "Scene/StageSceneStateModConfig.cpp"
#include "Scene/StageSceneStateModConfig.hpp"

#include "sead/container/seadSafeArray.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Play/Layout/RollParts.h"

#include "Sequence/HakoniwaSequence.h"
#include "System/GameDataFunction.h"
#include "System/GameSystem.h"
#include "game/Layout/CommonVerticalList.h"
#include "game/Layout/FooterParts.h"
#include "game/Layout/SimpleLayoutMenu.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/SaveDataAccessFunction.h"
#include "game/Util/StageInputFunction.h"

#include "Archipelago/Archipelago.h"

#include "SD/SDHooks.h"

#include "nn/err.h"
#include "nn/util.h"

#include "../stage_warp.h"
#include "EntranceDB.h"
#include "ShineDB.h"
#include "heap/seadHeapMgr.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Re-renders the Archipelago rows in place, without going through
// loadRandoMiscPage().
//
// Deliberately does NOT call addStringData(): that function APPENDS - it parks
// the buffer pointer at its internal counter and then increments it - so
// calling it again per refresh would walk off the end of the list's string
// table. It never needs calling again anyway, because the pointer it stored
// the first time is live: rewriting msgList's buffers in place is what the
// list re-reads on updateParts().
//
// applyRandoMiscPageStyles() is included because the masked and unmasked forms
// of the IP/Port rows differ in length and the row styles are what set
// TxtContent's pane size.
void StageSceneStateModConfig::refreshArchipelagoRows() {
    applyRandoMiscPageStyles();
    updateRandomizerMiscOptions();
    if (mCurrentList) {
        // CommonVerticalList CACHES its rendered strings. updateParts() only
        // re-reads the buffers addStringData() pointed it at when this flag is
        // set, so without it the rows keep whatever text they were built with -
        // which is exactly why toggling Show IP and Port left the masked values
        // on screen. updateEnemySwapInspectorOptions() sets the same flag for
        // its own dynamic rows; it is the only reason that page can relabel
        // itself without a full reload.
        mCurrentList->mStringsDirty = true;
        mCurrentList->updateParts();
    }
    repositionRandomizerMiscPanes();
}

void StageSceneStateModConfig::updateArchipelagoOptions() {
    CommonVerticalList* list = optionsList[MENU_RANDOMIZER_MISC];
    if (!list || !list->mListPartsArr)
        return;

    // Archipelago::getIp()/getPort() already report a session override in
    // preference to the .ini value, so these rows show what Connect would
    // actually use without needing to know which of the two it came from.
    char portText[16] = {};
    nn::util::SNPrintf(portText, sizeof(portText), "%d", Archipelago::getPort());

    setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_HOSTNAME],
        "Hostname: %s", Archipelago::getHost());
    setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_PORT], "Port: %s", portText);
    setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_SLOT],
        "Slot Name: %s", Archipelago::getSlot());

    // char masked[80] = {};
    // maskEndpointText(masked, sizeof(masked), Archipelago::getHost());
    // setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_HOSTNAME], "IP Address: %s", masked);
    // maskEndpointText(masked, sizeof(masked), portText);
    // setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_PORT], "Port: %s", masked);

    // if (mApSaveStatusBuf[0])
    //     setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[Ap_SAVE_SETTINGS],
    //         "Save Settings - %s", mApSaveStatusBuf);
    // else if (Archipelago::hasSessionOverride())
    //     msgList[MENU_RANDOMIZER_MISC]->mBuffer[Ap_SAVE_SETTINGS].copy(
    //         u"Save Settings (unsaved changes)");
    // else
    //     msgList[MENU_RANDOMIZER_MISC]->mBuffer[Ap_SAVE_SETTINGS].copy(u"Save Settings");

    // Never masked - the room name is what a player reads out to whoever is
    // joining them, and is the one field they must set themselves.

    if (mApStatusBuf[0])
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_CONNECT],
            "Connect - %s", mApStatusBuf);
    else if (!Archipelago::getHost()[0])
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_CONNECT].copy(
            u"Connect (set a Hostname first)");
    else if (!Archipelago::getSlot()[0])
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_CONNECT].copy(
            u"Connect (set a Slot Name first)");
    else
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[AP_CONNECT].copy(u"Connect");
}

// Starts the Connect row's handshake and returns the same frame.
// Client::connectWithRoomCode() only kicks off the socket thread, so nothing
// here blocks; the wait for the server's room assignment is
// updateArchipelagoConnect()'s job, one frame at a time.
void StageSceneStateModConfig::startArchipelagoConnect() {
#if SMOR_FEATURE_ARCHIPELAGO
    mApConnectPending = false;
    if (!Archipelago::getHost()[0]) {
        nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf), "Set a Hostname first.");
        return;
    }

    nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf), "Connecting...");
    Archipelago::startArchipelago();
    // if (Client::hasRoomError()) {
    //     // Rejected before the socket was even touched (an unusable room code).
    //     nn::util::SNPrintf(
    //         mApStatusBuf, sizeof(mApStatusBuf), "%s", Client::getRoomStatusMessage());
    //     return;
    // }

    mApConnectPending = true;
    mApConnectFrames = 0;
#else
    nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf), "Archipelago not compiled in.");
#endif
}

// Polls the in-flight handshake. Runs every frame while the Connect row is
// waiting, so the game keeps rendering and the row keeps reporting instead of
// the whole thread stalling on the answer.
//
// The timeout matches what the old blocking wait allowed (30s), just spent a
// frame at a time - long enough for a slow server, short enough that a room
// assignment which is never coming eventually says so on the row.
void StageSceneStateModConfig::updateArchipelagoConnect() {
#if SMOR_FEATURE_ARCHIPELAGO
    if (!mApConnectPending)
        return;

    mApConnectFrames++;

    if (Archipelago::isConnected()) {
        mApConnectPending = false;
        nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf), "Connected.");
        // The title's Wait handler picks this up and moves on to the
        // Archipelago file select, matching where the old room-code prompt
        // landed.
        sArchipelagoFileSelectRequest = true;
        exitArchipelagoMenu();
        return;
    }

    // getRoomStatusMessage() carries the server's own reason (bad code, room
    // full, protocol mismatch), which is far more useful on the row than a
    // generic failure string.
    if (Archipelago::isRefused()) {
        mApConnectPending = false;
        nn::util::SNPrintf(
            mApStatusBuf, sizeof(mApStatusBuf), "%s", "Slot or Password is incorrect.");
        refreshArchipelagoRows();
        activateInput();
        return;
    }

    if (mApConnectFrames >= cApConnectTimeoutFrames) {
        mApConnectPending = false;
        nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf),
            "Could not connect to Archipelago. Is the Hostname and port correct?");
        // Randomizer::logPush("ModConfig: connect timed out with no room assignment (socket=%d)",
        //     Client::isSocketActive() ? 1 : 0);
        refreshArchipelagoRows();
        activateInput();
        return;
    }

    // Once a second, so the row shows the wait is alive rather than looking
    // like the frozen state this replaced.
    if ((mApConnectFrames % 60) == 0) {
        nn::util::SNPrintf(mApStatusBuf, sizeof(mApStatusBuf), "Connecting... %ds",
            mApConnectFrames / 60);
        refreshArchipelagoRows();
    }
#endif
}

// Leaves the Archipelago page and the whole ModConfig state, dropping back to
// whatever host put us here (the title menu, in the only flow that reaches
// this).
//
// This is kill() minus SaveDataAccessFunction::startSaveDataWrite(): nothing on
// this page is saved data. Both IP/Port overrides are session-only by design
// and "Show IP and Port" is never persisted, so there is nothing to write - and
// the only route onto this page is the TITLE host, where there is no loaded
// save sitting behind mGameDataHolder to write in the first place. Also
// deactivates the list, which kill() leaves to its caller and which
// endSubMenu() does for every other page.
void StageSceneStateModConfig::exitArchipelagoMenu() {
    showRandoMiscInfoBox(false);
    hideKingdomChecksRouteView();
    mCurrentList->deactivate();
    mCurrentMenu->startEnd("End");
    al::NerveStateBase::kill();
}

// One nerve for all three keyboard rows; mApKeyboardField says which. Modelled
// on exeOpenKeyboardSeed() below, including the
// deactivate -> blocking swkbd -> activate/appearCursor dance.
void StageSceneStateModConfig::exeOpenKeyboardArchipelago() {
    if (!al::isFirstStep(this))
        return;

    mCurrentList->deactivate();

    const s32 field = mApKeyboardField;
    char seed[80] = {};

    switch (field) {
    case AP_HOSTNAME:
        mKeyboard->setHeaderText("Archipelago Hostname");
        mKeyboard->setSubText("IPv4 address or hostname.");
        nn::util::SNPrintf(seed, sizeof(seed), "%s", Archipelago::getHost());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = 63;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    case AP_PORT:
        mKeyboard->setHeaderText("Archipelago Port");
        mKeyboard->setSubText("Port number.");
        nn::util::SNPrintf(seed, sizeof(seed), "%d", Archipelago::getPort());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 5;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    case AP_SLOT:
        mKeyboard->setHeaderText("Slot Name");
        mKeyboard->setSubText("Name of your Archipelago Slot.");
        nn::util::SNPrintf(seed, sizeof(seed), "%d", Archipelago::getPort());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = 63;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    default:
        mKeyboard->setHeaderText("Password");
        mKeyboard->setSubText("Lobby Password. Leave blank for none.");
        nn::util::SNPrintf(seed, sizeof(seed), "%s", Archipelago::getPassword());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = 63;
            config.textMinLength = 0;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
            config.passwordMode = nn::swkbd::PasswordMode::Hide;
        });
        break;
    }

    while (!mKeyboard->isDone())
        nn::os::YieldThread();

    const char* result = mKeyboard->isCancelled() ? nullptr : mKeyboard->getResult();
    if (result && result[0]) {
        switch (field) {
        case AP_HOSTNAME:
            // Session-only: server.ini is deliberately left alone, so pointing
            // at someone else's server for one night never silently becomes the
            // permanent default. See Archipelago::setSessionOverride().
            Archipelago::setHostName(result);
            break;
        case AP_PORT: {
            const int port = atoi(result);
            if (port > 0 && port <= 65535)
                Archipelago::setPort(port);
            break;
        }
        case AP_SLOT: {
            Archipelago::setSlot(result);
            break;
        }
        default:
            // Password
            Archipelago::setPassword(result);
            break;
        }
        // A stale "Connected." / error line no longer describes these settings,
        // and neither does a stale "Saved to server.ini." - what was just typed
        // is unsaved again.
        mApStatusBuf[0] = '\0';
        // mApSaveStatusBuf[0] = '\0';
    }

    mApKeyboardField = -1;
    mApRestoreRow = field;
    al::startHitReaction(mCurrentMenu, "\u30ea\u30bb\u30c3\u30c8", 0);
    mCurrentList->activate();
    mCurrentList->appearCursor();
    al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
}