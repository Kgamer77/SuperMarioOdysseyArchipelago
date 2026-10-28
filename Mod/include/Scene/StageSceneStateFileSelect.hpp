#pragma once

#include <basis/seadTypes.h>
#include <cstddef>

#include "sead/container/seadSafeArray.h"

#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Message/IUseMessageSystem.h"
#include "al/Library/Message/MessageSystem.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Library/Scene/Scene.h"

#include "game/Input/InputSeparator.h"
#include "game/Layout/CommonVerticalList.h"
#include "game/Layout/SimpleLayoutMenu.h"
#include "game/System/GameDataHolder.h"

#include "Keyboard.hpp"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/RandoSaveFile.h"
#include "Randomizer/UserPresets.h"
#include "Randomizer/WorldMeta.h"
#include "SD/SDHooks.h"

class FooterParts;

class StageSceneStateFileSelect : public al::HostStateBase<al::Scene>,
                                  public al::IUseMessageSystem {
public:
    enum class StartMode : s32
    {
        None,
        Load,
        NewGame,
    };

    StageSceneStateFileSelect(const char* name, al::Scene* scene,
        const al::LayoutInitInfo& initInfo,
        FooterParts* footerParts,
        GameDataHolder* dataHolder);

    virtual void init() override;
    virtual void appear() override;
    virtual void kill() override;
    virtual al::MessageSystem* getMessageSystem() const override { return mMsgSystem; }

    void exeSlotList();
    void exeSlotAction();
    void exeDeleteConfirm();
    void exeNewSaveSettings();
    void exeOpenKeyboardSeed();
    void exeOpenKeyboardMoonReq();
    void exeOpenKeyboardMoonReqDeviation();
    void exeOpenKeyboardSubPageValue();
    void exeOpenKeyboardAdvancedValue();
    void exeOpenKeyboardMedalAvailable();
    void exeOpenKeyboardMedalRequired();
    void exeOpenKeyboardPresetName();
    void exeOpenKeyboardTrackerRoomCode();
    void exeGenerate();
    void exeActivateSlot();
    // Guest join mode's counterpart to exeGenerate(): instead of running the
    // seed generator it drives RoomSnapshot's download off the same progress
    // menu, then hands the downloaded slot to the ordinary Load path.
    void exeJoinRoom();
    // Re-enters the join screen to report a failure raised after the download
    // finished - the load itself happens two nerves later, in exeActivateSlot,
    // whose only failure exit is meaningless for a guest.
    void failGuestJoin(const char16_t* reason);
    // The setup-owner counterpart. A generated local save must not start if
    // the server rejected its durable commit, because that would recreate the
    // old host-owned run that this flow is specifically replacing.
    void failRoomUpload(const char16_t* reason);

    // The title scene doesn't pump these menus' layout kit, so drive every
    // menu's nerve/animation ourselves each frame (called from exeFileSelect).
    void updateMenus();

    bool isDone() const { return mIsDone; }
    bool isStartRequested() const { return mStartMode != StartMode::None; }
    StartMode getStartMode() const { return mStartMode; }
    s32 getSelectedSlot() const { return mSelectedSlot; }
    GameDataHolder* getGameDataHolder() const { return mGameDataHolder; }

    // Mode flag, not a second instance: gStageSceneStateFileSelect is
    // constructed exactly once (PauseMenuHooks.cpp), and this file's own
    // documented crash history around a sibling state's heap allocation
    // (StageSceneStateModConfig's leak, PauseMenuHooks.cpp) is a direct
    // warning against allocating a second copy of this same heavy object
    // graph. The title screen's Multiplayer button calls this (Multiplayer)
    // before entering FileSelect; Randomize! never touches it, leaving the
    // default (SinglePlayer).
    //
    // Also re-runs the slot list scan (see the .cpp) - appear() already
    // builds the slot list once, off whatever category was active at that
    // moment, before the Multiplayer button's room-code prompt has even
    // asked for a code yet (confirmed via mp_debug log: appear() and
    // rebuildSlotList() both fire with mSaveCategory still SinglePlayer,
    // several log lines before this setter's later call sets it to
    // Multiplayer) - without re-scanning here, the category itself ends up
    // correct but the on-screen list stays the stale SinglePlayer one for
    // the rest of that FileSelect visit, since nothing else ever rebuilds
    // it afterward.
    void setSaveCategory(SaveRedirect::Category category);
    SaveRedirect::Category getSaveCategory() const { return mSaveCategory; }

    // Guest join mode: what a player who joined SOMEONE ELSE'S room gets
    // instead of the normal file select.
    //
    // A guest has no seed to configure - the host's is downloaded whole (see
    // Multiplayer/RoomSnapshot.h) - so the slot list and every seed-affecting
    // page would be a lie. In this mode appear() skips straight to the paged
    // settings, isPageVisible() narrows those pages to the ones that are
    // genuinely local (Quality of Life I/II and Tracker, all backed by
    // GlobalConfig rather than rando.bin, so downloading the host's save does
    // not overwrite them), and the Summary page's Generate Seed row becomes
    // Join Game.
    //
    // Static because pageDef()/isPageVisible() are - there is exactly one
    // StageSceneStateFileSelect for the whole process (gStageSceneStateFileSelect,
    // constructed once in PauseMenuHooks.cpp), so per-instance would buy
    // nothing.
    static void setGuestJoinMode(bool guest) { sGuestJoinMode = guest; }
    static bool isGuestJoinMode() { return sGuestJoinMode; }

    // Setup-owner mode: this player is the one making the room's seed, so the
    // Multiplayer settings page (Death Link) is theirs to set. Off for
    // single-player - those settings would have no room to apply to - and off
    // for a guest, whose own copy is ignored in favour of the host's
    // (RoomStatus::flags). Static for the same reason as sGuestJoinMode:
    // isPageVisible() is.
    static void setMultiplayerHostMode(bool host) { sMultiplayerHostMode = host; }
    static bool isMultiplayerHostMode() { return sMultiplayerHostMode; }

private:
    // ONE reconfigurable settings menu drives every page. The title scene's
    // executor table can't hold a separate menu per page (6 menus × a vertical
    // list each overflows it at scene init), so the pages share MENU_SETTINGS and
    // it is rebuilt on each L/R page switch. Its fixed physical parts form a
    // scrolling window over the logical rows.
    enum Menu
    {
        MENU_SLOT_LIST,
        MENU_SLOT_ACTION,
        MENU_DELETE_CONFIRM,
        MENU_SETTINGS,
        MENU_GENERATE_PROGRESS,
        MENU_COUNT,
    };

#if SMOR_FEATURE_CHAOS_ANY
    static constexpr s32 kPageCount = 16; // number of logical settings pages
#else
    static constexpr s32 kPageCount = 15; // number of logical settings pages
#endif
    // Logical index of the run-mode-specific page. It sits right after Main
    // Settings (where the Run Mode is chosen) and is skipped entirely by L/R
    // navigation for modes that have no settings of their own - Standard, and
    // any mode in a build where its feature is compiled out.
    static constexpr s32 kGameModePageIndex = 1;
    // Largest page's option count - currently the Game Mode Settings union,
    // which carries every run mode's rows even though pageDef() only ever shows
    // one mode's at a time. MUST be >= every page's row count: it sizes the
    // settings list and the mSettingsRollParts[] array. Adding a row to the
    // biggest page without bumping this writes past mSettingsRollParts and
    // corrupts an adjacent layout actor's vtable (crash in
    // LayoutPartsActorKeeper::update).
    static constexpr s32 kMaxPageOptions = 96;
    // The layout archive's visible row-slot count is a separate, lower limit
    // than this logical array-sizing constant. configureScrollableList() keeps
    // the settings menu inside that physical window while allowing larger
    // pages to scroll.

    // The info box renders a description across up to this many stacked single-line
    // text layouts. The reused TxtOption textbox has a small (~54 glyph) buffer, so
    // a long description is wrapped and split one line per layout to avoid mid-word
    // truncation while keeping the original wording.
    static constexpr s32 kInfoMaxLines = 4;

    static constexpr s32 kSlotCount = 10;
    // The guest save root holds exactly one slot: it is scratch space for the
    // room save the host hands over, wiped on every join
    // (SaveRedirect::deleteGuestTemp, which is written against Slot1), not a
    // library of files to browse.
    static constexpr s32 kGuestSlot = 1;
    // The multiplayer HOST root works the same way, for the same reason. A
    // room's seed lives on the server (server/Server/RoomStore.cs) from the
    // moment it finishes generating, and everyone - the player who made it
    // included - gets it back by asking the server for it, so there is nothing
    // for a host to browse either. Picking a room code is the whole choice
    // now: an active code joins that room, a fresh one makes you its host and
    // drops you straight on the settings pages, so MPSaves/Slot1 is scratch
    // space for the seed being built rather than a save file anyone keeps.
    static constexpr s32 kHostSlot = 1;
    static constexpr s32 kMaxMsgCount = 96;
    static constexpr s32 kMaxKeyboardInputLength = 8;
    // A settings page fills one row of mMsgList / mSettingsEnable per option, so
    // the biggest page has to fit both arrays. Keep these capacities together;
    // the assertion catches a future mismatch before it becomes heap corruption.
    static_assert(kMaxPageOptions <= kMaxMsgCount,
        "kMaxMsgCount must cover every settings row a page can show");

public:
    // Unified option identifiers across every page. These values are serialized
    // in UserPresets::Entry, so they are deliberately explicit and must never
    // be reordered, reused, or inserted between existing values. Feature-gated
    // options still reserve their ids in builds where the feature is disabled.
    enum OptionId : s32
    {
        OPT_NONE = -1,
        // Main
        OPT_CURRENT_SEED = 0,
        OPT_RANDOM_SEED = 1,
        OPT_SET_SEED = 2,
        OPT_PRESET = 3,
        OPT_USER_PRESET = 4,
        OPT_RUN_MODE = 5,
        // Randomization
        OPT_MOONS = 6,
        OPT_TRIPLE_MOONS = 7, // 4-state roll: Vanilla / Anywhere / Randomized / Anywhere + Randomized
        OPT_LOADING_ZONES = 8,
        OPT_PAINTINGS = 9,
        OPT_KINGDOM_ORDER = 10,
        OPT_ENEMY_SWAP = 11,
        // Retired: swappable captures are simply unavailable to logic unless
        // an authored capture gate opts one back in, so there is nothing left
        // to toggle. The id stays reserved so older tagged user presets that
        // still carry it stay readable (it is ignored on apply).
        OPT_KEEP_CRITICAL_CAPTURES_RETIRED = 12,
        OPT_NPC_SWAP = 13,
        OPT_PROP_SWAP = 14,
        OPT_PROP_SWAP_RATE = 15,
        OPT_PLACEMENT_LOGIC = 16,
        // Retired: the checkbox folded into OPT_SEED_DIFFICULTY (a set value
        // means Hard). The id stays reserved and is still honoured on apply so
        // older tagged user presets migrate instead of breaking.
        OPT_PRIORITIZE_DIFFICULT_MOVEMENT_RETIRED = 17,
        // Retired: Seed Style split into the three axes appended at the end of
        // this enum. Reserved and honoured on apply for the same reason.
        OPT_SEED_STYLE_RETIRED = 18,
        OPT_TOADETTE_MEDAL_RATIO = 19,
        // Locks
        OPT_LOCKED_CAPTURES = 20,
        OPT_LOCKED_ABILITIES = 21,
        OPT_STARTING_EQUIPMENT = 22,
        OPT_CAPTURE_DIFFICULTY = 23,
        OPT_PURPLE_SHOP_ABILITIES = 24,
        OPT_PURPLE_SHOP_CAPTURES = 25,
        OPT_PURPLE_SHOP_MOONS = 26,
        OPT_PURPLE_SHOP_PRICE = 27,
        // Additional
        OPT_MOON_REQUIREMENT = 28,
        OPT_MOON_REQ_DIFFICULTY = 29,
        OPT_CAP_MOON_USE = 30,
        OPT_CLOUD_MOON_USE = 31,
        OPT_MOON_MOON_USE = 32,
        OPT_WORLD_PEACE_REQ = 33,
        OPT_MOON_ROCK_KEY = 34,
        OPT_VISITED_KINGDOMS_MAP = 35,
        // Misc
        OPT_SKYBOXES = 36,
        OPT_STABILIZE_SHADERS = 37,
        OPT_MUSIC = 38,
        OPT_STREAMER_MODE = 39,
        OPT_MUSIC_OVERLAY = 40,
        OPT_SPHINX = 41,
        OPT_HINT_PRICES = 42,
        OPT_MOON_PURCHASES = 43,
        // Quality of Life
        OPT_FAST_MOON_DEMOS = 44,
        OPT_MOTION_FREE = 45,
        OPT_SKIP_CUTSCENE_CAMERAS = 46,
        // Retired: costume-gated doors are now always open, so there is no
        // toggle. The id stays reserved so older tagged user presets that still
        // carry it stay readable (it is simply ignored on apply).
        OPT_COSTUME_DOORS_OPEN_RETIRED = 47,
        OPT_FREE_COSTUME_MOON = 48,
        OPT_ONE_SHEEP_HERDING = 49,
        OPT_TRACKER_OVERLAY = 50,
        OPT_BOARD_SCALE = 51,
        OPT_EASY_MINIGAMES = 52,
        OPT_AREA_NAME_OVERLAY = 53,
        OPT_NO_CLIP = 54,
        OPT_TRACKER_CAVE_SKIP = 55,
        OPT_TRACKER_MOON_LIMITS = 56,
        OPT_TRACKER_RIGHT_ALIGN = 57,
        OPT_COMPLETED_KINGDOM_MOON_COLLECTED = 58,
        OPT_RANDOMIZE_OUTFITS = 59,
        // Misc. These ids stay after the historical QoL ids so older tagged
        // presets continue to point at the same settings.
        OPT_EXTENDED_HINTS = 60,
        // Replaces the two legacy hint toggles above. Kept after them so old
        // tagged user presets remain readable and can be migrated on apply.
        OPT_HINT_MODE = 61,

        // Summary
        OPT_SAVE_AS_PRESET = 62,
        OPT_GENERATE_SEED = 63,

        // Randomization
        // Appended after every existing option id so tagged user presets keep
        // their historical ids and continue to apply correctly.
        OPT_START_KINGDOM_STATE = 64,
        // Quality of Life
        // Appended here for the same reason as OPT_START_KINGDOM_STATE above.
        OPT_TRACKER_TIMER = 65,
        // Game Mode Settings. Appended, like everything above, so tagged user
        // presets written before this page existed stay readable.
        OPT_SOFT_NUZLOCKE = 66,
        OPT_SOFT_NUZLOCKE_LOSS = 67,
        OPT_TIMER_START = 68,
        OPT_TIMER_MOON_BONUS = 69,
        OPT_TIMER_TRIPLE_BONUS = 70,
        OPT_TOADETTE_MEDAL_CUSTOM = 71,
        OPT_TOADETTE_MEDAL_AVAILABLE = 72,
        OPT_TOADETTE_MEDAL_REQUIRED = 73,
        OPT_SOFT_NUZLOCKE_ABILITY_LOSS = 74,
        OPT_SOFT_NUZLOCKE_CAPTURE_LOSS = 75,
        // Randomization. Appended for the same reason as everything above.
        OPT_CHEST_ORDER = 76,
        OPT_RANDOMIZED_START_POSITION = 77,
        // Music Randomization page. OPT_MUSIC / OPT_STREAMER_MODE /
        // OPT_MUSIC_OVERLAY moved there from Misc and keep their original ids.
        OPT_MUSIC_DISABLE_2D = 78,
        OPT_MUSIC_DISABLE_EXTRA = 79,
        // Quality of Life II.
        OPT_ALWAYS_NEUTRAL_CAP_THROW = 80,
        // Music Randomization, appended after the id above rather than beside
        // its page-mates for the same never-reorder reason.
        OPT_MUSIC_DISABLE_BOSS = 81,
        // Additional Options II. Appended rather than placed beside its
        // page-mates for the same never-reorder reason as everything above.
        OPT_SUBAREA_UNLOCKS = 82,
        OPT_SUBAREA_UNLOCK_HOST = 83,
        // Nuzlocke settings. Appended so older tagged user presets retain
        // their historical option ids.
        OPT_EXTREME_NUZLOCKE = 84,
        // Moon Allocation. Appended after the Nuzlocke setting so every existing
        // preset option id remains stable.
        OPT_ENABLE_MOON_REALLOCATION = 85,
        OPT_RANDOMIZE_MOON_ALLOCATION = 86,
        OPT_MOON_ALLOCATION_DIFFICULTY = 87,
        OPT_MOON_COIN_POLICY = 88,
        // Web tracker room code. Appended after every existing option id for
        // the same never-reorder/never-reuse reason as everything above -
        // deliberately excluded from captureCurrentSettingsSnapshot()'s
        // preset capture (a room code baked into a shared preset would send
        // everyone who applies it into the same tracker session).
        OPT_TRACKER_WEB_HEADER = 89,
        OPT_TRACKER_ROOM_CODE = 90,
        // Chaotic Randomization. Appended after every existing option id for
        // the same never-reorder/never-reuse reason as everything above.
        // Retired: the planned swaps are written into the spoiler log now,
        // so there is no separate diagnostic file to switch on. Reserved for
        // the same preset-compatibility reason as the id above.
        OPT_CHAOS_SWAP_LOG_RETIRED = 91,
        OPT_TWO_D_SWAP = 92,
        // Global QoL. Appended so existing tagged user-preset ids remain stable.
        OPT_UNLOCK_HINT_ART = 93,
        // Randomization. Appended so every existing tagged preset id remains stable.
        OPT_COIN_RANDO = 94,
        // Timer Mode penalties. Appended after every existing option id for the
        // same never-reorder/never-reuse reason as everything above. Index 0 on
        // both is "Off", so a preset written before they existed applies them as
        // absent rather than as a penalty the author never chose.
        OPT_TIMER_DAMAGE_PENALTY = 95,
        OPT_TIMER_DEATH_PENALTY = 96,
        // Randomization II. Appended for the same never-reorder reason.
        OPT_SHOP_LOCATIONS = 97,
        // Hint Configuration. These are appended so user-preset option ids
        // written before Hint Tokens retain their original meaning.
        OPT_HINT_PRICE_MOONS = 98,
        OPT_HINT_PRICE_CAPTURES = 99,
        OPT_HINT_PRICE_ABILITIES = 100,
        OPT_HINT_PRICE_PATH = 101,
        OPT_HINT_PRICE_PURCHASES = 102,
        // Read-only, derived from special-moon settings; KIND_DISPLAY means
        // it is deliberately excluded from preset snapshots.
        OPT_HINT_CURRENCY_MODE = 103,
        // Appended: applies an additive price increase independently to each
        // hint category after a successful purchase.  Do not reuse the retired
        // OPT_HINT_MODE id above; tagged presets serialize these ids.
        OPT_HINT_PROGRESSIVE_PRICE = 104,
        // Timer Mode's award decay. Appended for the same never-reorder reason;
        // index 0 is "Disabled", so a preset written before it existed applies
        // the flat awards its author actually chose.
        OPT_TIMER_HALF_LIFE = 105,
        // Everything below is MP-branch-only and has never shipped in a
        // release build, unlike everything above (already-released
        // origin/main ids) - so THESE are the ones renumbered to keep
        // origin/main's real preset-compatible numbering intact, not the
        // other way around.
        //
        // Retired with the Tab system: pages are a single flat L/R list now,
        // and the two-column pages need D-pad left/right for column switching,
        // which Tabbed mode spent on paging. The ids below stay so every later
        // id keeps its number and tagged user presets written before the
        // rework remain readable; all of them are ignored on apply.
        OPT_TABBED_SETTINGS_UI_RETIRED = 106,
        OPT_CUSTOM_TAB_COUNT_RETIRED = 107,
        OPT_CUSTOM_TAB_PAGE_0_RETIRED = 108,
        OPT_CUSTOM_TAB_PAGE_1_RETIRED = 109,
        OPT_CUSTOM_TAB_PAGE_2_RETIRED = 110,
        OPT_CUSTOM_TAB_PAGE_3_RETIRED = 111,
        OPT_CUSTOM_TAB_PAGE_4_RETIRED = 112,
        OPT_CUSTOM_TAB_PAGE_5_RETIRED = 113,
        OPT_CUSTOM_TAB_PAGE_6_RETIRED = 114,
        OPT_CUSTOM_TAB_PAGE_7_RETIRED = 115,
        OPT_CUSTOM_TAB_PAGE_8_RETIRED = 116,
        OPT_CUSTOM_TAB_PAGE_9_RETIRED = 117,
        OPT_CUSTOM_TAB_PAGE_10_RETIRED = 118,
        OPT_CUSTOM_TAB_PAGE_11_RETIRED = 119,
        OPT_CUSTOM_TAB_PAGE_12_RETIRED = 120,
        OPT_CUSTOM_TAB_PAGE_13_RETIRED = 121,
        OPT_CUSTOM_TAB_PAGE_14_RETIRED = 122,
        OPT_CUSTOM_TAB_PAGE_15_RETIRED = 123,
        OPT_CUSTOM_TAB_PAGE_16_RETIRED = 124,
        OPT_CUSTOM_TAB_PAGE_17_RETIRED = 125,
        OPT_EDIT_TABS_RETIRED = 126,
        OPT_CUSTOM_TAB_RENAME_SELECT_RETIRED = 127,
        OPT_CUSTOM_TAB_RENAME_ACTION_RETIRED = 128,
        // Guest-only replacement for OPT_GENERATE_SEED on the Summary page: a
        // guest does not make a seed, it pulls the host's. Never captured into
        // a preset (it is an action, like Generate Seed) and never reachable
        // outside guest join mode.
        OPT_JOIN_GAME = 129,
        // Multiplayer page (setup owner only). Room-owned rather than
        // seed-owned in effect: it lives in rando.bin like every other seed
        // setting, but the server mirrors the host's value back to everyone
        // (RoomStatus::flags), so a guest's own copy is never consulted.
        // Excluded from presets for that reason - see
        // captureCurrentSettingsSnapshot.
        OPT_DEATH_LINK = 130,
        // Same room-owned story as Death Link: set by the host, mirrored to
        // everyone through RoomStatus::flags, excluded from presets. While off,
        // the server will not relay player state across team lines.
        OPT_SYNC_OTHER_TEAMS = 131,
        // The one Multiplayer row EVERY player gets, host included - the only
        // team control there is. Backed by GlobalConfig rather than rando.bin
        // (see Randomizer::getSelectedTeam), so it is also excluded from
        // presets: a preset carrying someone else's team number would silently
        // move you off yours.
        OPT_TEAM = 132,
        // Hint Configuration: makes the progressive increment count every hint
        // purchase on the save instead of each category's own, so the price
        // ladder is shared. Off by default, which is the per-category
        // behaviour a preset written before it existed described.
        OPT_HINT_PROGRESSIVE_GLOBAL = 133,
        // Randomization page: re-prices Crazy Cap's gold-coin catalogue.
        OPT_SHOP_PRICE_RANDO = 134,
        // Purple Coin Shop page: reads the price selection as a share of the
        // kingdom's purple-coin total instead of a flat amount.
        OPT_PROPORTIONAL_KINGDOM_PRICING = 135,
        OPT_CUSTOM_TAB_PAGE_18_RETIRED = 136,
        // Logic Settings. The three placement axes that replaced Seed Style.
        // Appended after every existing id for the same never-reorder reason as
        // everything above: user presets serialize these values. Index 2 is
        // Standard on all three, so a preset written before they existed
        // applies the historical behaviour rather than a level nobody chose.
        OPT_UNLOCK_PACING = 137,
        OPT_SEED_OPENNESS = 138,
        OPT_SEED_DIFFICULTY = 139,
        // Movement Abilities page - the Ability Lock sub-page. Appended, never
        // inserted, for the same preset-serialization reason as everything above.
        // Every one defaults to index 0 / unchecked, which is the granted
        // behaviour a preset written before this page existed described.
        OPT_MOON_GRAVITY = 140,
        OPT_SWIM_LOCK = 141,
        OPT_GRAB_LOCK = 142,
        OPT_JAXI_LOCK = 143,
        OPT_SCOOTER_LOCK = 144,
        OPT_CUSTOM_TAB_PAGE_19_RETIRED = 145,
        OPT_ROCKET_FLOWER_LOCK = 146,
        OPT_NPC_TALK_LOCK = 147,
        OPT_PIPE_2D_LOCK = 148,
        OPT_TIMER_CHALLENGE_LOCK = 149,
        // Quality of Life II. Appended, never inserted, for the same
        // preset-serialization reason as everything above.
        OPT_FAST_DOG = 150,
        OPT_UNCAPPED_COINS = 151,
        // UI-only row shared by every Phase 3 advanced-settings page. It is
        // deliberately appended and never captured by user presets.
        OPT_ADVANCED_RESET = 152,
        OPT_CUSTOM_TAB_PAGE_20_RETIRED = 153,
        // Phase 4 Wave A derived/editor rows. These ids are UI-only and are
        // appended so every existing tagged preset id remains stable.
        OPT_MOON_ALLOCATION_MODE = 154,
        OPT_TRIPLE_MOONS_ANYWHERE = 155,
        OPT_MOON_ALLOCATION_KINGDOM_0 = 156,
        OPT_MOON_ALLOCATION_KINGDOM_16 = OPT_MOON_ALLOCATION_KINGDOM_0 + 16,
        OPT_TRIPLE_MOON_KINGDOM_0 = OPT_MOON_ALLOCATION_KINGDOM_16 + 1,
        OPT_TRIPLE_MOON_KINGDOM_16 = OPT_TRIPLE_MOON_KINGDOM_0 + 16,
        // Phase 4 Wave B editor rows. These ranges are UI-only; the actual
        // masks and numeric values are captured by the explicit snapshot ids
        // below. Reserve enough IDs for the stable item enumerations without
        // inserting anything into the already-serialized Wave A block.
        OPT_ADVANCED_CAPTURE_POOL_BASE = OPT_TRIPLE_MOON_KINGDOM_16 + 1,
        OPT_ADVANCED_ABILITY_POOL_BASE = OPT_ADVANCED_CAPTURE_POOL_BASE + 96,
        // AbilityId_Count is currently larger than 32 (movement abilities are
        // part of the same stable enum), so keep a full 64-id bank here even
        // though the mask itself is one u64.
        OPT_ADVANCED_STARTING_CAPTURES = OPT_ADVANCED_ABILITY_POOL_BASE + 64,
        OPT_ADVANCED_STARTING_THROWS = OPT_ADVANCED_STARTING_CAPTURES + 1,
        OPT_ADVANCED_STARTING_ABILITIES = OPT_ADVANCED_STARTING_THROWS + 1,
        OPT_ADVANCED_CAPTURE_POOL_MASK_LO = OPT_ADVANCED_STARTING_ABILITIES + 1,
        OPT_ADVANCED_CAPTURE_POOL_MASK_HI = OPT_ADVANCED_CAPTURE_POOL_MASK_LO + 1,
        OPT_ADVANCED_ABILITY_POOL_MASK = OPT_ADVANCED_CAPTURE_POOL_MASK_HI + 1,
        OPT_ADVANCED_PURPLE_SHOP_CUSTOM_PRICE = OPT_ADVANCED_ABILITY_POOL_MASK + 1,

        // Phase 4 Wave C editor rows. These ids are stable UI enumerations;
        // the authoritative masks/values live in Randomizer and rando.bin.
        OPT_ADVANCED_SPECIAL_MOON_CAP_BASE = OPT_ADVANCED_PURPLE_SHOP_CUSTOM_PRICE + 1,
        OPT_ADVANCED_SPECIAL_MOON_CLOUD_BASE = OPT_ADVANCED_SPECIAL_MOON_CAP_BASE + 7,
        OPT_ADVANCED_SPECIAL_MOON_MOON_BASE = OPT_ADVANCED_SPECIAL_MOON_CLOUD_BASE + 7,
        OPT_ADVANCED_MOON_ROCK_SCOPE = OPT_ADVANCED_SPECIAL_MOON_MOON_BASE + 7,
        OPT_ADVANCED_SUBAREA_SCOPE = OPT_ADVANCED_MOON_ROCK_SCOPE + 1,
        OPT_ADVANCED_START_KINGDOM_BASE = OPT_ADVANCED_SUBAREA_SCOPE + 1,
        OPT_ADVANCED_START_KINGDOM_16 = OPT_ADVANCED_START_KINGDOM_BASE + 16,
        OPT_ADVANCED_REQ_DEVIATION_BASE = OPT_ADVANCED_START_KINGDOM_16 + 1,
        OPT_ADVANCED_REQ_DEVIATION_16 = OPT_ADVANCED_REQ_DEVIATION_BASE + 16,

        // Phase 4 Wave D editor rows.
        OPT_ADVANCED_TIMER_VALUE_BASE = OPT_ADVANCED_REQ_DEVIATION_16 + 1,
        OPT_ADVANCED_TIMER_VALUE_5 = OPT_ADVANCED_TIMER_VALUE_BASE + 5,
        OPT_ADVANCED_NUZLOCKE_VALUE_BASE = OPT_ADVANCED_TIMER_VALUE_5 + 1,
        OPT_ADVANCED_NUZLOCKE_VALUE_2 = OPT_ADVANCED_NUZLOCKE_VALUE_BASE + 2,
        OPT_ADVANCED_HINT_COIN_OVERRIDE = OPT_ADVANCED_NUZLOCKE_VALUE_2 + 1,
        OPT_ADVANCED_HINT_CUSTOM_PRICE = OPT_ADVANCED_HINT_COIN_OVERRIDE + 1,
        // Retired: the per-theme editor is gone. The theme mask is still a
        // real setting - the seed builder and the save both carry it - so the
        // id bank stays reserved rather than being reused.
        OPT_ADVANCED_MUSIC_THEME_BASE = OPT_ADVANCED_HINT_CUSTOM_PRICE + 1,
        OPT_ADVANCED_MUSIC_THEME_30 = OPT_ADVANCED_MUSIC_THEME_BASE + 30,
        OPT_ADVANCED_MINIGAME_LOW = OPT_ADVANCED_MUSIC_THEME_30 + 1,
        OPT_ADVANCED_MINIGAME_HIGH = OPT_ADVANCED_MINIGAME_LOW + 1,
        OPT_ADVANCED_SHEEP_COUNT = OPT_ADVANCED_MINIGAME_HIGH + 1,
        // Phase 5 UI rows and scalar settings-code extensions. Appended so all
        // existing tagged preset ids remain stable.
        // Retired with the Settings Code: the typed form of ~150 settings runs
        // to a hundred characters or more, which is not something anyone is
        // going to read off a stream. Presets remain the way to move settings.
        OPT_LOAD_SETTINGS_RETIRED = OPT_ADVANCED_SHEEP_COUNT + 1,
        OPT_SETTINGS_CODE = OPT_LOAD_SETTINGS_RETIRED + 1,
        OPT_SETTINGS_PHRASE = OPT_SETTINGS_CODE + 1,
        OPT_COPY_SETTINGS_CODE = OPT_SETTINGS_PHRASE + 1,
        OPT_SETTINGS_HINT_OVERRIDE_MASK = OPT_COPY_SETTINGS_CODE + 1,
        OPT_SETTINGS_HINT_CUSTOM_PRICE_BASE = OPT_SETTINGS_HINT_OVERRIDE_MASK + 1,
        OPT_SETTINGS_HINT_CUSTOM_PRICE_4 = OPT_SETTINGS_HINT_CUSTOM_PRICE_BASE + 4,

        // Appended for Phase 8
        OPT_MOON_ROCK_KEY_ANY_MOON = OPT_SETTINGS_HINT_CUSTOM_PRICE_4 + 1,
        OPT_MOON_ROCK_KEY_OWN_KINGDOM = OPT_MOON_ROCK_KEY_ANY_MOON + 1,
        OPT_SUBAREA_SCOPE_ANY_MOON = OPT_MOON_ROCK_KEY_OWN_KINGDOM + 1,
        OPT_SUBAREA_SCOPE_OWN_KINGDOM = OPT_SUBAREA_SCOPE_ANY_MOON + 1,
        OPT_DARK_MOON_USE = OPT_SUBAREA_SCOPE_OWN_KINGDOM + 1,
        OPT_DARKER_MOON_USE = OPT_DARK_MOON_USE + 1,
        OPT_MUSHROOM_MOON_USE = OPT_DARKER_MOON_USE + 1,
        OPT_ADVANCED_SPECIAL_MOON_DARK_BASE = OPT_MUSHROOM_MOON_USE + 1,
        OPT_ADVANCED_SPECIAL_MOON_DARKER_BASE = OPT_ADVANCED_SPECIAL_MOON_DARK_BASE + 7,
        OPT_ADVANCED_SPECIAL_MOON_MUSHROOM_BASE = OPT_ADVANCED_SPECIAL_MOON_DARKER_BASE + 7,
        // Top row of the Locked Abilities advanced page: whether a multi-stage
        // move arrives one stage at a time or in its final form.
        OPT_ADVANCED_PROGRESSIVE_ABILITIES = OPT_ADVANCED_SPECIAL_MOON_MUSHROOM_BASE + 7,
        // Which kingdoms' moons hand out captures / abilities. Front-ends over
        // the per-kingdom special-moon-use masks, with one advanced row per
        // kingdom the generator can host a reward in.
        OPT_CAPTURE_GRANT_KINGDOMS = OPT_ADVANCED_PROGRESSIVE_ABILITIES + 1,
        OPT_ABILITY_GRANT_KINGDOMS = OPT_CAPTURE_GRANT_KINGDOMS + 1,
        // Cloud Kingdom is the only kingdom that can host a painting unlock, so
        // this one is a plain two-value select with no advanced page.
        OPT_PAINTING_GRANT_KINGDOMS = OPT_ABILITY_GRANT_KINGDOMS + 1,
        // Which kingdoms gate progress behind a moon requirement. A front-end
        // over the same per-kingdom KingdomUnlock bit the generator already
        // reads, with one advanced row per eligible kingdom.
        OPT_REQUIREMENT_KINGDOMS = OPT_PAINTING_GRANT_KINGDOMS + 1,
        OPT_ADVANCED_CAPTURE_GRANT_BASE = OPT_REQUIREMENT_KINGDOMS + 1,
        OPT_ADVANCED_CAPTURE_GRANT_END = OPT_ADVANCED_CAPTURE_GRANT_BASE + 6,
        OPT_ADVANCED_ABILITY_GRANT_BASE = OPT_ADVANCED_CAPTURE_GRANT_END,
        OPT_ADVANCED_ABILITY_GRANT_END = OPT_ADVANCED_ABILITY_GRANT_BASE + 6,
        OPT_ADVANCED_REQ_KINGDOM_BASE = OPT_ADVANCED_ABILITY_GRANT_END,
        // Retired: six ids, indexed by a table of six kingdoms. The advanced
        // page covers the whole fourteen-kingdom route now, which does not fit
        // here - see OPT_ADVANCED_REQ_KINGDOM2_BASE.
        OPT_ADVANCED_REQ_KINGDOM_END = OPT_ADVANCED_REQ_KINGDOM_BASE + 6,
        // Kingdom Moons page: what each kingdom's moons hand out. These
        // replace the six per-kingdom selects above as the rows a preset
        // captures - a preset entry carries the whole mask rather than the
        // display index, so a multi-value kingdom survives a round trip.
        OPT_KINGDOM_MOON_USE_0 = OPT_ADVANCED_REQ_KINGDOM_END,
        OPT_KINGDOM_MOON_USE_16 = OPT_KINGDOM_MOON_USE_0 + 16,
        // One id per (kingdom, SpecialMoonUse) pair for the multi-select
        // behind each row: 17 kingdoms x 7 uses.
        OPT_ADVANCED_KINGDOM_MOON_USE_BASE = OPT_KINGDOM_MOON_USE_16 + 1,
        OPT_ADVANCED_KINGDOM_MOON_USE_END = OPT_ADVANCED_KINGDOM_MOON_USE_BASE + 17 * 7,
        // Opens the whole-set presets that used to be rows of their own on
        // other pages (Requirement / Capture Grant / Ability Grant / Painting).
        // Retired before it shipped: the per-kingdom grid edits every kingdom
        // directly, so the whole-set presets had nothing left to add. Kept as
        // the anchor the ids below are numbered from.
        OPT_KINGDOM_MOON_PRESETS_RETIRED = OPT_ADVANCED_KINGDOM_MOON_USE_END,
        // Hint Settings page: which currency each category is priced in, and
        // one progressive increment per currency instead of one for whichever
        // currency happened to be active.
        OPT_HINT_CURRENCY_MOONS = OPT_KINGDOM_MOON_PRESETS_RETIRED + 1,
        OPT_HINT_CURRENCY_CAPTURES = OPT_HINT_CURRENCY_MOONS + 1,
        OPT_HINT_CURRENCY_ABILITIES = OPT_HINT_CURRENCY_CAPTURES + 1,
        OPT_HINT_CURRENCY_PATH = OPT_HINT_CURRENCY_ABILITIES + 1,
        OPT_HINT_CURRENCY_PURCHASES = OPT_HINT_CURRENCY_PATH + 1,
        OPT_HINT_COIN_PROGRESSIVE = OPT_HINT_CURRENCY_PURCHASES + 1,
        OPT_HINT_TOKEN_PROGRESSIVE = OPT_HINT_COIN_PROGRESSIVE + 1,
        // Both shared-ladder flags, on the advanced page behind either
        // progressive row.
        OPT_HINT_COIN_PROGRESSIVE_GLOBAL = OPT_HINT_TOKEN_PROGRESSIVE + 1,
        OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL = OPT_HINT_COIN_PROGRESSIVE_GLOBAL + 1,
        // The +/- spread the named Req Difficulty values describe, as its own
        // row rather than only as the thing that makes that select read Custom.
        OPT_MOON_REQ_DEVIATION = OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL + 1,
        // Retired before it shipped: a kingdom can hand out more than one kind
        // of reward, so the three bits stayed independent checkboxes.
        OPT_ADVANCED_KINGDOM_REWARD_KIND_RETIRED = OPT_MOON_REQ_DEVIATION + 1,
        // One per kingdom: which unlock its moons carry, as a select. The three
        // checkboxes this replaces could express "captures and abilities and
        // either", which is not a thing a moon can be.
        OPT_ADVANCED_KINGDOM_UNLOCK_0 = OPT_ADVANCED_KINGDOM_REWARD_KIND_RETIRED + 1,
        OPT_ADVANCED_KINGDOM_UNLOCK_16 = OPT_ADVANCED_KINGDOM_UNLOCK_0 + 16,
        // One per frontier-route kingdom: does it gate progress. Indexed by
        // world id directly, unlike the retired range above.
        OPT_ADVANCED_REQ_KINGDOM2_BASE = OPT_ADVANCED_KINGDOM_UNLOCK_16 + 1,
        OPT_ADVANCED_REQ_KINGDOM2_END = OPT_ADVANCED_REQ_KINGDOM2_BASE + 14,
        // The Moon Requirement advanced page: the total, then one row per
        // kingdom that gates. Indexed by world id, so all seventeen are
        // reserved even though only the route ones are ever shown.
        OPT_ADVANCED_REQ_TOTAL = OPT_ADVANCED_REQ_KINGDOM2_END,
        OPT_ADVANCED_REQ_BASE_0 = OPT_ADVANCED_REQ_TOTAL + 1,
        OPT_ADVANCED_REQ_BASE_16 = OPT_ADVANCED_REQ_BASE_0 + 16,
        // Music Source. Appended, never inserted, for the same
        // preset-serialization reason as everything above.
        OPT_MUSIC_SOURCE = OPT_ADVANCED_REQ_BASE_16 + 1,
        OPT_COUNT = OPT_MUSIC_SOURCE + 1,
    };

    // Tagged preset entries and the settings code are unsigned, so the signed
    // moon-requirement deviation travels with this added and taken off again.
    static constexpr s32 kMoonReqDeviationBias = 128;

private:
    // Every advanced range below is closed by the id that starts the next
    // one rather than by OPT_COUNT: an open-ended range silently swallowed
    // each option appended after it.
    static bool isAdvancedRequirementBaseOption(OptionId id) {
        return id >= OPT_ADVANCED_REQ_BASE_0 && id <= OPT_ADVANCED_REQ_BASE_16;
    }
    static s32 advancedRequirementBaseIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_ADVANCED_REQ_BASE_0);
    }

    static bool isAdvancedKingdomUnlockOption(OptionId id) {
        return id >= OPT_ADVANCED_KINGDOM_UNLOCK_0
            && id <= OPT_ADVANCED_KINGDOM_UNLOCK_16;
    }
    static s32 advancedKingdomUnlockIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_ADVANCED_KINGDOM_UNLOCK_0);
    }

    static bool isAdvancedRequirementKingdomOption(OptionId id) {
        return id >= OPT_ADVANCED_REQ_KINGDOM2_BASE
            && id < OPT_ADVANCED_REQ_KINGDOM2_END;
    }
    // The world id of the kingdom this row gates.
    static s32 advancedRequirementKingdomIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_ADVANCED_REQ_KINGDOM2_BASE);
    }

    static bool isAdvancedCaptureGrantOption(OptionId id) {
        return id >= OPT_ADVANCED_CAPTURE_GRANT_BASE
            && id < OPT_ADVANCED_CAPTURE_GRANT_END;
    }
    static s32 advancedCaptureGrantIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_ADVANCED_CAPTURE_GRANT_BASE);
    }
    static bool isAdvancedAbilityGrantOption(OptionId id) {
        return id >= OPT_ADVANCED_ABILITY_GRANT_BASE
            && id < OPT_ADVANCED_ABILITY_GRANT_END;
    }
    static s32 advancedAbilityGrantIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_ADVANCED_ABILITY_GRANT_BASE);
    }

    static bool isMoonAllocationKingdomOption(OptionId id) {
        return id >= OPT_MOON_ALLOCATION_KINGDOM_0
            && id <= OPT_MOON_ALLOCATION_KINGDOM_16;
    }
    static s32 moonAllocationKingdomIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_MOON_ALLOCATION_KINGDOM_0);
    }
    static bool isTripleMoonKingdomOption(OptionId id) {
        return id >= OPT_TRIPLE_MOON_KINGDOM_0
            && id <= OPT_TRIPLE_MOON_KINGDOM_16;
    }
    static s32 tripleMoonKingdomIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_TRIPLE_MOON_KINGDOM_0);
    }

    static bool isAdvancedCapturePoolOption(OptionId id) {
        return id >= OPT_ADVANCED_CAPTURE_POOL_BASE
            && id < OPT_ADVANCED_ABILITY_POOL_BASE;
    }
    static s32 advancedCapturePoolIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_CAPTURE_POOL_BASE);
    }
    static bool isAdvancedAbilityPoolOption(OptionId id) {
        return id >= OPT_ADVANCED_ABILITY_POOL_BASE
            && id < OPT_ADVANCED_STARTING_CAPTURES;
    }
    static s32 advancedAbilityPoolIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_ABILITY_POOL_BASE);
    }

    // Kingdom Moons rows and the multi-select behind each of them. Both
    // ranges are indexed by WorldMeta world id, so no six-kingdom table sits
    // between the menu and Randomizer any more.
    static bool isHintCurrencyOption(OptionId id) {
        return id >= OPT_HINT_CURRENCY_MOONS && id <= OPT_HINT_CURRENCY_PURCHASES;
    }
    static s32 hintCurrencyIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_HINT_CURRENCY_MOONS);
    }

    static bool isKingdomMoonUseOption(OptionId id) {
        return id >= OPT_KINGDOM_MOON_USE_0 && id <= OPT_KINGDOM_MOON_USE_16;
    }
    static s32 kingdomMoonUseIndex(OptionId id) {
        return static_cast<s32>(id) - static_cast<s32>(OPT_KINGDOM_MOON_USE_0);
    }
    static bool isAdvancedKingdomMoonUseOption(OptionId id) {
        return id >= OPT_ADVANCED_KINGDOM_MOON_USE_BASE
            && id < OPT_ADVANCED_KINGDOM_MOON_USE_END;
    }
    // Packed (worldId * 7 + SpecialMoonUse).
    static s32 advancedKingdomMoonUseIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_KINGDOM_MOON_USE_BASE);
    }
    static bool isAdvancedStartKingdomOption(OptionId id) {
        return id >= OPT_ADVANCED_START_KINGDOM_BASE
            && id <= OPT_ADVANCED_START_KINGDOM_16;
    }
    static s32 advancedStartKingdomIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_START_KINGDOM_BASE);
    }
    static bool isAdvancedReqDeviationOption(OptionId id) {
        return id >= OPT_ADVANCED_REQ_DEVIATION_BASE
            && id <= OPT_ADVANCED_REQ_DEVIATION_16;
    }
    static s32 advancedReqDeviationIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_REQ_DEVIATION_BASE);
    }
    static bool isAdvancedTimerValueOption(OptionId id) {
        return id >= OPT_ADVANCED_TIMER_VALUE_BASE
            && id <= OPT_ADVANCED_TIMER_VALUE_5;
    }
    static s32 advancedTimerValueIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_TIMER_VALUE_BASE);
    }
    static bool isAdvancedNuzlockeValueOption(OptionId id) {
        return id >= OPT_ADVANCED_NUZLOCKE_VALUE_BASE
            && id <= OPT_ADVANCED_NUZLOCKE_VALUE_2;
    }
    static s32 advancedNuzlockeValueIndex(OptionId id) {
        return static_cast<s32>(id)
            - static_cast<s32>(OPT_ADVANCED_NUZLOCKE_VALUE_BASE);
    }

    // Which operation the (reused) delete-confirm dialog performs when its
    // "confirm" row is picked, and where it returns to afterward. DeleteSlot
    // is the original/only behavior before user presets existed.
    enum class ConfirmAction : s32
    {
        DeleteSlot,
        ApplyDefaultPreset,
        ApplyUserPreset,
        DeleteUserPreset,
        OverwriteUserPreset,
        RedistributeAllocation,
        RedistributeTripleMoons,
    };

    enum OptKind : u8
    {
        KIND_DISPLAY, // plain text, A does nothing
        KIND_ACTION, // plain text, A triggers an action
        KIND_CHECK, // checkbox, A toggles
        KIND_ROLL, // left/right cycles a value
    };

    struct OptionDef {
        OptionId id;
        OptKind kind;
        const char16_t* label;
        const char16_t* const* rollText; // KIND_ROLL only (matches RollPartsData::mRollMsgList)
        s32 rollCount; // KIND_ROLL only
    };

    struct PageDef {
        const char16_t* title;
        const OptionDef* options;
        s32 count;
        // 2 lays the visible rows out as a grid (see setListColumnMode). A
        // two-column page cannot scroll, so its visible row count must fit
        // twice the list's physical part count.
        s32 columns = 1;
    };

    // Advanced settings use the same MENU_SETTINGS/CommonVerticalList as the
    // ordinary pages. The contents are built into mSubPageRows when a parent
    // row opens one; they are not additional entries in sPages[].
    enum class SubPage : s32
    {
        None = -1,
        MoonAllocationKingdoms,
        TripleMoonKingdoms,
        LockedCapturePool,
        LockedAbilityPool,
        StartingEquipment,
        PurpleShopPrice,
        ReqDeviation,
        MoonRequirementBases,
        // One editor for every kingdom; mSubPageParam is the world id.
        KingdomMoonUses,
        MoonRockKeyScope,
        SubareaScope,
        StartKingdoms,
        HintEntry,
        TimerValues,
        NuzlockeValues,
        MinigameRange,
        SheepCount,
        CaptureGrantKingdoms,
        AbilityGrantKingdoms,
        RequirementKingdoms,
        Count,
    };

    // pageDef() is static today, as are the page-visibility helpers. There is
    // exactly one file-select state in the title scene, so keeping this small
    // navigation state static lets the sub-page remain a real PageDef without
    // changing every existing static caller.
    static SubPage mSubPage;
    static s32 mSubPageParam;
    static s32 mReturnPage;
    static s32 mReturnRow;
    static OptionId mReturnOption;
    static OptionDef mSubPageRows[kMaxPageOptions];
    static PageDef mSubPageDef;

    enum class AdvancedKeyboardField : u8
    {
        None,
        PurpleShopPrice,
        StartingCaptures,
        StartingThrows,
        StartingAbilities,
        MoonRequirementDeviation,
        TimerValue,
        NuzlockeValue,
        HintCustomPrice,
        MinigameLow,
        MinigameHigh,
    };

    static const OptionDef sMainOptions[];
    // Every option the Game Mode Settings page can show, across all run modes.
    // sPages points at this so the user-preset snapshot/apply loops see them all;
    // pageDef() narrows it to the current mode's rows for display.
    static const OptionDef sGameModeOptions[];
    static const OptionDef sBoardModeOptions[];
    static const OptionDef sMedalModeOptions[];
    static const OptionDef sNuzlockeModeOptions[];
    static const OptionDef sTimerModeOptions[];
    static const OptionDef sRandomizationOptions[];
    static const OptionDef sRandomization2Options[];
    static const OptionDef sRandomization3Options[];
    // Per-kingdom moon rewards, two columns.
    static const OptionDef sKingdomMoonsOptions[];
    static const OptionDef sLogicSettingsOptions[];
#if SMOR_FEATURE_CHAOS_ANY
    static const OptionDef sChaoticRandomizationOptions[];
#endif
    static const OptionDef sLockOptions[];
    // The Ability Lock page's continuation: five abilities that gate a
    // traversal verb rather than a move. Its own page because the Lock page is
    // already at 4 rows of a different subject and 9 would overflow the list.
    // The price roll chooses its flat/percentage labels while the page is
    // rebuilt, so one option table is enough for both modes.
    static const OptionDef sPurpleShopOptions[];
    // One table for both currencies: each price row picks its own roll text
    // from the currency its category is set to.
    static const OptionDef sHintSettingsOptions[];
    static const OptionDef sVisualAudioOptions[];
    static const OptionDef sQualityOfLifeOptions[];
    static const OptionDef sTrackerOptions[];
    // Host's Multiplayer page - see isPageVisible() and sMultiplayerHostMode.
    static const OptionDef sMultiplayerOptions[];
    // What a guest sees on that same page: the team picker alone. The room
    // settings beside it on the host's version belong to the host's seed and a
    // guest's copy is never consulted, so showing them would be a lie.
    static const OptionDef sGuestMultiplayerOptions[];
    static const OptionDef sSummaryOptions[];
    // Guest join mode's Summary page - see setGuestJoinMode().
    static const OptionDef sGuestSummaryOptions[];
    static const PageDef sPages[];

    void initSlotListMenu(const al::LayoutInitInfo& initInfo);
    void initSlotActionMenu(const al::LayoutInitInfo& initInfo);
    void initDeleteConfirmMenu(const al::LayoutInitInfo& initInfo);
    void initGenerateProgressMenu(const al::LayoutInitInfo& initInfo);
    void initSettingsMenu(const al::LayoutInitInfo& initInfo);
    void initInfoBox(const al::LayoutInitInfo& initInfo);

    void refreshSlotMeta();
    void rebuildSlotList();
    void rebuildSlotAction();
    void rebuildDeleteConfirm();

    // The slot list rows are vanilla ListDataParts file cards, so they carry
    // five text panes instead of the settings pages' single TxtContent. These
    // bind those panes and drive each card's per-row visual state.
    void bindSlotList();
    void applySlotRowStyles();
    // Builds the card thumbnails once and binds the per-slot array to PicDummy.
    void initSlotThumbnails();

    // ── page helpers ──────────────────────────────────────────────────────────
    static const PageDef& pageDef(s32 pageIndex);
    // A page with no rows under the current settings is skipped by D-pad
    // paging and left out of the "(x/y)" counter, so it never appears as an
    // empty list.
    static bool isPageVisible(s32 pageIndex);
    // The pages a guest may see - identified by their body in sPages[] rather
    // than by index, so the SMOR_FEATURE_CHAOS_ANY page shifting every later
    // index by one cannot silently move the set.
    static bool isGuestPage(s32 pageIndex);
    static s32 visiblePageCount();
    static s32 visiblePageOrdinal(s32 pageIndex);
    static s32 findVisiblePage(s32 pageIndex, s32 step);
    // Switches the shared settings list between the scrolling single-column
    // window and the full-height grid a two-column page needs. Called by
    // loadPage() for every page, so the two never disagree.
    void setListColumnMode(bool twoColumn);
    void loadPage(s32 pageIndex); // full rebuild of MENU_SETTINGS for a page
    void refreshPage(); // light update (labels/checks/rolls)
    void applySettingsRowStyles(); // re-apply check/roll/base panes after list rebuilds
    void setSettingsRollPart(s32 row, const OptionDef& option);
    // A row can be informational even though its table entry is a select: the
    // hint currency rows have nothing to pick while no kingdom hands out Hint
    // Tokens. Every path that reads OptionDef::kind goes through this.
    static OptKind rowKind(const OptionDef& option);
    static const char16_t* subPageTitle(SubPage subPage);
    static bool advancedFor(OptionId id, SubPage* out, s32* param = nullptr);
    // Long advanced sub-pages are paged with L/R instead of scrolling. These
    // return 0 for a sub-page that fits on one screen (and for HintEntry, whose
    // mSubPageParam is the hint category rather than a page index), so the
    // build, label-refresh and input paths all slice the same window.
    static s32 subPageItemCount(SubPage subPage);
    static s32 subPagePageCount(SubPage subPage);
    // The settings menu's title bar. Shared by loadPage() and refreshPage() so
    // the two can never disagree about the page counter again.
    void composeSettingsTitle(s32 pageIndex, sead::WFixedSafeString<0x100>* out);
    void buildSubPage();
    void enterSubPage(SubPage subPage, s32 param = 0);
    void exitSubPage();
    void resetSubPageDefaults(SubPage subPage);
    void updateSubPageRows();
    void updateSubPageBalance();
    void normalizeSubPageTargets();
    bool checkSubPageBalance();
    bool closeSubPage(bool fromPlus);
    void enterPage(s32 pageIndex); // open settings at `pageIndex`
    void changePage(s32 delta); // L/R (and D-pad on single-column pages)
    const OptionDef* currentOption() const;
    void onDecideOption(const OptionDef& opt);
    void rollOption(const OptionDef& opt, s32 delta);
    void showInfoBox(bool visible);
    void updateInfoBox();
    void setInfoBoxText(const char* text);
    // "Sand Kingdom: collect moons to progress, get coins and get an ability."
    // Built per row because the answer is the kingdom's whole reward set, not
    // one value the row could name on its own.
    const char* makeKingdomMoonUseDescription(s32 worldId);
    // Sets the info box from pre-built char16_t lines that may contain button-icon tags
    // (used for the Motion-Free Controls hint), bypassing the ASCII word-wrap path.
    void setInfoBoxIconLines();

    bool optionCheckState(OptionId id) const;
    void setOptionCheckState(OptionId id, bool value);
    void toggleOption(OptionId id);
    static bool optionVisible(OptionId id);
    s32 optionRollIdx(OptionId id) const;
    void setOptionRollIdx(OptionId id, s32 idx);
    const char* optionDescription(OptionId id) const;

    // ── user preset snapshot ─────────────────────────────────────────────────
    // Captures every settings-page option (every page except Main Settings'
    // seed/preset rows and the Summary page) into a tagged (optionId, value)
    // list, and restores one the same way. Reuses the option accessors above
    // so a restored preset goes through the exact same setters (and any
    // GlobalConfig::save() side effects) as a manual UI change.
    void captureCurrentSettingsSnapshot(UserPresets::Entry* out, s32* outCount) const;
    void applySettingsSnapshot(const UserPresets::Entry* entries, s32 count);
    void buildPresetDescription(char* buf, s32 bufSize) const;

    // Setup owner only: publishes the slot that is about to start playing as
    // the room's save, so guests have something to download. Returns false in
    // owner mode if the connection/role or durable commit is unavailable; a
    // false result must keep the local save from starting.
    bool tryUploadRoomSnapshot();

    void startMenu(Menu menu, const al::Nerve* nerve, bool killPrevious = false);
    void startActivateSlot(StartMode mode);
    void returnToSlotList(bool killPrevious = false);
    void handleListInput();
    void activateInput();
    void deactivateInput();
    void deleteSelectedSlot();

    al::MessageSystem* mMsgSystem = nullptr;
    FooterParts* mFooterParts = nullptr;
    GameDataHolder* mGameDataHolder = nullptr;
    InputSeparator* mInput = nullptr;
    Keyboard* mKeyboard = nullptr;

    SimpleLayoutMenu* mMenuList[MENU_COUNT] = {};
    CommonVerticalList* mOptionsList[MENU_COUNT] = {};
    sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>* mMsgList[MENU_COUNT] = {};
    // Extra row text for the slot list's file cards. mMsgList[MENU_SLOT_LIST]
    // stays the "File N" line (TxtNumber); these carry the other four panes.
    // CommonVerticalList keeps the bound pointers, so they must outlive the list.
    sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>* mSlotWorldMsg = nullptr;
    sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>* mSlotShineMsg = nullptr;
    sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>* mSlotDayMsg = nullptr;
    sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>* mSlotBadgeMsg = nullptr;
    FooterParts* mMenuFooters[MENU_COUNT] = {};
    RollPartsData* mSettingsRollParts = nullptr; // reused across pages on MENU_SETTINGS

    // Display row -> index into pageDef(mCurrentPage).options[]. Rebuilt by
    // loadPage() whenever the current page is rebuilt.
    s32 mRowMap[kMaxPageOptions] = {};
    s32 mRowCount = 0;
    // Cheap signature of the visible option set. refreshPage() compares this
    // with the current rules before doing an in-place update.
    s32 mVisibleRowSignatureCount = 0;
    u32 mVisibleRowSignatureXor = 0;
    // Some pages keep the same option ids/count while swapping their labels
    // and roll ranges (for example the hint currency page).
    const OptionDef* mLoadedPageOptions = nullptr;

    // The current option's description is shown above the controls footer so
    // the footer can stay dedicated to button help.
    SimpleLayoutMenu* mInfoBox[kInfoMaxLines] = {};
    bool mInfoBoxVisible = false;
    sead::WFixedSafeString<0x200> mInfoText[kInfoMaxLines];
    char mInfoDescriptionBuf[0x400] = {};
    // Built separately from mInfoDescriptionBuf: the row description is
    // composed first and then possibly reprinted INTO that buffer with the
    // advanced-settings suffix, which cannot read and write the same storage.
    char mRowDescriptionBuf[0x200] = {};

    SimpleLayoutMenu* mCurrentMenu = nullptr;
    CommonVerticalList* mCurrentList = nullptr;
    s32 mCurrentPage = 0; // logical page index 0..kPageCount-1
    // Physical row parts the layout archive actually has, captured before
    // configureScrollableList() clamps the list down to its scrolling window.
    s32 mFullListPartsNum = 0;
    // Rows in the left column of a two-column page; 0 on a single-column one.
    // Doubles as "is this page a grid" for the input and layout paths.
    s32 mCurrentColSize = 0;
    // The X the layout archive gives a row part. A grid page overwrites it on
    // every part it poses, and the list never restores it, so a single-column
    // page has to put it back or it inherits the previous page's columns.
    float mSingleColumnPaneX = 0.0f;

    // The allocation editors fill these when their Phase 4 rows are built.
    // A negative value means that this Phase 3 framework page has no balance
    // yet, so + closes normally until the editor supplies real totals.
    s32 mSubPageBalanceAssigned = -1;
    s32 mSubPageBalanceRequired = -1;
    bool mSubPageBalanceNeedsNormalization = false;
    sead::WFixedSafeString<0x80> mSubPageRowLabels[kMaxPageOptions];
    sead::WFixedSafeString<0x40> mPurpleShopPriceCustomLabel;
    const char16_t* mPurpleShopPriceRollText[7] = {};
    AdvancedKeyboardField mAdvancedKeyboardField = AdvancedKeyboardField::None;

    // Which ListDataParts "State" pose each card shows. Vanilla drives the same
    // three: NoData (empty file - no moon/date line, no pill), Normal (a file
    // with a timestamp) and Play (adds the pill, which carries the seed here).
    enum class SlotRowState : u8
    {
        Empty,
        Damaged,
        Ready
    };

    RandoSaveFile::Header mSlotHeader[kSlotCount] = {};
    RandoSaveFile::Meta mSlotMeta[kSlotCount] = {};
    SlotRowState mSlotRowState[kSlotCount] = {};
    // Drives both the run-mode label on the file-number line and the seed pill's
    // tint. Kept per slot because applySlotRowStyles re-derives the tint from the
    // scrolled part window every frame, long after rebuildSlotList has run.
    Randomizer::RunMode mSlotRunMode[kSlotCount] = {};
    // Card thumbnails. al::createTextureInfo allocates, so one is built per
    // kingdom (plus the "Empty" placeholder) at construction and reused;
    // mSlotThumbnail is the array CommonVerticalList::setImageData keeps a
    // pointer to, indexed by data row, so it must outlive the list.
    nn::ui2d::TextureInfo* mWorldThumbnail[WorldMeta::sWorldCount] = {};
    nn::ui2d::TextureInfo* mEmptyThumbnail = nullptr;
    nn::ui2d::TextureInfo* mSlotThumbnail[kSlotCount] = {};
    bool mHasThumbnails = false;
    bool mSlotCompatible[kSlotCount] = {};
    bool mSlotHasData[kSlotCount] = {};
    bool mSlotLoadFailed[kSlotCount] = {};
    bool mActionEnabled[kMaxMsgCount] = {};
    bool mDeleteEnabled[kMaxMsgCount] = {};
    bool mSettingsEnable[kMaxMsgCount] = {};
    bool mGenerateEnabled[kMaxMsgCount] = {};

    s32 mSelectedSlot = 1;
    SaveRedirect::Category mSaveCategory = SaveRedirect::Category::SinglePlayer;
    SaveRedirect::Category mSaveMode = SaveRedirect::Category::Randomizer;
    s32 mActionCount = 0;
    s32 mGenerateFrame = -1;
    s32 mGenerateDoneStep = -1;
    s32 mPresetIdx = 0;
    s32 mUserPresetIdx = 0;

    // State for the reused delete-confirm dialog: which operation "confirm"
    // performs, and which user-preset slot it targets (Apply/Delete/Overwrite).
    ConfirmAction mConfirmAction = ConfirmAction::DeleteSlot;
    s32 mConfirmPresetIndex = -1;

    // Captured settings + typed name, held between the "Save as Preset"
    // keyboard closing and an overwrite confirmation actually committing it
    // (only used when the typed name collides with an existing preset).
    char16_t mPendingPresetName[UserPresets::kNameLength] = {};
    char mPendingPresetDescription[UserPresets::kDescriptionLength] = {};
    UserPresets::Entry mPendingPresetEntries[UserPresets::kMaxEntries] = {};
    s32 mPendingPresetEntryCount = 0;

    // Guest join mode's own progress bookkeeping, kept apart from the
    // generator's so neither can be left half-set by the other.
    s32 mJoinFrame = -1;
    s32 mJoinDoneStep = -1;
    bool mJoinFailed = false;
    bool mJoinFailureIsUpload = false;
    // Points at a string literal, so it needs no storage and cannot dangle.
    const char16_t* mJoinFailReason = nullptr;

    bool mIsDecide = false;
    bool mIsDone = false;
    bool mGenerateStarted = false;
    bool mGenerateStartFailed = false;
    bool mActivateWriteStarted = false;
    bool mLoadAutoGrantWriteStarted = false;
    // Guest NewGame has written its native save and asked the server to replay
    // the team's durable log into it. Keep the file select alive until the
    // server's completion marker arrives.
    bool mGuestReplayRequested = false;
    StartMode mStartMode = StartMode::None;

    static bool sGuestJoinMode;
    static bool sMultiplayerHostMode;
};

extern StageSceneStateFileSelect* gStageSceneStateFileSelect;

namespace {
    NERVE_IMPL(StageSceneStateFileSelect, SlotList)
    NERVE_IMPL(StageSceneStateFileSelect, SlotAction)
    NERVE_IMPL(StageSceneStateFileSelect, DeleteConfirm)
    NERVE_IMPL(StageSceneStateFileSelect, NewSaveSettings)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardSeed)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardMoonReq)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardMoonReqDeviation)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardSubPageValue)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardAdvancedValue)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardMedalAvailable)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardMedalRequired)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardPresetName)
    NERVE_IMPL(StageSceneStateFileSelect, OpenKeyboardTrackerRoomCode)
    NERVE_IMPL(StageSceneStateFileSelect, Generate)
    NERVE_IMPL(StageSceneStateFileSelect, JoinRoom)
    NERVE_IMPL(StageSceneStateFileSelect, ActivateSlot)

    NERVES_MAKE_STRUCT(StageSceneStateFileSelect,
        SlotList, SlotAction, DeleteConfirm, NewSaveSettings,
        OpenKeyboardSeed, OpenKeyboardMoonReq, OpenKeyboardMoonReqDeviation, OpenKeyboardSubPageValue,
        OpenKeyboardAdvancedValue,
        OpenKeyboardMedalAvailable,
        OpenKeyboardMedalRequired, OpenKeyboardPresetName, OpenKeyboardTrackerRoomCode,
        Generate, JoinRoom, ActivateSlot)
} // namespace
