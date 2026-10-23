#include "Randomizer/PauseMenuHooks.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/StartPositionRando.h"
#include "Scene/StageSceneStateFileSelect.hpp"
#include "Scene/StageSceneStateModConfig.hpp"

#include "Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Scene/Scene.h"
#include "al/Library/Scene/SceneUtil.h"
#include "sead/heap/seadHeapMgr.h"

#include "game/Layout/FooterParts.h"
#include "game/Layout/MenuSelectParts.h"
#include "game/Layout/StageSceneLayout.h"
#include "game/Scene/StageScene.h"
#include "game/Scene/StageSceneStateOption.h"
#include "game/Scene/StageSceneStatePauseMenu.h"
#include "game/Scene/TitleMenuScene.h"
#include "game/System/GameDataHolder.h"
#include "game/Util/StageInputFunction.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "hk/ro/RoUtil.h"

#include "nn/oe.h"

#include <cstring>

class SceneAudioSystemPauseController;

namespace PauseMenuHooks {

    static constexpr const char16_t* cRandomizedAdventureLoadingText1 = u"Enjoy this";
    static constexpr const char16_t* cRandomizedAdventureLoadingText2 = u"Randomized Adventure";

    static constexpr const char* cDecideHitReaction = "\xe6\xb1\xba\xe5\xae\x9a"; // "決定"

    // Declared early so lambdas below can reference them.
    static StageSceneStateModConfig* sceneStateModConfig = nullptr;
    static StageSceneStatePauseMenu* sModConfigPauseMenu = nullptr;
    // WorldMapStage still constructs StageSceneStatePauseMenu even though its
    // normal pause UI is unreachable.  Building the eight ModConfig layouts in
    // that transition-only stage exhausts SceneHeap before Scene::endInit can
    // allocate the actor executor tables.
    static bool sSkipModConfigConstruction = false;
    static bool sAuditMenuCloseRequested = false;
    static bool sPendingRoomCodePrompt = false;

    StageSceneStateModConfig* getModConfigInstance() { return sceneStateModConfig; }
    void requestAuditMenuClose() { sAuditMenuCloseRequested = true; }
    bool consumeAuditMenuCloseRequest() {
        const bool requested = sAuditMenuCloseRequested;
        sAuditMenuCloseRequested = false;
        return requested;
    }
    bool consumePendingRoomCodePrompt() {
        const bool requested = sPendingRoomCodePrompt;
        sPendingRoomCodePrompt = false;
        return requested;
    }

    // Captured in the pause-menu ctor hook when constructed in title mode
    // (isTitle == true). This is the authoritative title-host test, independent
    // of any field-offset assumptions.
    static StageSceneStatePauseMenu* sTitlePauseMenu = nullptr;
    static s32 sTitleStartPendingFrames = 0;
    static constexpr s32 cTitleStartRedirectDelay = 8;

    static void setRandomizedAdventureLoadingText(TitleMenuScene* scene) {
        if (!scene || !scene->mLayoutContinueLoading)
            return;

        if (al::isExistPane(scene->mLayoutContinueLoading, "TxtGoal")) {
            al::showPane(scene->mLayoutContinueLoading, "TxtGoal");
            al::setPaneLocalSize(scene->mLayoutContinueLoading, "TxtGoal", { 1280.0f, 80.0f });
            al::setPaneLocalScale(scene->mLayoutContinueLoading, "TxtGoal", { 0.70f, 0.70f });
            al::setPaneString(scene->mLayoutContinueLoading, "TxtGoal",
                cRandomizedAdventureLoadingText1, 0);
        }
        if (al::isExistPane(scene->mLayoutContinueLoading, "TxtScenario")) {
            al::showPane(scene->mLayoutContinueLoading, "TxtScenario");
            al::setPaneLocalSize(scene->mLayoutContinueLoading, "TxtScenario", { 1280.0f, 80.0f });
            al::setPaneLocalScale(scene->mLayoutContinueLoading, "TxtScenario", { 0.70f, 0.70f });
            al::setPaneString(scene->mLayoutContinueLoading, "TxtScenario",
                cRandomizedAdventureLoadingText2, 0);
        }
    }

    // exeFadeBeforeHelp → ModConfig redirect (in-game only).
    // The title pause menu must never enter this path: it has no WipeSimple and
    // ModConfig showing over the FileSelect produces the empty overlay bug.
    static HkTrampoline<void, StageSceneStatePauseMenu*> overrideHelpFadeNerve = hk::hook::trampoline([](StageSceneStatePauseMenu* state) -> void {
        if (state == sTitlePauseMenu) {
            // Title menu should not enter FadeBeforeHelp at all; if it somehow
            // does, skip back to Wait so we don't leave the menu stuck.
            al::setNerve(state, &NrvStageSceneStatePauseMenu.Wait);
            return;
        }
        if (state != sModConfigPauseMenu || !sceneStateModConfig) {
            overrideHelpFadeNerve.orig(state);
            return;
        }
        state->exeModConfig();
        al::setNerve(state, &NrvStageSceneStatePauseMenu.ModConfig);
    });

    // Title row indices for the rows we now show (see setAfterTitleHook below).
    // Raw mCursorItemIndex values on the title's own select list, NOT the
    // vanilla action-table indirection selectActionTable()/
    // setSelectMessageByAction() use elsewhere in this file - those exist to
    // find where a VANILLA action (SeparatePlay, Help, ...) landed among
    // vanilla's own rows. On the title we built these rows ourselves, so their
    // positions are just the order we put them in, no table lookup needed.
    //
    // Which rows exist depends on which features are compiled in: Randomize! is
    // always row 0, and Multiplayer / Archipelago each take the next slot only
    // when their own flag is on. A compiled-out row's index is -1, which
    // mCursorItemIndex can never hold, so every comparison against it below is
    // simply never true.
    static constexpr s32 cTitleRowRandomize = 0;
    static constexpr s32 cTitleRowMp = Randomizer::FeatureFlags::Multiplayer ? 1 : -1;
    static constexpr s32 cTitleRowAp = Randomizer::FeatureFlags::Archipelago
        ? (Randomizer::FeatureFlags::Multiplayer ? 2 : 1)
        : -1;
    static constexpr s32 cTitleRowCount = 1 + (Randomizer::FeatureFlags::Multiplayer ? 1 : 0)
        + (Randomizer::FeatureFlags::Archipelago ? 1 : 0);

    // Which title row was highlighted when decide fired - captured at decide
    // time (mCursorItemIndex is only meaningful then) and read again once
    // sTitleStartPendingFrames' countdown elapses, since that branch doesn't
    // otherwise know which row triggered it.
    static s32 sTitleSelectedRow = cTitleRowRandomize;

    // Keep vanilla title-menu setup alive for the animated Mario/title state, then
    // clamp the select list to our Randomize!/MP/AP rows.
    static HkTrampoline<void, StageSceneStatePauseMenu*> setAfterTitleHook = hk::hook::trampoline([](StageSceneStatePauseMenu* state) -> void {
        // Deliberately do NOT call orig. Vanilla setAfterTitle calls
        // MenuSelectParts::appear(5) and fills three rows (Start / Resume 2P /
        // New Game); the select layout is *constructed* with six physical ParList
        // rows, and the trailing rows carry layout-default text ("Action Guide",
        // "Options"). appear() only animates indices [0, count) — it never hides
        // the other rows — so the menu kept rendering all of them. Show only the
        // rows we built, then explicitly hide the rest.
        MenuSelectParts* selectParts = state->mSelectParts;
        state->field_0xd8 &= ~0xFF; // clear the isSeparatePlay byte (offset 0xD8)
        // appear() resets its own +0x58 to 0 (main layout)
        selectParts->appear(cTitleRowCount);
        selectParts->setMainMenu(true);

        // A row is visible because of its TEXT (rows we left unlabelled vanished;
        // rows carrying the layout's baked "Action Guide"/"Options" labels stayed),
        // so blank every row past the last one we built. The index map ALIASES —
        // blanking one of the upper logical rows also writes one of our own rows'
        // physical pane — so set our labels LAST, after the blanking loop, so they
        // win. The off-screen move stays as a backstop. (al::hidePaneRoot is
        // declared but not exported in this build.)
        if (selectParts->mLayoutArray) {
            const sead::Vector3f kOffscreen(0.0f, -100000.0f, 0.0f);
            for (s32 i = cTitleRowCount; i < selectParts->mMax; i++) {
                selectParts->setSelectMessage(i, u"");
                if (selectParts->mLayoutArray[i])
                    al::setLocalTrans(selectParts->mLayoutArray[i], kOffscreen);
            }
        }
        selectParts->setSelectMessage(cTitleRowRandomize, u"Randomize!");
        if (Randomizer::FeatureFlags::Multiplayer)
            selectParts->setSelectMessage(cTitleRowMp, u"Multiplayer");
        if (Randomizer::FeatureFlags::Archipelago)
            selectParts->setSelectMessage(cTitleRowAp, u"Archipelago");
    });

    static HkTrampoline<void, StageSceneStateOption*, const char*, al::Scene*, const al::LayoutInitInfo&, FooterParts*, GameDataHolder*, bool>
        initStateHook = hk::hook::trampoline([](StageSceneStateOption* thisPtr, const char* stateName, al::Scene* host,
                                                 const al::LayoutInitInfo& initInfo, FooterParts* footer, GameDataHolder* data, bool unkBool) -> void {
            initStateHook.orig(thisPtr, stateName, host, initInfo, footer, data, unkBool);
            // KNOWN ISSUE: sceneStateModConfig is reassigned here on every single
            // stage load (this ctor hook fires once per StageScene::init) with no
            // destructor and no delete of the previous instance - the whole
            // 7-submenu object graph (layouts, lists, footers, the keyboard)
            // accumulates forever. Two fix attempts at deleting/rehoming the
            // allocation itself were tried and REVERTED (froze at boot; caused a
            // different crash in native teardown - NerveStateBase's real,
            // unavailable-source destructor doing more than a bare delete
            // accounted for). Real dominant root cause found and fixed instead:
            // Keyboard's per-instance nn::swkbd work buffer (src/Keyboard.cpp)
            // was leaking a large chunk of gHeap on every one of these
            // reconstructions - now a shared, allocate-once static. This log
            // line is kept as a cheap ongoing sanity check, not a fix by itself.
            //
            // Merged from origin/main: sSkipModConfigConstruction (set below)
            // skips this allocation entirely on WorldMapStage, which constructs
            // the pause menu unnecessarily - reduces how often this
            // reconstruction happens at all, independent of the Keyboard fix.
            if (sSkipModConfigConstruction) {
                sceneStateModConfig = nullptr;
            } else {
                MpDebugLog::write("ModConfig leak: ambient heap=%p gHeap=%p match=%d",
                    sead::HeapMgr::instance()->getCurrentHeap(), gHeap,
                    sead::HeapMgr::instance()->getCurrentHeap() == gHeap);
                sceneStateModConfig = new StageSceneStateModConfig("ModConfig", host, initInfo, footer, data, unkBool);
            }
        });

    static HkTrampoline<void, StageSceneStatePauseMenu*, const char*, al::Scene*, al::SimpleLayoutAppearWaitEnd*,
        GameDataHolder*, const al::SceneInitInfo&, const al::ActorInitInfo&, const al::LayoutInitInfo&,
        al::WindowConfirm*, StageSceneLayout*, bool, SceneAudioSystemPauseController*>
        initNerveStateHook = hk::hook::trampoline([](StageSceneStatePauseMenu* state, const char* name, al::Scene* host,
                                                      al::SimpleLayoutAppearWaitEnd* menuLayout, GameDataHolder* gameDataHolder,
                                                      const al::SceneInitInfo& sceneInitInfo, const al::ActorInitInfo& actorInitInfo,
                                                      const al::LayoutInitInfo& layoutInitInfo, al::WindowConfirm* windowConfirm,
                                                      StageSceneLayout* stageSceneLayout, bool isTitle, SceneAudioSystemPauseController* sceneAudioSystemPauseController) -> void {
            // The vanilla pause-menu ctor (orig) constructs StageSceneStateOption,
            // which our StageSceneStateOption ctor hook uses to `new` the ModConfig
            // sub-state. On the title host that ModConfig must NOT pull live
            // CaptureRando / AbilityRando + save state into its debug menus: that
            // data is only valid in-game, and reading it while the title scene
            // re-initialises (the "Return to Main Menu" path) crashes in the
            // capture-debug row build. Suppress live-data population for the
            // duration of the title ctor; those debug menus are never shown on the
            // title anyway.
            const bool skipModConfig = !isTitle && sceneInitInfo.initStageName
                && std::strcmp(sceneInitInfo.initStageName, "WorldMapStage") == 0;
            StageSceneStateModConfig::sSuppressLiveData = isTitle;
            sSkipModConfigConstruction = skipModConfig;
            sModConfigPauseMenu = nullptr;
            initNerveStateHook.orig(state, name, host, menuLayout, gameDataHolder, sceneInitInfo, actorInitInfo,
                layoutInitInfo, windowConfirm, stageSceneLayout, isTitle, sceneAudioSystemPauseController);
            StageSceneStateModConfig::sSuppressLiveData = false;
            sSkipModConfigConstruction = false;
            if (sceneStateModConfig) {
                al::initNerveState(state, sceneStateModConfig,
                    &NrvStageSceneStatePauseMenu.ModConfig,
                    "CustomNerveOverride");
                sModConfigPauseMenu = state;
                Randomizer::logPush(
                    "ctorHook: menu=%p isTitle=%d ModConfig registered",
                    state, (int)isTitle);
            } else {
                Randomizer::logPush(
                    "ctorHook: menu=%p stage=%s ModConfig skipped",
                    state, sceneInitInfo.initStageName ? sceneInitInfo.initStageName : "?");
            }
            if (isTitle) {
                sTitlePauseMenu = state;
                sTitleStartPendingFrames = 0;
                gStageSceneStateFileSelect = new StageSceneStateFileSelect(
                    "FileSelect", host, layoutInitInfo, state->mFooterParts, gameDataHolder);
                al::initNerveState(state, gStageSceneStateFileSelect,
                    &NrvStageSceneStatePauseMenu.FileSelect, "FileSelect");
                Randomizer::logPush("ctorHook: FileSelect registered sub=%p nerve=%p",
                    gStageSceneStateFileSelect, &NrvStageSceneStatePauseMenu.FileSelect);
            }
        });

    // MenuSelectParts maps a menu ROW to an action id through one of two static
    // tables, picked by its +0x58 "main menu" byte:
    //
    //     setSelectMessage(row, text) -> layoutArray[table[row]] "TxtContent"
    //     isDecideSeparatePlay()      -> table[selected] == 1
    //     isDecideHelp()              -> table[selected] == 3
    //
    // So a hardcoded row index is guessing at the table's contents, and guessing
    // wrong is silent: setSelectMessage no-ops when the mapped layout slot is
    // null. Read the same table the game reads and label the row that actually
    // carries the action.
    static constexpr ptrdiff_t cSelectActionTableMain = 0x18AF570;
    static constexpr ptrdiff_t cSelectActionTableSeparate = 0x18AF55C;
    static constexpr s32 cActionSeparatePlay = 1;

    static const s32* selectActionTable(const MenuSelectParts* parts) {
        const bool isSeparateTable = *(reinterpret_cast<const u8*>(parts) + 0x58) != 0;
        const ptrdiff_t offset
            = isSeparateTable ? cSelectActionTableSeparate : cSelectActionTableMain;
        return reinterpret_cast<const s32*>(hk::ro::getMainModule()->range().start() + offset);
    }

    // Fallback row for the 2P entry if the table scan matches nothing (a zero
    // mMax before appear() has sized the list would do it). Vanilla order is
    // Continue / Two Players / Action Guide / Save / Options.
    static constexpr s32 cSeparatePlayFallbackRow = 1;

    // Diagnostic for the other half of the gate: if the caption never appears AND
    // this is the only line in the log, the start node was not active here.
    static void logPauseLabelSkipOnce() {
        static bool sLogged = false;
        if (sLogged)
            return;
        sLogged = true;
        Randomizer::logPush("pause label skipped: start position inactive (mode=%d)",
            (s32)Randomizer::getStartPositionMode());
    }

    static void setSelectMessageByAction(MenuSelectParts* parts, s32 action, const char16_t* text) {
        if (!parts)
            return;

        bool matched = false;
        const s32* table = selectActionTable(parts);
        for (s32 row = 0; row < parts->mMax; row++) {
            if (table[row] == action) {
                parts->setSelectMessage(row, text);
                matched = true;
            }
        }
        if (!matched)
            parts->setSelectMessage(cSeparatePlayFallbackRow, text);

        static bool sLoggedOnce = false;
        if (!sLoggedOnce) {
            sLoggedOnce = true;
            Randomizer::logPush(
                "pause label: action=%d matched=%d mMax=%d sep=%d table=[%d,%d,%d,%d,%d]",
                action, matched ? 1 : 0, parts->mMax,
                (s32) * (reinterpret_cast<const u8*>(parts) + 0x58),
                table[0], table[1], table[2], table[3], table[4]);
        }
    }

    static HkTrampoline<void, StageSceneStatePauseMenu*> pauseMenuExeWaitHook = hk::hook::trampoline([](StageSceneStatePauseMenu* menu) -> void {
        const bool isTitleHost = (menu == sTitlePauseMenu);

        if (isTitleHost) {
            if (al::isFirstStep(menu))
                MpDebugLog::write("pauseMenuExeWaitHook: Wait entered (first step)");

            if (!menu->mSelectParts) {
                al::setNerve(menu, &NrvStageSceneStatePauseMenu.FileSelect);
                return;
            }

            // The Multiplayer menu's Connect row succeeded and killed the
            // ModConfig state, which lands us back here. Continue on to the
            // multiplayer file select - the same destination the old room-code
            // prompt reached from exeFileSelect. Consuming the request clears
            // it, so simply backing out of the menu later cannot re-trigger it.
            if (StageSceneStateModConfig::consumeMultiplayerFileSelectRequest()) {
                // Which half of the room this is decides which screen comes
                // next, and the connect handshake already waited for the
                // server to say (Client::connectAndAwaitRoom blocks until the
                // role lands, precisely so this branch has an answer).
                //
                // The setup owner - the first client in the room - makes the
                // seed, so it gets the normal multiplayer file select.
                // Everyone else joins that seed instead of making one, so they
                // get guest join mode (StageSceneStateFileSelect's
                // setGuestJoinMode). Gated on NOT being the owner rather than
                // on RoomRole::Guest specifically, because the server hands
                // out RoomRole::Player, not Guest, to anyone who arrives after
                // the room is already Ready (RoomSession.RoleFor) - joining a
                // room that is already running has to take the same path.
                const bool isSetupOwner = Client::isSetupOwner();
                MpDebugLog::write(
                    "pauseMenuExeWaitHook: MP connect ok, setupOwner=%d -> %s file select",
                    isSetupOwner ? 1 : 0, isSetupOwner ? "MP" : "guest join");
                StageSceneStateFileSelect::setGuestJoinMode(!isSetupOwner);
                StageSceneStateFileSelect::setMultiplayerHostMode(isSetupOwner);
                if (gStageSceneStateFileSelect)
                    gStageSceneStateFileSelect->setSaveCategory(isSetupOwner
                            ? SaveRedirect::Category::Multiplayer
                            : SaveRedirect::Category::MultiplayerGuest);
                al::setNerve(menu, &NrvStageSceneStatePauseMenu.FileSelect);
                return;
            }

            if (sTitleStartPendingFrames > 0) {
                menu->mSelectParts->update();
                sTitleStartPendingFrames++;
                if (sTitleStartPendingFrames >= cTitleStartRedirectDelay) {
                    sTitleStartPendingFrames = 0;
                    if (sTitleSelectedRow == cTitleRowMp) {
                        // Route through the EXISTING FileSelect nerve rather
                        // than a brand-new custom one. A dedicated third
                        // custom nerve (RoomCodeKeyboard) was tried first and
                        // made things WORSE - confirmed via mp_debug log:
                        // after landing back on Wait, al::isFirstStep(menu)
                        // read true on every single subsequent frame forever
                        // (280+ frames straight in one capture) and pad
                        // trigger detection went dead with it, i.e. bumping
                        // the R_ZN24StageSceneStatePauseMenuNrvStateCount
                        // patch from 6 to 7 to accommodate it corrupted
                        // adjacent nerve-keeper/pad state rather than safely
                        // growing some dynamic table - that symbol is
                        // load-bearing for something with a real upper bound,
                        // not a simple "+1 per custom nerve added" counter
                        // like ModConfig+FileSelect going from a presumed 4
                        // to 6 suggested. FileSelect's own exeFileSelect
                        // already has a proven-safe FileSelect -> Wait
                        // transition (its no-start-requested else branch), so
                        // the room-code prompt now happens on FileSelect's
                        // own first tick instead (gated by
                        // consumePendingRoomCodePrompt(), see
                        // StageSceneStatePauseMenu.cpp) - zero new nerve
                        // slots, zero NrvStateCount changes.
                        // The room-code keyboard this used to open directly
                        // is now one row inside a proper Multiplayer menu
                        // (Show IP and Port / IP / Port / Room Name /
                        // Connect), which lives on ModConfig's Multiplayer
                        // page. ModConfig is an ALREADY-REGISTERED nerve on
                        // this same host object (see initNerveStateHook
                        // above), so this adds no nerve slot and needs no
                        // NrvStateCount change - the thing that corrupted
                        // pad/nerve state when a dedicated RoomCodeKeyboard
                        // nerve was tried, described at length above.
                        //
                        // sPendingRoomCodePrompt / promptRoomCodeAndConnect()
                        // are deliberately left intact but no longer
                        // triggered from here: if this route ever misbehaves
                        // on hardware, restoring the old flow is these two
                        // lines again.
                        StageSceneStateModConfig::requestMultiplayerPageOnAppear();
                        al::setNerve(menu, &NrvStageSceneStatePauseMenu.ModConfig);
                    } else {
                        // Randomize! always means single-player, even if a
                        // previous visit to this same title session left the
                        // category on Multiplayer (backed out of MP file-
                        // select without picking a slot, then chose
                        // Randomize! instead). Also drop any live MP
                        // connection from that earlier visit - confirmed real
                        // bug: entering a room code, then backing out to play
                        // Randomize! instead, left the socket connected
                        // through the whole single-player session.
                        Client::disconnect();
                        // Same reasoning as the category reset below it: a
                        // previous visit to Multiplayer this session could
                        // have left guest join mode on, and it must never
                        // survive into a single-player file select.
                        StageSceneStateFileSelect::setGuestJoinMode(false);
                        StageSceneStateFileSelect::setMultiplayerHostMode(false);
                        if (gStageSceneStateFileSelect)
                            gStageSceneStateFileSelect->setSaveCategory(
                                SaveRedirect::Category::SinglePlayer);
                        al::setNerve(menu, &NrvStageSceneStatePauseMenu.FileSelect);
                    }
                }
                return;
            }

            // On row 0 the item is the vanilla "Continue" selection (message key
            // "StartGame"), which is INVALID with our empty save, so do not let
            // the pause-menu state consume the decide. Let the select parts
            // update on the trigger frame, though, because that is the vanilla
            // path that plays the menu decide sound. The MP row rides the same
            // sound/countdown for parity.
            if (rs::isTriggerUiDecide(menu->mHost) || al::isPadTriggerA(-1)) {
                MpDebugLog::write("pauseMenuExeWaitHook: A/decide trigger, cursorRow=%d",
                    menu->mSelectParts->mCursorItemIndex);
                if (menu->mSelectParts->mCursorItemIndex == cTitleRowAp) {
                    // The AP row is meant to be a true no-op, but every
                    // physical row is bound to a real vanilla action regardless
                    // of its text label (same class of issue as the MP row and
                    // SeparatePlay, fixed above in separatePlayHook), and that
                    // holds wherever the row lands, so this swallow follows the
                    // row instead of a fixed index - confirmed on
                    // hardware: falling through to pauseMenuExeWaitHook.orig()
                    // on this exact frame let vanilla process its own decide
                    // handling for whatever this row really is, opening
                    // vanilla's "create a new save data" flow. Swallow the
                    // decide-trigger frame entirely instead of falling
                    // through - idle frames below still reach orig()
                    // normally, so animation/pose keeps working.
                    // StageSceneStateModConfig::requestArchipelagoPageOnAppear();
                    return;
                }
                sTitleSelectedRow = menu->mSelectParts->mCursorItemIndex;
                if (menu->field_0x20)
                    al::startHitReaction(menu->field_0x20, cDecideHitReaction, nullptr);
                if (menu->mSelectParts->mLayoutActor)
                    al::startHitReaction(menu->mSelectParts->mLayoutActor, cDecideHitReaction, nullptr);
                menu->mSelectParts->update();
                sTitleStartPendingFrames = 1;
                return;
            }

            // Let vanilla keep the title scene/pose/layout update pipeline alive
            // on idle frames. We only suppress it on the A-trigger frames above.
            pauseMenuExeWaitHook.orig(menu);
            return;
        }

        if (menu == sModConfigPauseMenu && al::isFirstStep(menu))
            menu->mSelectParts->setSelectMessage(2, u"Mod Menu");

        pauseMenuExeWaitHook.orig(menu);

        // AFTER orig, and every frame - not once on the first step. The 2P row is
        // the one entry whose text vanilla owns dynamically: it swaps between
        // "Two Players" and "One Player" off rs::isSeparatePlay, and vanilla's
        // exeWait re-resolves it as part of its own update. Writing before orig
        // (or only on the first step) puts our label down and then lets vanilla
        // put its own back on top. Row 2's "Action Guide" is static, which is why
        // "Mod Menu" sticks from a single first-step write.
        //
        // No nerve guard here. An earlier revision gated this on
        // al::isNerve(menu, &NrvStageSceneStatePauseMenu.Wait) to avoid touching
        // the row after orig moved on, and that suppressed the write entirely:
        // NrvStageSceneStatePauseMenu is the MOD's nerve struct (hence the
        // NrvStateCount patch in installHooks), so its Wait entry is not the same
        // object as the vanilla Wait nerve this executor actually runs under and
        // the comparison never matched. The proof was that neither this label nor
        // its "skipped" diagnostic ever reached the log while "Mod Menu" - written
        // before orig with no guard - showed up fine.
        //
        // Writing a pane string on a menu that has moved on is harmless anyway;
        // mSelectParts outlives the nerve change and the row is redrawn on the
        // next appear.
        //
        // The randomized pool can deliberately roll the ordinary Odyssey
        // drop-off. It has no logic node, but it is still a valid Return to Start
        // destination, so use the dedicated capability check rather than the
        // forced-stage/node check used by the opening redirect.
        if (StartPositionRando::canReturnToStart())
            setSelectMessageByAction(menu->mSelectParts, cActionSeparatePlay, u"Return to Start");
        else
            logPauseLabelSkipOnce();
    });

    // exeRoomCodeKeyboard's body lives in StageSceneStatePauseMenu.cpp
    // alongside exeModConfig/exeFileSelect (out-of-line member definitions
    // for this class have to sit in a namespace that actually encloses it -
    // PauseMenuHooks doesn't).

    // 2P mode -> Return to Start. Same shape as the exeFadeBeforeHelp -> ModConfig
    // redirect above: MenuSelectParts dispatches the decide to this nerve, so
    // intercepting the nerve captures the option wherever its row sits.
    //
    // The logic depends on this: the start node is modeled as permanently
    // reachable precisely because this warp can always get back to it. If this
    // redirect is ever removed, MoonRando::applyRandomizedStartPosition has to go
    // back to modeling the opening as temporary access.
    static HkTrampoline<void, StageSceneStatePauseMenu*>
        separatePlayHook = hk::hook::trampoline([](StageSceneStatePauseMenu* state) -> void {
            if (state == sTitlePauseMenu) {
                // Physical row 1 is hard-wired to vanilla's SeparatePlay action
                // regardless of what text setAfterTitleHook puts on it - this is
                // the row now labelled "MP". Falling through to orig() here is
                // what actually started a real vanilla 2-player game straight
                // from the title (confirmed on hardware: it went on to load a
                // real StageScene and hit the already-documented
                // sceneStateModConfig heap-leak abort, since the title was never
                // meant to reach a real stage load at all). Same redirect-to-
                // Wait pattern as overrideHelpFadeNerve above, for the same
                // reason: the title should never act on vanilla's native
                // interpretation of this row.
                al::setNerve(state, &NrvStageSceneStatePauseMenu.Wait);
                return;
            }
            if (!StartPositionRando::canReturnToStart()
                || !StartPositionRando::warpToStart(state->mDataHolder)) {
                separatePlayHook.orig(state);
                return;
            }
            // The stage change is queued; End closes the menu and lets the scene
            // finish, which is when the game acts on it.
            al::setNerve(state, &NrvStageSceneStatePauseMenu.End);
        });

    static HkTrampoline<bool, const TitleMenuScene*>
        titleIsNewGameHook = hk::hook::trampoline([](const TitleMenuScene* scene) -> bool {
            if (gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested())
                return false;

            return titleIsNewGameHook.orig(scene);
        });

    // TitleMenuScene::setScenario only prepares the ContinueLoading layout's
    // label.  File-select launches supply their own generic loading text, and
    // their GameData transition can leave that title-layout message interface
    // invalid while the file is being swapped.  Calling the vanilla label path
    // then jumps through a stale IUseMessageSystem vtable (the crash seen in
    // TitleMenuScene::exeWipe).  It has no bearing on the actual stage target,
    // so skip it exclusively for an active file-select launch.
    static HkTrampoline<void, TitleMenuScene*>
        titleSetScenarioHook = hk::hook::trampoline([](TitleMenuScene* scene) -> void {
            if (gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested()) {
                Randomizer::logPush("TitleMenuScene::setScenario skipped (file-select transition)");
                return;
            }
            titleSetScenarioHook.orig(scene);
        });

    static HkTrampoline<void, TitleMenuScene*>
        titleExeWipeHook = hk::hook::trampoline([](TitleMenuScene* scene) -> void {
            titleExeWipeHook.orig(scene);
            setRandomizedAdventureLoadingText(scene);

            if (gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested()
                && al::isFirstStep(scene)) {
                // Force ONE world-resource reload for the loaded file's kingdom.
                // BootLoadData::exeMenu only requests the world-home resources on
                // its first step (during the initial title), so after a file-select
                // start it never re-requests and the loader sits at 0%. Setting
                // mIsCancelLoadResource makes exeMenu cancel + destroy + re-request;
                // it clears the flag itself (startLoadWorldResource), so set it only
                // once here — every-frame would re-cancel and pin loadPercent at 0.
                scene->mIsCancelLoadResource = true;
                scene->mLoadPercent = 0.0f;
                scene->mLoadPercentBuffered = 0.0f;
                scene->mLoadCompleteCounter = 0;
                Randomizer::logPush("titleExeWipe: forced mIsCancelLoadResource=true (file-select start)");
            }
        });

    static HkTrampoline<void, TitleMenuScene*>
        titleExeLoadAppearHook = hk::hook::trampoline([](TitleMenuScene* scene) -> void {
            titleExeLoadAppearHook.orig(scene);
            setRandomizedAdventureLoadingText(scene);
        });

    static HkTrampoline<void, TitleMenuScene*>
        titleExeLoadWaitHook = hk::hook::trampoline([](TitleMenuScene* scene) -> void {
            titleExeLoadWaitHook.orig(scene);
            setRandomizedAdventureLoadingText(scene);
        });

    // TitleMenuScene::drawMain only draws its own layout kit groups, not the
    // pause-menu group ("２Ｄ（ポーズ無視）") that the file-select (OptionModCheck)
    // menus live in. So the file-select logic/input runs but the menu never
    // renders. Draw that group after the title's own draw so the menu shows.
    //
    // IMPORTANT: only draw when the FileSelect nerve is active. Drawing
    // unconditionally renders every layout in the group including the
    // ModConfig / FileSelect layouts that are visible by default before
    // appear() is called — producing the "empty overlay" the user saw.
    static HkTrampoline<void, al::Scene*> titleDrawMainHook = hk::hook::trampoline([](al::Scene* scene) -> void {
        titleDrawMainHook.orig(scene);
        if (sTitlePauseMenu && al::isNerve(sTitlePauseMenu, &NrvStageSceneStatePauseMenu.FileSelect)) {
            al::drawKit(scene,
                "\xef\xbc\x92\xef\xbc\xa4\xef\xbc\x88\xe3\x83\x9d\xe3\x83\xbc\xe3\x82\xba\xe7\x84\xa1\xe8\xa6\x96\xef\xbc\x89"); // "２Ｄ（ポーズ無視）"
        }
    });

    // Debug hook: log TitleMenuScene::exeAppear progress to confirm the nerve
    // advances (or reveal exactly where it gets stuck).
    static HkTrampoline<void, TitleMenuScene*> titleExeAppearDebugHook = hk::hook::trampoline([](TitleMenuScene* scene) -> void {
        static s32 sFrame = 0;
        if (al::isFirstStep(scene)) {
            sFrame = 0;
            Randomizer::logPush("[TitleAppear] enter");
        }
        sFrame++;
        if (sFrame == 2)
            Randomizer::logPush("[TitleAppear] step=2 (Boot start / setAfterTitle)");
        if (sFrame == 3)
            Randomizer::logPush("[TitleAppear] step=3 (first advance check)");
        if (sFrame == 150)
            Randomizer::logPush("[TitleAppear] step=150 (appearMarioModel)");
        if (sFrame % 120 == 0)
            Randomizer::logPush("[TitleAppear] STILL IN exeAppear frame=%d", sFrame);
        titleExeAppearDebugHook.orig(scene);
    });

    // TitleMenuScene::startLoadDirect direct-loads the current save's scenario into
    // a stage, skipping the title menu. HakoniwaStateBootLoadData::exeBootLayoutInitScene
    // calls it when boot carries a "continue the save" intent (its byte+257). On
    // "Return to Main Menu" that intent is set, but the GameDataFile is being torn
    // down, so startLoadDirect -> setScenario -> rs::tryGetMapMainScenarioLabel ->
    // GameDataFile::isExistJango crashes on MainThread during boot. The rando only
    // ever enters games through the title file-select, so allow the direct load
    // solely while a file-select start is in progress; otherwise skip it and let
    // the title menu appear.
    static HkTrampoline<void, TitleMenuScene*, bool> titleStartLoadDirectHook = hk::hook::trampoline([](TitleMenuScene* scene, bool isNewGame) -> void {
        if (gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested()) {
            titleStartLoadDirectHook.orig(scene, isNewGame);
            return;
        }
        Randomizer::logPush("TitleMenuScene::startLoadDirect skipped (return to title)");
    });

    static HkReplace<void, StageSceneStateOption*> optionDataManagerReturnHook = hk::hook::replace([](StageSceneStateOption* state) -> void {
        if (al::isFirstStep(state))
            Randomizer::logPush("OptionTop_Data: Return to Main Menu requested");

        // RELAUNCH ATTEMPT (disabled): nn::oe::RequestToRelaunchApplication is NOT
        // exported by this game's nnSdk module, so referencing it makes SAIL abort
        // at boot (mod fails to load). We have no nnSdk offset to use instead, so
        // this approach is not viable. Kept for reference:
        //   nn::oe::RequestToRelaunchApplication();

        // Drop any live MP connection before leaving the game - the title's
        // own Multiplayer entry (Client::promptRoomCodeAndConnect(), above)
        // is what re-establishes it on a later visit, using a fresh room
        // code, so no re-enable step is needed here beyond just stopping.
        Client::disconnect();

        // In-place return to the title scene. NOTE: this still hits the
        // CommonVerticalList::setEnableData crash when the mod menus re-construct
        // on the scene-init thread; that crash needs the real fix (ModConfig log).
        state->mIsLoadData = true;
        state->kill();
    });

    void installHooks() {
        hk::hook::a64::assemble<"mov w2, #6">().installAtSym<"R_ZN24StageSceneStatePauseMenuNrvStateCount">();
        initNerveStateHook.installAtSym<"R_ZN24StageSceneStatePauseMenuC1">();
        initStateHook.installAtSym<"_ZN21StageSceneStateOptionC1EPKcPN2al5SceneERKNS2_14LayoutInitInfoEP11FooterPartsP14GameDataHolderb">();
        overrideHelpFadeNerve.installAtSym<"_ZN24StageSceneStatePauseMenu17exeFadeBeforeHelpEv">();
        setAfterTitleHook.installAtSym<"_ZN24StageSceneStatePauseMenu13setAfterTitleEv">();
        pauseMenuExeWaitHook.installAtSym<"_ZN24StageSceneStatePauseMenu7exeWaitEv">();
        separatePlayHook.installAtSym<"_ZN24StageSceneStatePauseMenu20exeStartSeparatePlayEv">();
        titleIsNewGameHook.installAtMainOffset(0x505F08);
        titleSetScenarioHook.installAtMainOffset(0x506004);
        titleExeWipeHook.installAtMainOffset(0x506480);
        titleExeLoadAppearHook.installAtMainOffset(0x5065f0);
        titleExeLoadWaitHook.installAtSym<"_ZN14TitleMenuScene11exeLoadWaitEv">();
        titleDrawMainHook.installAtMainOffset(0x505CCC);
        titleStartLoadDirectHook.installAtMainOffset(0x505f60);
        titleExeAppearDebugHook.installAtMainOffset(0x5061c0);
        optionDataManagerReturnHook.installAtMainOffset(0x4e6e54);

        // Historical note: this patch is intentionally disabled now. The old
        // forced fall-through path is documented here because it explained the
        // broken title Mario state.
        // TitleMenuScene::exeAppear advances at step >= 3 via:
        //   0x506300: BL isActionEnd(Boot)
        //   0x506304: TBZ  (Boot not ended → skip)
        //   0x50630c: BL getMarioActor
        //   0x506310: BL isActionEnd(MarioActor)   <-- CRASHES at step 3
        //   0x506314: TBZ  (Mario not ended → skip)
        //   0x506328: setNerve(Menu)
        // We want to skip the Boot animation wait (the Boot layout uses
        // SimpleLayoutAppearWaitEnd and waits for a button press our hook consumes,
        // which otherwise hangs exeAppear). NOPping the two TBZs forces fall-through,
        // but that makes isActionEnd(MarioActor) at 0x506310 run at step 3 — before
        // appearMarioModel (step 150) sets up the model, so its skeletal anim player
        // is null and isSklAnimEnd derefs null. Also NOP the call itself so we reach
        // setNerve without touching the not-yet-initialised Mario model.
        // Leave the vanilla JoyCon/Boot/Mario appear checks intact.
    }

} // namespace PauseMenuHooks
