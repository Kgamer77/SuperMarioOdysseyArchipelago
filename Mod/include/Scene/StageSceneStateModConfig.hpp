#pragma once

#include <basis/seadTypes.h>

#include "sead/container/seadSafeArray.h"

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Message/IUseMessageSystem.h"
#include "al/Library/Message/MessageSystem.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Library/Scene/Scene.h"
#include "sead/container/seadSafeArray.h"

#include "game/Input/InputSeparator.h"
#include "game/Layout/CommonVerticalList.h"
#include "game/Layout/SimpleLayoutMenu.h"
#include "game/System/GameDataHolder.h"

#include "Keyboard.hpp"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/Randomizer.h"

namespace ShineDB {
    struct ShineEntry;
}

#if SMOR_FEATURE_CHAOS_ANY
#include "Randomizer/EnemySwapDB.h"
#endif
#include "Randomizer/ScenarioNames.h"

namespace ShineDB {
    struct ShineEntry;
}

class FooterParts;

class StageSceneStateModConfig : public al::HostStateBase<al::Scene>,
                                 public al::IUseMessageSystem {
public:
    StageSceneStateModConfig(const char* name, al::Scene* scene,
        const al::LayoutInitInfo& initInfo,
        FooterParts* footerParts,
        GameDataHolder* dataHolder,
        bool unused);

    virtual void init() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual al::MessageSystem* getMessageSystem() const override { return mMsgSystem; }

    void exeMainMenu();
    void exeRandomizerSettings();
    void exeRandomizerMiscSettings();
    void exeCaptureDebug();
    void exeAbilityDebug();
    void exeDebugWarpKingdom();
    void exeDebugWarpSubarea();
    void exeKingdomChecksList();
#if SMOR_FEATURE_CHAOS_ANY
    void exeEnemySwapInspector();
#endif
    void exeOpenKeyboardSeed();
    void exeOpenKeyboardMoonReq();
    void exeOpenKeyboardMultiplayer();
    void exeOpenKeyboardArchipelago();

    enum class RowStyle : u8
    {
        Base,
        Check,
        Roll,
    };

    // Keep the roll value in the same place in the in-game Mod Menu and the
    // title-screen setup menu. applyRowStyle() is called again whenever a
    // shared list is rebuilt, so this must remain the single source of truth.
    static constexpr float kRollPaneX = 160.0f;
    // A two-column row has half the width to work with, so its text box and
    // roll widget both pull in. Without this the left column's value sat on
    // top of the right column's label.
    static constexpr float kNarrowRollPaneX = 120.0f;
    static constexpr float kWideTextWidth = 420.0f;
    static constexpr float kWideCheckTextWidth = 360.0f;
    static constexpr float kNarrowTextWidth = 330.0f;
    static constexpr float kNarrowCheckTextWidth = 270.0f;

    // The layout authors TxtContent at its own X, and every row in every list
    // shares one archive, so one reading is the answer for all of them. Cached
    // on the first call, which is always a freshly built row.
    static float authoredTextX(const al::LayoutActor* item) {
        static float sAuthoredX = 0.0f;
        static bool sHaveAuthoredX = false;
        if (!sHaveAuthoredX && item) {
            sAuthoredX = al::getPaneLocalTrans(item, "TxtContent").x;
            sHaveAuthoredX = true;
        }
        return sAuthoredX;
    }

    static void applyRowStyle(al::LayoutActor* item, RowStyle style,
        bool narrow = false) {
        if (!item)
            return;

        al::hidePane(item, "WinCheck");
        al::hidePane(item, "Roll");

        const float rollX = narrow ? kNarrowRollPaneX : kRollPaneX;
        const float wide = narrow ? kNarrowTextWidth : kWideTextWidth;
        const float checked = narrow ? kNarrowCheckTextWidth : kWideCheckTextWidth;
        // TxtContent is centred on its origin, so a narrower box moves the text
        // right by half the difference. Undo that so the label starts in the
        // same place on every page, single-column or grid - measured from where
        // the layout put the pane, not from zero. Writing a flat 0 for the wide
        // case dragged the label off its authored position and into the
        // checkbox on every single-column page.
        const float baseX = authoredTextX(item);
        const float textX = narrow
            ? baseX - (kWideTextWidth - kNarrowTextWidth) * 0.5f
            : baseX;
        const float checkTextX = narrow
            ? baseX - (kWideCheckTextWidth - kNarrowCheckTextWidth) * 0.5f
            : baseX;

        switch (style) {
        case RowStyle::Check:
            al::setPaneLocalSize(item, "TxtContent", { checked, 50 });
            al::setPaneLocalTrans(item, "TxtContent", { checkTextX, 0.0f, 0.0f });
            al::showPane(item, "WinCheck");
            break;
        case RowStyle::Roll:
            al::setPaneLocalSize(item, "TxtContent", { wide, 50 });
            al::setPaneLocalTrans(item, "TxtContent", { textX, 0.0f, 0.0f });
            al::showPane(item, "Roll");
            al::setPaneLocalTrans(item, "Roll", { rollX, 0.0f, 0.0f });
            break;
        case RowStyle::Base:
        default:
            al::setPaneLocalSize(item, "TxtContent", { wide, 50 });
            al::setPaneLocalTrans(item, "TxtContent", { textX, 0.0f, 0.0f });
            break;
        }
    }

    // Shared icon-based footer string ("LStick Select  B Back  A Confirm"),
    // reused by the title file-select so its footer matches the in-game menus.
    static const char16_t* makeStandardFooterText();

    // Footer for the file-select settings pages. When the currently focused
    // row is a preset selector, showApplyHint/showDeleteHint swap the normal
    // "Toggle" hint for "Apply" (and, for the User Preset row with at least
    // one saved preset, an additional "Delete" hint on Y). Advanced settings
    // reuse that button slot to advertise the sub-page entry point.
    // advancedOnA moves the Advanced hint onto A and relabels it: select rows
    // have nothing for A to decide, so they use it for Advanced instead of
    // advertising a "Toggle" that does nothing. hasDecideAction is false for a
    // select with no Advanced page at all, which drops the A hint entirely.
    // showPlusHint advertises the advanced pages' confirm-and-close button.
    // Written as a literal "+" rather than a pad-style glyph tag: the footer's
    // icon tags name specific layout glyphs and there is no verified Plus one.
    // showColumnHint advertises the D-pad's left/right on a two-column page,
    // where it crosses between the columns instead of rolling a value.
    static const char16_t* makeSettingsFooterText(bool showApplyHint = false, bool showDeleteHint = false,
        bool showColumnHint = false, bool showAdvancedHint = false, bool advancedOnA = false,
        bool hasDecideAction = true, bool showPlusHint = false);

    // Motion-Free Controls hint, line by line, rendered with in-game button icons
    // (returns nullptr past the last line). Shares the footer icon-tag builders so the
    // file-select info box shows real X/B/R/Y button glyphs.
    static const char16_t* getMotionFreeHintLine(s32 lineIdx);

    // Limit a vertical list to a fixed visible window so it scrolls instead of
    // overflowing off-screen (used by the title file-select's settings list).
    static void configureScrollableList(CommonVerticalList* list);

    // Lay a list's rows out as two columns instead of one: rows [0, colSize)
    // keep their vertical positions on the left, and rows [colSize, count) are
    // pulled up beside them on the right. The list itself still treats the
    // rows as one linear sequence, so the caller owns left/right navigation.
    //
    // Every row part has to be present for this - a two-column page cannot
    // also scroll - so the caller must undo configureScrollableList()'s clamp
    // on mListPartsNum first. Must be re-applied every frame: the list's own
    // update re-poses the parts into a single column.
    static void repositionTwoColumnPanes(CommonVerticalList* list, s32 optionCount,
        s32 colSize);

    bool isDone() const { return mIsDone; }

    // Makes the next appear() land straight on the Multiplayer page instead of
    // the ModConfig main menu. Set by the title screen's Multiplayer row right
    // before it enters the ModConfig nerve (PauseMenuHooks.cpp); consumed by
    // appear(), so it only ever affects the one entry it was set for.
    static void requestMultiplayerPageOnAppear() { sEnterOnMultiplayerPage = true; }
    static bool sEnterOnMultiplayerPage;

    // Set by the Connect row once the server has assigned a room, then read
    // once by the title's pause-menu Wait handler to move on to the
    // multiplayer file select. Consuming clears it, so a later visit to the
    // menu that does not connect cannot re-trigger the transition.
    static bool consumeMultiplayerFileSelectRequest() {
        const bool requested = sMultiplayerFileSelectRequest;
        sMultiplayerFileSelectRequest = false;
        return requested;
    }
    static bool sMultiplayerFileSelectRequest;

    // Makes the next appear() land straight on the Archipelago page instead of
    // the ModConfig main menu. Set by the title screen's Archipelago row right
    // before it enters the ModConfig nerve (PauseMenuHooks.cpp); consumed by
    // appear(), so it only ever affects the one entry it was set for.
    static void requestArchipelagoPageOnAppear() { sEnterOnArchipelagoPage = true; }
    static bool sEnterOnArchipelagoPage;

    // Set by the Connect row once the Archipelago handshake is complete, then read
    // once by the title's pause-menu Wait handler to move on to the
    // archipelago file select. Consuming clears it, so a later visit to the
    // menu that does not connect cannot re-trigger the transition.
    static bool consumeArchipelagoFileSelectRequest() {
        const bool requested = sArchipelagoFileSelectRequest;
        sArchipelagoFileSelectRequest = false;
        return requested;
    }
    static bool sArchipelagoFileSelectRequest;

    // Set by the pause-menu ctor hook while this ModConfig is constructed for the
    // TITLE host. The capture/ability debug menus pull live CaptureRando /
    // AbilityRando lists and read save state through the GameDataHolder; that is
    // only valid in-game, and building those rows during title re-init (the
    // "Return to Main Menu" path) crashes. When set, those menus init empty —
    // they are never shown on the title.
    static bool sSuppressLiveData;

    // Current Misc/QoL/Scenario Override description text (wrapped, '\n'-joined),
    // or null when the menu isn't showing one. Read by ModConfigInfoOverlay
    // (an ImGui overlay - see updateRandoMiscInfoBox() below for why this isn't
    // a native layout part).
    const char* getMiscInfoText() const { return mMiscInfoBoxVisible ? mMiscInfoTextBuf : nullptr; }

private:
    // Ceiling enforced by the title scene's layout-executor table - a 9th
    // registered SimpleLayoutMenu/CommonVerticalList pair crashes at boot (see
    // the MENU_RANDOMIZER_MISC page-sharing comment below for the mechanism).
    // Only 7 of the 8 available slots are in use below, leaving one spare -
    // Tracker Live Testing used to fold into MENU_RANDOMIZER_MISC's page-
    // sharing mechanism (see RandoMiscPage) rather than needing its own slot;
    // it's been removed entirely since (dead debug/testing surface, not the
    // real Auto Tracker room-code/cloud-sync system, which lives on in
    // TrackerBridge.h/.cpp and the file-select wizard's own room-code entry
    // untouched). Kingdom Checks still shares the two debug-warp slots the
    // same way (see KingdomPickerMode).
    static constexpr int menuCount = 7;
    static constexpr int maxMsgCount = 64;

    SimpleLayoutMenu* menuList[menuCount];
    CommonVerticalList* optionsList[menuCount];
    sead::SafeArray<sead::WFixedSafeString<0x200>, maxMsgCount>* msgList[menuCount];

    enum Menu
    {
        MENU_MAIN,
        MENU_RANDOMIZER,
        MENU_RANDOMIZER_MISC,
        MENU_CAPTURE_DEBUG,
        MENU_ABILITY_DEBUG,
        MENU_DEBUG_WARP_KINGDOM,
        MENU_DEBUG_WARP_SUBAREA,
    };

    // -------------------------------------------------------------------------
    // Main menu
    // -------------------------------------------------------------------------
    void initMainMenu(const al::LayoutInitInfo& initInfo);
    void rebuildMainMenuOptionMap();
    void updateMainMenuOptions();
    int getSelectedMainMenuOption() const;

    enum MainMenuOption
    {
        MAIN_RANDOMIZER_SETTINGS = 0,
        MAIN_RANDOMIZER_MISC = 1,
        MAIN_QUALITY_OF_LIFE = 2,
        MAIN_KINGDOM_CHECKS = 3,
        MAIN_DEBUG_WARP = 4,
        MAIN_ADD_COINS = 5,
        MAIN_CAPTURE_DEBUG = 6,
        MAIN_ABILITY_DEBUG = 7,
        MAIN_SCENARIO_OVERRIDE = 8,
#if SMOR_FEATURE_CHAOS_ANY
        MAIN_ENEMY_SWAP_INSPECTOR = 9,
#endif
    };
    static constexpr int mMainMenuOptionsCount =
#if SMOR_FEATURE_CHAOS_ANY
        10;
#else
        9;
#endif
    int mMainMenuOptionMap[maxMsgCount] = {};
    int mMainMenuOptionCount = 0;

    // -------------------------------------------------------------------------
    // Randomizer menu
    // -------------------------------------------------------------------------
    void initRandomizerMenu(const al::LayoutInitInfo& initInfo);
    void initRandomizerMiscMenu(const al::LayoutInitInfo& initInfo);
    void repositionRandomizerPanes();
    void repositionRandomizerMiscPanes();
    void updateRandomizerOptions();
    void updateRandomizerMiscOptions();
    static int getLogicRollIdx();

    // Rebuilds the "Override Scenario" roll (Default + each named scenario) for
    // whichever kingdom the player is currently in, and points mRandoMiscRollParts
    // at it. Call whenever the Scenario Override page is (re)loaded.
    void rebuildScenarioOverrideRoll();

    // The "Misc Randomization" and "Quality of Life" menus share ONE
    // CommonVerticalList (MENU_RANDOMIZER_MISC), re-paged in place. A separate
    // menu would register another layout executor and overflow the title scene's
    // executor-list table at scene init. loadRandoMiscPage() reconfigures the row
    // count/labels/styles for a page and resets the cursor to the top.
    void loadRandoMiscPage(s32 page);
    void applyRandoMiscPageStyles();
    s32 randoMiscPageCount() const;
    s32 randoMiscPageColSize() const;

    // Per-item description text for the Misc/QoL/Scenario Override pages, drawn
    // by ModConfigInfoOverlay (an ImGui overlay, not a native layout part - the
    // title scene's layout-executor table is already at capacity, so a new
    // SimpleLayoutMenu here overflows it and crashes at boot). Updates every
    // frame this submenu is active, so text is only ever populated while this
    // page is genuinely on screen; getMiscInfoText() returns null otherwise.
    void showRandoMiscInfoBox(bool visible);
    void updateRandoMiscInfoBox();
    const char* randoMiscOptionDescription(s32 page, s32 index) const;
    bool mMiscInfoBoxVisible = false;
    s32 mMiscInfoBoxLastPage = -1;
    s32 mMiscInfoBoxLastIndex = -1;
    char mMiscInfoTextBuf[0x200] = {};

    enum RandomizerMenuOption
    {
        RANDO_SEED = 0,
        RANDO_MOONS = 1,
        RANDO_PAINTINGS = 2,
        RANDO_KINGDOM_ORDER = 3,
        RANDO_LOADING_ZONES = 4,
        RANDO_LOGIC = 5,
        RANDO_LOCKED_CAPTURES = 6,
        RANDO_LOCKED_ABILITIES = 7,
        RANDO_STARTING_EQUIPMENT = 8,
        RANDO_CAPTURE_DIFFICULTY = 9,
        RANDO_PURPLE_SHOPS = 10,
        RANDO_MOON_REQ = 11,
        RANDO_MOON_DIFFICULTY = 12,
        RANDO_CAP_MOON_USE = 13,
        RANDO_CLOUD_MOON_USE = 14,
        RANDO_MOON_MOON_USE = 15,
        RANDO_WORLD_PEACE_REQ = 16,
        RANDO_MOON_ROCK_KEY = 17,
        RANDO_WIN_CONDITION = 18,
        RANDO_TOADETTE_MEDAL_RATIO = 19,
        // Stable row 20 is intentionally shared by the Triple Moons and Moon
        // Allocation summaries; the overview has a hard ceiling of 23 rows.
        RANDO_TRIPLE_MOONS = 20,
        RANDO_START_KINGDOM_STATE = 21,
        RANDO_COIN_RANDO = 22,
    };
    // At the "OptionModCheck" layout's physical parts ceiling. initRandomizerMenu()
    // and repositionTwoColumnPanes() both index mListPartsArr[row + 1], so a 24th
    // row reads one past the allocation and crashes inside al::hidePane during
    // TitleMenuScene::init. Any new row here needs those two loops bounded by the
    // list's real parts count first (see getVisibleListItemCount's callers).
    static constexpr int mRandoMenuOptionsCount = 23;
    static constexpr int mRandoColSize = 12;

    // Page 0 of MENU_RANDOMIZER_MISC.
    enum RandomizerMiscMenuOption
    {
        RANDO_MISC_SKYBOXES = 0,
        RANDO_MISC_STABILIZE_SHADERS = 1,
        RANDO_MISC_MUSIC = 2,
        // Directly under Music Rando, and inside the left column:
        // mRandoMiscColSize is 4, so rows 4+ are placed in the right-hand
        // column. At row 5 this roll rendered over there, away from the mode it
        // belongs with.
        RANDO_MISC_USE_CUSTOM_MUSIC = 3,
        RANDO_MISC_STREAMER_MODE = 4,
        RANDO_MISC_MUSIC_OVERLAY = 5,
        RANDO_MISC_SPHINX = 6,
#if SMOR_FEATURE_CHAOS_ANY
        // Row 7 is QOL_MOON_LIMITS on the Quality of Life page, which is a
        // roll. Sharing the index is fine -- mRandoMiscRollParts is indexed by
        // row but only consulted for rows the active page styles as rolls, and
        // this one is a checkbox.
        RANDO_MISC_FORCE_DISABLE_CHAOS = 7,
#endif
    };
    static constexpr int mRandoMiscMenuOptionsCount =
#if SMOR_FEATURE_CHAOS_ANY
        8;
#else
        7;
#endif
    static constexpr int mRandoMiscColSize = 4;
    int mActiveColumn = 0;

    // Page 1 of MENU_RANDOMIZER_MISC (Quality of Life — mostly checkboxes;
    // tracker overlay, cave skip, and moon limits are rolls). Those roll rows must avoid colliding with the
    // Misc page's roll rows (RANDO_MISC_SKYBOXES=0, RANDO_MISC_MUSIC=2),
    // since mRandoMiscRollParts's slots are reused across pages.
    enum QualityOfLifeMenuOption
    {
        QOL_FAST_MOON_DEMOS = 0,
        QOL_MOTION_FREE = 1,
        QOL_SKIP_CUTSCENE_CAMERAS = 2,
        // Took over the row freed by the removed Costume Doors Open toggle
        // rather than shifting every row below it: the roll rows' indices are
        // also indices into the shared mRandoMiscRollParts array, so moving
        // them would collide with the Misc page's roll slots.
        QOL_TRACKER_TIMER = 3,
        QOL_TRACKER_OVERLAY = 4,
        QOL_TRACKER_RIGHT_ALIGN = 5,
        QOL_CAVE_SKIP = 6,
        QOL_MOON_LIMITS = 7,
        QOL_COMPLETED_KINGDOM_MOON_COLLECTED = 8,
    };
    static constexpr int mQualityOfLifeMenuOptionsCount = 9;
    // Five rows on the left and the rest on the right: repositionTwoColumnPanes
    // pairs row i with row i + colSize.
    static constexpr int mQualityOfLifeColSize = 5;

    // Page 2 of MENU_RANDOMIZER_MISC (Quality of Life II). It reuses the same
    // physical list as page 1, avoiding another title-scene layout executor.
    enum QualityOfLife2MenuOption
    {
        QOL2_EASY_MINIGAMES = 0,
        QOL2_AREA_NAME_OVERLAY = 1,
        QOL2_NO_CLIP = 2,
        QOL2_SHUFFLE_MUSIC = 3,
        QOL2_FREE_COSTUME_MOON = 4,
        QOL2_ONE_SHEEP_HERDING = 5,
        QOL2_RANDOMIZE_OUTFITS = 6,
        QOL2_BOARD_SCALE = 7,
        QOL2_UNLOCK_HINT_ART = 8,
        QOL2_FAST_DOG = 9,
        QOL2_UNCAPPED_COINS = 10,
    };
    static constexpr int mQualityOfLife2MenuOptionsCount = 11;

    // Left-column height: repositionTwoColumnPanes only pairs row i with row
    // i + colSize, so rows past 2 * colSize are never placed. Five keeps all
    // ten rows in the two columns.
    static constexpr int mQualityOfLife2ColSize = 5;

    // Page 3 of MENU_RANDOMIZER_MISC — debug tool to force a kingdom's scenario.
    enum ScenarioOverrideMenuOption
    {
        SCENARIO_OVERRIDE_CURRENT = 0, // read-only: what's actually loaded right now
        SCENARIO_OVERRIDE_VALUE = 1, // roll: Default, or a forced scenario number
    };
    static constexpr int mScenarioOverrideMenuOptionsCount = 2;
    static constexpr int mScenarioOverrideColSize = 2;
    s32 mScenarioOverrideWorldId = -1; // kingdom the roll options were built for

    // Backing storage for the "Override Scenario" roll: "Default" plus one label
    // per named scenario of mScenarioOverrideWorldId. Rebuilt whenever the page
    // loads, since the option list depends on whichever kingdom is current.
    static constexpr int mScenarioOverrideRollOptionsMax = Randomizer::ScenarioNames::kMaxScenariosPerKingdom + 1;
    sead::WFixedSafeString<0x40> mScenarioOverrideRollLabels[mScenarioOverrideRollOptionsMax];
    const char16_t* mScenarioOverrideRollLabelPtrs[mScenarioOverrideRollOptionsMax] = {};
    s32 mScenarioOverrideRollScenarioNos[mScenarioOverrideRollOptionsMax] = {}; // 0 = Default

    // Page 4 of MENU_RANDOMIZER_MISC (Controls) - three rolls remapping Motion
    // Free Controls' roles to any button. Row indices double as
    // mRandoMiscRollParts slots, same as every other roll-styled page; 0-2 are
    // free to reuse since Controls is never the active page at the same time
    // as Misc (which also uses 0-2 for its own roll rows).
    enum ControlsMenuOption
    {
        CONTROLS_UP_THROW = 0,
        CONTROLS_DOWN_THROW_JUMP = 1,
        CONTROLS_SPIN_SHAKE = 2,
    };
    static constexpr int mControlsMenuOptionsCount = 3;
    static constexpr int mControlsColSize = 3;
    // Same 8 names for all three rows - built once, reused by
    // rebuildControlsRoll() every time the Controls page loads.
    const char16_t* mControllerButtonRollLabelPtrs[Randomizer::cControllerButtonCount] = {};
    void rebuildControlsRoll();

    // Page 5 of MENU_RANDOMIZER_MISC (Multiplayer) - the connect screen behind
    // the title's Multiplayer row. Rows 1-3 are keyboard rows and row 4 is an
    // action, so unlike every other page here none of these index
    // mRandoMiscRollParts; the page has no rolls at all.
    enum MultiplayerMenuOption
    {
        MP_SHOW_ENDPOINT = 0, // checkbox: unmask the IP/Port rows
        MP_IP = 1, // keyboard, masked unless MP_SHOW_ENDPOINT
        MP_PORT = 2, // keyboard, masked unless MP_SHOW_ENDPOINT
        // Sits with the two rows it persists, not with Connect: it writes the
        // IP and Port to server.ini and nothing else. The room name is not part
        // of that file.
        MP_SAVE_SETTINGS = 3, // action
        MP_ROOM_NAME = 4, // keyboard, never masked, required to connect
        // The Multiplayer Settings row that used to sit here is gone. Death
        // Link is a SEED setting - it is stored in rando.bin and, for the host,
        // becomes the room's flags - so setting it here meant setting it before
        // the seed it belongs to existed. It now lives on the file select's own
        // Multiplayer page, next to the rest of the seed settings and before
        // Generate Seed (StageSceneStateFileSelect's sMultiplayerOptions).
        MP_CONNECT = 5, // action
    };
    static constexpr int mMultiplayerMenuOptionsCount = 6;
    // One column: repositionTwoColumnPanes only pairs row i with row i +
    // colSize, so a colSize equal to the row count leaves the right column
    // empty. The IP row can render a full hostname and would collide with a
    // second column.
    static constexpr int mMultiplayerColSize = 6;

    // The Multiplayer Settings page that used to be declared here is gone -
    // its one setting (Death Link) belongs to the SEED, so it moved to the file
    // select's own Multiplayer page where the seed is actually being made. See
    // MP_CONNECT above.

    // Which of the three keyboard rows OpenKeyboardMultiplayer is editing.
    // -1 when that nerve isn't active.
    s32 mMpKeyboardField = -1;

    // Row to restore the cursor to after the keyboard closes.
    // loadRandoMiscPage() resets the cursor to the top on every page (re)load,
    // and coming back from a keyboard re-enters the page - without this,
    // editing Port would drop the cursor back onto Show IP and Port.
    s32 mMpRestoreRow = -1;

    // True when the page was entered from the title's Multiplayer row rather
    // than by paging across from Controls. Changes what B does (leave the whole
    // ModConfig state instead of falling back to the ModConfig main menu, which
    // on the title is a randomizer-debug surface the player never asked for)
    // and suppresses L/R paging off the Multiplayer page.
    bool mMpStandalonePage = false;

    // Result/progress line shown on the Connect row. Empty until a connect is
    // attempted.
    char mMpStatusBuf[0x60] = {};

    // Result line for the Save Settings row. Kept separate from mMpStatusBuf
    // because that one renders on the Connect row - sharing it would print
    // "Saved to server.ini." against Connect, which is the wrong row.
    char mMpSaveStatusBuf[0x40] = {};

    // The Connect row's handshake, split across frames. It was one blocking
    // call that could sleep 30 seconds on the main thread waiting for the
    // server's room assignment, which made a server that accepts the socket
    // but never assigns a room look exactly like the game hanging at 0 fps.
    // Now Connect starts it and returns, and the wait is polled.
    bool mMpConnectPending = false;
    s32 mMpConnectFrames = 0;
    static constexpr s32 cMpConnectTimeoutFrames = 60 * 30;

    void updateMultiplayerOptions();
    void refreshMultiplayerRows();
    void startMultiplayerConnect();
    void updateMultiplayerConnect();
    void exitMultiplayerMenu();

    // "Show IP and Port" and the typed room name are static (once per process,
    // i.e. once per mod boot) rather than per-instance ON PURPOSE: this state
    // object is reconstructed on every single StageScene::init (see the
    // ModConfig leak comment in PauseMenuHooks.cpp), so a plain member would
    // reset whenever the player crossed a scene boundary and back. Static
    // storage still satisfies "Show IP and Port is off when the mod boots" -
    // it is zero-initialised once at process start and never persisted to
    // disk, so it cannot come back on across a reboot.
    static bool sMpShowEndpoint;
    static char sMpRoomName[17];

    enum ArchipelagoMenuOption
    {
        AP_HOSTNAME = 0, // keyboard never masked
        AP_PORT = 1, // keyboard never masked
        AP_SLOT = 2, // keyboard never masked
        AP_PASSWORD = 3, // keyboard never masked
        AP_DEATHLINK_OVERRIDE = 4, // Checkbox never masked
        AP_CONNECT = 5, // action
    };
    static constexpr int mArchipelgoMenuOptionsCount = 5;
    static constexpr int mArchipelagoColSize = 5;

    // Which of the three keyboard rows OpenKeyboardArchipelago is editing.
    // -1 when that nerve isn't active.
    s32 mApKeyboardField = -1;

    // The Connect row's handshake, split across frames. It was one blocking
    // call that could sleep 30 seconds on the main thread waiting for the
    // server's room assignment, which made a server that accepts the socket
    // but never assigns a room look exactly like the game hanging at 0 fps.
    // Now Connect starts it and returns, and the wait is polled.
    bool mApConnectPending = false;
    s32 mApConnectFrames = 0;
    s32 mApRestoreRow = -1;
    static constexpr s32 cApConnectTimeoutFrames = 60 * 30;

    void updateArchipelagoOptions();
    void refreshArchipelagoRows();
    void startArchipelagoConnect();
    void updateArchipelagoConnect();
    void exitArchipelagoMenu();

    char mApStatusBuf[0x60] = {};

    enum RandoMiscPage
    {
        RANDO_MISC_PAGE_MISC = 0,
        RANDO_MISC_PAGE_QOL = 1,
        RANDO_MISC_PAGE_QOL_2 = 2,
        RANDO_MISC_PAGE_SCENARIO_OVERRIDE = 3,
        RANDO_MISC_PAGE_CONTROLS = 4,
        // The title screen's Multiplayer row lands directly on this page
        // (see PauseMenuHooks.cpp); it is also reachable by paging right from
        // Controls. Multiplayer stays the LAST page so compiling it out is
        // just a shorter page count: the L/R page flip and
        // loadRandoMiscPage()'s clamp are the only two readers, and nothing
        // sizes an array by this.
        RANDO_MISC_PAGE_MULTIPLAYER = 5,
        // A Multiplayer Settings child page used to sit at 6, holding Death
        // Link. It moved to the file select (see MP_CONNECT's comment above),
        // and since it was the last value, dropping it shifts nothing.
        RANDO_MISC_PAGE_COUNT = SMOR_FEATURE_MULTIPLAYER ? 6 : 5,
    };
    s32 mRandoMiscPage = RANDO_MISC_PAGE_MISC;
    RollPartsData* mRandoMiscRollParts = nullptr; // active shared-page data
    RollPartsData* mDefaultRandoMiscRollParts = nullptr; // Misc/QoL/etc. data
    // QOL2 Board Size aliases QOL Moon Limits at physical row 7, so retain
    // its distinct roll metadata and swap it in when that page is active.
    RollPartsData* mBoardScaleRollParts = nullptr;
    s32 mRandoMiscRollPartCount = 0;

    // -------------------------------------------------------------------------
    // Capture debug menu
    // -------------------------------------------------------------------------
    void initCaptureDebugMenu(const al::LayoutInitInfo& initInfo);
    void updateCaptureDebugOptions(bool refreshParts = true);
    void updateCaptureDebugStatePanes();

    bool mCaptureDebugEnabled[maxMsgCount] = {};

    // -------------------------------------------------------------------------
    // Ability debug menu
    // -------------------------------------------------------------------------
    void initAbilityDebugMenu(const al::LayoutInitInfo& initInfo);
    void updateAbilityDebugOptions(bool refreshParts = true);
    void updateAbilityDebugStatePanes();

    bool mAbilityDebugEnabled[maxMsgCount] = {};

    // -------------------------------------------------------------------------
    // Debug warp menus (MENU_DEBUG_WARP_KINGDOM/MENU_DEBUG_WARP_SUBAREA are
    // also reused, page-style, by the read-only "Kingdom Checks" menu below -
    // a separate pair of SimpleLayoutMenu/CommonVerticalList would add two more
    // layout executors to the title scene's already-near-capacity executor
    // table, see the MENU_RANDOMIZER_MISC page-sharing comment above.
    // MENU_DEBUG_WARP_SUBAREA is shared the same way by the Enemy Swap
    // Inspector below.)
    // -------------------------------------------------------------------------
    void initDebugWarpKingdomMenu(const al::LayoutInitInfo& initInfo);
    void initDebugWarpSubareaMenu(const al::LayoutInitInfo& initInfo);
    void updateDebugWarpKingdomOptions();
    void updateDebugWarpSubareaOptions();
    void updateKingdomChecksOptions();

    s32 mDebugWarpSelectedKingdomIndex = 0;
    s32 mDebugWarpSubareaCount = 0;
    s32 mDebugWarpLinkIndices[maxMsgCount] = {};
    bool mDebugWarpFromFarSide[maxMsgCount] = {};
    bool mDebugWarpEnabled[maxMsgCount] = {};

    // Which feature is currently driving the shared MENU_DEBUG_WARP_KINGDOM
    // list: warping (existing Debug Warp flow) or read-only browsing (Kingdom
    // Checks). Set right before entering DebugWarpKingdom; read when the
    // kingdom is picked to decide whether to go to DebugWarpSubarea or
    // KingdomChecksList.
    enum KingdomPickerMode
    {
        KINGDOM_PICKER_DEBUG_WARP,
        KINGDOM_PICKER_CHECKS,
#if SMOR_FEATURE_CHAOS_ANY
        KINGDOM_PICKER_ENEMY_INSPECTOR,
#endif
    };
    KingdomPickerMode mKingdomPickerMode = KINGDOM_PICKER_DEBUG_WARP;
    s32 mKingdomChecksCount = 0;
    // Row -> the check it lists, so A can look up a route for the highlighted
    // one. Null for kingdom-access rows and the "Nothing currently reachable"
    // placeholder row.
    const ShineDB::ShineEntry* mKingdomChecksEntries[maxMsgCount] = {};
    // Row -> target kingdom for an "Access to ..." row, or -1 for moon rows
    // and the empty placeholder.
    s16 mKingdomChecksAccessTargets[maxMsgCount] = {};
    // Row the route panel is currently describing, so scrolling away closes it.
    s32 mKingdomChecksRouteRow = -1;
    // Row the footer text was last built for; moon sub-area and access rows
    // offer A.
    s32 mKingdomChecksFooterRow = -1;
    void updateKingdomChecksRouteView();
    void showKingdomChecksRouteView(s32 row);
    void hideKingdomChecksRouteView();
    bool kingdomChecksRowHasRoute(s32 row) const;

#if SMOR_FEATURE_CHAOS_ANY
    // -------------------------------------------------------------------------
    // Enemy Swap Inspector (reuses MENU_DEBUG_WARP_SUBAREA)
    // -------------------------------------------------------------------------
    static constexpr s32 kEnemyInspectorResetAllRow = 0;
    static constexpr s32 kEnemyInspectorObjectIdsVisibleRow = 1;
    static constexpr s32 kEnemyInspectorObjectIdRow = 2;
    static constexpr s32 kEnemyInspectorObjectReplacementRow = 3;
    static constexpr s32 kEnemyInspectorApplyRow = 4;
    static constexpr s32 kEnemyInspectorRerollRow = 5;
    static constexpr s32 kEnemyInspectorRowCount = 6;
    // Object replacement pools are built from the complete entity table; the
    // shared EnemySwapDebug policy filters the asymmetric source/target pairs.
    static constexpr s32 kEnemyInspectorChoiceCount = Randomizer::EnemySwapDB::sEntityCount + 1;

    void updateEnemySwapInspectorOptions();
    void rebuildEnemySwapInspectorTargets();

    const char* mEnemyInspectorLogicStage = nullptr;
    const char* mEnemyInspectorPhysicalStage = nullptr;
    bool mEnemyInspectorDirty = false;
    s32 mEnemyInspectorActiveObjectChoiceCount = 1;
    s32 mEnemyInspectorActiveObjectReplacementChoiceCount = 1;
    bool mEnemyInspectorEnabled[maxMsgCount] = {};
    RollPartsData mEnemyInspectorRollParts[maxMsgCount] = {};
    // One editable pair is kept in the menu. Pressing A stores that pair and
    // leaves the inspector open so additional independent pairs can be added;
    // the Apply row reloads the stage after all desired pairs are committed.
    s16 mEnemyInspectorSelectedPlacement = -1;
    s16* mEnemyInspectorSortedObjectReplacementTargets = nullptr;
    sead::WFixedSafeString<0x40>*
        mEnemyInspectorObjectReplacementTargetLabels
        = nullptr;
    const char16_t** mEnemyInspectorObjectReplacementChoiceLabels = nullptr;
    s16* mEnemyInspectorObjectReplacementChoiceEntities = nullptr;
    // The object-ID selector contains only eligible placements from the
    // current stage aliases, but the storage is fixed to the generated table
    // so a long stage can still be browsed without heap allocation.
    static constexpr s32 kEnemyInspectorObjectChoiceCount = Randomizer::EnemySwapDB::sPlacementCount + 1;
    const char16_t** mEnemyInspectorObjectChoiceLabels = nullptr;
    s16* mEnemyInspectorObjectChoicePlacements = nullptr;
    sead::WFixedSafeString<0x40>* mEnemyInspectorObjectIdLabels = nullptr;

    bool applyEnemyInspectorPair();
#endif

    // -------------------------------------------------------------------------
    // Navigation helpers
    // -------------------------------------------------------------------------
    void handleMenuInput();
    void subMenuStart();
    void subMenuUpdate();
    void endSubMenu();
    void activateInput();
    void deactivateInput();
    void updateDataFromRollParts();

    al::MessageSystem* mMsgSystem = nullptr;
    FooterParts* mFooterParts = nullptr;
    GameDataHolder* mGameDataHolder = nullptr;
    InputSeparator* mInput = nullptr;
    FooterParts* mMenuFooters[menuCount] = {};

    SimpleLayoutMenu* mCurrentMenu = nullptr;
    CommonVerticalList* mCurrentList = nullptr;
    static constexpr int kMaxKeyboardInputLength = 8;
    Keyboard* mKeyboard = nullptr;

    bool mIsDecideConfig = false;
    bool mIsDone = false;
};

namespace {
    NERVE_IMPL(StageSceneStateModConfig, MainMenu)
    NERVE_IMPL(StageSceneStateModConfig, RandomizerSettings)
    NERVE_IMPL(StageSceneStateModConfig, RandomizerMiscSettings)
    NERVE_IMPL(StageSceneStateModConfig, CaptureDebug)
    NERVE_IMPL(StageSceneStateModConfig, AbilityDebug)
    NERVE_IMPL(StageSceneStateModConfig, DebugWarpKingdom)
    NERVE_IMPL(StageSceneStateModConfig, DebugWarpSubarea)
    NERVE_IMPL(StageSceneStateModConfig, KingdomChecksList)
#if SMOR_FEATURE_CHAOS_ANY
    NERVE_IMPL(StageSceneStateModConfig, EnemySwapInspector)
#endif
    NERVE_IMPL(StageSceneStateModConfig, OpenKeyboardSeed)
    NERVE_IMPL(StageSceneStateModConfig, OpenKeyboardMoonReq)
    NERVE_IMPL(StageSceneStateModConfig, OpenKeyboardMultiplayer)
    NERVE_IMPL(StageSceneStateModConfig, OpenKeyboardArchipelago)

    NERVES_MAKE_STRUCT(StageSceneStateModConfig,
        MainMenu, RandomizerSettings, RandomizerMiscSettings,
        CaptureDebug, AbilityDebug,
        DebugWarpKingdom, DebugWarpSubarea, KingdomChecksList,
#if SMOR_FEATURE_CHAOS_ANY
        EnemySwapInspector,
#endif
        OpenKeyboardSeed, OpenKeyboardMoonReq, OpenKeyboardMultiplayer, OpenKeyboardArchipelago)
}

namespace {
    static void setMenuRowText(sead::WFixedSafeString<0x200>& row, const char* fmt, ...);
}