#include "Scene/StageSceneStateFileSelect.hpp"

#include "Scene/StageSceneStateModConfig.hpp"

#include "Library/Base/StringUtil.h"
#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "../audio/CustomBgmManager.h"
#include "EntranceDB.h"
#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/RoomSnapshot.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"
#include "MusicDB.h"
#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/ArrivalLog.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/GlobalConfig.h"
#include "Randomizer/HintEconomy.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MenuText.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MusicRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/Presets.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SeedGenerator.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/TrackerBridge.h"
#include "Randomizer/UserPresets.h"
#include "Randomizer/WorldMeta.h"
#include "SD/SDHooks.h"
#include "ShineDB.h"
#include "game/Layout/FooterParts.h"
#include "game/Layout/ShopLayoutInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Scene/TitleMenuScene.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/SaveDataAccessFunction.h"
#include "game/Util/StageInputFunction.h"
#include "gfx/seadColor.h"

#include "nn/fs.h"
#include "nn/os.h"
#include "nn/time.h"
#include "nn/util.h"
#include <cstdarg>
#include <cstdlib>
#include <cstring>

StageSceneStateFileSelect* gStageSceneStateFileSelect = nullptr;

bool StageSceneStateFileSelect::sGuestJoinMode = false;
bool StageSceneStateFileSelect::sMultiplayerHostMode = false;
StageSceneStateFileSelect::SubPage StageSceneStateFileSelect::mSubPage
    = StageSceneStateFileSelect::SubPage::None;
s32 StageSceneStateFileSelect::mSubPageParam = 0;
s32 StageSceneStateFileSelect::mReturnPage = 0;
s32 StageSceneStateFileSelect::mReturnRow = 0;
StageSceneStateFileSelect::OptionId StageSceneStateFileSelect::mReturnOption
    = StageSceneStateFileSelect::OPT_NONE;
StageSceneStateFileSelect::OptionDef
    StageSceneStateFileSelect::mSubPageRows[StageSceneStateFileSelect::kMaxPageOptions]
    = {};
StageSceneStateFileSelect::PageDef StageSceneStateFileSelect::mSubPageDef = {};

namespace {
    constexpr s32 kSeedStartingCoins = 100;
    constexpr s32 kOutfitInventoryCapacity = 64;
    constexpr u32 kMaxSeedValue = 99999999u;

    s32 copyOutfitInventory(sead::FixedSafeString<64>* destination,
        const sead::PtrArray<ShopItem::ItemInfo>& source) {
        if (!destination)
            return 0;

        for (s32 i = 0; i < kOutfitInventoryCapacity; i++)
            destination[i].copy("");

        s32 copied = 0;
        for (s32 i = 0; i < source.size() && copied < kOutfitInventoryCapacity; i++) {
            const ShopItem::ItemInfo* item = source.at(i);
            if (!item || item->name[0] == '\0')
                continue;
            destination[copied++].copy(item->name);
        }
        return copied;
    }

    s32 unlockAllOutfits(GameDataFile* file, const GameDataHolder* holder) {
        if (!file || !holder)
            return 0;

        return copyOutfitInventory(file->getClothList(), holder->getClothList())
            + copyOutfitInventory(file->getCapList(), holder->getCapList());
    }

    // Loading Zones is now purely a RULE setting: how doors pair, not how long
    // the chains get. Chain length moved to Seed Openness, so SmallChains and
    // LongChains are no longer offered here - they survive in the enum only as
    // legacy save values and are migrated on load (see RandoSaveFile.cpp).
    //
    // Display order still differs from EntranceRandoMode's stable numeric
    // values (see Randomizer.h), which keep Off/Standard/Decoupled/Limited at
    // their original 0-3 for save-file compatibility. This table maps menu
    // position -> enum value so the menu can show them in a natural order
    // without renumbering the enum.
    const Randomizer::EntranceRandoMode sEntranceModeUiOrder[] = {
        Randomizer::EntranceRandoMode_Off,
        Randomizer::EntranceRandoMode_Standard,
        Randomizer::EntranceRandoMode_Limited,
        Randomizer::EntranceRandoMode_Decoupled,
    };
    constexpr s32 sEntranceModeUiCount = MenuText::ROLL_ENTRANCE_COUNT;

    s32 entranceModeToUiIndex(Randomizer::EntranceRandoMode mode) {
        for (s32 i = 0; i < sEntranceModeUiCount; i++)
            if (sEntranceModeUiOrder[i] == mode)
                return i;
        return 0;
    }

    Randomizer::EntranceRandoMode entranceModeFromUiIndex(s32 idx) {
        if (idx < 0 || idx >= sEntranceModeUiCount)
            return Randomizer::EntranceRandoMode_Off;
        return sEntranceModeUiOrder[idx];
    }

    // Index 0 is No Logic; indices 1.. are LogicDifficulty + 1, so new levels must
    // be appended to keep saved preset indices stable.
    constexpr s32 sLogicRollCount = MenuText::ROLL_LOGIC_COUNT;
    // The three placement axes. Every roll is 5 levels + Random, index 2 is
    // Standard, and the order runs least -> most so the roll reads like a dial.
    constexpr s32 sSeedAxisRollCount = MenuText::ROLL_SEED_AXIS_COUNT;
    // Keep the first four entries stable: user presets serialize the UI index.
    // New modes are deliberately appended rather than inserted after Standard so
    // old presets continue to select their original run modes. The two Nuzlocke
    // variants have been merged into the single "Nuzlocke" entry at index 4; Soft
    // is now the checkbox on the Game Mode Settings page, so a preset written
    // while Soft had its own index 5 selects Timer instead - Soft Nuzlocke itself
    // is still reachable, just one row further in.
    // One list, one place. Each mode contributes its row only when its own
    // feature flag is on, and the label array, the mode array and the count are
    // all generated from it - so an index can never disagree with a label, and
    // disabling a mode closes its gap instead of leaving a row that setRunMode
    // would just bounce back to Standard.
#if SMOR_FEATURE_TOADETTE_MEDALS
#define SMOR_RUN_MODE_ROLL_TOADETTE(X) X(Randomizer::RunMode::ToadetteMedals)
#else
#define SMOR_RUN_MODE_ROLL_TOADETTE(X)
#endif
#if SMOR_FEATURE_BINGO_ASCEND
#define SMOR_RUN_MODE_ROLL_BINGO_ASCEND(X) \
    X(Randomizer::RunMode::Bingo)          \
    X(Randomizer::RunMode::Ascend)
#else
#define SMOR_RUN_MODE_ROLL_BINGO_ASCEND(X)
#endif
#if SMOR_FEATURE_NUZLOCKE_MODES
    // Both Nuzlocke variants share this one row; the Game Mode Settings page's
    // Soft checkbox is what tells them apart.
#define SMOR_RUN_MODE_ROLL_NUZLOCKE(X) X(Randomizer::RunMode::Nuzlocke)
#else
#define SMOR_RUN_MODE_ROLL_NUZLOCKE(X)
#endif
#if SMOR_FEATURE_TIMER_MODE
#define SMOR_RUN_MODE_ROLL_TIMER(X) X(Randomizer::RunMode::Timer)
#else
#define SMOR_RUN_MODE_ROLL_TIMER(X)
#endif

#define SMOR_RUN_MODE_ROLL_LIST(X)     \
    X(Randomizer::RunMode::Standard)   \
    SMOR_RUN_MODE_ROLL_TOADETTE(X)     \
    SMOR_RUN_MODE_ROLL_BINGO_ASCEND(X) \
    SMOR_RUN_MODE_ROLL_NUZLOCKE(X)     \
    SMOR_RUN_MODE_ROLL_TIMER(X)

    constexpr Randomizer::RunMode sRunModeRollModes[] = {
#define SMOR_RUN_MODE_ROLL_MODE(mode) mode,
        SMOR_RUN_MODE_ROLL_LIST(SMOR_RUN_MODE_ROLL_MODE)
#undef SMOR_RUN_MODE_ROLL_MODE
    };
    constexpr s32 sRunModeRollCount = MenuText::ROLL_RUN_MODE_COUNT;
    static_assert(sizeof(sRunModeRollModes) / sizeof(sRunModeRollModes[0])
            == MenuText::ROLL_RUN_MODE_COUNT,
        "Run mode labels and modes must stay in the same order");

    // Row for a run mode, or 0 (Standard) when this build has no row for it -
    // which is also what a save carrying a compiled-out mode should display.
    static s32 runModeRollIdxOf(Randomizer::RunMode mode) {
        for (s32 i = 0; i < sRunModeRollCount; i++) {
            if (sRunModeRollModes[i] == mode)
                return i;
        }
        return 0;
    }
    const char* moonAllocationSummary() {
        if (!Randomizer::isMoonAllocationActive())
            return MenuText::VALUE_OFF;

        static char summary[32];
        nn::util::SNPrintf(summary, sizeof(summary), "%s/%s",
            Randomizer::getMoonAllocationDifficultyName(
                Randomizer::getMoonAllocationDifficulty()),
            Randomizer::getMoonCoinPolicyName(Randomizer::getMoonCoinPolicy()));
        return summary;
    }
    // Both chaos sliders share the first three ChaosSwapMode values. Props
    // expose the fourth, deletion-enabled widening as well. Each half's rows
    // are compiled in with its own feature, so its labels are too.
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    constexpr s32 sNpcSwapModeCount = MenuText::ROLL_NPC_SWAP_MODE_COUNT;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    constexpr s32 sPropSwapModeCount = MenuText::ROLL_PROP_SWAP_MODE_COUNT;
#endif
    // This display order is deliberately not the serialized enum order: Hint
    // Tokens was appended as value 6 to keep old saves stable, but is the first
    // and default Cap Moon choice for new configurations.
    // Subarea Keys. The host row lists all three pools even when the current
    // mode does not fit in one of them: picking an unusable host snaps back to
    // Cap + Moon (Randomizer::clampSubareaUnlockHost), and refreshPage() writes
    // the clamped value back into the row right after the roll.
    // Keep the persisted enum values stable (Max is already 2) while showing
    // the new mode in the natural Off/Standard/Extended/Max order.
    const Randomizer::MusicRandoMode sMusicRandoUiOrder[] = {
        Randomizer::MusicRandoMode::Off,
        Randomizer::MusicRandoMode::On,
        Randomizer::MusicRandoMode::Extended,
        Randomizer::MusicRandoMode::Max,
    };
    constexpr s32 sMusicRandoUiCount = MenuText::ROLL_MUSIC_COUNT;

    s32 musicRandoModeToUiIndex(Randomizer::MusicRandoMode mode) {
        for (s32 i = 0; i < sMusicRandoUiCount; i++)
            if (sMusicRandoUiOrder[i] == mode)
                return i;
        return 0;
    }

    Randomizer::MusicRandoMode musicRandoModeFromUiIndex(s32 index) {
        if (index < 0 || index >= sMusicRandoUiCount)
            return Randomizer::MusicRandoMode::Off;
        return sMusicRandoUiOrder[index];
    }

    // Filled at init from CustomBgmManager so the two custom entries can carry
    // the installed track count.
    const char16_t* sMusicSourceRollText[4]
        = { u"Off", u"Vanilla", u"Custom", u"Vanilla + Custom" };
    // Filled from Presets at init (count is small, capped here).
    const char16_t* sPresetRollText[8] = {};
    // Filled from UserPresets at init and whenever the saved-preset list
    // changes (save/delete/overwrite). Compacted to roll-index order (see
    // UserPresets::nthValidIndex) so gaps left by a delete don't show blank
    // entries. Shows a single disabled "None" entry when there are none yet.
    const char16_t* sUserPresetRollText[UserPresets::kMaxUserPresets] = {};

    // Maps a "User Preset" roll index (0..UserPresets::count()-1) to the
    // actual UserPresets storage slot backing it.
    s32 userPresetSlotForRoll(s32 rollIdx) {
        return UserPresets::nthValidIndex(rollIdx);
    }

    // Advanced sub-pages page their items with L/R instead of scrolling. Eight
    // rows is what actually fits on hardware, and row 0 is always the Reset
    // action, which leaves 7 item rows per page.
    constexpr s32 kSubPageItemsPerPage = 7;

    // Roll entries are "Auto" followed by every value 0..kMoonTargetMaxValue.
    // Both array bounds derive from that one number: sizing them independently
    // let the fill loop run one past the end of the storage array, and that
    // stray format() wrote over whatever followed it.
    constexpr s32 kMoonTargetMaxValue = 657;
    constexpr s32 kMoonTargetValueCount = kMoonTargetMaxValue + 1;
    constexpr s32 kMoonTargetRollCount = kMoonTargetValueCount + 1; // + "Auto"
    sead::WFixedSafeString<8> sMoonTargetRollStorage[kMoonTargetValueCount];
    const char16_t* sMoonTargetRollText[kMoonTargetRollCount] = {};
    bool sMoonTargetRollTextReady = false;

    void ensureMoonTargetRollText() {
        if (sMoonTargetRollTextReady)
            return;
        sMoonTargetRollText[0] = MenuText::VALUE_AUTO;
        for (s32 value = 0; value < kMoonTargetValueCount; value++) {
            sMoonTargetRollStorage[value].format(MenuText::INTEGER_FORMAT, value);
            sMoonTargetRollText[value + 1] = sMoonTargetRollStorage[value].cstr();
        }
        sMoonTargetRollTextReady = true;
    }

    // No "Auto" entry: an unset kingdom rolls to its own vanilla count, so the
    // row always reads as the total that will actually be used. The vanilla
    // count is shown in the row label instead - see buildSubPage().
    const char16_t* const sTripleMoonTargetRollText[] = {
        u"0",
        u"1",
        u"2",
        u"3",
        u"4",
        u"5",
        u"6",
        u"7",
        u"8",
    };

    s32 vanillaTripleMoonCount() {
        s32 count = 0;
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++)
            if (ShineDB::sShineTable[i].flags & ShineDB::ShineFlag_Multi)
                count++;
        return count;
    }

    s32 vanillaTripleMoonCountForKingdom(s32 worldId) {
        s32 count = 0;
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry& entry = ShineDB::sShineTable[i];
            if (entry.worldId == worldId && (entry.flags & ShineDB::ShineFlag_Multi))
                count++;
        }
        return count;
    }

    // The Locked Captures "Critical" preset: the captures a run genuinely
    // cannot be completed without. Keyed by hack name rather than table index
    // so reordering or renaming sCaptures cannot silently change the set.
    constexpr const char* kCriticalCaptureHackNames[] = {
        "Frog",
        "ElectricWire",
        "Wanwan",
        "TRex",
        "WanwanBig",
        "BreedaWanwan",
        "Killer",
        "BossKnuckleHand",
        "Pukupuku",
        "Fastener",
        "Senobi",
        "FireBros",
        "Imomu",
        "Tank",
        "Byugo",
        "PukupukuSnow",
        "Yukimaru",
        "Hosui",
        "CarryMeat",
        "Bubble",
        "Tsukkun",
        "Manhole",
        "Koopa",
        "KaronWing",
        "KillerMagnum",
        "KuriboWing",
        "Kuribo",
        "BazookaElectric",
        "HammerBros",
        "Yoshi",
    };

    bool isCriticalCaptureHackName(const char* hackName) {
        if (!hackName)
            return false;
        for (const char* critical : kCriticalCaptureHackNames) {
            if (std::strcmp(hackName, critical) == 0)
                return true;
        }
        return false;
    }

    // Bits for every real capture, and for the Critical subset. The pool masks
    // are two 64-bit banks but the table is shorter, so comparisons are made
    // against the valid bits only - the unused high bits sit at their reset
    // default of 1 and would otherwise never compare equal.
    void captureLockPresetMasks(u64* validLo, u64* validHi, u64* criticalLo,
        u64* criticalHi) {
        *validLo = 0;
        *validHi = 0;
        *criticalLo = 0;
        *criticalHi = 0;
        const s32 captureCount = CaptureRando::getCaptureCount();
        for (s32 captureId = 0; captureId < captureCount && captureId < 128;
             captureId++) {
            const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(captureId);
            const s32 bit = captureId < 64 ? captureId : captureId - 64;
            u64* valid = captureId < 64 ? validLo : validHi;
            u64* critical = captureId < 64 ? criticalLo : criticalHi;
            *valid |= 1ull << bit;
            if (capture && isCriticalCaptureHackName(capture->hackName))
                *critical |= 1ull << bit;
        }
    }

    // 0 Vanilla, 1 All, 2 Critical, 3 Custom - derived from the live state so
    // the row always reflects what the pool actually holds.
    s32 captureLockPresetIndex() {
        if (!Randomizer::isCaptureRandoEnabled())
            return 0;
        u64 validLo = 0, validHi = 0, criticalLo = 0, criticalHi = 0;
        captureLockPresetMasks(&validLo, &validHi, &criticalLo, &criticalHi);
        const u64 lo = Randomizer::getCaptureLockPoolMask(0) & validLo;
        const u64 hi = Randomizer::getCaptureLockPoolMask(1) & validHi;
        if (lo == validLo && hi == validHi)
            return 1;
        if (lo == criticalLo && hi == criticalHi)
            return 2;
        return 3;
    }

    bool isCaptureLockPoolCustom() { return captureLockPresetIndex() == 3; }

    // Defined with the advanced-page row filter below; used here by
    // setAbilityLockRow() to carry a chain's folded-away steps with it.
    bool isIntermediateAbilityStep(s32 abilityId);

    // The Movement Abilities each carry their own dial as well as a pool bit;
    // an ability is only withheld when both say so. The dedicated Movement
    // Abilities page is gone, so the advanced pool rows drive both together and
    // these two helpers are the single place that mapping lives.
    bool isMovementAbilityDialOn(s32 abilityId) {
        switch (abilityId) {
        case AbilityRando::AbilityId_MoonLowGravity:
            return Randomizer::getMoonGravityMode()
                != Randomizer::MoonGravityMode::Unlocked;
        case AbilityRando::AbilityId_Swim:
            return Randomizer::isSwimLockEnabled();
        case AbilityRando::AbilityId_Grab:
            return Randomizer::isGrabLockEnabled();
        case AbilityRando::AbilityId_JaxiRide:
            return Randomizer::isJaxiLockEnabled();
        case AbilityRando::AbilityId_ScooterRide:
            return Randomizer::isScooterLockEnabled();
        case AbilityRando::AbilityId_RocketFlower:
            return Randomizer::isRocketFlowerLockEnabled();
        case AbilityRando::AbilityId_NpcTalk:
            return Randomizer::isNpcTalkLockEnabled();
        case AbilityRando::AbilityId_Pipe2D:
            return Randomizer::isPipe2DLockEnabled();
        case AbilityRando::AbilityId_TimerChallenge:
            return Randomizer::isTimerChallengeLockEnabled();
        default:
            return true; // not a Movement Ability - the pool bit alone decides
        }
    }

    void setMovementAbilityDial(s32 abilityId, bool on) {
        switch (abilityId) {
        case AbilityRando::AbilityId_MoonLowGravity:
            // Only the Ability Lock mode belongs to the pool; the two moon-count
            // modes withhold low gravity without making it an item, so they are
            // left alone rather than being clobbered by a pool edit.
            if (!on)
                Randomizer::setMoonGravityMode(Randomizer::MoonGravityMode::Unlocked);
            else if (Randomizer::getMoonGravityMode()
                == Randomizer::MoonGravityMode::Unlocked)
                Randomizer::setMoonGravityMode(Randomizer::MoonGravityMode::AbilityLock);
            break;
        case AbilityRando::AbilityId_Swim:
            Randomizer::setSwimLockEnabled(on);
            break;
        case AbilityRando::AbilityId_Grab:
            Randomizer::setGrabLockEnabled(on);
            break;
        case AbilityRando::AbilityId_JaxiRide:
            Randomizer::setJaxiLockEnabled(on);
            break;
        case AbilityRando::AbilityId_ScooterRide:
            Randomizer::setScooterLockEnabled(on);
            break;
        case AbilityRando::AbilityId_RocketFlower:
            Randomizer::setRocketFlowerLockEnabled(on);
            break;
        case AbilityRando::AbilityId_NpcTalk:
            Randomizer::setNpcTalkLockEnabled(on);
            break;
        case AbilityRando::AbilityId_Pipe2D:
            Randomizer::setPipe2DLockEnabled(on);
            break;
        case AbilityRando::AbilityId_TimerChallenge:
            Randomizer::setTimerChallengeLockEnabled(on);
            break;
        default:
            break;
        }
    }

    // A pool row is on when the mask says so AND, for a Movement Ability, its
    // own dial agrees.
    bool isAbilityLockRowOn(s32 abilityId) {
        return Randomizer::isAbilityInLockPool(abilityId)
            && isMovementAbilityDialOn(abilityId);
    }

    void setAbilityLockRow(s32 abilityId, bool on) {
        Randomizer::setAbilityInLockPool(abilityId, on);
        setMovementAbilityDial(abilityId, on);
        // The row is named after the move you end up with, so it carries the
        // folded-away steps below it (Triple Jump takes Double Jump with it).
        // Stops at the first prerequisite that is a row in its own right.
        const AbilityRando::AbilityEntry* walk = AbilityRando::getAbility(abilityId);
        for (s32 depth = 0; walk && walk->prerequisite >= 0
             && depth < AbilityRando::AbilityId_Count;
             depth++) {
            const s32 stepId = walk->prerequisite;
            if (!isIntermediateAbilityStep(stepId))
                break;
            Randomizer::setAbilityInLockPool(stepId, on);
            setMovementAbilityDial(stepId, on);
            walk = AbilityRando::getAbility(stepId);
        }
    }

    // 0 Vanilla, 1 All, 2 Original, 3 Non-Progressive, 4 Custom. "Original"
    // withholds Mario's moves but leaves the traversal verbs alone, which is
    // exactly "everything except the Movement Abilities".
    s32 abilityLockPresetIndex() {
        if (!Randomizer::isLockedAbilitiesEnabled())
            return 0;
        const s32 abilityCount = AbilityRando::getAbilityCount();
        bool allOn = true;
        bool originalShape = true;
        for (s32 abilityId = 0; abilityId < abilityCount; abilityId++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(abilityId);
            if (!ability || ability->alwaysUnlocked)
                continue;
            const bool on = isAbilityLockRowOn(abilityId);
            const bool movement = AbilityRando::isMovementAbility(
                static_cast<AbilityRando::AbilityId>(abilityId));
            if (!on)
                allOn = false;
            if (on == movement)
                originalShape = false;
        }
        const bool progressive = Randomizer::isProgressiveAbilitiesEnabled();
        if (allOn)
            return progressive ? 1 : 3;
        if (originalShape && progressive)
            return 2;
        return 4;
    }

    bool isAbilityLockPoolCustom() { return abilityLockPresetIndex() == 4; }

    void applyAbilityLockPreset(s32 preset) {
        const s32 abilityCount = AbilityRando::getAbilityCount();
        for (s32 abilityId = 0; abilityId < abilityCount; abilityId++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(abilityId);
            if (!ability || ability->alwaysUnlocked)
                continue;
            const bool movement = AbilityRando::isMovementAbility(
                static_cast<AbilityRando::AbilityId>(abilityId));
            setAbilityLockRow(abilityId, preset == 2 ? !movement : true);
        }
        Randomizer::setProgressiveAbilitiesEnabled(preset != 3);
    }

    // An ability that is only a step on the way to another one. The advanced
    // list names each row after the move you end up with, so the intermediate
    // step is folded into its successor's row rather than getting its own.
    bool isIntermediateAbilityStep(s32 abilityId) {
        const s32 abilityCount = AbilityRando::getAbilityCount();
        s32 dependents = 0;
        for (s32 other = 0; other < abilityCount; other++) {
            const AbilityRando::AbilityEntry* entry = AbilityRando::getAbility(other);
            if (entry && entry->prerequisite == abilityId)
                dependents++;
        }
        // Exactly one dependent means a straight chain (Double Jump -> Triple
        // Jump, Roll -> Roll Boost) and the step is folded away. Crouch and
        // Ground Pound feed several branches, so they stay as their own rows.
        return dependents == 1;
    }

    // Rows the Locked Abilities advanced page shows: the stable ability table
    // minus the always-granted moves (which belong to no reward pool) and the
    // folded-away intermediate steps.
    bool isLockedAbilityPoolRow(s32 abilityId) {
        const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(abilityId);
        return ability && !ability->alwaysUnlocked
            && !isIntermediateAbilityStep(abilityId);
    }

    s32 lockedAbilityPoolItemCount() {
        s32 count = 0;
        const s32 abilityCount = AbilityRando::getAbilityCount();
        for (s32 abilityId = 0; abilityId < abilityCount; abilityId++) {
            if (isLockedAbilityPoolRow(abilityId))
                count++;
        }
        // Plus the Progressive Abilities toggle, which is item 0 of the page.
        return count + 1;
    }

    s32 moonAllocationDisplayValue(s32 worldId) {
        MoonRando::KingdomAllocationSummary summary = {};
        if (MoonRando::getKingdomAllocationSummary(worldId, &summary))
            return summary.assignedValue;
        return WorldMeta::getVanillaMoonRequirement(worldId);
    }

    bool moonAllocationUsesRunTotal() {
        switch (Randomizer::getRunMode()) {
        case Randomizer::RunMode::Bingo:
        case Randomizer::RunMode::Ascend:
        case Randomizer::RunMode::ToadetteMedals:
            return false;
        default:
            return true;
        }
    }

    s32 moonAllocationRequiredTotal() {
        if (moonAllocationUsesRunTotal())
            return static_cast<s32>(Randomizer::getMoonRequirement());

        s32 total = 0;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            total += WorldMeta::getVanillaMoonRequirement(worldId);
        return total;
    }

    const char16_t* specialMoonUseText(Randomizer::SpecialMoonUse use) {
        switch (use) {
        case Randomizer::SpecialMoonUse_Coins:
            return u"Coins";
        case Randomizer::SpecialMoonUse_HintTokens:
            return u"Hint Tokens";
        case Randomizer::SpecialMoonUse_Abilities:
            return u"Abilities";
        case Randomizer::SpecialMoonUse_Captures:
            return u"Captures";
        case Randomizer::SpecialMoonUse_AbilitiesOrCaptures:
            return u"Abilities or Captures";
        case Randomizer::SpecialMoonUse_KingdomUnlock:
            return u"Kingdom Unlock";
        case Randomizer::SpecialMoonUse_PaintingUnlock:
            return u"Painting Unlock";
        default:
            return MenuText::EMPTY;
        }
    }

    // The six story kingdoms the whole-set preset editors work over, as world
    // ids. Every kingdom carries a mask now (Kingdom Moons page); these
    // editors stay scoped to the six that the presets are actually about.
    constexpr s32 kSpecialMoonKingdomCount = 6;
    constexpr s32 kSpecialMoonWorldId[kSpecialMoonKingdomCount] = {
        WorldMeta::WorldId_Cap,
        WorldMeta::WorldId_Cloud,
        WorldMeta::WorldId_Moon,
        WorldMeta::WorldId_Dark,
        WorldMeta::WorldId_Darker,
        WorldMeta::WorldId_Mushroom,
    };

    u8 specialMoonUseMaskForKingdom(s32 kingdom) {
        if (kingdom < 0 || kingdom >= kSpecialMoonKingdomCount)
            return Randomizer::getKingdomMoonUseMask(WorldMeta::WorldId_Moon);
        return Randomizer::getKingdomMoonUseMask(kSpecialMoonWorldId[kingdom]);
    }

    void setSpecialMoonUseMaskForKingdom(s32 kingdom, u8 mask) {
        if (kingdom < 0 || kingdom >= kSpecialMoonKingdomCount)
            return;
        Randomizer::setKingdomMoonUseMask(kSpecialMoonWorldId[kingdom], mask);
    }

    const char16_t* specialMoonKingdomName(s32 kingdom) {
        if (kingdom < 0 || kingdom >= kSpecialMoonKingdomCount)
            return MenuText::EMPTY;
        return WorldMeta::getDisplayName16ForWorldId(kSpecialMoonWorldId[kingdom]);
    }

    // Whether a kingdom gates the route. The bit shares its storage with the
    // reward mask but is a separate setting: every reward readout and write has
    // to step around it, or a Progress kingdom that also pays coins reads as
    // "Custom" on the rewards page.
    constexpr u8 kKingdomProgressBit = static_cast<u8>(1u << Randomizer::SpecialMoonUse_KingdomUnlock);

    // Every kingdom on the frontier route can gate progress, and the advanced
    // page is a checkbox per kingdom. World ids 0..13 are exactly that route -
    // Mushroom, Dark Side and Darker Side sit past it, so a requirement there
    // would gate nothing and RequirementRando::includesRequirement() refuses
    // them.
    constexpr s32 kRequirementKingdomCount = 14;
    // The three the named presets turn on and off. The other eleven are on in
    // every preset; only the advanced page can clear one of those.
    constexpr s32 kRequirementPresetKingdoms[] = {
        WorldMeta::WorldId_Cap,
        WorldMeta::WorldId_Cloud,
        WorldMeta::WorldId_Moon,
    };
    constexpr s32 kRequirementPresetKingdomCount = sizeof(kRequirementPresetKingdoms) / sizeof(kRequirementPresetKingdoms[0]);

    bool isRequirementKingdomSelectable(s32 worldId) {
        return worldId >= 0 && worldId < kRequirementKingdomCount;
    }

    bool isRequirementKingdomEnabled(s32 worldId) {
        return isRequirementKingdomSelectable(worldId)
            && Randomizer::isKingdomMoonUseSelected(
                worldId, Randomizer::SpecialMoonUse_KingdomUnlock);
    }

    void setRequirementKingdomEnabled(s32 worldId, bool on) {
        if (!isRequirementKingdomSelectable(worldId))
            return;
        u8 mask = Randomizer::getKingdomMoonUseMask(worldId);
        if (on)
            mask |= static_cast<u8>(1u << Randomizer::SpecialMoonUse_KingdomUnlock);
        else
            mask &= static_cast<u8>(~(1u << Randomizer::SpecialMoonUse_KingdomUnlock));
        // The rewards half of the mask is a separate setting and stays as it is:
        // a kingdom can pay coins whether or not it also gates.
        Randomizer::setKingdomMoonUseMask(worldId, mask);
    }

    void resetRequirementKingdomsToDefault() {
        for (s32 worldId = 0; worldId < kRequirementKingdomCount; worldId++)
            setRequirementKingdomEnabled(worldId,
                (Randomizer::getDefaultKingdomMoonUseMask(worldId)
                    & kKingdomProgressBit)
                    != 0);
    }

    bool isRequirementPresetKingdom(s32 worldId) {
        for (s32 i = 0; i < kRequirementPresetKingdomCount; i++)
            if (kRequirementPresetKingdoms[i] == worldId)
                return true;
        return false;
    }

    // 0 Vanilla, 1 +Cap, 2 +Cloud, 3 +Moon, 4 All, 5 Custom. A named preset
    // means every ordinary route kingdom gates and the optional three match;
    // clearing one of the ordinary eleven is what Custom is for.
    s32 requirementKingdomsPresetIndex() {
        for (s32 worldId = 0; worldId < kRequirementKingdomCount; worldId++) {
            if (!isRequirementPresetKingdom(worldId)
                && !isRequirementKingdomEnabled(worldId))
                return 5;
        }
        s32 count = 0;
        s32 only = -1;
        for (s32 i = 0; i < kRequirementPresetKingdomCount; i++) {
            if (isRequirementKingdomEnabled(kRequirementPresetKingdoms[i])) {
                count++;
                only = i;
            }
        }
        if (count == 0)
            return 0;
        if (count == 1)
            return only + 1;
        if (count == kRequirementPresetKingdomCount)
            return 4;
        return 5;
    }

    void applyRequirementKingdomsPreset(s32 preset) {
        // Every ordinary route kingdom gates under all of the named presets;
        // they differ only in which of the optional three join it.
        for (s32 worldId = 0; worldId < kRequirementKingdomCount; worldId++)
            if (!isRequirementPresetKingdom(worldId))
                setRequirementKingdomEnabled(worldId, true);
        for (s32 i = 0; i < kRequirementPresetKingdomCount; i++) {
            const bool on = preset == 4 || (preset >= 1 && preset <= 3 && i == preset - 1);
            setRequirementKingdomEnabled(kRequirementPresetKingdoms[i], on);
        }
    }

    bool kingdomGrantsSpecialMoonUse(s32 kingdom, Randomizer::SpecialMoonUse use) {
        const u8 mask = specialMoonUseMaskForKingdom(kingdom);
        return (mask & (1u << use)) != 0
            || (mask & (1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures)) != 0;
    }

    void setKingdomGrantsSpecialMoonUse(s32 kingdom,
        Randomizer::SpecialMoonUse use, bool on) {
        u8 mask = specialMoonUseMaskForKingdom(kingdom);
        if (on) {
            mask |= static_cast<u8>(1u << use);
        } else {
            // "Abilities or Captures" covers both, so turning one off has to
            // narrow it to the other rather than leave the combined bit set.
            const Randomizer::SpecialMoonUse other = use == Randomizer::SpecialMoonUse_Captures
                ? Randomizer::SpecialMoonUse_Abilities
                : Randomizer::SpecialMoonUse_Captures;
            if ((mask & (1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures)) != 0) {
                mask &= static_cast<u8>(
                    ~(1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures));
                mask |= static_cast<u8>(1u << other);
            }
            mask &= static_cast<u8>(~(1u << use));
            // A kingdom that grants nothing at all falls back to coins.
            if (mask == 0)
                mask = static_cast<u8>(1u << Randomizer::SpecialMoonUse_Coins);
        }
        setSpecialMoonUseMaskForKingdom(kingdom, mask);
    }

    // The select reads 0 (the named default) when exactly the default set of
    // kingdoms grants this reward, and 1 (Custom) for anything else.
    s32 grantKingdomsPresetIndex(Randomizer::SpecialMoonUse use) {
        for (s32 kingdom = 0; kingdom < kSpecialMoonKingdomCount; kingdom++) {
            const bool expected = use == Randomizer::SpecialMoonUse_Captures
                ? kingdom == 5 // Mushroom
                : (kingdom == 3 || kingdom == 4); // Dark + Darker
            if (kingdomGrantsSpecialMoonUse(kingdom, use) != expected)
                return 1;
        }
        return 0;
    }

    void applyGrantKingdomsPreset(Randomizer::SpecialMoonUse use) {
        for (s32 kingdom = 0; kingdom < kSpecialMoonKingdomCount; kingdom++) {
            const bool expected = use == Randomizer::SpecialMoonUse_Captures
                ? kingdom == 5
                : (kingdom == 3 || kingdom == 4);
            setKingdomGrantsSpecialMoonUse(kingdom, use, expected);
        }
    }

    bool specialMoonUseAllowsPainting(s32 kingdom) {
        return kingdom >= 0 && kingdom < kSpecialMoonKingdomCount
            && kSpecialMoonWorldId[kingdom] == WorldMeta::WorldId_Cloud;
    }

    void copyAsciiToWide(sead::WFixedSafeString<0x80>& output, const char* text) {
        if (!text)
            output.copy(MenuText::EMPTY);
        else
            output.convertFromMultiByteString(text, std::strlen(text));
    }

    void refreshUserPresetRollText() {
        const s32 count = UserPresets::count();
        for (s32 i = 0; i < count && i < UserPresets::kMaxUserPresets; i++)
            sUserPresetRollText[i] = UserPresets::getName(userPresetSlotForRoll(i));
    }

    Randomizer::SpecialMoonUse specialMoonUseFromRollIndex(s32 index, bool cloud,
        bool capMoon = false) {
        if (capMoon) {
            switch (index) {
            case 0:
                return Randomizer::SpecialMoonUse_HintTokens;
            case 1:
                return Randomizer::SpecialMoonUse_Coins;
            case 2:
                return Randomizer::SpecialMoonUse_Abilities;
            case 3:
                return Randomizer::SpecialMoonUse_Captures;
            case 4:
                return Randomizer::SpecialMoonUse_AbilitiesOrCaptures;
            case 5:
            default:
                return Randomizer::SpecialMoonUse_KingdomUnlock;
            }
        }
        switch (index) {
        case 1:
            return Randomizer::SpecialMoonUse_Abilities;
        case 2:
            return Randomizer::SpecialMoonUse_Captures;
        case 3:
            return Randomizer::SpecialMoonUse_AbilitiesOrCaptures;
        case 4:
            return Randomizer::SpecialMoonUse_KingdomUnlock;
        case 5:
            return cloud ? Randomizer::SpecialMoonUse_PaintingUnlock
                         : Randomizer::SpecialMoonUse_HintTokens;
        case 6:
            return Randomizer::SpecialMoonUse_HintTokens;
        case 0:
        default:
            return Randomizer::SpecialMoonUse_Coins;
        }
    }

    s32 specialMoonUseRollIndex(Randomizer::SpecialMoonUse use, bool cloud,
        bool capMoon = false) {
        if (capMoon) {
            switch (use) {
            case Randomizer::SpecialMoonUse_HintTokens:
                return 0;
            case Randomizer::SpecialMoonUse_Coins:
                return 1;
            case Randomizer::SpecialMoonUse_Abilities:
                return 2;
            case Randomizer::SpecialMoonUse_Captures:
                return 3;
            case Randomizer::SpecialMoonUse_AbilitiesOrCaptures:
                return 4;
            case Randomizer::SpecialMoonUse_KingdomUnlock:
            default:
                return 5;
            }
        }
        switch (use) {
        case Randomizer::SpecialMoonUse_Abilities:
            return 1;
        case Randomizer::SpecialMoonUse_Captures:
            return 2;
        case Randomizer::SpecialMoonUse_AbilitiesOrCaptures:
            return 3;
        case Randomizer::SpecialMoonUse_KingdomUnlock:
            return 4;
        case Randomizer::SpecialMoonUse_PaintingUnlock:
            return cloud ? 5 : 0;
        case Randomizer::SpecialMoonUse_HintTokens:
            return cloud ? 6 : 5;
        case Randomizer::SpecialMoonUse_Coins:
        default:
            return 0;
        }
    }

    // Roll order is Easy, Normal, Hard, Harder, Hardest, Insane, Max; the enum
    // keeps Max at the legacy Insanity save value (3), so the orders differ.
    // Subarea Keys reads Per Kingdom before Per Type, which is not the order
    // SubareaUnlockMode is numbered in. The mapping is its own involution.
    constexpr s32 kSubareaUnlockRollOrder[4] = {
        Randomizer::SubareaUnlockMode_Unlocked,
        Randomizer::SubareaUnlockMode_PerKingdom,
        Randomizer::SubareaUnlockMode_PerType,
        Randomizer::SubareaUnlockMode_PerKingdomAndType,
    };
    Randomizer::SubareaUnlockMode subareaUnlockRollToMode(s32 idx) {
        if (idx < 0 || idx > 3)
            idx = 0;
        return static_cast<Randomizer::SubareaUnlockMode>(kSubareaUnlockRollOrder[idx]);
    }
    s32 subareaUnlockModeToRoll(Randomizer::SubareaUnlockMode mode) {
        for (s32 i = 0; i < 4; i++) {
            if (kSubareaUnlockRollOrder[i] == static_cast<s32>(mode))
                return i;
        }
        return 0;
    }

    // Each named difficulty carries its own spread, so the deviation row moves
    // with the select until the player types a number of their own - which is
    // the only thing that makes the select read Custom.
    s32 defaultMoonRequirementDeviation() {
        return RequirementRando::getDefaultDeviation(Randomizer::getMoonDifficulty());
    }

    bool isMoonRequirementDeviationCustom() {
        return Randomizer::getMoonRequirementDeviation()
            != defaultMoonRequirementDeviation()
            || RequirementRando::isRequirementDeviationAuthored();
    }

    Randomizer::MoonDifficulty moonDifficultyFromRollIndex(s32 idx) {
        switch (idx) {
        case 0:
            return Randomizer::MoonDifficulty_Easy;
        case 2:
            return Randomizer::MoonDifficulty_Hard;
        case 3:
            return Randomizer::MoonDifficulty_Harder;
        case 4:
            return Randomizer::MoonDifficulty_Hardest;
        case 5:
            return Randomizer::MoonDifficulty_Insane;
        case 6:
            return Randomizer::MoonDifficulty_Max;
        case 1:
        default:
            return Randomizer::MoonDifficulty_Medium;
        }
    }

    Randomizer::HintPriceCategory hintCategoryAt(s32 index) {
        if (index < 0 || index >= Randomizer::kHintPriceCategoryCount)
            index = 0;
        return static_cast<Randomizer::HintPriceCategory>(index);
    }

    bool hintCategoryUsesTokens(Randomizer::HintPriceCategory category) {
        return Randomizer::getHintCategoryCurrency(category)
            == Randomizer::HintCurrencyMode::HintTokens;
    }

    // A category priced in coins reads the coin bank even in a Hint Token run,
    // so the row cannot go through the active-currency accessors.
    u8 getHintCategoryPrice(Randomizer::HintPriceCategory category) {
        return hintCategoryUsesTokens(category)
            ? Randomizer::getHintTokenPrice(category)
            : Randomizer::getHintCoinPrice(category);
    }
    void setHintCategoryPrice(Randomizer::HintPriceCategory category, u8 option) {
        if (hintCategoryUsesTokens(category))
            Randomizer::setHintTokenPrice(category, option);
        else
            Randomizer::setHintCoinPrice(category, option);
    }

    // Display order for every kingdom's reward select, shared by all 17.
    // KingdomUnlock is deliberately absent: "does this kingdom gate progress"
    // is the Kingdoms with Requirements setting, and its bit rides in the same
    // mask but is never shown or edited here - see kingdomRewardMask().
    constexpr Randomizer::SpecialMoonUse kKingdomMoonUseOrder[] = {
        Randomizer::SpecialMoonUse_Coins,
        Randomizer::SpecialMoonUse_Captures,
        Randomizer::SpecialMoonUse_Abilities,
        Randomizer::SpecialMoonUse_AbilitiesOrCaptures,
        Randomizer::SpecialMoonUse_HintTokens,
        Randomizer::SpecialMoonUse_PaintingUnlock,
    };
    constexpr s32 kKingdomMoonUseOrderCount = sizeof(kKingdomMoonUseOrder) / sizeof(kKingdomMoonUseOrder[0]);

    // Dark and Darker Side are edited together: one row, both masks. Darker
    // keeps its own row in the table (hidden) so presets and the settings code
    // still carry it, and every write to Dark is mirrored onto it.
    bool isKingdomMoonUseMirrored(s32 worldId) {
        return worldId == WorldMeta::WorldId_Dark;
    }
    s32 kingdomMoonUseMirror(s32 worldId) {
        return worldId == WorldMeta::WorldId_Dark ? WorldMeta::WorldId_Darker : -1;
    }
    void writeKingdomMoonUseMask(s32 worldId, u8 mask) {
        Randomizer::setKingdomMoonUseMask(worldId, mask);
        const s32 mirror = kingdomMoonUseMirror(worldId);
        if (mirror >= 0)
            Randomizer::setKingdomMoonUseMask(mirror, mask);
    }

    const char16_t* kingdomMoonUseRowName(s32 worldId) {
        return isKingdomMoonUseMirrored(worldId)
            ? MenuText::KINGDOM_MOONS_DARK_SIDES_LABEL
            : WorldMeta::getDisplayName16ForWorldId(worldId);
    }

    // What a kingdom's moons do, in the words the info box uses.
    const char* specialMoonUseEffectText(Randomizer::SpecialMoonUse use) {
        switch (use) {
        case Randomizer::SpecialMoonUse_KingdomUnlock:
            return "collect moons to progress";
        case Randomizer::SpecialMoonUse_Coins:
            return "get coins";
        case Randomizer::SpecialMoonUse_Captures:
            return "get a capture";
        case Randomizer::SpecialMoonUse_Abilities:
            return "get an ability";
        case Randomizer::SpecialMoonUse_AbilitiesOrCaptures:
            return "get an ability or a capture";
        case Randomizer::SpecialMoonUse_HintTokens:
            return "get Hint Tokens";
        case Randomizer::SpecialMoonUse_PaintingUnlock:
            return "unlock a painting";
        default:
            return "";
        }
    }

    u8 kingdomRewardMask(s32 worldId) {
        return static_cast<u8>(
            Randomizer::getKingdomMoonUseMask(worldId) & ~kKingdomProgressBit);
    }

    // Keeps the kingdom's Progress bit whatever the rewards become.
    void writeKingdomRewardMask(s32 worldId, u8 rewards) {
        const u8 progress = static_cast<u8>(
            Randomizer::getKingdomMoonUseMask(worldId) & kKingdomProgressBit);
        writeKingdomMoonUseMask(worldId,
            static_cast<u8>(progress | (rewards & ~kKingdomProgressBit)));
    }

    // Every kingdom offers the same rewards now.
    const Randomizer::SpecialMoonUse* kingdomMoonUseOrder(s32) {
        return kKingdomMoonUseOrder;
    }

    s32 kingdomMoonUseNamedCount(s32) {
        return MenuText::ROLL_KINGDOM_MOON_USE_COUNT;
    }

    // 0 None, 1 Captures, 2 Abilities, 3 Captures or Abilities, 4 both. The
    // combined bit wins over the pair when a mask somehow holds all three -
    // rolling the row then collapses it to one answer.
    s32 kingdomUnlockRollIndex(s32 worldId) {
        const bool captures = Randomizer::isKingdomMoonUseSelected(
            worldId, Randomizer::SpecialMoonUse_Captures);
        const bool abilities = Randomizer::isKingdomMoonUseSelected(
            worldId, Randomizer::SpecialMoonUse_Abilities);
        if (Randomizer::isKingdomMoonUseSelected(
                worldId, Randomizer::SpecialMoonUse_AbilitiesOrCaptures))
            return 3;
        if (captures && abilities)
            return 4;
        if (abilities)
            return 2;
        if (captures)
            return 1;
        return 0;
    }

    void setKingdomUnlockRollIndex(s32 worldId, s32 idx) {
        constexpr u8 unlockBits = static_cast<u8>(1u << Randomizer::SpecialMoonUse_Captures)
            | static_cast<u8>(1u << Randomizer::SpecialMoonUse_Abilities)
            | static_cast<u8>(1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
        u8 mask = static_cast<u8>(kingdomRewardMask(worldId) & ~unlockBits);
        switch (idx) {
        case 1:
            mask |= static_cast<u8>(1u << Randomizer::SpecialMoonUse_Captures);
            break;
        case 2:
            mask |= static_cast<u8>(1u << Randomizer::SpecialMoonUse_Abilities);
            break;
        case 3:
            mask |= static_cast<u8>(
                1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
            break;
        case 4:
            mask |= static_cast<u8>(1u << Randomizer::SpecialMoonUse_Captures);
            mask |= static_cast<u8>(1u << Randomizer::SpecialMoonUse_Abilities);
            break;
        default:
            break;
        }
        writeKingdomRewardMask(worldId, mask);
    }

    bool isKingdomMoonUseEmpty(s32 worldId) {
        return kingdomRewardMask(worldId) == 0;
    }

    bool isKingdomMoonUseCustom(s32 worldId) {
        const u8 mask = kingdomRewardMask(worldId);
        return mask != 0 && (mask & (mask - 1)) != 0;
    }

    // Custom is the last value, and only exists while it is what the kingdom
    // actually holds - a set of rewards has no single named value.
    s32 kingdomMoonUseRollIndex(s32 worldId) {
        const s32 named = kingdomMoonUseNamedCount(worldId);
        // None and Custom share the slot after the named values - a mask cannot
        // be empty and hold two things at once - with Custom one further on.
        if (isKingdomMoonUseEmpty(worldId))
            return named;
        if (isKingdomMoonUseCustom(worldId))
            return named + 1;
        const Randomizer::SpecialMoonUse* order = kingdomMoonUseOrder(worldId);
        const Randomizer::SpecialMoonUse use = Randomizer::getKingdomMoonUse(worldId);
        for (s32 i = 0; i < named; i++) {
            if (order[i] == use)
                return i;
        }
        return 0;
    }

    void setKingdomMoonUseFromRollIndex(s32 worldId, s32 idx) {
        const s32 named = kingdomMoonUseNamedCount(worldId);
        // Rolling onto Custom means nothing: it is a readout of a set that is
        // only editable on the advanced page.
        if (idx < 0 || idx >= named)
            return;
        const Randomizer::SpecialMoonUse use = kingdomMoonUseOrder(worldId)[idx];
        writeKingdomRewardMask(worldId,
            static_cast<u8>(1u << static_cast<u8>(use)));
    }

    s32 moonDifficultyRollIndex(Randomizer::MoonDifficulty difficulty) {
        switch (difficulty) {
        case Randomizer::MoonDifficulty_Easy:
            return 0;
        case Randomizer::MoonDifficulty_Hard:
            return 2;
        case Randomizer::MoonDifficulty_Harder:
            return 3;
        case Randomizer::MoonDifficulty_Hardest:
            return 4;
        case Randomizer::MoonDifficulty_Insane:
            return 5;
        case Randomizer::MoonDifficulty_Max:
            return 6;
        case Randomizer::MoonDifficulty_Medium:
        default:
            return 1;
        }
    }

    void setRow(sead::WFixedSafeString<0x200>& row, const char* fmt, ...) {
        char buffer[0x200];
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(buffer, sizeof(buffer), fmt, args);
        va_end(args);
        row.convertFromMultiByteString(buffer, strlen(buffer));
    }

    // Greedy word-wrap into '\n'-separated lines no wider than `cols` chars, so
    // long descriptions fill the multi-line info box instead of clipping at the
    // box edge (the layout's TxtOption text box does not auto-wrap).
    constexpr s32 kInfoWrapCols = 42;
    void wrapText(const char* src, char* out, size_t outSize, s32 cols) {
        size_t o = 0;
        s32 lineLen = 0;
        size_t i = 0;
        if (!src)
            src = "";
        while (src[i] && o + 1 < outSize) {
            while (src[i] == ' ')
                i++;
            const size_t wordStart = i;
            while (src[i] && src[i] != ' ')
                i++;
            const s32 wordLen = static_cast<s32>(i - wordStart);
            if (wordLen == 0)
                break;
            if (lineLen > 0 && lineLen + 1 + wordLen > cols) {
                out[o++] = '\n';
                lineLen = 0;
            } else if (lineLen > 0 && o + 1 < outSize) {
                out[o++] = ' ';
                lineLen++;
            }
            for (size_t k = wordStart; k < i && o + 1 < outSize; k++) {
                out[o++] = src[k];
                lineLen++;
            }
        }
        out[o] = '\0';
    }

    u32 makeRandomSeed() {
        // Mix several volatile sources so "Random Seed" is genuinely random and not
        // derived from the wall clock: a persistent LCG state, the system tick, and
        // the previous seed.
        static u32 sSeedState = 0x13572468;
        const u64 tick = nn::os::GetSystemTick().value;
        sSeedState = sSeedState * 1664525u + 1013904223u;
        sSeedState ^= static_cast<u32>(tick) + (static_cast<u32>(tick >> 32) * 2654435761u);
        sSeedState += Randomizer::getSeed() * 2246822519u;
        const u32 seed = sSeedState ^ (sSeedState >> 16);
        // Seed generation and manual input intentionally share the same
        // eight-digit limit. Keep zero out of the random pool while covering
        // the full 1..99,999,999 range.
        return (seed % kMaxSeedValue) + 1;
    }

    constexpr s32 kGenerateStartDelayFrames = 30;

    u32 rebuildForcedPeaceKingdoms(GameDataFile* file) {
        if (!file
            || Randomizer::getWorldPeaceRequirementMode()
                != Randomizer::WorldPeaceRequirementMode_OnKingdomComplete) {
            return 0;
        }

        const KingdomProgress::KingdomProgressList* progress = KingdomProgress::get(file);
        if (!progress)
            return 0;

        u32 mask = 0;
        for (s32 i = 0; i < KingdomProgress::cKingdomCount; i++) {
            const KingdomProgress::KingdomProgressEntry& entry = progress->entries[i];
            if (entry.completeStatus
                && entry.kingdomId >= 0 && entry.kingdomId < 32) {
                mask |= 1u << entry.kingdomId;
            }
        }
        return mask;
    }

    const char* categoryRoot(SaveRedirect::Category category) {
        switch (category) {
        case SaveRedirect::Category::Multiplayer:
            return "MPSaves";
        // Guest join mode downloads the host's save into its own root rather
        // than over the local multiplayer saves - the same mapping every other
        // category->root switch in the mod already uses (SaveOnSDCard.cpp,
        // RandoSaveFile.cpp, BingoBoard.cpp, TrackerBridge.cpp). Missing it
        // here silently sent the guest slot's reads at the SINGLE-PLAYER tree.
        case SaveRedirect::Category::MultiplayerGuest:
            return "MPSaves/_guest";
        default:
            return "Saves";
        }
    }

    al::StringTmp<128> makeRandoPath(s32 slot, SaveRedirect::Category category) {
        return al::StringTmp<128>(
            "sd:/SMO-Randomizer/%s/Slot%d/rando.bin", categoryRoot(category), slot);
    }

    al::StringTmp<128> makeFilePath(s32 slot, SaveRedirect::Category category) {
        return al::StringTmp<128>(
            "sd:/SMO-Randomizer/%s/Slot%d/File1.bin", categoryRoot(category), slot);
    }

    al::StringTmp<128> makeIndexedFilePath(s32 slot, s32 index, SaveRedirect::Category category) {
        return al::StringTmp<128>(
            "sd:/SMO-Randomizer/%s/Slot%d/File%d.bin", categoryRoot(category), slot, index);
    }

    bool tryGetLastPlayTime(s32 slot, SaveRedirect::Category category, nn::time::CalendarTime* out) {
        if (!out)
            return false;

        nn::fs::FileTimeStamp timestamp = {};
        auto path = makeFilePath(slot, category);
        if (nn::fs::GetFileTimeStampForDebug(&timestamp, path.cstr()).IsFailure()) {
            // A generated slot can briefly have rando.bin before its first
            // vanilla save write. Its modification time is the best preview in
            // that narrow window.
            path = makeRandoPath(slot, category);
            if (nn::fs::GetFileTimeStampForDebug(&timestamp, path.cstr()).IsFailure())
                return false;
        }

        // FileTimeStamp stores create/modify/access in that order. Modification
        // time tracks the most recent redirected save and therefore the last
        // time this slot was played; unkBool is the SDK structure's validity
        // flag in this header revision.
        const u64 modified = timestamp.mTime2 ? timestamp.mTime2 : timestamp.mTime1;
        if (!timestamp.unkBool || !modified)
            return false;

        const nn::time::PosixTime posix { modified };
        nn::time::CalendarAdditionalInfo additional = {};
        if (nn::time::ToCalendarTime(out, &additional, posix).IsFailure())
            return false;

        return out->year >= 2000 && out->year <= 3000;
    }

    bool fileExists(const char* path) {
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
            return false;
        nn::fs::CloseFile(handle);
        return true;
    }

    // Byte size, or -1 when the file is not there. Diagnostic only: it is what
    // separates "the download never wrote this" from "it wrote it and the save
    // reader still did not see it".
    s64 fileSizeOrMissing(const char* path) {
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
            return -1;
        s64 size = -1;
        if (nn::fs::GetFileSize(&size, handle).IsFailure())
            size = -1;
        nn::fs::CloseFile(handle);
        return size;
    }

    constexpr u32 kSavePreviewByamlMaxSize = 0x200000;
    // SaveDataFile::writeToStream adds a 12-byte stream header. Keep a little
    // headroom so a full-sized vanilla BYAML (0x200000 bytes) is accepted.
    constexpr u32 kSavePreviewFileMaxSize = kSavePreviewByamlMaxSize + 0x10;
    static u8 sSavePreviewFileBuffer[kSavePreviewFileMaxSize];

    bool tryReadSavePreview(GameDataHolder* holder, s32 slot, RandoSaveFile::Meta* outMeta,
        SaveRedirect::Category category) {
        if (!holder || !outMeta) {
            Randomizer::logPush("FS PREVIEW slot=%d reject holder=%d meta=%d",
                slot, holder ? 1 : 0, outMeta ? 1 : 0);
            return false;
        }

        nn::fs::FileHandle handle;
        const auto path = makeFilePath(slot, category);
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure()) {
            Randomizer::logPush("FS PREVIEW slot=%d open-fail path=%s",
                slot, path.cstr());
            return false;
        }

        long fileSize = 0;
        const bool sizeOk = nn::fs::GetFileSize(&fileSize, handle).IsSuccess()
            && fileSize > (long)sizeof(s32)
            && fileSize <= (long)kSavePreviewFileMaxSize;
        const bool readOk = sizeOk
            && nn::fs::ReadFile(handle, 0, sSavePreviewFileBuffer, (u64)fileSize).IsSuccess();
        nn::fs::CloseFile(handle);
        Randomizer::logPush("FS PREVIEW slot=%d size=%ld sizeOk=%d readOk=%d",
            slot, fileSize, sizeOk, readOk);
        if (!readOk)
            return false;

        // Metadata is already supplied by the validated rando.bin header. The
        // game's save reader cannot deserialize a second GameDataFile in-place
        // here, so validate the compact checksummed progression record directly
        // inside the save BYAML instead.
        const bool valid = KingdomProgress::isValidRecordInBuffer(
            sSavePreviewFileBuffer, static_cast<u32>(fileSize));
        Randomizer::logPush("FS PREVIEW slot=%d curN=%d moons=%d kpValid=%d",
            slot, outMeta->currentWorldId, outMeta->totalMoons, valid);
        return valid;
    }

    void deleteFileIfExists(const char* path) {
        if (fileExists(path))
            nn::fs::DeleteFile(path);
    }

    bool isHeaderCompatible(const RandoSaveFile::Header& header) {
        return header.magic == RandoSaveFile::cMagic
            && RandoSaveFile::isVersionSupported(header.version)
            && header.ldMoonCount == Randomizer::LogicData::sMoonCount
            && header.shineTableSize == ShineDB::sShineTableSize
            && header.entranceLinkCount > 0
            && header.entranceLinkCount <= EntranceDB::sLinkCount;
    }

    s32 clampSelectedSlotIndex(s32 slot) {
        constexpr s32 slotCount = 10;
        if (slot < 1)
            return 0;
        if (slot > slotCount)
            return slotCount - 1;
        return slot - 1;
    }

    s32 clampRoll(s32 idx, s32 count) {
        if (idx < 0)
            return 0;
        if (idx >= count)
            return count - 1;
        return idx;
    }

    void prepareTitleSceneForLoad(al::Scene* scene) {
        if (!scene)
            return;

        auto* titleScene = static_cast<TitleMenuScene*>(scene);
        titleScene->mLoadPercent = 0.0f;
        titleScene->mLoadPercentBuffered = 0.0f;
        // Force HakoniwaStateBootLoadData::exeMenu (0x511498) to cancel + destroy +
        // re-request the world-home resources for THIS file's kingdom. The title
        // already preloaded the default new-game start kingdom's resources; without
        // this flag (read by isCancelLoadWorldResource @ +0x120) the loader keeps
        // those stale resources and the randomized start kingdom never finishes
        // loading, which leaves the boot stuck before the stage activates.
        titleScene->mIsCancelLoadResource = true;
        titleScene->mLoadCompleteCounter = 0;
    }

    void setStandardFooter(FooterParts** footers, s32 menuIndex,
        al::LayoutActor* menu, const al::LayoutInitInfo& initInfo) {
        footers[menuIndex] = new FooterParts(menu, initInfo, MenuText::EMPTY, "TxtGuide", "ParFooter");
        footers[menuIndex]->changeText(StageSceneStateModConfig::makeStandardFooterText());
    }

    void configureList(CommonVerticalList* list) {
        if (!list || !list->mListPartsArr)
            return;

        for (s32 i = 0; i < list->mListPartsArraySize; i++) {
            al::LayoutActor* item = list->mListPartsArr[i];
            if (!item)
                continue;

            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Base);
        }
    }

    // ── slot-list file cards (vanilla ListDataParts rows) ───────────────────
    // The kingdom images the cards show live in this bfres, keyed by the base
    // names in WorldMeta::Entry::saveThumbnail. Same source vanilla uses; see
    // StageSceneStateOption's ctor.
    constexpr const char* kSaveTextureResource = "ObjectData/TextureSaveData";
    constexpr const char* kSaveTextureFile = "TextureSaveData";
    constexpr const char* kEmptyThumbnail = "Empty";

    // "Seed 4294967295" needs a wider pill than vanilla's "Playing...". WinBase
    // and TxtPlay are Right-origin at x = 370, so widening only extends leftward,
    // clear of TxtWorld/TxtShine (which end at x = 226).
    //
    // Resizing is the safe way to restyle these rows: pane size is not animatable
    // by ui2d at all, whereas translate is - every row animation (Wait/Select/
    // Decide/Hide/FadeIn/FadeOut) carries an SRT track, on the parent pane Data.
    constexpr float kSlotBadgeWidth = 220.0f;
    // "File 10 - Toadette Medal Hunt" is longer than the pane vanilla sized for
    // "File 1". The date text only reaches back to about x = 194, so there is
    // room to grow the Left-origin number pane from 324 without colliding.
    constexpr float kSlotNumberWidth = 380.0f;

    // Per-run-mode tint for the seed pill. This lands on the pill's TEXT, not its
    // fill: the fill colour comes from WinBase's material (its four content vertex
    // colours are all white), and al::setPaneVtxColor - the only pane-colour helper
    // the game exposes - writes vertex indices 0 and 2 only, which are a TextBox's
    // two gradient stops but merely the LEFT edge of a window. Colours are chosen
    // to read against the gold pill.
    const sead::Color4u8 kModeColorStandard(255, 255, 255, 255);
    const sead::Color4u8 kModeColorBingo(40, 70, 200, 255);
    const sead::Color4u8 kModeColorAscend(120, 45, 180, 255);
    const sead::Color4u8 kModeColorNuzlocke(170, 20, 20, 255);
    const sead::Color4u8 kModeColorSoftNuzlocke(205, 80, 15, 255);
    const sead::Color4u8 kModeColorTimer(10, 95, 110, 255);
    const sead::Color4u8 kModeColorMedals(175, 25, 120, 255);

    const sead::Color4u8& runModeBadgeColor(Randomizer::RunMode mode) {
        switch (mode) {
        case Randomizer::RunMode::Bingo:
            return kModeColorBingo;
        case Randomizer::RunMode::Ascend:
            return kModeColorAscend;
        case Randomizer::RunMode::Nuzlocke:
            return kModeColorNuzlocke;
        case Randomizer::RunMode::SoftNuzlocke:
            return kModeColorSoftNuzlocke;
        case Randomizer::RunMode::Timer:
            return kModeColorTimer;
        case Randomizer::RunMode::ToadetteMedals:
            return kModeColorMedals;
        case Randomizer::RunMode::Standard:
        default:
            return kModeColorStandard;
        }
    }

    void setPaneVisibleIfExists(al::LayoutActor* menu, const char* pane, bool isVisible) {
        if (!menu || !pane || !al::isExistPane(menu, pane))
            return;

        if (isVisible)
            al::showPane(menu, pane);
        else
            al::hidePane(menu, pane);
    }

    void setMenuContentsVisible(al::LayoutActor* menu, bool isVisible) {
        setPaneVisibleIfExists(menu, "TxtOption", isVisible);
        setPaneVisibleIfExists(menu, "ListArea", isVisible);
        setPaneVisibleIfExists(menu, "ParFooter", isVisible);
        setPaneVisibleIfExists(menu, "ParCursor", isVisible);
        setPaneVisibleIfExists(menu, "ParScroll", isVisible);
    }

    // Show/hide one description line. Only line 0 carries the background box, so
    // PicBG/PicLine are shown for line 0 only (otherwise four boxes would stack).
    // Root-pane helpers are declared in headers but not exported in this build, so
    // use ordinary pane visibility for the known layout groups.
    void setInfoLineVisible(al::LayoutActor* menu, s32 lineIdx, bool isVisible) {
        const bool box = isVisible && lineIdx == 0;
        setPaneVisibleIfExists(menu, "RootPane", isVisible);
        setPaneVisibleIfExists(menu, "MainGroup", isVisible);
        setPaneVisibleIfExists(menu, "TxtOption", isVisible);
        setPaneVisibleIfExists(menu, "PicBG00", box);
        setPaneVisibleIfExists(menu, "PicBG01", box);
        setPaneVisibleIfExists(menu, "PicLine", box);
        setPaneVisibleIfExists(menu, "PicCaptureUse00", false);
        setPaneVisibleIfExists(menu, "PicCaptureUse01", false);
        setPaneVisibleIfExists(menu, "PicIcon", false);
        setPaneVisibleIfExists(menu, "ListArea", false);
        setPaneVisibleIfExists(menu, "ParFooter", false);
        setPaneVisibleIfExists(menu, "ParCursor", false);
        setPaneVisibleIfExists(menu, "ParScroll", false);
    }

    // Info-box geometry. The description is shown as up to kInfoMaxLines stacked
    // single-line layouts sharing one background box (drawn by line 0 only). SMO
    // full-screen layouts use a 1280x720 space (x in [-640, 640], y in [-360, 360]).
    constexpr float kInfoBoxW = 1600.0f; // wider than screen → full-width band
    constexpr float kInfoBoxCenterY = -235.0f; // box center; lowered so it clears the longer list
    constexpr float kInfoBoxH = 115.0f; // covers four compact lines, clears footer
    constexpr float kInfoTextScale = 0.5f;
    constexpr float kInfoTextMargin = 40.0f;
    constexpr float kInfoLineSpacing = 27.0f;
    // Left-anchored TxtOption: trans X is where text STARTS. Anchor just inside the
    // LEFT SCREEN edge (-640), not the box edge (the 1600-wide box overhangs).
    constexpr float kInfoTextX = -640.0f + kInfoTextMargin; // -600

    float infoLineY(s32 lineIdx) {
        const float topY = kInfoBoxCenterY + kInfoBoxH * 0.5f - 28.0f;
        return topY - static_cast<float>(lineIdx) * kInfoLineSpacing;
    }

    // Position one description line (geometry only — visibility is setInfoLineVisible).
    // Only line 0 carries the background box + top border; the others are text-only
    // and sit on top of it.
    void applyInfoBoxLineLayout(al::LayoutActor* menu, s32 lineIdx) {
        if (!menu)
            return;

        if (lineIdx == 0) {
            if (al::isExistPane(menu, "PicBG00")) {
                al::setPaneLocalSize(menu, "PicBG00", { kInfoBoxW, kInfoBoxH });
                al::setPaneLocalTrans(menu, "PicBG00", { 0.0f, kInfoBoxCenterY, 0.0f });
            }
            if (al::isExistPane(menu, "PicBG01")) {
                al::setPaneLocalSize(menu, "PicBG01", { kInfoBoxW, kInfoBoxH });
                al::setPaneLocalTrans(menu, "PicBG01", { 0.0f, kInfoBoxCenterY, 0.0f });
            }
            if (al::isExistPane(menu, "PicLine")) {
                al::setPaneLocalSize(menu, "PicLine", { kInfoBoxW, 4.0f });
                al::setPaneLocalTrans(menu, "PicLine",
                    { 0.0f, kInfoBoxCenterY + kInfoBoxH * 0.5f, 0.0f });
            }
        }
        if (al::isExistPane(menu, "TxtOption")) {
            al::setPaneLocalSize(menu, "TxtOption",
                { (kInfoBoxW - kInfoTextMargin * 2.0f) / kInfoTextScale, 60.0f / kInfoTextScale });
            al::setPaneLocalScale(menu, "TxtOption", { kInfoTextScale, kInfoTextScale });
            al::setPaneLocalTrans(menu, "TxtOption", { kInfoTextX, infoLineY(lineIdx), 0.0f });
        }
    }

    template <s32 MessageCount>
    void bindStringList(CommonVerticalList* list,
        sead::SafeArray<sead::WFixedSafeString<0x200>, MessageCount>* messages,
        s32 count, const bool* enableData = nullptr) {
        if (!list || !messages)
            return;

        list->initDataNoResetSelected(count);
        list->addStringData(messages->mBuffer, "TxtContent");
        if (enableData)
            list->setEnableData(enableData);
    }

} // namespace

// ============================================================================
// Page definitions
// ============================================================================

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sMainOptions[] = {
    { OPT_CURRENT_SEED, KIND_DISPLAY, MenuText::OPT_CURRENT_SEED_LABEL, nullptr, 0 },
    { OPT_RANDOM_SEED, KIND_ACTION, MenuText::OPT_RANDOM_SEED_LABEL, nullptr, 0 },
    { OPT_SET_SEED, KIND_ACTION, MenuText::OPT_SET_SEED_LABEL, nullptr, 0 },
    { OPT_PRESET, KIND_ROLL, MenuText::OPT_PRESET_LABEL, sPresetRollText, 2 },
    { OPT_USER_PRESET, KIND_ROLL, MenuText::OPT_USER_PRESET_LABEL, sUserPresetRollText, 1 },
    { OPT_RUN_MODE, KIND_ROLL, MenuText::OPT_RUN_MODE_LABEL,
        MenuText::ROLL_RUN_MODE, MenuText::ROLL_RUN_MODE_COUNT },
};

// ── Game Mode Settings ──────────────────────────────────────────────────────
// One page, four possible bodies. pageDef() picks the body from the current run
// mode; sGameModeOptions is the union of them all and is what sPages points at,
// so the user-preset snapshot still sees every option regardless of which mode
// happens to be selected when a preset is saved or applied.

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sBoardModeOptions[] = {
    { OPT_BOARD_SCALE, KIND_ROLL, MenuText::OPT_BOARD_SCALE_LABEL,
        MenuText::ROLL_BOARD_SCALE, MenuText::ROLL_BOARD_SCALE_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sMedalModeOptions[] = {
    { OPT_TOADETTE_MEDAL_RATIO, KIND_ROLL, MenuText::OPT_TOADETTE_MEDAL_RATIO_LABEL,
        MenuText::ROLL_TOADETTE_MEDAL_RATIO, MenuText::ROLL_TOADETTE_MEDAL_RATIO_COUNT },
    { OPT_TOADETTE_MEDAL_CUSTOM, KIND_CHECK, MenuText::OPT_TOADETTE_MEDAL_CUSTOM_LABEL, nullptr, 0 },
    { OPT_TOADETTE_MEDAL_AVAILABLE, KIND_ACTION,
        MenuText::OPT_TOADETTE_MEDAL_AVAILABLE_LABEL, nullptr, 0 },
    { OPT_TOADETTE_MEDAL_REQUIRED, KIND_ACTION,
        MenuText::OPT_TOADETTE_MEDAL_REQUIRED_LABEL, nullptr, 0 },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sNuzlockeModeOptions[] = {
    { OPT_SOFT_NUZLOCKE, KIND_CHECK, MenuText::OPT_SOFT_NUZLOCKE_LABEL, nullptr, 0 },
    { OPT_EXTREME_NUZLOCKE, KIND_CHECK, MenuText::OPT_EXTREME_NUZLOCKE_LABEL, nullptr, 0 },
    { OPT_SOFT_NUZLOCKE_LOSS, KIND_ROLL, MenuText::OPT_SOFT_NUZLOCKE_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_LOSS, MenuText::ROLL_SOFT_NUZLOCKE_LOSS_COUNT },
    { OPT_SOFT_NUZLOCKE_ABILITY_LOSS, KIND_ROLL,
        MenuText::OPT_SOFT_NUZLOCKE_ABILITY_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_COUNT },
    { OPT_SOFT_NUZLOCKE_CAPTURE_LOSS, KIND_ROLL,
        MenuText::OPT_SOFT_NUZLOCKE_CAPTURE_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sTimerModeOptions[] = {
    { OPT_TIMER_START, KIND_ROLL, MenuText::OPT_TIMER_START_LABEL,
        MenuText::ROLL_TIMER_START, MenuText::ROLL_TIMER_START_COUNT },
    { OPT_TIMER_MOON_BONUS, KIND_ROLL, MenuText::OPT_TIMER_MOON_BONUS_LABEL,
        MenuText::ROLL_TIMER_MOON_BONUS, MenuText::ROLL_TIMER_MOON_BONUS_COUNT },
    { OPT_TIMER_TRIPLE_BONUS, KIND_ROLL, MenuText::OPT_TIMER_TRIPLE_BONUS_LABEL,
        MenuText::ROLL_TIMER_TRIPLE_BONUS, MenuText::ROLL_TIMER_TRIPLE_BONUS_COUNT },
    { OPT_TIMER_DAMAGE_PENALTY, KIND_ROLL, MenuText::OPT_TIMER_DAMAGE_PENALTY_LABEL,
        MenuText::ROLL_TIMER_DAMAGE_PENALTY, MenuText::ROLL_TIMER_DAMAGE_PENALTY_COUNT },
    { OPT_TIMER_DEATH_PENALTY, KIND_ROLL, MenuText::OPT_TIMER_DEATH_PENALTY_LABEL,
        MenuText::ROLL_TIMER_DEATH_PENALTY, MenuText::ROLL_TIMER_DEATH_PENALTY_COUNT },
    { OPT_TIMER_HALF_LIFE, KIND_ROLL, MenuText::OPT_TIMER_HALF_LIFE_LABEL,
        MenuText::ROLL_TIMER_HALF_LIFE, MenuText::ROLL_TIMER_HALF_LIFE_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sGameModeOptions[] = {
    { OPT_BOARD_SCALE, KIND_ROLL, MenuText::OPT_BOARD_SCALE_LABEL,
        MenuText::ROLL_BOARD_SCALE, MenuText::ROLL_BOARD_SCALE_COUNT },
    { OPT_TOADETTE_MEDAL_RATIO, KIND_ROLL, MenuText::OPT_TOADETTE_MEDAL_RATIO_LABEL,
        MenuText::ROLL_TOADETTE_MEDAL_RATIO, MenuText::ROLL_TOADETTE_MEDAL_RATIO_COUNT },
    { OPT_TOADETTE_MEDAL_CUSTOM, KIND_CHECK, MenuText::OPT_TOADETTE_MEDAL_CUSTOM_LABEL, nullptr, 0 },
    { OPT_TOADETTE_MEDAL_AVAILABLE, KIND_ACTION,
        MenuText::OPT_TOADETTE_MEDAL_AVAILABLE_LABEL, nullptr, 0 },
    { OPT_TOADETTE_MEDAL_REQUIRED, KIND_ACTION,
        MenuText::OPT_TOADETTE_MEDAL_REQUIRED_LABEL, nullptr, 0 },
    { OPT_SOFT_NUZLOCKE, KIND_CHECK, MenuText::OPT_SOFT_NUZLOCKE_LABEL, nullptr, 0 },
    { OPT_EXTREME_NUZLOCKE, KIND_CHECK, MenuText::OPT_EXTREME_NUZLOCKE_LABEL, nullptr, 0 },
    { OPT_SOFT_NUZLOCKE_LOSS, KIND_ROLL, MenuText::OPT_SOFT_NUZLOCKE_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_LOSS, MenuText::ROLL_SOFT_NUZLOCKE_LOSS_COUNT },
    { OPT_SOFT_NUZLOCKE_ABILITY_LOSS, KIND_ROLL,
        MenuText::OPT_SOFT_NUZLOCKE_ABILITY_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_COUNT },
    { OPT_SOFT_NUZLOCKE_CAPTURE_LOSS, KIND_ROLL,
        MenuText::OPT_SOFT_NUZLOCKE_CAPTURE_LOSS_LABEL,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS,
        MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_COUNT },
    { OPT_TIMER_START, KIND_ROLL, MenuText::OPT_TIMER_START_LABEL,
        MenuText::ROLL_TIMER_START, MenuText::ROLL_TIMER_START_COUNT },
    { OPT_TIMER_MOON_BONUS, KIND_ROLL, MenuText::OPT_TIMER_MOON_BONUS_LABEL,
        MenuText::ROLL_TIMER_MOON_BONUS, MenuText::ROLL_TIMER_MOON_BONUS_COUNT },
    { OPT_TIMER_TRIPLE_BONUS, KIND_ROLL, MenuText::OPT_TIMER_TRIPLE_BONUS_LABEL,
        MenuText::ROLL_TIMER_TRIPLE_BONUS, MenuText::ROLL_TIMER_TRIPLE_BONUS_COUNT },
    { OPT_TIMER_DAMAGE_PENALTY, KIND_ROLL, MenuText::OPT_TIMER_DAMAGE_PENALTY_LABEL,
        MenuText::ROLL_TIMER_DAMAGE_PENALTY, MenuText::ROLL_TIMER_DAMAGE_PENALTY_COUNT },
    { OPT_TIMER_DEATH_PENALTY, KIND_ROLL, MenuText::OPT_TIMER_DEATH_PENALTY_LABEL,
        MenuText::ROLL_TIMER_DEATH_PENALTY, MenuText::ROLL_TIMER_DEATH_PENALTY_COUNT },
    { OPT_TIMER_HALF_LIFE, KIND_ROLL, MenuText::OPT_TIMER_HALF_LIFE_LABEL,
        MenuText::ROLL_TIMER_HALF_LIFE, MenuText::ROLL_TIMER_HALF_LIFE_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sRandomizationOptions[] = {
    { OPT_MOONS, KIND_CHECK, MenuText::OPT_MOONS_LABEL, nullptr, 0 },
    { OPT_PAINTINGS, KIND_CHECK, MenuText::OPT_PAINTINGS_LABEL, nullptr, 0 },
    { OPT_KINGDOM_ORDER, KIND_CHECK, MenuText::OPT_KINGDOM_ORDER_LABEL, nullptr, 0 },
    { OPT_START_KINGDOM_STATE, KIND_CHECK, MenuText::OPT_START_KINGDOM_STATE_LABEL, nullptr, 0 },
    { OPT_CHEST_ORDER, KIND_CHECK, MenuText::OPT_CHEST_ORDER_LABEL, nullptr, 0 },
    { OPT_COIN_RANDO, KIND_CHECK, MenuText::OPT_COIN_RANDO_LABEL, nullptr, 0 },
    { OPT_SHOP_PRICE_RANDO, KIND_CHECK, MenuText::OPT_SHOP_PRICE_RANDO_LABEL, nullptr, 0 },
    { OPT_SPHINX, KIND_CHECK, MenuText::OPT_SPHINX_LABEL, nullptr, 0 },
};

// Where the player is put and where the doors go. Split off the page above so
// Shop Locations sits directly under the Loading Zones roll it modifies.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sRandomization2Options[] = {
    { OPT_RANDOMIZED_START_POSITION, KIND_ROLL,
        MenuText::OPT_RANDOMIZED_START_POSITION_LABEL,
        MenuText::ROLL_START_POSITION, MenuText::ROLL_START_POSITION_COUNT },
    { OPT_LOADING_ZONES, KIND_ROLL, MenuText::OPT_LOADING_ZONES_LABEL,
        MenuText::ROLL_ENTRANCE, MenuText::ROLL_ENTRANCE_COUNT },
    { OPT_SHOP_LOCATIONS, KIND_ROLL, MenuText::OPT_SHOP_LOCATIONS_LABEL,
        MenuText::ROLL_SHOP_LOCATION, MenuText::ROLL_SHOP_LOCATION_COUNT },
    // Reachable again after the rework: this is a live generation setting that
    // had no row at all.
    { OPT_VISITED_KINGDOMS_MAP, KIND_ROLL,
        MenuText::OPT_VISITED_KINGDOMS_MAP_LABEL,
        MenuText::ROLL_VISITED_KINGDOMS_MAP,
        MenuText::ROLL_VISITED_KINGDOMS_MAP_COUNT },
    // The two "find an item before this opens" systems. Both carry their scope
    // (and, for sub-areas, which kingdoms host the keys) on a Y advanced page.
    { OPT_MOON_ROCK_KEY, KIND_CHECK, MenuText::OPT_MOON_ROCK_KEY_LABEL, nullptr, 0 },
    // Superseded by the checkbox above plus the scope sub-page: as a pair of
    // checkboxes these could both be off, which is not a state the generator
    // has. Retained hidden so presets carrying them still apply.
    { OPT_MOON_ROCK_KEY_ANY_MOON, KIND_CHECK, MenuText::OPT_MOON_ROCK_KEY_LABEL, nullptr, 0 },
    { OPT_MOON_ROCK_KEY_OWN_KINGDOM, KIND_CHECK, MenuText::OPT_MOON_ROCK_KEY_LABEL, nullptr, 0 },
#if SMOR_FEATURE_SUBAREA_KEYS
    { OPT_SUBAREA_UNLOCKS, KIND_ROLL, MenuText::OPT_SUBAREA_UNLOCKS_LABEL,
        MenuText::ROLL_SUBAREA_UNLOCKS, MenuText::ROLL_SUBAREA_UNLOCKS_COUNT },
    // Edited on the Subarea Keys advanced page; kept here for preset capture.
    { OPT_SUBAREA_UNLOCK_HOST, KIND_ROLL, MenuText::OPT_SUBAREA_UNLOCK_HOST_LABEL,
        MenuText::ROLL_SUBAREA_UNLOCK_HOST, MenuText::ROLL_SUBAREA_UNLOCK_HOST_COUNT },
    { OPT_SUBAREA_SCOPE_ANY_MOON, KIND_CHECK, MenuText::OPT_SUBAREA_UNLOCKS_LABEL, nullptr, 0 },
    { OPT_SUBAREA_SCOPE_OWN_KINGDOM, KIND_CHECK, MenuText::OPT_SUBAREA_UNLOCKS_LABEL, nullptr, 0 },
#endif
};

// How many moons each kingdom holds and how many of them a kingdom needs.
// The two legacy allocation booleans and the moon-coin policy stay in the
// table (hidden by optionVisible) so user presets keep carrying them; the
// policy is edited on the Moon Allocation advanced page.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sRandomization3Options[] = {
    { OPT_MOON_ALLOCATION_MODE, KIND_ROLL,
        MenuText::OPT_MOON_ALLOCATION_MODE_LABEL,
        MenuText::ROLL_MOON_ALLOCATION_MODE,
        MenuText::ROLL_MOON_ALLOCATION_MODE_COUNT },
    // Retained in the union table so user-preset snapshots continue to carry
    // the two legacy booleans. optionVisible() hides them from the page.
    { OPT_ENABLE_MOON_REALLOCATION, KIND_CHECK,
        MenuText::OPT_ENABLE_MOON_REALLOCATION_LABEL, nullptr, 0 },
    { OPT_RANDOMIZE_MOON_ALLOCATION, KIND_CHECK,
        MenuText::OPT_RANDOMIZE_MOON_ALLOCATION_LABEL, nullptr, 0 },
    { OPT_MOON_ALLOCATION_DIFFICULTY, KIND_ROLL,
        MenuText::OPT_MOON_ALLOCATION_DIFFICULTY_LABEL,
        MenuText::ROLL_MOON_ALLOCATION_DIFFICULTY,
        MenuText::ROLL_MOON_ALLOCATION_DIFFICULTY_COUNT },
    { OPT_MOON_COIN_POLICY, KIND_ROLL, MenuText::OPT_MOON_COIN_POLICY_LABEL,
        MenuText::ROLL_MOON_COIN_POLICY, MenuText::ROLL_MOON_COIN_POLICY_COUNT },
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    { OPT_TRIPLE_MOONS, KIND_ROLL, MenuText::OPT_TRIPLE_MOONS_LABEL,
        MenuText::ROLL_TRIPLE_MOONS, MenuText::ROLL_TRIPLE_MOONS_COUNT },
#endif
    // Which kingdoms gate progress at all, then how many moons they ask for
    // between them. Y on either row opens the per-kingdom editor behind it.
    { OPT_REQUIREMENT_KINGDOMS, KIND_ROLL,
        MenuText::OPT_REQUIREMENT_KINGDOMS_LABEL,
        MenuText::ROLL_REQUIREMENT_KINGDOMS,
        MenuText::ROLL_REQUIREMENT_KINGDOMS_COUNT },
    { OPT_MOON_REQUIREMENT, KIND_ACTION, MenuText::OPT_MOON_REQUIREMENT_LABEL, nullptr, 0 },
    // Y on this row opens the numeric deviation keyboard; a deviation other
    // than the default reads back as "Custom".
    { OPT_MOON_REQ_DIFFICULTY, KIND_ROLL, MenuText::OPT_MOON_REQ_DIFFICULTY_LABEL,
        MenuText::ROLL_MOON_REQ_DIFFICULTY, MenuText::ROLL_MOON_REQ_DIFFICULTY_COUNT },
    { OPT_MOON_REQ_DEVIATION, KIND_ACTION, MenuText::OPT_MOON_REQ_DIFFICULTY_LABEL,
        nullptr, 0 },
    { OPT_WORLD_PEACE_REQ, KIND_ROLL, MenuText::OPT_WORLD_PEACE_REQ_LABEL,
        MenuText::ROLL_WORLD_PEACE_REQ, MenuText::ROLL_WORLD_PEACE_REQ_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sLogicSettingsOptions[] = {
    { OPT_PLACEMENT_LOGIC, KIND_ROLL, MenuText::OPT_PLACEMENT_LOGIC_LABEL,
        MenuText::ROLL_LOGIC, MenuText::ROLL_LOGIC_COUNT },
    { OPT_UNLOCK_PACING, KIND_ROLL, MenuText::OPT_UNLOCK_PACING_LABEL,
        MenuText::ROLL_UNLOCK_PACING, sSeedAxisRollCount },
    { OPT_SEED_OPENNESS, KIND_ROLL, MenuText::OPT_SEED_OPENNESS_LABEL,
        MenuText::ROLL_SEED_OPENNESS, sSeedAxisRollCount },
    { OPT_SEED_DIFFICULTY, KIND_ROLL, MenuText::OPT_SEED_DIFFICULTY_LABEL,
        MenuText::ROLL_SEED_DIFFICULTY, sSeedAxisRollCount },
};

#if SMOR_FEATURE_CHAOS_ANY
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sChaoticRandomizationOptions[] = {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    { OPT_ENEMY_SWAP, KIND_CHECK, MenuText::OPT_ENEMY_SWAP_LABEL, nullptr, 0 },
    { OPT_NPC_SWAP, KIND_ROLL, MenuText::OPT_NPC_SWAP_LABEL,
        MenuText::ROLL_NPC_SWAP_MODE, MenuText::ROLL_NPC_SWAP_MODE_COUNT },
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    { OPT_PROP_SWAP, KIND_ROLL, MenuText::OPT_PROP_SWAP_LABEL,
        MenuText::ROLL_PROP_SWAP_MODE, MenuText::ROLL_PROP_SWAP_MODE_COUNT },
    { OPT_PROP_SWAP_RATE, KIND_ROLL, MenuText::OPT_PROP_SWAP_RATE_LABEL,
        MenuText::ROLL_PROP_SWAP_RATE, MenuText::ROLL_PROP_SWAP_RATE_COUNT },
#endif
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    { OPT_TWO_D_SWAP, KIND_CHECK, MenuText::OPT_TWO_D_SWAP_LABEL, nullptr, 0 },
#endif
};
#endif

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sLockOptions[] = {
    { OPT_LOCKED_CAPTURES, KIND_ROLL, MenuText::OPT_LOCKED_CAPTURES_LABEL,
        MenuText::ROLL_LOCKED_CAPTURES, MenuText::ROLL_LOCKED_CAPTURES_COUNT },
    { OPT_LOCKED_ABILITIES, KIND_ROLL, MenuText::OPT_LOCKED_ABILITIES_LABEL,
        MenuText::ROLL_LOCKED_ABILITIES, MenuText::ROLL_LOCKED_ABILITIES_COUNT },
    // Moon gravity came off the retired Movement Abilities page. It stays a
    // roll rather than a pool checkbox because it has two non-pool ways to
    // arrive as well; see Randomizer::MoonGravityMode.
    { OPT_MOON_GRAVITY, KIND_ROLL, MenuText::OPT_MOON_GRAVITY_LABEL,
        MenuText::ROLL_MOON_GRAVITY, MenuText::ROLL_MOON_GRAVITY_COUNT },
    { OPT_STARTING_EQUIPMENT, KIND_ROLL, MenuText::OPT_STARTING_EQUIPMENT_LABEL,
        MenuText::ROLL_STARTING_EQUIPMENT, MenuText::ROLL_STARTING_EQUIPMENT_COUNT },
    { OPT_CAPTURE_DIFFICULTY, KIND_ROLL, MenuText::OPT_CAPTURE_DIFFICULTY_LABEL,
        MenuText::ROLL_CAPTURE_DIFFICULTY, MenuText::ROLL_CAPTURE_DIFFICULTY_COUNT },
    // The rest of the retired Movement Abilities page. Each one is edited
    // through its row on the Locked Abilities advanced page now (which drives
    // the dial and the pool bit together), but they stay in this table so user
    // preset snapshots keep carrying them - same reason the two legacy
    // moon-allocation booleans are retained. optionVisible() hides them.
    { OPT_SWIM_LOCK, KIND_CHECK, MenuText::OPT_SWIM_LOCK_LABEL, nullptr, 0 },
    { OPT_GRAB_LOCK, KIND_CHECK, MenuText::OPT_GRAB_LOCK_LABEL, nullptr, 0 },
    { OPT_JAXI_LOCK, KIND_CHECK, MenuText::OPT_JAXI_LOCK_LABEL, nullptr, 0 },
    { OPT_SCOOTER_LOCK, KIND_CHECK, MenuText::OPT_SCOOTER_LOCK_LABEL, nullptr, 0 },
    { OPT_ROCKET_FLOWER_LOCK, KIND_CHECK,
        MenuText::OPT_ROCKET_FLOWER_LOCK_LABEL, nullptr, 0 },
    { OPT_NPC_TALK_LOCK, KIND_CHECK, MenuText::OPT_NPC_TALK_LOCK_LABEL, nullptr, 0 },
    { OPT_PIPE_2D_LOCK, KIND_CHECK, MenuText::OPT_PIPE_2D_LOCK_LABEL, nullptr, 0 },
    { OPT_TIMER_CHALLENGE_LOCK, KIND_CHECK,
        MenuText::OPT_TIMER_CHALLENGE_LOCK_LABEL, nullptr, 0 },
};

// What the purple-coin shop sells and what it charges. Split off the Lock page
// it grew on: that page was already at the 8-row ceiling, and the price rows
// below are their own subject.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sPurpleShopOptions[] = {
    { OPT_PURPLE_SHOP_ABILITIES, KIND_ROLL,
        MenuText::OPT_PURPLE_SHOP_ABILITIES_LABEL,
        MenuText::ROLL_PURPLE_SHOP_OFFERS, MenuText::ROLL_PURPLE_SHOP_OFFERS_COUNT },
    { OPT_PURPLE_SHOP_CAPTURES, KIND_ROLL,
        MenuText::OPT_PURPLE_SHOP_CAPTURES_LABEL,
        MenuText::ROLL_PURPLE_SHOP_OFFERS, MenuText::ROLL_PURPLE_SHOP_OFFERS_COUNT },
    { OPT_PURPLE_SHOP_MOONS, KIND_CHECK, MenuText::OPT_PURPLE_SHOP_MOONS_LABEL, nullptr, 0 },
    { OPT_PURPLE_SHOP_PRICE, KIND_ROLL, MenuText::OPT_PURPLE_SHOP_PRICE_LABEL,
        MenuText::ROLL_PURPLE_SHOP_PRICE, MenuText::ROLL_PURPLE_SHOP_PRICE_COUNT },
    { OPT_PROPORTIONAL_KINGDOM_PRICING, KIND_CHECK,
        MenuText::OPT_PROPORTIONAL_KINGDOM_PRICING_LABEL,
        nullptr, 0 },
};

// What every kingdom's moons hand out, one select per kingdom, laid out as two
// columns of nine. Cloud Kingdom is the only one that offers Painting Unlock,
// so setSettingsRollPart() swaps its roll text; the Custom value appears on any
// row whose kingdom has more than one reward selected, and is only editable on
// that row's advanced page.
//
// Darker Side has no row of its own: it is edited by the Dark Side row and
// kept in the table (hidden) so presets and the settings code still carry it.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sKingdomMoonsOptions[] = {
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 0), KIND_ROLL,
        WorldMeta::sWorlds[0].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 1), KIND_ROLL,
        WorldMeta::sWorlds[1].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 2), KIND_ROLL,
        WorldMeta::sWorlds[2].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 3), KIND_ROLL,
        WorldMeta::sWorlds[3].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 4), KIND_ROLL,
        WorldMeta::sWorlds[4].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 5), KIND_ROLL,
        WorldMeta::sWorlds[5].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 6), KIND_ROLL,
        WorldMeta::sWorlds[6].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 7), KIND_ROLL,
        WorldMeta::sWorlds[7].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 8), KIND_ROLL,
        WorldMeta::sWorlds[8].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 9), KIND_ROLL,
        WorldMeta::sWorlds[9].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 10), KIND_ROLL,
        WorldMeta::sWorlds[10].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 11), KIND_ROLL,
        WorldMeta::sWorlds[11].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 12), KIND_ROLL,
        WorldMeta::sWorlds[12].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 13), KIND_ROLL,
        WorldMeta::sWorlds[13].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 14), KIND_ROLL,
        WorldMeta::sWorlds[14].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 15), KIND_ROLL,
        MenuText::KINGDOM_MOONS_DARK_SIDES_LABEL,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
    { static_cast<OptionId>(OPT_KINGDOM_MOON_USE_0 + 16), KIND_ROLL,
        WorldMeta::sWorlds[16].displayName16,
        MenuText::ROLL_KINGDOM_MOON_USE, MenuText::ROLL_KINGDOM_MOON_USE_COUNT },
};

// Two columns of six. Rows 0-5 are the left column - one currency select per
// hint category, then the coin price ladder - and rows 6-11 the right, holding
// each category's price and the token ladder. The price rows read their roll
// text from the currency the matching left-hand row is set to, which is why
// there is one table now instead of a coin one and a token one.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sHintSettingsOptions[] = {
    { OPT_HINT_CURRENCY_MOONS, KIND_ROLL, MenuText::OPT_HINT_CURRENCY_MOONS_LABEL,
        MenuText::ROLL_HINT_CURRENCY, MenuText::ROLL_HINT_CURRENCY_COUNT },
    { OPT_HINT_CURRENCY_CAPTURES, KIND_ROLL, MenuText::OPT_HINT_CURRENCY_CAPTURES_LABEL,
        MenuText::ROLL_HINT_CURRENCY, MenuText::ROLL_HINT_CURRENCY_COUNT },
    { OPT_HINT_CURRENCY_ABILITIES, KIND_ROLL, MenuText::OPT_HINT_CURRENCY_ABILITIES_LABEL,
        MenuText::ROLL_HINT_CURRENCY, MenuText::ROLL_HINT_CURRENCY_COUNT },
    { OPT_HINT_CURRENCY_PATH, KIND_ROLL, MenuText::OPT_HINT_CURRENCY_PATH_LABEL,
        MenuText::ROLL_HINT_CURRENCY, MenuText::ROLL_HINT_CURRENCY_COUNT },
    { OPT_HINT_CURRENCY_PURCHASES, KIND_ROLL, MenuText::OPT_HINT_CURRENCY_PURCHASES_LABEL,
        MenuText::ROLL_HINT_CURRENCY, MenuText::ROLL_HINT_CURRENCY_COUNT },
    { OPT_HINT_COIN_PROGRESSIVE, KIND_ROLL,
        MenuText::OPT_HINT_COIN_PROGRESSIVE_LABEL,
        MenuText::ROLL_HINT_COIN_PROGRESSIVE_PRICE,
        MenuText::ROLL_HINT_COIN_PROGRESSIVE_PRICE_COUNT },
    { OPT_HINT_COIN_PROGRESSIVE_GLOBAL, KIND_CHECK,
        MenuText::OPT_HINT_COIN_PROGRESSIVE_GLOBAL_LABEL, nullptr, 0 },

    { OPT_HINT_PRICE_MOONS, KIND_ROLL, MenuText::OPT_HINT_PRICE_MOONS_LABEL,
        MenuText::ROLL_HINT_COIN_PRICE, MenuText::ROLL_HINT_COIN_PRICE_COUNT },
    { OPT_HINT_PRICE_CAPTURES, KIND_ROLL, MenuText::OPT_HINT_PRICE_CAPTURES_LABEL,
        MenuText::ROLL_HINT_COIN_PRICE, MenuText::ROLL_HINT_COIN_PRICE_COUNT },
    { OPT_HINT_PRICE_ABILITIES, KIND_ROLL, MenuText::OPT_HINT_PRICE_ABILITIES_LABEL,
        MenuText::ROLL_HINT_COIN_PRICE, MenuText::ROLL_HINT_COIN_PRICE_COUNT },
    { OPT_HINT_PRICE_PATH, KIND_ROLL, MenuText::OPT_HINT_PRICE_PATH_LABEL,
        MenuText::ROLL_HINT_COIN_PRICE, MenuText::ROLL_HINT_COIN_PRICE_COUNT },
    { OPT_HINT_PRICE_PURCHASES, KIND_ROLL, MenuText::OPT_HINT_PRICE_PURCHASES_LABEL,
        MenuText::ROLL_HINT_COIN_PRICE, MenuText::ROLL_HINT_COIN_PRICE_COUNT },
    { OPT_HINT_TOKEN_PROGRESSIVE, KIND_ROLL,
        MenuText::OPT_HINT_TOKEN_PROGRESSIVE_LABEL,
        MenuText::ROLL_HINT_TOKEN_PROGRESSIVE_PRICE,
        MenuText::ROLL_HINT_TOKEN_PROGRESSIVE_PRICE_COUNT },
    { OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL, KIND_CHECK,
        MenuText::OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL_LABEL, nullptr, 0 },

    // Retained for preset portability: the single active-currency increment
    // and its shared-ladder flag, which the two per-currency rows replaced.
    { OPT_HINT_PROGRESSIVE_PRICE, KIND_ROLL,
        MenuText::OPT_HINT_PROGRESSIVE_PRICE_LABEL,
        MenuText::ROLL_HINT_COIN_PROGRESSIVE_PRICE,
        MenuText::ROLL_HINT_COIN_PROGRESSIVE_PRICE_COUNT },
    { OPT_HINT_PROGRESSIVE_GLOBAL, KIND_CHECK,
        MenuText::OPT_HINT_PROGRESSIVE_GLOBAL_LABEL, nullptr, 0 },
};

// Everything that changes what the game looks and sounds like without touching
// what is placed where. The three Disable rows only bite while a music mode
// other than Off is selected.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sVisualAudioOptions[] = {
    { OPT_SKYBOXES, KIND_ROLL, MenuText::OPT_SKYBOXES_LABEL,
        MenuText::ROLL_SKYBOXES, MenuText::ROLL_SKYBOXES_COUNT },
    { OPT_STABILIZE_SHADERS, KIND_CHECK,
        MenuText::OPT_STABILIZE_SHADERS_LABEL, nullptr, 0 },
    { OPT_MUSIC, KIND_ROLL, MenuText::OPT_MUSIC_LABEL,
        MenuText::ROLL_MUSIC, MenuText::ROLL_MUSIC_COUNT },
    { OPT_MUSIC_SOURCE, KIND_ROLL, MenuText::OPT_MUSIC_SOURCE_LABEL,
        sMusicSourceRollText, 4 },
    { OPT_MUSIC_OVERLAY, KIND_CHECK, MenuText::OPT_MUSIC_OVERLAY_LABEL, nullptr, 0 },
    { OPT_STREAMER_MODE, KIND_CHECK, MenuText::OPT_STREAMER_MODE_LABEL, nullptr, 0 },
    { OPT_MUSIC_DISABLE_2D, KIND_CHECK,
        MenuText::OPT_MUSIC_DISABLE_2D_LABEL, nullptr, 0 },
    { OPT_MUSIC_DISABLE_EXTRA, KIND_CHECK,
        MenuText::OPT_MUSIC_DISABLE_EXTRA_LABEL, nullptr, 0 },
    { OPT_MUSIC_DISABLE_BOSS, KIND_CHECK,
        MenuText::OPT_MUSIC_DISABLE_BOSS_LABEL, nullptr, 0 },
};

// Everything that is not tracker-related; the tracker rows live on their own
// page below. Two columns, so the 8-row single-column ceiling does not apply.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sQualityOfLifeOptions[] = {
    { OPT_FAST_MOON_DEMOS, KIND_CHECK, MenuText::OPT_FAST_MOON_DEMOS_LABEL, nullptr, 0 },
    { OPT_SKIP_CUTSCENE_CAMERAS, KIND_CHECK,
        MenuText::OPT_SKIP_CUTSCENE_CAMERAS_LABEL, nullptr, 0 },
    { OPT_MOTION_FREE, KIND_CHECK, MenuText::OPT_MOTION_FREE_LABEL, nullptr, 0 },
    { OPT_EASY_MINIGAMES, KIND_CHECK, MenuText::OPT_EASY_MINIGAMES_LABEL, nullptr, 0 },
    { OPT_ONE_SHEEP_HERDING, KIND_ROLL,
        MenuText::OPT_ONE_SHEEP_HERDING_LABEL,
        MenuText::ROLL_SHEEP_HERDING, MenuText::ROLL_SHEEP_HERDING_COUNT },
    { OPT_FREE_COSTUME_MOON, KIND_CHECK,
        MenuText::OPT_FREE_COSTUME_MOON_LABEL, nullptr, 0 },
    { OPT_UNLOCK_HINT_ART, KIND_CHECK, MenuText::OPT_UNLOCK_HINT_ART_LABEL, nullptr, 0 },
    { OPT_COMPLETED_KINGDOM_MOON_COLLECTED, KIND_CHECK,
        MenuText::OPT_COMPLETED_KINGDOM_MOON_COLLECTED_LABEL, nullptr, 0 },
    { OPT_AREA_NAME_OVERLAY, KIND_CHECK, MenuText::OPT_AREA_NAME_OVERLAY_LABEL, nullptr, 0 },
    { OPT_RANDOMIZE_OUTFITS, KIND_CHECK, MenuText::OPT_RANDOMIZE_OUTFITS_LABEL, nullptr, 0 },
    { OPT_ALWAYS_NEUTRAL_CAP_THROW, KIND_CHECK,
        MenuText::OPT_ALWAYS_NEUTRAL_CAP_THROW_LABEL, nullptr, 0 },
    { OPT_NO_CLIP, KIND_CHECK, MenuText::OPT_NO_CLIP_LABEL, nullptr, 0 },
    { OPT_FAST_DOG, KIND_CHECK, u"Fast Doggo", nullptr, 0 },
    { OPT_UNCAPPED_COINS, KIND_CHECK, u"Uncapped Coins", nullptr, 0 },
    // Board Size moved to Game Mode Settings, where it only shows for the board
    // modes it actually applies to.
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sTrackerOptions[] = {
    { OPT_TRACKER_OVERLAY, KIND_ROLL, MenuText::OPT_TRACKER_OVERLAY_LABEL,
        MenuText::ROLL_TRACKER_MODE, MenuText::ROLL_TRACKER_MODE_COUNT },
    { OPT_TRACKER_RIGHT_ALIGN, KIND_CHECK,
        MenuText::OPT_TRACKER_RIGHT_ALIGN_LABEL, nullptr, 0 },
    { OPT_TRACKER_CAVE_SKIP, KIND_ROLL, MenuText::OPT_TRACKER_CAVE_SKIP_LABEL,
        MenuText::ROLL_TRACKER_CAVE_SKIP, MenuText::ROLL_TRACKER_CAVE_SKIP_COUNT },
    { OPT_TRACKER_MOON_LIMITS, KIND_ROLL, MenuText::OPT_TRACKER_MOON_LIMITS_LABEL,
        MenuText::ROLL_TRACKER_MOON_LIMITS, MenuText::ROLL_TRACKER_MOON_LIMITS_COUNT },
    { OPT_TRACKER_TIMER, KIND_CHECK, MenuText::OPT_TRACKER_TIMER_LABEL, nullptr, 0 },
    { OPT_TRACKER_WEB_HEADER, KIND_DISPLAY,
        MenuText::OPT_TRACKER_WEB_HEADER_LABEL, nullptr, 0 },
    { OPT_TRACKER_ROOM_CODE, KIND_ACTION,
        MenuText::OPT_TRACKER_ROOM_CODE_LABEL, nullptr, 0 },
};

// The host's Multiplayer page. Death Link and Sync Other Teams are stored in
// rando.bin like any other seed setting, so they belong here with them rather
// than on the title screen's connect menu, where they had to be set before the
// seed they are part of even existed.
//
// Your Team is different in kind from the two below it: every player picks
// their own (see sGuestMultiplayerOptions for the guest's version of this
// page), and it is stored per-console rather than in the seed. It sits first
// because it is the row the host is most likely to want.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sMultiplayerOptions[] = {
    { OPT_TEAM, KIND_ROLL, MenuText::OPT_TEAM_LABEL,
        MenuText::ROLL_TEAM, MenuText::ROLL_TEAM_COUNT },
    { OPT_DEATH_LINK, KIND_CHECK, MenuText::OPT_DEATH_LINK_LABEL, nullptr, 0 },
    { OPT_SYNC_OTHER_TEAMS, KIND_CHECK,
        MenuText::OPT_SYNC_OTHER_TEAMS_LABEL, nullptr, 0 },
};

// The guest's version of the page above. Only the team picker: the other two
// rows are the HOST's to set, and a guest's local copy of them is overwritten
// by the room's flags the moment the seed lands.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sGuestMultiplayerOptions[] = {
    { OPT_TEAM, KIND_ROLL, MenuText::OPT_TEAM_LABEL,
        MenuText::ROLL_TEAM, MenuText::ROLL_TEAM_COUNT },
};

const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sSummaryOptions[] = {
    { OPT_SAVE_AS_PRESET, KIND_ACTION, MenuText::OPT_SAVE_AS_PRESET_LABEL, nullptr, 0 },
    { OPT_GENERATE_SEED, KIND_ACTION, MenuText::OPT_GENERATE_SEED_LABEL, nullptr, 0 },
};

// Guest join mode's Summary page. No "Save as Preset" either: a guest never
// picked any of the settings a preset would capture - it is about to be handed
// the host's whole seed.
const StageSceneStateFileSelect::OptionDef StageSceneStateFileSelect::sGuestSummaryOptions[] = {
    { OPT_JOIN_GAME, KIND_ACTION, MenuText::OPT_JOIN_GAME_LABEL, nullptr, 0 },
};

#define PAGE_DEF(title, arr) \
    { title, arr, (s32)(sizeof(arr) / sizeof(arr[0])), 1 }
// Two columns. The page must never need to scroll: its visible row count has
// to fit twice the layout archive's physical row parts.
#define PAGE_DEF_GRID(title, arr) \
    { title, arr, (s32)(sizeof(arr) / sizeof(arr[0])), 2 }

const StageSceneStateFileSelect::PageDef StageSceneStateFileSelect::sPages[] = {
    PAGE_DEF(MenuText::PAGE_MAIN, sMainOptions),
    // kGameModePageIndex. Display rows come from pageDef(); the full option list
    // here is what the preset snapshot walks.
    PAGE_DEF(MenuText::PAGE_GAME_MODE, sGameModeOptions),
    PAGE_DEF(MenuText::PAGE_RANDOMIZATION, sRandomizationOptions),
    // The moon rules read as the direct continuation of Randomization, so
    // they come first; where the player and the doors go follows.
    PAGE_DEF(MenuText::PAGE_RANDOMIZATION_II, sRandomization3Options),
    PAGE_DEF(MenuText::PAGE_RANDOMIZATION_III, sRandomization2Options),
    PAGE_DEF(MenuText::PAGE_ABILITY_CAPTURE_LOCK, sLockOptions),
    PAGE_DEF(MenuText::PAGE_LOGIC_SETTINGS, sLogicSettingsOptions),
    PAGE_DEF_GRID(MenuText::PAGE_KINGDOM_MOONS, sKingdomMoonsOptions),
#if SMOR_FEATURE_CHAOS_ANY
    PAGE_DEF(MenuText::PAGE_CHAOTIC_RANDOMIZATION, sChaoticRandomizationOptions),
#endif
    PAGE_DEF(MenuText::PAGE_PURPLE_COIN_SHOP, sPurpleShopOptions),
    PAGE_DEF_GRID(MenuText::PAGE_HINT_CONFIGURATION, sHintSettingsOptions),
    PAGE_DEF(MenuText::PAGE_VISUAL_AUDIO, sVisualAudioOptions),
    PAGE_DEF(MenuText::PAGE_TRACKER, sTrackerOptions),
    PAGE_DEF_GRID(MenuText::PAGE_QUALITY_OF_LIFE, sQualityOfLifeOptions),
    // Setup-owner-only; hidden everywhere else by isPageVisible(). Inserted
    // here rather than appended so it comes BEFORE the page carrying Generate
    // Seed - the room's settings have to be set before the seed that records
    // them is made.
    PAGE_DEF(MenuText::PAGE_MULTIPLAYER, sMultiplayerOptions),
    PAGE_DEF(MenuText::PAGE_SUMMARY, sSummaryOptions),
};

const StageSceneStateFileSelect::PageDef& StageSceneStateFileSelect::pageDef(s32 pageIndex) {
    // kPageCount drives page clamping, the "(x/y)" counter and both preset
    // snapshot loops, so a page added without bumping it would silently go
    // missing (or read past the table).
    static_assert(sizeof(sPages) / sizeof(sPages[0]) == kPageCount,
        "kPageCount must match the sPages table");
    static_assert(sizeof(sGameModeOptions) / sizeof(sGameModeOptions[0]) <= kMaxPageOptions,
        "The Game Mode Settings union must fit the shared settings list");
    static_assert(UserPresets::kMaxEntries >= 105,
        "Hint Settings' price increment must fit tagged preset snapshots");
#if SMOR_FEATURE_CHAOS_ANY
    // The chaos page grew from one checkbox to a checkbox and three rolls.
    // Overflowing kMaxPageOptions writes past mSettingsRollParts rather than
    // failing visibly, so assert the fit here.
    static_assert(sizeof(sChaoticRandomizationOptions)
                / sizeof(sChaoticRandomizationOptions[0])
            <= kMaxPageOptions,
        "The Chaotic Randomization page must fit the shared settings list");
#endif
    // The shared settings list treats an advanced editor as a temporary page.
    // Its parent page index remains in mCurrentPage for the return path, but
    // all row lookup while the editor is open must resolve to the live page.
    if (mSubPage != SubPage::None)
        return mSubPageDef;

    if (pageIndex < 0)
        pageIndex = 0;
    if (pageIndex >= kPageCount)
        pageIndex = kPageCount - 1;

    // Guest join mode swaps the Summary page's body: a guest has no seed to
    // generate and no settings worth saving as a preset, only the host's room
    // to join. Every other guest-visible page keeps its normal body - they are
    // all local (GlobalConfig-backed) settings that the downloaded rando.bin
    // deliberately does not overwrite (see RandoSaveFile's applySettingsFlags).
    if (sGuestJoinMode && sPages[pageIndex].options == sSummaryOptions) {
        static const PageDef sGuestSummary = PAGE_DEF(MenuText::PAGE_JOIN_ROOM, sGuestSummaryOptions);
        return sGuestSummary;
    }

    // A guest keeps the Multiplayer page but sees only the team picker on it -
    // that one row is theirs, the room settings beside it are the host's.
    if (sGuestJoinMode && sPages[pageIndex].options == sMultiplayerOptions) {
        static const PageDef sGuestMultiplayer = PAGE_DEF(MenuText::PAGE_MULTIPLAYER, sGuestMultiplayerOptions);
        return sGuestMultiplayer;
    }

    // The Game Mode Settings body comes from the mode that is currently
    // selected. An empty body - Standard, or a mode whose feature is compiled
    // out - is what hides the page from L/R navigation entirely.
    if (pageIndex == kGameModePageIndex) {
        static const PageDef sEmpty = { MenuText::PAGE_GAME_MODE, nullptr, 0 };
        static const PageDef sBoard = PAGE_DEF(MenuText::PAGE_GAME_MODE, sBoardModeOptions);
        static const PageDef sMedals = PAGE_DEF(MenuText::PAGE_GAME_MODE, sMedalModeOptions);
        static const PageDef sNuzlocke = PAGE_DEF(MenuText::PAGE_GAME_MODE, sNuzlockeModeOptions);
        static const PageDef sTimer = PAGE_DEF(MenuText::PAGE_GAME_MODE, sTimerModeOptions);

        switch (Randomizer::getRunMode()) {
        case Randomizer::RunMode::Bingo:
        case Randomizer::RunMode::Ascend:
            return sBoard;
        case Randomizer::RunMode::ToadetteMedals:
            return sMedals;
        case Randomizer::RunMode::Nuzlocke:
        case Randomizer::RunMode::SoftNuzlocke:
            return Randomizer::isNuzlockeModesFeatureEnabled() ? sNuzlocke : sEmpty;
        case Randomizer::RunMode::Timer:
            return Randomizer::isTimerModeFeatureEnabled() ? sTimer : sEmpty;
        case Randomizer::RunMode::Standard:
        default:
            return sEmpty;
        }
    }

    return sPages[pageIndex];
}

#undef PAGE_DEF
#undef PAGE_DEF_GRID

bool StageSceneStateFileSelect::isGuestPage(s32 pageIndex) {
    if (pageIndex < 0 || pageIndex >= kPageCount)
        return false;
    const OptionDef* body = sPages[pageIndex].options;
    // The Multiplayer page is guest-visible now that it carries the team
    // picker - pageDef() swaps its body down to that single row for a guest.
    return body == sQualityOfLifeOptions
        || body == sTrackerOptions || body == sSummaryOptions
        || body == sMultiplayerOptions;
}

bool StageSceneStateFileSelect::isPageVisible(s32 pageIndex) {
    if (mSubPage != SubPage::None)
        return false;
    if (pageIndex < 0 || pageIndex >= kPageCount)
        return false;
    // Guest join mode: everything that would configure a seed is gone, because
    // the guest's seed arrives whole from the host and anything set here would
    // be overwritten by it a moment later.
    if (sGuestJoinMode && !isGuestPage(pageIndex))
        return false;
    // The Multiplayer page means nothing to a solo run - there is no room to
    // set anything for. Both room participants get it: the host for the room
    // settings plus their own team, the guest for the team picker alone (see
    // pageDef, which swaps the body).
    if (sPages[pageIndex].options == sMultiplayerOptions && !sMultiplayerHostMode
        && !sGuestJoinMode)
        return false;

    const PageDef& pd = pageDef(pageIndex);
    for (s32 i = 0; i < pd.count; i++) {
        if (optionVisible(pd.options[i].id))
            return true;
    }
    return false;
}

const char16_t* StageSceneStateFileSelect::subPageTitle(SubPage subPage) {
    switch (subPage) {
    case SubPage::MoonAllocationKingdoms:
        return MenuText::PAGE_ADVANCED_MOON_ALLOCATION_KINGDOMS;
    case SubPage::TripleMoonKingdoms:
        return MenuText::PAGE_ADVANCED_TRIPLE_MOON_KINGDOMS;
    case SubPage::LockedCapturePool:
        return MenuText::PAGE_ADVANCED_LOCKED_CAPTURE_POOL;
    case SubPage::LockedAbilityPool:
        return MenuText::PAGE_ADVANCED_LOCKED_ABILITY_POOL;
    case SubPage::StartingEquipment:
        return MenuText::PAGE_ADVANCED_STARTING_EQUIPMENT;
    case SubPage::PurpleShopPrice:
        return MenuText::PAGE_ADVANCED_PURPLE_SHOP_PRICE;
    case SubPage::ReqDeviation:
        return MenuText::PAGE_ADVANCED_REQ_DEVIATION;
    case SubPage::MoonRequirementBases:
        return MenuText::PAGE_ADVANCED_MOON_REQUIREMENT;
    case SubPage::KingdomMoonUses:
        return MenuText::PAGE_ADVANCED_KINGDOM_MOON_USES;
    case SubPage::MoonRockKeyScope:
        return MenuText::PAGE_ADVANCED_MOON_ROCK_KEY_SCOPE;
    case SubPage::SubareaScope:
        return MenuText::PAGE_ADVANCED_SUBAREA_SCOPE;
    case SubPage::StartKingdoms:
        return MenuText::PAGE_ADVANCED_START_KINGDOMS;
    case SubPage::HintEntry:
        return MenuText::PAGE_ADVANCED_HINT_ENTRY;
    case SubPage::TimerValues:
        return MenuText::PAGE_ADVANCED_TIMER_VALUES;
    case SubPage::NuzlockeValues:
        return MenuText::PAGE_ADVANCED_NUZLOCKE_VALUES;
    case SubPage::MinigameRange:
        return MenuText::PAGE_ADVANCED_MINIGAME_RANGE;
    case SubPage::SheepCount:
        return MenuText::PAGE_ADVANCED_SHEEP_COUNT;
    case SubPage::RequirementKingdoms:
        return MenuText::PAGE_ADVANCED_REQUIREMENT_KINGDOMS;
    case SubPage::CaptureGrantKingdoms:
        return MenuText::PAGE_ADVANCED_CAPTURE_GRANT_KINGDOMS;
    case SubPage::AbilityGrantKingdoms:
        return MenuText::PAGE_ADVANCED_ABILITY_GRANT_KINGDOMS;
    case SubPage::None:
    case SubPage::Count:
    default:
        return MenuText::PAGE_MAIN;
    }
}

bool StageSceneStateFileSelect::advancedFor(OptionId id, SubPage* out, s32* param) {
    if (out)
        *out = SubPage::None;
    if (param)
        *param = 0;

    SubPage subPage = SubPage::None;
    s32 subPageParam = 0;

    // Every kingdom row opens the same multi-select, parameterised by which
    // kingdom it is.
    if (isKingdomMoonUseOption(id)) {
        if (out)
            *out = SubPage::KingdomMoonUses;
        if (param)
            *param = kingdomMoonUseIndex(id);
        return true;
    }

    switch (id) {
    case OPT_MOON_REQUIREMENT:
        subPage = SubPage::MoonRequirementBases;
        break;
    case OPT_MOON_REQ_DEVIATION:
        subPage = SubPage::ReqDeviation;
        break;
    case OPT_MOON_ALLOCATION_DIFFICULTY:
        subPage = SubPage::MoonAllocationKingdoms;
        break;
    case OPT_TRIPLE_MOONS:
        subPage = SubPage::TripleMoonKingdoms;
        break;
    case OPT_LOCKED_CAPTURES:
        subPage = SubPage::LockedCapturePool;
        break;
    case OPT_REQUIREMENT_KINGDOMS:
        subPage = SubPage::RequirementKingdoms;
        break;
    case OPT_CAPTURE_GRANT_KINGDOMS:
        subPage = SubPage::CaptureGrantKingdoms;
        break;
    case OPT_ABILITY_GRANT_KINGDOMS:
        subPage = SubPage::AbilityGrantKingdoms;
        break;
    case OPT_LOCKED_ABILITIES:
        subPage = SubPage::LockedAbilityPool;
        break;
    case OPT_MOON_ROCK_KEY:
        subPage = SubPage::MoonRockKeyScope;
        break;
#if SMOR_FEATURE_SUBAREA_KEYS
    case OPT_SUBAREA_UNLOCKS:
        subPage = SubPage::SubareaScope;
        break;
#endif
    case OPT_STARTING_EQUIPMENT:
        subPage = SubPage::StartingEquipment;
        break;
        // Removed ReqDeviation
    case OPT_HINT_PRICE_MOONS:
    case OPT_HINT_PRICE_CAPTURES:
    case OPT_HINT_PRICE_ABILITIES:
    case OPT_HINT_PRICE_PATH:
    case OPT_HINT_PRICE_PURCHASES:
        subPage = SubPage::HintEntry;
        subPageParam = static_cast<s32>(id) - static_cast<s32>(OPT_HINT_PRICE_MOONS);
        break;
    case OPT_TIMER_START:
    case OPT_TIMER_MOON_BONUS:
    case OPT_TIMER_TRIPLE_BONUS:
    case OPT_TIMER_DAMAGE_PENALTY:
    case OPT_TIMER_DEATH_PENALTY:
    case OPT_TIMER_HALF_LIFE:
        subPage = SubPage::TimerValues;
        break;
    case OPT_SOFT_NUZLOCKE_LOSS:
    case OPT_SOFT_NUZLOCKE_ABILITY_LOSS:
    case OPT_SOFT_NUZLOCKE_CAPTURE_LOSS:
        subPage = SubPage::NuzlockeValues;
        break;
    case OPT_EASY_MINIGAMES:
        subPage = SubPage::MinigameRange;
        break;
    case OPT_ONE_SHEEP_HERDING:
        subPage = SubPage::SheepCount;
        break;
    // Purple Shop Price opens a numeric keyboard in the next phase, so it is
    // intentionally not routed through a one-row sub-page here.
    case OPT_PURPLE_SHOP_PRICE:
    case OPT_NONE:
    default:
        return false;
    }

    if (out)
        *out = subPage;
    if (param)
        *param = subPageParam;
    return subPage != SubPage::None;
}

// Item count for the paged advanced sub-pages. 0 means "not paged": the
// sub-page fits one screen, or (HintEntry) mSubPageParam already means
// something else.
s32 StageSceneStateFileSelect::subPageItemCount(SubPage subPage) {
    switch (subPage) {
    case SubPage::MoonAllocationKingdoms:
        // The moon-coin policy is item 0, ahead of the per-kingdom rows.
        return WorldMeta::sWorldCount + 1;
    case SubPage::TripleMoonKingdoms:
        // The Anywhere toggle is item 0, ahead of the per-kingdom rows.
        return WorldMeta::sWorldCount + 1;
    case SubPage::LockedCapturePool:
        return CaptureRando::getCaptureCount();
    case SubPage::LockedAbilityPool:
        return lockedAbilityPoolItemCount();
    default:
        return 0;
    }
}

s32 StageSceneStateFileSelect::subPagePageCount(SubPage subPage) {
    const s32 items = subPageItemCount(subPage);
    if (items <= kSubPageItemsPerPage)
        return items > 0 ? 1 : 0;
    return (items + kSubPageItemsPerPage - 1) / kSubPageItemsPerPage;
}

void StageSceneStateFileSelect::buildSubPage() {
    for (s32 i = 0; i < kMaxPageOptions; i++)
        mSubPageRows[i] = { OPT_NONE, KIND_DISPLAY, MenuText::EMPTY, nullptr, 0 };

    for (s32 i = 0; i < kMaxPageOptions; i++)
        mSubPageRowLabels[i].copy(MenuText::EMPTY);

    if (mSubPage == SubPage::None) {
        mSubPageDef = {};
        return;
    }

    // Keep the page index in range here so buildSubPage(), the label refresh
    // and the L/R handler all slice the same window.
    const s32 subPages = subPagePageCount(mSubPage);
    if (subPages > 0) {
        if (mSubPageParam < 0)
            mSubPageParam = subPages - 1;
        else if (mSubPageParam >= subPages)
            mSubPageParam = 0;
    }
    const s32 subPageFirstItem = subPages > 0 ? mSubPageParam * kSubPageItemsPerPage : 0;

    mSubPageRows[0] = { OPT_ADVANCED_RESET, KIND_ACTION,
        MenuText::OPT_ADVANCED_RESET_LABEL, nullptr, 0 };

    if (mSubPage == SubPage::MoonAllocationKingdoms) {
        ensureMoonTargetRollText();
        s32 rowCount = 0;
        // Item 0 of the paged list is the moon-coin policy; kingdoms follow it.
        if (subPageFirstItem == 0) {
            rowCount++;
            mSubPageRows[rowCount] = { OPT_MOON_COIN_POLICY, KIND_ROLL,
                MenuText::OPT_MOON_COIN_POLICY_LABEL,
                MenuText::ROLL_MOON_COIN_POLICY,
                MenuText::ROLL_MOON_COIN_POLICY_COUNT };
        }
        for (s32 worldId = subPageFirstItem == 0 ? 0 : subPageFirstItem - 1;
             worldId < WorldMeta::sWorldCount && rowCount < kSubPageItemsPerPage;
             worldId++) {
            const s32 row = ++rowCount;
            const s16 target = Randomizer::getKingdomMoonTarget(worldId);
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u": ");
            if (target < 0) {
                mSubPageRowLabels[row].append(MenuText::VALUE_AUTO);
                mSubPageRowLabels[row].append(u" (");
                mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                    moonAllocationDisplayValue(worldId));
                mSubPageRowLabels[row].append(u")");
            } else {
                mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                    target);
            }
            mSubPageRows[row] = {
                static_cast<OptionId>(static_cast<s32>(OPT_MOON_ALLOCATION_KINGDOM_0)
                    + worldId),
                KIND_ROLL,
                mSubPageRowLabels[row].cstr(),
                sMoonTargetRollText,
                kMoonTargetRollCount,
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, rowCount + 1 };
    } else if (mSubPage == SubPage::TripleMoonKingdoms) {
        // Item 0 of the paged list is the Anywhere toggle; kingdoms follow it.
        s32 rowCount = 0;
        if (subPageFirstItem == 0) {
            rowCount++;
            mSubPageRows[rowCount] = { OPT_TRIPLE_MOONS_ANYWHERE, KIND_CHECK,
                MenuText::OPT_TRIPLE_MOONS_ANYWHERE_LABEL, nullptr, 0 };
        }
        for (s32 worldId = subPageFirstItem == 0 ? 0 : subPageFirstItem - 1;
             worldId < WorldMeta::sWorldCount && rowCount < kSubPageItemsPerPage;
             worldId++) {
            const s32 row = ++rowCount;
            // The parenthesised number is the kingdom's own vanilla triple-moon
            // count, so the default stays readable once the row is overridden.
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u" (");
            mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                vanillaTripleMoonCountForKingdom(worldId));
            mSubPageRowLabels[row].append(u")");
            mSubPageRows[row] = {
                static_cast<OptionId>(static_cast<s32>(OPT_TRIPLE_MOON_KINGDOM_0)
                    + worldId),
                KIND_ROLL,
                mSubPageRowLabels[row].cstr(),
                sTripleMoonTargetRollText,
                sizeof(sTripleMoonTargetRollText)
                    / sizeof(sTripleMoonTargetRollText[0]),
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, rowCount + 1 };
    } else if (mSubPage == SubPage::LockedCapturePool) {
        const s32 captureCount = CaptureRando::getCaptureCount();
        s32 rowCount = 0;
        for (s32 captureId = subPageFirstItem;
             captureId < captureCount && rowCount < kSubPageItemsPerPage;
             captureId++) {
            const s32 row = ++rowCount;
            const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(captureId);
            copyAsciiToWide(mSubPageRowLabels[row],
                capture ? capture->displayName : "Capture");
            mSubPageRows[row] = {
                static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_CAPTURE_POOL_BASE)
                    + captureId),
                KIND_CHECK,
                mSubPageRowLabels[row].cstr(),
                nullptr,
                0,
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, rowCount + 1 };
    } else if (mSubPage == SubPage::LockedAbilityPool) {
        // Enumerate the stable table, not getUnlockableAbility(), whose result
        // intentionally changes with the current mask. If an item vanished
        // when unchecked there would be no way to check it again.
        const s32 abilityCount = AbilityRando::getAbilityCount();
        // Item 0 is the Progressive Abilities toggle - it governs how every row
        // below it is handed over, so it sits at the top of the first page.
        s32 itemIndex = 0;
        s32 rowCount = 0;
        if (subPageFirstItem == 0) {
            rowCount++;
            mSubPageRows[rowCount] = { OPT_ADVANCED_PROGRESSIVE_ABILITIES,
                KIND_CHECK, MenuText::OPT_ADVANCED_PROGRESSIVE_ABILITIES_LABEL,
                nullptr, 0 };
        }
        itemIndex++;
        for (s32 abilityId = 0; abilityId < abilityCount
             && rowCount < kSubPageItemsPerPage;
             abilityId++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(abilityId);
            if (!isLockedAbilityPoolRow(abilityId) || !ability)
                continue;
            // The filter above means the item index is not the ability id, so
            // the page window is applied to the filtered sequence.
            if (itemIndex++ < subPageFirstItem)
                continue;
            const s32 row = ++rowCount;
            // Final-form name, not "Progressive Jump (2)": the row stands for
            // the move you end up with, and toggling it moves the whole chain.
            copyAsciiToWide(mSubPageRowLabels[row],
                ability->displayName ? ability->displayName : ability->key);
            mSubPageRows[row] = {
                static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_ABILITY_POOL_BASE)
                    + abilityId),
                KIND_CHECK,
                mSubPageRowLabels[row].cstr(),
                nullptr,
                0,
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, rowCount + 1 };
    } else if (mSubPage == SubPage::ReqDeviation) {
        // One row per kingdom that gates, same set and same layout as the
        // requirement page it sits beside.
        s32 row = 1;
        const s32 deviationKingdoms = RequirementRando::getRequirementKingdomCount();
        for (s32 i = 0; i < deviationKingdoms && row < kMaxPageOptions; i++) {
            const s32 worldId = RequirementRando::getRequirementKingdomWorldId(i);
            mSubPageRows[row] = {
                static_cast<OptionId>(
                    static_cast<s32>(OPT_ADVANCED_REQ_DEVIATION_BASE) + worldId),
                KIND_ACTION,
                MenuText::EMPTY,
                nullptr,
                0,
            };
            row++;
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, row, 2 };
    } else if (mSubPage == SubPage::MoonRequirementBases) {
        // The total first, then the kingdoms it is spread over. Both are
        // numeric entry: A on a row opens the keyboard.
        mSubPageRowLabels[1].format(MenuText::ADVANCED_REQ_TOTAL_FORMAT,
            RequirementRando::getRequirementBaseTotal());
        mSubPageRows[1] = { OPT_ADVANCED_REQ_TOTAL, KIND_ACTION,
            mSubPageRowLabels[1].cstr(), nullptr, 0 };
        // Every kingdom the route can gate, whether it currently does or not -
        // a kingdom sitting at 0 stays on the page so it can be given a
        // requirement again without going back to the checkbox page.
        s32 row = 2;
        for (s32 worldId = 0;
             worldId < kRequirementKingdomCount && row < kMaxPageOptions;
             worldId++) {
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u": ");
            mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                RequirementRando::getRequirementBase(worldId));
            mSubPageRows[row] = {
                static_cast<OptionId>(
                    static_cast<s32>(OPT_ADVANCED_REQ_BASE_0) + worldId),
                KIND_ACTION,
                mSubPageRowLabels[row].cstr(),
                nullptr,
                0,
            };
            row++;
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, row, 2 };
    } else if (mSubPage == SubPage::RequirementKingdoms) {
        // Fourteen kingdoms plus the reset row - too many for one column, so
        // this is one of the grid pages.
        s32 rowCount = 0;
        for (s32 worldId = 0; worldId < kRequirementKingdomCount; worldId++) {
            const s32 row = ++rowCount;
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRows[row] = {
                static_cast<OptionId>(
                    static_cast<s32>(OPT_ADVANCED_REQ_KINGDOM2_BASE) + worldId),
                KIND_CHECK,
                mSubPageRowLabels[row].cstr(),
                nullptr,
                0,
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, rowCount + 1, 2 };
    } else if (mSubPage == SubPage::CaptureGrantKingdoms
        || mSubPage == SubPage::AbilityGrantKingdoms) {
        // Six rows, so no paging: one per kingdom whose moons the generator can
        // host a reward in (see CaptureRando's getSpecialMoonUseMask).
        const bool captures = mSubPage == SubPage::CaptureGrantKingdoms;
        const s32 base = captures
            ? static_cast<s32>(OPT_ADVANCED_CAPTURE_GRANT_BASE)
            : static_cast<s32>(OPT_ADVANCED_ABILITY_GRANT_BASE);
        for (s32 kingdom = 0; kingdom < kSpecialMoonKingdomCount; kingdom++) {
            const s32 row = kingdom + 1;
            mSubPageRowLabels[row].copy(specialMoonKingdomName(kingdom));
            mSubPageRows[row] = {
                static_cast<OptionId>(base + kingdom),
                KIND_CHECK,
                mSubPageRowLabels[row].cstr(),
                nullptr,
                0,
            };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows,
            kSpecialMoonKingdomCount + 1 };
    } else if (mSubPage == SubPage::StartingEquipment) {
        mSubPageRowLabels[1].format(u"Starting Captures: %d",
            Randomizer::getStartingCaptureCount());
        mSubPageRows[1] = { OPT_ADVANCED_STARTING_CAPTURES, KIND_ACTION,
            mSubPageRowLabels[1].cstr(), nullptr, 0 };
        mSubPageRowLabels[2].format(u"Starting Throws: %d",
            Randomizer::getStartingThrowCount());
        mSubPageRows[2] = { OPT_ADVANCED_STARTING_THROWS, KIND_ACTION,
            mSubPageRowLabels[2].cstr(), nullptr, 0 };
        mSubPageRowLabels[3].format(u"Starting Abilities: %d",
            Randomizer::getStartingAbilityCount());
        mSubPageRows[3] = { OPT_ADVANCED_STARTING_ABILITIES, KIND_ACTION,
            mSubPageRowLabels[3].cstr(), nullptr, 0 };
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 4 };
    } else if (mSubPage == SubPage::KingdomMoonUses) {
        // mSubPageParam is the world id, so one editor serves all 17.
        const s32 worldId = mSubPageParam >= 0
                && mSubPageParam < WorldMeta::sWorldCount
            ? mSubPageParam
            : 0;
        s32 row = 1;
        for (s32 i = 0; i < kingdomMoonUseNamedCount(worldId); i++) {
            const Randomizer::SpecialMoonUse use = kingdomMoonUseOrder(worldId)[i];
            // The three unlock values are one question, answered by the select
            // that takes the first of their slots.
            if (use == Randomizer::SpecialMoonUse_Abilities
                || use == Randomizer::SpecialMoonUse_AbilitiesOrCaptures)
                continue;
            if (use == Randomizer::SpecialMoonUse_Captures) {
                mSubPageRows[row++] = {
                    static_cast<OptionId>(
                        static_cast<s32>(OPT_ADVANCED_KINGDOM_UNLOCK_0) + worldId),
                    KIND_ROLL,
                    MenuText::ADVANCED_KINGDOM_UNLOCK_LABEL,
                    MenuText::ROLL_KINGDOM_UNLOCK,
                    MenuText::ROLL_KINGDOM_UNLOCK_COUNT,
                };
                continue;
            }
            mSubPageRows[row++] = {
                static_cast<OptionId>(
                    static_cast<s32>(OPT_ADVANCED_KINGDOM_MOON_USE_BASE)
                    + worldId * 7 + static_cast<s32>(use)),
                KIND_CHECK,
                specialMoonUseText(use),
                nullptr,
                0,
            };
        }
        // The title names the kingdom being edited - the rows are the same
        // for every one of them.
        mSubPageRowLabels[0].format(MenuText::PAGE_KINGDOM_MOON_REWARDS_FORMAT,
            kingdomMoonUseRowName(worldId));
        mSubPageDef = { mSubPageRowLabels[0].cstr(), mSubPageRows, row };
    } else if (mSubPage == SubPage::HintEntry) {
        if (mSubPageParam < 0 || mSubPageParam >= 5)
            mSubPageParam = 0;
        s32 row = 1;
        const Randomizer::HintPriceCategory category = static_cast<Randomizer::HintPriceCategory>(mSubPageParam);
        s32 price = Randomizer::getHintPriceQuote(category, 0).amount;
        if (price < 1)
            price = 1;
        mSubPageRowLabels[row].format(MenuText::ADVANCED_HINT_PRICE_FORMAT, price);
        mSubPageRows[row] = { OPT_ADVANCED_HINT_CUSTOM_PRICE, KIND_ACTION,
            mSubPageRowLabels[row].cstr(), nullptr, 0 };
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, row + 1 };
    } else if (mSubPage == SubPage::TimerValues) {
        static constexpr OptionId sTimerOptions[] = {
            OPT_ADVANCED_TIMER_VALUE_BASE,
            static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_TIMER_VALUE_BASE) + 1),
            static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_TIMER_VALUE_BASE) + 2),
            static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_TIMER_VALUE_BASE) + 3),
            static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_TIMER_VALUE_BASE) + 4),
            OPT_ADVANCED_TIMER_VALUE_5,
        };
        static constexpr const char16_t* sTimerFormats[] = {
            MenuText::ADVANCED_TIMER_START_FORMAT,
            MenuText::ADVANCED_TIMER_MOON_BONUS_FORMAT,
            MenuText::ADVANCED_TIMER_TRIPLE_BONUS_FORMAT,
            MenuText::ADVANCED_TIMER_DAMAGE_PENALTY_FORMAT,
            MenuText::ADVANCED_TIMER_DEATH_PENALTY_FORMAT,
            MenuText::ADVANCED_TIMER_HALF_LIFE_FORMAT,
        };
        for (s32 valueIndex = 0; valueIndex < 6; valueIndex++) {
            mSubPageRowLabels[valueIndex + 1].format(sTimerFormats[valueIndex],
                valueIndex == 0       ? Randomizer::getTimerStartSeconds()
                    : valueIndex == 1 ? Randomizer::getTimerMoonBonusSeconds()
                    : valueIndex == 2 ? Randomizer::getTimerTripleBonusSeconds()
                    : valueIndex == 3 ? Randomizer::getTimerDamagePenaltySeconds()
                    : valueIndex == 4 ? Randomizer::getTimerDeathPenaltySeconds()
                                      : Randomizer::getTimerHalfLifeSeconds());
            mSubPageRows[valueIndex + 1] = { sTimerOptions[valueIndex], KIND_ACTION,
                mSubPageRowLabels[valueIndex + 1].cstr(), nullptr, 0 };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 7 };
    } else if (mSubPage == SubPage::NuzlockeValues) {
        static constexpr OptionId sNuzlockeOptions[] = {
            OPT_ADVANCED_NUZLOCKE_VALUE_BASE,
            static_cast<OptionId>(static_cast<s32>(OPT_ADVANCED_NUZLOCKE_VALUE_BASE) + 1),
            OPT_ADVANCED_NUZLOCKE_VALUE_2,
        };
        static constexpr const char16_t* sNuzlockeFormats[] = {
            MenuText::ADVANCED_NUZLOCKE_MOONS_FORMAT,
            MenuText::ADVANCED_NUZLOCKE_ABILITY_FORMAT,
            MenuText::ADVANCED_NUZLOCKE_CAPTURE_FORMAT,
        };
        for (s32 valueIndex = 0; valueIndex < 3; valueIndex++) {
            const s32 percent = valueIndex == 0
                ? Randomizer::getSoftNuzlockeLossPercent()
                : valueIndex == 1
                ? Randomizer::getSoftNuzlockeAbilityLossPercent()
                : Randomizer::getSoftNuzlockeCaptureLossPercent();
            mSubPageRowLabels[valueIndex + 1].format(sNuzlockeFormats[valueIndex],
                percent);
            mSubPageRows[valueIndex + 1] = { sNuzlockeOptions[valueIndex],
                KIND_ACTION, mSubPageRowLabels[valueIndex + 1].cstr(), nullptr, 0 };
        }
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 4 };
    } else if (mSubPage == SubPage::MinigameRange) {
        mSubPageRowLabels[1].format(MenuText::ADVANCED_MINIGAME_LOW_FORMAT,
            Randomizer::getEasyMinigameLow());
        mSubPageRows[1] = { OPT_ADVANCED_MINIGAME_LOW, KIND_ACTION,
            mSubPageRowLabels[1].cstr(), nullptr, 0 };
        mSubPageRowLabels[2].format(MenuText::ADVANCED_MINIGAME_HIGH_FORMAT,
            Randomizer::getEasyMinigameHigh());
        mSubPageRows[2] = { OPT_ADVANCED_MINIGAME_HIGH, KIND_ACTION,
            mSubPageRowLabels[2].cstr(), nullptr, 0 };
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 3 };
    } else if (mSubPage == SubPage::SheepCount) {
        mSubPageRows[1] = { OPT_ADVANCED_SHEEP_COUNT, KIND_ROLL,
            MenuText::ADVANCED_SHEEP_COUNT_LABEL, MenuText::ROLL_SHEEP_COUNT,
            MenuText::ROLL_SHEEP_COUNT_COUNT };
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 2 };
    } else {
        mSubPageDef = { subPageTitle(mSubPage), mSubPageRows, 1 };
    }
    updateSubPageBalance();
}

void StageSceneStateFileSelect::updateSubPageRows() {
    if (mSubPage == SubPage::MoonAllocationKingdoms) {
        const s32 startIdx = mSubPageParam * kSubPageItemsPerPage;
        s32 itemsToCopy = WorldMeta::sWorldCount - startIdx;
        if (itemsToCopy > kSubPageItemsPerPage)
            itemsToCopy = kSubPageItemsPerPage;
        for (s32 i = 0; i < itemsToCopy; i++) {
            const s32 worldId = startIdx + i;
            const s32 row = i + 1;
            const s16 target = Randomizer::getKingdomMoonTarget(worldId);
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u": ");
            if (target < 0) {
                mSubPageRowLabels[row].append(MenuText::VALUE_AUTO);
                mSubPageRowLabels[row].append(u" (");
                mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                    moonAllocationDisplayValue(worldId));
                mSubPageRowLabels[row].append(u")");
            } else {
                mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                    target);
            }
            mSubPageRows[row].label = mSubPageRowLabels[row].cstr();
        }
    } else if (mSubPage == SubPage::TripleMoonKingdoms) {
        const s32 totalItems = 1 + WorldMeta::sWorldCount;
        const s32 startIdx = mSubPageParam * kSubPageItemsPerPage;
        s32 itemsToCopy = totalItems - startIdx;
        if (itemsToCopy > kSubPageItemsPerPage)
            itemsToCopy = kSubPageItemsPerPage;
        for (s32 i = 0; i < itemsToCopy; i++) {
            const s32 listIdx = startIdx + i;
            if (listIdx == 0)
                continue;
            const s32 worldId = listIdx - 1;
            const s32 row = i + 1;
            // Label carries the kingdom's vanilla count; the roll carries the
            // chosen total. Keep both in step with buildSubPage().
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u" (");
            mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                vanillaTripleMoonCountForKingdom(worldId));
            mSubPageRowLabels[row].append(u")");
            mSubPageRows[row].label = mSubPageRowLabels[row].cstr();
        }
    } else if (mSubPage == SubPage::StartingEquipment) {
        mSubPageRowLabels[1].format(u"Starting Captures: %d",
            Randomizer::getStartingCaptureCount());
        mSubPageRows[1].label = mSubPageRowLabels[1].cstr();
        mSubPageRowLabels[2].format(u"Starting Throws: %d",
            Randomizer::getStartingThrowCount());
        mSubPageRows[2].label = mSubPageRowLabels[2].cstr();
        mSubPageRowLabels[3].format(u"Starting Abilities: %d",
            Randomizer::getStartingAbilityCount());
        mSubPageRows[3].label = mSubPageRowLabels[3].cstr();
    } else if (mSubPage == SubPage::ReqDeviation) {
        s32 row = 1;
        const s32 deviationKingdoms = RequirementRando::getRequirementKingdomCount();
        for (s32 i = 0; i < deviationKingdoms && row < kMaxPageOptions; i++) {
            const s32 worldId = RequirementRando::getRequirementKingdomWorldId(i);
            const s32 deviation = RequirementRando::getRequirementDeviation(worldId);
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u": ");
            mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                deviation);
            mSubPageRows[row].label = mSubPageRowLabels[row].cstr();
            row++;
        }
    } else if (mSubPage == SubPage::MoonRequirementBases) {
        mSubPageRowLabels[1].format(MenuText::ADVANCED_REQ_TOTAL_FORMAT,
            RequirementRando::getRequirementBaseTotal());
        mSubPageRows[1].label = mSubPageRowLabels[1].cstr();
        s32 row = 2;
        for (s32 worldId = 0;
             worldId < kRequirementKingdomCount && row < kMaxPageOptions;
             worldId++) {
            mSubPageRowLabels[row].copy(
                WorldMeta::getDisplayName16ForWorldId(worldId));
            mSubPageRowLabels[row].append(u": ");
            mSubPageRowLabels[row].appendWithFormat(MenuText::INTEGER_FORMAT,
                RequirementRando::getRequirementBase(worldId));
            mSubPageRows[row].label = mSubPageRowLabels[row].cstr();
            row++;
        }
    } else if (mSubPage == SubPage::HintEntry) {
        const s32 priceRow = 1;
        const Randomizer::HintPriceCategory category = static_cast<Randomizer::HintPriceCategory>(mSubPageParam);
        s32 price = Randomizer::getHintPriceQuote(category, 0).amount;
        if (price < 1)
            price = 1;
        mSubPageRowLabels[priceRow].format(MenuText::ADVANCED_HINT_PRICE_FORMAT,
            price);
        mSubPageRows[priceRow].label = mSubPageRowLabels[priceRow].cstr();
    } else if (mSubPage == SubPage::TimerValues) {
        const s32 seconds[] = {
            Randomizer::getTimerStartSeconds(),
            Randomizer::getTimerMoonBonusSeconds(),
            Randomizer::getTimerTripleBonusSeconds(),
            Randomizer::getTimerDamagePenaltySeconds(),
            Randomizer::getTimerDeathPenaltySeconds(),
            Randomizer::getTimerHalfLifeSeconds(),
        };
        static constexpr const char16_t* sTimerFormats[] = {
            MenuText::ADVANCED_TIMER_START_FORMAT,
            MenuText::ADVANCED_TIMER_MOON_BONUS_FORMAT,
            MenuText::ADVANCED_TIMER_TRIPLE_BONUS_FORMAT,
            MenuText::ADVANCED_TIMER_DAMAGE_PENALTY_FORMAT,
            MenuText::ADVANCED_TIMER_DEATH_PENALTY_FORMAT,
            MenuText::ADVANCED_TIMER_HALF_LIFE_FORMAT,
        };
        for (s32 valueIndex = 0; valueIndex < 6; valueIndex++) {
            mSubPageRowLabels[valueIndex + 1].format(sTimerFormats[valueIndex],
                seconds[valueIndex]);
            mSubPageRows[valueIndex + 1].label = mSubPageRowLabels[valueIndex + 1].cstr();
        }
    } else if (mSubPage == SubPage::NuzlockeValues) {
        const s32 percentages[] = {
            Randomizer::getSoftNuzlockeLossPercent(),
            Randomizer::getSoftNuzlockeAbilityLossPercent(),
            Randomizer::getSoftNuzlockeCaptureLossPercent(),
        };
        static constexpr const char16_t* sNuzlockeFormats[] = {
            MenuText::ADVANCED_NUZLOCKE_MOONS_FORMAT,
            MenuText::ADVANCED_NUZLOCKE_ABILITY_FORMAT,
            MenuText::ADVANCED_NUZLOCKE_CAPTURE_FORMAT,
        };
        for (s32 valueIndex = 0; valueIndex < 3; valueIndex++) {
            mSubPageRowLabels[valueIndex + 1].format(sNuzlockeFormats[valueIndex],
                percentages[valueIndex]);
            mSubPageRows[valueIndex + 1].label = mSubPageRowLabels[valueIndex + 1].cstr();
        }
    } else if (mSubPage == SubPage::MinigameRange) {
        mSubPageRowLabels[1].format(MenuText::ADVANCED_MINIGAME_LOW_FORMAT,
            Randomizer::getEasyMinigameLow());
        mSubPageRows[1].label = mSubPageRowLabels[1].cstr();
        mSubPageRowLabels[2].format(MenuText::ADVANCED_MINIGAME_HIGH_FORMAT,
            Randomizer::getEasyMinigameHigh());
        mSubPageRows[2].label = mSubPageRowLabels[2].cstr();
    }
    updateSubPageBalance();
}

void StageSceneStateFileSelect::updateSubPageBalance() {
    mSubPageBalanceAssigned = -1;
    mSubPageBalanceRequired = -1;

    // The running total has to be the sum of the numbers the rows are actually
    // showing, which for an unset kingdom is the value it falls back to rather
    // than nothing. Reporting the required total back whenever any kingdom was
    // still on Auto made the readout agree with itself no matter what had been
    // picked, and left the over-budget warning permanently unreachable.
    if (mSubPage == SubPage::MoonAllocationKingdoms) {
        if (!Randomizer::isKingdomMoonTargetCustom())
            return;
        mSubPageBalanceRequired = moonAllocationRequiredTotal();
        mSubPageBalanceAssigned = 0;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const s16 target = Randomizer::getKingdomMoonTarget(worldId);
            mSubPageBalanceAssigned += target >= 0
                ? static_cast<s32>(target)
                : moonAllocationDisplayValue(worldId);
        }
    } else if (mSubPage == SubPage::TripleMoonKingdoms) {
        if (!Randomizer::isKingdomTripleMoonTargetCustom())
            return;
        mSubPageBalanceRequired = vanillaTripleMoonCount();
        mSubPageBalanceAssigned = 0;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const s8 target = Randomizer::getKingdomTripleMoonTarget(worldId);
            mSubPageBalanceAssigned += target >= 0
                ? static_cast<s32>(target)
                : vanillaTripleMoonCountForKingdom(worldId);
        }
    }
}

void StageSceneStateFileSelect::normalizeSubPageTargets() {
    if (mSubPage != SubPage::MoonAllocationKingdoms
        && mSubPage != SubPage::TripleMoonKingdoms)
        return;

    const s32 required = mSubPageBalanceRequired;
    if (required < 0)
        return;

    s32 assigned = 0;
    s32 autoCount = 0;
    for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
        const s32 target = mSubPage == SubPage::MoonAllocationKingdoms
            ? Randomizer::getKingdomMoonTarget(worldId)
            : Randomizer::getKingdomTripleMoonTarget(worldId);
        if (target < 0)
            autoCount++;
        else
            assigned += target;
    }

    s32 remaining = required - assigned;
    if (remaining > 0 && autoCount > 0) {
        const s32 each = remaining / autoCount;
        s32 extra = remaining % autoCount;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const s32 target = mSubPage == SubPage::MoonAllocationKingdoms
                ? Randomizer::getKingdomMoonTarget(worldId)
                : Randomizer::getKingdomTripleMoonTarget(worldId);
            if (target >= 0)
                continue;
            s32 value = each + (extra-- > 0 ? 1 : 0);
            const s32 maxValue = mSubPage == SubPage::MoonAllocationKingdoms ? 657 : 8;
            if (value > maxValue)
                value = maxValue;
            if (mSubPage == SubPage::MoonAllocationKingdoms)
                Randomizer::setKingdomMoonTarget(worldId, static_cast<s16>(value));
            else
                Randomizer::setKingdomTripleMoonTarget(worldId, static_cast<s8>(value));
        }
        remaining = 0;
    }

    // If the explicit values overshoot, trim them from the end until the
    // aggregate is valid. This only affects the values that caused the
    // mismatch; automatic rows remain automatic whenever possible.
    if (remaining < 0) {
        for (s32 worldId = WorldMeta::sWorldCount - 1;
             worldId >= 0 && remaining < 0; worldId--) {
            s32 target = mSubPage == SubPage::MoonAllocationKingdoms
                ? Randomizer::getKingdomMoonTarget(worldId)
                : Randomizer::getKingdomTripleMoonTarget(worldId);
            if (target < 0)
                continue;
            const s32 reduction = target < -remaining ? target : -remaining;
            target -= reduction;
            remaining += reduction;
            if (mSubPage == SubPage::MoonAllocationKingdoms)
                Randomizer::setKingdomMoonTarget(worldId, static_cast<s16>(target));
            else
                Randomizer::setKingdomTripleMoonTarget(worldId, static_cast<s8>(target));
        }
    }
    updateSubPageRows();
}

void StageSceneStateFileSelect::enterSubPage(SubPage subPage, s32 param) {
    if (subPage == SubPage::None || subPage == SubPage::Count)
        return;

    const OptionDef* parent = currentOption();
    if (!parent)
        return;

    SubPage expected = SubPage::None;
    s32 expectedParam = 0;
    if (!advancedFor(parent->id, &expected, &expectedParam) || expected != subPage)
        return;

    mReturnPage = mCurrentPage;
    mReturnRow = mCurrentList ? mCurrentList->mCurSelected : 0;
    if (mReturnRow < 0)
        mReturnRow = 0;
    mReturnOption = parent->id;
    mSubPageParam = param;
    if (subPage == SubPage::HintEntry && param == 0 && expectedParam != 0)
        mSubPageParam = expectedParam;

    mSubPage = subPage;
    mSubPageBalanceAssigned = -1;
    mSubPageBalanceRequired = -1;
    mSubPageBalanceNeedsNormalization = false;
    buildSubPage();

    // Prevent loadPage() from interpreting the parent row map as the new
    // sub-page's previous selection. The return page/option above is the only
    // cursor state that should survive this transition.
    mCurrentPage = -1;
    // startMenu() first: its Appear animation was running after loadPage() had
    // already written the title and posed the rows, so a freshly opened
    // multi-page editor showed neither its page counter nor its rows until the
    // next L/R rebuilt it. Only worth doing when the settings menu is not
    // already up, though - replaying Appear on the live menu restarts the
    // layout intro and drops its backing pane for the length of it.
    if (mCurrentList != mOptionsList[MENU_SETTINGS])
        startMenu(MENU_SETTINGS, &NrvStageSceneStateFileSelect.NewSaveSettings);
    loadPage(mReturnPage);
    showInfoBox(true);
    activateInput();
    updateInfoBox();
}

void StageSceneStateFileSelect::exitSubPage() {
    if (mSubPage == SubPage::None)
        return;

    const s32 returnPage = mReturnPage;
    const s32 returnRow = mReturnRow;
    const OptionId returnOption = mReturnOption;
    const bool settingsAlreadyActive = mCurrentList == mOptionsList[MENU_SETTINGS];

    mSubPage = SubPage::None;
    mSubPageParam = 0;
    mCurrentPage = -1;
    loadPage(returnPage);

    if (!settingsAlreadyActive)
        startMenu(MENU_SETTINGS, &NrvStageSceneStateFileSelect.NewSaveSettings);

    s32 selectedRow = returnRow;
    const PageDef& pd = pageDef(mCurrentPage);
    for (s32 row = 0; row < mRowCount; row++) {
        const s32 optionIndex = mRowMap[row];
        if (optionIndex >= 0 && optionIndex < pd.count
            && pd.options[optionIndex].id == returnOption) {
            selectedRow = row;
            break;
        }
    }
    if (mRowCount > 0) {
        if (selectedRow < 0)
            selectedRow = 0;
        if (selectedRow >= mRowCount)
            selectedRow = mRowCount - 1;
        mOptionsList[MENU_SETTINGS]->setSelectedIdx(selectedRow, 0);
        mOptionsList[MENU_SETTINGS]->updateParts();
        applySettingsRowStyles();
    }

    mReturnPage = 0;
    mReturnRow = 0;
    mReturnOption = OPT_NONE;
    mSubPageBalanceAssigned = -1;
    mSubPageBalanceRequired = -1;
    mSubPageBalanceNeedsNormalization = false;
    showInfoBox(true);
    activateInput();
    updateInfoBox();
}

void StageSceneStateFileSelect::resetSubPageDefaults(SubPage subPage) {
    switch (subPage) {
    case SubPage::MoonAllocationKingdoms:
        Randomizer::clearKingdomMoonTargets();
        mSubPageBalanceAssigned = -1;
        mSubPageBalanceRequired = -1;
        mSubPageBalanceNeedsNormalization = false;
        break;
    case SubPage::TripleMoonKingdoms:
        Randomizer::clearKingdomTripleMoonTargets();
        Randomizer::setTripleMoonsAnywhereEnabled(false);
        mSubPageBalanceAssigned = -1;
        mSubPageBalanceRequired = -1;
        mSubPageBalanceNeedsNormalization = false;
        break;
    case SubPage::LockedCapturePool:
        Randomizer::setCaptureLockPoolMask(0, ~static_cast<u64>(0));
        Randomizer::setCaptureLockPoolMask(1, ~static_cast<u64>(0));
        break;
    case SubPage::LockedAbilityPool:
        Randomizer::setAbilityLockPoolMask(~static_cast<u64>(0));
        break;
    case SubPage::StartingEquipment:
        Randomizer::setStartingEquipmentOverrides(-1, -1, -1);
        break;
    case SubPage::PurpleShopPrice:
        Randomizer::setPurpleShopCustomPrice(50);
        if (Randomizer::isPurpleShopPriceCustom())
            Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Purple50);
        break;
    case SubPage::MoonRequirementBases:
        Randomizer::setMoonRequirementLevel(Randomizer::MoonRequirement_124);
        resetRequirementKingdomsToDefault();
        RequirementRando::clearRequirementBases();
        break;
    case SubPage::ReqDeviation:
        // Back to every kingdom following the one Req Deviation number.
        RequirementRando::clearRequirementDeviations();
        break;
    case SubPage::KingdomMoonUses:
        // Only the rewards half: whether the kingdom gates progress is not this
        // page's setting, so Reset Defaults here must not disturb it.
        writeKingdomRewardMask(mSubPageParam,
            Randomizer::getDefaultKingdomMoonUseMask(mSubPageParam));
        break;
    case SubPage::MoonRockKeyScope:
        Randomizer::setMoonRockKeyScope(Randomizer::KeyScope::WithinKingdom);
        break;
    case SubPage::SubareaScope:
        Randomizer::setSubareaKeyScope(Randomizer::KeyScope::WithinKingdom);
        break;
    case SubPage::StartKingdoms:
        Randomizer::setStartKingdomsMask(1u << WorldMeta::WorldId_Cap);
        break;
    case SubPage::HintEntry:
        Randomizer::clearHintPriceOverride(static_cast<Randomizer::HintPriceCategory>(
            mSubPageParam));
        break;
    case SubPage::TimerValues:
        Randomizer::setTimerStartIdx(Randomizer::kTimerStartDefaultIdx);
        Randomizer::setTimerMoonBonusIdx(Randomizer::kTimerMoonBonusDefaultIdx);
        Randomizer::setTimerTripleBonusIdx(Randomizer::kTimerTripleBonusDefaultIdx);
        Randomizer::setTimerDamagePenaltyIdx(0);
        Randomizer::setTimerDeathPenaltyIdx(0);
        Randomizer::setTimerHalfLifeIdx(0);
        break;
    case SubPage::NuzlockeValues:
        Randomizer::setSoftNuzlockeLoss(Randomizer::SoftNuzlockeLoss::Half);
        Randomizer::setSoftNuzlockeAbilityLoss(
            Randomizer::SoftNuzlockeEquipLoss::Half);
        Randomizer::setSoftNuzlockeCaptureLoss(
            Randomizer::SoftNuzlockeEquipLoss::Half);
        break;
    case SubPage::MinigameRange:
        Randomizer::setEasyMinigameHigh(Randomizer::kEasyMinigameDefaultHigh);
        Randomizer::setEasyMinigameLow(Randomizer::kEasyMinigameDefaultLow);
        GlobalConfig::save();
        break;
    case SubPage::SheepCount:
        Randomizer::setSheepHerdingCount(Randomizer::kSheepHerdingDefaultCount);
        GlobalConfig::save();
        break;
    case SubPage::RequirementKingdoms:
        resetRequirementKingdomsToDefault();
        break;
    case SubPage::CaptureGrantKingdoms:
        applyGrantKingdomsPreset(Randomizer::SpecialMoonUse_Captures);
        break;
    case SubPage::AbilityGrantKingdoms:
        applyGrantKingdomsPreset(Randomizer::SpecialMoonUse_Abilities);
        break;
    case SubPage::None:
    case SubPage::Count:
    default:
        break;
    }
}

bool StageSceneStateFileSelect::checkSubPageBalance() {
    if (mSubPage != SubPage::MoonAllocationKingdoms
        && mSubPage != SubPage::TripleMoonKingdoms)
        return true;

    updateSubPageBalance();

    // Only an over-budget total needs confirming: anything left under is
    // absorbed by the kingdoms still sitting on their fallback value.
    if (mSubPageBalanceAssigned < 0 || mSubPageBalanceRequired < 0
        || mSubPageBalanceAssigned <= mSubPageBalanceRequired)
        return true;

    mConfirmAction = mSubPage == SubPage::MoonAllocationKingdoms
        ? ConfirmAction::RedistributeAllocation
        : ConfirmAction::RedistributeTripleMoons;
    rebuildDeleteConfirm();
    showInfoBox(false);
    startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
    return false;
}

bool StageSceneStateFileSelect::closeSubPage(bool fromPlus) {
    // Checked on every way out, not just Plus - backing out with B committed an
    // over-budget set of totals with no warning at all.
    (void)fromPlus;
    if (!checkSubPageBalance())
        return false;
    exitSubPage();
    return true;
}

s32 StageSceneStateFileSelect::visiblePageCount() {
    s32 result = 0;
    for (s32 i = 0; i < kPageCount; i++)
        result += isPageVisible(i) ? 1 : 0;
    return result;
}

s32 StageSceneStateFileSelect::visiblePageOrdinal(s32 pageIndex) {
    s32 ordinal = 0;
    for (s32 i = 0; i <= pageIndex && i < kPageCount; i++)
        ordinal += isPageVisible(i) ? 1 : 0;
    return ordinal > 0 ? ordinal : 1;
}

s32 StageSceneStateFileSelect::findVisiblePage(s32 pageIndex, s32 step) {
    if (step == 0)
        step = 1;
    for (s32 i = pageIndex; i >= 0 && i < kPageCount; i += step) {
        if (isPageVisible(i))
            return i;
    }
    for (s32 i = pageIndex; i >= 0 && i < kPageCount; i -= step) {
        if (isPageVisible(i))
            return i;
    }
    return 0;
}

// ============================================================================
// Construction / lifecycle
// ============================================================================

StageSceneStateFileSelect::StageSceneStateFileSelect(
    const char* name, al::Scene* scene,
    const al::LayoutInitInfo& initInfo,
    FooterParts* footerParts,
    GameDataHolder* dataHolder)
    : al::HostStateBase<al::Scene>(name, scene) {
    mFooterParts = footerParts;
    mGameDataHolder = dataHolder;
    mMsgSystem = initInfo.getMessageSystem();
    mInput = new InputSeparator(mHost, true);
    mKeyboard = new Keyboard();

    for (s32 i = 0; i < Presets::count() && i < 8; i++)
        sPresetRollText[i] = Presets::get(i).name;

    // The two custom entries carry the installed track count, so they are only
    // known once the Music folder has been scanned.
    {
        const char16_t* const* labels = CustomBgmManager::instance().getMusicSourceLabels();
        for (s32 i = 0; i < 4; i++)
            sMusicSourceRollText[i] = labels[i];
    }

    UserPresets::loadAll();
    refreshUserPresetRollText();

    for (s32 i = 0; i < MENU_COUNT; i++)
        mMsgList[i] = new sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>();
    mSlotWorldMsg = new sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>();
    mSlotShineMsg = new sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>();
    mSlotDayMsg = new sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>();
    mSlotBadgeMsg = new sead::SafeArray<sead::WFixedSafeString<0x200>, kMaxMsgCount>();

    initSlotListMenu(initInfo);
    initSlotActionMenu(initInfo);
    initDeleteConfirmMenu(initInfo);
    initSettingsMenu(initInfo);
    initGenerateProgressMenu(initInfo);
    initInfoBox(initInfo);

    for (s32 i = 0; i < MENU_COUNT; i++)
        setMenuContentsVisible(mMenuList[i], i == MENU_SLOT_LIST);

    mCurrentMenu = mMenuList[MENU_SLOT_LIST];
    mCurrentList = mOptionsList[MENU_SLOT_LIST];
}

void StageSceneStateFileSelect::init() {
    initNerve(&NrvStageSceneStateFileSelect.SlotList, 0);
}

void StageSceneStateFileSelect::setSaveCategory(SaveRedirect::Category category) {
    if (mSaveCategory == category)
        return;
    mSaveCategory = category;
    // See the header's doc comment - without this, a category change that
    // arrives after appear() already built the slot list (the Multiplayer
    // button's room-code prompt is asked AFTER appear() runs) never gets
    // reflected on screen. refreshSlotMeta()/rebuildSlotList() only touch
    // the MENU_SLOT_LIST arrays directly (not mCurrentMenu/mCurrentList),
    // so this is safe to call regardless of which menu is currently active.
    refreshSlotMeta();
    rebuildSlotList();
}

void StageSceneStateFileSelect::appear() {
    // A title visit always starts at the file list. Do not let a previous
    // visit that was interrupted inside an advanced editor leak its dynamic
    // PageDef into the new visit.
    mSubPage = SubPage::None;
    mSubPageParam = 0;
    mReturnPage = 0;
    mReturnRow = 0;
    mReturnOption = OPT_NONE;
    mSubPageDef = {};
    mIsDone = false;
    mIsDecide = false;
    mStartMode = StartMode::None;
    showInfoBox(false);
    MpDebugLog::write("StageSceneStateFileSelect::appear: mSaveCategory=%d guest=%d "
                      "(0=SinglePlayer,1=Multiplayer,2=MultiplayerGuest)",
        (int)mSaveCategory, sGuestJoinMode ? 1 : 0);
    refreshSlotMeta();
    rebuildSlotList();
    for (s32 i = 0; i < MENU_COUNT; i++)
        setMenuContentsVisible(mMenuList[i], i == MENU_SLOT_LIST);
    mCurrentMenu = mMenuList[MENU_SLOT_LIST];
    mCurrentList = mOptionsList[MENU_SLOT_LIST];
    mCurrentMenu->startAppear("Appear");
    al::NerveStateBase::appear();

    // al::NerveStateBase::appear() only clears mIsDead - it does NOT put the
    // nerve back to init()'s, and this state is entered more than once per
    // title scene (back out of the file select, pick the other title row, come
    // back), so without this the next visit resumes on whichever nerve the
    // previous one happened to end on. Harmless while every exit was
    // exeSlotList's; not harmless now that guest join mode leaves from
    // NewSaveSettings.
    //
    // Guest join mode's jump past the slot list happens on exeSlotList's first
    // step, NOT here. Calling enterPage() - and through it al::setNerve() -
    // from inside appear() means changing the nerve from outside a nerve
    // update, which is the one thing this screen's history says not to do
    // casually (see PauseMenuHooks.cpp's account of what a mishandled nerve
    // did to isFirstStep and pad input). exeSlotAction's New Game row already
    // reaches the very same enterPage(0) from inside a nerve update, so guest
    // mode uses that proven shape instead.
    al::setNerve(this, &NrvStageSceneStateFileSelect.SlotList);
}

void StageSceneStateFileSelect::kill() {
    showInfoBox(false);
    if (mCurrentMenu) {
        setMenuContentsVisible(mCurrentMenu, false);
        mCurrentMenu->startEnd("End");
    }
    mSubPage = SubPage::None;
    mSubPageParam = 0;
    mReturnPage = 0;
    mReturnRow = 0;
    mReturnOption = OPT_NONE;
    mSubPageDef = {};
    al::NerveStateBase::kill();
}

// ============================================================================
// Simple menus (slot list / action / delete / progress)
// ============================================================================

void StageSceneStateFileSelect::initSlotListMenu(const al::LayoutInitInfo& initInfo) {
    // "OptionFile" is vanilla's own save-data page (OptionData - the pause menu's
    // Options -> Data -> Save/Load list), whose ListDataParts rows are the wide
    // file cards, rebuilt under a private name by tools/build_file_select_layout.py.
    // The rebuild only swaps in OptionModCheck's executor entry (so it draws in the
    // layer TitleMenuScene renders) and adds its ParFooter pane; everything the code
    // below touches - TxtOption, ListArea, ParCursor, ParScroll, ParFooter - is
    // named exactly as on the other pages, so nothing else here has to change.
    mMenuList[MENU_SLOT_LIST] = new SimpleLayoutMenu(
        "FileSelectMenu", "OptionFile", initInfo, 0, false);
    mOptionsList[MENU_SLOT_LIST] = new CommonVerticalList(
        mMenuList[MENU_SLOT_LIST], initInfo, true);

    al::setPaneString(mMenuList[MENU_SLOT_LIST], "TxtOption", MenuText::SLOT_LIST_TITLE, 0);
    setStandardFooter(mMenuFooters, MENU_SLOT_LIST, mMenuList[MENU_SLOT_LIST], initInfo);
    bindSlotList();
    initSlotThumbnails();
    applySlotRowStyles();
}

// The kingdom image on each card. Vanilla's option state builds a TextureInfo
// for every texture in ObjectData/TextureSaveData and looks one up per file
// through GameDataFunction::makeTextureSaveDataFileName, which needs a vanilla
// file id and picks a scenario variant. The rando's save metadata carries only
// the kingdom, so it builds just the 17 base images (plus "Empty") and indexes
// them by world id.
//
// al::createTextureInfo allocates and uploads, so this runs once here rather
// than in the per-appear rebuild - and it returns nullptr for a name the bfres
// does not hold, which is why an unusable set falls back to hiding the pane
// instead of handing setImageData an array of nulls.
void StageSceneStateFileSelect::initSlotThumbnails() {
    mEmptyThumbnail
        = al::createTextureInfo(kSaveTextureResource, kSaveTextureFile, kEmptyThumbnail);

    for (s32 i = 0; i < WorldMeta::sWorldCount; i++) {
        const char* name = WorldMeta::getSaveThumbnailForWorldId(i);
        mWorldThumbnail[i]
            = name ? al::createTextureInfo(kSaveTextureResource, kSaveTextureFile, name) : nullptr;
        if (!mWorldThumbnail[i])
            mWorldThumbnail[i] = mEmptyThumbnail;
        if (mWorldThumbnail[i])
            mHasThumbnails = true;
    }

    if (!mEmptyThumbnail || !mHasThumbnails) {
        Randomizer::logPush("FS thumbnails unavailable empty=%p any=%d",
            mEmptyThumbnail, (s32)mHasThumbnails);
        mHasThumbnails = false;
        return;
    }

    for (s32 i = 0; i < kSlotCount; i++)
        mSlotThumbnail[i] = mEmptyThumbnail;
    mOptionsList[MENU_SLOT_LIST]->setImageData(mSlotThumbnail, "PicDummy");
}

// The card's five text panes, in vanilla's own order (see
// StageSceneStateOption::updateSaveDataInfo): file number, kingdom, moon total,
// timestamp, status pill. addStringData appends through an auto-incrementing
// registration counter that only initDataNoResetSelected resets, so these must
// always be re-registered together right after it.
void StageSceneStateFileSelect::bindSlotList() {
    CommonVerticalList* list = mOptionsList[MENU_SLOT_LIST];
    if (!list)
        return;

    list->initDataNoResetSelected(kSlotCount);
    list->addStringData(mMsgList[MENU_SLOT_LIST]->mBuffer, "TxtNumber");
    list->addStringData(mSlotWorldMsg->mBuffer, "TxtWorld");
    list->addStringData(mSlotShineMsg->mBuffer, "TxtShine");
    list->addStringData(mSlotDayMsg->mBuffer, "TxtDay");
    list->addStringData(mSlotBadgeMsg->mBuffer, "TxtPlay");
}

// Per-card geometry and visual state. Unlike the settings pages this list has
// fewer part slots (8) than data rows (10), so the parts are a scrolling window:
// updateParts maps part i to data row (mTopSelectableIdx + i - 1), and the row a
// given part shows changes as the cursor scrolls. That mapping is why the state
// poses are (re)applied from the parts side every frame rather than once per
// rebuild - the same approach HintSystem's custom shine list uses. Re-applying
// is free: State/OnOff are zero-frame visibility poses, so restarting one is
// visually identical to it already running.
void StageSceneStateFileSelect::applySlotRowStyles() {
    CommonVerticalList* list = mOptionsList[MENU_SLOT_LIST];
    if (!list || !list->mListPartsArr)
        return;

    s32 partCount = list->mListPartsNum;
    if (partCount > list->mListPartsArraySize)
        partCount = list->mListPartsArraySize;

    for (s32 physical = 0; physical < partCount; physical++) {
        al::LayoutActor* item = list->mListPartsArr[physical];
        if (!item || !al::isExistPane(item, "TxtNumber"))
            continue;

        // Without a usable image set the well would render its authored
        // CmBaseDummy placeholder, so drop it rather than show that.
        if (!mHasThumbnails)
            setPaneVisibleIfExists(item, "PicDummy", false);
        al::setPaneLocalSize(item, "WinBase", { kSlotBadgeWidth, 35.0f });
        al::setPaneLocalSize(item, "TxtPlay", { kSlotBadgeWidth, 34.0f });
        al::setPaneLocalSize(item, "TxtNumber", { kSlotNumberWidth, 34.0f });

        const s32 row = list->mTopSelectableIdx + physical - 1;
        if (row < 0 || row >= kSlotCount)
            continue;

        // Set unconditionally, not just on rows that show a pill: the parts are
        // recycled as the list scrolls, so a row left unset would inherit the
        // previous occupant's tint.
        al::setPaneVtxColor(item, "TxtPlay", runModeBadgeColor(mSlotRunMode[row]));

        const char* state = "NoData";
        if (mSlotRowState[row] == SlotRowState::Ready)
            state = "Play";
        else if (mSlotRowState[row] == SlotRowState::Damaged)
            state = "Normal";
        al::startAction(item, state, "State");
        // PicComplete is vanilla's "file is finished" star. The rando save's
        // metadata carries no completion flag, so the card never shows one.
        al::startAction(item, "Off", "OnOff");
    }
}

void StageSceneStateFileSelect::initSlotActionMenu(const al::LayoutInitInfo& initInfo) {
    mMenuList[MENU_SLOT_ACTION] = new SimpleLayoutMenu(
        "FileSelectActionMenu", "OptionModCheck", initInfo, 0, false);
    mOptionsList[MENU_SLOT_ACTION] = new CommonVerticalList(
        mMenuList[MENU_SLOT_ACTION], initInfo, true);

    al::setPaneString(mMenuList[MENU_SLOT_ACTION], "TxtOption", MenuText::SLOT_ACTION_TITLE, 0);
    setStandardFooter(mMenuFooters, MENU_SLOT_ACTION, mMenuList[MENU_SLOT_ACTION], initInfo);
    mOptionsList[MENU_SLOT_ACTION]->initDataNoResetSelected(3);
    mOptionsList[MENU_SLOT_ACTION]->addStringData(mMsgList[MENU_SLOT_ACTION]->mBuffer, "TxtContent");
    mOptionsList[MENU_SLOT_ACTION]->setEnableData(mActionEnabled);
    configureList(mOptionsList[MENU_SLOT_ACTION]);
}

void StageSceneStateFileSelect::initDeleteConfirmMenu(const al::LayoutInitInfo& initInfo) {
    mMenuList[MENU_DELETE_CONFIRM] = new SimpleLayoutMenu(
        "FileSelectDeleteMenu", "OptionModCheck", initInfo, 0, false);
    mOptionsList[MENU_DELETE_CONFIRM] = new CommonVerticalList(
        mMenuList[MENU_DELETE_CONFIRM], initInfo, true);

    al::setPaneString(mMenuList[MENU_DELETE_CONFIRM], "TxtOption", MenuText::SLOT_DELETE_TITLE, 0);
    setStandardFooter(mMenuFooters, MENU_DELETE_CONFIRM, mMenuList[MENU_DELETE_CONFIRM], initInfo);
    mOptionsList[MENU_DELETE_CONFIRM]->initDataNoResetSelected(2);
    mOptionsList[MENU_DELETE_CONFIRM]->addStringData(mMsgList[MENU_DELETE_CONFIRM]->mBuffer, "TxtContent");
    mOptionsList[MENU_DELETE_CONFIRM]->setEnableData(mDeleteEnabled);
    configureList(mOptionsList[MENU_DELETE_CONFIRM]);
}

void StageSceneStateFileSelect::initGenerateProgressMenu(const al::LayoutInitInfo& initInfo) {
    mMenuList[MENU_GENERATE_PROGRESS] = new SimpleLayoutMenu(
        "FileSelectGenerateMenu", "OptionModCheck", initInfo, 0, false);
    mOptionsList[MENU_GENERATE_PROGRESS] = new CommonVerticalList(
        mMenuList[MENU_GENERATE_PROGRESS], initInfo, true);

    al::setPaneString(mMenuList[MENU_GENERATE_PROGRESS], "TxtOption", MenuText::GENERATE_TITLE, 0);
    setStandardFooter(mMenuFooters, MENU_GENERATE_PROGRESS, mMenuList[MENU_GENERATE_PROGRESS], initInfo);
    mOptionsList[MENU_GENERATE_PROGRESS]->initDataNoResetSelected(2);
    mOptionsList[MENU_GENERATE_PROGRESS]->addStringData(mMsgList[MENU_GENERATE_PROGRESS]->mBuffer, "TxtContent");
    mOptionsList[MENU_GENERATE_PROGRESS]->setEnableData(mGenerateEnabled);
    configureList(mOptionsList[MENU_GENERATE_PROGRESS]);
}

void StageSceneStateFileSelect::initInfoBox(const al::LayoutInitInfo& initInfo) {
    // Standalone description panel (overlay), shown only on the settings pages.
    // Reuses the menu layout's TxtOption pane, moved down toward the footer; its
    // window panel doubles as the panel background. The title's draw hook only
    // draws this kit group while the FileSelect nerve is active, and showInfoBox()
    // gates the text, so it doesn't leak onto the slot list / generate screens.
    // The list/footer panes are hidden so TxtOption acts as a compact info box
    // above the normal controls footer.
    for (s32 i = 0; i < kInfoMaxLines; i++) {
        mInfoBox[i] = new SimpleLayoutMenu(
            "FileSelectInfoBox", "OptionModCheck", initInfo, 0, false);
        applyInfoBoxLineLayout(mInfoBox[i], i);
        al::setPaneString(mInfoBox[i], "TxtOption", MenuText::EMPTY, 0);
        setInfoLineVisible(mInfoBox[i], i, false);
    }
}

void StageSceneStateFileSelect::refreshSlotMeta() {
    for (s32 i = 0; i < kSlotCount; i++) {
        const s32 slot = i + 1;
        mSlotHeader[i] = {};
        mSlotMeta[i] = {};
        const bool hasHeader
            = RandoSaveFile::readHeader(slot, &mSlotHeader[i], &mSlotMeta[i], mSaveCategory);
        mSlotCompatible[i] = hasHeader && isHeaderCompatible(mSlotHeader[i]) && !mSlotLoadFailed[i];
        if (mSlotCompatible[i]) {
            const Randomizer::RunMode mode = RandoSaveFile::getRunMode(mSlotHeader[i], &mSlotMeta[i]);
            mSlotCompatible[i] = Bingo::validate(
                slot, mSlotHeader[i].seed, static_cast<u8>(mode), mSaveCategory);
        }
        mSlotHasData[i] = hasHeader
            || fileExists(makeRandoPath(slot, mSaveCategory).cstr())
            || fileExists(makeFilePath(slot, mSaveCategory).cstr());
        if (mSlotHasData[i]) {
            Randomizer::logPush(
                "FS SLOT %d header=%d magic=%08x ver=%u size=%u links=%d sideOK=%d fail=%d file=%d",
                slot, hasHeader, mSlotHeader[i].magic, mSlotHeader[i].version,
                mSlotHeader[i].fileSize, mSlotHeader[i].entranceLinkCount,
                mSlotCompatible[i], mSlotLoadFailed[i],
                fileExists(makeFilePath(slot, mSaveCategory).cstr()));
        }
        if (mSlotHasData[i]) {
            RandoSaveFile::Meta preview = mSlotMeta[i];
            if (tryReadSavePreview(mGameDataHolder, slot, &preview, mSaveCategory)) {
                Randomizer::logPush(
                    "file-select preview slot=%d compatible=%d sidecar=%d/%d file=%d/%d",
                    slot, (int)mSlotCompatible[i],
                    mSlotMeta[i].totalMoons, mSlotMeta[i].currentWorldId,
                    preview.totalMoons, preview.currentWorldId);
                mSlotMeta[i] = preview;
            } else if (mSlotCompatible[i] && fileExists(makeFilePath(slot, mSaveCategory).cstr())) {
                mSlotCompatible[i] = false;
                Randomizer::logPush("file-select preview failed slot=%d sidecar=%d/%d",
                    slot, mSlotMeta[i].totalMoons, mSlotMeta[i].currentWorldId);
            }
        }
    }
}

void StageSceneStateFileSelect::rebuildSlotList() {
    MpDebugLog::write("rebuildSlotList: mSaveCategory=%d (0=SinglePlayer,1=Multiplayer)",
        (int)mSaveCategory);
    al::setPaneString(mMenuList[MENU_SLOT_LIST], "TxtOption",
        mSaveCategory == SaveRedirect::Category::Multiplayer
            ? MenuText::SLOT_LIST_TITLE_MULTIPLAYER
            : MenuText::SLOT_LIST_TITLE_SINGLEPLAYER,
        0);

    for (s32 i = 0; i < kMaxMsgCount; i++) {
        mMsgList[MENU_SLOT_LIST]->mBuffer[i].copy(MenuText::EMPTY);
        mSlotWorldMsg->mBuffer[i].copy(MenuText::EMPTY);
        mSlotShineMsg->mBuffer[i].copy(MenuText::EMPTY);
        mSlotDayMsg->mBuffer[i].copy(MenuText::EMPTY);
        mSlotBadgeMsg->mBuffer[i].copy(MenuText::EMPTY);
    }

    for (s32 i = 0; i < kSlotCount; i++) {
        const s32 slot = i + 1;
        mMsgList[MENU_SLOT_LIST]->mBuffer[i].format(MenuText::SLOT_NUMBER_FORMAT, slot);
        // Reset rather than leave the previous refresh's value behind: a slot
        // that has just been deleted still gets its tint applied by
        // applySlotRowStyles, even though its pill is hidden.
        mSlotRunMode[i] = Randomizer::RunMode::Standard;

        // The timestamp line is hidden by the NoData pose on an empty slot, so
        // it is only worth reading for a slot that has something in it.
        nn::time::CalendarTime lastPlay = {};
        if (mSlotHasData[i] && tryGetLastPlayTime(slot, mSaveCategory, &lastPlay)) {
            mSlotDayMsg->mBuffer[i].format(MenuText::SLOT_DATE_FORMAT,
                lastPlay.month, lastPlay.day, lastPlay.year,
                lastPlay.hour, lastPlay.minute);
        }

        // currentWorldId is the LOGICAL world id, which is exactly how
        // WorldMeta indexes - do not remap it through the scenario id.
        const s32 worldId = mSlotMeta[i].currentWorldId;
        mSlotThumbnail[i]
            = (mSlotCompatible[i] && worldId >= 0 && worldId < WorldMeta::sWorldCount)
            ? mWorldThumbnail[worldId]
            : mEmptyThumbnail;

        if (mSlotCompatible[i]) {
            // The run mode joins the file-number line - the one line on the card
            // with room to spare - and tints the seed pill's text to match.
            mSlotRunMode[i] = RandoSaveFile::getRunMode(mSlotHeader[i], &mSlotMeta[i]);
            setRow(mMsgList[MENU_SLOT_LIST]->mBuffer[i], MenuText::SLOT_MODE_FORMAT, slot,
                Randomizer::getRunModeName(mSlotRunMode[i]));

            mSlotWorldMsg->mBuffer[i].copy(WorldMeta::getDisplayName16ForWorldId(worldId));
            setRow(mSlotShineMsg->mBuffer[i], MenuText::SLOT_TOTAL_MOONS_FORMAT,
                mSlotMeta[i].totalMoons);
            setRow(mSlotBadgeMsg->mBuffer[i], MenuText::SLOT_SEED_FORMAT, mSlotHeader[i].seed);
            mSlotRowState[i] = SlotRowState::Ready;
        } else if (mSlotHasData[i]) {
            mSlotWorldMsg->mBuffer[i].copy(MenuText::SLOT_INCOMPATIBLE);
            mSlotRowState[i] = SlotRowState::Damaged;
        } else {
            mSlotWorldMsg->mBuffer[i].copy(MenuText::SLOT_NEW_GAME);
            mSlotRowState[i] = SlotRowState::Empty;
        }
    }

    bindSlotList();
    // The card layout has fewer physical parts than the ten data rows. Passing
    // -1 lets CommonVerticalList choose a scroll origin that keeps a selected
    // row inside that physical window; forcing top=0 makes returning from a
    // delete on the lower slots pass a null part to updateCursorPos().
    mOptionsList[MENU_SLOT_LIST]->setSelectedIdx(
        clampSelectedSlotIndex(mSelectedSlot), -1);
    mOptionsList[MENU_SLOT_LIST]->updateParts();
    applySlotRowStyles();
}

void StageSceneStateFileSelect::rebuildSlotAction() {
    for (s32 i = 0; i < kMaxMsgCount; i++) {
        mMsgList[MENU_SLOT_ACTION]->mBuffer[i].copy(MenuText::EMPTY);
        mActionEnabled[i] = false;
    }

    const s32 idx = mSelectedSlot - 1;
    if (idx >= 0 && idx < kSlotCount && mSlotCompatible[idx]) {
        const char* mode = Randomizer::getRunModeName(
            RandoSaveFile::getRunMode(mSlotHeader[idx], &mSlotMeta[idx]));
        sead::WFixedSafeString<64> mode16;
        mode16.convertFromMultiByteString(mode, std::strlen(mode));
        sead::WFixedSafeString<128> title;
        title.format(MenuText::SLOT_MODE_FORMAT_WIDE, mSelectedSlot, mode16.cstr());
        al::setPaneString(mMenuList[MENU_SLOT_ACTION], "TxtOption", title.cstr(), 0);
    } else {
        al::setPaneString(mMenuList[MENU_SLOT_ACTION], "TxtOption", MenuText::SLOT_ACTION_TITLE, 0);
    }
    if (idx >= 0 && idx < kSlotCount && mSlotCompatible[idx]) {
        mActionCount = 3;
        mMsgList[MENU_SLOT_ACTION]->mBuffer[0].copy(MenuText::SLOT_LOAD);
        mMsgList[MENU_SLOT_ACTION]->mBuffer[1].copy(MenuText::SLOT_DELETE);
        mMsgList[MENU_SLOT_ACTION]->mBuffer[2].copy(MenuText::SLOT_BACK);
        mActionEnabled[0] = true;
        mActionEnabled[1] = true;
        mActionEnabled[2] = true;
    } else if (idx >= 0 && idx < kSlotCount && mSlotHasData[idx]) {
        mActionCount = 2;
        mMsgList[MENU_SLOT_ACTION]->mBuffer[0].copy(MenuText::SLOT_DELETE);
        mMsgList[MENU_SLOT_ACTION]->mBuffer[1].copy(MenuText::SLOT_BACK);
        mActionEnabled[0] = true;
        mActionEnabled[1] = true;
    } else {
        mActionCount = 2;
        mMsgList[MENU_SLOT_ACTION]->mBuffer[0].copy(MenuText::SLOT_NEW_GAME);
        mMsgList[MENU_SLOT_ACTION]->mBuffer[1].copy(MenuText::SLOT_BACK);
        mActionEnabled[0] = true;
        mActionEnabled[1] = true;
    }

    // Rebind through the list API so its internal row count and draw state stay
    // in sync with the shorter action list.
    bindStringList(mOptionsList[MENU_SLOT_ACTION], mMsgList[MENU_SLOT_ACTION],
        mActionCount, mActionEnabled);
    mOptionsList[MENU_SLOT_ACTION]->setSelectedIdx(0, 0);
    mOptionsList[MENU_SLOT_ACTION]->updateParts();
    configureList(mOptionsList[MENU_SLOT_ACTION]);
}

// This dialog is reused for every irreversible confirm in this scene (not
// just deleting a save slot) — see ConfirmAction. mConfirmAction /
// mConfirmPresetIndex (and, for OverwriteUserPreset, mPendingPresetName) are
// set by the caller right before rebuildDeleteConfirm() + startMenu().
void StageSceneStateFileSelect::rebuildDeleteConfirm() {
    for (s32 i = 0; i < kMaxMsgCount; i++) {
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[i].copy(MenuText::EMPTY);
        mDeleteEnabled[i] = false;
    }

    const char16_t* title = MenuText::SLOT_DELETE_TITLE;
    switch (mConfirmAction) {
    case ConfirmAction::DeleteSlot:
        title = MenuText::SLOT_DELETE_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_DELETE_SLOT_FORMAT, mSelectedSlot);
        break;
    case ConfirmAction::ApplyDefaultPreset:
        title = MenuText::DLG_APPLY_PRESET_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_APPLY_PRESET_FORMAT, Presets::get(mConfirmPresetIndex).name);
        break;
    case ConfirmAction::ApplyUserPreset:
        title = MenuText::DLG_APPLY_PRESET_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_APPLY_PRESET_FORMAT, UserPresets::getName(mConfirmPresetIndex));
        break;
    case ConfirmAction::DeleteUserPreset:
        title = MenuText::DLG_DELETE_PRESET_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_DELETE_PRESET_FORMAT, UserPresets::getName(mConfirmPresetIndex));
        break;
    case ConfirmAction::OverwriteUserPreset:
        title = MenuText::DLG_OVERWRITE_PRESET_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_OVERWRITE_PRESET_FORMAT, mPendingPresetName);
        break;
    case ConfirmAction::RedistributeAllocation:
        title = MenuText::DLG_REDISTRIBUTE_TITLE;
        // The dialog only appears over budget, so the third number is the
        // excess that has to come back off somewhere.
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_REDISTRIBUTE_MOONS_FORMAT,
            mSubPageBalanceAssigned, mSubPageBalanceRequired,
            mSubPageBalanceAssigned - mSubPageBalanceRequired);
        break;
    case ConfirmAction::RedistributeTripleMoons:
        title = MenuText::DLG_REDISTRIBUTE_TITLE;
        mMsgList[MENU_DELETE_CONFIRM]->mBuffer[0].format(
            MenuText::DLG_REDISTRIBUTE_TRIPLE_MOONS_FORMAT,
            mSubPageBalanceAssigned, mSubPageBalanceRequired,
            mSubPageBalanceAssigned - mSubPageBalanceRequired);
        break;
    }
    al::setPaneString(mMenuList[MENU_DELETE_CONFIRM], "TxtOption", title, 0);

    mMsgList[MENU_DELETE_CONFIRM]->mBuffer[1].copy(
        mConfirmAction == ConfirmAction::DeleteSlot ? MenuText::SLOT_BACK : MenuText::SLOT_CANCEL);
    mDeleteEnabled[0] = true;
    mDeleteEnabled[1] = true;
    bindStringList(mOptionsList[MENU_DELETE_CONFIRM], mMsgList[MENU_DELETE_CONFIRM],
        2, mDeleteEnabled);
    mOptionsList[MENU_DELETE_CONFIRM]->setSelectedIdx(0, 0);
    mOptionsList[MENU_DELETE_CONFIRM]->updateParts();
    configureList(mOptionsList[MENU_DELETE_CONFIRM]);
}

// ============================================================================
// Settings pages (one reconfigurable menu shared by all pages)
// ============================================================================

void StageSceneStateFileSelect::initSettingsMenu(const al::LayoutInitInfo& initInfo) {
    const Menu menu = MENU_SETTINGS;
    mMenuList[menu] = new SimpleLayoutMenu("FileSelectSettings", "OptionModCheck", initInfo, 0, false);
    mOptionsList[menu] = new CommonVerticalList(mMenuList[menu], initInfo, true);
    // configureScrollableList() clamps this permanently, so record what the
    // archive actually provides first - the grid pages need all of it back.
    mFullListPartsNum = mOptionsList[menu]->mListPartsNum;
    if (mOptionsList[menu]->mListPartsArr && mOptionsList[menu]->mListPartsArraySize > 1
        && mOptionsList[menu]->mListPartsArr[1])
        mSingleColumnPaneX = al::getLocalTrans(mOptionsList[menu]->mListPartsArr[1]).x;
    StageSceneStateModConfig::configureScrollableList(mOptionsList[menu]);

    al::setPaneString(mMenuList[menu], "TxtOption", MenuText::SLOT_NEW_FILE_TITLE, 0);
    setStandardFooter(mMenuFooters, menu, mMenuList[menu], initInfo);
    // Settings-specific footer, which also advertises the L/R page switch.
    if (mMenuFooters[menu])
        mMenuFooters[menu]->changeText(StageSceneStateModConfig::makeSettingsFooterText(
            false, false, false));
    // Size once for the largest page; loadPage() rebinds the active row count.
    mOptionsList[menu]->initDataNoResetSelected(kMaxPageOptions);

    // One roll array reused across pages, refilled by loadPage().
    mSettingsRollParts = new RollPartsData[kMaxPageOptions];
    for (s32 i = 0; i < kMaxPageOptions; i++)
        mSettingsRollParts[i] = { 0, MenuText::ROLL_EMPTY, 0, true };
    mOptionsList[menu]->setRollPartsData(mSettingsRollParts);
    mOptionsList[menu]->addStringData(mMsgList[menu]->mBuffer, "TxtContent");
    mOptionsList[menu]->startLoopActionAll("Loop", "Loop");
    mOptionsList[menu]->setEnableData(mSettingsEnable);
}

StageSceneStateFileSelect::OptKind StageSceneStateFileSelect::rowKind(
    const OptionDef& option) {
    if (isHintCurrencyOption(option.id) && !Randomizer::isHintTokenMode())
        return KIND_DISPLAY;
    return option.kind;
}

void StageSceneStateFileSelect::setSettingsRollPart(s32 row, const OptionDef& o) {
    if (!mSettingsRollParts || row < 0 || row >= kMaxPageOptions
        || rowKind(o) != KIND_ROLL)
        return;

    if (o.id == OPT_PRESET) {
        mSettingsRollParts[row] = { Presets::count(), o.rollText,
            optionRollIdx(o.id), false };
        return;
    }

    if (o.id == OPT_USER_PRESET) {
        const s32 userCount = UserPresets::count();
        mSettingsRollParts[row] = userCount > 0
            ? RollPartsData { userCount, sUserPresetRollText,
                  optionRollIdx(o.id), false }
            : RollPartsData { MenuText::ROLL_NO_USER_PRESET_COUNT,
                  MenuText::ROLL_NO_USER_PRESET, 0, false };
        return;
    }

    const char16_t* const* rollText = o.rollText;
    s32 rollCount = o.rollCount;
    if (isMoonAllocationKingdomOption(o.id)) {
        ensureMoonTargetRollText();
        rollText = sMoonTargetRollText;
        rollCount = kMoonTargetRollCount;
    } else if (isTripleMoonKingdomOption(o.id)) {
        rollText = sTripleMoonTargetRollText;
        rollCount = sizeof(sTripleMoonTargetRollText)
            / sizeof(sTripleMoonTargetRollText[0]);
    } else if (o.id == OPT_TRIPLE_MOONS) {
        rollCount = Randomizer::isKingdomTripleMoonTargetCustom() ? 3 : 2;
    } else if (isKingdomMoonUseOption(o.id)) {
        const s32 worldId = kingdomMoonUseIndex(o.id);
        rollText = MenuText::ROLL_KINGDOM_MOON_USE;
        // None and Custom sit past the named values, each offered only while it
        // is what the kingdom actually holds - neither can be rolled onto.
        rollCount = kingdomMoonUseNamedCount(worldId);
        if (isKingdomMoonUseEmpty(worldId))
            rollCount += 1;
        else if (isKingdomMoonUseCustom(worldId))
            rollCount += 2;
    } else if (o.id == OPT_VISITED_KINGDOMS_MAP) {
        // Custom has no editor behind it here; it is only offered while a
        // loaded save or preset is already sitting on it.
        rollCount = Randomizer::getVisitedKingdomsMapMode()
                == Randomizer::VisitedKingdomsMapMode_Custom
            ? MenuText::ROLL_VISITED_KINGDOMS_MAP_CUSTOM_COUNT
            : MenuText::ROLL_VISITED_KINGDOMS_MAP_COUNT;
    } else if (o.id == OPT_MOON_REQ_DIFFICULTY) {
        rollCount = isMoonRequirementDeviationCustom()
            ? MenuText::ROLL_MOON_REQ_DIFFICULTY_CUSTOM_COUNT
            : MenuText::ROLL_MOON_REQ_DIFFICULTY_COUNT;
    } else if (o.id == OPT_REQUIREMENT_KINGDOMS) {
        // Custom only exists while the set actually differs from every named
        // preset.
        rollCount = requirementKingdomsPresetIndex() == 5
            ? MenuText::ROLL_REQUIREMENT_KINGDOMS_COUNT
            : MenuText::ROLL_REQUIREMENT_KINGDOMS_COUNT - 1;
    } else if (o.id == OPT_CAPTURE_GRANT_KINGDOMS) {
        rollCount = grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Captures) == 1
            ? MenuText::ROLL_CAPTURE_GRANT_KINGDOMS_COUNT
            : MenuText::ROLL_CAPTURE_GRANT_KINGDOMS_COUNT - 1;
    } else if (o.id == OPT_ABILITY_GRANT_KINGDOMS) {
        rollCount = grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Abilities) == 1
            ? MenuText::ROLL_ABILITY_GRANT_KINGDOMS_COUNT
            : MenuText::ROLL_ABILITY_GRANT_KINGDOMS_COUNT - 1;
    } else if (o.id == OPT_LOCKED_ABILITIES) {
        rollCount = isAbilityLockPoolCustom()
            ? MenuText::ROLL_LOCKED_ABILITIES_COUNT
            : MenuText::ROLL_LOCKED_ABILITIES_COUNT - 1;
    } else if (o.id == OPT_LOCKED_CAPTURES) {
        rollCount = isCaptureLockPoolCustom()
            ? MenuText::ROLL_LOCKED_CAPTURES_COUNT
            : MenuText::ROLL_LOCKED_CAPTURES_COUNT - 1;
    } else if (o.id == OPT_STARTING_EQUIPMENT) {
        rollCount = Randomizer::isStartingEquipmentCustom() ? 4 : 3;
    } else if (o.id == OPT_PURPLE_SHOP_PRICE
        && Randomizer::isProportionalKingdomPricingEnabled()) {
        rollText = MenuText::ROLL_PURPLE_SHOP_PRICE_PERCENT;
        rollCount = MenuText::ROLL_PURPLE_SHOP_PRICE_PERCENT_COUNT;
    }

    if (o.id == OPT_PURPLE_SHOP_PRICE
        && Randomizer::isPurpleShopPriceCustom()) {
        const char16_t* const* fixedText = Randomizer::isProportionalKingdomPricingEnabled()
            ? MenuText::ROLL_PURPLE_SHOP_PRICE_PERCENT
            : MenuText::ROLL_PURPLE_SHOP_PRICE;
        for (s32 i = 0; i < 6; i++)
            mPurpleShopPriceRollText[i] = fixedText[i];
        mPurpleShopPriceCustomLabel.format(
            Randomizer::isProportionalKingdomPricingEnabled()
                ? u"Custom (%d%%)"
                : u"Custom (%d)",
            Randomizer::getPurpleShopCustomPrice());
        mPurpleShopPriceRollText[6] = mPurpleShopPriceCustomLabel.cstr();
        rollText = mPurpleShopPriceRollText;
        rollCount = 7;
    } else if (o.id == OPT_TIMER_START) {
        if (Randomizer::isTimerStartCustom()) {
            rollText = MenuText::ROLL_TIMER_START_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_START_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_TIMER_MOON_BONUS) {
        if (Randomizer::isTimerMoonBonusCustom()) {
            rollText = MenuText::ROLL_TIMER_MOON_BONUS_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_MOON_BONUS_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_TIMER_TRIPLE_BONUS) {
        if (Randomizer::isTimerTripleBonusCustom()) {
            rollText = MenuText::ROLL_TIMER_TRIPLE_BONUS_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_TRIPLE_BONUS_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_TIMER_DAMAGE_PENALTY) {
        if (Randomizer::isTimerDamagePenaltyCustom()) {
            rollText = MenuText::ROLL_TIMER_DAMAGE_PENALTY_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_DAMAGE_PENALTY_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_TIMER_DEATH_PENALTY) {
        if (Randomizer::isTimerDeathPenaltyCustom()) {
            rollText = MenuText::ROLL_TIMER_DEATH_PENALTY_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_DEATH_PENALTY_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_TIMER_HALF_LIFE) {
        if (Randomizer::isTimerHalfLifeCustom()) {
            rollText = MenuText::ROLL_TIMER_HALF_LIFE_CUSTOM;
            rollCount = MenuText::ROLL_TIMER_HALF_LIFE_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_SOFT_NUZLOCKE_LOSS) {
        if (Randomizer::isSoftNuzlockeLossCustom()) {
            rollText = MenuText::ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM;
            rollCount = MenuText::ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_SOFT_NUZLOCKE_ABILITY_LOSS) {
        if (Randomizer::isSoftNuzlockeAbilityLossCustom()) {
            rollText = MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM;
            rollCount = MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_SOFT_NUZLOCKE_CAPTURE_LOSS) {
        if (Randomizer::isSoftNuzlockeCaptureLossCustom()) {
            rollText = MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM;
            rollCount = MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT;
        }
    } else if (o.id == OPT_HINT_PRICE_MOONS || o.id == OPT_HINT_PRICE_CAPTURES
        || o.id == OPT_HINT_PRICE_ABILITIES || o.id == OPT_HINT_PRICE_PATH
        || o.id == OPT_HINT_PRICE_PURCHASES) {
        const Randomizer::HintPriceCategory category = hintCategoryAt(
            static_cast<s32>(o.id) - static_cast<s32>(OPT_HINT_PRICE_MOONS));
        const bool tokens = hintCategoryUsesTokens(category);
        const bool custom = Randomizer::isHintPriceCustom(category);
        if (tokens) {
            rollText = custom ? MenuText::ROLL_HINT_TOKEN_PRICE_CUSTOM
                              : MenuText::ROLL_HINT_TOKEN_PRICE;
            rollCount = custom ? MenuText::ROLL_HINT_TOKEN_PRICE_CUSTOM_COUNT
                               : MenuText::ROLL_HINT_TOKEN_PRICE_COUNT;
        } else {
            rollText = custom ? MenuText::ROLL_HINT_COIN_PRICE_CUSTOM
                              : MenuText::ROLL_HINT_COIN_PRICE;
            rollCount = custom ? MenuText::ROLL_HINT_COIN_PRICE_CUSTOM_COUNT
                               : MenuText::ROLL_HINT_COIN_PRICE_COUNT;
        }
    } else if (isHintCurrencyOption(o.id) && !Randomizer::isHintTokenMode()) {
        // Nothing hands out Hint Tokens, so Coins is the only real answer and
        // the row says so instead of offering a choice that does not exist.
        rollCount = 1;
    } else if (o.id == OPT_ONE_SHEEP_HERDING) {
        rollText = MenuText::ROLL_SHEEP_HERDING;
        rollCount = MenuText::ROLL_SHEEP_HERDING_COUNT;
    }

    // RollParts::setData() dereferences rollText[selected] without checking it,
    // so a derived roll index that outran its own value list (or a table that
    // never got filled in) crashed inside al::setPaneString. Normalise here,
    // where every roll on every page passes through.
    if (!rollText || rollCount <= 0) {
        rollText = MenuText::ROLL_EMPTY;
        rollCount = MenuText::ROLL_EMPTY_COUNT;
    }
    s32 selected = optionRollIdx(o.id);
    if (selected < 0 || selected >= rollCount)
        selected = 0;

    mSettingsRollParts[row] = { rollCount, rollText, selected, false };
}

// Full rebuild of the shared settings menu for one logical page. The data table
// stores every possible row, while the list only receives rows whose current
// visibility rules allow them.
// One place that composes the settings menu's title. loadPage() and
// refreshPage() both write it, and refreshPage() runs on the first step of
// exeNewSaveSettings - so its own private copy of this logic, which had no
// sub-page counter, overwrote a freshly opened editor's "(1/3)" before the
// player ever saw it.
void StageSceneStateFileSelect::composeSettingsTitle(s32 pageIndex,
    sead::WFixedSafeString<0x100>* out) {
    if (!out)
        return;
    const PageDef& pd = pageDef(pageIndex);

    if (mSubPage != SubPage::None) {
        // A sub-page is not part of the outer page counter, but a paged one
        // still says which of its own pages you are on.
        out->copy(pd.title);
        const s32 subPages = subPagePageCount(mSubPage);
        if (subPages > 1) {
            out->appendWithFormat(MenuText::PAGE_COUNTER_FORMAT,
                mSubPageParam + 1, subPages);
        }
        if (mSubPageBalanceAssigned >= 0 && mSubPageBalanceRequired >= 0) {
            out->appendWithFormat(MenuText::ADVANCED_BALANCE_FORMAT,
                mSubPageBalanceAssigned, mSubPageBalanceRequired,
                mSubPageBalanceRequired - mSubPageBalanceAssigned);
        }
        return;
    }

    out->copy(pd.title);
    out->appendWithFormat(MenuText::PAGE_COUNTER_FORMAT,
        visiblePageOrdinal(pageIndex), visiblePageCount());
}

void StageSceneStateFileSelect::setListColumnMode(bool twoColumn) {
    CommonVerticalList* list = mOptionsList[MENU_SETTINGS];
    if (!list || !list->mListPartsArr)
        return;

    // applySettingsRowStyles() hides every physical part and then re-shows the
    // first mListPartsNum of them, so widening the window here is what makes
    // the extra grid rows visible at all.
    const s32 wanted = twoColumn && mFullListPartsNum > 0 ? mFullListPartsNum : 0;
    if (wanted > 0) {
        list->mListPartsNum = wanted;
    } else {
        StageSceneStateModConfig::configureScrollableList(list);
        // Undo any sideways pose a grid page left behind. Only X is ever
        // changed, and the list owns Y, so putting X back is enough. Index 0
        // is not a data row and repositionTwoColumnPanes never touches it.
        for (s32 i = 1; i < list->mListPartsArraySize; i++) {
            al::LayoutActor* item = list->mListPartsArr[i];
            if (!item)
                continue;
            const sead::Vector2f trans = al::getLocalTrans(item);
            al::setLocalTrans(item,
                sead::Vector2f(mSingleColumnPaneX, trans.y));
        }
    }

    // The scroll offset means nothing in the other mode, and a stale one would
    // make applySettingsRowStyles() pose the wrong rows. The cursor itself is
    // set by loadPage() right after this.
    list->mTopSelectableIdx = 0;
}

void StageSceneStateFileSelect::loadPage(s32 pageIndex) {
    if (pageIndex < 0)
        pageIndex = 0;
    if (pageIndex >= kPageCount)
        pageIndex = kPageCount - 1;

    const Menu menu = MENU_SETTINGS;
    CommonVerticalList* list = mOptionsList[menu];
    if (!list)
        return;

    // A visibility-triggered rebuild should feel like an in-place update. Save
    // the selected option before replacing the row map so revealing a row
    // below the cursor does not throw the cursor back to row zero.
    const bool preserveCursor = pageIndex == mCurrentPage && mCurrentList == list;
    const s32 previousRow = list->mCurSelected;
    OptionId previousId = OPT_NONE;
    if (preserveCursor && previousRow >= 0 && previousRow < mRowCount) {
        const PageDef& previousPage = pageDef(mCurrentPage);
        const s32 previousOption = mRowMap[previousRow];
        if (previousOption >= 0 && previousOption < previousPage.count)
            previousId = previousPage.options[previousOption].id;
    }

    mCurrentPage = pageIndex;
    const bool subPageActive = mSubPage != SubPage::None;
    if (subPageActive)
        updateSubPageRows();
    const PageDef& pd = pageDef(pageIndex);
    mLoadedPageOptions = pd.options;

    mRowCount = 0;
    mVisibleRowSignatureCount = 0;
    mVisibleRowSignatureXor = 0;
    for (s32 i = 0; i < kMaxPageOptions; i++)
        mRowMap[i] = -1;

    for (s32 optionIndex = 0; optionIndex < pd.count; optionIndex++) {
        const OptionDef& o = pd.options[optionIndex];
        if (!optionVisible(o.id))
            continue;

        // Keep the write bounded even if a future page grows without updating
        // kMaxPageOptions. The static page-size assertions catch known large
        // tables; this guard prevents a bad table from corrupting the adjacent
        // roll-parts/layout state at runtime.
        if (mRowCount >= kMaxPageOptions)
            break;

        mRowMap[mRowCount++] = optionIndex;
        mVisibleRowSignatureXor ^= static_cast<u32>(o.id);
    }
    mVisibleRowSignatureCount = mRowCount;

    sead::WFixedSafeString<0x100> title;
    composeSettingsTitle(pageIndex, &title);
    al::setPaneString(mMenuList[menu], "TxtOption", title.cstr(), 0);

    for (s32 i = 0; i < kMaxMsgCount; i++)
        mMsgList[menu]->mBuffer[i].copy(MenuText::EMPTY);
    for (s32 i = 0; i < kMaxPageOptions; i++) {
        mSettingsEnable[i] = false;
        mSettingsRollParts[i] = { 0, MenuText::ROLL_EMPTY, 0, true };
    }

    if (list->mListPartsArr) {
        for (s32 physical = 0; physical < list->mListPartsArraySize; physical++) {
            al::LayoutActor* item = list->mListPartsArr[physical];
            if (!item)
                continue;

            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Base,
                pd.columns >= 2);
            al::startAction(item, "Hide", nullptr);
        }
    }

    for (s32 row = 0; row < mRowCount; row++) {
        const s32 optionIndex = mRowMap[row];
        if (optionIndex < 0 || optionIndex >= pd.count)
            continue;
        const OptionDef& o = pd.options[optionIndex];

        if (o.id == OPT_CURRENT_SEED)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_CURRENT_SEED_FORMAT,
                Randomizer::getSeed());
        else if (o.id == OPT_MOON_REQUIREMENT)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_MOON_REQUIREMENT_FORMAT,
                Randomizer::getMoonRequirement());
        else if (o.id == OPT_MOON_REQ_DEVIATION)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_MOON_REQ_DEVIATION_FORMAT,
                Randomizer::getMoonRequirementDeviation());
        else if (isHintCurrencyOption(o.id) && rowKind(o) == KIND_DISPLAY) {
            mMsgList[menu]->mBuffer[row].copy(o.label);
            mMsgList[menu]->mBuffer[row].append(u" ");
            mMsgList[menu]->mBuffer[row].append(MenuText::ROLL_HINT_CURRENCY[0]);
        } else if (o.id == OPT_TOADETTE_MEDAL_AVAILABLE)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_MEDALS_AVAILABLE_FORMAT,
                Randomizer::getCustomToadetteMedalAvailable());
        else if (o.id == OPT_TOADETTE_MEDAL_REQUIRED)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_MEDALS_REQUIRED_FORMAT,
                Randomizer::getCustomToadetteMedalRequired());
        else if (o.id == OPT_TRACKER_ROOM_CODE)
            setRow(mMsgList[menu]->mBuffer[row], MenuText::ROW_ROOM_CODE_FORMAT,
                TrackerBridge::getRoomCodeDisplayText());
        else
            mMsgList[menu]->mBuffer[row].copy(o.label);

        if (rowKind(o) == KIND_ROLL)
            setSettingsRollPart(row, o);

        // Visibility filtering leaves no disabled settings rows. Keep this
        // array populated because CommonVerticalList still requires a valid
        // enable-data pointer for its row machinery.
        mSettingsEnable[row] = true;
    }

    // Two columns only when the page asks for it and has something to split.
    const bool twoColumn = pd.columns >= 2 && mRowCount > 1;
    mCurrentColSize = twoColumn ? (mRowCount + 1) / 2 : 0;
    setListColumnMode(twoColumn);

    list->initDataNoResetSelected(mRowCount);
    list->setRollPartsData(mSettingsRollParts); // copies selected idx into mRollPartsSelected
    list->addStringData(mMsgList[menu]->mBuffer, "TxtContent");
    list->startLoopActionAll("Loop", "Loop");
    list->setEnableData(mSettingsEnable);
    if (mRowCount > 0) {
        s32 selectedRow = 0;
        if (preserveCursor) {
            bool foundPreviousOption = false;
            for (s32 row = 0; row < mRowCount; row++) {
                const s32 optionIndex = mRowMap[row];
                if (optionIndex >= 0 && optionIndex < pd.count
                    && pd.options[optionIndex].id == previousId) {
                    selectedRow = row;
                    foundPreviousOption = true;
                    break;
                }
            }
            if (!foundPreviousOption)
                // If the old option disappeared, retain its row where possible
                // and clamp to the nearest lower row otherwise.
                selectedRow = previousRow < mRowCount ? previousRow : mRowCount - 1;
            if (selectedRow < 0)
                selectedRow = 0;
        }
        list->setSelectedIdx(selectedRow, 0);
    }
    list->updateParts();
    applySettingsRowStyles();
}

void StageSceneStateFileSelect::applySettingsRowStyles() {
    const PageDef& pd = pageDef(mCurrentPage);
    CommonVerticalList* list = mOptionsList[MENU_SETTINGS];
    if (!list || !list->mListPartsArr)
        return;

    const bool narrow = mCurrentColSize > 0;

    for (s32 physical = 0; physical < list->mListPartsArraySize; physical++) {
        al::LayoutActor* item = list->mListPartsArr[physical];
        if (!item)
            continue;

        StageSceneStateModConfig::applyRowStyle(
            item, StageSceneStateModConfig::RowStyle::Base, narrow);
        al::startAction(item, "Hide", nullptr);
    }

    s32 partCount = list->mListPartsNum;
    if (partCount > list->mListPartsArraySize)
        partCount = list->mListPartsArraySize;

    // CommonVerticalList recycles these physical parts as the data rows scroll.
    // Resolve the logical row through mRowMap before applying its kind-specific
    // pose, otherwise a recycled checkbox/roll would keep the previous row's
    // visual state.
    for (s32 physical = 0; physical < partCount; physical++) {
        al::LayoutActor* item = list->mListPartsArr[physical];
        if (!item)
            continue;

        al::startAction(item, "Loop", nullptr);
        const s32 row = list->mTopSelectableIdx + physical - 1;
        if (row < 0 || row >= mRowCount)
            continue;
        const s32 optionIndex = mRowMap[row];
        if (optionIndex < 0 || optionIndex >= pd.count)
            continue;
        const OptionDef& o = pd.options[optionIndex];
        switch (rowKind(o)) {
        case KIND_CHECK:
            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Check, narrow);
            al::startAction(item, optionCheckState(o.id) ? "On" : "Off", "State");
            break;
        case KIND_ROLL:
            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Roll, narrow);
            break;
        default:
            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Base, narrow);
            break;
        }
    }
}

// Light refresh of the current page: seed label, roll selections and check
// states. If a setting changed the visible set, this escalates to loadPage(),
// which also rebuilds the row map and preserves the cursor by OptionId.
void StageSceneStateFileSelect::refreshPage() {
    CommonVerticalList* list = mOptionsList[MENU_SETTINGS];
    if (!list)
        return;

    if (mSubPage != SubPage::None)
        updateSubPageRows();

    const PageDef& pd = pageDef(mCurrentPage);
    s32 visibleCount = 0;
    u32 visibleXor = 0;
    for (s32 optionIndex = 0; optionIndex < pd.count; optionIndex++) {
        const OptionDef& o = pd.options[optionIndex];
        if (!optionVisible(o.id))
            continue;
        if (visibleCount >= kMaxPageOptions)
            break;
        visibleCount++;
        visibleXor ^= static_cast<u32>(o.id);
    }
    if (pd.options != mLoadedPageOptions
        || visibleCount != mVisibleRowSignatureCount
        || visibleXor != mVisibleRowSignatureXor) {
        loadPage(mCurrentPage);
        return;
    }

    // Changing the Run Mode adds or removes the Game Mode Settings page, which
    // moves every later page's number, so the counter is rebuilt here too.
    sead::WFixedSafeString<0x100> title;
    composeSettingsTitle(mCurrentPage, &title);
    al::setPaneString(mMenuList[MENU_SETTINGS], "TxtOption", title.cstr(), 0);

    for (s32 row = 0; row < mRowCount; row++) {
        const s32 optionIndex = mRowMap[row];
        if (optionIndex < 0 || optionIndex >= pd.count)
            continue;
        const OptionDef& o = pd.options[optionIndex];
        if (o.id == OPT_CURRENT_SEED)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row], MenuText::ROW_CURRENT_SEED_FORMAT,
                Randomizer::getSeed());
        else if (o.id == OPT_MOON_REQUIREMENT)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row], MenuText::ROW_MOON_REQUIREMENT_FORMAT,
                Randomizer::getMoonRequirement());
        else if (o.id == OPT_MOON_REQ_DEVIATION)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row],
                MenuText::ROW_MOON_REQ_DEVIATION_FORMAT,
                Randomizer::getMoonRequirementDeviation());
        else if (isHintCurrencyOption(o.id) && rowKind(o) == KIND_DISPLAY) {
            mMsgList[MENU_SETTINGS]->mBuffer[row].copy(o.label);
            mMsgList[MENU_SETTINGS]->mBuffer[row].append(u" ");
            mMsgList[MENU_SETTINGS]->mBuffer[row].append(MenuText::ROLL_HINT_CURRENCY[0]);
        } else if (o.id == OPT_TOADETTE_MEDAL_AVAILABLE)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row], MenuText::ROW_MEDALS_AVAILABLE_FORMAT,
                Randomizer::getCustomToadetteMedalAvailable());
        else if (o.id == OPT_TOADETTE_MEDAL_REQUIRED)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row], MenuText::ROW_MEDALS_REQUIRED_FORMAT,
                Randomizer::getCustomToadetteMedalRequired());
        else if (o.id == OPT_TRACKER_ROOM_CODE)
            setRow(mMsgList[MENU_SETTINGS]->mBuffer[row], MenuText::ROW_ROOM_CODE_FORMAT,
                TrackerBridge::getRoomCodeDisplayText());
        // Everything else takes its label verbatim, exactly as loadPage() does.
        // This used to name the two kingdom-target row families instead, so any
        // other row whose label is rebuilt per frame - every numeric row on a
        // sub-page - kept the text loadPage happened to write first and looked
        // stuck even though the setting behind it had changed.
        else
            mMsgList[MENU_SETTINGS]->mBuffer[row].copy(o.label);

        if (rowKind(o) == KIND_ROLL) {
            setSettingsRollPart(row, o);
            const s32 idx = mSettingsRollParts[row].mSelectedIdx;
            if (list->mRollPartsSelected)
                list->mRollPartsSelected[row] = idx;
        }

        mSettingsEnable[row] = true;
    }

    list->setEnableData(mSettingsEnable);
    // updateParts() only re-copies row text into the panes when the list's
    // "strings dirty" flag is set (normally raised by navigation). A value-only
    // refresh like a new seed never moves the selection, so raise it explicitly —
    // otherwise the "Current Seed" label keeps its stale text after Random Seed or
    // a manual seed entry. (The previous addStringData() call here didn't set the
    // flag and leaked a string-data slot on every refresh.)
    list->mStringsDirty = true;
    list->updateParts();
    applySettingsRowStyles();
}

void StageSceneStateFileSelect::enterPage(s32 pageIndex) {
    loadPage(findVisiblePage(pageIndex, 1));
    startMenu(MENU_SETTINGS, &NrvStageSceneStateFileSelect.NewSaveSettings);
    showInfoBox(true);
    updateInfoBox();
}

// Sequential, no wrap-around: clamp at the first/last page. L/R and D-pad
// left/right both land here on a single-column page; a two-column page keeps
// D-pad for column switching and reaches this from L/R only.
void StageSceneStateFileSelect::changePage(s32 delta) {
    s32 idx = mCurrentPage + delta;
    if (idx < 0)
        idx = 0;
    if (idx >= kPageCount)
        idx = kPageCount - 1;
    idx = findVisiblePage(idx, delta < 0 ? -1 : 1);
    if (idx == mCurrentPage)
        return;
    loadPage(idx);
    activateInput();
    updateInfoBox();
}

const StageSceneStateFileSelect::OptionDef* StageSceneStateFileSelect::currentOption() const {
    const PageDef& pd = pageDef(mCurrentPage);
    const s32 i = mCurrentList ? mCurrentList->mCurSelected : -1;
    if (i < 0 || i >= mRowCount)
        return nullptr;
    const s32 optionIndex = mRowMap[i];
    if (optionIndex < 0 || optionIndex >= pd.count)
        return nullptr;
    return &pd.options[optionIndex];
}

void StageSceneStateFileSelect::rollOption(const OptionDef& opt, s32 delta) {
    CommonVerticalList* list = mCurrentList;
    if (!list || !list->RollPartsArr || rowKind(opt) != KIND_ROLL)
        return;

    const s32 row = list->mCurSelected;
    if (delta < 0)
        list->rollLeft();
    else if (delta > 0)
        list->rollRight();

    const s32 idx = list->mRollPartsSelected ? list->mRollPartsSelected[row]
                                             : list->RollPartsArr[row].mSelectedIdx;
    setOptionRollIdx(opt.id, idx);
    list->RollPartsArr[row].mSelectedIdx = idx;

    // A roll can change visibility (or a derived page body), so use the same
    // signature check as checkbox changes rather than relying on positional
    // rows remaining stable.
    refreshPage();
}

void StageSceneStateFileSelect::onDecideOption(const OptionDef& opt) {
    if (mSubPage != SubPage::None && opt.id == OPT_ADVANCED_RESET) {
        resetSubPageDefaults(mSubPage);
        refreshPage();
        activateInput();
        setInfoBoxText(MenuText::DESC_ADVANCED_RESET_NOTICE);
        return;
    }

    // Preset rows roll a preview only (see setOptionRollIdx); applying is an
    // explicit, confirmed action since it overwrites every current setting.
    if (opt.id == OPT_PRESET) {
        mConfirmAction = ConfirmAction::ApplyDefaultPreset;
        mConfirmPresetIndex = mPresetIdx;
        rebuildDeleteConfirm();
        startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
        return;
    }
    if (opt.id == OPT_USER_PRESET) {
        if (UserPresets::count() == 0) {
            activateInput();
            return;
        }
        mConfirmAction = ConfirmAction::ApplyUserPreset;
        mConfirmPresetIndex = userPresetSlotForRoll(mUserPresetIdx);
        rebuildDeleteConfirm();
        startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
        return;
    }

    // Kingdom target rows are rolls for quick +/-1 adjustments, while A on
    // the selected row opens a direct numeric entry. Keep this as a row-level
    // action so the shared KIND_ROLL styling and input path remain intact.
    if (isMoonAllocationKingdomOption(opt.id)
        || isTripleMoonKingdomOption(opt.id)
        || isAdvancedRequirementBaseOption(opt.id)
        || opt.id == OPT_ADVANCED_REQ_TOTAL) {
        al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardSubPageValue);
        return;
    }

    if (mSubPage != SubPage::None
        && (opt.id == OPT_ADVANCED_STARTING_CAPTURES
            || opt.id == OPT_ADVANCED_STARTING_THROWS
            || opt.id == OPT_ADVANCED_STARTING_ABILITIES
            || isAdvancedReqDeviationOption(opt.id)
            || isAdvancedTimerValueOption(opt.id)
            || isAdvancedNuzlockeValueOption(opt.id)
            || opt.id == OPT_ADVANCED_HINT_CUSTOM_PRICE
            || opt.id == OPT_ADVANCED_MINIGAME_LOW
            || opt.id == OPT_ADVANCED_MINIGAME_HIGH)) {
        if (opt.id == OPT_ADVANCED_STARTING_CAPTURES)
            mAdvancedKeyboardField = AdvancedKeyboardField::StartingCaptures;
        else if (opt.id == OPT_ADVANCED_STARTING_THROWS)
            mAdvancedKeyboardField = AdvancedKeyboardField::StartingThrows;
        else if (opt.id == OPT_ADVANCED_STARTING_ABILITIES)
            mAdvancedKeyboardField = AdvancedKeyboardField::StartingAbilities;
        else if (isAdvancedReqDeviationOption(opt.id))
            mAdvancedKeyboardField = AdvancedKeyboardField::MoonRequirementDeviation;
        else if (isAdvancedTimerValueOption(opt.id))
            mAdvancedKeyboardField = AdvancedKeyboardField::TimerValue;
        else if (isAdvancedNuzlockeValueOption(opt.id))
            mAdvancedKeyboardField = AdvancedKeyboardField::NuzlockeValue;
        else if (opt.id == OPT_ADVANCED_HINT_CUSTOM_PRICE)
            mAdvancedKeyboardField = AdvancedKeyboardField::HintCustomPrice;
        else if (opt.id == OPT_ADVANCED_MINIGAME_LOW)
            mAdvancedKeyboardField = AdvancedKeyboardField::MinigameLow;
        else
            mAdvancedKeyboardField = AdvancedKeyboardField::MinigameHigh;
        al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardAdvancedValue);
        return;
    }

    // A on a select row opens its Advanced settings. A has nothing else to do
    // on a roll (the value is cycled with left/right), whereas on a checkbox it
    // already means "toggle" - so checkboxes keep Advanced on Y and only the
    // selects move it to A.
    if (mSubPage == SubPage::None && rowKind(opt) == KIND_ROLL) {
        if (opt.id == OPT_PURPLE_SHOP_PRICE) {
            mAdvancedKeyboardField = AdvancedKeyboardField::PurpleShopPrice;
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardAdvancedValue);
            return;
        }
        SubPage rollSubPage = SubPage::None;
        s32 rollSubPageParam = 0;
        if (advancedFor(opt.id, &rollSubPage, &rollSubPageParam)) {
            enterSubPage(rollSubPage, rollSubPageParam);
            return;
        }
    }

    switch (rowKind(opt)) {
    case KIND_CHECK:
        toggleOption(opt.id);
        refreshPage();
        activateInput();
        updateInfoBox();
        return;
    case KIND_ACTION:
        if (opt.id == OPT_RANDOM_SEED) {
            Randomizer::setSeed(makeRandomSeed());
            refreshPage();
            activateInput();
            return;
        }
        if (opt.id == OPT_SET_SEED) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardSeed);
            return;
        }
        if (opt.id == OPT_MOON_REQUIREMENT) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardMoonReq);
            return;
        }
        if (opt.id == OPT_MOON_REQ_DEVIATION) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardMoonReqDeviation);
            return;
        }
        if (opt.id == OPT_TOADETTE_MEDAL_AVAILABLE) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardMedalAvailable);
            return;
        }
        if (opt.id == OPT_TOADETTE_MEDAL_REQUIRED) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardMedalRequired);
            return;
        }
        if (opt.id == OPT_SAVE_AS_PRESET) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardPresetName);
            return;
        }
        if (opt.id == OPT_TRACKER_ROOM_CODE) {
            al::setNerve(this, &NrvStageSceneStateFileSelect.OpenKeyboardTrackerRoomCode);
            return;
        }
        if (opt.id == OPT_GENERATE_SEED) {
            // A host's MP slot is scratch space every room this console hosts
            // reuses, so whatever the last one left behind has to go before
            // the new seed is built on top of it. board.bin is the one that
            // actually bites: RoomSnapshot::upload sends it whenever the file
            // exists, so a plain room hosted after a board run would otherwise
            // ship the previous run's board to everyone who joined.
            //
            // Seed 0 rather than the real one on purpose - the seed argument
            // only picks which spoiler log to delete alongside the save, and
            // the seed being generated has no log to lose yet.
            if (sMultiplayerHostMode && mSaveCategory == SaveRedirect::Category::Multiplayer) {
                mSelectedSlot = kHostSlot;
                // Category active BEFORE the wipe, not after. deleteSlot()
                // reaches ArrivalLog::remove(), which resolves its path from
                // the ACTIVE category rather than the one it was handed (see
                // ArrivalLog's makePath) - wiping while single-player was
                // still active would delete Saves/Slot1's arrival log instead
                // of this room's. deleteSlot() then clears the active slot it
                // just emptied, which is what the call below puts back.
                SaveRedirect::setActiveSlot(mSelectedSlot, mSaveCategory);
                SaveRedirect::deleteSlot(kHostSlot, 0, SaveRedirect::Category::Multiplayer);
            }
            SaveRedirect::setActiveSlot(mSelectedSlot, mSaveCategory);
            mGenerateFrame = -1;
            mGenerateDoneStep = -1;
            mGenerateStarted = false;
            mGenerateStartFailed = false;
            showInfoBox(false);
            startMenu(MENU_GENERATE_PROGRESS, &NrvStageSceneStateFileSelect.Generate, true);
            return;
        }
        if (opt.id == OPT_JOIN_GAME) {
            // The guest's counterpart to Generate Seed - same progress menu,
            // but the work is a download rather than a generator run.
            mSelectedSlot = kGuestSlot;
            mSaveCategory = SaveRedirect::Category::MultiplayerGuest;
            SaveRedirect::setActiveSlot(mSelectedSlot, mSaveCategory);
            mJoinFrame = -1;
            mJoinDoneStep = -1;
            mJoinFailed = false;
            mJoinFailureIsUpload = false;
            showInfoBox(false);
            startMenu(MENU_GENERATE_PROGRESS, &NrvStageSceneStateFileSelect.JoinRoom, true);
            return;
        }
        activateInput();
        return;
    default:
        // Display / roll rows: A does nothing (rolls use left/right).
        activateInput();
        return;
    }
}

// ── option <-> Randomizer mapping ───────────────────────────────────────────

bool StageSceneStateFileSelect::optionCheckState(OptionId id) const {
    if (isAdvancedCapturePoolOption(id))
        return Randomizer::isCaptureInLockPool(advancedCapturePoolIndex(id));
    if (id == OPT_ADVANCED_PROGRESSIVE_ABILITIES)
        return Randomizer::isProgressiveAbilitiesEnabled();
    if (isAdvancedRequirementKingdomOption(id))
        return isRequirementKingdomEnabled(advancedRequirementKingdomIndex(id));
    if (isAdvancedCaptureGrantOption(id))
        return kingdomGrantsSpecialMoonUse(advancedCaptureGrantIndex(id),
            Randomizer::SpecialMoonUse_Captures);
    if (isAdvancedAbilityGrantOption(id))
        return kingdomGrantsSpecialMoonUse(advancedAbilityGrantIndex(id),
            Randomizer::SpecialMoonUse_Abilities);
    if (isAdvancedAbilityPoolOption(id))
        return isAbilityLockRowOn(advancedAbilityPoolIndex(id));
    if (isAdvancedKingdomMoonUseOption(id)) {
        const s32 selection = advancedKingdomMoonUseIndex(id);
        return Randomizer::isKingdomMoonUseSelected(selection / 7,
            static_cast<Randomizer::SpecialMoonUse>(selection % 7));
    }
    if (isAdvancedStartKingdomOption(id))
        return Randomizer::isStartKingdomSelected(advancedStartKingdomIndex(id));
    if (id == OPT_ADVANCED_HINT_COIN_OVERRIDE) {
        if (mSubPageParam < 0 || mSubPageParam >= 5)
            return false;
        return Randomizer::isHintCoinOverride(
            static_cast<Randomizer::HintPriceCategory>(mSubPageParam));
    }

    switch (id) {
    case OPT_MOONS:
        return Randomizer::isMoonRandoEnabled();
    case OPT_ENABLE_MOON_REALLOCATION:
        return Randomizer::isMoonReallocationEnabled();
    case OPT_RANDOMIZE_MOON_ALLOCATION:
        return Randomizer::isRandomizeMoonAllocationEnabled();
    case OPT_TRIPLE_MOONS_ANYWHERE:
        return Randomizer::isTripleMoonsAnywhereEnabled();
    case OPT_PAINTINGS:
        return Randomizer::isPaintingRandoEnabled();
    case OPT_KINGDOM_ORDER:
        return Randomizer::isKingdomRandoEnabled();
    case OPT_START_KINGDOM_STATE:
        return Randomizer::isStartKingdomStateRandoEnabled();
    case OPT_CHEST_ORDER:
        return Randomizer::isChestOrderRandoEnabled();
    case OPT_COIN_RANDO:
        return Randomizer::isCoinRandoEnabled();
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    case OPT_ENEMY_SWAP:
        return Randomizer::isEnemySwapRandoEnabled();
    case OPT_NPC_SWAP:
        return Randomizer::isNpcSwapRandoEnabled();
    case OPT_TWO_D_SWAP:
        return Randomizer::isTwoDSwapRandoEnabled();
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP:
        return Randomizer::isPropSwapRandoEnabled();
#endif
    case OPT_LOCKED_CAPTURES:
        return Randomizer::isCaptureRandoEnabled();
    case OPT_LOCKED_ABILITIES:
        return Randomizer::isLockedAbilitiesEnabled();
    // Raw stored values, not the isLockedAbilitiesEnabled()-folded predicates:
    // the page is only reachable with that toggle on anyway, and reading the
    // folded form would make a row spring back to unchecked the moment it was
    // toggled off and on again.
    case OPT_SWIM_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 2)) != 0;
    case OPT_GRAB_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 3)) != 0;
    case OPT_JAXI_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 4)) != 0;
    case OPT_SCOOTER_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 5)) != 0;
    case OPT_ROCKET_FLOWER_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 6)) != 0;
    case OPT_NPC_TALK_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 7)) != 0;
    case OPT_PIPE_2D_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 8)) != 0;
    case OPT_TIMER_CHALLENGE_LOCK:
        return (Randomizer::packMovementAbilitySettings() & (1u << 9)) != 0;
    case OPT_PURPLE_SHOP_MOONS:
        return Randomizer::arePurpleShopMoonsEnabled();
    case OPT_MOON_ROCK_KEY:
        return Randomizer::isMoonRockKeyEnabled();
    case OPT_MOON_ROCK_KEY_ANY_MOON:
        return Randomizer::isMoonRockKeyEnabled() && Randomizer::getMoonRockKeyScope() == Randomizer::KeyScope::AnyMoon;
    case OPT_MOON_ROCK_KEY_OWN_KINGDOM:
        return Randomizer::isMoonRockKeyEnabled() && Randomizer::getMoonRockKeyScope() == Randomizer::KeyScope::WithinKingdom;
    case OPT_SUBAREA_SCOPE_ANY_MOON:
        return Randomizer::getSubareaKeyScope() == Randomizer::KeyScope::AnyMoon;
    case OPT_SUBAREA_SCOPE_OWN_KINGDOM:
        return Randomizer::getSubareaKeyScope() == Randomizer::KeyScope::WithinKingdom;
    case OPT_STABILIZE_SHADERS:
        return Randomizer::isStabilizeShadersEnabled();
    case OPT_DEATH_LINK:
        return Randomizer::isDeathLinkEnabled();
    case OPT_SYNC_OTHER_TEAMS:
        return Randomizer::isSyncOtherTeamsEnabled();
    case OPT_FAST_MOON_DEMOS:
        return Randomizer::isFastMoonDemosEnabled();
    case OPT_MOTION_FREE:
        return Randomizer::isMotionFreeControlsEnabled();
    case OPT_SKIP_CUTSCENE_CAMERAS:
        return Randomizer::isSkipCutsceneCamerasEnabled();
    case OPT_FREE_COSTUME_MOON:
        return Randomizer::isFreeCostumeMoonEnabled();
    case OPT_UNLOCK_HINT_ART:
        return Randomizer::isUnlockHintArtEnabled();
    case OPT_ONE_SHEEP_HERDING:
        return Randomizer::isOneSheepHerdingEnabled();
    case OPT_AREA_NAME_OVERLAY:
        return Randomizer::isAreaNameOverlayEnabled();
    case OPT_EASY_MINIGAMES:
        return Randomizer::isEasyMinigamesEnabled();
    case OPT_FAST_DOG:
        return Randomizer::isFastDogEnabled();
    case OPT_UNCAPPED_COINS:
        return Randomizer::isUncappedCoinsEnabled();
    case OPT_NO_CLIP:
        return Randomizer::isNoClipEnabled();
    case OPT_TRACKER_RIGHT_ALIGN:
        return Randomizer::isTrackerRightAligned();
    case OPT_TRACKER_TIMER:
        return Randomizer::isTrackerTimerEnabled();
    case OPT_COMPLETED_KINGDOM_MOON_COLLECTED:
        return Randomizer::isCompletedKingdomMoonCollectedEnabled();
    case OPT_RANDOMIZE_OUTFITS:
        return Randomizer::isRandomizeOutfitsEnabled();
    case OPT_STREAMER_MODE:
        return Randomizer::isStreamerModeEnabled();
    case OPT_MUSIC_OVERLAY:
        return Randomizer::isMusicOverlayEnabled();
    case OPT_MUSIC_DISABLE_2D:
        return Randomizer::isMusic2DThemesDisabled();
    case OPT_MUSIC_DISABLE_EXTRA:
        return Randomizer::isMusicExtraThemesDisabled();
    case OPT_MUSIC_DISABLE_BOSS:
        return Randomizer::isMusicBossThemesDisabled();
    case OPT_ALWAYS_NEUTRAL_CAP_THROW:
        return Randomizer::isAlwaysNeutralCapThrowEnabled();
    case OPT_SPHINX:
        return Randomizer::isSphinxRandoEnabled();
    case OPT_SOFT_NUZLOCKE:
        return Randomizer::isSoftNuzlockeRunMode();
    case OPT_EXTREME_NUZLOCKE:
        return Randomizer::getExtremeNuzlockeEnabled();
    case OPT_TOADETTE_MEDAL_CUSTOM:
        return Randomizer::isCustomToadetteMedalCountsEnabled();
    case OPT_HINT_PROGRESSIVE_GLOBAL:
        return Randomizer::isActiveHintProgressiveGlobal();
    case OPT_HINT_COIN_PROGRESSIVE_GLOBAL:
        return Randomizer::isHintCoinProgressiveGlobal();
    case OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL:
        return Randomizer::isHintTokenProgressiveGlobal();
    case OPT_SHOP_PRICE_RANDO:
        return Randomizer::isShopPriceRandoEnabled();
    case OPT_PROPORTIONAL_KINGDOM_PRICING:
        return Randomizer::isProportionalKingdomPricingEnabled();
    default:
        return false;
    }
}

void StageSceneStateFileSelect::setOptionCheckState(OptionId id, bool value) {
    if (isAdvancedCapturePoolOption(id)) {
        Randomizer::setCaptureInLockPool(advancedCapturePoolIndex(id), value);
        return;
    }
    if (id == OPT_ADVANCED_PROGRESSIVE_ABILITIES) {
        Randomizer::setProgressiveAbilitiesEnabled(value);
        return;
    }
    if (isAdvancedRequirementKingdomOption(id)) {
        setRequirementKingdomEnabled(advancedRequirementKingdomIndex(id), value);
        return;
    }
    if (isAdvancedCaptureGrantOption(id)) {
        setKingdomGrantsSpecialMoonUse(advancedCaptureGrantIndex(id),
            Randomizer::SpecialMoonUse_Captures, value);
        return;
    }
    if (isAdvancedAbilityGrantOption(id)) {
        setKingdomGrantsSpecialMoonUse(advancedAbilityGrantIndex(id),
            Randomizer::SpecialMoonUse_Abilities, value);
        return;
    }
    if (isAdvancedAbilityPoolOption(id)) {
        setAbilityLockRow(advancedAbilityPoolIndex(id), value);
        return;
    }
    if (isAdvancedKingdomMoonUseOption(id)) {
        const s32 selection = advancedKingdomMoonUseIndex(id);
        const s32 worldId = selection / 7;
        const u8 bit = static_cast<u8>(1u << (selection % 7));
        u8 mask = kingdomRewardMask(worldId);
        if (value)
            mask |= bit;
        else
            mask &= static_cast<u8>(~bit);
        // Clearing every reward is a legal answer, and the usual one: that
        // kingdom's moons are then plain moons and the row reads None.
        writeKingdomRewardMask(worldId, mask);
        return;
    }
    if (isAdvancedStartKingdomOption(id)) {
        const s32 worldId = advancedStartKingdomIndex(id);
        u32 mask = Randomizer::getStartKingdomsMask();
        const u32 bit = 1u << worldId;
        if (value)
            mask |= bit;
        else
            mask &= ~bit;
        Randomizer::setStartKingdomsMask(mask);
        return;
    }
    if (id == OPT_ADVANCED_HINT_COIN_OVERRIDE) {
        if (mSubPageParam >= 0 && mSubPageParam < 5)
            Randomizer::setHintCoinOverride(
                static_cast<Randomizer::HintPriceCategory>(mSubPageParam), value);
        return;
    }

    switch (id) {
    case OPT_MOONS:
        Randomizer::setMoonRandoEnabled(value);
        break;
    case OPT_ENABLE_MOON_REALLOCATION:
        Randomizer::setMoonReallocationEnabled(value);
        break;
    case OPT_RANDOMIZE_MOON_ALLOCATION:
        Randomizer::setRandomizeMoonAllocationEnabled(value);
        break;
    case OPT_TRIPLE_MOONS_ANYWHERE:
        Randomizer::setTripleMoonsAnywhereEnabled(value);
        break;
    case OPT_PAINTINGS:
        Randomizer::setPaintingRandoEnabled(value);
        break;
    case OPT_KINGDOM_ORDER:
        Randomizer::setKingdomRandoEnabled(value);
        break;
    case OPT_START_KINGDOM_STATE:
        Randomizer::setStartKingdomStateRandoEnabled(value);
        break;
    case OPT_CHEST_ORDER:
        Randomizer::setChestOrderRandoEnabled(value);
        break;
    case OPT_COIN_RANDO:
        Randomizer::setCoinRandoEnabled(value);
        break;
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    case OPT_ENEMY_SWAP:
        Randomizer::setEnemySwapRandoEnabled(value);
        break;
    case OPT_NPC_SWAP:
        Randomizer::setNpcSwapRandoEnabled(value);
        break;
    case OPT_TWO_D_SWAP:
        Randomizer::setTwoDSwapRandoEnabled(value);
        break;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP:
        Randomizer::setPropSwapRandoEnabled(value);
        break;
#endif
    case OPT_LOCKED_CAPTURES:
        Randomizer::setCaptureRandoEnabled(value);
        break;
    case OPT_LOCKED_ABILITIES:
        Randomizer::setLockedAbilitiesEnabled(value);
        break;
    case OPT_SWIM_LOCK:
        Randomizer::setSwimLockEnabled(value);
        break;
    case OPT_GRAB_LOCK:
        Randomizer::setGrabLockEnabled(value);
        break;
    case OPT_JAXI_LOCK:
        Randomizer::setJaxiLockEnabled(value);
        break;
    case OPT_SCOOTER_LOCK:
        Randomizer::setScooterLockEnabled(value);
        break;
    case OPT_ROCKET_FLOWER_LOCK:
        Randomizer::setRocketFlowerLockEnabled(value);
        break;
    case OPT_NPC_TALK_LOCK:
        Randomizer::setNpcTalkLockEnabled(value);
        break;
    case OPT_PIPE_2D_LOCK:
        Randomizer::setPipe2DLockEnabled(value);
        break;
    case OPT_TIMER_CHALLENGE_LOCK:
        Randomizer::setTimerChallengeLockEnabled(value);
        break;
    case OPT_PURPLE_SHOP_MOONS:
        Randomizer::setPurpleShopMoonsEnabled(value);
        break;
    case OPT_MOON_ROCK_KEY:
        Randomizer::setMoonRockKeyEnabled(value);
        break;
    case OPT_MOON_ROCK_KEY_ANY_MOON:
        if (value) {
            Randomizer::setMoonRockKeyEnabled(true);
            Randomizer::setMoonRockKeyScope(Randomizer::KeyScope::AnyMoon);
        } else if (Randomizer::getMoonRockKeyScope() == Randomizer::KeyScope::AnyMoon) {
            Randomizer::setMoonRockKeyEnabled(false);
        }
        break;
    case OPT_MOON_ROCK_KEY_OWN_KINGDOM:
        if (value) {
            Randomizer::setMoonRockKeyEnabled(true);
            Randomizer::setMoonRockKeyScope(Randomizer::KeyScope::WithinKingdom);
        } else if (Randomizer::getMoonRockKeyScope() == Randomizer::KeyScope::WithinKingdom) {
            Randomizer::setMoonRockKeyEnabled(false);
        }
        break;
    case OPT_SUBAREA_SCOPE_ANY_MOON:
        if (value)
            Randomizer::setSubareaKeyScope(Randomizer::KeyScope::AnyMoon);
        break;
    case OPT_SUBAREA_SCOPE_OWN_KINGDOM:
        if (value)
            Randomizer::setSubareaKeyScope(Randomizer::KeyScope::WithinKingdom);
        break;
    case OPT_STABILIZE_SHADERS:
        Randomizer::setStabilizeShadersEnabled(value);
        break;
    case OPT_DEATH_LINK:
        Randomizer::setDeathLinkEnabled(value);
        // Push it to the server the moment it changes. The host is already
        // connected by the time this page is reachable (connecting is what
        // routed them here), and the room's flags are what a guest actually
        // reads - its own copy is ignored. The snapshot upload re-sends the
        // same value in SnapshotBegin::flags, so a room that is set up
        // offline-then-connected still ends up correct.
#if SMOR_FEATURE_MULTIPLAYER
        if (Client::isSocketActive())
            Client::sendRoomSettings();
#endif
        break;
    case OPT_SYNC_OTHER_TEAMS:
        Randomizer::setSyncOtherTeamsEnabled(value);
        // Same round trip as Death Link above: it is one of the room's flags,
        // and the server is what enforces it - with this off it stops relaying
        // player state across team lines entirely.
#if SMOR_FEATURE_MULTIPLAYER
        if (Client::isSocketActive())
            Client::sendRoomSettings();
#endif
        break;
    case OPT_FAST_MOON_DEMOS:
        Randomizer::setFastMoonDemosEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_MOTION_FREE:
        Randomizer::setMotionFreeControlsEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_SKIP_CUTSCENE_CAMERAS:
        Randomizer::setSkipCutsceneCamerasEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_FREE_COSTUME_MOON:
        Randomizer::setFreeCostumeMoonEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_UNLOCK_HINT_ART:
        Randomizer::setUnlockHintArtEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_ONE_SHEEP_HERDING:
        Randomizer::setOneSheepHerdingEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_AREA_NAME_OVERLAY:
        Randomizer::setAreaNameOverlayEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_EASY_MINIGAMES:
        Randomizer::setEasyMinigamesEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_FAST_DOG:
        Randomizer::setFastDogEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_UNCAPPED_COINS:
        Randomizer::setUncappedCoinsEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_NO_CLIP:
        Randomizer::setNoClipEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_TRACKER_RIGHT_ALIGN:
        Randomizer::setTrackerRightAligned(value);
        GlobalConfig::save();
        break;
    case OPT_TRACKER_TIMER:
        Randomizer::setTrackerTimerEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_COMPLETED_KINGDOM_MOON_COLLECTED:
        Randomizer::setCompletedKingdomMoonCollectedEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_RANDOMIZE_OUTFITS:
        Randomizer::setRandomizeOutfitsEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_STREAMER_MODE:
        Randomizer::setStreamerModeEnabled(value);
        break;
    case OPT_MUSIC_OVERLAY:
        Randomizer::setMusicOverlayEnabled(value);
        break;
    case OPT_MUSIC_DISABLE_2D:
        Randomizer::setMusic2DThemesDisabled(value);
        break;
    case OPT_MUSIC_DISABLE_EXTRA:
        Randomizer::setMusicExtraThemesDisabled(value);
        break;
    case OPT_MUSIC_DISABLE_BOSS:
        Randomizer::setMusicBossThemesDisabled(value);
        break;
    case OPT_ALWAYS_NEUTRAL_CAP_THROW:
        Randomizer::setAlwaysNeutralCapThrowEnabled(value);
        GlobalConfig::save();
        break;
    case OPT_SPHINX:
        Randomizer::setSphinxRandoEnabled(value);
        break;
    case OPT_SOFT_NUZLOCKE:
        // Soft is a variant of the merged Nuzlocke run mode rather than a
        // setting of its own, so the checkbox picks between the two RunMode
        // values. It only has meaning while a Nuzlocke run is selected.
        if (Randomizer::isNuzlockeRunMode()) {
            Randomizer::setRunMode(value ? Randomizer::RunMode::SoftNuzlocke
                                         : Randomizer::RunMode::Nuzlocke);
        }
        break;
    case OPT_EXTREME_NUZLOCKE:
        Randomizer::setExtremeNuzlockeEnabled(value);
        break;
    case OPT_TOADETTE_MEDAL_CUSTOM:
        Randomizer::setCustomToadetteMedalCountsEnabled(value);
        break;
    case OPT_HINT_COIN_PROGRESSIVE_GLOBAL:
        Randomizer::setHintCoinProgressiveGlobal(value);
        break;
    case OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL:
        Randomizer::setHintTokenProgressiveGlobal(value);
        break;
    case OPT_HINT_PROGRESSIVE_GLOBAL:
        Randomizer::setActiveHintProgressiveGlobal(value);
        break;
    case OPT_SHOP_PRICE_RANDO:
        Randomizer::setShopPriceRandoEnabled(value);
        break;
    case OPT_PROPORTIONAL_KINGDOM_PRICING:
        Randomizer::setProportionalKingdomPricingEnabled(value);
        break;
    default:
        break;
    }
}

void StageSceneStateFileSelect::toggleOption(OptionId id) {
    setOptionCheckState(id, !optionCheckState(id));
}

bool StageSceneStateFileSelect::optionVisible(OptionId id) {
    switch (id) {
    // Re-allocation only means anything while the moons themselves are being
    // shuffled, so the whole page hangs off Randomize Moons rather than each
    // row testing it separately. Without that, picking Custom revealed nothing
    // and looked broken.
    case OPT_MOON_ALLOCATION_MODE:
        return Randomizer::isMoonRandoEnabled();
    case OPT_ENABLE_MOON_REALLOCATION:
        return false;
    case OPT_RANDOMIZE_MOON_ALLOCATION:
        return false;
    case OPT_MOON_ALLOCATION_DIFFICULTY:
        return Randomizer::isMoonRandoEnabled()
            && Randomizer::isMoonReallocationEnabled()
            && Randomizer::isRandomizeMoonAllocationEnabled();
    // Off the main page, but still editable on the Moon Allocation advanced
    // page - deleting it is the one thing this rework does not do.
    case OPT_MOON_COIN_POLICY:
        return mSubPage == SubPage::MoonAllocationKingdoms;
    case OPT_BOARD_SCALE:
        return Randomizer::isGameModesFeatureEnabled();
    case OPT_UNLOCK_PACING:
    case OPT_SEED_DIFFICULTY:
        // Gate on the FILL, not on Logic Rando. With Logic Rando off the
        // buffer-managed fill still runs (sNoLogicFill only drops the
        // capture/ability and trick requirements), so pacing weights, the
        // carrier cap, depth weighting and the sphere band all still shape the
        // seed. Hiding these there left the value applied but unreachable.
        // Seed Difficulty's movement-tier half is the only part that needs
        // harder-than-casual methods, and that half no-ops on its own.
        return Randomizer::isMoonRandoEnabled();
    case OPT_SEED_OPENNESS:
        // Openness reaches further still: with Moon Rando off it is the whole
        // shape of the one-shot entrance builder (chainBiasForOpenness,
        // doorClassWeight, openDoorReserve), so it stays editable whenever
        // either randomizer that reads it is on.
        return Randomizer::isMoonRandoEnabled() || Randomizer::isEntranceRandoEnabled();
    case OPT_LOCKED_CAPTURES:
        return Randomizer::isCaptureLockFeatureEnabled();
    case OPT_LOCKED_ABILITIES:
        return Randomizer::isAbilityLockFeatureEnabled();
    // Only meaningful while there is something to hand out.
    case OPT_CAPTURE_GRANT_KINGDOMS:
        return Randomizer::isCaptureLockFeatureEnabled()
            && Randomizer::isCaptureRandoEnabled();
    case OPT_ABILITY_GRANT_KINGDOMS:
        return Randomizer::isAbilityLockFeatureEnabled()
            && Randomizer::isLockedAbilitiesEnabled();
    // Absorbed from the retired Movement Abilities page. Only meaningful once
    // low gravity is actually being withheld, which is its row in the Locked
    // Abilities advanced page - this row then picks how it is earned back.
    case OPT_MOON_GRAVITY:
        return Randomizer::isAbilityLockFeatureEnabled()
            && Randomizer::isLockedAbilitiesEnabled()
            && Randomizer::isAbilityInLockPool(
                AbilityRando::AbilityId_MoonLowGravity);
    // Retained in the table for preset portability only - these are edited on
    // the Locked Abilities advanced page now.
    case OPT_SWIM_LOCK:
    case OPT_GRAB_LOCK:
    case OPT_JAXI_LOCK:
    case OPT_SCOOTER_LOCK:
    case OPT_ROCKET_FLOWER_LOCK:
    case OPT_NPC_TALK_LOCK:
    case OPT_PIPE_2D_LOCK:
    case OPT_TIMER_CHALLENGE_LOCK:
        return false;
    case OPT_TOADETTE_MEDAL_RATIO:
        // The fixed ratio table is what the custom counts replace, so the two
        // are never editable at the same time.
        return Randomizer::isToadetteMedalModeEnabled()
            && !Randomizer::isCustomToadetteMedalCountsEnabled();
    case OPT_TOADETTE_MEDAL_AVAILABLE:
    case OPT_TOADETTE_MEDAL_REQUIRED:
        return Randomizer::isCustomToadetteMedalCountsEnabled();
    case OPT_SOFT_NUZLOCKE_LOSS:
    case OPT_SOFT_NUZLOCKE_ABILITY_LOSS:
    case OPT_SOFT_NUZLOCKE_CAPTURE_LOSS:
        // A hard Nuzlocke loses the save outright; there is no share to pick.
        return Randomizer::isSoftNuzlockeRunMode();
    case OPT_MUSIC_DISABLE_2D:
    case OPT_MUSIC_DISABLE_EXTRA:
    case OPT_MUSIC_DISABLE_BOSS:
        // These only trim the shuffled pool, so none of them does anything
        // while the game is playing its original music.
        return Randomizer::isMusicRandoEnabled();
    // Edited on the Subarea Keys advanced page now; retained in the table so
    // presets keep carrying them.
    case OPT_SUBAREA_UNLOCK_HOST:
    case OPT_SUBAREA_SCOPE_ANY_MOON:
    case OPT_SUBAREA_SCOPE_OWN_KINGDOM:
    // Superseded by the single Moon Rock Keys checkbox plus its scope page.
    case OPT_MOON_ROCK_KEY_ANY_MOON:
    case OPT_MOON_ROCK_KEY_OWN_KINGDOM:
        return false;
    case OPT_RUN_MODE:
        // Was expressed by trimming the last row off the Main Settings page,
        // which silently pointed at whatever row happened to be last.
        return Randomizer::isGameModesFeatureEnabled();
    // Edited by the Dark Side row, which writes both masks.
    case OPT_KINGDOM_MOON_USE_16:
        return false;
    case OPT_SHOP_LOCATIONS:
        // Nothing is shuffled while Loading Zones is Off, so every value of
        // this one leaves the shops exactly where they are.
        return Randomizer::isEntranceRandoEnabled();
    case OPT_PROPORTIONAL_KINGDOM_PRICING:
        // Random already prices against the kingdom's own purple-coin total,
        // so there is nothing left for this to scale.
        return Randomizer::getPurpleShopPrice() != Randomizer::PurpleShopPrice::Random;
    // Retained for preset portability: the two per-currency rows replaced both
    // of these, and the shared-ladder flags moved onto their advanced page.
    case OPT_HINT_PROGRESSIVE_PRICE:
    case OPT_HINT_PROGRESSIVE_GLOBAL:
        return false;

    case OPT_USER_PRESET:
        // Keep the row as a discoverable entry point even before the first
        // preset exists. Its roll value becomes the explanatory "None" item.
        return true;
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP_RATE:
        return Randomizer::isPropSwapRandoEnabled();
#endif
    default:
        return true;
    }
}

s32 StageSceneStateFileSelect::optionRollIdx(OptionId id) const {
    if (id == OPT_TEAM)
        return Randomizer::getSelectedTeam();
    if (isHintCurrencyOption(id))
        return hintCategoryUsesTokens(hintCategoryAt(hintCurrencyIndex(id))) ? 1 : 0;
    if (isKingdomMoonUseOption(id))
        return kingdomMoonUseRollIndex(kingdomMoonUseIndex(id));
    if (isAdvancedKingdomUnlockOption(id))
        return kingdomUnlockRollIndex(advancedKingdomUnlockIndex(id));
    if (isMoonAllocationKingdomOption(id)) {
        const s16 target = Randomizer::getKingdomMoonTarget(
            moonAllocationKingdomIndex(id));
        return target < 0 ? 0 : static_cast<s32>(target) + 1;
    }
    if (isTripleMoonKingdomOption(id)) {
        const s32 kingdom = tripleMoonKingdomIndex(id);
        const s8 target = Randomizer::getKingdomTripleMoonTarget(kingdom);
        return target < 0 ? vanillaTripleMoonCountForKingdom(kingdom)
                          : static_cast<s32>(target);
    }
    if (id == OPT_ADVANCED_HINT_CUSTOM_PRICE)
        return 0;
    if (id == OPT_ADVANCED_MOON_ROCK_SCOPE)
        return static_cast<s32>(Randomizer::getMoonRockKeyScope());
    if (id == OPT_ADVANCED_SUBAREA_SCOPE)
        return static_cast<s32>(Randomizer::getSubareaKeyScope());
    if (id == OPT_ADVANCED_SHEEP_COUNT)
        return Randomizer::getSheepHerdingCount()
            - Randomizer::kSheepHerdingMinCount;

    switch (id) {
    case OPT_PRESET:
        return mPresetIdx;
    case OPT_USER_PRESET:
        return mUserPresetIdx;
    case OPT_RUN_MODE: {
        const Randomizer::RunMode mode = Randomizer::getRunMode();
        // Soft Nuzlocke has no row of its own - it shares the Nuzlocke row and
        // is distinguished by the Game Mode Settings page's Soft checkbox.
        return runModeRollIdxOf(mode == Randomizer::RunMode::SoftNuzlocke
                ? Randomizer::RunMode::Nuzlocke
                : mode);
    }
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    case OPT_TRIPLE_MOONS:
        if (Randomizer::isKingdomTripleMoonTargetCustom())
            return 2;
        return Randomizer::isRandomizeTripleMoonsEnabled() ? 1 : 0;
#endif
    case OPT_LOCKED_CAPTURES:
        return captureLockPresetIndex();
    case OPT_LOCKED_ABILITIES:
        return abilityLockPresetIndex();
    case OPT_REQUIREMENT_KINGDOMS:
        return requirementKingdomsPresetIndex();
    case OPT_CAPTURE_GRANT_KINGDOMS:
        return grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Captures);
    case OPT_ABILITY_GRANT_KINGDOMS:
        return grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Abilities);
    case OPT_MOON_ALLOCATION_MODE:
        return Randomizer::isMoonReallocationEnabled()
                && Randomizer::isRandomizeMoonAllocationEnabled()
            ? 1
            : 0;
    case OPT_MOON_ALLOCATION_DIFFICULTY:
        return static_cast<s32>(Randomizer::getMoonAllocationDifficulty());
    case OPT_MOON_COIN_POLICY:
        return static_cast<s32>(Randomizer::getMoonCoinPolicy());
    case OPT_LOADING_ZONES:
        return entranceModeToUiIndex(Randomizer::getEntranceRandoMode());
    case OPT_SHOP_LOCATIONS:
        return static_cast<s32>(Randomizer::getShopLocationMode());
    case OPT_PLACEMENT_LOGIC:
        if (!Randomizer::isLogicRandoEnabled())
            return 0;
        switch (Randomizer::getLogicDifficulty()) {
        case Randomizer::LogicDifficulty_Advanced:
            return 2;
        case Randomizer::LogicDifficulty_Expert:
            return 3;
        case Randomizer::LogicDifficulty_Glitched:
            return 4;
        default:
            return 1;
        }
    case OPT_UNLOCK_PACING:
        return static_cast<s32>(Randomizer::getUnlockPacing());
    case OPT_SEED_OPENNESS:
        return static_cast<s32>(Randomizer::getSeedOpenness());
    case OPT_SEED_DIFFICULTY:
        return static_cast<s32>(Randomizer::getSeedDifficulty());
    case OPT_TOADETTE_MEDAL_RATIO:
        return Randomizer::getToadetteMedalRatioIdx();
    case OPT_STARTING_EQUIPMENT:
        return Randomizer::isStartingEquipmentCustom()
            ? 3
            : static_cast<s32>(Randomizer::getStartingEquipment());
    case OPT_CAPTURE_DIFFICULTY:
        return static_cast<s32>(Randomizer::getCaptureAbilityDifficulty());
    case OPT_MOON_GRAVITY:
        // Unlocked is not one of this roll's values, so the modes shift down
        // by one. The row is hidden entirely while low gravity is unlocked.
        return static_cast<s32>(Randomizer::getMoonGravityMode()) - 1;
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    case OPT_NPC_SWAP:
        return static_cast<s32>(Randomizer::getNpcSwapMode());
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP:
        return static_cast<s32>(Randomizer::getPropSwapMode());
    case OPT_PROP_SWAP_RATE:
        switch (Randomizer::getPropSwapChancePercent()) {
        case 50:
            return 1;
        case 75:
            return 2;
        case 100:
            return 3;
        case 25:
        default:
            return 0;
        }
#endif
    case OPT_PURPLE_SHOP_ABILITIES:
        return Randomizer::getPurpleShopAbilityCount();
    case OPT_PURPLE_SHOP_CAPTURES:
        return Randomizer::getPurpleShopCaptureCount();
    case OPT_PURPLE_SHOP_PRICE:
        return Randomizer::isPurpleShopPriceCustom()
            ? 6
            : static_cast<s32>(Randomizer::getPurpleShopPrice());
    case OPT_MOON_REQ_DIFFICULTY:
        return isMoonRequirementDeviationCustom()
            ? MenuText::ROLL_MOON_REQ_DIFFICULTY_COUNT
            : moonDifficultyRollIndex(Randomizer::getMoonDifficulty());
    case OPT_MUSIC:
        return musicRandoModeToUiIndex(Randomizer::getMusicRandoMode());
    case OPT_MUSIC_SOURCE:
        return static_cast<s32>(Randomizer::getMusicSourceMode());
    case OPT_HINT_PRICE_MOONS:
    case OPT_HINT_PRICE_CAPTURES:
    case OPT_HINT_PRICE_ABILITIES:
    case OPT_HINT_PRICE_PATH:
    case OPT_HINT_PRICE_PURCHASES: {
        const Randomizer::HintPriceCategory category = hintCategoryAt(
            static_cast<s32>(id) - static_cast<s32>(OPT_HINT_PRICE_MOONS));
        if (!Randomizer::isHintPriceCustom(category))
            return getHintCategoryPrice(category);
        // Custom is the value appended past the named ones, and which list
        // that is depends on the currency this category is priced in.
        return hintCategoryUsesTokens(category)
            ? MenuText::ROLL_HINT_TOKEN_PRICE_CUSTOM_COUNT - 1
            : MenuText::ROLL_HINT_COIN_PRICE_CUSTOM_COUNT - 1;
    }
    case OPT_HINT_COIN_PROGRESSIVE:
        return Randomizer::getHintCoinProgressiveIncrement();
    case OPT_HINT_TOKEN_PROGRESSIVE:
        return Randomizer::getHintTokenProgressiveIncrement();
    case OPT_HINT_PROGRESSIVE_PRICE:
        return Randomizer::getActiveHintProgressiveIncrement();
    case OPT_SKYBOXES:
        return static_cast<s32>(Randomizer::getSkyboxRandoMode());
    case OPT_WORLD_PEACE_REQ:
        return static_cast<s32>(Randomizer::getWorldPeaceRequirementMode());
    case OPT_SUBAREA_UNLOCKS:
        return subareaUnlockModeToRoll(Randomizer::getSubareaUnlockMode());
    case OPT_SUBAREA_UNLOCK_HOST:
        return static_cast<s32>(Randomizer::getSubareaUnlockHost());
    case OPT_RANDOMIZED_START_POSITION:
        return static_cast<s32>(Randomizer::getStartPositionMode());
    case OPT_TRACKER_OVERLAY:
        return static_cast<s32>(Randomizer::getTrackerOverlayMode());
    case OPT_TRACKER_CAVE_SKIP:
        return static_cast<s32>(Randomizer::getTrackerCaveSkipMode());
    case OPT_TRACKER_MOON_LIMITS:
        return static_cast<s32>(Randomizer::getTrackerMoonLimitMode());
    case OPT_BOARD_SCALE:
        return static_cast<s32>(Randomizer::getBoardScale());
    case OPT_SOFT_NUZLOCKE_LOSS:
        return Randomizer::isSoftNuzlockeLossCustom()
            ? MenuText::ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM_COUNT - 1
            : static_cast<s32>(Randomizer::getSoftNuzlockeLoss());
    case OPT_SOFT_NUZLOCKE_ABILITY_LOSS:
        return Randomizer::isSoftNuzlockeAbilityLossCustom()
            ? MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT - 1
            : static_cast<s32>(Randomizer::getSoftNuzlockeAbilityLoss());
    case OPT_SOFT_NUZLOCKE_CAPTURE_LOSS:
        return Randomizer::isSoftNuzlockeCaptureLossCustom()
            ? MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT - 1
            : static_cast<s32>(Randomizer::getSoftNuzlockeCaptureLoss());
    case OPT_TIMER_START:
        return Randomizer::isTimerStartCustom()
            ? MenuText::ROLL_TIMER_START_CUSTOM_COUNT - 1
            : Randomizer::getTimerStartIdx();
    case OPT_TIMER_MOON_BONUS:
        return Randomizer::isTimerMoonBonusCustom()
            ? MenuText::ROLL_TIMER_MOON_BONUS_CUSTOM_COUNT - 1
            : Randomizer::getTimerMoonBonusIdx();
    case OPT_TIMER_TRIPLE_BONUS:
        return Randomizer::isTimerTripleBonusCustom()
            ? MenuText::ROLL_TIMER_TRIPLE_BONUS_CUSTOM_COUNT - 1
            : Randomizer::getTimerTripleBonusIdx();
    case OPT_TIMER_DAMAGE_PENALTY:
        return Randomizer::isTimerDamagePenaltyCustom()
            ? MenuText::ROLL_TIMER_DAMAGE_PENALTY_CUSTOM_COUNT - 1
            : Randomizer::getTimerDamagePenaltyIdx();
    case OPT_TIMER_DEATH_PENALTY:
        return Randomizer::isTimerDeathPenaltyCustom()
            ? MenuText::ROLL_TIMER_DEATH_PENALTY_CUSTOM_COUNT - 1
            : Randomizer::getTimerDeathPenaltyIdx();
    case OPT_TIMER_HALF_LIFE:
        return Randomizer::isTimerHalfLifeCustom()
            ? MenuText::ROLL_TIMER_HALF_LIFE_CUSTOM_COUNT - 1
            : Randomizer::getTimerHalfLifeIdx();
    case OPT_ONE_SHEEP_HERDING:
        return Randomizer::isOneSheepHerdingEnabled()
            ? Randomizer::getSheepHerdingCount()
                - Randomizer::kSheepHerdingMinCount + 1
            : 0;
    default:
        return 0;
    }
}

void StageSceneStateFileSelect::setOptionRollIdx(OptionId id, s32 idx) {
    if (id == OPT_TEAM) {
        const u8 team = (u8)clampRoll(idx, MultiplayerProtocol::cTeamCount);
        Randomizer::setSelectedTeam(team);
        // Per-console, not per-seed - see Randomizer::getSelectedTeam for why
        // rando.bin would be the wrong home for this.
        GlobalConfig::save();
#if SMOR_FEATURE_MULTIPLAYER
        // Tells the server too when there is one. The join path re-sends it
        // regardless, so a pick made while disconnected is not lost.
        Client::setLocalTeam(team);
#endif
        return;
    }
    if (isHintCurrencyOption(id)) {
        // Without a token economy there is nothing to switch to, so the row is
        // informational and this is a no-op.
        if (Randomizer::isHintTokenMode())
            Randomizer::setHintCategoryCurrency(hintCategoryAt(hintCurrencyIndex(id)),
                idx == 0 ? Randomizer::HintCurrencyMode::Coins
                         : Randomizer::HintCurrencyMode::HintTokens);
        return;
    }
    if (isKingdomMoonUseOption(id)) {
        setKingdomMoonUseFromRollIndex(kingdomMoonUseIndex(id), idx);
        return;
    }
    if (isAdvancedKingdomUnlockOption(id)) {
        setKingdomUnlockRollIndex(advancedKingdomUnlockIndex(id),
            clampRoll(idx, MenuText::ROLL_KINGDOM_UNLOCK_COUNT));
        return;
    }
    if (isMoonAllocationKingdomOption(id)) {
        const s32 value = clampRoll(idx, kMoonTargetRollCount);
        Randomizer::setKingdomMoonTarget(
            moonAllocationKingdomIndex(id),
            value == 0 ? static_cast<s16>(-1) : static_cast<s16>(value - 1));
        if (value > 0) {
            Randomizer::setMoonReallocationEnabled(true);
            Randomizer::setRandomizeMoonAllocationEnabled(true);
        }
        return;
    }
    if (isTripleMoonKingdomOption(id)) {
        const s32 value = clampRoll(idx,
            sizeof(sTripleMoonTargetRollText) / sizeof(sTripleMoonTargetRollText[0]));
        Randomizer::setKingdomTripleMoonTarget(
            tripleMoonKingdomIndex(id), static_cast<s8>(value));
        Randomizer::setRandomizeTripleMoonsEnabled(true);
        return;
    }
    if (id == OPT_ADVANCED_HINT_CUSTOM_PRICE)
        return;
    if (id == OPT_ADVANCED_MOON_ROCK_SCOPE) {
        Randomizer::setMoonRockKeyScope(static_cast<Randomizer::KeyScope>(
            clampRoll(idx, MenuText::ROLL_KEY_SCOPE_COUNT)));
        return;
    }
    if (id == OPT_ADVANCED_SUBAREA_SCOPE) {
        Randomizer::setSubareaKeyScope(static_cast<Randomizer::KeyScope>(
            clampRoll(idx, MenuText::ROLL_KEY_SCOPE_COUNT)));
        return;
    }
    if (id == OPT_ADVANCED_SHEEP_COUNT) {
        Randomizer::setSheepHerdingCount(
            clampRoll(idx, MenuText::ROLL_SHEEP_COUNT_COUNT)
            + Randomizer::kSheepHerdingMinCount);
        return;
    }

    switch (id) {
    case OPT_PRESET:
        // Preview only — rolling no longer applies immediately. Applying is
        // an explicit, confirmed action (see onDecideOption).
        mPresetIdx = clampRoll(idx, Presets::count());
        break;
    case OPT_USER_PRESET: {
        const s32 userCount = UserPresets::count();
        mUserPresetIdx = userCount > 0 ? clampRoll(idx, userCount) : 0;
        break;
    }
    case OPT_RUN_MODE: {
        const Randomizer::RunMode picked = sRunModeRollModes[clampRoll(idx, sRunModeRollCount)];
        // Re-entering the merged Nuzlocke row keeps whichever variant the Soft
        // checkbox last selected.
        Randomizer::setRunMode(
            picked == Randomizer::RunMode::Nuzlocke && Randomizer::isSoftNuzlockeRunMode()
                ? Randomizer::RunMode::SoftNuzlocke
                : picked);
        break;
    }
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    case OPT_TRIPLE_MOONS: {
        if (idx == 2 && Randomizer::isKingdomTripleMoonTargetCustom())
            return;
        if (Randomizer::isKingdomTripleMoonTargetCustom())
            Randomizer::clearKingdomTripleMoonTargets();
        Randomizer::setRandomizeTripleMoonsEnabled(idx == 1);
        break;
    }
#endif
    case OPT_REQUIREMENT_KINGDOMS: {
        // Custom is the last value and only reachable by editing the kingdoms
        // by hand, so rolling onto it does nothing.
        const s32 v = clampRoll(idx, MenuText::ROLL_REQUIREMENT_KINGDOMS_COUNT);
        if (v >= MenuText::ROLL_REQUIREMENT_KINGDOMS_COUNT - 1)
            return;
        applyRequirementKingdomsPreset(v);
        break;
    }
    // Rolling back to a named default restores it; Custom is only reachable by
    // editing the kingdoms by hand, so rolling onto it does nothing.
    case OPT_CAPTURE_GRANT_KINGDOMS:
        if (clampRoll(idx, MenuText::ROLL_CAPTURE_GRANT_KINGDOMS_COUNT) == 0)
            applyGrantKingdomsPreset(Randomizer::SpecialMoonUse_Captures);
        break;
    case OPT_ABILITY_GRANT_KINGDOMS:
        if (clampRoll(idx, MenuText::ROLL_ABILITY_GRANT_KINGDOMS_COUNT) == 0)
            applyGrantKingdomsPreset(Randomizer::SpecialMoonUse_Abilities);
        break;
    case OPT_LOCKED_ABILITIES: {
        // Custom is only reachable by editing the pool by hand, so rolling onto
        // it from a preset is a no-op rather than a silent pool wipe.
        const s32 v = clampRoll(idx, MenuText::ROLL_LOCKED_ABILITIES_COUNT);
        if (v == 4)
            return;
        Randomizer::setLockedAbilitiesEnabled(v != 0);
        if (v != 0)
            applyAbilityLockPreset(v);
        break;
    }
    case OPT_LOCKED_CAPTURES: {
        // Custom is only reachable by editing the pool by hand, so rolling
        // onto it from a preset is a no-op rather than a silent pool wipe.
        const s32 v = clampRoll(idx, MenuText::ROLL_LOCKED_CAPTURES_COUNT);
        if (v == 3)
            return;
        Randomizer::setCaptureRandoEnabled(v != 0);
        if (v == 1) {
            Randomizer::setCaptureLockPoolMask(0, ~0ull);
            Randomizer::setCaptureLockPoolMask(1, ~0ull);
        } else if (v == 2) {
            u64 validLo = 0, validHi = 0, criticalLo = 0, criticalHi = 0;
            captureLockPresetMasks(&validLo, &validHi, &criticalLo, &criticalHi);
            Randomizer::setCaptureLockPoolMask(0, criticalLo);
            Randomizer::setCaptureLockPoolMask(1, criticalHi);
        }
        break;
    }
    case OPT_MOON_ALLOCATION_MODE: {
        const s32 v = clampRoll(idx, MenuText::ROLL_MOON_ALLOCATION_MODE_COUNT);
        if (v == 0 && Randomizer::isKingdomMoonTargetCustom())
            Randomizer::clearKingdomMoonTargets();
        Randomizer::setMoonReallocationEnabled(v != 0);
        Randomizer::setRandomizeMoonAllocationEnabled(v != 0);
        break;
    }
    case OPT_MOON_ALLOCATION_DIFFICULTY:
        Randomizer::setMoonAllocationDifficulty(
            static_cast<Randomizer::MoonAllocationDifficulty>(clampRoll(idx, 5)));
        break;
    case OPT_MOON_COIN_POLICY:
        Randomizer::setMoonCoinPolicy(
            static_cast<Randomizer::MoonCoinPolicy>(clampRoll(idx, 3)));
        break;
    case OPT_LOADING_ZONES:
        Randomizer::setEntranceRandoMode(
            entranceModeFromUiIndex(clampRoll(idx, sEntranceModeUiCount)));
        break;
    case OPT_SHOP_LOCATIONS:
        Randomizer::setShopLocationMode(
            static_cast<Randomizer::ShopLocationMode>(clampRoll(idx, 3)));
        break;
    case OPT_PLACEMENT_LOGIC:
        if (clampRoll(idx, sLogicRollCount) == 0) {
            Randomizer::setLogicRandoEnabled(false);
        } else {
            Randomizer::setLogicRandoEnabled(true);
            Randomizer::setLogicDifficulty(
                static_cast<Randomizer::LogicDifficulty>(clampRoll(idx, sLogicRollCount) - 1));
        }
        break;
    case OPT_UNLOCK_PACING:
        Randomizer::setUnlockPacing(
            static_cast<Randomizer::UnlockPacing>(clampRoll(idx, sSeedAxisRollCount)));
        break;
    case OPT_SEED_OPENNESS:
        Randomizer::setSeedOpenness(
            static_cast<Randomizer::SeedOpenness>(clampRoll(idx, sSeedAxisRollCount)));
        break;
    case OPT_SEED_DIFFICULTY:
        Randomizer::setSeedDifficulty(
            static_cast<Randomizer::SeedDifficulty>(clampRoll(idx, sSeedAxisRollCount)));
        break;
    case OPT_TOADETTE_MEDAL_RATIO:
        Randomizer::setToadetteMedalRatioIdx(clampRoll(idx, Randomizer::kToadetteMedalRatioCount));
        break;
    case OPT_STARTING_EQUIPMENT:
        if (idx == 3 && Randomizer::isStartingEquipmentCustom())
            return;
        Randomizer::setStartingEquipment(static_cast<Randomizer::StartingEquipment>(clampRoll(idx, 3)));
        break;
    case OPT_CAPTURE_DIFFICULTY:
        Randomizer::setCaptureAbilityDifficulty(static_cast<Randomizer::CaptureAbilityDifficulty>(clampRoll(idx, 2)));
        break;
    case OPT_MOON_GRAVITY:
        Randomizer::setMoonGravityMode(static_cast<Randomizer::MoonGravityMode>(
            clampRoll(idx, MenuText::ROLL_MOON_GRAVITY_COUNT) + 1));
        break;
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    case OPT_NPC_SWAP:
        Randomizer::setNpcSwapMode(static_cast<Randomizer::ChaosSwapMode>(
            clampRoll(idx, sNpcSwapModeCount)));
        break;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP:
        Randomizer::setPropSwapMode(static_cast<Randomizer::ChaosSwapMode>(
            clampRoll(idx, sPropSwapModeCount)));
        break;
    case OPT_PROP_SWAP_RATE:
        switch (clampRoll(idx, 4)) {
        case 1:
            Randomizer::setPropSwapChancePercent(50);
            break;
        case 2:
            Randomizer::setPropSwapChancePercent(75);
            break;
        case 3:
            Randomizer::setPropSwapChancePercent(100);
            break;
        case 0:
        default:
            Randomizer::setPropSwapChancePercent(25);
            break;
        }
        break;
#endif
    case OPT_PURPLE_SHOP_ABILITIES:
        Randomizer::setPurpleShopAbilityCount(clampRoll(idx, 4));
        break;
    case OPT_PURPLE_SHOP_CAPTURES:
        Randomizer::setPurpleShopCaptureCount(clampRoll(idx, 4));
        break;
    case OPT_PURPLE_SHOP_PRICE:
        if (idx == 6) {
            if (!Randomizer::isPurpleShopPriceCustom())
                Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Custom);
            return;
        }
        Randomizer::setPurpleShopPrice(
            static_cast<Randomizer::PurpleShopPrice>(clampRoll(idx, 6)));
        break;
    case OPT_MOON_REQ_DIFFICULTY:
        // Custom is not selectable by rolling - it only shows a deviation the
        // player typed in - so rolling onto any named value restores the
        // default deviation the named values describe.
        if (idx >= MenuText::ROLL_MOON_REQ_DIFFICULTY_COUNT)
            break;
        Randomizer::setMoonDifficulty(moonDifficultyFromRollIndex(idx));
        Randomizer::setMoonRequirementDeviation(
            static_cast<s16>(defaultMoonRequirementDeviation()));
        // Including the per-kingdom overrides: picking a named spread means
        // every kingdom takes it, which is what the row now says.
        RequirementRando::clearRequirementDeviations();
        break;
    case OPT_MUSIC:
        Randomizer::setMusicRandoMode(
            musicRandoModeFromUiIndex(clampRoll(idx, sMusicRandoUiCount)));
        break;
    case OPT_MUSIC_SOURCE:
        Randomizer::setMusicSourceMode(
            static_cast<Randomizer::MusicSourceMode>(clampRoll(idx, 4)));
        break;
    case OPT_HINT_PRICE_MOONS:
    case OPT_HINT_PRICE_CAPTURES:
    case OPT_HINT_PRICE_ABILITIES:
    case OPT_HINT_PRICE_PATH:
    case OPT_HINT_PRICE_PURCHASES: {
        const Randomizer::HintPriceCategory category = hintCategoryAt(
            static_cast<s32>(id) - static_cast<s32>(OPT_HINT_PRICE_MOONS));
        // Rolling onto Custom is a no-op: the number behind it is typed in on
        // the advanced page, not cycled here.
        const s32 customIdx = hintCategoryUsesTokens(category)
            ? MenuText::ROLL_HINT_TOKEN_PRICE_CUSTOM_COUNT - 1
            : MenuText::ROLL_HINT_COIN_PRICE_CUSTOM_COUNT - 1;
        if (Randomizer::isHintPriceCustom(category) && idx == customIdx)
            return;
        // Leaving Custom means leaving it: the typed price is what made the
        // row read Custom, so it goes before the named value is stored.
        Randomizer::clearHintPriceOverride(category);
        setHintCategoryPrice(category, static_cast<u8>(idx));
        break;
    }
    case OPT_HINT_COIN_PROGRESSIVE:
        Randomizer::setHintCoinProgressiveIncrement(static_cast<u8>(idx));
        break;
    case OPT_HINT_TOKEN_PROGRESSIVE:
        Randomizer::setHintTokenProgressiveIncrement(static_cast<u8>(idx));
        break;
    case OPT_HINT_PROGRESSIVE_PRICE:
        Randomizer::setActiveHintProgressiveIncrement(static_cast<u8>(idx));
        break;
    case OPT_SKYBOXES:
        Randomizer::setSkyboxRandoMode(static_cast<Randomizer::SkyboxRandoMode>(clampRoll(idx, 5)));
        break;
    case OPT_WORLD_PEACE_REQ:
        Randomizer::setWorldPeaceRequirementMode(static_cast<Randomizer::WorldPeaceRequirementMode>(clampRoll(idx, 3)));
        break;
    case OPT_SUBAREA_UNLOCKS:
        // Both setters re-settle the mode/host pair, so a mode that outgrows the
        // current host widens it here rather than leaving the seed builder an
        // impossible combination.
        Randomizer::setSubareaUnlockMode(subareaUnlockRollToMode(clampRoll(idx, 4)));
        break;
    case OPT_SUBAREA_UNLOCK_HOST:
        Randomizer::setSubareaUnlockHost(
            static_cast<Randomizer::SubareaUnlockHost>(clampRoll(
                idx, MenuText::ROLL_SUBAREA_UNLOCK_HOST_COUNT)));
        break;
    case OPT_VISITED_KINGDOMS_MAP:
        Randomizer::setVisitedKingdomsMapMode(
            static_cast<Randomizer::VisitedKingdomsMapMode>(clampRoll(
                idx, MenuText::ROLL_VISITED_KINGDOMS_MAP_COUNT)));
        break;
    case OPT_RANDOMIZED_START_POSITION:
        // Generation setting: no GlobalConfig::save() here (that file is QoL only).
        Randomizer::setStartPositionMode(
            static_cast<Randomizer::StartPositionMode>(clampRoll(idx, 4)));
        break;
    case OPT_TRACKER_OVERLAY:
        Randomizer::setTrackerOverlayMode(static_cast<Randomizer::TrackerOverlayMode>(clampRoll(idx, 5)));
        GlobalConfig::save();
        break;
    case OPT_TRACKER_CAVE_SKIP:
        Randomizer::setTrackerCaveSkipMode(
            static_cast<Randomizer::TrackerCaveSkipMode>(clampRoll(idx, 3)));
        GlobalConfig::save();
        break;
    case OPT_TRACKER_MOON_LIMITS:
        Randomizer::setTrackerMoonLimitMode(
            static_cast<Randomizer::TrackerMoonLimitMode>(clampRoll(idx, 3)));
        GlobalConfig::save();
        break;
    case OPT_BOARD_SCALE:
        Randomizer::setBoardScale(static_cast<Randomizer::BoardScale>(clampRoll(idx, 3)));
        GlobalConfig::save();
        break;
    case OPT_SOFT_NUZLOCKE_LOSS:
        if (idx == MenuText::ROLL_SOFT_NUZLOCKE_LOSS_CUSTOM_COUNT - 1)
            Randomizer::setSoftNuzlockeLoss(Randomizer::SoftNuzlockeLoss::Custom);
        else
            Randomizer::setSoftNuzlockeLoss(
                static_cast<Randomizer::SoftNuzlockeLoss>(clampRoll(idx, 3)));
        break;
    case OPT_SOFT_NUZLOCKE_ABILITY_LOSS:
        if (idx == MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT - 1)
            Randomizer::setSoftNuzlockeAbilityLoss(
                Randomizer::SoftNuzlockeEquipLoss::Custom);
        else
            Randomizer::setSoftNuzlockeAbilityLoss(
                static_cast<Randomizer::SoftNuzlockeEquipLoss>(clampRoll(idx, 5)));
        break;
    case OPT_SOFT_NUZLOCKE_CAPTURE_LOSS:
        if (idx == MenuText::ROLL_SOFT_NUZLOCKE_EQUIPMENT_LOSS_CUSTOM_COUNT - 1)
            Randomizer::setSoftNuzlockeCaptureLoss(
                Randomizer::SoftNuzlockeEquipLoss::Custom);
        else
            Randomizer::setSoftNuzlockeCaptureLoss(
                static_cast<Randomizer::SoftNuzlockeEquipLoss>(clampRoll(idx, 5)));
        break;
    case OPT_TIMER_START:
        Randomizer::setTimerStartIdx(idx == MenuText::ROLL_TIMER_START_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerStartCount));
        break;
    case OPT_TIMER_MOON_BONUS:
        Randomizer::setTimerMoonBonusIdx(idx == MenuText::ROLL_TIMER_MOON_BONUS_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerMoonBonusCount));
        break;
    case OPT_TIMER_TRIPLE_BONUS:
        Randomizer::setTimerTripleBonusIdx(idx == MenuText::ROLL_TIMER_TRIPLE_BONUS_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerTripleBonusCount));
        break;
    case OPT_TIMER_DAMAGE_PENALTY:
        Randomizer::setTimerDamagePenaltyIdx(idx == MenuText::ROLL_TIMER_DAMAGE_PENALTY_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerDamagePenaltyCount));
        break;
    case OPT_TIMER_DEATH_PENALTY:
        Randomizer::setTimerDeathPenaltyIdx(idx == MenuText::ROLL_TIMER_DEATH_PENALTY_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerDeathPenaltyCount));
        break;
    case OPT_TIMER_HALF_LIFE:
        Randomizer::setTimerHalfLifeIdx(idx == MenuText::ROLL_TIMER_HALF_LIFE_CUSTOM_COUNT - 1
                ? Randomizer::kCustomIdx
                : clampRoll(idx, Randomizer::kTimerHalfLifeCount));
        break;
    case OPT_ONE_SHEEP_HERDING:
        if (clampRoll(idx, MenuText::ROLL_SHEEP_HERDING_COUNT) == 0) {
            Randomizer::setOneSheepHerdingEnabled(false);
        } else {
            Randomizer::setSheepHerdingCount(
                clampRoll(idx, MenuText::ROLL_SHEEP_HERDING_COUNT) - 1
                + Randomizer::kSheepHerdingMinCount);
            Randomizer::setOneSheepHerdingEnabled(true);
        }
        GlobalConfig::save();
        break;
    default:
        break;
    }
}

const char* StageSceneStateFileSelect::optionDescription(OptionId id) const {
    if (isMoonAllocationKingdomOption(id))
        return MenuText::DESC_ADVANCED_MOON_ALLOCATION_KINGDOM;
    if (isTripleMoonKingdomOption(id))
        return MenuText::DESC_ADVANCED_TRIPLE_MOON_KINGDOM;
    if (isAdvancedCapturePoolOption(id))
        return MenuText::DESC_ADVANCED_CAPTURE_POOL;
    if (id == OPT_ADVANCED_PROGRESSIVE_ABILITIES)
        return MenuText::DESC_ADVANCED_PROGRESSIVE_ABILITIES;
    if (isAdvancedRequirementKingdomOption(id))
        return MenuText::DESC_ADVANCED_REQUIREMENT_KINGDOM;
    if (isAdvancedCaptureGrantOption(id))
        return MenuText::DESC_ADVANCED_CAPTURE_GRANT_KINGDOM;
    if (isAdvancedAbilityGrantOption(id))
        return MenuText::DESC_ADVANCED_ABILITY_GRANT_KINGDOM;
    if (isAdvancedAbilityPoolOption(id))
        return MenuText::DESC_ADVANCED_ABILITY_POOL;
    if (id == OPT_ADVANCED_REQ_TOTAL)
        return MenuText::DESC_ADVANCED_REQ_TOTAL;
    if (isAdvancedRequirementBaseOption(id))
        return MenuText::DESC_ADVANCED_REQ_BASE;
    if (isAdvancedKingdomUnlockOption(id))
        return MenuText::DESC_ADVANCED_KINGDOM_UNLOCK;
    if (isAdvancedKingdomMoonUseOption(id))
        return MenuText::DESC_ADVANCED_KINGDOM_MOON_USE;
    if (isKingdomMoonUseOption(id)) {
        return isKingdomMoonUseCustom(kingdomMoonUseIndex(id))
            ? MenuText::DESC_KINGDOM_MOON_USE_CUSTOM
            : MenuText::DESC_KINGDOM_MOON_USE;
    }
    if (isAdvancedStartKingdomOption(id))
        return MenuText::DESC_VISITED_KINGDOMS_MAP_CUSTOM;
    if (isAdvancedReqDeviationOption(id))
        return MenuText::DESC_MOON_REQ_DIFFICULTY_NORMAL;
    if (isAdvancedTimerValueOption(id)) {
        switch (advancedTimerValueIndex(id)) {
        case 0:
            return MenuText::DESC_TIMER_START;
        case 1:
            return MenuText::DESC_TIMER_MOON_BONUS;
        case 2:
            return MenuText::DESC_TIMER_TRIPLE_BONUS;
        case 3:
            return MenuText::DESC_TIMER_DAMAGE_PENALTY;
        case 4:
            return MenuText::DESC_TIMER_DEATH_PENALTY;
        default:
            return MenuText::DESC_TIMER_HALF_LIFE;
        }
    }
    if (isAdvancedNuzlockeValueOption(id)) {
        switch (advancedNuzlockeValueIndex(id)) {
        case 0:
            return MenuText::DESC_SOFT_NUZLOCKE_LOSS;
        case 1:
            return MenuText::DESC_SOFT_NUZLOCKE_ABILITY_LOSS;
        default:
            return MenuText::DESC_SOFT_NUZLOCKE_CAPTURE_LOSS;
        }
    }
    if (id == OPT_ADVANCED_HINT_COIN_OVERRIDE)
        return MenuText::DESC_ADVANCED_HINT_COIN_OVERRIDE;
    if (id == OPT_ADVANCED_HINT_CUSTOM_PRICE)
        return MenuText::DESC_HINT_PRICE;
    if (id == OPT_ADVANCED_MINIGAME_LOW || id == OPT_ADVANCED_MINIGAME_HIGH)
        return MenuText::DESC_EASY_MINIGAMES;
    if (id == OPT_ADVANCED_SHEEP_COUNT)
        return MenuText::DESC_ONE_SHEEP_HERDING;

    switch (id) {
    case OPT_HINT_CURRENCY_MOONS:
    case OPT_HINT_CURRENCY_CAPTURES:
    case OPT_HINT_CURRENCY_ABILITIES:
    case OPT_HINT_CURRENCY_PATH:
    case OPT_HINT_CURRENCY_PURCHASES:
        return Randomizer::isHintTokenMode()
            ? MenuText::DESC_HINT_CURRENCY
            : MenuText::DESC_HINT_CURRENCY_NO_TOKENS;
    case OPT_CURRENT_SEED:
        return MenuText::DESC_CURRENT_SEED;
    case OPT_RANDOM_SEED:
        return MenuText::DESC_RANDOM_SEED;
    case OPT_SET_SEED:
        return MenuText::DESC_SET_SEED;
    case OPT_PRESET:
        return Presets::get(mPresetIdx).description;
    case OPT_USER_PRESET:
        if (UserPresets::count() == 0)
            return MenuText::DESC_USER_PRESET_EMPTY;
        return UserPresets::getDescription(userPresetSlotForRoll(mUserPresetIdx));
    case OPT_RUN_MODE:
        switch (Randomizer::getRunMode()) {
        case Randomizer::RunMode::ToadetteMedals:
            return MenuText::DESC_RUN_MODE_TOADETTE;
        case Randomizer::RunMode::Bingo:
            return MenuText::DESC_RUN_MODE_BINGO;
        case Randomizer::RunMode::Ascend:
            return MenuText::DESC_RUN_MODE_ASCEND;
        case Randomizer::RunMode::Nuzlocke:
            return MenuText::DESC_RUN_MODE_NUZLOCKE;
        case Randomizer::RunMode::SoftNuzlocke:
            return MenuText::DESC_RUN_MODE_SOFT_NUZLOCKE;
        case Randomizer::RunMode::Timer:
            return MenuText::DESC_RUN_MODE_TIMER;
        default:
            return MenuText::DESC_RUN_MODE_STANDARD;
        }

    case OPT_MOONS:
        return MenuText::DESC_MOONS;
    case OPT_MOON_ALLOCATION_MODE:
        return MenuText::DESC_MOON_ALLOCATION_MODE;
    case OPT_ENABLE_MOON_REALLOCATION:
        return MenuText::DESC_ENABLE_MOON_REALLOCATION;
    case OPT_RANDOMIZE_MOON_ALLOCATION:
        return MenuText::DESC_RANDOMIZE_MOON_ALLOCATION;
    case OPT_MOON_ALLOCATION_DIFFICULTY:
        switch (optionRollIdx(OPT_MOON_ALLOCATION_DIFFICULTY)) {
        case static_cast<s32>(Randomizer::MoonAllocationDifficulty::Easy):
            return MenuText::DESC_MOON_ALLOCATION_DIFFICULTY_EASY;
        case static_cast<s32>(Randomizer::MoonAllocationDifficulty::Hard):
            return MenuText::DESC_MOON_ALLOCATION_DIFFICULTY_HARD;
        case static_cast<s32>(Randomizer::MoonAllocationDifficulty::Expert):
            return MenuText::DESC_MOON_ALLOCATION_DIFFICULTY_EXPERT;
        case static_cast<s32>(Randomizer::MoonAllocationDifficulty::Max):
            return MenuText::DESC_MOON_ALLOCATION_DIFFICULTY_MAX;
        case static_cast<s32>(Randomizer::MoonAllocationDifficulty::Vanilla):
        default:
            return MenuText::DESC_MOON_ALLOCATION_DIFFICULTY_VANILLA;
        }
    case OPT_MOON_COIN_POLICY:
        switch (optionRollIdx(OPT_MOON_COIN_POLICY)) {
        case static_cast<s32>(Randomizer::MoonCoinPolicy::Standard):
            return MenuText::DESC_MOON_COIN_POLICY_STANDARD;
        case static_cast<s32>(Randomizer::MoonCoinPolicy::AbilityRush):
            return MenuText::DESC_MOON_COIN_POLICY_ABILITY_RUSH;
        case static_cast<s32>(Randomizer::MoonCoinPolicy::Vanilla):
        default:
            return MenuText::DESC_MOON_COIN_POLICY_VANILLA;
        }
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    case OPT_TRIPLE_MOONS:
        if (Randomizer::isKingdomTripleMoonTargetCustom())
            return MenuText::DESC_TRIPLE_MOONS_CUSTOM;
        if (Randomizer::isRandomizeTripleMoonsEnabled())
            return MenuText::DESC_TRIPLE_MOONS_RANDOMIZED;
        return MenuText::DESC_TRIPLE_MOONS_VANILLA;
    case OPT_TRIPLE_MOONS_ANYWHERE:
        return MenuText::DESC_TRIPLE_MOONS_ANYWHERE_CHECK;
#endif
    case OPT_LOADING_ZONES:
        switch (Randomizer::getEntranceRandoMode()) {
        case Randomizer::EntranceRandoMode_Off:
            return MenuText::DESC_LOADING_ZONES_OFF;
        case Randomizer::EntranceRandoMode_Limited:
            return MenuText::DESC_LOADING_ZONES_LIMITED;
        case Randomizer::EntranceRandoMode_Decoupled:
            return MenuText::DESC_LOADING_ZONES_DECOUPLED;
        case Randomizer::EntranceRandoMode_Standard:
        default:
            return MenuText::DESC_LOADING_ZONES_COUPLED;
        }
    case OPT_SHOP_LOCATIONS:
        if (!Randomizer::isEntranceRandoEnabled())
            return MenuText::DESC_SHOP_LOCATIONS_DISABLED;
        switch (Randomizer::getShopLocationMode()) {
        case Randomizer::ShopLocationMode_Swap:
            return MenuText::DESC_SHOP_LOCATIONS_SWAP;
        case Randomizer::ShopLocationMode_Vanilla:
            return MenuText::DESC_SHOP_LOCATIONS_VANILLA;
        case Randomizer::ShopLocationMode_Randomized:
        default:
            return MenuText::DESC_SHOP_LOCATIONS_RANDOMIZED;
        }
    case OPT_PAINTINGS:
        return MenuText::DESC_PAINTINGS;
    case OPT_KINGDOM_ORDER:
        return MenuText::DESC_KINGDOM_ORDER;
    case OPT_START_KINGDOM_STATE:
        return MenuText::DESC_START_KINGDOM_STATE;
    case OPT_CHEST_ORDER:
        return MenuText::DESC_CHEST_ORDER;
    case OPT_COIN_RANDO:
        return MenuText::DESC_COIN_RANDO;
    case OPT_RANDOMIZED_START_POSITION:
        switch (Randomizer::getStartPositionMode()) {
        case Randomizer::StartPositionMode_Overworld:
            return MenuText::DESC_RANDOMIZED_START_POSITION_OVERWORLD;
        case Randomizer::StartPositionMode_Max:
            return MenuText::DESC_RANDOMIZED_START_POSITION_MAX;
        case Randomizer::StartPositionMode_Odyssey:
            return MenuText::DESC_RANDOMIZED_START_POSITION_ODYSSEY;
        default:
            return MenuText::DESC_RANDOMIZED_START_POSITION_VANILLA;
        }
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    case OPT_ENEMY_SWAP:
        return MenuText::DESC_ENEMY_SWAP;
    case OPT_NPC_SWAP:
        return MenuText::DESC_NPC_SWAP;
    case OPT_TWO_D_SWAP:
        return MenuText::DESC_TWO_D_SWAP;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    case OPT_PROP_SWAP:
        return MenuText::DESC_PROP_SWAP;
    case OPT_PROP_SWAP_RATE:
        return MenuText::DESC_PROP_SWAP_RATE;
#endif
    case OPT_PLACEMENT_LOGIC:
        switch (optionRollIdx(OPT_PLACEMENT_LOGIC)) {
        case 0:
            return MenuText::DESC_PLACEMENT_LOGIC_NO_LOGIC;
        case 1:
            return MenuText::DESC_PLACEMENT_LOGIC_CASUAL;
        case 2:
            return MenuText::DESC_PLACEMENT_LOGIC_ADVANCED;
        case 3:
            return MenuText::DESC_PLACEMENT_LOGIC_EXPERT;
        default:
            return MenuText::DESC_PLACEMENT_LOGIC_GLITCHED;
        }

    case OPT_UNLOCK_PACING:
        switch (Randomizer::getUnlockPacing()) {
        case Randomizer::UnlockPacing_FrontLoaded:
            return MenuText::DESC_UNLOCK_PACING_FRONT_LOADED;
        case Randomizer::UnlockPacing_Early:
            return MenuText::DESC_UNLOCK_PACING_EARLY;
        case Randomizer::UnlockPacing_Late:
            return MenuText::DESC_UNLOCK_PACING_LATE;
        case Randomizer::UnlockPacing_BackLoaded:
            return MenuText::DESC_UNLOCK_PACING_BACK_LOADED;
        case Randomizer::UnlockPacing_Random:
            return MenuText::DESC_UNLOCK_PACING_RANDOM;
        case Randomizer::UnlockPacing_Standard:
        default:
            return MenuText::DESC_UNLOCK_PACING_STANDARD;
        }

    case OPT_SEED_OPENNESS:
        switch (Randomizer::getSeedOpenness()) {
        case Randomizer::SeedOpenness_Claustrophobic:
            return MenuText::DESC_SEED_OPENNESS_CLAUSTROPHOBIC;
        case Randomizer::SeedOpenness_Compact:
            return MenuText::DESC_SEED_OPENNESS_COMPACT;
        case Randomizer::SeedOpenness_Open:
            return MenuText::DESC_SEED_OPENNESS_OPEN;
        case Randomizer::SeedOpenness_WideOpen:
            return MenuText::DESC_SEED_OPENNESS_WIDE_OPEN;
        case Randomizer::SeedOpenness_Random:
            return MenuText::DESC_SEED_OPENNESS_RANDOM;
        case Randomizer::SeedOpenness_Standard:
        default:
            return MenuText::DESC_SEED_OPENNESS_STANDARD;
        }

    case OPT_SEED_DIFFICULTY:
        switch (Randomizer::getSeedDifficulty()) {
        case Randomizer::SeedDifficulty_Relaxed:
            return MenuText::DESC_SEED_DIFFICULTY_RELAXED;
        case Randomizer::SeedDifficulty_Easy:
            return MenuText::DESC_SEED_DIFFICULTY_EASY;
        case Randomizer::SeedDifficulty_Hard:
            return MenuText::DESC_SEED_DIFFICULTY_HARD;
        case Randomizer::SeedDifficulty_Brutal:
            return MenuText::DESC_SEED_DIFFICULTY_BRUTAL;
        case Randomizer::SeedDifficulty_Random:
            return MenuText::DESC_SEED_DIFFICULTY_RANDOM;
        case Randomizer::SeedDifficulty_Standard:
        default:
            return MenuText::DESC_SEED_DIFFICULTY_STANDARD;
        }

    case OPT_TOADETTE_MEDAL_RATIO:
        return MenuText::DESC_TOADETTE_MEDAL_RATIO;
    case OPT_TOADETTE_MEDAL_CUSTOM:
        return MenuText::DESC_TOADETTE_MEDAL_CUSTOM;
    case OPT_TOADETTE_MEDAL_AVAILABLE:
        return MenuText::DESC_TOADETTE_MEDAL_AVAILABLE;
    case OPT_TOADETTE_MEDAL_REQUIRED:
        return MenuText::DESC_TOADETTE_MEDAL_REQUIRED;
    case OPT_SOFT_NUZLOCKE:
        return MenuText::DESC_SOFT_NUZLOCKE;
    case OPT_EXTREME_NUZLOCKE:
        return MenuText::DESC_EXTREME_NUZLOCKE;
    case OPT_SOFT_NUZLOCKE_LOSS:
        return MenuText::DESC_SOFT_NUZLOCKE_LOSS;
    case OPT_SOFT_NUZLOCKE_ABILITY_LOSS:
        return MenuText::DESC_SOFT_NUZLOCKE_ABILITY_LOSS;
    case OPT_SOFT_NUZLOCKE_CAPTURE_LOSS:
        return MenuText::DESC_SOFT_NUZLOCKE_CAPTURE_LOSS;
    case OPT_TIMER_START:
        return MenuText::DESC_TIMER_START;
    case OPT_TIMER_MOON_BONUS:
        return MenuText::DESC_TIMER_MOON_BONUS;
    case OPT_TIMER_TRIPLE_BONUS:
        return MenuText::DESC_TIMER_TRIPLE_BONUS;
    case OPT_TIMER_DAMAGE_PENALTY:
        return MenuText::DESC_TIMER_DAMAGE_PENALTY;
    case OPT_TIMER_DEATH_PENALTY:
        return MenuText::DESC_TIMER_DEATH_PENALTY;
    case OPT_TIMER_HALF_LIFE:
        return MenuText::DESC_TIMER_HALF_LIFE;

    case OPT_LOCKED_CAPTURES:
        switch (captureLockPresetIndex()) {
        case 0:
            return MenuText::DESC_LOCKED_CAPTURES_VANILLA;
        case 1:
            return MenuText::DESC_LOCKED_CAPTURES_ALL;
        case 2:
            return MenuText::DESC_LOCKED_CAPTURES_CRITICAL;
        default:
            return MenuText::DESC_LOCKED_CAPTURES_CUSTOM;
        }
    case OPT_REQUIREMENT_KINGDOMS:
        switch (requirementKingdomsPresetIndex()) {
        case 0:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_VANILLA;
        case 1:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_CAP;
        case 2:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_CLOUD;
        case 3:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_MOON;
        case 4:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_ALL;
        default:
            return MenuText::DESC_REQUIREMENT_KINGDOMS_CUSTOM;
        }
    case OPT_CAPTURE_GRANT_KINGDOMS:
        return grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Captures) == 0
            ? MenuText::DESC_CAPTURE_GRANT_KINGDOMS_DEFAULT
            : MenuText::DESC_CAPTURE_GRANT_KINGDOMS_CUSTOM;
    case OPT_ABILITY_GRANT_KINGDOMS:
        return grantKingdomsPresetIndex(Randomizer::SpecialMoonUse_Abilities) == 0
            ? MenuText::DESC_ABILITY_GRANT_KINGDOMS_DEFAULT
            : MenuText::DESC_ABILITY_GRANT_KINGDOMS_CUSTOM;
    case OPT_LOCKED_ABILITIES:
        switch (abilityLockPresetIndex()) {
        case 0:
            return MenuText::DESC_LOCKED_ABILITIES_VANILLA;
        case 1:
            return MenuText::DESC_LOCKED_ABILITIES_ALL;
        case 2:
            return MenuText::DESC_LOCKED_ABILITIES_ORIGINAL;
        case 3:
            return MenuText::DESC_LOCKED_ABILITIES_NON_PROGRESSIVE;
        default:
            return MenuText::DESC_LOCKED_ABILITIES_CUSTOM;
        }
    case OPT_MOON_GRAVITY:
        switch (optionRollIdx(OPT_MOON_GRAVITY)) {
        case 1:
            return MenuText::DESC_MOON_GRAVITY_REQUIREMENTS;
        case 2:
            return MenuText::DESC_MOON_GRAVITY_EXTRA_REQUIREMENTS;
        default:
            return MenuText::DESC_MOON_GRAVITY_ABILITY_LOCK;
        }
    case OPT_SWIM_LOCK:
        return MenuText::DESC_SWIM_LOCK;
    case OPT_GRAB_LOCK:
        return MenuText::DESC_GRAB_LOCK;
    case OPT_JAXI_LOCK:
        return MenuText::DESC_JAXI_LOCK;
    case OPT_SCOOTER_LOCK:
        return MenuText::DESC_SCOOTER_LOCK;
    case OPT_ROCKET_FLOWER_LOCK:
        return MenuText::DESC_ROCKET_FLOWER_LOCK;
    case OPT_NPC_TALK_LOCK:
        return MenuText::DESC_NPC_TALK_LOCK;
    case OPT_PIPE_2D_LOCK:
        return MenuText::DESC_PIPE_2D_LOCK;
    case OPT_TIMER_CHALLENGE_LOCK:
        return MenuText::DESC_TIMER_CHALLENGE_LOCK;
    case OPT_STARTING_EQUIPMENT:
        if (Randomizer::isStartingEquipmentCustom())
            return MenuText::DESC_STARTING_EQUIPMENT_CUSTOM;
        switch (optionRollIdx(OPT_STARTING_EQUIPMENT)) {
        case 0:
            return MenuText::DESC_STARTING_EQUIPMENT_SCARCE;
        case 1:
            return MenuText::DESC_STARTING_EQUIPMENT_NORMAL;
        default:
            return MenuText::DESC_STARTING_EQUIPMENT_PLENTIFUL;
        }
    case OPT_CAPTURE_DIFFICULTY:
        switch (optionRollIdx(OPT_CAPTURE_DIFFICULTY)) {
        case 0:
            return MenuText::DESC_CAPTURE_DIFFICULTY_SCARCE;
        default:
            return MenuText::DESC_CAPTURE_DIFFICULTY_NORMAL;
        }

    case OPT_MOON_REQUIREMENT:
        return MenuText::DESC_MOON_REQUIREMENT;
    case OPT_MOON_REQ_DEVIATION:
        return MenuText::DESC_MOON_REQ_DEVIATION;
    case OPT_MOON_REQ_DIFFICULTY:
        if (isMoonRequirementDeviationCustom())
            return MenuText::DESC_MOON_REQ_DIFFICULTY_CUSTOM;
        switch (Randomizer::getMoonDifficulty()) {
        case Randomizer::MoonDifficulty_Easy:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_EASY;
        case Randomizer::MoonDifficulty_Hard:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_HARD;
        case Randomizer::MoonDifficulty_Harder:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_HARDER;
        case Randomizer::MoonDifficulty_Hardest:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_HARDEST;
        case Randomizer::MoonDifficulty_Insane:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_INSANE;
        case Randomizer::MoonDifficulty_Max:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_MAX;
        default:
            return MenuText::DESC_MOON_REQ_DIFFICULTY_NORMAL;
        }
    case OPT_HINT_COIN_PROGRESSIVE:
        return MenuText::DESC_HINT_COIN_PROGRESSIVE;
    case OPT_HINT_TOKEN_PROGRESSIVE:
        return MenuText::DESC_HINT_TOKEN_PROGRESSIVE;
    case OPT_HINT_COIN_PROGRESSIVE_GLOBAL:
    case OPT_HINT_TOKEN_PROGRESSIVE_GLOBAL:
        return MenuText::DESC_HINT_PROGRESSIVE_GLOBAL;
    case OPT_WORLD_PEACE_REQ:
        switch (optionRollIdx(OPT_WORLD_PEACE_REQ)) {
        case Randomizer::WorldPeaceRequirementMode_OnKingdomComplete:
            return MenuText::DESC_WORLD_PEACE_REQ_ON_KINGDOM_COMPLETE;
        case Randomizer::WorldPeaceRequirementMode_Required:
            return MenuText::DESC_WORLD_PEACE_REQ_REQUIRED;
        default:
            return MenuText::DESC_WORLD_PEACE_REQ_STORY;
        }
    case OPT_MOON_ROCK_KEY:
    case OPT_MOON_ROCK_KEY_ANY_MOON:
    case OPT_MOON_ROCK_KEY_OWN_KINGDOM:
        return MenuText::DESC_MOON_ROCK_KEY;
    case OPT_SUBAREA_SCOPE_ANY_MOON:
    case OPT_SUBAREA_SCOPE_OWN_KINGDOM:
        return "Scope for subarea unlocks";
    case OPT_SUBAREA_UNLOCKS:
        switch (optionRollIdx(OPT_SUBAREA_UNLOCKS)) {
        case Randomizer::SubareaUnlockMode_PerType:
            return MenuText::DESC_SUBAREA_UNLOCKS_PER_TYPE;
        case Randomizer::SubareaUnlockMode_PerKingdom:
            return MenuText::DESC_SUBAREA_UNLOCKS_PER_KINGDOM;
        case Randomizer::SubareaUnlockMode_PerKingdomAndType:
            return MenuText::DESC_SUBAREA_UNLOCKS_PER_KINGDOM_AND_TYPE;
        default:
            return MenuText::DESC_SUBAREA_UNLOCKS_UNLOCKED;
        }
    case OPT_SUBAREA_UNLOCK_HOST:
        switch (optionRollIdx(OPT_SUBAREA_UNLOCK_HOST)) {
        case Randomizer::SubareaUnlockHost_Cap:
            return MenuText::DESC_SUBAREA_UNLOCK_HOST_CAP;
        case Randomizer::SubareaUnlockHost_Moon:
            return MenuText::DESC_SUBAREA_UNLOCK_HOST_MOON;
        case Randomizer::SubareaUnlockHost_AllKingdoms:
            return MenuText::DESC_SUBAREA_UNLOCK_HOST_ALL;
        default:
            return MenuText::DESC_SUBAREA_UNLOCK_HOST_CAP_AND_MOON;
        }
    case OPT_VISITED_KINGDOMS_MAP:
        switch (optionRollIdx(OPT_VISITED_KINGDOMS_MAP)) {
        case Randomizer::VisitedKingdomsMapMode_Visited:
            return MenuText::DESC_VISITED_KINGDOMS_MAP_VISITED;
        case Randomizer::VisitedKingdomsMapMode_Unlocked:
            return MenuText::DESC_VISITED_KINGDOMS_MAP_UNLOCKED;
        case Randomizer::VisitedKingdomsMapMode_Custom:
            return MenuText::DESC_VISITED_KINGDOMS_MAP_CUSTOM;
        default:
            return MenuText::DESC_VISITED_KINGDOMS_MAP_VANILLA;
        }

    case OPT_SKYBOXES:
        return MenuText::DESC_SKYBOXES;
    case OPT_STABILIZE_SHADERS:
        return MenuText::DESC_STABILIZE_SHADERS;
    case OPT_DEATH_LINK:
        return MenuText::DESC_DEATH_LINK;
    case OPT_SYNC_OTHER_TEAMS:
        return MenuText::DESC_SYNC_OTHER_TEAMS;
    case OPT_TEAM:
        return MenuText::DESC_TEAM;
    case OPT_FAST_MOON_DEMOS:
        return MenuText::DESC_FAST_MOON_DEMOS;
    case OPT_MOTION_FREE:
        return MenuText::DESC_MOTION_FREE;
    case OPT_SKIP_CUTSCENE_CAMERAS:
        return MenuText::DESC_SKIP_CUTSCENE_CAMERAS;
    case OPT_FREE_COSTUME_MOON:
        return MenuText::DESC_FREE_COSTUME_MOON;
    case OPT_UNLOCK_HINT_ART:
        return MenuText::DESC_UNLOCK_HINT_ART;
    case OPT_ONE_SHEEP_HERDING:
        return MenuText::DESC_ONE_SHEEP_HERDING;
    case OPT_TRACKER_OVERLAY:
        switch (optionRollIdx(OPT_TRACKER_OVERLAY)) {
        case 1:
            return MenuText::DESC_TRACKER_OVERLAY_BASIC;
        case 2:
            return MenuText::DESC_TRACKER_OVERLAY_EXTENDED;
        case 3:
            return MenuText::DESC_TRACKER_OVERLAY_HINTS;
        case 4:
            return MenuText::DESC_TRACKER_OVERLAY_GUIDED;
        default:
            return MenuText::DESC_TRACKER_OVERLAY_NONE;
        }
    case OPT_TRACKER_CAVE_SKIP:
        switch (optionRollIdx(OPT_TRACKER_CAVE_SKIP)) {
        case 1:
            return MenuText::DESC_TRACKER_CAVE_SKIP_EASY;
        case 2:
            return MenuText::DESC_TRACKER_CAVE_SKIP_HARD;
        default:
            return MenuText::DESC_TRACKER_CAVE_SKIP_NONE;
        }
    case OPT_TRACKER_MOON_LIMITS:
        switch (optionRollIdx(OPT_TRACKER_MOON_LIMITS)) {
        case 1:
            return MenuText::DESC_TRACKER_MOON_LIMITS_UPPER;
        case 2:
            return MenuText::DESC_TRACKER_MOON_LIMITS_FULL;
        default:
            return MenuText::DESC_TRACKER_MOON_LIMITS_NONE;
        }
    case OPT_BOARD_SCALE:
        return MenuText::DESC_BOARD_SCALE;
    case OPT_AREA_NAME_OVERLAY:
        return MenuText::DESC_AREA_NAME_OVERLAY;
    case OPT_NO_CLIP:
        return MenuText::DESC_NO_CLIP;
    case OPT_TRACKER_RIGHT_ALIGN:
        return MenuText::DESC_TRACKER_RIGHT_ALIGN;
    case OPT_TRACKER_TIMER:
        return MenuText::DESC_TRACKER_TIMER;
    case OPT_COMPLETED_KINGDOM_MOON_COLLECTED:
        return MenuText::DESC_COMPLETED_KINGDOM_MOON_COLLECTED;
    case OPT_RANDOMIZE_OUTFITS:
        return MenuText::DESC_RANDOMIZE_OUTFITS;
    case OPT_PURPLE_SHOP_ABILITIES:
        return MenuText::DESC_PURPLE_SHOP_ABILITIES;
    case OPT_PURPLE_SHOP_CAPTURES:
        return MenuText::DESC_PURPLE_SHOP_CAPTURES;
    case OPT_PURPLE_SHOP_MOONS:
        return MenuText::DESC_PURPLE_SHOP_MOONS;
    case OPT_PURPLE_SHOP_PRICE:
        if (Randomizer::getPurpleShopPrice() == Randomizer::PurpleShopPrice::Random)
            return MenuText::DESC_PURPLE_SHOP_PRICE_RANDOM;
        if (Randomizer::isProportionalKingdomPricingEnabled())
            return MenuText::DESC_PURPLE_SHOP_PRICE_PROPORTIONAL;
        return MenuText::DESC_PURPLE_SHOP_PRICE_FLAT;
    case OPT_PROPORTIONAL_KINGDOM_PRICING:
        return MenuText::DESC_PROPORTIONAL_KINGDOM_PRICING;
    case OPT_SHOP_PRICE_RANDO:
        return MenuText::DESC_SHOP_PRICE_RANDO;
    case OPT_EASY_MINIGAMES:
        return MenuText::DESC_EASY_MINIGAMES;
    case OPT_FAST_DOG:
        return "Doggo goes faster and straight to the moon.";
    case OPT_UNCAPPED_COINS:
        return "Coins go past 9999, up to 999999.";
    case OPT_MUSIC:
        switch (Randomizer::getMusicRandoMode()) {
        case Randomizer::MusicRandoMode::Max:
            return MenuText::DESC_MUSIC_MAX;
        case Randomizer::MusicRandoMode::Extended:
            return MenuText::DESC_MUSIC_EXTENDED;
        case Randomizer::MusicRandoMode::On:
            return MenuText::DESC_MUSIC_STANDARD;
        case Randomizer::MusicRandoMode::Off:
        default:
            return MenuText::DESC_MUSIC_OFF;
        }
    case OPT_MUSIC_SOURCE:
        return Randomizer::getMusicSourceModeDescription(Randomizer::getMusicSourceMode());
    case OPT_STREAMER_MODE:
        return MenuText::DESC_STREAMER_MODE;
    case OPT_MUSIC_OVERLAY:
        return MenuText::DESC_MUSIC_OVERLAY;
    case OPT_MUSIC_DISABLE_2D:
        return MenuText::DESC_MUSIC_DISABLE_2D;
    case OPT_MUSIC_DISABLE_EXTRA:
        return MenuText::DESC_MUSIC_DISABLE_EXTRA;
    case OPT_MUSIC_DISABLE_BOSS:
        return MenuText::DESC_MUSIC_DISABLE_BOSS;
    case OPT_ALWAYS_NEUTRAL_CAP_THROW:
        return MenuText::DESC_ALWAYS_NEUTRAL_CAP_THROW;
    case OPT_SPHINX:
        return MenuText::DESC_SPHINX;
    case OPT_HINT_PRICE_MOONS:
    case OPT_HINT_PRICE_CAPTURES:
    case OPT_HINT_PRICE_ABILITIES:
    case OPT_HINT_PRICE_PATH:
    case OPT_HINT_PRICE_PURCHASES:
        return MenuText::DESC_HINT_PRICE;
    case OPT_HINT_PROGRESSIVE_PRICE:
        return MenuText::DESC_HINT_PROGRESSIVE_PRICE;
    case OPT_HINT_PROGRESSIVE_GLOBAL:
        return MenuText::DESC_HINT_PROGRESSIVE_GLOBAL;

    case OPT_SAVE_AS_PRESET:
        return MenuText::DESC_SAVE_AS_PRESET;

    case OPT_GENERATE_SEED: {
        static char sSummary[512];
        nn::util::SNPrintf(sSummary, sizeof(sSummary),
            MenuText::GENERATE_DESCRIPTION_FORMAT,
            Randomizer::getSeed(),
            Randomizer::getRunModeName(Randomizer::getRunMode()),
            Randomizer::isLogicRandoEnabled()
                ? Randomizer::getLogicDifficultyName(Randomizer::getLogicDifficulty())
                : MenuText::VALUE_NONE,
            Randomizer::getUnlockPacingName(Randomizer::getUnlockPacing()),
            Randomizer::getSeedOpennessName(Randomizer::getSeedOpenness()),
            Randomizer::getSeedDifficultyName(Randomizer::getSeedDifficulty()),
            Randomizer::isMoonRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
            moonAllocationSummary(),
            Randomizer::getCurrentEntranceRandoModeName(),
            Randomizer::isPaintingRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
            Randomizer::isKingdomRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
            Randomizer::isStartKingdomStateRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
            Randomizer::getStartPositionModeName(Randomizer::getStartPositionMode()),
            Randomizer::getCurrentMoonRequirementLabel());
        return sSummary;
    }
    case OPT_JOIN_GAME: {
        static char sJoinSummary[256];
        // "From the server", not "from the host". The room's seed and your
        // team's progress live on the server from the moment the seed finishes
        // generating, so a room you already played in comes back whole even
        // with nobody else online - which also makes this the resume path, not
        // just the join one.
        nn::util::SNPrintf(sJoinSummary, sizeof(sJoinSummary),
            MenuText::JOIN_DESCRIPTION_FORMAT,
            Client::getRoomStatusMessage(),
            Client::isRoomReady() ? MenuText::EMPTY_ASCII : MenuText::JOIN_DESCRIPTION_PENDING_SUFFIX);
        return sJoinSummary;
    }
    case OPT_TRACKER_WEB_HEADER:
        return MenuText::DESC_TRACKER_WEB_HEADER;
    case OPT_TRACKER_ROOM_CODE:
        return MenuText::DESC_TRACKER_ROOM_CODE;
    case OPT_ADVANCED_RESET:
        return MenuText::DESC_ADVANCED_RESET;
    default:
        return MenuText::EMPTY_ASCII;
    }
}

// ── user preset snapshot ─────────────────────────────────────────────────

// Captures every settings-page option (every page, including Summary, but
// seed/preset/action rows are skipped) into a tagged (optionId, value) list.
// OPT_MOON_REQUIREMENT is a KIND_ACTION row backed by a real numeric setting
// (not a check/roll), so it's captured explicitly alongside the generic loop.
void StageSceneStateFileSelect::captureCurrentSettingsSnapshot(
    UserPresets::Entry* out, s32* outCount) const {
    s32 n = 0;
    for (s32 pageIdx = 0; pageIdx < kPageCount && n < UserPresets::kMaxEntries; pageIdx++) {
        const PageDef& pd = sPages[pageIdx];
        for (s32 i = 0; i < pd.count && n < UserPresets::kMaxEntries; i++) {
            const OptionDef& o = pd.options[i];
            // Derived roll views have no independent storage. Keep the legacy
            // moon-allocation booleans below so older presets remain portable.
            // The two "moons granting ..." rows are views over the per-kingdom
            // special-moon-use masks, which travel in their own entries.
            if (o.id == OPT_MOON_ALLOCATION_MODE
                || o.id == OPT_CAPTURE_GRANT_KINGDOMS
                || o.id == OPT_ABILITY_GRANT_KINGDOMS
                || o.id == OPT_REQUIREMENT_KINGDOMS)
                continue;

            switch (o.id) {
            case OPT_CURRENT_SEED:
            case OPT_RANDOM_SEED:
            case OPT_SET_SEED:
            case OPT_PRESET:
            case OPT_USER_PRESET:
            case OPT_SAVE_AS_PRESET:
            case OPT_GENERATE_SEED:
            // Room-owned in practice: the server mirrors the host's value to
            // everyone, so a preset carrying someone else's Death Link would
            // either do nothing (as a guest) or silently change a room setting
            // for everybody (as a host). Same reasoning as the tracker room
            // code's exclusion.
            case OPT_DEATH_LINK:
            case OPT_SYNC_OTHER_TEAMS:
            // Not room-owned but PLAYER-owned, and excluded for the mirror
            // image of the same reason: a preset carrying the team number of
            // whoever made it would quietly move you off yours.
            case OPT_TEAM:
                continue;
            default:
                break;
            }

            if (o.id == OPT_MOON_REQUIREMENT) {
                out[n++] = { static_cast<u16>(o.id), Randomizer::getMoonRequirement() };
            } else if (o.id == OPT_MOON_REQ_DEVIATION) {
                out[n++] = { static_cast<u16>(o.id),
                    static_cast<u32>(Randomizer::getMoonRequirementDeviation()
                        + kMoonReqDeviationBias) };
            } else if (o.id == OPT_TOADETTE_MEDAL_AVAILABLE) {
                out[n++] = { static_cast<u16>(o.id),
                    static_cast<u32>(Randomizer::getCustomToadetteMedalAvailable()) };
            } else if (o.id == OPT_TOADETTE_MEDAL_REQUIRED) {
                out[n++] = { static_cast<u16>(o.id),
                    static_cast<u32>(Randomizer::getCustomToadetteMedalRequired()) };
            } else if (isKingdomMoonUseOption(o.id)) {
                // The whole mask, not the display index: a kingdom set to more
                // than one reward has no single named value, and dropping to
                // one would silently narrow it on every preset round trip.
                out[n++] = { static_cast<u16>(o.id),
                    Randomizer::getKingdomMoonUseMask(kingdomMoonUseIndex(o.id)) };
            } else if (o.kind == KIND_CHECK) {
                out[n++] = { static_cast<u16>(o.id), optionCheckState(o.id) ? 1u : 0u };
            } else if (o.kind == KIND_ROLL) {
                s32 value = optionRollIdx(o.id);
                // Older builds only know the four original Triple Moons
                // values. A custom distribution has no legacy equivalent,
                // so carry the closest enabled behavior instead of writing an
                // out-of-range index to a preset an older build must read.
                // no longer adjust value since we only have 3 options
                out[n++] = { static_cast<u16>(o.id), static_cast<u32>(value) };
            }
        }
    }
    if (outCount)
        *outCount = n;
}

// Restores a captured snapshot through the exact same setters a manual UI
// change would use (setOptionCheckState / setOptionRollIdx), so any side
// effects (e.g. GlobalConfig::save() for the QoL toggles that persist
// cross-save) happen exactly as they would from the player's own input.
// Entry ids the current build doesn't recognize (an older/newer preset) are
// silently skipped rather than treated as an error.
void StageSceneStateFileSelect::applySettingsSnapshot(
    const UserPresets::Entry* entries, s32 count) {
    if (!entries)
        return;

    // Wave A target arrays are save-file state, not tagged scalar preset
    // entries yet. Applying a preset therefore starts from automatic rows so
    // a previous custom editor session cannot leak into the new settings.
    Randomizer::clearKingdomMoonTargets();
    Randomizer::clearKingdomTripleMoonTargets();

    bool sawHintPrices = false;
    bool sawHintProgressivePrice = false;
    bool migrateLegacyCoinProgressive = false;
    bool migrateLegacyTokenProgressive = false;
    bool sawLegacyHintPrices = false;
    Randomizer::HintPriceTier legacyHintPriceTier = Randomizer::HintPriceTier::Normal;
    bool sawLegacyHintMode = false;
    Randomizer::HintMode legacyHintMode = Randomizer::HintMode::Basic;
    bool sawLegacyMoonPurchases = false;
    bool legacyMoonPurchases = false;
    bool sawLegacyExtendedHints = false;
    bool legacyExtendedHints = false;
    bool sawExtremeNuzlocke = false;
    bool sawShopLocations = false;
    bool sawMoonReallocation = false;
    bool sawRandomizeMoonAllocation = false;
    bool sawMoonAllocationDifficulty = false;
    bool sawMoonCoinPolicy = false;
    bool sawLegacySeedStyle = false;
    Randomizer::SeedStyle legacySeedStyle = Randomizer::SeedStyle_Standard;
    bool legacyPrioritizeDifficultMovement = false;
    // -1 = the legacy Loading Zones value carried no chain-length meaning.
    s32 legacyChainOpenness = -1;

    // Hint Tokens and the Progressive Price row arrived with the Cap selector
    // reorder. Every current snapshot captures that row, so its absence marks
    // the old Cap roll-index order: Coins, Abilities, Captures, Abilities or
    // Captures, Kingdom Unlock, Hint Tokens.
    bool legacyCapMoonUseRollOrder = true;
    for (s32 e = 0; e < count; e++) {
        if (entries[e].optionId == OPT_HINT_PROGRESSIVE_PRICE) {
            legacyCapMoonUseRollOrder = false;
            break;
        }
    }

    // Seed Style split into three axes at the same time Loading Zones lost its
    // two chain-length entries. A preset that captures any axis row was written
    // after that change; anything else still serializes the old 6-entry Loading
    // Zones roll order (Off, Small Chains, Standard, Long Chains, Limited,
    // Decoupled) and a Seed Style row instead.
    bool legacySeedAxisFormat = true;
    for (s32 e = 0; e < count; e++) {
        const u16 id = entries[e].optionId;
        if (id == OPT_UNLOCK_PACING || id == OPT_SEED_OPENNESS || id == OPT_SEED_DIFFICULTY) {
            legacySeedAxisFormat = false;
            break;
        }
    }

    for (s32 e = 0; e < count; e++) {
        const OptionId id = static_cast<OptionId>(entries[e].optionId);
        switch (id) {
        case OPT_ENABLE_MOON_REALLOCATION:
            sawMoonReallocation = true;
            break;
        case OPT_RANDOMIZE_MOON_ALLOCATION:
            sawRandomizeMoonAllocation = true;
            break;
        case OPT_MOON_ALLOCATION_DIFFICULTY:
            sawMoonAllocationDifficulty = true;
            break;
        case OPT_MOON_COIN_POLICY:
            sawMoonCoinPolicy = true;
            break;
        default:
            break;
        }
        if (id == OPT_MOON_REQUIREMENT) {
            Randomizer::setMoonRequirement(entries[e].value);
            continue;
        }
        if (id == OPT_MOON_REQ_DEVIATION) {
            Randomizer::setMoonRequirementDeviation(static_cast<s16>(
                static_cast<s32>(entries[e].value) - kMoonReqDeviationBias));
            continue;
        }
        if (isKingdomMoonUseOption(id)) {
            Randomizer::setKingdomMoonUseMask(kingdomMoonUseIndex(id),
                static_cast<u8>(entries[e].value));
            continue;
        }
        if (id == OPT_MOON_ALLOCATION_MODE || id == OPT_CAPTURE_GRANT_KINGDOMS
            || id == OPT_ABILITY_GRANT_KINGDOMS || id == OPT_REQUIREMENT_KINGDOMS)
            continue;
        if (id == OPT_CAP_MOON_USE || id == OPT_CLOUD_MOON_USE
            || id == OPT_MOON_MOON_USE || id == OPT_DARK_MOON_USE
            || id == OPT_DARKER_MOON_USE || id == OPT_MUSHROOM_MOON_USE) {
            s32 value = static_cast<s32>(entries[e].value);
            // Before Hint Tokens were added, Cap's six-value roll used the
            // old order Coins, Abilities, Captures, Abilities or Captures,
            // Kingdom Unlock, Hint Tokens. Current snapshots carry a marker
            // row for the new order, so only legacy entries are remapped.
            if (id == OPT_CAP_MOON_USE && legacyCapMoonUseRollOrder) {
                switch (value) {
                case 0:
                    value = 1;
                    break;
                case 1:
                    value = 2;
                    break;
                case 2:
                    value = 3;
                    break;
                case 3:
                    value = 4;
                    break;
                case 4:
                    value = 5;
                    break;
                case 5:
                    value = 0;
                    break;
                default:
                    value = 0;
                    break;
                }
            }
            const bool cap = id == OPT_CAP_MOON_USE;
            const bool cloud = id == OPT_CLOUD_MOON_USE;
            const Randomizer::SpecialMoonUse use = specialMoonUseFromRollIndex(
                value, cloud, cap); // This legacy function is fine for 0,1,2. For 3,4,5 it'll just use the regular mapping which is the same as cap/moon.
            if (id == OPT_CAP_MOON_USE)
                Randomizer::setCapKingdomMoonUse(use);
            else if (id == OPT_CLOUD_MOON_USE)
                Randomizer::setCloudKingdomMoonUse(use);
            else if (id == OPT_MOON_MOON_USE)
                Randomizer::setMoonKingdomMoonUse(use);
            else if (id == OPT_DARK_MOON_USE)
                Randomizer::setDarkKingdomMoonUse(use);
            else if (id == OPT_DARKER_MOON_USE)
                Randomizer::setDarkerKingdomMoonUse(use);
            else if (id == OPT_MUSHROOM_MOON_USE)
                Randomizer::setMushroomKingdomMoonUse(use);
            continue;
        }
        // Retired rows: no page owns them any more, so they would fall through
        // the def lookup below and be dropped. Capture them and migrate after
        // the loop, where the order the two were serialized in cannot matter.
        if (id == OPT_SEED_STYLE_RETIRED) {
            sawLegacySeedStyle = true;
            legacySeedStyle = static_cast<Randomizer::SeedStyle>(entries[e].value);
            continue;
        }
        if (id == OPT_PRIORITIZE_DIFFICULT_MOVEMENT_RETIRED) {
            legacyPrioritizeDifficultMovement = entries[e].value != 0;
            continue;
        }
        if (id == OPT_TOADETTE_MEDAL_AVAILABLE) {
            Randomizer::setCustomToadetteMedalAvailable(static_cast<s32>(entries[e].value));
            continue;
        }
        if (id == OPT_TOADETTE_MEDAL_REQUIRED) {
            Randomizer::setCustomToadetteMedalRequired(static_cast<s32>(entries[e].value));
            continue;
        }
        if (id == OPT_MOON_PURCHASES) {
            sawLegacyMoonPurchases = true;
            legacyMoonPurchases = entries[e].value != 0;
            continue;
        }
        if (id == OPT_EXTENDED_HINTS) {
            sawLegacyExtendedHints = true;
            legacyExtendedHints = entries[e].value != 0;
            continue;
        }
        if (id == OPT_HINT_MODE) {
            sawLegacyHintMode = true;
            legacyHintMode = static_cast<Randomizer::HintMode>(entries[e].value);
            continue;
        }
        if (id == OPT_HINT_PRICE_MOONS || id == OPT_HINT_PRICE_CAPTURES
            || id == OPT_HINT_PRICE_ABILITIES || id == OPT_HINT_PRICE_PATH
            || id == OPT_HINT_PRICE_PURCHASES) {
            sawHintPrices = true;
            // Old Hint Configuration rows exposed a per-category Progressive
            // value.  Convert it to the documented base price and migrate the
            // new, profile-wide additive increment after all rows apply.
            const bool tokenMode = Randomizer::isHintTokenMode();
            if ((tokenMode && entries[e].value == 5u)
                || (!tokenMode && entries[e].value == 8u)) {
                if (tokenMode)
                    migrateLegacyTokenProgressive = true;
                else
                    migrateLegacyCoinProgressive = true;
                setOptionRollIdx(id, tokenMode ? 1 : 3);
                continue;
            }
        }
        if (id == OPT_HINT_PROGRESSIVE_PRICE)
            sawHintProgressivePrice = true;
        if (id == OPT_HINT_PRICES) {
            sawLegacyHintPrices = true;
            legacyHintPriceTier = static_cast<Randomizer::HintPriceTier>(entries[e].value);
            continue;
        }
        if (id == OPT_EXTREME_NUZLOCKE)
            sawExtremeNuzlocke = true;
        if (id == OPT_SHOP_LOCATIONS)
            sawShopLocations = true;

        const OptionDef* def = nullptr;
        for (s32 pageIdx = 0; pageIdx < kPageCount && !def; pageIdx++) {
            const PageDef& pd = sPages[pageIdx];
            for (s32 i = 0; i < pd.count; i++) {
                if (pd.options[i].id == id) {
                    def = &pd.options[i];
                    break;
                }
            }
        }
        if (!def)
            continue;

        if (def->kind == KIND_CHECK)
            setOptionCheckState(id, entries[e].value != 0);
        else if (def->kind == KIND_ROLL) {
            s32 value = static_cast<s32>(entries[e].value);
            if (id == OPT_LOADING_ZONES && legacySeedAxisFormat) {
                // Old order: 0 Off, 1 Small Chains, 2 Standard, 3 Long Chains,
                // 4 Limited, 5 Decoupled. Chain length is Seed Openness now, so
                // the two chain entries become Coupled plus the openness level
                // that reproduces them - and long chains are the LESS open one,
                // because short chains mean more chains and so more independent
                // ways into each kingdom.
                switch (value) {
                case 1:
                    value = 1;
                    legacyChainOpenness = Randomizer::SeedOpenness_Open;
                    break;
                case 2:
                    value = 1;
                    break;
                case 3:
                    value = 1;
                    legacyChainOpenness = Randomizer::SeedOpenness_Compact;
                    break;
                case 4:
                    value = 2;
                    break;
                case 5:
                    value = 3;
                    break;
                default:
                    value = 0;
                    break;
                }
            }
            setOptionRollIdx(id, value);
        }
    }

    // A preset written before the axes existed carries a Seed Style row (or
    // nothing at all) plus the retired difficulty checkbox. Run the same
    // migration the save file uses, then let the retired Loading Zones chain
    // length override the openness the style implied - it is the more specific
    // statement of intent.
    if (legacySeedAxisFormat) {
        Randomizer::applyLegacySeedStyle(
            sawLegacySeedStyle ? legacySeedStyle : Randomizer::SeedStyle_Standard);
        Randomizer::applyLegacyPrioritizeDifficultMovement(legacyPrioritizeDifficultMovement);
        if (legacyChainOpenness >= 0)
            Randomizer::setSeedOpenness(static_cast<Randomizer::SeedOpenness>(legacyChainOpenness));
    }

    // User presets written before Moon Allocation was added have no tagged
    // entries for these settings. Reset only the new fields so an older preset
    // cannot inherit values left behind by a previous menu session.
    if (!sawMoonReallocation)
        Randomizer::setMoonReallocationEnabled(false);
    if (!sawRandomizeMoonAllocation)
        Randomizer::setRandomizeMoonAllocationEnabled(false);
    if (!sawMoonAllocationDifficulty)
        Randomizer::setMoonAllocationDifficulty(
            Randomizer::MoonAllocationDifficulty::Vanilla);
    if (!sawMoonCoinPolicy)
        Randomizer::setMoonCoinPolicy(Randomizer::MoonCoinPolicy::Vanilla);

    // Old tagged presets used the retired three-tier selector. Apply its
    // documented coin profile only when this preset has none of the new
    // category entries; a modern preset is authoritative even in token mode.
    if (!sawHintPrices && sawLegacyHintPrices)
        Randomizer::setHintPriceTier(legacyHintPriceTier);

    // Hint Mode used to be the cumulative availability gate.  Migrate it
    // after all tagged price rows have applied so old mode data disables only
    // the categories it historically hid, preserving any explicit newer price
    // entries. The old separate toggles predate the merged selector and use
    // the same precedence as the original migration: Purchases, then
    // Extended, then Basic.
    if (sawLegacyHintMode) {
        Randomizer::setHintMode(legacyHintMode);
    } else if (sawLegacyMoonPurchases || sawLegacyExtendedHints) {
        Randomizer::HintMode mode = Randomizer::HintMode::Basic;
        if (legacyMoonPurchases)
            mode = Randomizer::HintMode::Purchases;
        else if (legacyExtendedHints)
            mode = Randomizer::HintMode::Extended;
        Randomizer::setHintMode(mode);
    }

    // The retired per-category Progressive entries used 250 coins / 1 token
    // as their starting price.  A modern Progressive Price row wins when one
    // exists; otherwise carry that old meaning into the matching profile.
    if (!sawHintProgressivePrice) {
        // A preset predating this row has no saved increment, so it must not
        // inherit one from whichever profile happened to be configured before
        // the preset was applied.
        Randomizer::setHintCoinProgressiveIncrement(0);
        Randomizer::setHintTokenProgressiveIncrement(0);
        if (migrateLegacyCoinProgressive)
            Randomizer::setHintCoinProgressiveIncrement(3); // +250 coins
        if (migrateLegacyTokenProgressive)
            Randomizer::setHintTokenProgressiveIncrement(1); // +1 Hint Token
    }

    // Presets written before Extreme Nuzlocke existed have no tagged entry for
    // it.  Applying one must reproduce the old behavior rather than leaving a
    // previously selected Extreme setting active.
    if (!sawExtremeNuzlocke)
        Randomizer::setExtremeNuzlockeEnabled(false);

    // Same for Shop Locations: a preset that predates it meant the shops to
    // shuffle with everything else, which is what Randomized does.
    if (!sawShopLocations)
        Randomizer::setShopLocationMode(Randomizer::ShopLocationMode_Randomized);
}

// Short at-a-glance summary of the most seed-defining settings, computed from
// live state at save time (mirrors the OPT_GENERATE_SEED summary above, minus
// the seed number itself — a preset isn't tied to one seed).
void StageSceneStateFileSelect::buildPresetDescription(char* buf, s32 bufSize) const {
    if (!buf || bufSize <= 0)
        return;
    nn::util::SNPrintf(buf, bufSize,
        MenuText::SUMMARY_DESCRIPTION_FORMAT,
        Randomizer::getRunModeName(Randomizer::getRunMode()),
        Randomizer::isLogicRandoEnabled()
            ? Randomizer::getLogicDifficultyName(Randomizer::getLogicDifficulty())
            : MenuText::VALUE_NONE,
        Randomizer::getUnlockPacingName(Randomizer::getUnlockPacing()),
        Randomizer::getSeedOpennessName(Randomizer::getSeedOpenness()),
        Randomizer::getSeedDifficultyName(Randomizer::getSeedDifficulty()),
        Randomizer::isMoonRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
        moonAllocationSummary(),
        Randomizer::getCurrentEntranceRandoModeName(),
        Randomizer::isPaintingRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
        Randomizer::isKingdomRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
        Randomizer::isStartKingdomStateRandoEnabled() ? MenuText::VALUE_ON : MenuText::VALUE_OFF,
        Randomizer::getStartPositionModeName(Randomizer::getStartPositionMode()),
        Randomizer::getCurrentMoonRequirementLabel());
}

// ── info box ────────────────────────────────────────────────────────────────

void StageSceneStateFileSelect::showInfoBox(bool visible) {
    mInfoBoxVisible = visible;
    if (mMenuFooters[MENU_SETTINGS]) {
        const OptionDef* opt = currentOption();
        const bool showApply = opt
            && (opt->id == OPT_PRESET || (opt->id == OPT_USER_PRESET && UserPresets::count() > 0));
        const bool showDelete = opt && opt->id == OPT_USER_PRESET && UserPresets::count() > 0;
        const bool hasAdvanced = opt
            && (advancedFor(opt->id, nullptr, nullptr)
                || opt->id == OPT_PURPLE_SHOP_PRICE);
        // A select has no decide action of its own, so it advertises
        // Advanced on A rather than a "Toggle" that does nothing.
        const bool rollRow = opt && rowKind(*opt) == KIND_ROLL;
        const bool advancedOnA = hasAdvanced && rollRow && mSubPage == SubPage::None;
        const bool hasDecideAction = !rollRow;
        mMenuFooters[MENU_SETTINGS]->changeText(
            StageSceneStateModConfig::makeSettingsFooterText(
                showApply, showDelete, mCurrentColSize > 0,
                hasAdvanced, advancedOnA, hasDecideAction,
                mSubPage != SubPage::None));
    }

    for (s32 i = 0; i < kInfoMaxLines; i++) {
        if (!mInfoBox[i])
            continue;
        applyInfoBoxLineLayout(mInfoBox[i], i);
        if (visible) {
            mInfoBox[i]->startAppear("Appear");
            applyInfoBoxLineLayout(mInfoBox[i], i);
            // Final per-line visibility is set by setInfoBoxText (called right after
            // via updateInfoBox) based on how many lines the description fills.
            setInfoLineVisible(mInfoBox[i], i, false);
        } else {
            al::setPaneString(mInfoBox[i], "TxtOption", MenuText::EMPTY, 0);
            mInfoBox[i]->startEnd("End");
            setInfoLineVisible(mInfoBox[i], i, false);
        }
    }
}

void StageSceneStateFileSelect::updateInfoBox() {
    if (!mInfoBoxVisible)
        return;
    const OptionDef* opt = currentOption();
    if (mMenuFooters[MENU_SETTINGS]) {
        const bool showApply = opt
            && (opt->id == OPT_PRESET || (opt->id == OPT_USER_PRESET && UserPresets::count() > 0));
        const bool showDelete = opt && opt->id == OPT_USER_PRESET && UserPresets::count() > 0;
        const bool hasAdvanced = opt
            && (advancedFor(opt->id, nullptr, nullptr)
                || opt->id == OPT_PURPLE_SHOP_PRICE);
        // A select has no decide action of its own, so it advertises
        // Advanced on A rather than a "Toggle" that does nothing.
        const bool rollRow = opt && rowKind(*opt) == KIND_ROLL;
        const bool advancedOnA = hasAdvanced && rollRow && mSubPage == SubPage::None;
        const bool hasDecideAction = !rollRow;
        mMenuFooters[MENU_SETTINGS]->changeText(
            StageSceneStateModConfig::makeSettingsFooterText(
                showApply, showDelete, mCurrentColSize > 0,
                hasAdvanced, advancedOnA, hasDecideAction,
                mSubPage != SubPage::None));
    }
    if (opt && opt->id == OPT_MOTION_FREE) {
        setInfoBoxIconLines();
        return;
    }
    const char* desc = opt ? optionDescription(opt->id) : MenuText::EMPTY_ASCII;
    if (opt && isKingdomMoonUseOption(opt->id))
        desc = makeKingdomMoonUseDescription(kingdomMoonUseIndex(opt->id));
    if (opt && (advancedFor(opt->id, nullptr, nullptr) || opt->id == OPT_PURPLE_SHOP_PRICE)) {
        nn::util::SNPrintf(mInfoDescriptionBuf, sizeof(mInfoDescriptionBuf), "%s%s",
            desc ? desc : MenuText::EMPTY_ASCII, MenuText::DESC_SUFFIX_ADVANCED);
        desc = mInfoDescriptionBuf;
    }
    setInfoBoxText(desc);
}

const char* StageSceneStateFileSelect::makeKingdomMoonUseDescription(s32 worldId) {
    char name[64] = {};
    const char* display = WorldMeta::getDisplayNameForWorldId(worldId);
    if (isKingdomMoonUseMirrored(worldId))
        display = "Dark / Darker Side";
    nn::util::SNPrintf(name, sizeof(name), "%s", display ? display : "");

    // Listed in the select's own order so the sentence reads the same way the
    // advanced page is laid out.
    const char* effects[kKingdomMoonUseOrderCount] = {};
    s32 count = 0;
    for (s32 i = 0; i < kingdomMoonUseNamedCount(worldId); i++) {
        const Randomizer::SpecialMoonUse use = kingdomMoonUseOrder(worldId)[i];
        if (Randomizer::isKingdomMoonUseSelected(worldId, use))
            effects[count++] = specialMoonUseEffectText(use);
    }

    char list[0x200] = {};
    s32 written = 0;
    for (s32 i = 0; i < count; i++) {
        const char* separator = i == 0 ? "" : (i == count - 1 ? " and " : ", ");
        written += nn::util::SNPrintf(list + written,
            static_cast<s32>(sizeof(list)) - written, "%s%s", separator,
            effects[i] ? effects[i] : "");
        if (written >= static_cast<s32>(sizeof(list)) - 1)
            break;
    }

    nn::util::SNPrintf(mRowDescriptionBuf, sizeof(mRowDescriptionBuf),
        "%s: %s.", name, count > 0 ? list : "nothing");
    return mRowDescriptionBuf;
}

void StageSceneStateFileSelect::setInfoBoxIconLines() {
    for (s32 i = 0; i < kInfoMaxLines; i++) {
        if (!mInfoBox[i])
            continue;
        const char16_t* line = StageSceneStateModConfig::getMotionFreeHintLine(i);
        applyInfoBoxLineLayout(mInfoBox[i], i);
        al::setPaneString(mInfoBox[i], "TxtOption", line ? line : MenuText::EMPTY, 0);
        setInfoLineVisible(mInfoBox[i], i, mInfoBoxVisible && (i == 0 || line != nullptr));
    }
}

void StageSceneStateFileSelect::setInfoBoxText(const char* text) {
    if (!mInfoBox[0])
        return;

    // No description → hide every line (including the box on line 0).
    if (!text || !text[0]) {
        for (s32 i = 0; i < kInfoMaxLines; i++) {
            if (!mInfoBox[i])
                continue;
            mInfoText[i].copy(MenuText::EMPTY);
            al::setPaneString(mInfoBox[i], "TxtOption", MenuText::EMPTY, 0);
            setInfoLineVisible(mInfoBox[i], i, false);
        }
        return;
    }

    // Word-wrap into '\n'-separated lines, then hand each line to its own layout so
    // no single TxtOption textbox exceeds its small (~54 glyph) buffer.
    char wrapped[0x200];
    wrapText(text, wrapped, sizeof(wrapped), kInfoWrapCols);

    s32 lineStart = 0;
    s32 lineIdx = 0;
    const s32 wrappedLen = static_cast<s32>(strlen(wrapped));
    for (s32 i = 0; i <= wrappedLen && lineIdx < kInfoMaxLines; i++) {
        if (wrapped[i] != '\n' && wrapped[i] != '\0')
            continue;

        char line[0x80];
        s32 lineLen = i - lineStart;
        if (lineLen > static_cast<s32>(sizeof(line)) - 1)
            lineLen = sizeof(line) - 1;
        for (s32 k = 0; k < lineLen; k++)
            line[k] = wrapped[lineStart + k];
        line[lineLen] = '\0';

        mInfoText[lineIdx].convertFromMultiByteString(line, strlen(line));
        if (mInfoBox[lineIdx]) {
            applyInfoBoxLineLayout(mInfoBox[lineIdx], lineIdx);
            al::setPaneString(mInfoBox[lineIdx], "TxtOption", mInfoText[lineIdx].cstr(), 0);
            // line 0 stays visible (it draws the box); text lines show only when used.
            setInfoLineVisible(mInfoBox[lineIdx], lineIdx, mInfoBoxVisible && (lineIdx == 0 || lineLen > 0));
        }

        lineStart = i + 1;
        lineIdx++;
    }

    // Clear/hide any lines this description did not fill.
    for (; lineIdx < kInfoMaxLines; lineIdx++) {
        if (!mInfoBox[lineIdx])
            continue;
        mInfoText[lineIdx].copy(MenuText::EMPTY);
        al::setPaneString(mInfoBox[lineIdx], "TxtOption", MenuText::EMPTY, 0);
        setInfoLineVisible(mInfoBox[lineIdx], lineIdx, mInfoBoxVisible && lineIdx == 0);
    }
}

// ============================================================================
// Menu transitions / input
// ============================================================================

void StageSceneStateFileSelect::startMenu(Menu menu, const al::Nerve* nerve, bool killPrevious) {
    SimpleLayoutMenu* previousMenu = mCurrentMenu;
    CommonVerticalList* previousList = mCurrentList;
    SimpleLayoutMenu* nextMenu = mMenuList[menu];
    CommonVerticalList* nextList = mOptionsList[menu];

    if (previousList && previousList != nextList) {
        previousList->deactivate();
        // The list rows are separate RollParts actors, NOT children of the menu
        // layout's pane tree — the menu's "End" animation does not hide them.
        // hideAll() plays each row's "Hide" action, which is what actually removes
        // them, so the old menu's rows don't draw over the next menu.
        previousList->hideAll();
    }

    if (previousMenu && previousMenu != nextMenu) {
        setMenuContentsVisible(previousMenu, false);
        previousMenu->startEnd("End");
    }
    (void)killPrevious;

    mCurrentMenu = nextMenu;
    mCurrentList = nextList;
    setMenuContentsVisible(mCurrentMenu, true);
    mCurrentMenu->startAppear("Appear");
    al::setNerve(this, nerve);
}

bool StageSceneStateFileSelect::tryUploadRoomSnapshot() {
    // Only the setup owner has a seed to publish, and only from the real
    // multiplayer tree - the guest tree holds a copy of someone else's.
    if (mSaveCategory != SaveRedirect::Category::Multiplayer)
        return true;
    // In the multiplayer owner tree this is not an optional side effect. If
    // the connection disappeared (or the room no longer recognises us as its
    // owner), continuing would launch the only copy locally and put the run
    // straight back on the host console.
    if (!Client::isSocketActive() || !Client::isSetupOwner()) {
        MpDebugLog::write("StageSceneStateFileSelect: room snapshot upload unavailable active=%d owner=%d",
            Client::isSocketActive() ? 1 : 0, Client::isSetupOwner() ? 1 : 0);
        return false;
    }

    // Blocking, and deliberately so: the room stays in RoomState::Configuring
    // (i.e. nobody can join it) until this lands, and the very next thing this
    // state does is hand off to the stage load, where there is no longer a
    // screen to report a failure on. A few hundred KB over LAN is a short
    // hitch on a screen that is already about to fade out - the same tradeoff
    // startSaveDataWriteSync() above already makes.
    const bool uploaded = RoomSnapshot::upload(mSelectedSlot, mSaveCategory);
    Randomizer::logPush("FS room snapshot upload slot=%d ok=%d: %s", mSelectedSlot,
        uploaded ? 1 : 0, RoomSnapshot::getMessage());
    MpDebugLog::write("StageSceneStateFileSelect: room snapshot upload ok=%d msg=%s",
        uploaded ? 1 : 0, RoomSnapshot::getMessage());
    return uploaded;
}

void StageSceneStateFileSelect::startActivateSlot(StartMode mode) {
    mStartMode = mode;
    mActivateWriteStarted = false;
    mLoadAutoGrantWriteStarted = false;
    mGuestReplayRequested = false;
    Randomizer::logPush("FS ACTIVATE start mode=%d slot=%d active=%d",
        (int)mode, mSelectedSlot, SaveRedirect::getActiveSlot());
    showInfoBox(false);

    if (mCurrentList) {
        mCurrentList->deactivate();
        mCurrentList->hideAll();
    }

    if (mCurrentMenu) {
        setMenuContentsVisible(mCurrentMenu, false);
        mCurrentMenu->startEnd("End");
    }

    mCurrentMenu = mMenuList[MENU_SLOT_ACTION];
    mCurrentList = mOptionsList[MENU_SLOT_ACTION];
    if (mCurrentList) {
        mCurrentList->deactivate();
        mCurrentList->hideAll();
    }
    if (mCurrentMenu) {
        setMenuContentsVisible(mCurrentMenu, false);
        mCurrentMenu->startEnd("End");
    }

    al::setNerve(this, &NrvStageSceneStateFileSelect.ActivateSlot);
}

void StageSceneStateFileSelect::returnToSlotList(bool killPrevious) {
    showInfoBox(false);
    refreshSlotMeta();
    rebuildSlotList();
    startMenu(MENU_SLOT_LIST, &NrvStageSceneStateFileSelect.SlotList, killPrevious);
}

void StageSceneStateFileSelect::handleListInput() {
    mInput->update();
    mCurrentList->update();

    if (mInput->isHoldUiUp() && mInput->isRepeatUiUp()) {
        // mTopSelectableIdx is the list's SCROLL offset (updateParts maps part i
        // to data row mTopSelectableIdx + i - 1), not the first row - it only
        // reads as "the top" on a list short enough never to scroll. The slot
        // list now has more rows than part slots, so compare against row 0 or
        // Up would wrap to the bottom from the top of the visible window.
        if (mInput->isTriggerUiUp() && mCurrentList->mCurSelected == 0)
            mCurrentList->jumpBottom();
        mCurrentList->up();
    }

    if (mInput->isHoldUiDown() && mInput->isRepeatUiDown()) {
        if (mInput->isTriggerUiDown() && mCurrentList->mCurSelected == (mCurrentList->mDataCount - 1))
            mCurrentList->jumpTop();
        mCurrentList->down();
    }

    if (rs::isTriggerUiDecide(mHost))
        deactivateInput();

    // Re-hide stray checkboxes/rolls every frame for the plain fixed menus
    // (action / delete / progress). Paged settings menus set their per-row
    // styles once and must NOT be flattened here. The slot list's rows are file
    // cards from a different parts layout with none of those panes, so it gets
    // its own styling pass - configureList would hide panes it does not have.
    if (mCurrentList == mOptionsList[MENU_SLOT_LIST])
        applySlotRowStyles();
    else
        configureList(mCurrentList);
}

void StageSceneStateFileSelect::activateInput() {
    mInput->reset();
    mCurrentList->activate();
    mCurrentList->appearCursor();
    mIsDecide = false;
}

void StageSceneStateFileSelect::deactivateInput() {
    if (mCurrentMenu)
        al::startHitReaction(mCurrentMenu, "\xe6\xb1\xba\xe5\xae\x9a", nullptr);
    mCurrentList->endCursor();
    mCurrentList->decide();
    mIsDecide = true;
}

void StageSceneStateFileSelect::deleteSelectedSlot() {
    if (mSelectedSlot < 1 || mSelectedSlot > kSlotCount)
        return;

    const s32 idx = mSelectedSlot - 1;
    u32 seed = mSlotHeader[idx].seed;

    if (!SaveRedirect::deleteSlot(mSelectedSlot, seed, mSaveCategory)) {
        Randomizer::logPush("FS delete slot=%d failed", mSelectedSlot);
        return;
    }

    mSlotLoadFailed[idx] = false;
    mSlotCompatible[idx] = false;
    mSlotHasData[idx] = false;
    mSlotHeader[idx] = {};
    mSlotMeta[idx] = {};
}

void StageSceneStateFileSelect::updateMenus() {
    // The pause-menu FileSelect nerve already pumps this layout kit with
    // updateKitList(). Manually ticking each menu here double-steps animations.
}

// ============================================================================
// Nerve handlers
// ============================================================================

void StageSceneStateFileSelect::exeSlotList() {
    if (al::isFirstStep(this)) {
        mCurrentMenu = mMenuList[MENU_SLOT_LIST];
        mCurrentList = mOptionsList[MENU_SLOT_LIST];
        activateInput();

        // Guest join mode has no slot to pick: the host's save lands in the
        // guest root's one fixed slot (Category::MultiplayerGuest, wiped and
        // rewritten on every join), so a file list would only be offering a
        // choice that does not exist. Step straight through to the
        // local-settings pages, which end on Join Game.
        //
        // Same shape as exeSlotAction's New Game row - enterPage(0) from
        // inside a nerve update, so the nerve change lands the way every other
        // transition on this screen does. Costs one frame of the slot list
        // being on screen, which is the price of not inventing a new way to
        // change nerves here.
        if (sGuestJoinMode) {
            MpDebugLog::write("FileSelect: guest join mode, skipping slot list");
            mSelectedSlot = kGuestSlot;
            mPresetIdx = 0;
            enterPage(0);
            MpDebugLog::write("FileSelect: guest entered page %d (of %d visible)",
                mCurrentPage, visiblePageCount());
            return;
        }

        // ...and neither does the HOST any more, for the same reason one step
        // earlier. The room's seed is the server's from the moment it finishes
        // generating, and a host who comes back to their own room downloads it
        // like everybody else (the room is Ready by then, so the server hands
        // out RoomRole::Player and this screen opens in guest join mode
        // instead) - so a host's local MP slot is never something they return
        // to, only scratch space for the seed being built right now. Offering
        // a file list here would be offering to load a save the server has no
        // room for.
        //
        // Which leaves picking the room code as the entire choice: an active
        // code joins that room, a fresh one makes you its host and lands you
        // straight on the settings pages, which end on Generate Seed.
        if (sMultiplayerHostMode) {
            MpDebugLog::write("FileSelect: multiplayer host mode, skipping slot list");
            mSelectedSlot = kHostSlot;
            mPresetIdx = 0;
            enterPage(0);
            MpDebugLog::write("FileSelect: host entered page %d (of %d visible)",
                mCurrentPage, visiblePageCount());
            return;
        }
    }

    handleListInput();

    if (rs::isTriggerUiCancel(mHost) && !mIsDecide) {
        mIsDone = true;
        kill();
        return;
    }

    if (mIsDecide && mCurrentList->isDecideEnd()) {
        mSelectedSlot = mCurrentList->mCurSelected + 1;
        rebuildSlotAction();
        startMenu(MENU_SLOT_ACTION, &NrvStageSceneStateFileSelect.SlotAction);
    }
}

void StageSceneStateFileSelect::exeSlotAction() {
    if (al::isFirstStep(this)) {
        activateInput();
        return;
    }

    handleListInput();

    if (rs::isTriggerUiCancel(mHost) && !mIsDecide) {
        returnToSlotList();
        return;
    }

    if (!mIsDecide || !mCurrentList->isDecideEnd())
        return;

    const s32 selected = mCurrentList->mCurSelected;
    const s32 slotIdx = mSelectedSlot - 1;
    const bool compatible = slotIdx >= 0 && slotIdx < kSlotCount && mSlotCompatible[slotIdx];
    const bool hasData = slotIdx >= 0 && slotIdx < kSlotCount && mSlotHasData[slotIdx];

    if (compatible) {
        if (selected == 0) {
            SaveRedirect::setActiveSlot(mSelectedSlot, mSaveCategory);
            if (RandoSaveFile::loadAndApply(mSelectedSlot, mSaveCategory)
                && (!Randomizer::isBoardRunMode() || Bingo::load(mSelectedSlot, mSaveCategory))) {
                // Missing logs are expected for saves created before entrance
                // discovery tracking; load() still resets the in-memory state.
                ArrivalLog::load(mSelectedSlot);
                TrackerBridge::loadRoomCodeFromSlot(mSelectedSlot, mSaveCategory);
                startActivateSlot(StartMode::Load);
                return;
            }
            SaveRedirect::setActiveSlot(-1, mSaveCategory);
            mSlotLoadFailed[slotIdx] = true;
            mSlotCompatible[slotIdx] = false;
            mSlotHasData[slotIdx] = true;
            rebuildSlotAction();
            activateInput();
            return;
        }
        if (selected == 1) {
            mConfirmAction = ConfirmAction::DeleteSlot;
            rebuildDeleteConfirm();
            startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
            return;
        }
        if (selected == 2) {
            returnToSlotList();
            return;
        }
    } else if (hasData) {
        if (selected == 0) {
            mConfirmAction = ConfirmAction::DeleteSlot;
            rebuildDeleteConfirm();
            startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
            return;
        }
        if (selected == 1) {
            returnToSlotList();
            return;
        }
    } else {
        if (selected == 0) {
            // New Game on an empty slot → open the paged settings at the first page.
            mPresetIdx = 0;
            enterPage(0);
            return;
        }
        if (selected == 1) {
            returnToSlotList();
            return;
        }
    }

    activateInput();
}

void StageSceneStateFileSelect::exeDeleteConfirm() {
    if (al::isFirstStep(this)) {
        activateInput();
        return;
    }

    handleListInput();

    const bool isSubPageAction = mConfirmAction == ConfirmAction::RedistributeAllocation
        || mConfirmAction == ConfirmAction::RedistributeTripleMoons;
    const bool isPresetAction = mConfirmAction != ConfirmAction::DeleteSlot
        && !isSubPageAction;

    if (rs::isTriggerUiCancel(mHost) && !mIsDecide) {
        if (isSubPageAction) {
            startMenu(MENU_SETTINGS, &NrvStageSceneStateFileSelect.NewSaveSettings);
            showInfoBox(true);
            activateInput();
            updateInfoBox();
        } else if (isPresetAction) {
            enterPage(mCurrentPage);
        } else {
            rebuildSlotAction();
            startMenu(MENU_SLOT_ACTION, &NrvStageSceneStateFileSelect.SlotAction);
        }
        return;
    }

    if (!mIsDecide || !mCurrentList->isDecideEnd())
        return;

    const bool confirmed = mCurrentList->mCurSelected == 0;
    if (confirmed) {
        switch (mConfirmAction) {
        case ConfirmAction::DeleteSlot:
            deleteSelectedSlot();
            break;
        case ConfirmAction::ApplyDefaultPreset:
            Presets::apply(mConfirmPresetIndex);
            break;
        case ConfirmAction::ApplyUserPreset: {
            s32 entryCount = 0;
            const UserPresets::Entry* entries = UserPresets::getEntries(mConfirmPresetIndex, &entryCount);
            applySettingsSnapshot(entries, entryCount);
            break;
        }
        case ConfirmAction::DeleteUserPreset:
            UserPresets::remove(mConfirmPresetIndex);
            refreshUserPresetRollText();
            mUserPresetIdx = UserPresets::count() > 0 ? clampRoll(mUserPresetIdx, UserPresets::count()) : 0;
            break;
        case ConfirmAction::OverwriteUserPreset:
            UserPresets::save(mPendingPresetName, mPendingPresetDescription,
                mPendingPresetEntries, mPendingPresetEntryCount, mConfirmPresetIndex);
            refreshUserPresetRollText();
            break;
        case ConfirmAction::RedistributeAllocation:
        case ConfirmAction::RedistributeTripleMoons:
            // Generation will normalise the explicitly assigned array after
            // this page closes. The array itself is supplied by Phase 4's
            // editor; this flag is the framework hand-off.
            mSubPageBalanceNeedsNormalization = true;
            normalizeSubPageTargets();
            break;
        }
    }

    if (isSubPageAction) {
        if (confirmed)
            exitSubPage();
        else {
            startMenu(MENU_SETTINGS, &NrvStageSceneStateFileSelect.NewSaveSettings);
            showInfoBox(true);
            activateInput();
            updateInfoBox();
        }
    } else if (isPresetAction)
        enterPage(mCurrentPage);
    else
        returnToSlotList(confirmed);
}

void StageSceneStateFileSelect::exeNewSaveSettings() {
    // The info box's "Appear" animation is advanced by updateKitList() in
    // exeFileSelect, which runs just BEFORE this nerve every frame. That animation
    // overwrites the TxtOption position/size applyInfoBoxLineLayout() set on the
    // first frame, so without this the description slid and clipped. Re-pin every
    // line here (after the kit update) so each stays put.
    if (mInfoBoxVisible) {
        for (s32 i = 0; i < kInfoMaxLines; i++)
            if (mInfoBox[i])
                applyInfoBoxLineLayout(mInfoBox[i], i);
    }

    if (al::isFirstStep(this)) {
        activateInput();
        refreshPage();
        updateInfoBox();
        // Guest-mode breadcrumbs, first few entries only - never per frame.
        // MpDebugLog rewrites its whole current chunk file on every call, so a
        // per-frame line here would itself cost more than the frame does.
        if (sGuestJoinMode) {
            static s32 sGuestSettingsEntries = 0;
            if (++sGuestSettingsEntries <= 4)
                MpDebugLog::write("FileSelect: guest settings nerve step0 #%d page=%d rows=%d",
                    sGuestSettingsEntries, mCurrentPage, pageDef(mCurrentPage).count);
        }
        return;
    }

    const bool inSubPage = mSubPage != SubPage::None;

    // L / R shoulder buttons page through the settings. This is the only way
    // to change page on a two-column page, where D-pad left/right moves
    // between the columns instead.
    if (!mIsDecide && !inSubPage) {
        if (al::isPadTriggerL()) {
            changePage(-1);
            return;
        }
        if (al::isPadTriggerR()) {
            changePage(+1);
            return;
        }
    }

    mInput->update();
    mCurrentList->update();

    if (mCurrentColSize > 0) {
        // A grid page shows every row at once, so the scroll offset must stay
        // at zero: applySettingsRowStyles() and the pane layout below both map
        // physical part p to row p-1, and a non-zero offset would shear them
        // apart against each other.
        mCurrentList->mTopSelectableIdx = 0;
        StageSceneStateModConfig::repositionTwoColumnPanes(
            mCurrentList, mRowCount, mCurrentColSize);
    }

    const OptionDef* opt = currentOption();

    // Hint entries are a five-category editor. Keep the user inside the page
    // while L/R changes the category, so the same two controls can configure
    // every hint bank without repeatedly backing out to the parent page.
    if (inSubPage && !mIsDecide) {
        // HintEntry's param is the hint category, not a page index; every other
        // paged sub-page shares subPagePageCount() with buildSubPage() so the
        // rows, labels and this wrap-around all agree on the window.
        const s32 maxPages = mSubPage == SubPage::HintEntry
            ? 5
            : subPagePageCount(mSubPage);

        if (maxPages > 0) {
            if (al::isPadTriggerL() || al::isPadTriggerR()) {
                if (al::isPadTriggerL())
                    mSubPageParam--;
                else
                    mSubPageParam++;
                if (mSubPageParam < 0)
                    mSubPageParam = maxPages - 1; // wrap around
                if (mSubPageParam >= maxPages)
                    mSubPageParam = 0; // wrap around
                buildSubPage();
                mCurrentPage = -1;
                loadPage(mReturnPage);
                activateInput();
                updateInfoBox();
                return;
            }
        }
    }

    // Right stick or D-pad left/right cycles a roll option's value.
    // Trigger-only (no auto-repeat) so tilting the stick doesn't spin
    // through values too fast — one step per press. Pages move on L/R, so
    // the D-pad is free for values here; the two-column pages take the D-pad
    // back for column switching and are right-stick-only (see §3 of the
    // menu rework plan).
    const bool gridPage = mCurrentColSize > 0;

    // Grid pages: D-pad left/right crosses between the columns. The rows are
    // still one linear sequence to the list, so this is a jump of colSize.
    if (gridPage && !mIsDecide) {
        const s32 selected = mCurrentList->mCurSelected;
        s32 target = -1;
        if (mInput->isTriggerUiRight() && selected < mCurrentColSize)
            target = selected + mCurrentColSize;
        else if (mInput->isTriggerUiLeft() && selected >= mCurrentColSize)
            target = selected - mCurrentColSize;
        if (target >= 0 && target < mRowCount) {
            mCurrentList->setSelectedIdx(target, 0);
            updateInfoBox();
            applySettingsRowStyles();
            return;
        }
    }

    if (!mIsDecide && opt && rowKind(*opt) == KIND_ROLL) {
        bool rolled = false;
        if (al::isPadTriggerLeftRightStick()
            || (!gridPage && mInput->isTriggerUiLeft())) {
            rollOption(*opt, -1);
            rolled = true;
        }
        if (al::isPadTriggerRightRightStick()
            || (!gridPage && mInput->isTriggerUiRight())) {
            rollOption(*opt, +1);
            rolled = true;
        }
        if (rolled) {
            updateInfoBox();
        }
    }

    // Y deletes the currently previewed user preset (defaults can't be
    // deleted). Gated on the row actually having a preset to delete.
    if (!mIsDecide && opt && opt->id == OPT_USER_PRESET && UserPresets::count() > 0
        && al::isPadTriggerY()) {
        mConfirmAction = ConfirmAction::DeleteUserPreset;
        mConfirmPresetIndex = userPresetSlotForRoll(mUserPresetIdx);
        rebuildDeleteConfirm();
        startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
        return;
    }

    if (inSubPage && !mIsDecide && al::isPadTriggerPlus()) {
        closeSubPage(true);
        return;
    }

    // Y opens Advanced for checkbox rows only. Selects moved it to A (where the
    // button otherwise did nothing) - see the KIND_ROLL branch in the decide
    // handler.
    if (!inSubPage && !mIsDecide && opt && opt->kind != KIND_ROLL
        && al::isPadTriggerY()) {
        SubPage subPage = SubPage::None;
        s32 subPageParam = 0;
        if (advancedFor(opt->id, &subPage, &subPageParam)) {
            enterSubPage(subPage, subPageParam);
            return;
        }
    }

    if (mInput->isHoldUiUp() && mInput->isRepeatUiUp()) {
        if (mInput->isTriggerUiUp() && mCurrentList->mCurSelected == mCurrentList->mTopSelectableIdx)
            mCurrentList->jumpBottom();
        mCurrentList->up();
        updateInfoBox();
        applySettingsRowStyles();
    }

    if (mInput->isHoldUiDown() && mInput->isRepeatUiDown()) {
        if (mInput->isTriggerUiDown() && mCurrentList->mCurSelected == (mCurrentList->mDataCount - 1))
            mCurrentList->jumpTop();
        mCurrentList->down();
        updateInfoBox();
        applySettingsRowStyles();
    }

    if (rs::isTriggerUiDecide(mHost))
        deactivateInput();

    if (rs::isTriggerUiCancel(mHost) && !mIsDecide) {
        if (inSubPage) {
            closeSubPage(false);
            return;
        }
        if (sGuestJoinMode || sMultiplayerHostMode) {
            // There is no slot list behind these pages in either multiplayer
            // mode - they ARE the first screen. B leaves the file select
            // entirely, back to the title, the same way B on the slot list
            // does for everyone else.
            mIsDone = true;
            kill();
            return;
        }
        returnToSlotList();
        return;
    }

    if (!mIsDecide || !mCurrentList->isDecideEnd())
        return;

    const OptionDef* decided = currentOption();
    if (decided)
        onDecideOption(*decided);
    else
        activateInput();
}

void StageSceneStateFileSelect::exeOpenKeyboardSeed() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_SEED_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_SEED_SUBTEXT);

        char buf[16];
        const u32 seed = Randomizer::getSeed() > kMaxSeedValue
            ? kMaxSeedValue
            : Randomizer::getSeed();
        nn::util::SNPrintf(buf, sizeof(buf), "%u", seed);

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = kMaxKeyboardInputLength;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            Randomizer::setSeed((u32)atoi(mKeyboard->getResult()));

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeOpenKeyboardMoonReq() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_MOON_REQUIREMENT_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_MOON_REQUIREMENT_SUBTEXT);

        char buf[16];
        nn::util::SNPrintf(buf, sizeof(buf), "%u", Randomizer::getMoonRequirement());

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 3;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            Randomizer::setMoonRequirement((u32)atoi(mKeyboard->getResult()));

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeOpenKeyboardMoonReqDeviation() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_MOON_REQUIREMENT_DEVIATION_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_MOON_REQUIREMENT_DEVIATION_SUBTEXT);

        char buf[16];
        nn::util::SNPrintf(buf, sizeof(buf), "%d", Randomizer::getMoonRequirementDeviation());

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 4;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            Randomizer::setMoonRequirementDeviation((s16)atoi(mKeyboard->getResult()));

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeOpenKeyboardSubPageValue() {
    if (!al::isFirstStep(this))
        return;

    if (mCurrentList)
        mCurrentList->deactivate();

    const OptionDef* option = currentOption();
    if (!option) {
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
        return;
    }

    // Moon requirements: the page total, or one kingdom's share of it. Setting
    // the total re-spreads it over the kingdoms; setting a kingdom leaves the
    // others alone and the total re-reads as their sum.
    if (option->id == OPT_ADVANCED_REQ_TOTAL
        || isAdvancedRequirementBaseOption(option->id)) {
        const bool isTotal = option->id == OPT_ADVANCED_REQ_TOTAL;
        const s32 worldId = isTotal
            ? -1
            : advancedRequirementBaseIndex(option->id);
        const s32 current = isTotal
            ? RequirementRando::getRequirementBaseTotal()
            : RequirementRando::getRequirementBase(worldId);
        char reqBuf[16];
        nn::util::SNPrintf(reqBuf, sizeof(reqBuf), "%d", current < 0 ? 0 : current);

        mKeyboard->setHeaderText(isTotal
                ? MenuText::KEYBOARD_REQ_TOTAL_HEADER
                : MenuText::KEYBOARD_REQ_BASE_HEADER);
        mKeyboard->setSubText(isTotal
                ? MenuText::KEYBOARD_REQ_TOTAL_SUBTEXT
                : MenuText::KEYBOARD_REQ_BASE_SUBTEXT);
        mKeyboard->openKeyboard(reqBuf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 4;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled()) {
            s32 value = atoi(mKeyboard->getResult());
            if (value < 0)
                value = 0;
            if (isTotal) {
                RequirementRando::redistributeRequirementBases(value);
            } else if (value == 0) {
                // Zero means the kingdom stops gating: its globe takes the
                // deposit on arrival and the route moves on. The row stays,
                // reading 0, so a number can be typed back into it.
                //
                // Pin the others first, or the distribution simply re-spreads
                // the same total over the kingdoms that are left and the total
                // never moves.
                RequirementRando::authorRequirementBases();
                setRequirementKingdomEnabled(worldId, false);
            } else {
                // Which is what typing a number over that 0 does. Same pinning,
                // for the same reason in reverse: the kingdom being added
                // should raise the total, not take its share out of the others.
                RequirementRando::authorRequirementBases();
                setRequirementKingdomEnabled(worldId, true);
                RequirementRando::setRequirementBase(worldId, value);
            }
            // The main page's Moon Requirement row is this total, so it has to
            // follow whichever of the two the player just typed.
            Randomizer::setMoonRequirement(static_cast<u32>(
                RequirementRando::getRequirementBaseTotal()));
        }

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
        return;
    }

    if (!isMoonAllocationKingdomOption(option->id)
        && !isTripleMoonKingdomOption(option->id)) {
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
        return;
    }

    const bool moonAllocation = isMoonAllocationKingdomOption(option->id);
    const s32 worldId = moonAllocation
        ? moonAllocationKingdomIndex(option->id)
        : tripleMoonKingdomIndex(option->id);
    const s32 current = moonAllocation
        ? Randomizer::getKingdomMoonTarget(worldId)
        : Randomizer::getKingdomTripleMoonTarget(worldId);
    char buf[16];
    nn::util::SNPrintf(buf, sizeof(buf), "%d", current < 0 ? 0 : current);

    mKeyboard->setHeaderText(moonAllocation
            ? MenuText::KEYBOARD_MOON_ALLOCATION_HEADER
            : MenuText::KEYBOARD_TRIPLE_MOON_HEADER);
    mKeyboard->setSubText(moonAllocation
            ? MenuText::KEYBOARD_MOON_ALLOCATION_SUBTEXT
            : MenuText::KEYBOARD_TRIPLE_MOON_SUBTEXT);
    mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
        config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
        config.textMaxLength = 3;
        config.textMinLength = 1;
        config.isUseUtf8 = true;
        config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
    });

    while (!mKeyboard->isDone())
        nn::os::YieldThread();

    if (!mKeyboard->isCancelled()) {
        s32 value = atoi(mKeyboard->getResult());
        if (value < 0)
            value = 0;
        if (moonAllocation) {
            if (value > 657)
                value = 657;
            Randomizer::setKingdomMoonTarget(worldId, static_cast<s16>(value));
            Randomizer::setMoonReallocationEnabled(true);
            Randomizer::setRandomizeMoonAllocationEnabled(true);
        } else {
            if (value > 8)
                value = 8;
            Randomizer::setKingdomTripleMoonTarget(worldId, static_cast<s8>(value));
            Randomizer::setRandomizeTripleMoonsEnabled(true);
        }
    }

    refreshPage();
    al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
}

void StageSceneStateFileSelect::exeOpenKeyboardAdvancedValue() {
    if (!al::isFirstStep(this))
        return;

    if (mCurrentList)
        mCurrentList->deactivate();

    const OptionDef* option = currentOption();
    const OptionId optionId = option ? option->id : OPT_NONE;
    s32 value = 0;
    const char* header = MenuText::KEYBOARD_TIMER_VALUE_HEADER;
    const char* subText = MenuText::KEYBOARD_TIMER_VALUE_SUBTEXT;

    switch (mAdvancedKeyboardField) {
    case AdvancedKeyboardField::PurpleShopPrice:
        value = Randomizer::getPurpleShopCustomPrice();
        header = MenuText::KEYBOARD_PURPLE_SHOP_PRICE_HEADER;
        subText = MenuText::KEYBOARD_PURPLE_SHOP_PRICE_SUBTEXT;
        break;
    case AdvancedKeyboardField::StartingCaptures:
        value = Randomizer::getStartingCaptureCount();
        header = MenuText::KEYBOARD_STARTING_CAPTURES_HEADER;
        subText = MenuText::KEYBOARD_STARTING_CAPTURES_SUBTEXT;
        break;
    case AdvancedKeyboardField::StartingThrows:
        value = Randomizer::getStartingThrowCount();
        header = MenuText::KEYBOARD_STARTING_THROWS_HEADER;
        subText = MenuText::KEYBOARD_STARTING_THROWS_SUBTEXT;
        break;
    case AdvancedKeyboardField::StartingAbilities:
        value = Randomizer::getStartingAbilityCount();
        header = MenuText::KEYBOARD_STARTING_ABILITIES_HEADER;
        subText = MenuText::KEYBOARD_STARTING_ABILITIES_SUBTEXT;
        break;
    case AdvancedKeyboardField::MoonRequirementDeviation:
        value = isAdvancedReqDeviationOption(optionId)
            ? RequirementRando::getRequirementDeviation(
                  advancedReqDeviationIndex(optionId))
            : 0;
        header = MenuText::KEYBOARD_MOON_REQUIREMENT_DEVIATION_HEADER;
        subText = MenuText::KEYBOARD_MOON_REQUIREMENT_DEVIATION_SUBTEXT;
        break;
    case AdvancedKeyboardField::TimerValue:
        if (isAdvancedTimerValueOption(optionId)) {
            switch (advancedTimerValueIndex(optionId)) {
            case 0:
                value = Randomizer::getTimerStartSeconds();
                break;
            case 1:
                value = Randomizer::getTimerMoonBonusSeconds();
                break;
            case 2:
                value = Randomizer::getTimerTripleBonusSeconds();
                break;
            case 3:
                value = Randomizer::getTimerDamagePenaltySeconds();
                break;
            case 4:
                value = Randomizer::getTimerDeathPenaltySeconds();
                break;
            default:
                value = Randomizer::getTimerHalfLifeSeconds();
                break;
            }
        }
        header = MenuText::KEYBOARD_TIMER_VALUE_HEADER;
        subText = MenuText::KEYBOARD_TIMER_VALUE_SUBTEXT;
        break;
    case AdvancedKeyboardField::NuzlockeValue:
        if (isAdvancedNuzlockeValueOption(optionId)) {
            switch (advancedNuzlockeValueIndex(optionId)) {
            case 0:
                value = Randomizer::getSoftNuzlockeLossPercent();
                break;
            case 1:
                value = Randomizer::getSoftNuzlockeAbilityLossPercent();
                break;
            default:
                value = Randomizer::getSoftNuzlockeCaptureLossPercent();
                break;
            }
        }
        header = MenuText::KEYBOARD_NUZLOCKE_VALUE_HEADER;
        subText = MenuText::KEYBOARD_NUZLOCKE_VALUE_SUBTEXT;
        break;
    case AdvancedKeyboardField::HintCustomPrice:
        if (mSubPageParam >= 0 && mSubPageParam < 5)
            value = Randomizer::getHintPriceQuote(
                static_cast<Randomizer::HintPriceCategory>(mSubPageParam), 0)
                        .amount;
        if (value < 1)
            value = 1;
        header = MenuText::KEYBOARD_HINT_PRICE_HEADER;
        subText = MenuText::KEYBOARD_HINT_PRICE_SUBTEXT;
        break;
    case AdvancedKeyboardField::MinigameLow:
        value = Randomizer::getEasyMinigameLow();
        header = MenuText::KEYBOARD_MINIGAME_VALUE_HEADER;
        subText = MenuText::KEYBOARD_MINIGAME_VALUE_SUBTEXT;
        break;
    case AdvancedKeyboardField::MinigameHigh:
        value = Randomizer::getEasyMinigameHigh();
        header = MenuText::KEYBOARD_MINIGAME_VALUE_HEADER;
        subText = MenuText::KEYBOARD_MINIGAME_VALUE_SUBTEXT;
        break;
    case AdvancedKeyboardField::None:
    default:
        mAdvancedKeyboardField = AdvancedKeyboardField::None;
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
        return;
    }

    char buffer[16];
    nn::util::SNPrintf(buffer, sizeof(buffer), "%d", value);
    mKeyboard->setHeaderText(header);
    mKeyboard->setSubText(subText);
    mKeyboard->openKeyboard(buffer, [](nn::swkbd::KeyboardConfig& config) {
        config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
        // Advanced setters clamp the field-specific range; five characters
        // cover the signed deviation and largest price input.
        config.textMaxLength = 5;
        config.textMinLength = 1;
        config.isUseUtf8 = true;
        config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
    });

    while (!mKeyboard->isDone())
        nn::os::YieldThread();

    if (!mKeyboard->isCancelled()) {
        value = std::atoi(mKeyboard->getResult());
        switch (mAdvancedKeyboardField) {
        case AdvancedKeyboardField::PurpleShopPrice:
            Randomizer::setPurpleShopCustomPrice(value);
            Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Custom);
            break;
        case AdvancedKeyboardField::StartingCaptures:
            Randomizer::setStartingCaptureCount(value);
            break;
        case AdvancedKeyboardField::StartingThrows:
            Randomizer::setStartingThrowCount(value);
            break;
        case AdvancedKeyboardField::StartingAbilities:
            Randomizer::setStartingAbilityCount(value);
            break;
        case AdvancedKeyboardField::MoonRequirementDeviation:
            if (isAdvancedReqDeviationOption(optionId)) {
                RequirementRando::setRequirementDeviation(
                    advancedReqDeviationIndex(optionId), value);
            }
            break;
        case AdvancedKeyboardField::TimerValue:
            if (isAdvancedTimerValueOption(optionId)) {
                switch (advancedTimerValueIndex(optionId)) {
                case 0:
                    Randomizer::setTimerStartCustomSeconds(value);
                    break;
                case 1:
                    Randomizer::setTimerMoonBonusCustomSeconds(value);
                    break;
                case 2:
                    Randomizer::setTimerTripleBonusCustomSeconds(value);
                    break;
                case 3:
                    Randomizer::setTimerDamagePenaltyCustomSeconds(value);
                    break;
                case 4:
                    Randomizer::setTimerDeathPenaltyCustomSeconds(value);
                    break;
                default:
                    Randomizer::setTimerHalfLifeCustomSeconds(value);
                    break;
                }
            }
            break;
        case AdvancedKeyboardField::NuzlockeValue:
            if (isAdvancedNuzlockeValueOption(optionId)) {
                switch (advancedNuzlockeValueIndex(optionId)) {
                case 0:
                    Randomizer::setSoftNuzlockeLossCustomPercent(value);
                    break;
                case 1:
                    Randomizer::setSoftNuzlockeAbilityLossCustomPercent(value);
                    break;
                default:
                    Randomizer::setSoftNuzlockeCaptureLossCustomPercent(value);
                    break;
                }
            }
            break;
        case AdvancedKeyboardField::HintCustomPrice:
            if (mSubPageParam >= 0 && mSubPageParam < 5) {
                if (value < 1)
                    value = 1;
                if (value > 9999)
                    value = 9999;
                Randomizer::setHintCustomPrice(
                    static_cast<Randomizer::HintPriceCategory>(mSubPageParam),
                    static_cast<u16>(value));
            }
            break;
        case AdvancedKeyboardField::MinigameLow:
            Randomizer::setEasyMinigameLow(value);
            GlobalConfig::save();
            break;
        case AdvancedKeyboardField::MinigameHigh:
            Randomizer::setEasyMinigameHigh(value);
            GlobalConfig::save();
            break;
        case AdvancedKeyboardField::None:
        default:
            break;
        }
    }

    mAdvancedKeyboardField = AdvancedKeyboardField::None;
    refreshPage();
    al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
}

// The two custom Toadette Medal counts reuse the numeric keyboard flow the seed
// and moon requirement rows already use. Randomizer's setters keep required and
// available consistent with each other, so either can be entered first.
void StageSceneStateFileSelect::exeOpenKeyboardMedalAvailable() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_MEDALS_AVAILABLE_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_MEDALS_AVAILABLE_SUBTEXT);

        char buf[16];
        nn::util::SNPrintf(buf, sizeof(buf), "%d", Randomizer::getCustomToadetteMedalAvailable());

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 3;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            Randomizer::setCustomToadetteMedalAvailable(atoi(mKeyboard->getResult()));

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeOpenKeyboardMedalRequired() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_MEDALS_REQUIRED_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_MEDALS_REQUIRED_SUBTEXT);

        char buf[16];
        nn::util::SNPrintf(buf, sizeof(buf), "%d", Randomizer::getCustomToadetteMedalRequired());

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 3;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            Randomizer::setCustomToadetteMedalRequired(atoi(mKeyboard->getResult()));

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeOpenKeyboardPresetName() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_PRESET_NAME_HEADER);
        mKeyboard->setSubText(MenuText::KEYBOARD_PRESET_NAME_SUBTEXT);

        mKeyboard->openKeyboard(MenuText::EMPTY_ASCII, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = UserPresets::kNameLength - 1;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        const char* result = mKeyboard->getResult();
        if (mKeyboard->isCancelled() || !result || result[0] == '\0') {
            refreshPage();
            al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
            return;
        }

        sead::WFixedSafeString<UserPresets::kNameLength> nameBuf;
        nameBuf.convertFromMultiByteString(result, std::strlen(result));

        const char16_t* src = nameBuf.cstr();
        s32 copyLen = 0;
        while (src && src[copyLen] != u'\0' && copyLen < UserPresets::kNameLength - 1) {
            mPendingPresetName[copyLen] = src[copyLen];
            copyLen++;
        }
        mPendingPresetName[copyLen] = u'\0';

        captureCurrentSettingsSnapshot(mPendingPresetEntries, &mPendingPresetEntryCount);
        buildPresetDescription(mPendingPresetDescription, UserPresets::kDescriptionLength);

        const s32 existing = UserPresets::findByName(mPendingPresetName);
        if (existing >= 0) {
            // Name collides with a saved preset — confirm before overwriting.
            mConfirmAction = ConfirmAction::OverwriteUserPreset;
            mConfirmPresetIndex = existing;
            rebuildDeleteConfirm();
            startMenu(MENU_DELETE_CONFIRM, &NrvStageSceneStateFileSelect.DeleteConfirm);
            return;
        }

        if (UserPresets::isFull()) {
            refreshPage();
            al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
            setInfoBoxText(MenuText::PRESET_LIMIT_REACHED);
            return;
        }

        UserPresets::save(mPendingPresetName, mPendingPresetDescription,
            mPendingPresetEntries, mPendingPresetEntryCount);
        refreshUserPresetRollText();

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

// Mirrors exeOpenKeyboardPresetName() (ASCII text instead of numeric), minus
// the preset-specific collision/snapshot handling this field doesn't need.
void StageSceneStateFileSelect::exeOpenKeyboardTrackerRoomCode() {
    if (al::isFirstStep(this)) {
        if (mCurrentList)
            mCurrentList->deactivate();

        mKeyboard->setHeaderText(MenuText::KEYBOARD_TRACKER_ROOM_CODE_HEADER);
        // The system keyboard still highlights each key as it's pressed -
        // that's OS-level overlay behavior no KeyboardConfig flag can turn
        // off, so masking the result can't hide the keypresses themselves.
        //
        // Kept in sync with the identical string in StageSceneStateModConfig's
        // exeOpenKeyboardTrackerField - this is the second, separate call site
        // for the same "Tracker Room Code" keyboard (file-select wizard vs.
        // pause menu). The long Streamer Mode variant here is what actually
        // triggered the SetGuideText/AbortImpl crash (confirmed via
        // mp_debug.log: SetSubTextUtf8 len=162, the old string, on a build
        // where the pause-menu copy had already been shortened) - fixing only
        // the other copy earlier missed this one entirely.
        mKeyboard->setSubText(Randomizer::isStreamerModeEnabled()
                ? MenuText::KEYBOARD_TRACKER_ROOM_CODE_STREAMER_SUBTEXT
                : MenuText::KEYBOARD_TRACKER_ROOM_CODE_SUBTEXT);

        mKeyboard->openKeyboard(TrackerBridge::getRoomCode(), [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = TrackerBridge::kRoomCodeMaxLength;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
            config.passwordMode = Randomizer::isStreamerModeEnabled()
                ? nn::swkbd::PasswordMode::Hide
                : nn::swkbd::PasswordMode::Show;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled())
            TrackerBridge::setRoomCode(mKeyboard->getResult());

        refreshPage();
        al::setNerve(this, &NrvStageSceneStateFileSelect.NewSaveSettings);
    }
}

void StageSceneStateFileSelect::exeGenerate() {
    const Menu displayMenu = MENU_GENERATE_PROGRESS;

    // Debug: distinguish "not pumped" (only enter#1 ever logs) from "pumped but
    // frame stuck" (enter# climbs, frame does not) from "worker stuck" (frame
    // climbs, SeedGenerator progress does not).
    static s32 sGenEnterCount = 0;
    sGenEnterCount++;
    // The first few entries answer "is this being pumped at all". After that a
    // fixed cadence just repeats itself for the whole generation (which can run
    // for minutes), so only report when something actually moved.
    static s32 sLastLoggedGenProgress = -1;
    static bool sLastLoggedGenRunning = false;
    const s32 genProgress = SeedGenerator::progressPercent();
    const bool genRunning = SeedGenerator::isRunning();
    const bool genStateChanged = sLastLoggedGenProgress != genProgress || sLastLoggedGenRunning != genRunning;
    if (sGenEnterCount <= 12 || genStateChanged) {
        sLastLoggedGenProgress = genProgress;
        sLastLoggedGenRunning = genRunning;
        Randomizer::logPush("exeGenerate: enter#%d frame=%d started=%d done=%d running=%d prog=%d",
            sGenEnterCount, mGenerateFrame, (int)mGenerateStarted,
            (int)SeedGenerator::isDone(), (int)genRunning, genProgress);
    }

    if (mGenerateFrame < 0) {
        Randomizer::logPush("exeGenerate: first step, slot=%d", mSelectedSlot);
        mGenerateFrame = 0;
        mGenerateDoneStep = -1;
        mGenerateStarted = false;
        mGenerateStartFailed = false;
        if (mCurrentList)
            mCurrentList->endCursor();
        mCurrentMenu = mMenuList[displayMenu];
        mCurrentList = mOptionsList[displayMenu];
        for (s32 i = 0; i < MENU_COUNT; i++) {
            if (mMenuList[i])
                setMenuContentsVisible(mMenuList[i], i == displayMenu);
            // End the window AND hide the row actors of the other menus so nothing
            // keeps drawing on top of the progress text.
            if (i != displayMenu && mMenuList[i]) {
                mMenuList[i]->startEnd("End");
                if (mOptionsList[i])
                    mOptionsList[i]->hideAll();
            }
        }

        for (s32 i = 0; i < kMaxMsgCount; i++) {
            mMsgList[displayMenu]->mBuffer[i].copy(MenuText::EMPTY);
            mGenerateEnabled[i] = false;
        }

        al::setPaneString(mMenuList[displayMenu], "TxtOption", MenuText::GENERATE_TITLE, 0);
        mMsgList[displayMenu]->mBuffer[0].copy(MenuText::GENERATE_PROGRESS_ZERO);
        mMsgList[displayMenu]->mBuffer[1].copy(MenuText::GENERATE_PREPARING);
        bindStringList(mOptionsList[displayMenu], mMsgList[displayMenu],
            2, mGenerateEnabled);
        mOptionsList[displayMenu]->setSelectedIdx(0, 0);
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        setMenuContentsVisible(mMenuList[displayMenu], true);
        mMenuList[displayMenu]->startAppear("Appear");
        return;
    }

    mGenerateFrame++;
    if ((mGenerateFrame % 60) == 0) {
        Randomizer::logPush("exeGenerate: frame=%d started=%d done=%d progress=%d status=%s",
            mGenerateFrame, (int)mGenerateStarted, (int)SeedGenerator::isDone(),
            SeedGenerator::progressPercent(), SeedGenerator::status());
    }

    if (!mGenerateStarted) {
        if (mGenerateFrame < kGenerateStartDelayFrames) {
            const s32 warmupProgress = mGenerateFrame / 8;
            mMsgList[displayMenu]->mBuffer[0].format(
                MenuText::GENERATE_PROGRESS_FORMAT, warmupProgress);
            mMsgList[displayMenu]->mBuffer[1].copy(
                SeedGenerator::isPrepared() ? MenuText::GENERATE_READY : MenuText::GENERATE_PREPARING);
            // Re-bind so the updated buffer text is copied into the row panes;
            // updateParts() alone does not re-read the string data.
            bindStringList(mOptionsList[displayMenu], mMsgList[displayMenu], 2, mGenerateEnabled);
            mOptionsList[displayMenu]->updateParts();
            configureList(mOptionsList[displayMenu]);
            return;
        }

        if (mGenerateFrame == kGenerateStartDelayFrames) {
            Randomizer::logPush("exeGenerate: launch frame reached prepared=%d",
                (int)SeedGenerator::isPrepared());
            mMsgList[displayMenu]->mBuffer[0].copy(MenuText::GENERATE_PROGRESS_FOUR);
            mMsgList[displayMenu]->mBuffer[1].copy(MenuText::GENERATE_LAUNCHING);
            mOptionsList[displayMenu]->updateParts();
            configureList(mOptionsList[displayMenu]);
            return;
        }

        mGenerateStarted = true;
        if (!SeedGenerator::start(mSelectedSlot, mSaveCategory)) {
            Randomizer::logPush("exeGenerate: failed to start generator slot=%d running=%d",
                mSelectedSlot, (int)SeedGenerator::isRunning());
            SaveRedirect::setActiveSlot(-1, mSaveCategory);
            mGenerateDoneStep = mGenerateFrame;
            mGenerateStartFailed = true;
            al::setPaneString(mMenuList[displayMenu], "TxtOption", MenuText::GENERATE_FAILED_TITLE, 0);
            mMsgList[displayMenu]->mBuffer[0].copy(MenuText::GENERATE_FAILED);
            mMsgList[displayMenu]->mBuffer[1].copy(MenuText::GENERATE_START_FAILED);
            mOptionsList[displayMenu]->updateParts();
            configureList(mOptionsList[displayMenu]);
            return;
        }

        Randomizer::logPush("exeGenerate: worker started slot=%d", mSelectedSlot);
        mMsgList[displayMenu]->mBuffer[0].copy(MenuText::GENERATE_PROGRESS_FIVE);
        setRow(mMsgList[displayMenu]->mBuffer[1], "%s", SeedGenerator::status());
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        return;
    }

    if (mGenerateStartFailed) {
        if (mGenerateFrame - mGenerateDoneStep < 20)
            return;
        returnToSlotList();
        return;
    }

    if (!SeedGenerator::isDone()) {
        // Real worker progress only — no time-based smoothing. The worker reports
        // placement progress moon-by-moon (46-76%), so the number tracks actual
        // generation work instead of racing ahead on a timer.
        s32 progress = SeedGenerator::progressPercent();
        if (progress > 95)
            progress = 95;
        mMsgList[displayMenu]->mBuffer[0].format(MenuText::GENERATE_PROGRESS_FORMAT, progress);
        const char* status = SeedGenerator::status();
        if (status && status[0]) {
            setRow(mMsgList[displayMenu]->mBuffer[1], "%s", status);
        } else {
            const s32 logCount = Randomizer::logCount();
            if (logCount > 0) {
                const char* latest = Randomizer::logGet(logCount - 1);
                if (latest)
                    setRow(mMsgList[displayMenu]->mBuffer[1], "%s", latest);
            }
        }
        // Re-bind so the updated %/status text is copied into the row panes.
        bindStringList(mOptionsList[displayMenu], mMsgList[displayMenu], 2, mGenerateEnabled);
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        return;
    }

    if (mGenerateDoneStep < 0) {
        mGenerateDoneStep = mGenerateFrame;
        Randomizer::logPush("exeGenerate: thread done succeeded=%d", (int)SeedGenerator::succeeded());
        mMsgList[displayMenu]->mBuffer[0].copy(
            SeedGenerator::succeeded() ? MenuText::GENERATE_PROGRESS_COMPLETE
                                       : MenuText::GENERATE_FAILED);
        setRow(mMsgList[displayMenu]->mBuffer[1], "%s", SeedGenerator::status());
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        return;
    }

    if (mGenerateFrame - mGenerateDoneStep < 20)
        return;

    if (SeedGenerator::succeeded()) {
        // The generator thread already ran the full reinit() (placement + entrance
        // + spoiler) and wrote rando.bin, so the in-memory randomizer state is
        // already correct. Calling loadAndApply() here would re-read the file and
        // re-run reinitDeterministic() on the MAIN thread, freezing the UI; skip it.
        Randomizer::logPush("exeGenerate: succeeded -> ActivateSlot(NewGame)");
        SaveRedirect::setActiveSlot(mSelectedSlot, mSaveCategory);
        startActivateSlot(StartMode::NewGame);
        return;
    }

    Randomizer::logPush("exeGenerate: FAILED -> back to slot list");
    SaveRedirect::setActiveSlot(-1, mSaveCategory);
    al::setPaneString(mMenuList[displayMenu], "TxtOption", MenuText::GENERATE_FAILED_TITLE, 0);
    returnToSlotList();
}

// Guest join mode's Join Game row. Structurally the same as exeGenerate above
// (drive MENU_GENERATE_PROGRESS, poll a background worker, hand the finished
// slot to startActivateSlot), except the worker is RoomSnapshot's download and
// the finished slot is a Load, not a NewGame: the file the host uploaded is
// their own freshly initialised save, so loading it verbatim puts both players
// on byte-identical starting state instead of re-deriving it here and hoping
// the two derivations agree.
// Re-enters the join screen to report a failure raised somewhere else - the
// downloaded save failing to load happens two nerves later, in exeActivateSlot,
// which has no progress menu of its own and whose normal failure exit
// (returnToSlotList) is meaningless for a guest.
void StageSceneStateFileSelect::failGuestJoin(const char16_t* reason) {
    mJoinFailReason = reason;
    mJoinFailed = true;
    mJoinFailureIsUpload = false;
    mStartMode = StartMode::None;
    mJoinFrame = -1; // makes exeJoinRoom rebuild the progress menu
    mJoinDoneStep = -1;
    SaveRedirect::setActiveSlot(-1, SaveRedirect::Category::MultiplayerGuest);
    showInfoBox(false);
    startMenu(MENU_GENERATE_PROGRESS, &NrvStageSceneStateFileSelect.JoinRoom, true);
}

void StageSceneStateFileSelect::failRoomUpload(const char16_t* reason) {
    mJoinFailReason = reason;
    mJoinFailed = true;
    mJoinFailureIsUpload = true;
    // startActivateSlot armed the outer title state's direct-load path. Disarm
    // it before returning to settings; the seed is not a playable room until
    // the server has acknowledged its durable copy.
    mStartMode = StartMode::None;
    mJoinFrame = -1;
    mJoinDoneStep = -1;
    SaveRedirect::setActiveSlot(-1, mSaveCategory);
    showInfoBox(false);
    startMenu(MENU_GENERATE_PROGRESS, &NrvStageSceneStateFileSelect.JoinRoom, true);
}

void StageSceneStateFileSelect::exeJoinRoom() {
    const Menu displayMenu = MENU_GENERATE_PROGRESS;

    if (mJoinFrame < 0) {
        mJoinFrame = 0;
        mJoinDoneStep = -1;

        if (mCurrentList)
            mCurrentList->endCursor();
        mCurrentMenu = mMenuList[displayMenu];
        mCurrentList = mOptionsList[displayMenu];
        for (s32 i = 0; i < MENU_COUNT; i++) {
            if (mMenuList[i])
                setMenuContentsVisible(mMenuList[i], i == displayMenu);
            if (i != displayMenu && mMenuList[i]) {
                mMenuList[i]->startEnd("End");
                if (mOptionsList[i])
                    mOptionsList[i]->hideAll();
            }
        }

        for (s32 i = 0; i < kMaxMsgCount; i++) {
            mMsgList[displayMenu]->mBuffer[i].copy(MenuText::EMPTY);
            mGenerateEnabled[i] = false;
        }

        // failGuestJoin() re-enters here with mJoinFailed already set, to show a
        // reason from a later stage. Do not start a second download in that
        // case - just report and let the shared countdown below return to the
        // page.
        const bool reporting = mJoinFailed;

        al::setPaneString(mMenuList[displayMenu], "TxtOption",
            reporting ? (mJoinFailureIsUpload ? MenuText::JOIN_UPLOAD_FAILED_TITLE
                                              : MenuText::JOIN_FAILED_TITLE)
                      : MenuText::JOIN_TITLE,
            0);
        mMsgList[displayMenu]->mBuffer[0].copy(
            reporting ? (mJoinFailureIsUpload ? MenuText::JOIN_FAILED_UPLOAD
                                              : MenuText::JOIN_FAILED)
                      : MenuText::JOIN_CONTACTING_SERVER);
        mMsgList[displayMenu]->mBuffer[1].copy(reporting
                ? (mJoinFailReason ? mJoinFailReason : MenuText::EMPTY)
                : MenuText::JOIN_CANCEL_HINT);
        bindStringList(mOptionsList[displayMenu], mMsgList[displayMenu], 2, mGenerateEnabled);
        mOptionsList[displayMenu]->setSelectedIdx(0, 0);
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        setMenuContentsVisible(mMenuList[displayMenu], true);
        mMenuList[displayMenu]->startAppear("Appear");

        if (reporting) {
            mJoinDoneStep = mJoinFrame;
            return;
        }

        if (!RoomSnapshot::beginDownload(kGuestSlot)) {
            mJoinFailed = true;
            mJoinDoneStep = mJoinFrame;
        }
        return;
    }

    mJoinFrame++;

    // Every frame, not just on entry: the layout kit re-runs each row's own
    // action as it animates, which brings back the checkbox pane this menu has
    // no use for. exeGenerate re-applies it per frame for the same reason -
    // skipping it here is what put green ticks on the join screen's text rows.
    configureList(mOptionsList[displayMenu]);

    if (mJoinDoneStep < 0) {
        // Backing out mid-wait is the one input this screen takes. It matters
        // because "the host has not made the seed yet" is a completely normal
        // state that can last minutes, and the guest should be able to leave.
        if (rs::isTriggerUiCancel(mHost)) {
            RoomSnapshot::cancel();
            SaveRedirect::setActiveSlot(-1, mSaveCategory);
            enterPage(mCurrentPage);
            return;
        }

        const RoomSnapshot::State state = RoomSnapshot::getState();
        switch (state) {
        case RoomSnapshot::State::Waiting:
            // The server refuses the request outright while the room is still
            // Configuring, so this polls rather than waiting on a push - there
            // is no "room is ready now" packet for a guest to wait on.
            if ((mJoinFrame % 120) == 0)
                RoomSnapshot::retryDownload();
            mMsgList[displayMenu]->mBuffer[0].copy(MenuText::JOIN_WAITING);
            break;
        case RoomSnapshot::State::Downloading:
            mMsgList[displayMenu]->mBuffer[0].format(
                MenuText::JOIN_DOWNLOADING_FORMAT, RoomSnapshot::getProgressPercent());
            break;
        case RoomSnapshot::State::DownloadDone:
            mJoinDoneStep = mJoinFrame;
            mMsgList[displayMenu]->mBuffer[0].copy(MenuText::JOIN_RECEIVED);
            break;
        default:
            mJoinFailed = true;
            mJoinDoneStep = mJoinFrame;
            al::setPaneString(mMenuList[displayMenu], "TxtOption", MenuText::JOIN_FAILED_TITLE, 0);
            mMsgList[displayMenu]->mBuffer[0].copy(MenuText::JOIN_FAILED);
            break;
        }

        setRow(mMsgList[displayMenu]->mBuffer[1], "%s", RoomSnapshot::getMessage());
        bindStringList(mOptionsList[displayMenu], mMsgList[displayMenu], 2, mGenerateEnabled);
        mOptionsList[displayMenu]->updateParts();
        configureList(mOptionsList[displayMenu]);
        return;
    }

    // A failure needs long enough to actually read; a success is just a beat
    // before the stage load takes over.
    if (mJoinFrame - mJoinDoneStep < (mJoinFailed ? 150 : 20))
        return;

    if (mJoinFailed) {
        SaveRedirect::setActiveSlot(-1, mSaveCategory);
        mJoinFailReason = nullptr;
        mJoinFailureIsUpload = false;
        enterPage(mCurrentPage);
        return;
    }

    // From here it is the ordinary "load an existing slot" path, just pointed
    // at the guest root - same sequence as exeSlotAction's Load branch.
    SaveRedirect::setActiveSlot(kGuestSlot, SaveRedirect::Category::MultiplayerGuest);
    // The one line that separates "the download landed but the save reader
    // never saw it" from "the save was read and its contents were wrong".
    // makeReadPath() falls back to <root>/_none/ whenever the active slot is
    // not 1..10, so an activeSlot that is not kGuestSlot here means every
    // subsequent read misses the downloaded files entirely.
    MpDebugLog::write("exeJoinRoom: loading room save slot=%d activeSlot=%d category=%d",
        kGuestSlot, SaveRedirect::getActiveSlot(),
        (int)SaveRedirect::getActiveCategory());
    // File1.bin is expected to be absent here: the host no longer uploads a
    // native save and the guest would discard one anyway, so the slot holds
    // only rando.bin until NewGame writes the save set itself.
    MpDebugLog::write("exeJoinRoom: on disk rando.bin=%lld (-1 = missing)",
        (long long)fileSizeOrMissing(
            makeRandoPath(kGuestSlot, SaveRedirect::Category::MultiplayerGuest).cstr()));

    if (RandoSaveFile::loadAndApply(kGuestSlot, SaveRedirect::Category::MultiplayerGuest)
        && (!Randomizer::isBoardRunMode()
            || Bingo::load(kGuestSlot, SaveRedirect::Category::MultiplayerGuest))) {
        TrackerBridge::loadRoomCodeFromSlot(kGuestSlot, SaveRedirect::Category::MultiplayerGuest);
        Randomizer::logPush("exeJoinRoom: room seed applied -> ActivateSlot(NewGame)");
        MpDebugLog::write("exeJoinRoom: rando.bin applied seed=%u -> ActivateSlot(NewGame)",
            Randomizer::getSeed());
        // NewGame, not Load, and deliberately so.
        //
        // Loading the host's File1.bin was the obvious route - identical start
        // state, no re-derivation - but the game will not accept a save file it
        // did not write itself here. All five reads report success from the
        // right paths and startSaveDataReadAll still hands back a blank
        // GameDataFile (worldId=-1 shines=0 dictEntries=0), even though the
        // bytes are identical to the host's and the record is provably inside
        // them. The file select's own preview path sidesteps the same reader by
        // scanning the raw BYAML instead, and says as much in its comment.
        //
        // NewGame never reads a save at all: it initialises in memory from the
        // rando.bin that just applied and writes the result. Everything it
        // derives - kingdom order, start stage, starting equipment, coins,
        // the progression record - comes from the seed, so a guest running it
        // against the host's rando.bin lands on the same world the host did.
        // It is also the exact path the host itself took, rather than one that
        // has never worked.
        startActivateSlot(StartMode::NewGame);
        return;
    }

    Randomizer::logPush("exeJoinRoom: downloaded room save failed to load");
    MpDebugLog::write("exeJoinRoom: rando.bin failed to load from the guest slot");
    failGuestJoin(MenuText::JOIN_HOST_SEED_READ_FAILED);
}

void StageSceneStateFileSelect::exeActivateSlot() {
    if (al::isFirstStep(this)) {
        Randomizer::logPush("exeActivateSlot: first step mode=%d slot=%d",
            (int)mStartMode, mSelectedSlot);
        // The redirect resolves every read below against these two, so they are
        // what decides whether the native save comes from the guest slot or
        // from an empty _none fallback.
        MpDebugLog::write("exeActivateSlot: first step mode=%d slot=%d activeSlot=%d category=%d",
            (int)mStartMode, mSelectedSlot, SaveRedirect::getActiveSlot(),
            (int)SaveRedirect::getActiveCategory());
        if (mCurrentList)
            mCurrentList->deactivate();
        SaveDataAccessFunction::startSaveDataReadAll(mGameDataHolder);
        return;
    }

    if (!SaveDataAccessFunction::isDoneSave(mGameDataHolder))
        return;

    if (mStartMode == StartMode::Load) {
        Randomizer::logPush("exeActivateSlot: Load done -> setPlayingFileId(0), exit to title");
        GameDataFile* file = mGameDataHolder->getGameDataFile(0);
        KingdomProgress::reset();
        // Same hazard as the progression record: file 0 is reused across slots,
        // so anything cached against that pointer has to be dropped once the
        // newly read save has landed in it.
        Nuzlocke::dropCarriedMoonCache();
        // Splits "the save file was read but its contents were rejected"
        // (everything here reads as a blank save) from "the save loaded and
        // only the hack dictionary is missing" (worldId/shines carry the
        // host's values while dictEntries is 0). The read itself already
        // reported success, so one of those two is what is actually happening.
        MpDebugLog::write("exeActivateSlot: file0 worldId=%d shines=%d dictEntries=%d",
            file ? file->getCurrentWorldId() : -1, file ? file->getTotalShineNum() : -1,
            file ? file->customCountHackDictionary() : -1);
        KingdomProgress::debugLogState(file, "activate-load-before");
        if (!KingdomProgress::load(file)) {
            Randomizer::logPush("exeActivateSlot: invalid progression record slot=%d", mSelectedSlot);
            MpDebugLog::write(
                "exeActivateSlot: progression record missing slot=%d activeSlot=%d category=%d",
                mSelectedSlot, SaveRedirect::getActiveSlot(), (int)mSaveCategory);
            SaveRedirect::setActiveSlot(-1, mSaveCategory);
            const s32 idx = mSelectedSlot - 1;
            if (idx >= 0 && idx < kSlotCount) {
                mSlotLoadFailed[idx] = true;
                mSlotCompatible[idx] = false;
                mSlotHasData[idx] = true;
            }
            // In guest join mode there IS no slot list behind this, and
            // returnToSlotList() would bounce through exeSlotList's guest
            // branch straight onto page 0 - silently dumping the player back
            // on Quality of Life with no hint that the room save failed to
            // load. Report it on the join screen instead.
            if (sGuestJoinMode) {
                failGuestJoin(MenuText::JOIN_ROOM_PROGRESS_MISSING);
                return;
            }
            returnToSlotList();
            return;
        }
        const s32 autoGrantCount = RandoSaveFile::getAutoGrantCount();
        if (autoGrantCount > 0)
            ArrivalLog::recordAutoGrantSummary(autoGrantCount);
        if (mLoadAutoGrantWriteStarted) {
            if (!RandoSaveFile::commitAutoGrantsNotified(
                    mSelectedSlot, mSaveCategory))
                Randomizer::logPush(
                    "exeActivateSlot: load auto-grant notification commit failed slot=%d",
                    mSelectedSlot);
            mLoadAutoGrantWriteStarted = false;
        } else {
            const s32 autoGrantResult = RandoSaveFile::applyAutoGrants(
                GameDataHolderWriter(mGameDataHolder));
            if (autoGrantResult < 0) {
                Randomizer::logPush(
                    "exeActivateSlot: load auto-grant application failed slot=%d",
                    mSelectedSlot);
            } else if (autoGrantResult > 0) {
                mGameDataHolder->setRequireSave();
                SaveDataAccessFunction::startSaveDataWriteSync(mGameDataHolder);
                mLoadAutoGrantWriteStarted = true;
                return;
            }
        }
        const u32 savedForcedPeace = Randomizer::getForcedPeaceKingdoms();
        const u32 rebuiltForcedPeace = rebuildForcedPeaceKingdoms(file);
        Randomizer::setForcedPeaceKingdoms(rebuiltForcedPeace);
        if (savedForcedPeace != rebuiltForcedPeace) {
            Randomizer::logPush(
                "exeActivateSlot: repaired forced-peace slot=%d %08x -> %08x",
                mSelectedSlot, savedForcedPeace, rebuiltForcedPeace);
        }
        if (!RandoSaveFile::updateMeta(
                mSelectedSlot, mGameDataHolder->getGameDataFile(0), mSaveCategory))
            Randomizer::logPush("exeActivateSlot: metadata refresh failed slot=%d", mSelectedSlot);
        // After the progression record has loaded, since the redirect asks the
        // kingdom order where the run starts.
        ProgressionHooks::redirectLoadForChaosRandomization(
            GameDataHolderWriter(mGameDataHolder));
        mGameDataHolder->requestSetPlayingFileId(0);
        mGameDataHolder->setRequireSave();
        if (!tryUploadRoomSnapshot()) {
            failRoomUpload(MenuText::JOIN_UPLOAD_RETRY);
            return;
        }
        prepareTitleSceneForLoad(mHost);
        mIsDone = true;
        kill();
        return;
    }

    if (mStartMode == StartMode::NewGame) {
        if (mActivateWriteStarted) {
            GameDataFile* file = mGameDataHolder->getGameDataFile(0);
            KingdomProgress::debugLogState(file, "new-game-write-complete");
            Randomizer::logPush(
                "FS NEW write-done active=%d file0=%llx curN=%d done=%d",
                SaveRedirect::getActiveSlot(),
                (unsigned long long)(uintptr_t)file,
                file ? file->getCurrentWorldId() : -1,
                SaveDataAccessFunction::isDoneSave(mGameDataHolder));
            if (!RandoSaveFile::commitAutoGrantsNotified(
                    mSelectedSlot, mSaveCategory))
                Randomizer::logPush(
                    "exeActivateSlot: new-game auto-grant notification commit failed slot=%d",
                    mSelectedSlot);
            if (!RandoSaveFile::updateMeta(
                    mSelectedSlot, file, mSaveCategory))
                Randomizer::logPush(
                    "exeActivateSlot: new-game metadata refresh failed slot=%d",
                    mSelectedSlot);
            mGameDataHolder->requestSetPlayingFileId(0);
            // A guest must initialize its native save before the durable team
            // log is replayed. Applying moons/coins before initializeData
            // would look successful and then be overwritten a few frames
            // later. Keep this state alive until the server's replay-complete
            // marker arrives.
            if (mSaveCategory == SaveRedirect::Category::MultiplayerGuest) {
                if (!mGuestReplayRequested) {
                    mGuestReplayRequested = true;
                    if (!RoomSnapshot::beginReplay()) {
                        failGuestJoin(MenuText::JOIN_REPLAY_REQUEST_FAILED);
                    }
                    return;
                }

                const RoomSnapshot::State replayState = RoomSnapshot::getState();
                if (replayState == RoomSnapshot::State::Replaying && Client::isSocketActive())
                    return;
                if (replayState != RoomSnapshot::State::ReplayDone) {
                    failGuestJoin(MenuText::JOIN_REPLAY_FAILED);
                    return;
                }
            }

            // The owner publishes rando.bin (and board.bin when present) only
            // after the freshly generated native save has been initialized.
            if (!tryUploadRoomSnapshot()) {
                failRoomUpload(MenuText::JOIN_UPLOAD_RETRY);
                return;
            }
            prepareTitleSceneForLoad(mHost);
            Randomizer::logPush(
                "exeActivateSlot: NewGame save complete -> exit to title");
            mIsDone = true;
            kill();
            return;
        }

        Randomizer::logPush("exeActivateSlot: NewGame readAll done -> initializeData");
        GameDataFunction::initializeData(GameDataHolderWriter(mGameDataHolder), 0);
        ScenarioPatcher::applyInitialKingdomStates(
            mGameDataHolder->getGameDataFile(0));
        // Tracker discovery is session metadata, not part of the fresh vanilla
        // save we just initialized. Clear it here so a new slot doesn't inherit
        // visited-kingdom reveal state from the previous run.
        Randomizer::setVisitedKingdoms(0);
        Randomizer::resetKingdomVisitOrder();
        // An otherwise-empty slot may retain a sidecar after an interrupted
        // legacy deletion. Ensure a fresh run can never inherit those arrivals.
        ArrivalLog::remove(mSelectedSlot);
        ArrivalLog::reset();
        if (RandoSaveFile::getAutoGrantCount() > 0)
            ArrivalLog::recordAutoGrantSummary(
                RandoSaveFile::getAutoGrantCount());
        // ...then re-seed it for the Unlocked map mode, which starts every
        // kingdom but the first one already "visited" (no-op otherwise).
        Randomizer::applyUnlockedKingdomsMapStart();
        Randomizer::setForcedPeaceKingdoms(0);
        mGameDataHolder->setPlayingFileId(0);
        GameDataFile* file = mGameDataHolder->getGameDataFile(0);
        if (!HintEconomy::initializeSaveState(file))
            Randomizer::logPush("exeActivateSlot: failed to initialize Hint Economy state");
        {
            GameDataHolderWriter writer(mGameDataHolder);
            const s32 unlockedOutfits = unlockAllOutfits(file, mGameDataHolder);
            // Both players independently grant themselves these starting
            // coins on their own seed gen - relaying this one player's
            // grant to whoever's already connected would hand them an
            // unearned +100 on top of their own starting grant (confirmed
            // via a real report: the already-connected/waiting player got
            // an instant +100 the moment the second player generated).
            {
                SuppressCoinSyncScope coinSyncScope;
                GameDataFunction::addCoin(writer, kSeedStartingCoins);
            }
            Randomizer::logPush("exeActivateSlot: granted %d starting coins and unlocked %d outfit pieces",
                kSeedStartingCoins, unlockedOutfits);
            CaptureRando::applyStartingEquipment(writer);
            AbilityRando::applyStartingEquipment(writer);
            const s32 autoGrantResult = RandoSaveFile::applyAutoGrants(writer);
            if (autoGrantResult < 0)
                Randomizer::logPush(
                    "exeActivateSlot: new-game auto-grant application failed slot=%d",
                    mSelectedSlot);
            else if (autoGrantResult > 0)
                Randomizer::logPush(
                    "exeActivateSlot: applied %d auto-grant item(s)",
                    autoGrantResult);
        }
        AbilityOverlay::requestStartingEquipment();
        PaintingRando::requestFreeLinkAnnouncement();
        KingdomProgress::reset();
        Nuzlocke::dropCarriedMoonCache();
        const bool progressionInitialized = KingdomProgress::initializeNew(file);
        Randomizer::logPush("exeActivateSlot: progression initialized=%d valid=%d",
            (int)progressionInitialized,
            (int)(progressionInitialized && KingdomProgress::isValidSave(file)));
        StartPositionRando::armOpeningHudHide();
        const bool redirected = progressionInitialized
            && ProgressionHooks::prepareNewSaveStartStage(
                GameDataHolderWriter(mGameDataHolder), nullptr, nullptr);
        Randomizer::logPush("exeActivateSlot: prepareNewSaveStartStage redirected=%d", (int)redirected);
        KingdomProgress::debugLogState(file, "new-game-before-write");
        if (!redirected) {
            ChangeStageInfo info(mGameDataHolder, "", "CapWorldHomeStage", false, 1,
                ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
            GameDataFunction::tryChangeNextStage(GameDataHolderWriter(mGameDataHolder), &info);
        }
        mGameDataHolder->setRequireSave();
        SaveDataAccessFunction::startSaveDataWriteSync(mGameDataHolder);
        mActivateWriteStarted = true;
        Randomizer::logPush(
            "FS NEW write-started slot=%d active=%d file0=%llx curN=%d done=%d",
            mSelectedSlot, SaveRedirect::getActiveSlot(),
            (unsigned long long)(uintptr_t)file,
            file ? file->getCurrentWorldId() : -1,
            SaveDataAccessFunction::isDoneSave(mGameDataHolder));
        return;
    }

    returnToSlotList();
}
