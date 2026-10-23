#include "game/Scene/StageSceneStatePauseMenu.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/server/Client.hpp"
#include "Scene/StageSceneStateFileSelect.hpp"

#include "al/Library/Message/MessageHolder.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Scene/SceneUtil.h"

#include "game/Layout/FooterParts.h"
#include "game/Scene/StageScene.h"

#include "Randomizer/PauseMenuHooks.h"
#include "Randomizer/Randomizer.h"

namespace {
    constexpr const char* cPauseIgnore2DKit = "\xef\xbc\x92\xef\xbc\xa4\xef\xbc\x88\xe3\x83\x9d\xe3\x83\xbc\xe3\x82\xba\xe7\x84\xa1\xe8\xa6\x96\xef\xbc\x89";
}

void StageSceneStatePauseMenu::exeModConfig(void) {
    if (PauseMenuHooks::consumeAuditMenuCloseRequest()) {
        // The audit runner advances from HakoniwaSequence after this host
        // state returns. Leaving through the native End nerve clears the
        // pause/camera gate without calling ModConfig::kill(), which would
        // schedule an unrelated configuration save.
        al::endCameraPause(mPauseCameraCtrl);
        al::setNerve(this, &NrvStageSceneStatePauseMenu.End);
        return;
    }
    if (al::isFirstStep(this)) {
    }

    al::updateKitListPrev(mHost);
    rs::requestGraphicsPresetAndCubeMapPause(mHost);
    al::updateKitList(mHost, cPauseIgnore2DKit);
    al::updateKitListPost(mHost);

    if (al::updateNerveState(this)) {
        if (mStateOption->isChangeLanguage() || mStateOption->mIsLoadData) {
            kill();
        } else {
            mSelectParts->appearWait();
            mFooterParts->tryChangeTextFade(al::getSystemMessageString(mMenuGuide, "Footer", "MenuMessage_Footer"));

            al::setNerve(this, &NrvStageSceneStatePauseMenu.Wait);
        }
    }
}

void StageSceneStatePauseMenu::exeFileSelect(void) {
    const bool firstStep = al::isFirstStep(this);

    // Room-code keyboard prompt for the title's Multiplayer row
    // (PauseMenuHooks.cpp sets this right before transitioning here). Runs
    // the blocking swkbd call on FileSelect's own first tick rather than on
    // a dedicated nerve of its own - a dedicated third custom nerve
    // (RoomCodeKeyboard) was tried first and made the original "stuck after
    // cancel" bug WORSE: confirmed via mp_debug log that bumping the
    // R_ZN24StageSceneStatePauseMenuNrvStateCount patch from 6 to 7 to
    // register it corrupted adjacent state - al::isFirstStep(this) read
    // true on every subsequent frame forever after returning to Wait, and
    // pad trigger detection went dead with it. FileSelect already has a
    // proven-safe path back to Wait (the no-start-requested branch below),
    // so reusing it needs no new nerve slot and no NrvStateCount change.
    if (firstStep && PauseMenuHooks::consumePendingRoomCodePrompt()) {
        MpDebugLog::write("exeFileSelect: pending room code prompt, calling promptRoomCodeAndConnect");
        const bool connected = Client::promptRoomCodeAndConnect();
        MpDebugLog::write("exeFileSelect: promptRoomCodeAndConnect returned %d", connected ? 1 : 0);

        if (connected) {
            // Multiplayer's own, fully separate save-file category - see
            // SaveRedirect::Category. Fall through into the normal
            // FileSelect flow below, just pointed at MPSaves/ instead of
            // Saves/.
            // Same owner/guest split the title Multiplayer menu's Connect row
            // makes (PauseMenuHooks.cpp) - this legacy room-code entry point
            // is kept working rather than left as the one route that drops a
            // guest on a seed-making file select.
            const bool isSetupOwner = Client::isSetupOwner();
            StageSceneStateFileSelect::setGuestJoinMode(!isSetupOwner);
            StageSceneStateFileSelect::setMultiplayerHostMode(isSetupOwner);
            if (gStageSceneStateFileSelect) {
                gStageSceneStateFileSelect->setSaveCategory(isSetupOwner
                        ? SaveRedirect::Category::Multiplayer
                        : SaveRedirect::Category::MultiplayerGuest);
                MpDebugLog::write("exeFileSelect: setSaveCategory called, setupOwner=%d readback=%d",
                    isSetupOwner ? 1 : 0, (int)gStageSceneStateFileSelect->getSaveCategory());
            }
        } else {
            // disconnect() is safe to call even when never connected
            // (early-returns if there's nothing to do) - unconditionally
            // drops/resets any client state a cancelled prompt might have
            // left behind before returning to the idle title.
            MpDebugLog::write("exeFileSelect: cancelled, calling Client::disconnect");
            Client::disconnect();
            // The actual missing piece, found by diffing against the
            // proven-working reference keyboard functions
            // (StageSceneStateFileSelect::exeOpenKeyboardTrackerRoomCode/
            // exeOpenKeyboardTabName): both of those call
            // mSelectParts->appearWait() (their mCurrentList equivalent)
            // right before landing back on their idle nerve - the same call
            // the sibling "no start requested" branch below already makes
            // for every OTHER FileSelect -> Wait transition. This cancel
            // path skipped straight to setNerve(Wait) without it, leaving
            // mSelectParts never told to "wake back up" for input after the
            // multi-second blocking swkbd call - matches the symptom
            // exactly: isFirstStep read true forever and pad-trigger
            // detection went dead, both consistent with mSelectParts never
            // resuming normal per-frame updates.
            mSelectParts->appearWait();
            mFooterParts->tryChangeTextFade(al::getSystemMessageString(mMenuGuide, "Footer", "MenuMessage_Footer"));
            MpDebugLog::write("exeFileSelect: appearWait done, setNerve(Wait)");
            al::setNerve(this, &NrvStageSceneStatePauseMenu.Wait);
            MpDebugLog::write("exeFileSelect: setNerve(Wait) done, returning");
            return;
        }
    }

    if (firstStep)
        Randomizer::logPush("exeFileSelect: nerve entered (first step), subState=%p", gStageSceneStateFileSelect);

    al::updateKitListPrev(mHost);
    if (firstStep)
        Randomizer::logPush("exeFileSelect: after updateKitListPrev");
    rs::requestGraphicsPresetAndCubeMapPause(mHost);
    if (firstStep)
        Randomizer::logPush("exeFileSelect: after requestGraphicsPreset");
    al::updateKitList(mHost, cPauseIgnore2DKit);
    al::updateKitListPost(mHost);

    // FileSelect handles logical state here; updateKitList above drives layout
    // movement/animation for the pause-ignore kit.
    if (gStageSceneStateFileSelect)
        gStageSceneStateFileSelect->updateMenus();

    if (al::updateNerveState(this)) {
        if (gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested()) {
            const StageSceneStateFileSelect::StartMode startMode = gStageSceneStateFileSelect->getStartMode();
            if (mStateOption && (startMode == StageSceneStateFileSelect::StartMode::Load || startMode == StageSceneStateFileSelect::StartMode::NewGame))
                mStateOption->mIsLoadData = true;
            // Always take the load path, never the vanilla new-game path. New
            // games route through DemoOpening -> FirstSequenceScene ->
            // SceneStateSelectMode (the assist-mode picker), which crashes here
            // on null game data. The new slot's File1.bin is already freshly
            // initialized by exeActivateSlot, so loading it gives a clean new
            // game; the randomizer redirects the start kingdom on stage init.
            mIsNewGame = false;
            Randomizer::logPush("exeFileSelect: start requested mode=%d -> mIsLoadData=%d End nerve",
                (int)startMode, mStateOption ? (int)mStateOption->mIsLoadData : -1);
            al::endCameraPause(mPauseCameraCtrl);
            al::setNerve(this, &NrvStageSceneStatePauseMenu.End);
        } else {
            mSelectParts->appearWait();
            mFooterParts->tryChangeTextFade(al::getSystemMessageString(mMenuGuide, "Footer", "MenuMessage_Footer"));
            al::setNerve(this, &NrvStageSceneStatePauseMenu.Wait);
        }
    }
}
