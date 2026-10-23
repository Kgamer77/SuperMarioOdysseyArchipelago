#include "Randomizer/Randomizer.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EnemySwapRando.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MusicHooks.h"
#include "Randomizer/MusicRando.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/Rng.h"
#include "Randomizer/SeedGenerator.h"
#include "Randomizer/SkyboxRando.h"
#include "Randomizer/SphinxRando.h"
#include "Randomizer/SubareaKeyDB.h"

#include "sead/prim/seadSafeString.h"

#include "MusicDB.h"
#include "Randomizer/SpoilerLog.hpp"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/WorldMeta.h"

#include "hk/diag/diag.h"
#include "nn/util.h"

#include <cstdarg>
#include <cstring>

namespace Randomizer {
    static RunMode sRunMode = RunMode::Standard;
    static bool sDeathLinkEnabled = false;
    static bool sSyncOtherTeamsEnabled = false;
    static u8 sSelectedTeam = 0;

    bool isDeathLinkEnabled() { return sDeathLinkEnabled; }
    void setDeathLinkEnabled(bool enabled) { sDeathLinkEnabled = enabled; }

    bool isSyncOtherTeamsEnabled() { return sSyncOtherTeamsEnabled; }
    void setSyncOtherTeamsEnabled(bool enabled) { sSyncOtherTeamsEnabled = enabled; }

    // Mirrors MultiplayerProtocol::cTeamCount, restated rather than included:
    // this file has no multiplayer includes and multiplayer is behind a feature
    // flag, so the team setting has to remain compilable without it.
    static constexpr u8 cTeamCount = 4;

    u8 getSelectedTeam() { return sSelectedTeam; }
    void setSelectedTeam(u8 team) { sSelectedTeam = team < cTeamCount ? team : 0; }

    // Each run mode now stands on its own feature flag rather than a shared
    // "game modes" one, so membership is a per-mode question. Standard always
    // exists - it is what everything else falls back to.
    static bool isRunModeCompiledIn(RunMode mode) {
        switch (mode) {
        case RunMode::ToadetteMedals:
            return FeatureFlags::ToadetteMedals;
        case RunMode::Bingo:
        case RunMode::Ascend:
            return FeatureFlags::BingoAscend;
        case RunMode::Nuzlocke:
        case RunMode::SoftNuzlocke:
            return FeatureFlags::NuzlockeModes;
        case RunMode::Timer:
            return FeatureFlags::TimerMode;
        case RunMode::Standard:
            return true;
        }
        return false;
    }

    RunMode getRunMode() {
        // Also covers a save written by a build that had the mode compiled in.
        return isRunModeCompiledIn(sRunMode) ? sRunMode : RunMode::Standard;
    }

    void setRunMode(RunMode mode) {
        if (!isRunModeCompiledIn(mode))
            mode = RunMode::Standard;
        sRunMode = mode;
        if (mode == RunMode::ToadetteMedals)
            setBowserWinCondition(BowserWinCondition_ToadetteMedals);
        else
            setBowserWinCondition(BowserWinCondition_DefeatBowser);
    }

    const char* getRunModeName(RunMode mode) {
        switch (mode) {
        case RunMode::ToadetteMedals:
            return "Toadette Medal Hunt";
        case RunMode::Bingo:
            return "Bingo";
        case RunMode::Ascend:
            return "Ascend";
        case RunMode::Nuzlocke:
            return "Nuzlocke";
        case RunMode::SoftNuzlocke:
            return "Soft Nuzlocke";
        case RunMode::Timer:
            return "Timer";
        case RunMode::Standard:
        default:
            return "Standard";
        }
    }

    bool isBoardRunMode() {
        return FeatureFlags::BingoAscend && (sRunMode == RunMode::Bingo || sRunMode == RunMode::Ascend);
    }

    bool isNuzlockeRunMode() {
        return FeatureFlags::NuzlockeModes
            && (sRunMode == RunMode::Nuzlocke || sRunMode == RunMode::SoftNuzlocke);
    }

    bool isSoftNuzlockeRunMode() {
        return FeatureFlags::NuzlockeModes && sRunMode == RunMode::SoftNuzlocke;
    }

    static bool sExtremeNuzlockeEnabled = false;

    bool getExtremeNuzlockeEnabled() {
        return FeatureFlags::NuzlockeModes && sExtremeNuzlockeEnabled;
    }

    void setExtremeNuzlockeEnabled(bool enabled) {
        sExtremeNuzlockeEnabled = FeatureFlags::NuzlockeModes && enabled;
    }

    bool isNuzlockeModesFeatureEnabled() {
        return FeatureFlags::NuzlockeModes;
    }

    bool isTimerRunMode() {
        return FeatureFlags::TimerMode && sRunMode == RunMode::Timer;
    }

    bool isTimerModeFeatureEnabled() {
        return FeatureFlags::TimerMode;
    }

    // â”€â”€ game mode settings â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

    static SoftNuzlockeLoss sSoftNuzlockeLoss = SoftNuzlockeLoss::Half;
    static s32 sSoftNuzlockeLossCustomPercent = 50;

    SoftNuzlockeLoss getSoftNuzlockeLoss() { return sSoftNuzlockeLoss; }

    void setSoftNuzlockeLoss(SoftNuzlockeLoss loss) {
        if (loss == SoftNuzlockeLoss::Custom) {
            sSoftNuzlockeLoss = loss;
            return;
        }
        sSoftNuzlockeLoss = loss > SoftNuzlockeLoss::ThreeQuarters
            ? SoftNuzlockeLoss::Half
            : loss;
    }

    const char* getSoftNuzlockeLossName(SoftNuzlockeLoss loss) {
        switch (loss) {
        case SoftNuzlockeLoss::Quarter:
            return "25%";
        case SoftNuzlockeLoss::ThreeQuarters:
            return "75%";
        case SoftNuzlockeLoss::Custom:
            return "Custom";
        case SoftNuzlockeLoss::Half:
        default:
            return "50%";
        }
    }

    s32 getSoftNuzlockeLossPercent() {
        if (sSoftNuzlockeLoss == SoftNuzlockeLoss::Custom)
            return sSoftNuzlockeLossCustomPercent;
        switch (sSoftNuzlockeLoss) {
        case SoftNuzlockeLoss::Quarter:
            return 25;
        case SoftNuzlockeLoss::ThreeQuarters:
            return 75;
        case SoftNuzlockeLoss::Half:
        default:
            return 50;
        }
    }

    bool isSoftNuzlockeLossCustom() {
        return sSoftNuzlockeLoss == SoftNuzlockeLoss::Custom;
    }

    s32 getSoftNuzlockeLossCustomPercent() {
        return sSoftNuzlockeLossCustomPercent;
    }

    void setSoftNuzlockeLossCustomPercent(s32 percent) {
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        sSoftNuzlockeLossCustomPercent = percent;
        if (percent == 25)
            setSoftNuzlockeLoss(SoftNuzlockeLoss::Quarter);
        else if (percent == 50)
            setSoftNuzlockeLoss(SoftNuzlockeLoss::Half);
        else if (percent == 75)
            setSoftNuzlockeLoss(SoftNuzlockeLoss::ThreeQuarters);
        else
            setSoftNuzlockeLoss(SoftNuzlockeLoss::Custom);
    }

    // Half for both, which is what the moon dial used to impose on them.
    static SoftNuzlockeEquipLoss sSoftNuzlockeAbilityLoss = SoftNuzlockeEquipLoss::Half;
    static SoftNuzlockeEquipLoss sSoftNuzlockeCaptureLoss = SoftNuzlockeEquipLoss::Half;
    static s32 sSoftNuzlockeAbilityLossCustomPercent = 50;
    static s32 sSoftNuzlockeCaptureLossCustomPercent = 50;

    static SoftNuzlockeEquipLoss clampEquipLoss(SoftNuzlockeEquipLoss loss) {
        if (loss == SoftNuzlockeEquipLoss::Custom)
            return loss;
        return loss > SoftNuzlockeEquipLoss::All ? SoftNuzlockeEquipLoss::Half : loss;
    }

    SoftNuzlockeEquipLoss getSoftNuzlockeAbilityLoss() { return sSoftNuzlockeAbilityLoss; }

    void setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss loss) {
        sSoftNuzlockeAbilityLoss = clampEquipLoss(loss);
    }

    SoftNuzlockeEquipLoss getSoftNuzlockeCaptureLoss() { return sSoftNuzlockeCaptureLoss; }

    void setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss loss) {
        sSoftNuzlockeCaptureLoss = clampEquipLoss(loss);
    }

    s32 getSoftNuzlockeEquipLossPercent(SoftNuzlockeEquipLoss loss) {
        switch (loss) {
        case SoftNuzlockeEquipLoss::None:
            return 0;
        case SoftNuzlockeEquipLoss::Quarter:
            return 25;
        case SoftNuzlockeEquipLoss::ThreeQuarters:
            return 75;
        case SoftNuzlockeEquipLoss::All:
            return 100;
        case SoftNuzlockeEquipLoss::Custom:
            return 50;
        case SoftNuzlockeEquipLoss::Half:
        default:
            return 50;
        }
    }

    s32 getSoftNuzlockeAbilityLossPercent() {
        if (sSoftNuzlockeAbilityLoss == SoftNuzlockeEquipLoss::Custom)
            return sSoftNuzlockeAbilityLossCustomPercent;
        return getSoftNuzlockeEquipLossPercent(sSoftNuzlockeAbilityLoss);
    }

    s32 getSoftNuzlockeCaptureLossPercent() {
        if (sSoftNuzlockeCaptureLoss == SoftNuzlockeEquipLoss::Custom)
            return sSoftNuzlockeCaptureLossCustomPercent;
        return getSoftNuzlockeEquipLossPercent(sSoftNuzlockeCaptureLoss);
    }

    bool isSoftNuzlockeAbilityLossCustom() {
        return sSoftNuzlockeAbilityLoss == SoftNuzlockeEquipLoss::Custom;
    }

    bool isSoftNuzlockeCaptureLossCustom() {
        return sSoftNuzlockeCaptureLoss == SoftNuzlockeEquipLoss::Custom;
    }

    s32 getSoftNuzlockeAbilityLossCustomPercent() {
        return sSoftNuzlockeAbilityLossCustomPercent;
    }

    s32 getSoftNuzlockeCaptureLossCustomPercent() {
        return sSoftNuzlockeCaptureLossCustomPercent;
    }

    void setSoftNuzlockeAbilityLossCustomPercent(s32 percent) {
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        sSoftNuzlockeAbilityLossCustomPercent = percent;
        if (percent == 0)
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::None);
        else if (percent == 25)
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::Quarter);
        else if (percent == 50)
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::Half);
        else if (percent == 75)
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::ThreeQuarters);
        else if (percent == 100)
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::All);
        else
            setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss::Custom);
    }

    void setSoftNuzlockeCaptureLossCustomPercent(s32 percent) {
        if (percent < 0)
            percent = 0;
        if (percent > 100)
            percent = 100;
        sSoftNuzlockeCaptureLossCustomPercent = percent;
        if (percent == 0)
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::None);
        else if (percent == 25)
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::Quarter);
        else if (percent == 50)
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::Half);
        else if (percent == 75)
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::ThreeQuarters);
        else if (percent == 100)
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::All);
        else
            setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss::Custom);
    }

    static constexpr s32 kTimerStartSeconds[kTimerStartCount] = { 60, 120, 180, 300 };
    static constexpr s32 kTimerMoonBonusSeconds[kTimerMoonBonusCount] = { 20, 30, 45, 60 };
    static constexpr s32 kTimerTripleBonusSeconds[kTimerTripleBonusCount] = { 30, 45, 60, 90, 120 };

    // All three selectors default to their middle option (3:00 / 45s / 60s)
    // rather than to an end of their range, so the mode opens on a balanced
    // preset and both harsher and gentler settings are one step away. The
    // one-minute opening clock the mode was first specified with is still the
    // first entry of the starting-time roll.
    static s32 sTimerStartIdx = kTimerStartDefaultIdx;
    static s32 sTimerMoonBonusIdx = kTimerMoonBonusDefaultIdx;
    static s32 sTimerTripleBonusIdx = kTimerTripleBonusDefaultIdx;
    static s32 sTimerStartCustomSeconds = kTimerStartSeconds[kTimerStartDefaultIdx];
    static s32 sTimerMoonBonusCustomSeconds = kTimerMoonBonusSeconds[kTimerMoonBonusDefaultIdx];
    static s32 sTimerTripleBonusCustomSeconds = kTimerTripleBonusSeconds[kTimerTripleBonusDefaultIdx];

    static s32 clampIdx(s32 idx, s32 count, s32 fallback) {
        return (idx < 0 || idx >= count) ? fallback : idx;
    }

    s32 getTimerStartIdx() { return sTimerStartIdx; }
    void setTimerStartIdx(s32 idx) {
        sTimerStartIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerStartCount, kTimerStartDefaultIdx);
    }

    s32 getTimerMoonBonusIdx() { return sTimerMoonBonusIdx; }
    void setTimerMoonBonusIdx(s32 idx) {
        sTimerMoonBonusIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerMoonBonusCount, kTimerMoonBonusDefaultIdx);
    }

    s32 getTimerTripleBonusIdx() { return sTimerTripleBonusIdx; }
    void setTimerTripleBonusIdx(s32 idx) {
        sTimerTripleBonusIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerTripleBonusCount, kTimerTripleBonusDefaultIdx);
    }

    static s32 clampTimerSeconds(s32 seconds, s32 max) {
        if (seconds < 0)
            return 0;
        return seconds > max ? max : seconds;
    }

    static s32 findTimerValue(const s32* values, s32 count, s32 value) {
        for (s32 i = 0; i < count; i++) {
            if (values[i] == value)
                return i;
        }
        return kCustomIdx;
    }

    s32 getTimerStartSeconds() {
        return sTimerStartIdx == kCustomIdx
            ? sTimerStartCustomSeconds
            : kTimerStartSeconds[sTimerStartIdx];
    }
    s32 getTimerMoonBonusSeconds() {
        return sTimerMoonBonusIdx == kCustomIdx
            ? sTimerMoonBonusCustomSeconds
            : kTimerMoonBonusSeconds[sTimerMoonBonusIdx];
    }
    s32 getTimerTripleBonusSeconds() {
        return sTimerTripleBonusIdx == kCustomIdx
            ? sTimerTripleBonusCustomSeconds
            : kTimerTripleBonusSeconds[sTimerTripleBonusIdx];
    }

    bool isTimerStartCustom() { return sTimerStartIdx == kCustomIdx; }
    bool isTimerMoonBonusCustom() { return sTimerMoonBonusIdx == kCustomIdx; }
    bool isTimerTripleBonusCustom() { return sTimerTripleBonusIdx == kCustomIdx; }
    s32 getTimerStartCustomSeconds() { return sTimerStartCustomSeconds; }
    s32 getTimerMoonBonusCustomSeconds() { return sTimerMoonBonusCustomSeconds; }
    s32 getTimerTripleBonusCustomSeconds() { return sTimerTripleBonusCustomSeconds; }

    void setTimerStartCustomSeconds(s32 seconds) {
        sTimerStartCustomSeconds = clampTimerSeconds(seconds, 24 * 60 * 60);
        sTimerStartIdx = findTimerValue(kTimerStartSeconds, kTimerStartCount,
            sTimerStartCustomSeconds);
    }

    void setTimerMoonBonusCustomSeconds(s32 seconds) {
        sTimerMoonBonusCustomSeconds = clampTimerSeconds(seconds, 60 * 60);
        sTimerMoonBonusIdx = findTimerValue(kTimerMoonBonusSeconds, kTimerMoonBonusCount,
            sTimerMoonBonusCustomSeconds);
    }

    void setTimerTripleBonusCustomSeconds(s32 seconds) {
        sTimerTripleBonusCustomSeconds = clampTimerSeconds(seconds, 60 * 60);
        sTimerTripleBonusIdx = findTimerValue(kTimerTripleBonusSeconds, kTimerTripleBonusCount,
            sTimerTripleBonusCustomSeconds);
    }

    // Unlike the three dials above, both penalties default to their FIRST entry
    // rather than the middle one: they take time away, so the mode has to keep
    // behaving exactly as it did before they existed until a player asks for
    // them. A zero here is also what every pre-penalty save deserializes to.
    static constexpr s32 kTimerDamagePenaltySeconds[kTimerDamagePenaltyCount] = { 0, 1, 5, 15 };
    static constexpr s32 kTimerDeathPenaltySeconds[kTimerDeathPenaltyCount] = { 0, 15, 30, 45 };

    static constexpr s32 kTimerDamagePenaltyDefaultIdx = 0;
    static constexpr s32 kTimerDeathPenaltyDefaultIdx = 0;

    static s32 sTimerDamagePenaltyIdx = kTimerDamagePenaltyDefaultIdx;
    static s32 sTimerDeathPenaltyIdx = kTimerDeathPenaltyDefaultIdx;
    static s32 sTimerDamagePenaltyCustomSeconds = 0;
    static s32 sTimerDeathPenaltyCustomSeconds = 0;

    s32 getTimerDamagePenaltyIdx() { return sTimerDamagePenaltyIdx; }
    void setTimerDamagePenaltyIdx(s32 idx) {
        sTimerDamagePenaltyIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerDamagePenaltyCount, kTimerDamagePenaltyDefaultIdx);
    }

    s32 getTimerDeathPenaltyIdx() { return sTimerDeathPenaltyIdx; }
    void setTimerDeathPenaltyIdx(s32 idx) {
        sTimerDeathPenaltyIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerDeathPenaltyCount, kTimerDeathPenaltyDefaultIdx);
    }

    s32 getTimerDamagePenaltySeconds() {
        return sTimerDamagePenaltyIdx == kCustomIdx
            ? sTimerDamagePenaltyCustomSeconds
            : kTimerDamagePenaltySeconds[sTimerDamagePenaltyIdx];
    }
    s32 getTimerDeathPenaltySeconds() {
        return sTimerDeathPenaltyIdx == kCustomIdx
            ? sTimerDeathPenaltyCustomSeconds
            : kTimerDeathPenaltySeconds[sTimerDeathPenaltyIdx];
    }
    bool isTimerDamagePenaltyCustom() { return sTimerDamagePenaltyIdx == kCustomIdx; }
    bool isTimerDeathPenaltyCustom() { return sTimerDeathPenaltyIdx == kCustomIdx; }
    s32 getTimerDamagePenaltyCustomSeconds() { return sTimerDamagePenaltyCustomSeconds; }
    s32 getTimerDeathPenaltyCustomSeconds() { return sTimerDeathPenaltyCustomSeconds; }
    void setTimerDamagePenaltyCustomSeconds(s32 seconds) {
        sTimerDamagePenaltyCustomSeconds = clampTimerSeconds(seconds, 60 * 60);
        sTimerDamagePenaltyIdx = findTimerValue(kTimerDamagePenaltySeconds,
            kTimerDamagePenaltyCount, sTimerDamagePenaltyCustomSeconds);
    }
    void setTimerDeathPenaltyCustomSeconds(s32 seconds) {
        sTimerDeathPenaltyCustomSeconds = clampTimerSeconds(seconds, 60 * 60);
        sTimerDeathPenaltyIdx = findTimerValue(kTimerDeathPenaltySeconds,
            kTimerDeathPenaltyCount, sTimerDeathPenaltyCustomSeconds);
    }

    // The award decay, defaulting to its "Disabled" first entry for the same
    // reason the two penalties do. A zero here is also what every save written
    // before the dial existed deserializes to.
    static constexpr s32 kTimerHalfLifeSeconds[kTimerHalfLifeCount]
        = { 0, 60 * 60, 90 * 60, 120 * 60 };

    static constexpr s32 kTimerHalfLifeDefaultIdx = 0;

    static s32 sTimerHalfLifeIdx = kTimerHalfLifeDefaultIdx;
    static s32 sTimerHalfLifeCustomSeconds = 0;

    s32 getTimerHalfLifeIdx() { return sTimerHalfLifeIdx; }
    void setTimerHalfLifeIdx(s32 idx) {
        sTimerHalfLifeIdx = idx == kCustomIdx
            ? kCustomIdx
            : clampIdx(idx, kTimerHalfLifeCount, kTimerHalfLifeDefaultIdx);
    }

    s32 getTimerHalfLifeSeconds() {
        return sTimerHalfLifeIdx == kCustomIdx
            ? sTimerHalfLifeCustomSeconds
            : kTimerHalfLifeSeconds[sTimerHalfLifeIdx];
    }
    bool isTimerHalfLifeCustom() { return sTimerHalfLifeIdx == kCustomIdx; }
    s32 getTimerHalfLifeCustomSeconds() { return sTimerHalfLifeCustomSeconds; }
    void setTimerHalfLifeCustomSeconds(s32 seconds) {
        sTimerHalfLifeCustomSeconds = clampTimerSeconds(seconds, 24 * 60 * 60);
        sTimerHalfLifeIdx = findTimerValue(kTimerHalfLifeSeconds, kTimerHalfLifeCount,
            sTimerHalfLifeCustomSeconds);
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ seed Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

    static u32 sSeed = 0;

    u32 getSeed() { return sSeed; }
    void setSeed(u32 seed) { sSeed = seed; }

    // Ã¢â€â‚¬Ã¢â€â‚¬ moon requirement Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

    static MoonDifficulty sMoonDifficulty = MoonDifficulty_Medium;
    static MoonRequirement sMoonRequirement = MoonRequirement_124;
    static u32 sMoonRequirementTotal = 124;
    static LogicDifficulty sLogicDifficulty = LogicDifficulty_Casual;
    static UnlockPacing sUnlockPacing = UnlockPacing_Standard;
    static SeedOpenness sSeedOpenness = SeedOpenness_Standard;
    static SeedDifficulty sSeedDifficulty = SeedDifficulty_Standard;
    static bool sStartKingdomStateRando = false;
    static bool sChestOrderRando = false;
    static bool sCoinRando = false;
    static bool sShopPriceRando = false;
    static bool sProportionalKingdomPricing = false;

    static bool isValidMoonDifficulty(MoonDifficulty difficulty) {
        return difficulty == MoonDifficulty_Easy
            || difficulty == MoonDifficulty_Medium
            || difficulty == MoonDifficulty_Hard
            || difficulty == MoonDifficulty_Harder
            || difficulty == MoonDifficulty_Hardest
            || difficulty == MoonDifficulty_Insane
            || difficulty == MoonDifficulty_Max;
    }

    static bool isValidMoonRequirement(MoonRequirement requirement) {
        return requirement == MoonRequirement_64 || requirement == MoonRequirement_124 || requirement == MoonRequirement_256;
    }

    static bool isValidLogicDifficulty(LogicDifficulty difficulty) {
        return difficulty == LogicDifficulty_Casual || difficulty == LogicDifficulty_Advanced
            || difficulty == LogicDifficulty_Expert || difficulty == LogicDifficulty_Glitched;
    }

    static bool isValidSeedStyle(SeedStyle style) {
        return style <= SeedStyle_Mystery;
    }

    static bool isValidSeedAxis(u8 value) { return value < cSeedAxisOptionCount; }

    // MurmurHash3's 32-bit finalizer after a Weyl increment. Separate salts make
    // each Mystery axis independent while keeping resolution a pure seed function.
    static u32 mixSeedStyleAxis(u32 value) {
        value += 0x9E3779B9u;
        value = (value ^ (value >> 16)) * 0x85EBCA6Bu;
        value = (value ^ (value >> 13)) * 0xC2B2AE35u;
        return value ^ (value >> 16);
    }

    MoonDifficulty getMoonDifficulty() { return sMoonDifficulty; }

    void setMoonDifficulty(MoonDifficulty difficulty) {
        if (!isValidMoonDifficulty(difficulty))
            difficulty = MoonDifficulty_Medium;
        sMoonDifficulty = difficulty;
    }

    void cycleMoonDifficulty() {
        switch (sMoonDifficulty) {
        case MoonDifficulty_Easy:
            setMoonDifficulty(MoonDifficulty_Medium);
            break;
        case MoonDifficulty_Medium:
            setMoonDifficulty(MoonDifficulty_Hard);
            break;
        case MoonDifficulty_Hard:
            setMoonDifficulty(MoonDifficulty_Harder);
            break;
        case MoonDifficulty_Harder:
            setMoonDifficulty(MoonDifficulty_Hardest);
            break;
        case MoonDifficulty_Hardest:
            setMoonDifficulty(MoonDifficulty_Insane);
            break;
        case MoonDifficulty_Insane:
            setMoonDifficulty(MoonDifficulty_Max);
            break;
        default:
            setMoonDifficulty(MoonDifficulty_Easy);
            break;
        }
    }

    const char* getMoonDifficultyName(MoonDifficulty difficulty) {
        switch (difficulty) {
        case MoonDifficulty_Easy:
            return "Easy";
        case MoonDifficulty_Hard:
            return "Hard";
        case MoonDifficulty_Harder:
            return "Random Hard";
        case MoonDifficulty_Hardest:
            return "Random Harder";
        case MoonDifficulty_Insane:
            return "Insane";
        case MoonDifficulty_Max:
            return "Max";
        case MoonDifficulty_Medium:
        default:
            return "Medium";
        }
    }

    const char* getMoonDifficultyLabel(MoonDifficulty difficulty) {
        switch (difficulty) {
        case MoonDifficulty_Easy:
            return "Easy (Favourable Distribution)";
        case MoonDifficulty_Hard:
            return "Hard (Unfavourable Distribution, 30% Cap)";
        case MoonDifficulty_Harder:
            return "Random Hard (Random Distribution, 40% Cap)";
        case MoonDifficulty_Hardest:
            return "Random Harder (Random Distribution, 60% Cap)";
        case MoonDifficulty_Insane:
            return "Insane (Random Distribution, 80% Cap)";
        case MoonDifficulty_Max:
            return "Max (Unbounded Distribution)";
        case MoonDifficulty_Medium:
        default:
            return "Medium (Vanilla Distribution)";
        }
    }

    const char* getCurrentMoonDifficultyLabel() {
        return getMoonDifficultyLabel(sMoonDifficulty);
    }

    LogicDifficulty getLogicDifficulty() { return sLogicDifficulty; }

    void setLogicDifficulty(LogicDifficulty difficulty) {
        if (!isValidLogicDifficulty(difficulty))
            difficulty = LogicDifficulty_Casual;
        sLogicDifficulty = difficulty;
    }

    void cycleLogicDifficulty() {
        switch (sLogicDifficulty) {
        case LogicDifficulty_Casual:
            setLogicDifficulty(LogicDifficulty_Advanced);
            break;
        case LogicDifficulty_Advanced:
            setLogicDifficulty(LogicDifficulty_Expert);
            break;
        case LogicDifficulty_Expert:
            setLogicDifficulty(LogicDifficulty_Glitched);
            break;
        default:
            setLogicDifficulty(LogicDifficulty_Casual);
            break;
        }
    }

    const char* getLogicDifficultyName(LogicDifficulty difficulty) {
        switch (difficulty) {
        case LogicDifficulty_Advanced:
            return "Advanced";
        case LogicDifficulty_Expert:
            return "Expert";
        case LogicDifficulty_Glitched:
            return "Glitched";
        case LogicDifficulty_Casual:
        default:
            return "Casual";
        }
    }

    const char* getLogicDifficultyLabel(LogicDifficulty difficulty) {
        switch (difficulty) {
        case LogicDifficulty_Advanced:
            return "Advanced";
        case LogicDifficulty_Expert:
            return "Expert";
        case LogicDifficulty_Glitched:
            return "Glitched";
        case LogicDifficulty_Casual:
        default:
            return "Casual";
        }
    }

    const char* getCurrentLogicDifficultyLabel() {
        return getLogicDifficultyLabel(sLogicDifficulty);
    }

    const char* getCurrentLogicModeLabel() {
        return isLogicRandoEnabled() ? getCurrentLogicDifficultyLabel() : "No Logic";
    }

    void cycleLogicMode() {
        if (!isLogicRandoEnabled()) {
            setLogicRandoEnabled(true);
            setLogicDifficulty(LogicDifficulty_Casual);
            return;
        }

        switch (sLogicDifficulty) {
        case LogicDifficulty_Casual:
            setLogicDifficulty(LogicDifficulty_Advanced);
            break;
        case LogicDifficulty_Advanced:
            setLogicDifficulty(LogicDifficulty_Expert);
            break;
        case LogicDifficulty_Expert:
            setLogicDifficulty(LogicDifficulty_Glitched);
            break;
        default:
            setLogicRandoEnabled(false);
            break;
        }
    }

    UnlockPacing getUnlockPacing() { return sUnlockPacing; }

    void setUnlockPacing(UnlockPacing pacing) {
        sUnlockPacing = isValidSeedAxis(static_cast<u8>(pacing)) ? pacing : UnlockPacing_Standard;
    }

    SeedOpenness getSeedOpenness() { return sSeedOpenness; }

    void setSeedOpenness(SeedOpenness openness) {
        sSeedOpenness = isValidSeedAxis(static_cast<u8>(openness)) ? openness : SeedOpenness_Standard;
    }

    SeedDifficulty getSeedDifficulty() { return sSeedDifficulty; }

    void setSeedDifficulty(SeedDifficulty difficulty) {
        sSeedDifficulty
            = isValidSeedAxis(static_cast<u8>(difficulty)) ? difficulty : SeedDifficulty_Standard;
    }

    const char* getUnlockPacingName(UnlockPacing pacing) {
        switch (pacing) {
        case UnlockPacing_FrontLoaded:
            return "Front-Loaded";
        case UnlockPacing_Early:
            return "Early";
        case UnlockPacing_Late:
            return "Late";
        case UnlockPacing_BackLoaded:
            return "Back-Loaded";
        case UnlockPacing_Random:
            return "Random";
        case UnlockPacing_Standard:
        default:
            return "Standard";
        }
    }

    const char* getSeedOpennessName(SeedOpenness openness) {
        switch (openness) {
        case SeedOpenness_Claustrophobic:
            return "Claustrophobic";
        case SeedOpenness_Compact:
            return "Compact";
        case SeedOpenness_Open:
            return "Open";
        case SeedOpenness_WideOpen:
            return "Wide Open";
        case SeedOpenness_Random:
            return "Random";
        case SeedOpenness_Standard:
        default:
            return "Standard";
        }
    }

    const char* getSeedDifficultyName(SeedDifficulty difficulty) {
        switch (difficulty) {
        case SeedDifficulty_Relaxed:
            return "Relaxed";
        case SeedDifficulty_Easy:
            return "Easy";
        case SeedDifficulty_Hard:
            return "Hard";
        case SeedDifficulty_Brutal:
            return "Brutal";
        case SeedDifficulty_Random:
            return "Random";
        case SeedDifficulty_Standard:
        default:
            return "Standard";
        }
    }

    // Random resolves to one of the five concrete levels from the seed alone,
    // with a per-axis salt so the three stay independent. Deliberately NOT a
    // draw from a generator stream: reinitDeterministic() rebuilds from
    // seed + attempt on save reload and must land on the same levels.
    UnlockPacing getResolvedUnlockPacing() {
        if (sUnlockPacing != UnlockPacing_Random)
            return sUnlockPacing;
        return static_cast<UnlockPacing>(
            mixSeedStyleAxis(sSeed ^ 0xA341316Cu) % cSeedAxisLevelCount);
    }

    SeedOpenness getResolvedSeedOpenness() {
        if (sSeedOpenness != SeedOpenness_Random)
            return sSeedOpenness;
        return static_cast<SeedOpenness>(
            mixSeedStyleAxis(sSeed ^ 0xC8013EA4u) % cSeedAxisLevelCount);
    }

    SeedDifficulty getResolvedSeedDifficulty() {
        if (sSeedDifficulty != SeedDifficulty_Random)
            return sSeedDifficulty;
        return static_cast<SeedDifficulty>(
            mixSeedStyleAxis(sSeed ^ 0xAD90777Du) % cSeedAxisLevelCount);
    }

    s32 getResolvedUnlockPacingLevel() { return static_cast<s32>(getResolvedUnlockPacing()); }

    s32 getResolvedSeedOpennessLevel() { return static_cast<s32>(getResolvedSeedOpenness()); }

    s32 getResolvedSeedDifficultyLevel() { return static_cast<s32>(getResolvedSeedDifficulty()); }

    u16 getSeedAxesPacked() {
        return static_cast<u16>(cSeedAxisPackedTag | (static_cast<u16>(sUnlockPacing) & 0x7u)
            | ((static_cast<u16>(sSeedOpenness) & 0x7u) << 3)
            | ((static_cast<u16>(sSeedDifficulty) & 0x7u) << 6));
    }

    void setSeedAxesPacked(u16 packed) {
        if ((packed & cSeedAxisPackedTag) == 0) {
            // A record written before the axes existed stores a bare SeedStyle.
            applyLegacySeedStyle(static_cast<SeedStyle>(packed & 0x7u));
            return;
        }
        setUnlockPacing(static_cast<UnlockPacing>(packed & 0x7u));
        setSeedOpenness(static_cast<SeedOpenness>((packed >> 3) & 0x7u));
        setSeedDifficulty(static_cast<SeedDifficulty>((packed >> 6) & 0x7u));
    }

    void applyLegacySeedStyle(SeedStyle style) {
        if (!isValidSeedStyle(style))
            style = SeedStyle_Standard;
        switch (style) {
        case SeedStyle_AbilityRush:
            setUnlockPacing(UnlockPacing_Early);
            setSeedOpenness(SeedOpenness_Standard);
            setSeedDifficulty(SeedDifficulty_Standard);
            break;
        case SeedStyle_FocusedRoute:
            setUnlockPacing(UnlockPacing_Standard);
            setSeedOpenness(SeedOpenness_Compact);
            setSeedDifficulty(SeedDifficulty_Easy);
            break;
        case SeedStyle_Explorer:
            setUnlockPacing(UnlockPacing_Standard);
            setSeedOpenness(SeedOpenness_Open);
            setSeedDifficulty(SeedDifficulty_Hard);
            break;
        case SeedStyle_Mystery:
            setUnlockPacing(UnlockPacing_Random);
            setSeedOpenness(SeedOpenness_Random);
            setSeedDifficulty(SeedDifficulty_Random);
            break;
        case SeedStyle_Standard:
        default:
            setUnlockPacing(UnlockPacing_Standard);
            setSeedOpenness(SeedOpenness_Standard);
            setSeedDifficulty(SeedDifficulty_Standard);
            break;
        }
    }

    void applyLegacyPrioritizeDifficultMovement(bool enabled) {
        // The old checkbox masked casual alternatives during construction,
        // which is exactly what SeedDifficulty_Hard does now. Only promote:
        // a legacy style that already resolved to something harder wins.
        if (!enabled)
            return;
        // SeedDifficulty_Random is numerically ABOVE Brutal, so a plain
        // "< Hard" test silently skips it - which is exactly the Mystery case:
        // that style migrates all three axes to Random, and Random can roll
        // Relaxed. The old checkbox applied independently of the style and
        // always masked casual alternatives, so an unresolved difficulty is not
        // evidence of anything harder and has to be pinned like any level below
        // Hard. Pacing and Openness stay Random, so a migrated Mystery seed
        // keeps its mystery everywhere the checkbox did not have a say.
        if (sSeedDifficulty == SeedDifficulty_Random || sSeedDifficulty < SeedDifficulty_Hard)
            setSeedDifficulty(SeedDifficulty_Hard);
    }

    MoonRequirement getMoonRequirementLevel() { return sMoonRequirement; }

    void setMoonRequirementLevel(MoonRequirement requirement) {
        // Legacy saves may carry MoonRequirement_128; the 128 preset was retired in
        // favour of 124 (the vanilla per-kingdom total).
        if (!isValidMoonRequirement(requirement))
            requirement = MoonRequirement_124;
        sMoonRequirement = requirement;
        sMoonRequirementTotal = getMoonRequirementTotal(requirement);
    }

    void cycleMoonRequirement() {
        switch (sMoonRequirement) {
        case MoonRequirement_64:
            setMoonRequirementLevel(MoonRequirement_124);
            break;
        case MoonRequirement_124:
            setMoonRequirementLevel(MoonRequirement_256);
            break;
        default:
            setMoonRequirementLevel(MoonRequirement_64);
            break;
        }
    }

    u32 getMoonRequirementTotal(MoonRequirement requirement) {
        switch (requirement) {
        case MoonRequirement_64:
            return 64;
        case MoonRequirement_256:
            return 256;
        case MoonRequirement_124:
        default:
            return 124;
        }
    }

    const char* getMoonRequirementLabel(MoonRequirement requirement) {
        switch (requirement) {
        case MoonRequirement_64:
            return "64 moons";
        case MoonRequirement_256:
            return "256 moons";
        case MoonRequirement_124:
        default:
            return "124 moons";
        }
    }

    const char* getCurrentMoonRequirementLabel() {
        static char label[24];
        nn::util::SNPrintf(label, sizeof(label), "%u moons", sMoonRequirementTotal);
        return label;
    }

    u32 getMoonRequirement() { return sMoonRequirementTotal; }

    void setMoonRequirement(u32 req) {
        if (req > 657)
            req = 657;
        sMoonRequirementTotal = req;

        // Keep the legacy enum meaningful for old serializers and diagnostics.
        if (req == 64)
            sMoonRequirement = MoonRequirement_64;
        else if (req == 256)
            sMoonRequirement = MoonRequirement_256;
        else
            sMoonRequirement = MoonRequirement_124;
    }

    static constexpr u8 cSpecialMoonUseCount = 7;
    static constexpr u8 cSpecialMoonUseMask = (1u << cSpecialMoonUseCount) - 1;
    static constexpr u8 specialMoonUseBit(SpecialMoonUse use) {
        return static_cast<u8>(1u << static_cast<u8>(use));
    }

    // What every kingdom moons hand out. The scalar getters are retained as
    // the lowest-set-bit compatibility view; the masks are authoritative for
    // new settings and save records.
    //
    // This used to be six named kingdoms, with the other eleven implicitly
    // gating progress. All seventeen are selectable now (Kingdom Moons page),
    // so the default for a kingdom that had no setting is the Progress it was
    // already doing.
    static u8 sKingdomMoonUseMask[kKingdomCount] = {
        specialMoonUseBit(SpecialMoonUse_HintTokens), // Cap
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Cascade
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Sand
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Wooded
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Lake
        specialMoonUseBit(SpecialMoonUse_PaintingUnlock), // Cloud
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Lost
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Metro
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Seaside
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Snow
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Luncheon
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Ruined
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Bowser
        specialMoonUseBit(SpecialMoonUse_KingdomUnlock), // Moon
        specialMoonUseBit(SpecialMoonUse_Captures), // Mushroom
        specialMoonUseBit(SpecialMoonUse_Abilities), // Dark Side
        specialMoonUseBit(SpecialMoonUse_Abilities), // Darker Side
    };
    static WorldPeaceRequirementMode sWorldPeaceRequirementMode = WorldPeaceRequirementMode_StoryProgression;
    // Per-kingdom bitmask: has this kingdom been left for a different one
    // while WorldPeaceRequirementMode_OnKingdomComplete was active? Bit index
    // is the native/logical WorldId_*, matching visitedKingdoms below.
    static u32 sForcedPeaceKingdoms = 0;
    // Generation-time setting (Additional Options page): color-code kingdom
    // names on the World Map and keep visited-but-not-unlocked kingdoms
    // travel-able from it. Defaults to Vanilla (Entrance/Kingdom Rando specific).
    static VisitedKingdomsMapMode sVisitedKingdomsMapMode = VisitedKingdomsMapMode_Vanilla;
    static u32 sStartKingdomsMask = 1u << WorldMeta::WorldId_Cap;
    // 5 is what the game's own kingdom requirements vary by, and what the
    // logic assumes, so it is the default rather than a flat 0.
    static s16 sMoonRequirementDeviation = 5;
    // Generation-time setting (Randomization page): where a brand new save drops
    // the player. See StartPositionRando for the selection and the logic model.
    static StartPositionMode sStartPositionMode = StartPositionMode_Vanilla;
    static bool sMoonRockKeyEnabled = false;
    // Generation-time settings (Additional Options page 2): lock a kingdom's
    // optional sub-areas behind found keys, cut by door kind and/or kingdom.
    static SubareaUnlockMode sSubareaUnlockMode = SubareaUnlockMode_Unlocked;
    static SubareaUnlockHost sSubareaUnlockHost = SubareaUnlockHost_CapAndMoon;
    static KeyScope sMoonRockKeyScope = KeyScope::WithinKingdom;
    static KeyScope sSubareaKeyScope = KeyScope::WithinKingdom;
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    static bool sTripleMoonsAnywhereEnabled = false;
    static bool sRandomizeTripleMoonsEnabled = false;
#endif
    static bool sMoonReallocationEnabled = false;
    static bool sRandomizeMoonAllocationEnabled = false;
    static MoonAllocationDifficulty sMoonAllocationDifficulty = MoonAllocationDifficulty::Vanilla;
    static MoonCoinPolicy sMoonCoinPolicy = MoonCoinPolicy::Vanilla;
    static s16 sKingdomMoonTargets[kKingdomCount] = {
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
    };
    static s8 sKingdomTripleMoonTargets[kKingdomCount] = {
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
        -1,
    };

    static bool isValidSpecialMoonUse(SpecialMoonUse use) {
        switch (use) {
        case SpecialMoonUse_Coins:
        case SpecialMoonUse_AbilitiesOrCaptures:
        case SpecialMoonUse_KingdomUnlock:
        case SpecialMoonUse_Abilities:
        case SpecialMoonUse_Captures:
        case SpecialMoonUse_HintTokens:
        case SpecialMoonUse_PaintingUnlock:
            return true;
        default:
            return false;
        }
    }

    static SpecialMoonUse lowestSelectedSpecialMoonUse(u8 mask, SpecialMoonUse fallback) {
        mask &= cSpecialMoonUseMask;
        if (mask == 0)
            return fallback;
        for (u8 i = 0; i < cSpecialMoonUseCount; i++) {
            if (mask & (1u << i))
                return static_cast<SpecialMoonUse>(i);
        }
        return fallback;
    }

    // An empty mask is a legal answer - that kingdom's moons hand out nothing
    // at all - so this only drops bits outside the enum. It used to substitute
    // the kingdom default for zero, which made "clear every reward" impossible
    // to express on the advanced page.
    static u8 sanitizeSpecialMoonUseMask(u8 mask) {
        return mask & cSpecialMoonUseMask;
    }

    // What a kingdom hands out before anyone changes it. Mirrored by the
    // sKingdomMoonUseMask initialiser above; keep the two in step.
    static SpecialMoonUse defaultKingdomMoonUse(s32 worldId) {
        switch (worldId) {
        case WorldMeta::WorldId_Cap:
            return SpecialMoonUse_HintTokens;
        case WorldMeta::WorldId_Cloud:
            return SpecialMoonUse_PaintingUnlock;
        case WorldMeta::WorldId_Mushroom:
            return SpecialMoonUse_Captures;
        case WorldMeta::WorldId_Dark:
        case WorldMeta::WorldId_Darker:
            return SpecialMoonUse_Abilities;
        default:
            return SpecialMoonUse_KingdomUnlock;
        }
    }

    u8 getKingdomMoonUseMask(s32 worldId) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return specialMoonUseBit(SpecialMoonUse_Coins);
        return sKingdomMoonUseMask[worldId];
    }
    void setKingdomMoonUseMask(s32 worldId, u8 mask) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return;
        sKingdomMoonUseMask[worldId] = sanitizeSpecialMoonUseMask(mask);
    }
    u8 getDefaultKingdomMoonUseMask(s32 worldId) {
        return specialMoonUseBit(defaultKingdomMoonUse(worldId));
    }
    bool isKingdomMoonUseSelected(s32 worldId, SpecialMoonUse use) {
        return (getKingdomMoonUseMask(worldId) & specialMoonUseBit(use)) != 0;
    }
    bool isCaptureRewardKingdom(s32 worldId) {
        return isKingdomMoonUseSelected(worldId, SpecialMoonUse_Captures)
            || isKingdomMoonUseSelected(worldId,
                SpecialMoonUse_AbilitiesOrCaptures);
    }
    bool isAbilityRewardKingdom(s32 worldId) {
        return isKingdomMoonUseSelected(worldId, SpecialMoonUse_Abilities)
            || isKingdomMoonUseSelected(worldId,
                SpecialMoonUse_AbilitiesOrCaptures);
    }
    bool isPaintingRewardKingdom(s32 worldId) {
        return isKingdomMoonUseSelected(worldId, SpecialMoonUse_PaintingUnlock);
    }
    bool isHintTokenRewardKingdom(s32 worldId) {
        return isKingdomMoonUseSelected(worldId, SpecialMoonUse_HintTokens);
    }

    // The lowest set bit. An empty mask has no answer, so it reports the
    // kingdom default - callers that need to tell the two apart read the mask,
    // and getKingdomMoonUseName() below says "None" for it.
    SpecialMoonUse getKingdomMoonUse(s32 worldId) {
        return lowestSelectedSpecialMoonUse(getKingdomMoonUseMask(worldId),
            defaultKingdomMoonUse(worldId));
    }
    const char* getKingdomMoonUseName(s32 worldId) {
        if (getKingdomMoonUseMask(worldId) == 0)
            return "None";
        return getSpecialMoonUseName(getKingdomMoonUse(worldId));
    }
    void setKingdomMoonUse(s32 worldId, SpecialMoonUse use) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return;
        sKingdomMoonUseMask[worldId] = isValidSpecialMoonUse(use)
            ? specialMoonUseBit(use)
            : specialMoonUseBit(defaultKingdomMoonUse(worldId));
    }

    // The six kingdoms that had their own accessors before the Kingdom Moons
    // page existed. Retained as forwarders: the seed builder, the shine list
    // and the hint economy all still call them by name.
    SpecialMoonUse getCapKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Cap); }
    void setCapKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Cap, use); }
    u8 getCapKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Cap); }
    void setCapKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Cap, mask); }
    bool isCapKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Cap, use);
    }

    SpecialMoonUse getCloudKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Cloud); }
    void setCloudKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Cloud, use); }
    u8 getCloudKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Cloud); }
    void setCloudKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Cloud, mask); }
    bool isCloudKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Cloud, use);
    }

    SpecialMoonUse getMoonKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Moon); }
    void setMoonKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Moon, use); }
    u8 getMoonKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Moon); }
    void setMoonKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Moon, mask); }
    bool isMoonKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Moon, use);
    }

    SpecialMoonUse getDarkKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Dark); }
    void setDarkKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Dark, use); }
    u8 getDarkKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Dark); }
    void setDarkKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Dark, mask); }
    bool isDarkKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Dark, use);
    }

    SpecialMoonUse getDarkerKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Darker); }
    void setDarkerKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Darker, use); }
    u8 getDarkerKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Darker); }
    void setDarkerKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Darker, mask); }
    bool isDarkerKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Darker, use);
    }

    SpecialMoonUse getMushroomKingdomMoonUse() { return getKingdomMoonUse(WorldMeta::WorldId_Mushroom); }
    void setMushroomKingdomMoonUse(SpecialMoonUse use) { setKingdomMoonUse(WorldMeta::WorldId_Mushroom, use); }
    u8 getMushroomKingdomMoonUseMask() { return getKingdomMoonUseMask(WorldMeta::WorldId_Mushroom); }
    void setMushroomKingdomMoonUseMask(u8 mask) { setKingdomMoonUseMask(WorldMeta::WorldId_Mushroom, mask); }
    bool isMushroomKingdomMoonUseSelected(SpecialMoonUse use) {
        return isKingdomMoonUseSelected(WorldMeta::WorldId_Mushroom, use);
    }

    const char* getSpecialMoonUseName(SpecialMoonUse use) {
        switch (use) {
        case SpecialMoonUse_Abilities:
            return "Abilities";
        case SpecialMoonUse_Captures:
            return "Captures";
        case SpecialMoonUse_AbilitiesOrCaptures:
            return "Abilities or Captures";
        case SpecialMoonUse_KingdomUnlock:
            return "Kingdom Unlock";
        case SpecialMoonUse_PaintingUnlock:
            return "Painting Unlock";
        case SpecialMoonUse_HintTokens:
            return "Hint Tokens";
        case SpecialMoonUse_Coins:
        default:
            return "Coins";
        }
    }

    HintCurrencyMode getHintCurrencyMode() {
        return isCapKingdomMoonUseSelected(SpecialMoonUse_HintTokens)
                || isCloudKingdomMoonUseSelected(SpecialMoonUse_HintTokens)
                || isMoonKingdomMoonUseSelected(SpecialMoonUse_HintTokens)
            ? HintCurrencyMode::HintTokens
            : HintCurrencyMode::Coins;
    }

    bool isHintTokenMode() { return getHintCurrencyMode() == HintCurrencyMode::HintTokens; }

    const char* getHintCurrencyModeName(HintCurrencyMode mode) {
        return mode == HintCurrencyMode::HintTokens ? "Hint Tokens" : "Coins";
    }

    static BowserWinCondition sBowserWinCondition = BowserWinCondition_DefeatBowser;
    static s32 sToadetteMedalRatioIdx = 0;

    static constexpr ToadetteMedalRatio kToadetteMedalRatios[kToadetteMedalRatioCount] = {
        { 4, 12 },
        { 8, 12 },
        { 12, 12 },
        { 8, 64 },
        { 16, 64 },
        { 24, 64 },
        { 32, 64 },
        { 40, 64 },
        { 48, 64 },
        { 56, 64 },
        { 64, 64 },
    };

    static bool isValidBowserWinCondition(BowserWinCondition condition) {
        return condition == BowserWinCondition_DefeatBowser
            || condition == BowserWinCondition_ToadetteMedals
            || condition == BowserWinCondition_ToadetteMedalsAndDefeat;
    }

    BowserWinCondition getBowserWinCondition() {
        return FeatureFlags::ToadetteMedals ? sBowserWinCondition : BowserWinCondition_DefeatBowser;
    }

    void setBowserWinCondition(BowserWinCondition condition) {
        if (!FeatureFlags::ToadetteMedals)
            condition = BowserWinCondition_DefeatBowser;
        if (isBoardRunMode())
            condition = BowserWinCondition_DefeatBowser;
        if (!isValidBowserWinCondition(condition))
            condition = BowserWinCondition_DefeatBowser;
        sBowserWinCondition = condition;
        // Keep the win-condition selector synchronized with the top-level
        // Toadette Medal Hunt run mode.
        if (condition == BowserWinCondition_ToadetteMedals && sRunMode == RunMode::Standard)
            sRunMode = RunMode::ToadetteMedals;
        else if (condition != BowserWinCondition_ToadetteMedals && sRunMode == RunMode::ToadetteMedals)
            sRunMode = RunMode::Standard;
    }

    const char* getBowserWinConditionName(BowserWinCondition condition) {
        switch (condition) {
        case BowserWinCondition_ToadetteMedals:
            return "Toadette Medal Hunt";
        case BowserWinCondition_ToadetteMedalsAndDefeat:
            return "Toadette Medal Hunt + Defeat";
        case BowserWinCondition_DefeatBowser:
        default:
            return "Defeat Bowser";
        }
    }

    bool isToadetteMedalModeEnabled() {
        return FeatureFlags::ToadetteMedals && sBowserWinCondition != BowserWinCondition_DefeatBowser;
    }

    s32 getToadetteMedalRatioIdx() { return sToadetteMedalRatioIdx; }

    void setToadetteMedalRatioIdx(s32 idx) {
        if (idx < 0 || idx >= kToadetteMedalRatioCount)
            idx = 0;
        sToadetteMedalRatioIdx = idx;
    }

    static bool sCustomToadetteMedalCounts = false;
    static s32 sCustomToadetteMedalAvailable = 12;
    static s32 sCustomToadetteMedalRequired = 8;

    bool isCustomToadetteMedalCountsEnabled() {
        return FeatureFlags::ToadetteMedals && sCustomToadetteMedalCounts;
    }

    void setCustomToadetteMedalCountsEnabled(bool enabled) {
        sCustomToadetteMedalCounts = FeatureFlags::ToadetteMedals && enabled;
    }

    static s32 clampMedalCount(s32 count) {
        if (count < 1)
            return 1;
        return count > kToadetteMedalCustomMax ? kToadetteMedalCustomMax : count;
    }

    s32 getCustomToadetteMedalAvailable() { return sCustomToadetteMedalAvailable; }

    void setCustomToadetteMedalAvailable(s32 count) {
        sCustomToadetteMedalAvailable = clampMedalCount(count);
        // A pool smaller than the requirement is unwinnable, so the two values
        // push each other rather than being validated only at generation time.
        if (sCustomToadetteMedalRequired > sCustomToadetteMedalAvailable)
            sCustomToadetteMedalRequired = sCustomToadetteMedalAvailable;
    }

    s32 getCustomToadetteMedalRequired() { return sCustomToadetteMedalRequired; }

    void setCustomToadetteMedalRequired(s32 count) {
        sCustomToadetteMedalRequired = clampMedalCount(count);
        if (sCustomToadetteMedalRequired > sCustomToadetteMedalAvailable)
            sCustomToadetteMedalAvailable = sCustomToadetteMedalRequired;
    }

    ToadetteMedalRatio getToadetteMedalRatio() {
        if (isCustomToadetteMedalCountsEnabled()) {
            return { static_cast<u8>(sCustomToadetteMedalRequired),
                static_cast<u8>(sCustomToadetteMedalAvailable) };
        }
        return kToadetteMedalRatios[sToadetteMedalRatioIdx];
    }

    bool isMoonKingdomRequirementsEnabled() {
        return isMoonKingdomMoonUseSelected(SpecialMoonUse_KingdomUnlock);
    }
    void setMoonKingdomRequirementsEnabled(bool enabled) {
        setMoonKingdomMoonUse(enabled
                ? SpecialMoonUse_KingdomUnlock
                : SpecialMoonUse_Coins);
    }

    WorldPeaceRequirementMode getWorldPeaceRequirementMode() { return sWorldPeaceRequirementMode; }
    void setWorldPeaceRequirementMode(WorldPeaceRequirementMode mode) {
        sWorldPeaceRequirementMode = mode <= WorldPeaceRequirementMode_Required
            ? mode
            : WorldPeaceRequirementMode_StoryProgression;
    }
    const char* getWorldPeaceRequirementModeName(WorldPeaceRequirementMode mode) {
        switch (mode) {
        case WorldPeaceRequirementMode_OnKingdomComplete:
            return "On Kingdom Complete";
        case WorldPeaceRequirementMode_Required:
            return "Required";
        case WorldPeaceRequirementMode_StoryProgression:
        default:
            return "Story Progression";
        }
    }
    const char* getCurrentWorldPeaceRequirementModeName() {
        return getWorldPeaceRequirementModeName(sWorldPeaceRequirementMode);
    }

    bool isWorldPeaceRequirementEnabled() { return sWorldPeaceRequirementMode == WorldPeaceRequirementMode_Required; }
    void setWorldPeaceRequirementEnabled(bool enabled) {
        sWorldPeaceRequirementMode = enabled ? WorldPeaceRequirementMode_Required : WorldPeaceRequirementMode_StoryProgression;
    }

    u32 getForcedPeaceKingdoms() { return sForcedPeaceKingdoms; }
    void setForcedPeaceKingdoms(u32 mask) { sForcedPeaceKingdoms = mask; }
    bool isKingdomForcedPeace(s32 worldId) {
        return worldId >= 0 && worldId < 32 && (sForcedPeaceKingdoms & (1u << worldId)) != 0;
    }
    void setKingdomForcedPeace(s32 worldId, bool forced) {
        if (worldId < 0 || worldId >= 32)
            return;
        const u32 bit = 1u << worldId;
        sForcedPeaceKingdoms = forced ? (sForcedPeaceKingdoms | bit) : (sForcedPeaceKingdoms & ~bit);
    }

    // Painting Unlock is selectable on any of the seventeen kingdoms, so the
    // feature is on when *someone* grants it. Cloud stays the canonical home:
    // the setter below still writes there, and it is the default.
    bool isCloudKingdomMoonsUnlockPaintingsEnabled() {
        for (s32 worldId = 0; worldId < kKingdomCount; worldId++)
            if (isKingdomMoonUseSelected(worldId, SpecialMoonUse_PaintingUnlock))
                return true;
        return false;
    }
    void setCloudKingdomMoonsUnlockPaintingsEnabled(bool enabled) {
        setCloudKingdomMoonUse(enabled
                ? SpecialMoonUse_PaintingUnlock
                : SpecialMoonUse_Coins);
    }
    VisitedKingdomsMapMode getVisitedKingdomsMapMode() { return sVisitedKingdomsMapMode; }
    void setVisitedKingdomsMapMode(VisitedKingdomsMapMode mode) {
        sVisitedKingdomsMapMode = mode <= VisitedKingdomsMapMode_Custom
            ? mode
            : VisitedKingdomsMapMode_Vanilla;
    }
    const char* getVisitedKingdomsMapModeName(VisitedKingdomsMapMode mode) {
        switch (mode) {
        case VisitedKingdomsMapMode_Visited:
            return "Visited";
        case VisitedKingdomsMapMode_Unlocked:
            return "Unlocked";
        case VisitedKingdomsMapMode_Custom:
            return "Custom";
        case VisitedKingdomsMapMode_Vanilla:
        default:
            return "Vanilla";
        }
    }
    bool isVisitedKingdomsMapEnabled() {
        return sVisitedKingdomsMapMode != VisitedKingdomsMapMode_Vanilla;
    }
    void setVisitedKingdomsMapEnabled(bool enabled) {
        sVisitedKingdomsMapMode = enabled ? VisitedKingdomsMapMode_Visited
                                          : VisitedKingdomsMapMode_Vanilla;
    }

    u32 getStartKingdomsMask() { return sStartKingdomsMask; }
    void setStartKingdomsMask(u32 mask) {
        mask &= (1u << WorldMeta::sWorldCount) - 1;
        if (mask == 0)
            mask = 1u << WorldMeta::WorldId_Cap;
        sStartKingdomsMask = mask;
    }
    bool isStartKingdomSelected(s32 worldId) {
        return worldId >= 0 && worldId < WorldMeta::sWorldCount
            && (sStartKingdomsMask & (1u << worldId)) != 0;
    }

    s16 getMoonRequirementDeviation() {
        return sMoonRequirementDeviation;
    }
    void setMoonRequirementDeviation(s16 deviation) {
        if (deviation < -100)
            deviation = -100;
        if (deviation > 100)
            deviation = 100;
        sMoonRequirementDeviation = deviation;
    }
    void clearMoonRequirementDeviations() {
        sMoonRequirementDeviation = kDefaultMoonRequirementDeviation;
    }

    bool isMoonRockKeyEnabled() { return sMoonRockKeyEnabled; }
    void setMoonRockKeyEnabled(bool enabled) { sMoonRockKeyEnabled = enabled; }

    KeyScope getMoonRockKeyScope() { return sMoonRockKeyScope; }
    void setMoonRockKeyScope(KeyScope scope) {
        sMoonRockKeyScope = scope <= KeyScope::WithinKingdom
            ? scope
            : KeyScope::WithinKingdom;
    }
    KeyScope getSubareaKeyScope() { return sSubareaKeyScope; }
    void setSubareaKeyScope(KeyScope scope) {
        sSubareaKeyScope = scope <= KeyScope::WithinKingdom
            ? scope
            : KeyScope::WithinKingdom;
    }

    SubareaUnlockMode getSubareaUnlockMode() { return sSubareaUnlockMode; }
    void setSubareaUnlockMode(SubareaUnlockMode mode) {
        sSubareaUnlockMode = mode <= SubareaUnlockMode_PerKingdomAndType
            ? mode
            : SubareaUnlockMode_Unlocked;
        // A mode change can outgrow the selected host, so re-settle the pair
        // here rather than leaving an unsatisfiable combination behind for the
        // seed builder to trip over.
        sSubareaUnlockHost = clampSubareaUnlockHost(sSubareaUnlockMode, sSubareaUnlockHost);
    }
    // Single gate for the whole Subarea Key feature. Key construction, the
    // entrance denial, the collection-list page, Cappy's line and the spoiler
    // section all funnel through this, so a build without the flag compiles the
    // feature's code but never arms any of it.
    bool isSubareaUnlockEnabled() {
        return FeatureFlags::SubareaKeys && sSubareaUnlockMode != SubareaUnlockMode_Unlocked;
    }
    const char* getSubareaUnlockModeName(SubareaUnlockMode mode) {
        switch (mode) {
        case SubareaUnlockMode_PerType:
            return "Per Type";
        case SubareaUnlockMode_PerKingdom:
            return "Per Kingdom";
        case SubareaUnlockMode_PerKingdomAndType:
            return "Per Kingdom + Type";
        default:
            return "Unlocked";
        }
    }

    SubareaUnlockHost getSubareaUnlockHost() { return sSubareaUnlockHost; }
    void setSubareaUnlockHost(SubareaUnlockHost host) {
        const SubareaUnlockHost requested = host <= SubareaUnlockHost_AllKingdoms
            ? host
            : SubareaUnlockHost_CapAndMoon;
        sSubareaUnlockHost = clampSubareaUnlockHost(sSubareaUnlockMode, requested);
    }
    const char* getSubareaUnlockHostName(SubareaUnlockHost host) {
        switch (host) {
        case SubareaUnlockHost_Cap:
            return "Cap";
        case SubareaUnlockHost_Moon:
            return "Moon";
        case SubareaUnlockHost_AllKingdoms:
            return "All Kingdoms";
        default:
            return "Cap + Moon";
        }
    }

    bool subareaHostFits(SubareaUnlockMode mode, SubareaUnlockHost host) {
        if (mode == SubareaUnlockMode_Unlocked)
            return true;
        return SubareaKeyDB::keyCountForMode(mode) <= SubareaKeyDB::hostMoonCapacity(host);
    }

    SubareaUnlockHost clampSubareaUnlockHost(SubareaUnlockMode mode, SubareaUnlockHost host) {
        if (subareaHostFits(mode, host))
            return host;
        if (subareaHostFits(mode, SubareaUnlockHost_CapAndMoon))
            return SubareaUnlockHost_CapAndMoon;
        return SubareaUnlockHost_AllKingdoms;
    }

    bool isStartKingdomStateRandoEnabled() { return sStartKingdomStateRando; }
    void setStartKingdomStateRandoEnabled(bool enabled) { sStartKingdomStateRando = enabled; }

    bool isChestOrderRandoEnabled() { return sChestOrderRando; }
    void setChestOrderRandoEnabled(bool enabled) { sChestOrderRando = enabled; }

    bool isCoinRandoEnabled() { return sCoinRando; }
    void setCoinRandoEnabled(bool enabled) { sCoinRando = enabled; }

    bool isShopPriceRandoEnabled() { return sShopPriceRando; }
    void setShopPriceRandoEnabled(bool enabled) { sShopPriceRando = enabled; }

    StartPositionMode getStartPositionMode() { return sStartPositionMode; }
    void setStartPositionMode(StartPositionMode mode) {
        sStartPositionMode = mode <= StartPositionMode_Odyssey ? mode : StartPositionMode_Vanilla;
    }
    const char* getStartPositionModeName(StartPositionMode mode) {
        switch (mode) {
        case StartPositionMode_Overworld:
            return "Overworld";
        case StartPositionMode_Max:
            return "Max";
        case StartPositionMode_Odyssey:
            // Short on purpose: this feeds fixed-width summary lines on the file
            // select page. The long wording lives in the option description.
            return "Odyssey";
        case StartPositionMode_Vanilla:
        default:
            return "Vanilla";
        }
    }

    s32 getInitialKingdomScenario(s32 worldId) {
        if (worldId < 0 || worldId >= LogicData::sKingdomCount)
            return -1;

        s32 vanillaStart = LogicData::sStartScenarioByKingdom[worldId] > 0
            ? LogicData::sStartScenarioByKingdom[worldId]
            : 1;
        // Cloud has no pre-peace story state in practice: the runtime and logic
        // already treat it as post-Bowser scenario 3. Keep that invariant here
        // so logic, scenario loading, and the actual new-game save agree.
        if (worldId == WorldMeta::WorldId_Cloud
            && LogicData::sPostGameScenarioByKingdom[worldId] > vanillaStart)
            vanillaStart = LogicData::sPostGameScenarioByKingdom[worldId];
        if (!sStartKingdomStateRando)
            return vanillaStart;

        s32 minimumScenario = vanillaStart;

        s32 candidates[3] = {};
        s32 candidateCount = 0;
        auto addCandidate = [&](s32 scenario) {
            if (scenario <= 0)
                return;
            if (scenario < minimumScenario)
                scenario = minimumScenario;
            if (LogicData::sMaxLogicScenarioByKingdom[worldId] >= 0
                && scenario > LogicData::sMaxLogicScenarioByKingdom[worldId])
                scenario = LogicData::sMaxLogicScenarioByKingdom[worldId];
            if (scenario < minimumScenario)
                return;
            for (s32 i = 0; i < candidateCount; i++)
                if (candidates[i] == scenario)
                    return;
            candidates[candidateCount++] = scenario;
        };

        addCandidate(vanillaStart);
        addCandidate(LogicData::sPostGameScenarioByKingdom[worldId]);
        // A negative Moon Rock scenario means the kingdom has no Moon Rock
        // state (Mushroom, Dark, Darker, and Moon), so it is not a choice.
        addCandidate(LogicData::sMoonRockScenarioByKingdom[worldId]);
        if (candidateCount <= 0)
            return vanillaStart;

        // Use a separate per-kingdom stream. This makes the result independent
        // of iteration order and guarantees the same choice for every retry.
        const u32 kingdomSalt = static_cast<u32>(worldId + 1) * 0x9E3779B9u;
        Rng rng = makeRng(sSeed ^ kingdomSalt ^ 0x53544152u, RngStream::Logic);
        return candidates[rng.nextModulo(static_cast<u32>(candidateCount))];
    }
    // Both options are hard-pinned to vanilla behavior (off) when the feature is
    // compiled out, so restored saves and presets can never re-enable them.
#if SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
    bool isTripleMoonsAnywhereEnabled() { return sTripleMoonsAnywhereEnabled; }
    void setTripleMoonsAnywhereEnabled(bool enabled) { sTripleMoonsAnywhereEnabled = enabled; }
    bool isRandomizeTripleMoonsEnabled() { return sRandomizeTripleMoonsEnabled; }
    void setRandomizeTripleMoonsEnabled(bool enabled) { sRandomizeTripleMoonsEnabled = enabled; }
#else
    bool isTripleMoonsAnywhereEnabled() { return false; }
    void setTripleMoonsAnywhereEnabled(bool) { }
    bool isRandomizeTripleMoonsEnabled() { return false; }
    void setRandomizeTripleMoonsEnabled(bool) { }
#endif

    bool isMoonReallocationEnabled() { return sMoonReallocationEnabled; }
    void setMoonReallocationEnabled(bool enabled) { sMoonReallocationEnabled = enabled; }

    bool isRandomizeMoonAllocationEnabled() { return sRandomizeMoonAllocationEnabled; }
    void setRandomizeMoonAllocationEnabled(bool enabled) {
        sRandomizeMoonAllocationEnabled = enabled;
    }

    MoonAllocationDifficulty getMoonAllocationDifficulty() {
        return sMoonAllocationDifficulty;
    }

    void setMoonAllocationDifficulty(MoonAllocationDifficulty difficulty) {
        const u8 value = static_cast<u8>(difficulty);
        if (value > static_cast<u8>(MoonAllocationDifficulty::Max))
            difficulty = MoonAllocationDifficulty::Vanilla;
        sMoonAllocationDifficulty = difficulty;
    }

    const char* getMoonAllocationDifficultyName(MoonAllocationDifficulty difficulty) {
        switch (difficulty) {
        case MoonAllocationDifficulty::Easy:
            return "Easy";
        case MoonAllocationDifficulty::Hard:
            return "Hard";
        case MoonAllocationDifficulty::Expert:
            return "Expert";
        case MoonAllocationDifficulty::Max:
            return "Max";
        case MoonAllocationDifficulty::Vanilla:
        default:
            return "Vanilla";
        }
    }

    MoonCoinPolicy getMoonCoinPolicy() { return sMoonCoinPolicy; }

    void setMoonCoinPolicy(MoonCoinPolicy policy) {
        const u8 value = static_cast<u8>(policy);
        if (value > static_cast<u8>(MoonCoinPolicy::AbilityRush))
            policy = MoonCoinPolicy::Vanilla;
        sMoonCoinPolicy = policy;
    }

    const char* getMoonCoinPolicyName(MoonCoinPolicy policy) {
        switch (policy) {
        case MoonCoinPolicy::Standard:
            return "Standard";
        case MoonCoinPolicy::AbilityRush:
            return "Ability Rush";
        case MoonCoinPolicy::Vanilla:
        default:
            return "Vanilla";
        }
    }

    bool isMoonAllocationActive() {
        return isMoonRandoEnabled()
            && isMoonReallocationEnabled()
            && isRandomizeMoonAllocationEnabled();
    }

    s16 getKingdomMoonTarget(s32 worldId) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return -1;
        return sKingdomMoonTargets[worldId];
    }

    void setKingdomMoonTarget(s32 worldId, s16 count) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return;
        if (count < -1)
            count = -1;
        if (count > 657)
            count = 657;
        sKingdomMoonTargets[worldId] = count;
    }

    bool isKingdomMoonTargetCustom() {
        for (s32 worldId = 0; worldId < kKingdomCount; worldId++)
            if (sKingdomMoonTargets[worldId] >= 0)
                return true;
        return false;
    }

    void clearKingdomMoonTargets() {
        for (s32 worldId = 0; worldId < kKingdomCount; worldId++)
            sKingdomMoonTargets[worldId] = -1;
    }

    s8 getKingdomTripleMoonTarget(s32 worldId) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return -1;
        return sKingdomTripleMoonTargets[worldId];
    }

    void setKingdomTripleMoonTarget(s32 worldId, s8 count) {
        if (worldId < 0 || worldId >= kKingdomCount)
            return;
        if (count < -1)
            count = -1;
        if (count > 8)
            count = 8;
        sKingdomTripleMoonTargets[worldId] = count;
    }

    bool isKingdomTripleMoonTargetCustom() {
        for (s32 worldId = 0; worldId < kKingdomCount; worldId++)
            if (sKingdomTripleMoonTargets[worldId] >= 0)
                return true;
        return false;
    }

    void clearKingdomTripleMoonTargets() {
        for (s32 worldId = 0; worldId < kKingdomCount; worldId++)
            sKingdomTripleMoonTargets[worldId] = -1;
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ toggles Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬
    // Ã¢â€ â‚¬Ã¢â€ â‚¬ toggles Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬Ã¢â€ â‚¬

    static bool sMoonRandoEnabled = true;
    static EntranceRandoMode sEntranceRandoMode = EntranceRandoMode_Standard;
    static ShopLocationMode sShopLocationMode = ShopLocationMode_Randomized;
    static bool sPaintingRandoEnabled = true;
    static bool sKingdomRandoEnabled = false;
    static bool sLogicRandoEnabled = true;

    static SkyboxRandoMode sSkyboxRandoMode = SkyboxRandoMode_Kingdoms;
    static bool sStabilizeShadersEnabled = true;
    static bool sCaptureRandoEnabled = true;
    // Chaos categories are opt-in. Enemy/capture swapping is the only category
    // which can alter capture logic; NPC chaos may also use combat actors.
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    static bool sEnemySwapRandoEnabled = false;
    static ChaosSwapMode sNpcSwapMode = ChaosSwapMode::Off;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    static ChaosSwapMode sPropSwapMode = ChaosSwapMode::Off;
#endif

#if SMOR_FEATURE_CHAOS_ANY
    // Modes arrive from saves, presets and the seed inspector, so an out of
    // range value is data rather than a programming error. Clamp instead of
    // asserting: an unknown mode means a build we do not know about widened
    // the pool, and the safe reading of an unknown widening is the vanilla
    // behaviour, not the widest one.
    static ChaosSwapMode clampChaosSwapMode(ChaosSwapMode mode) {
        switch (mode) {
        case ChaosSwapMode::Off:
        case ChaosSwapMode::SameCategory:
        case ChaosSwapMode::WithCombat:
        case ChaosSwapMode::WithDeletion:
            return mode;
        }
        return ChaosSwapMode::Off;
    }
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
    static u8 sPropSwapChancePercent = 25;
#endif
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
    static bool sTwoDSwapRandoEnabled = false;
#endif
    static bool sChaosForceDisabled = false;
    static MusicRandoMode sMusicRandoMode = MusicRandoMode::On;
    static bool sMusicOverlayEnabled = true;
    static bool sStreamerModeEnabled = false;
    static bool sMusic2DThemesDisabled = false;
    static bool sMusicExtraThemesDisabled = false;
    static bool sMusicBossThemesDisabled = false;
    static constexpr u64 kAllMusicThemeMask = MusicDB::sThemeCount >= 64
        ? ~static_cast<u64>(0)
        : (static_cast<u64>(1) << MusicDB::sThemeCount) - 1;
    static u64 sMusicThemeMask = kAllMusicThemeMask;
    static MusicSourceMode sMusicSourceMode = MusicSourceMode::Vanilla;

    MusicSourceMode getMusicSourceMode() { return sMusicSourceMode; }

    void setMusicSourceMode(MusicSourceMode mode) {
        if (sMusicSourceMode == mode)
            return;
        // Logged because a setting that silently reverts (a stage reload
        // re-applying the save header, say) is otherwise indistinguishable from
        // a menu row that never committed at all.
        Randomizer::logPush("[CustomBgm] music source %s -> %s",
            getMusicSourceModeName(sMusicSourceMode), getMusicSourceModeName(mode));
        sMusicSourceMode = mode;
    }

    const char* getMusicSourceModeName(MusicSourceMode mode) {
        switch (mode) {
        case MusicSourceMode::Off:
            return "Off";
        case MusicSourceMode::Custom:
            return "Custom";
        case MusicSourceMode::Both:
            return "Vanilla + Custom";
        case MusicSourceMode::Vanilla:
        default:
            return "Vanilla";
        }
    }

    const char* getMusicSourceModeDescription(MusicSourceMode mode) {
        switch (mode) {
        case MusicSourceMode::Off:
            return "No background music at all. Sound effects and voices are untouched.";
        case MusicSourceMode::Custom:
            return "Play your own MP3s instead of the game's music. Drop them in sd:/SMO-Randomizer/Music/. Each area draws a track from the seed, so revisits keep the same song.";
        case MusicSourceMode::Both:
            return "Each area draws either a vanilla theme or one of your MP3s, fixed per seed. Put MP3s in sd:/SMO-Randomizer/Music/.";
        case MusicSourceMode::Vanilla:
        default:
            return "Play the game's own music. Music Rando above still decides how that pool is shuffled.";
        }
    }

    bool isCustomMusicPossible() {
        return sMusicSourceMode == MusicSourceMode::Custom
            || sMusicSourceMode == MusicSourceMode::Both;
    }

    // Coin and token profiles intentionally remain separate.  The active one
    // is chosen only by the special-moon reward selections above.
    static HintPriceProfile sHintCoinPriceProfile = { {
                                                          static_cast<u8>(HintCoinPrice::Coins250), // Moons
                                                          static_cast<u8>(HintCoinPrice::Coins500), // Captures
                                                          static_cast<u8>(HintCoinPrice::Coins500), // Abilities
                                                          static_cast<u8>(HintCoinPrice::Coins250), // Path
                                                          static_cast<u8>(HintCoinPrice::Disabled), // Purchases
                                                      },
        static_cast<u8>(HintCoinProgressiveIncrement::Disabled), false };
    static HintPriceProfile sHintTokenPriceProfile = { {
                                                           static_cast<u8>(HintTokenPrice::Tokens1),
                                                           static_cast<u8>(HintTokenPrice::Tokens1),
                                                           static_cast<u8>(HintTokenPrice::Tokens1),
                                                           static_cast<u8>(HintTokenPrice::Tokens1),
                                                           static_cast<u8>(HintTokenPrice::Disabled),
                                                       },
        static_cast<u8>(HintTokenProgressiveIncrement::Disabled), false };
    static u16 sHintCustomPrice[kHintPriceCategoryCount] = {};
    static u8 sHintCustomMask = 0;
    static u8 sHintCoinOverrideMask = 0;
    // Coin overrides can synthesize a price when the current token profile has
    // no matching fixed value. Remember that synthetic state so disabling the
    // override does not leave an invisible custom token price behind.
    static u8 sHintCoinOverrideAutoPriceMask = 0;
    static HintMode sHintMode = HintMode::Purchases;

    bool isGameModesFeatureEnabled() { return FeatureFlags::BingoAscend || FeatureFlags::ToadetteMedals; }
    bool isTripleMoonRandomizationFeatureEnabled() { return FeatureFlags::TripleMoonRandomization; }
    bool isCaptureLockFeatureEnabled() { return true; }
    bool isLogicFeatureEnabled() { return true; }
    bool isAbilityLockFeatureEnabled() { return true; }

    bool isMoonRandoEnabled() { return sMoonRandoEnabled; }
    void setMoonRandoEnabled(bool v) { sMoonRandoEnabled = v; }

    static bool isValidEntranceRandoMode(EntranceRandoMode mode) {
        return mode == EntranceRandoMode_Off || mode == EntranceRandoMode_Standard
            || mode == EntranceRandoMode_Decoupled || mode == EntranceRandoMode_Limited
            || mode == EntranceRandoMode_SmallChains || mode == EntranceRandoMode_LongChains;
    }

    EntranceRandoMode getEntranceRandoMode() { return sEntranceRandoMode; }
    void setEntranceRandoMode(EntranceRandoMode mode) {
        if (!isValidEntranceRandoMode(mode))
            mode = EntranceRandoMode_Standard;
        sEntranceRandoMode = mode;
    }

    void applyLegacyEntranceRandoMode(EntranceRandoMode mode) {
        // Short chains mean MORE chains for a fixed door count, so more
        // independent overworld entry points: Small Chains was the more OPEN
        // shape and Long Chains the more compact one.
        if (mode == EntranceRandoMode_SmallChains) {
            setEntranceRandoMode(EntranceRandoMode_Standard);
            setSeedOpenness(SeedOpenness_Open);
            return;
        }
        if (mode == EntranceRandoMode_LongChains) {
            setEntranceRandoMode(EntranceRandoMode_Standard);
            setSeedOpenness(SeedOpenness_Compact);
            return;
        }
        setEntranceRandoMode(mode);
    }

    bool isEntranceRandoEnabled() { return sEntranceRandoMode != EntranceRandoMode_Off; }
    bool isEntranceRandoDecoupled() { return sEntranceRandoMode == EntranceRandoMode_Decoupled; }
    void setEntranceRandoEnabled(bool v) {
        if (v) {
            if (sEntranceRandoMode == EntranceRandoMode_Off)
                sEntranceRandoMode = EntranceRandoMode_Standard;
        } else {
            sEntranceRandoMode = EntranceRandoMode_Off;
        }
    }

    const char* getEntranceRandoModeName(EntranceRandoMode mode) {
        switch (mode) {
        case EntranceRandoMode_Off:
            return "Off";
        case EntranceRandoMode_Decoupled:
            return "Decoupled";
        case EntranceRandoMode_Limited:
            return "Limited";
        case EntranceRandoMode_SmallChains:
            return "Small Chains (retired)";
        case EntranceRandoMode_LongChains:
            return "Long Chains (retired)";
        case EntranceRandoMode_Standard:
        default:
            return "Standard";
        }
    }

    const char* getCurrentEntranceRandoModeName() {
        return getEntranceRandoModeName(sEntranceRandoMode);
    }

    ShopLocationMode getShopLocationMode() { return sShopLocationMode; }
    void setShopLocationMode(ShopLocationMode mode) {
        sShopLocationMode = mode <= ShopLocationMode_Vanilla
            ? mode
            : ShopLocationMode_Randomized;
    }

    const char* getShopLocationModeName(ShopLocationMode mode) {
        switch (mode) {
        case ShopLocationMode_Swap:
            return "Swap";
        case ShopLocationMode_Vanilla:
            return "Vanilla";
        case ShopLocationMode_Randomized:
        default:
            return "Randomized";
        }
    }

    const char* getCurrentShopLocationModeName() {
        return getShopLocationModeName(sShopLocationMode);
    }

    bool isPaintingRandoEnabled() { return sPaintingRandoEnabled; }
    void setPaintingRandoEnabled(bool v) { sPaintingRandoEnabled = v; }

    bool isKingdomRandoEnabled() { return sKingdomRandoEnabled; }
    void setKingdomRandoEnabled(bool v) { sKingdomRandoEnabled = v; }

    bool isRequirementRandoEnabled() { return true; }
    void setRequirementRandoEnabled(bool) { }

    bool isLogicRandoEnabled() { return isLogicFeatureEnabled() && sLogicRandoEnabled; }
    void setLogicRandoEnabled(bool v) { sLogicRandoEnabled = isLogicFeatureEnabled() && v; }

    static bool isValidSkyboxRandoMode(SkyboxRandoMode mode) {
        return mode >= SkyboxRandoMode_None && mode <= SkyboxRandoMode_Max;
    }

    SkyboxRandoMode getSkyboxRandoMode() { return sSkyboxRandoMode; }
    void setSkyboxRandoMode(SkyboxRandoMode mode) {
        sSkyboxRandoMode = isValidSkyboxRandoMode(mode) ? mode : SkyboxRandoMode_None;
    }

    const char* getSkyboxRandoModeName(SkyboxRandoMode mode) {
        switch (mode) {
        case SkyboxRandoMode_Kingdoms:
            return "Kingdoms";
        case SkyboxRandoMode_Subareas:
            return "Subareas";
        case SkyboxRandoMode_OwnAreas:
            return "Own Areas";
        case SkyboxRandoMode_Max:
            return "Max";
        case SkyboxRandoMode_None:
        default:
            return "None";
        }
    }

    bool isSkyboxRandoEnabled() { return sSkyboxRandoMode != SkyboxRandoMode_None; }
    bool isKingdomSkyboxRandoEnabled() {
        return sSkyboxRandoMode == SkyboxRandoMode_Kingdoms
            || sSkyboxRandoMode == SkyboxRandoMode_OwnAreas
            || sSkyboxRandoMode == SkyboxRandoMode_Max;
    }
    bool isSubAreaSkyboxRandoEnabled() {
        return sSkyboxRandoMode == SkyboxRandoMode_Subareas
            || sSkyboxRandoMode == SkyboxRandoMode_OwnAreas
            || sSkyboxRandoMode == SkyboxRandoMode_Max;
    }

    bool isStabilizeShadersEnabled() { return sStabilizeShadersEnabled; }
    void setStabilizeShadersEnabled(bool v) { sStabilizeShadersEnabled = v; }

    bool isCaptureRandoEnabled() { return isCaptureLockFeatureEnabled() && sCaptureRandoEnabled; }
    void setCaptureRandoEnabled(bool v) { sCaptureRandoEnabled = isCaptureLockFeatureEnabled() && v; }

    bool isEnemySwapRandoEnabled() {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        return sEnemySwapRandoEnabled;
#else
        return false;
#endif
    }
    void setEnemySwapRandoEnabled(bool v) {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        sEnemySwapRandoEnabled = v;
#else
        (void)v;
#endif
    }
    bool isEnemyNpcSwapRandoEnabled() {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        return sEnemySwapRandoEnabled || sNpcSwapMode != ChaosSwapMode::Off;
#else
        return false;
#endif
    }
    void setEnemyNpcSwapRandoEnabled(bool v) {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        sEnemySwapRandoEnabled = v;
        sNpcSwapMode = v ? ChaosSwapMode::WithCombat : ChaosSwapMode::Off;
#else
        (void)v;
#endif
    }
    ChaosSwapMode getNpcSwapMode() {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        return sNpcSwapMode;
#else
        return ChaosSwapMode::Off;
#endif
    }
    void setNpcSwapMode(ChaosSwapMode mode) {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        // Deletion is meaningful only for props. Treat a persisted/corrupt
        // NPC value of 3 as the widest valid NPC mode.
        sNpcSwapMode = mode == ChaosSwapMode::WithDeletion
            ? ChaosSwapMode::WithCombat
            : clampChaosSwapMode(mode);
#else
        (void)mode;
#endif
    }
    ChaosSwapMode getPropSwapMode() {
#if SMOR_FEATURE_PROP_RANDOMIZATION
        return sPropSwapMode;
#else
        return ChaosSwapMode::Off;
#endif
    }
    void setPropSwapMode(ChaosSwapMode mode) {
#if SMOR_FEATURE_PROP_RANDOMIZATION
        sPropSwapMode = clampChaosSwapMode(mode);
#else
        (void)mode;
#endif
    }
    // WithCombat is what the boolean used to mean: before the modes existed
    // both categories drew from the combat pool and nothing else.
    bool isNpcSwapRandoEnabled() {
        return getNpcSwapMode() != ChaosSwapMode::Off;
    }
    void setNpcSwapRandoEnabled(bool v) {
        setNpcSwapMode(v ? ChaosSwapMode::WithCombat : ChaosSwapMode::Off);
    }
    bool isPropSwapRandoEnabled() {
        return getPropSwapMode() != ChaosSwapMode::Off;
    }
    void setPropSwapRandoEnabled(bool v) {
        setPropSwapMode(v ? ChaosSwapMode::WithCombat : ChaosSwapMode::Off);
    }
    bool isTwoDSwapRandoEnabled() {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        return sTwoDSwapRandoEnabled;
#else
        return false;
#endif
    }
    void setTwoDSwapRandoEnabled(bool v) {
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        sTwoDSwapRandoEnabled = v;
#else
        (void)v;
#endif
    }
    bool isAnyChaosRandomizationConfigured() {
        bool configured = false;
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
        configured = configured || sEnemySwapRandoEnabled || sTwoDSwapRandoEnabled
            || sNpcSwapMode != ChaosSwapMode::Off;
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
        configured = configured || sPropSwapMode != ChaosSwapMode::Off;
#endif
        return configured;
    }
    bool isChaosForceDisabled() { return sChaosForceDisabled; }
    void setChaosForceDisabled(bool v) { sChaosForceDisabled = v; }
    bool isChaosRandomizationActive() {
        return !sChaosForceDisabled && isAnyChaosRandomizationConfigured();
    }
    u8 getPropSwapChancePercent() {
#if SMOR_FEATURE_PROP_RANDOMIZATION
        return sPropSwapChancePercent;
#else
        return 25;
#endif
    }
    void setPropSwapChancePercent(u8 percent) {
#if SMOR_FEATURE_PROP_RANDOMIZATION
        switch (percent) {
        case 25:
        case 50:
        case 75:
        case 100:
            sPropSwapChancePercent = percent;
            break;
        default:
            sPropSwapChancePercent = 25;
            break;
        }
#else
        (void)percent;
#endif
    }

    MusicRandoMode getMusicRandoMode() { return sMusicRandoMode; }
    void setMusicRandoMode(MusicRandoMode mode) {
        MusicRandoMode next = MusicRandoMode::Off;
        switch (mode) {
        case MusicRandoMode::On:
        case MusicRandoMode::Max:
        case MusicRandoMode::Extended:
        case MusicRandoMode::Off:
            next = mode;
            break;
        default:
            break;
        }
        if (sMusicRandoMode != next)
            MusicHooks::resetMaxRequestState();
        sMusicRandoMode = next;
    }
    const char* getMusicRandoModeName(MusicRandoMode mode) {
        switch (mode) {
        case MusicRandoMode::On:
            return "Standard";
        case MusicRandoMode::Max:
            return "Max";
        case MusicRandoMode::Extended:
            return "Extended";
        case MusicRandoMode::Off:
        default:
            return "Off";
        }
    }
    bool isMusicRandoEnabled() { return sMusicRandoMode != MusicRandoMode::Off; }
    void setMusicRandoEnabled(bool v) {
        setMusicRandoMode(v ? MusicRandoMode::On : MusicRandoMode::Off);
    }

    bool isMusicOverlayEnabled() { return sMusicOverlayEnabled; }
    void setMusicOverlayEnabled(bool v) { sMusicOverlayEnabled = v; }

    bool isStreamerModeEnabled() { return sStreamerModeEnabled; }
    void setStreamerModeEnabled(bool v) {
        if (sStreamerModeEnabled != v)
            MusicHooks::resetMaxRequestState();
        sStreamerModeEnabled = v;
    }

    bool isMusic2DThemesDisabled() { return sMusic2DThemesDisabled; }
    void setMusic2DThemesDisabled(bool v) {
        if (sMusic2DThemesDisabled != v)
            MusicHooks::resetMaxRequestState();
        sMusic2DThemesDisabled = v;
    }

    bool isMusicExtraThemesDisabled() { return sMusicExtraThemesDisabled; }
    void setMusicExtraThemesDisabled(bool v) {
        if (sMusicExtraThemesDisabled != v)
            MusicHooks::resetMaxRequestState();
        sMusicExtraThemesDisabled = v;
    }

    bool isMusicBossThemesDisabled() { return sMusicBossThemesDisabled; }
    void setMusicBossThemesDisabled(bool v) {
        if (sMusicBossThemesDisabled != v)
            MusicHooks::resetMaxRequestState();
        sMusicBossThemesDisabled = v;
    }

    u64 getMusicThemeMask() {
        return sMusicThemeMask & kAllMusicThemeMask;
    }

    void setMusicThemeMask(u64 mask) {
        mask &= kAllMusicThemeMask;
        if (sMusicThemeMask != mask)
            MusicHooks::resetMaxRequestState();
        sMusicThemeMask = mask;
    }

    bool isMusicThemeEnabled(s32 themeId) {
        return themeId >= 0 && themeId < MusicDB::sThemeCount
            && (getMusicThemeMask() & (static_cast<u64>(1) << themeId)) != 0;
    }

    void setMusicThemeEnabled(s32 themeId, bool enabled) {
        if (themeId < 0 || themeId >= MusicDB::sThemeCount)
            return;
        u64 mask = getMusicThemeMask();
        const u64 bit = static_cast<u64>(1) << themeId;
        if (enabled)
            mask |= bit;
        else
            mask &= ~bit;
        setMusicThemeMask(mask);
    }

    namespace {
        constexpr s32 cMaxHintPrice = 0x7fffffff;
        HintPriceProfile makeCoinProfile(HintCoinPrice moons, HintCoinPrice captures,
            HintCoinPrice abilities, HintCoinPrice path, HintCoinPrice purchases) {
            return { {
                         static_cast<u8>(moons),
                         static_cast<u8>(captures),
                         static_cast<u8>(abilities),
                         static_cast<u8>(path),
                         static_cast<u8>(purchases),
                     },
                static_cast<u8>(HintCoinProgressiveIncrement::Disabled), false };
        }

        HintPriceProfile getDefaultCoinProfile() {
            return makeCoinProfile(HintCoinPrice::Coins250, HintCoinPrice::Coins500,
                HintCoinPrice::Coins500, HintCoinPrice::Coins250, HintCoinPrice::Disabled);
        }

        HintPriceProfile getDefaultTokenProfile() {
            return { {
                         static_cast<u8>(HintTokenPrice::Tokens1),
                         static_cast<u8>(HintTokenPrice::Tokens1),
                         static_cast<u8>(HintTokenPrice::Tokens1),
                         static_cast<u8>(HintTokenPrice::Tokens1), // Path
                         static_cast<u8>(HintTokenPrice::Disabled), // Purchases
                     },
                static_cast<u8>(HintTokenProgressiveIncrement::Disabled), false };
        }

        HintPriceProfile getLegacyTierProfile(HintPriceTier tier) {
            switch (tier) {
            case HintPriceTier::Cheap:
                return makeCoinProfile(HintCoinPrice::Coins100, HintCoinPrice::Coins200,
                    HintCoinPrice::Coins200, HintCoinPrice::Coins100,
                    HintCoinPrice::Coins1000);
            case HintPriceTier::Expensive:
                return makeCoinProfile(HintCoinPrice::Coins500, HintCoinPrice::Coins1000,
                    HintCoinPrice::Coins1000, HintCoinPrice::Coins500,
                    HintCoinPrice::Coins2500);
            case HintPriceTier::Normal:
            default:
                // Hnt predates per-category profiles; preserve its original
                // normal-tier direct-purchase cost on migration. New profiles
                // use getDefaultCoinProfile(), whose Purchases entry is Disabled.
                return makeCoinProfile(HintCoinPrice::Coins250, HintCoinPrice::Coins500,
                    HintCoinPrice::Coins500, HintCoinPrice::Coins250,
                    HintCoinPrice::Coins2500);
            }
        }

        // Only backs the legacy HintPriceTier projection, which is a shape of
        // base prices and their increment.  progressiveGlobal deliberately
        // takes no part: it changes which counter the ladder reads, not what
        // any of the tiers charge.
        bool profilesEqual(const HintPriceProfile& lhs, const HintPriceProfile& rhs) {
            for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
                if (lhs.values[index] != rhs.values[index])
                    return false;
            }
            return lhs.progressiveIncrement == rhs.progressiveIncrement;
        }

        s32 getCoinPriceAmount(u8 option) {
            switch (static_cast<HintCoinPrice>(option)) {
            case HintCoinPrice::Coins100:
                return 100;
            case HintCoinPrice::Coins200:
                return 200;
            case HintCoinPrice::Coins250:
                return 250;
            case HintCoinPrice::Coins500:
                return 500;
            case HintCoinPrice::Coins1000:
                return 1000;
            case HintCoinPrice::Coins2000:
                return 2000;
            case HintCoinPrice::Coins2500:
                return 2500;
            default:
                return 0;
            }
        }

        s32 getTokenPriceAmount(u8 option) {
            switch (static_cast<HintTokenPrice>(option)) {
            case HintTokenPrice::Tokens1:
                return 1;
            case HintTokenPrice::Tokens2:
                return 2;
            case HintTokenPrice::Tokens3:
                return 3;
            case HintTokenPrice::Tokens5:
                return 5;
            default:
                return 0;
            }
        }

        s32 getCoinIncrementAmount(u8 option) {
            return getCoinPriceAmount(option);
        }

        s32 getTokenIncrementAmount(u8 option) {
            return getTokenPriceAmount(option);
        }

        void normalizeLegacyProgressive(HintPriceProfile* profile, bool tokenProfile) {
            bool sawLegacyProgressive = false;
            const u8 legacy = tokenProfile ? static_cast<u8>(HintTokenPrice::Progressive)
                                           : static_cast<u8>(HintCoinPrice::Progressive);
            const u8 base = tokenProfile ? static_cast<u8>(HintTokenPrice::Tokens1)
                                         : static_cast<u8>(HintCoinPrice::Coins250);
            for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
                if (profile->values[index] == legacy) {
                    profile->values[index] = base;
                    sawLegacyProgressive = true;
                }
            }
            const bool incrementValid = tokenProfile
                ? profile->progressiveIncrement
                    <= static_cast<u8>(HintTokenProgressiveIncrement::Tokens5)
                : profile->progressiveIncrement
                    <= static_cast<u8>(HintCoinProgressiveIncrement::Coins2500);
            if (sawLegacyProgressive && (!incrementValid || profile->progressiveIncrement == 0))
                profile->progressiveIncrement = tokenProfile
                    ? static_cast<u8>(HintTokenProgressiveIncrement::Tokens1)
                    : static_cast<u8>(HintCoinProgressiveIncrement::Coins250);
        }
    } // namespace

    // Always on now â€” the menu chooses which individual prices are available.
    bool isHintSystemEnabled() { return true; }

    bool isValidHintPriceCategory(HintPriceCategory category) {
        return static_cast<u8>(category) < kHintPriceCategoryCount;
    }

    bool isValidHintCoinPrice(u8 option) {
        return option <= static_cast<u8>(HintCoinPrice::Coins2500);
    }

    bool isValidHintTokenPrice(u8 option) {
        return option <= static_cast<u8>(HintTokenPrice::Tokens5);
    }

    bool isValidHintCoinProgressiveIncrement(u8 option) {
        return option <= static_cast<u8>(HintCoinProgressiveIncrement::Coins2500);
    }

    bool isValidHintTokenProgressiveIncrement(u8 option) {
        return option <= static_cast<u8>(HintTokenProgressiveIncrement::Tokens5);
    }

    HintPriceProfile getHintCoinPriceProfile() { return sHintCoinPriceProfile; }

    void setHintCoinPriceProfile(HintPriceProfile profile) {
        const HintPriceProfile defaults = getDefaultCoinProfile();
        // Old v10 profiles may contain the now-retired per-category sentinel.
        // Translate it before normal validation discards it.
        normalizeLegacyProgressive(&profile, false);
        for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
            if (!isValidHintCoinPrice(profile.values[index]))
                profile.values[index] = defaults.values[index];
        }
        if (!isValidHintCoinProgressiveIncrement(profile.progressiveIncrement))
            profile.progressiveIncrement = defaults.progressiveIncrement;
        sHintCoinPriceProfile = profile;
    }

    HintPriceProfile getHintTokenPriceProfile() { return sHintTokenPriceProfile; }

    void setHintTokenPriceProfile(HintPriceProfile profile) {
        const HintPriceProfile defaults = getDefaultTokenProfile();
        normalizeLegacyProgressive(&profile, true);
        for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
            if (!isValidHintTokenPrice(profile.values[index]))
                profile.values[index] = defaults.values[index];
        }
        if (!isValidHintTokenProgressiveIncrement(profile.progressiveIncrement))
            profile.progressiveIncrement = defaults.progressiveIncrement;
        sHintTokenPriceProfile = profile;
    }

    u8 getHintCoinPrice(HintPriceCategory category) {
        return isValidHintPriceCategory(category)
            ? sHintCoinPriceProfile.values[static_cast<u8>(category)]
            : static_cast<u8>(HintCoinPrice::Disabled);
    }

    void setHintCoinPrice(HintPriceCategory category, u8 option) {
        if (!isValidHintPriceCategory(category) || !isValidHintCoinPrice(option))
            return;
        sHintCoinPriceProfile.values[static_cast<u8>(category)] = option;
    }

    u8 getHintTokenPrice(HintPriceCategory category) {
        return isValidHintPriceCategory(category)
            ? sHintTokenPriceProfile.values[static_cast<u8>(category)]
            : static_cast<u8>(HintTokenPrice::Disabled);
    }

    void setHintTokenPrice(HintPriceCategory category, u8 option) {
        if (!isValidHintPriceCategory(category) || !isValidHintTokenPrice(option))
            return;
        sHintTokenPriceProfile.values[static_cast<u8>(category)] = option;
    }

    u8 getActiveHintPrice(HintPriceCategory category) {
        return isHintTokenMode() ? getHintTokenPrice(category) : getHintCoinPrice(category);
    }

    void setActiveHintPrice(HintPriceCategory category, u8 option) {
        if (isHintTokenMode())
            setHintTokenPrice(category, option);
        else
            setHintCoinPrice(category, option);
        clearHintPriceOverride(category);
    }

    u8 getHintCoinProgressiveIncrement() { return sHintCoinPriceProfile.progressiveIncrement; }
    void setHintCoinProgressiveIncrement(u8 option) {
        if (isValidHintCoinProgressiveIncrement(option))
            sHintCoinPriceProfile.progressiveIncrement = option;
    }
    u8 getHintTokenProgressiveIncrement() { return sHintTokenPriceProfile.progressiveIncrement; }
    void setHintTokenProgressiveIncrement(u8 option) {
        if (isValidHintTokenProgressiveIncrement(option))
            sHintTokenPriceProfile.progressiveIncrement = option;
    }
    u8 getActiveHintProgressiveIncrement() {
        return isHintTokenMode() ? getHintTokenProgressiveIncrement()
                                 : getHintCoinProgressiveIncrement();
    }
    void setActiveHintProgressiveIncrement(u8 option) {
        if (isHintTokenMode())
            setHintTokenProgressiveIncrement(option);
        else
            setHintCoinProgressiveIncrement(option);
    }

    bool isHintCoinProgressiveGlobal() { return sHintCoinPriceProfile.progressiveGlobal; }
    void setHintCoinProgressiveGlobal(bool global) {
        sHintCoinPriceProfile.progressiveGlobal = global;
    }
    bool isHintTokenProgressiveGlobal() { return sHintTokenPriceProfile.progressiveGlobal; }
    void setHintTokenProgressiveGlobal(bool global) {
        sHintTokenPriceProfile.progressiveGlobal = global;
    }
    bool isActiveHintProgressiveGlobal() {
        return isHintTokenMode() ? isHintTokenProgressiveGlobal()
                                 : isHintCoinProgressiveGlobal();
    }
    void setActiveHintProgressiveGlobal(bool global) {
        if (isHintTokenMode())
            setHintTokenProgressiveGlobal(global);
        else
            setHintCoinProgressiveGlobal(global);
    }

    bool isHintPriceCustom(HintPriceCategory category) {
        return isValidHintPriceCategory(category)
            && (sHintCustomMask & (1u << static_cast<u8>(category))) != 0;
    }

    u16 getHintCustomPrice(HintPriceCategory category) {
        return isValidHintPriceCategory(category)
            ? sHintCustomPrice[static_cast<u8>(category)]
            : 0;
    }

    static s32 fixedHintAmountForActiveCurrency(u8 option) {
        return isHintTokenMode() ? getTokenPriceAmount(option) : getCoinPriceAmount(option);
    }

    void setHintCustomPrice(HintPriceCategory category, u16 price) {
        if (!isValidHintPriceCategory(category))
            return;
        if (price == 0)
            price = 1;
        if (price > 9999)
            price = 9999;

        const u8 index = static_cast<u8>(category);
        sHintCustomPrice[index] = price;
        sHintCoinOverrideAutoPriceMask &= static_cast<u8>(~(1u << index));
        // A coin override is an explicit per-entry coin price even while the
        // global profile is using tokens. Keep it custom regardless of whether
        // the number happens to match one of the token profile's fixed values.
        // Otherwise the parent row would silently lose the override when a
        // player enters a value such as 1 or 5.
        if (isHintCoinOverride(category)) {
            sHintCustomMask |= static_cast<u8>(1u << index);
            return;
        }
        // A numeric entry that lands exactly on a normal option should become
        // that option again. This keeps the parent row honest: Custom only
        // appears when the stored number is genuinely outside its fixed table.
        const u8 optionCount = isHintTokenMode()
            ? static_cast<u8>(HintTokenPrice::Progressive)
            : static_cast<u8>(HintCoinPrice::Progressive);
        for (u8 option = 0; option < optionCount; option++) {
            if (fixedHintAmountForActiveCurrency(option) == price) {
                if (isHintTokenMode())
                    setHintTokenPrice(category, option);
                else
                    setHintCoinPrice(category, option);
                sHintCustomMask &= static_cast<u8>(~(1u << index));
                return;
            }
        }
        sHintCustomMask |= static_cast<u8>(1u << index);
    }

    void setHintCategoryCurrency(HintPriceCategory category, HintCurrencyMode currency) {
        if (!isValidHintPriceCategory(category))
            return;
        const u8 bit = static_cast<u8>(1u << static_cast<u8>(category));
        if (currency == HintCurrencyMode::Coins)
            sHintCoinOverrideMask |= bit;
        else
            sHintCoinOverrideMask &= static_cast<u8>(~bit);
        // Whatever price the category had was quoted in the other currency, so
        // an explicit typed price no longer describes it. Drop back to that
        // currency's own price bank rather than carrying the number across.
        sHintCoinOverrideAutoPriceMask &= static_cast<u8>(~bit);
        sHintCustomMask &= static_cast<u8>(~bit);
        sHintCustomPrice[static_cast<u8>(category)] = 0;
    }

    bool isHintCoinOverride(HintPriceCategory category) {
        return isValidHintPriceCategory(category)
            && (sHintCoinOverrideMask & (1u << static_cast<u8>(category))) != 0;
    }

    void setHintCoinOverride(HintPriceCategory category, bool enabled) {
        if (!isValidHintPriceCategory(category))
            return;
        const u8 index = static_cast<u8>(category);
        const u8 bit = static_cast<u8>(1u << index);
        if (enabled) {
            if (!isHintCoinOverride(category) && !isHintPriceCustom(category)) {
                const s32 amount = getCoinPriceAmount(getHintCoinPrice(category));
                sHintCustomPrice[index] = static_cast<u16>(amount > 0 ? amount : 1);
                sHintCustomMask |= bit;
                sHintCoinOverrideAutoPriceMask |= bit;
            }
            sHintCoinOverrideMask |= bit;
            return;
        }

        const bool hadAutoPrice = (sHintCoinOverrideAutoPriceMask & bit) != 0;
        sHintCoinOverrideMask &= static_cast<u8>(~bit);
        if (hadAutoPrice) {
            sHintCoinOverrideAutoPriceMask &= static_cast<u8>(~bit);
            sHintCustomMask &= static_cast<u8>(~bit);
            sHintCustomPrice[index] = 0;
            return;
        }

        if (isHintPriceCustom(category)) {
            // Leaving the override can land back on a normal token option; the
            // custom-price setter performs that normalization for us.
            const u16 price = sHintCustomPrice[index];
            setHintCustomPrice(category, price);
        }
    }

    void clearHintPriceOverride(HintPriceCategory category) {
        if (!isValidHintPriceCategory(category))
            return;
        const u8 bit = static_cast<u8>(1u << static_cast<u8>(category));
        sHintCustomMask &= static_cast<u8>(~bit);
        sHintCoinOverrideMask &= static_cast<u8>(~bit);
        sHintCoinOverrideAutoPriceMask &= static_cast<u8>(~bit);
        sHintCustomPrice[static_cast<u8>(category)] = 0;
    }

    void clearHintPriceOverrides() {
        sHintCustomMask = 0;
        sHintCoinOverrideMask = 0;
        sHintCoinOverrideAutoPriceMask = 0;
        for (u8 i = 0; i < kHintPriceCategoryCount; i++)
            sHintCustomPrice[i] = 0;
    }

    u32 packHintPriceProfile(HintPriceProfile profile) {
        u32 packed = 0;
        for (u8 index = 0; index < kHintPriceCategoryCount; ++index)
            packed |= (static_cast<u32>(profile.values[index]) & 0xf) << (index * 4);
        packed |= (static_cast<u32>(profile.progressiveIncrement) & 0xf) << 20;
        if (profile.progressiveGlobal)
            packed |= 1u << 24;
        return packed;
    }

    HintPriceProfile unpackHintCoinPriceProfile(u32 packed) {
        HintPriceProfile profile = getDefaultCoinProfile();
        for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
            const u8 option = static_cast<u8>((packed >> (index * 4)) & 0xf);
            if (option <= static_cast<u8>(HintCoinPrice::Progressive))
                profile.values[index] = option;
        }
        profile.progressiveIncrement = static_cast<u8>((packed >> 20) & 0xf);
        profile.progressiveGlobal = (packed & (1u << 24)) != 0;
        normalizeLegacyProgressive(&profile, false);
        if (!isValidHintCoinProgressiveIncrement(profile.progressiveIncrement))
            profile.progressiveIncrement = static_cast<u8>(HintCoinProgressiveIncrement::Disabled);
        return profile;
    }

    HintPriceProfile unpackHintTokenPriceProfile(u32 packed) {
        HintPriceProfile profile = getDefaultTokenProfile();
        for (u8 index = 0; index < kHintPriceCategoryCount; ++index) {
            const u8 option = static_cast<u8>((packed >> (index * 4)) & 0xf);
            if (option <= static_cast<u8>(HintTokenPrice::Progressive))
                profile.values[index] = option;
        }
        profile.progressiveIncrement = static_cast<u8>((packed >> 20) & 0xf);
        profile.progressiveGlobal = (packed & (1u << 24)) != 0;
        normalizeLegacyProgressive(&profile, true);
        if (!isValidHintTokenProgressiveIncrement(profile.progressiveIncrement))
            profile.progressiveIncrement = static_cast<u8>(HintTokenProgressiveIncrement::Disabled);
        return profile;
    }

    // Each hint category picks its own currency (Hint Settings page). The coin
    // override is what records that choice; without a token economy at all
    // there is nothing to choose, so the global mode is the answer.
    HintCurrencyMode getHintCategoryCurrency(HintPriceCategory category) {
        if (!isValidHintPriceCategory(category))
            return getHintCurrencyMode();
        if (getHintCurrencyMode() != HintCurrencyMode::HintTokens)
            return HintCurrencyMode::Coins;
        return isHintCoinOverride(category) ? HintCurrencyMode::Coins
                                            : HintCurrencyMode::HintTokens;
    }

    bool isHintCategoryProgressiveGlobal(HintPriceCategory category) {
        return getHintCategoryCurrency(category) == HintCurrencyMode::HintTokens
            ? isHintTokenProgressiveGlobal()
            : isHintCoinProgressiveGlobal();
    }

    HintPriceQuote getHintPriceQuote(HintPriceCategory category, u32 successfulPurchaseCount) {
        if (!isValidHintPriceCategory(category))
            return { getHintCurrencyMode(), 0, false, false };

        // The whole quote follows the category currency, not just the label:
        // a category paid in coins reads the coin price bank and the coin
        // increment, even while the rest of the run is on Hint Tokens.
        const HintCurrencyMode currency = getHintCategoryCurrency(category);
        const bool tokens = currency == HintCurrencyMode::HintTokens;
        const u8 categoryIndex = static_cast<u8>(category);
        if (isHintPriceCustom(category)) {
            const s32 amount = sHintCustomPrice[categoryIndex];
            return { currency, amount, amount > 0, false };
        }

        const u8 option = tokens ? getHintTokenPrice(category) : getHintCoinPrice(category);
        const u8 disabled = tokens
            ? static_cast<u8>(HintTokenPrice::Disabled)
            : static_cast<u8>(HintCoinPrice::Disabled);
        if (option == disabled)
            return { currency, 0, false, false };
        const s32 base = tokens ? getTokenPriceAmount(option) : getCoinPriceAmount(option);
        const u8 incrementOption = tokens ? getHintTokenProgressiveIncrement()
                                          : getHintCoinProgressiveIncrement();
        const s32 increment = tokens ? getTokenIncrementAmount(incrementOption)
                                     : getCoinIncrementAmount(incrementOption);
        const u64 increased = static_cast<u64>(base)
            + static_cast<u64>(successfulPurchaseCount) * static_cast<u64>(increment);
        const s32 amount = increased > static_cast<u64>(cMaxHintPrice)
            ? cMaxHintPrice
            : static_cast<s32>(increased);
        return { currency, amount, amount > 0, increment > 0 };
    }

    HintPriceTier getHintPriceTier() {
        if (profilesEqual(sHintCoinPriceProfile, getLegacyTierProfile(HintPriceTier::Cheap)))
            return HintPriceTier::Cheap;
        if (profilesEqual(sHintCoinPriceProfile, getLegacyTierProfile(HintPriceTier::Expensive)))
            return HintPriceTier::Expensive;
        return HintPriceTier::Normal;
    }

    void setHintPriceTier(HintPriceTier tier) {
        if (tier != HintPriceTier::Cheap && tier != HintPriceTier::Normal
            && tier != HintPriceTier::Expensive)
            tier = HintPriceTier::Normal;
        setHintCoinPriceProfile(getLegacyTierProfile(tier));
    }

    HintMode getHintMode() { return sHintMode; }
    void setHintMode(HintMode mode) {
        if (mode != HintMode::Basic && mode != HintMode::Extended
            && mode != HintMode::Purchases)
            mode = HintMode::Basic;
        // Hint Mode is retired, but old configuration and sidecar data still
        // carries its cumulative access gate.  Translate that gate before
        // discarding the mode, without disturbing any explicitly configured
        // base prices or progressive increments in either currency bank.
        if (mode == HintMode::Basic) {
            setHintCoinPrice(HintPriceCategory::Path,
                static_cast<u8>(HintCoinPrice::Disabled));
            setHintCoinPrice(HintPriceCategory::Purchases,
                static_cast<u8>(HintCoinPrice::Disabled));
            setHintTokenPrice(HintPriceCategory::Path,
                static_cast<u8>(HintTokenPrice::Disabled));
            setHintTokenPrice(HintPriceCategory::Purchases,
                static_cast<u8>(HintTokenPrice::Disabled));
        } else if (mode == HintMode::Extended) {
            setHintCoinPrice(HintPriceCategory::Purchases,
                static_cast<u8>(HintCoinPrice::Disabled));
            setHintTokenPrice(HintPriceCategory::Purchases,
                static_cast<u8>(HintTokenPrice::Disabled));
        }
        // Availability is now controlled solely by the individual Disabled
        // prices. Keep the compatibility state at Purchases after migration so
        // retired consumers cannot suppress an enabled price category.
        sHintMode = HintMode::Purchases;
    }

    const char* getHintModeName(HintMode mode) {
        switch (mode) {
        case HintMode::Extended:
            return "Extended";
        case HintMode::Purchases:
            return "Purchases";
        case HintMode::Basic:
        default:
            return "Basic";
        }
    }

    void cycleHintPriceTier() {
        setHintPriceTier(static_cast<HintPriceTier>(
            (static_cast<s32>(getHintPriceTier()) + 1) % 3));
    }

    const char* getHintPriceTierName(HintPriceTier tier) {
        switch (tier) {
        case HintPriceTier::Cheap:
            return "Cheap";
        case HintPriceTier::Expensive:
            return "Expensive";
        case HintPriceTier::Normal:
        default:
            return "Normal";
        }
    }

    // additional toggles

    static bool sSphinxRandoEnabled = true;
    static bool sLockedAbilitiesEnabled = true;
    // Multi-stage moves (jump, roll, and the prerequisite-gated moves)
    // normally arrive one stage at a time. Off hands each one over in its
    // final form the first time it is unlocked.
    static bool sProgressiveAbilitiesEnabled = true;
    // Wave B defaults every known item into its pool. Keeping the masks raw,
    // rather than folding in the master toggles, lets a player turn a lock
    // system off and later restore the exact pool they configured.
    static u64 sCaptureLockPoolMaskLo = ~0ull;
    static u64 sCaptureLockPoolMaskHi = ~0ull;
    static u64 sAbilityLockPoolMask = ~0ull;
    // Movement-ability locks (the Ability Lock sub-page). All default to the
    // vanilla behavior - granted - so an existing seed or preset that predates
    // the page plays exactly as it did.
    // Locked Abilities defaults to "All", so every Movement Ability dial starts
    // on too - the select reads the dials and the pool mask together, and a
    // dial left off would have made the default read as Original.
    static MoonGravityMode sMoonGravityMode = MoonGravityMode::AbilityLock;
    static bool sSwimLockEnabled = true;
    static bool sGrabLockEnabled = true;
    static bool sJaxiLockEnabled = true;
    static bool sScooterLockEnabled = true;
    static bool sRocketFlowerLockEnabled = true;
    static bool sNpcTalkLockEnabled = true;
    static bool sPipe2DLockEnabled = true;
    static bool sTimerChallengeLockEnabled = true;
    static bool sFastMoonDemosEnabled = false;
    static bool sMotionFreeControlsEnabled = false;
    static ControllerButton sUpThrowButton = ControllerButton::X;
    static ControllerButton sDownThrowJumpButton = ControllerButton::B;
    static ControllerButton sSpinShakeButton = ControllerButton::R;
    // QoL: skip the demo camera pan that AppearSwitchTimer plays. Defaults on to
    // match the previous always-on behavior of the hardcoded QoL patch.
    static bool sSkipCutsceneCamerasEnabled = true;
    // QoL: costume-check NPCs award their moon without a matching costume.
    static bool sFreeCostumeMoonEnabled = false;
    // QoL: sheep-herding moons can use a smaller configured target count.
    static bool sOneSheepHerdingEnabled = false;
    static s32 sSheepHerdingCount = kSheepHerdingDefaultCount;
    // QoL: jump-rope and volleyball moons use a configured low/high pair.
    static bool sEasyMinigamesEnabled = false;
    // QoL: the dig-spot dog goes straight to the moon's dig point, skipping
    // its coin spots and the wandering fake digs around each one.
    static bool sFastDogEnabled = false;
    // QoL: lift vanilla's 9999 coin ceiling - see CoinUncap.cpp.
    static bool sUncappedCoinsEnabled = false;
    static s32 sEasyMinigameLow = kEasyMinigameDefaultLow;
    static s32 sEasyMinigameHigh = kEasyMinigameDefaultHigh;
    // QoL: Hint Art dig points do not require first viewing the source artwork.
    static bool sUnlockHintArtEnabled = false;
    // Disabled by default: this is only a convenience/debug movement option.
    static bool sNoClipEnabled = false;
    // Tracker placement and optional Moon Kingdom cave-skip checklist. Both keep
    // the established left-side/no-checklist behavior until opted into.
    static bool sTrackerRightAligned = false;
    // QoL: hand out moons for kingdoms whose known requirement is met as if they
    // had already been collected.
    static bool sCompletedKingdomMoonCollectedEnabled = false;
    static bool sRandomizeOutfitsEnabled = false;
    // QoL: pin the seed's free starting cap throw to Neutral Throw.
    static bool sAlwaysNeutralCapThrowEnabled = false;

    static TrackerCaveSkipMode sTrackerCaveSkipMode = TrackerCaveSkipMode::None;
    // QoL: progress tracker HUD overlay. Defaults off (opt-in).
    static TrackerOverlayMode sTrackerOverlayMode = TrackerOverlayMode::None;
    // Tracker: moon-limit color coding. Defaults off (plain white counters).
    static TrackerMoonLimitMode sTrackerMoonLimitMode = TrackerMoonLimitMode::None;
    // Tracker: timer display. Defaults on
    static bool sTrackerTimerEnabled = true; // default on, matches old hardcoded behavior
    bool isTrackerTimerEnabled() { return sTrackerTimerEnabled; }
    void setTrackerTimerEnabled(bool value) { sTrackerTimerEnabled = value; }
    // Board HUD sizing. Medium is the established layout.
    static BoardScale sBoardScale = BoardScale::Medium;
    // QoL: current-area name banner, shown on entering a kingdom or subarea.
    static bool sAreaNameOverlayEnabled = true;
    // Purple shops are an independently seeded inventory. These defaults keep
    // the feature useful on a fresh seed without making its price punitive.
    static s32 sPurpleShopAbilityCount = 1;
    static s32 sPurpleShopCaptureCount = 1;
    static bool sPurpleShopMoonsEnabled = true;
    static PurpleShopPrice sPurpleShopPrice = PurpleShopPrice::Purple50;
    static s32 sPurpleShopCustomPrice = 50;

    bool isSphinxRandoEnabled() { return sSphinxRandoEnabled; }
    void setSphinxRandoEnabled(bool v) { sSphinxRandoEnabled = v; }

    bool isLockedAbilitiesEnabled() { return isAbilityLockFeatureEnabled() && sLockedAbilitiesEnabled; }
    void setLockedAbilitiesEnabled(bool v) { sLockedAbilitiesEnabled = isAbilityLockFeatureEnabled() && v; }

    bool isProgressiveAbilitiesEnabled() { return sProgressiveAbilitiesEnabled; }
    void setProgressiveAbilitiesEnabled(bool v) { sProgressiveAbilitiesEnabled = v; }

    bool isCaptureInLockPool(s32 captureId) {
        if (captureId < 0 || captureId >= CaptureRando::getCaptureCount())
            return false;
        const s32 bit = captureId < 64 ? captureId : captureId - 64;
        const u64 mask = captureId < 64 ? sCaptureLockPoolMaskLo : sCaptureLockPoolMaskHi;
        return (mask & (1ull << bit)) != 0;
    }

    void setCaptureInLockPool(s32 captureId, bool enabled) {
        if (captureId < 0 || captureId >= CaptureRando::getCaptureCount())
            return;
        const s32 bit = captureId < 64 ? captureId : captureId - 64;
        u64* mask = captureId < 64 ? &sCaptureLockPoolMaskLo : &sCaptureLockPoolMaskHi;
        const u64 value = 1ull << bit;
        if (enabled)
            *mask |= value;
        else
            *mask &= ~value;
    }

    bool isAbilityInLockPool(s32 abilityId) {
        if (abilityId < 0 || abilityId >= AbilityRando::getAbilityCount())
            return false;
        return (sAbilityLockPoolMask & (1ull << abilityId)) != 0;
    }

    void setAbilityInLockPool(s32 abilityId, bool enabled) {
        if (abilityId < 0 || abilityId >= AbilityRando::getAbilityCount())
            return;
        const u64 value = 1ull << abilityId;
        if (enabled)
            sAbilityLockPoolMask |= value;
        else
            sAbilityLockPoolMask &= ~value;
    }

    u64 getCaptureLockPoolMask(s32 bank) {
        if (bank == 0)
            return sCaptureLockPoolMaskLo;
        if (bank == 1)
            return sCaptureLockPoolMaskHi;
        return 0;
    }

    void setCaptureLockPoolMask(s32 bank, u64 mask) {
        if (bank == 0)
            sCaptureLockPoolMaskLo = mask;
        else if (bank == 1)
            sCaptureLockPoolMaskHi = mask;
    }

    u64 getAbilityLockPoolMask() { return sAbilityLockPoolMask; }

    void setAbilityLockPoolMask(u64 mask) { sAbilityLockPoolMask = mask; }

    void resetLockPools() {
        sCaptureLockPoolMaskLo = ~0ull;
        sCaptureLockPoolMaskHi = ~0ull;
        sAbilityLockPoolMask = ~0ull;
    }

    // Every movement-ability lock is a sub-option of Locked Abilities: the page
    // that carries them is hidden while it is off (isPageVisible), and the
    // predicates below fold to "granted" so nothing has to re-check the master
    // toggle at each of its call sites.
    MoonGravityMode getMoonGravityMode() { return sMoonGravityMode; }
    void setMoonGravityMode(MoonGravityMode mode) {
        switch (mode) {
        case MoonGravityMode::Unlocked:
        case MoonGravityMode::AbilityLock:
        case MoonGravityMode::MoonRequirements:
        case MoonGravityMode::ExtraMoonRequirements:
            sMoonGravityMode = mode;
            return;
        }
        sMoonGravityMode = MoonGravityMode::Unlocked;
    }

    const char* getMoonGravityModeName(MoonGravityMode mode) {
        switch (mode) {
        case MoonGravityMode::AbilityLock:
            return "Ability Lock";
        case MoonGravityMode::MoonRequirements:
            return "Moon Requirements";
        case MoonGravityMode::ExtraMoonRequirements:
            return "Extra Moon Requirements";
        default:
            return "Unlocked";
        }
    }

    bool isMoonGravityLocked() {
        return isLockedAbilitiesEnabled() && sMoonGravityMode != MoonGravityMode::Unlocked;
    }

    bool isMoonGravityAbilityLocked() {
        return isLockedAbilitiesEnabled() && sMoonGravityMode == MoonGravityMode::AbilityLock;
    }

    bool isMoonGravityCountLocked() {
        return isLockedAbilitiesEnabled()
            && (sMoonGravityMode == MoonGravityMode::MoonRequirements
                || sMoonGravityMode == MoonGravityMode::ExtraMoonRequirements);
    }

    s32 getMoonGravityExtraRequirement() {
        if (!isLockedAbilitiesEnabled()
            || sMoonGravityMode != MoonGravityMode::ExtraMoonRequirements)
            return 0;
        return RequirementRando::getMoonGravityExtra();
    }

    s32 getMoonGravityMoonRequirement() {
        if (!isMoonGravityCountLocked())
            return 0;
        // Deliberately RequirementRando rather than MoonRequirement::getRequirement:
        // that one reports 0 when the Moon Kingdom chapel gate is switched off, and
        // the gravity gate is a separate lock that should still have a number to
        // count towards. Both read the same per-kingdom value when the gate is on.
        const s32 base = RequirementRando::getRequirement(WorldMeta::WorldId_Moon);
        const s32 total = (base > 0 ? base : 0) + getMoonGravityExtraRequirement();
        return total > 0 ? total : 1;
    }

    bool isSwimLockEnabled() { return isLockedAbilitiesEnabled() && sSwimLockEnabled; }
    void setSwimLockEnabled(bool v) { sSwimLockEnabled = v; }
    bool isGrabLockEnabled() { return isLockedAbilitiesEnabled() && sGrabLockEnabled; }
    void setGrabLockEnabled(bool v) { sGrabLockEnabled = v; }
    bool isJaxiLockEnabled() { return isLockedAbilitiesEnabled() && sJaxiLockEnabled; }
    void setJaxiLockEnabled(bool v) { sJaxiLockEnabled = v; }
    bool isScooterLockEnabled() { return isLockedAbilitiesEnabled() && sScooterLockEnabled; }
    void setScooterLockEnabled(bool v) { sScooterLockEnabled = v; }
    bool isRocketFlowerLockEnabled() {
        return isLockedAbilitiesEnabled() && sRocketFlowerLockEnabled;
    }
    void setRocketFlowerLockEnabled(bool v) { sRocketFlowerLockEnabled = v; }
    bool isNpcTalkLockEnabled() { return isLockedAbilitiesEnabled() && sNpcTalkLockEnabled; }
    void setNpcTalkLockEnabled(bool v) { sNpcTalkLockEnabled = v; }
    bool isPipe2DLockEnabled() { return isLockedAbilitiesEnabled() && sPipe2DLockEnabled; }
    void setPipe2DLockEnabled(bool v) { sPipe2DLockEnabled = v; }
    bool isTimerChallengeLockEnabled() {
        return isLockedAbilitiesEnabled() && sTimerChallengeLockEnabled;
    }
    void setTimerChallengeLockEnabled(bool v) { sTimerChallengeLockEnabled = v; }

    // Reads the raw stored dials, not the isLockedAbilitiesEnabled()-folded
    // predicates: a seed saved with Locked Abilities off must still round-trip
    // the page's settings, so turning it back on restores what was configured.
    u32 packMovementAbilitySettings() {
        u32 packed = (u32)sMoonGravityMode & 0x3u;
        if (sSwimLockEnabled)
            packed |= 1u << 2;
        if (sGrabLockEnabled)
            packed |= 1u << 3;
        if (sJaxiLockEnabled)
            packed |= 1u << 4;
        if (sScooterLockEnabled)
            packed |= 1u << 5;
        if (sRocketFlowerLockEnabled)
            packed |= 1u << 6;
        if (sNpcTalkLockEnabled)
            packed |= 1u << 7;
        if (sPipe2DLockEnabled)
            packed |= 1u << 8;
        if (sTimerChallengeLockEnabled)
            packed |= 1u << 9;
        return packed;
    }

    void applyMovementAbilitySettings(u32 packed) {
        setMoonGravityMode((MoonGravityMode)(packed & 0x3u));
        setSwimLockEnabled((packed & (1u << 2)) != 0);
        setGrabLockEnabled((packed & (1u << 3)) != 0);
        setJaxiLockEnabled((packed & (1u << 4)) != 0);
        setScooterLockEnabled((packed & (1u << 5)) != 0);
        setRocketFlowerLockEnabled((packed & (1u << 6)) != 0);
        setNpcTalkLockEnabled((packed & (1u << 7)) != 0);
        setPipe2DLockEnabled((packed & (1u << 8)) != 0);
        setTimerChallengeLockEnabled((packed & (1u << 9)) != 0);
    }

    bool isFastMoonDemosEnabled() { return sFastMoonDemosEnabled; }
    void setFastMoonDemosEnabled(bool v) {
        sFastMoonDemosEnabled = v;
        // The actual effect is delivered by raw asm patches; keep their install
        // state in sync with the flag so it works regardless of where it was set
        // (main-menu file select, in-game submenu, or loading a saved seed).
        if (v)
            MoonHooks::installMoonCutsceneHooks();
        else
            MoonHooks::uninstallMoonCutsceneHooks();
    }

    bool isMotionFreeControlsEnabled() { return sMotionFreeControlsEnabled; }
    void setMotionFreeControlsEnabled(bool v) { sMotionFreeControlsEnabled = v; }

    const char16_t* getControllerButtonName(ControllerButton button) {
        switch (button) {
        case ControllerButton::A:
            return u"A";
        case ControllerButton::B:
            return u"B";
        case ControllerButton::X:
            return u"X";
        case ControllerButton::Y:
            return u"Y";
        case ControllerButton::L:
            return u"L";
        case ControllerButton::R:
            return u"R";
        case ControllerButton::ZL:
            return u"ZL";
        case ControllerButton::ZR:
            return u"ZR";
        default:
            return u"?";
        }
    }

    ControllerButton getUpThrowButton() { return sUpThrowButton; }
    void setUpThrowButton(ControllerButton button) { sUpThrowButton = button; }
    ControllerButton getDownThrowJumpButton() { return sDownThrowJumpButton; }
    void setDownThrowJumpButton(ControllerButton button) { sDownThrowJumpButton = button; }
    ControllerButton getSpinShakeButton() { return sSpinShakeButton; }
    void setSpinShakeButton(ControllerButton button) { sSpinShakeButton = button; }

    bool isSkipCutsceneCamerasEnabled() { return sSkipCutsceneCamerasEnabled; }
    void setSkipCutsceneCamerasEnabled(bool v) { sSkipCutsceneCamerasEnabled = v; }

    bool isFreeCostumeMoonEnabled() { return sFreeCostumeMoonEnabled; }
    void setFreeCostumeMoonEnabled(bool v) { sFreeCostumeMoonEnabled = v; }

    bool isOneSheepHerdingEnabled() { return sOneSheepHerdingEnabled; }
    void setOneSheepHerdingEnabled(bool v) { sOneSheepHerdingEnabled = v; }

    s32 getSheepHerdingCount() { return sSheepHerdingCount; }
    void setSheepHerdingCount(s32 count) {
        if (count < kSheepHerdingMinCount)
            count = kSheepHerdingMinCount;
        if (count > kSheepHerdingMaxCount)
            count = kSheepHerdingMaxCount;
        sSheepHerdingCount = count;
        // The count editor is the replacement for the old one-sheep switch:
        // choosing a count is an explicit request to enable the QoL.
        sOneSheepHerdingEnabled = true;
    }

    bool isEasyMinigamesEnabled() { return sEasyMinigamesEnabled; }
    void setEasyMinigamesEnabled(bool v) { sEasyMinigamesEnabled = v; }

    bool isFastDogEnabled() { return sFastDogEnabled; }
    void setFastDogEnabled(bool v) { sFastDogEnabled = v; }

    bool isUncappedCoinsEnabled() { return sUncappedCoinsEnabled; }
    void setUncappedCoinsEnabled(bool v) { sUncappedCoinsEnabled = v; }
    s32 getEasyMinigameLow() { return sEasyMinigameLow; }
    s32 getEasyMinigameHigh() { return sEasyMinigameHigh; }
    void setEasyMinigameLow(s32 value) {
        if (value < kEasyMinigameMin)
            value = kEasyMinigameMin;
        if (value > kEasyMinigameMax)
            value = kEasyMinigameMax;
        sEasyMinigameLow = value;
        if (sEasyMinigameHigh < value)
            sEasyMinigameHigh = value;
    }

    void setEasyMinigameHigh(s32 value) {
        if (value < kEasyMinigameMin)
            value = kEasyMinigameMin;
        if (value > kEasyMinigameMax)
            value = kEasyMinigameMax;
        sEasyMinigameHigh = value;
        if (sEasyMinigameLow > value)
            sEasyMinigameLow = value;
    }

    bool isUnlockHintArtEnabled() { return sUnlockHintArtEnabled; }
    void setUnlockHintArtEnabled(bool v) { sUnlockHintArtEnabled = v; }

    bool isNoClipEnabled() { return sNoClipEnabled; }
    void setNoClipEnabled(bool v) { sNoClipEnabled = v; }

    bool isTrackerRightAligned() { return sTrackerRightAligned; }
    void setTrackerRightAligned(bool v) { sTrackerRightAligned = v; }

    bool isCompletedKingdomMoonCollectedEnabled() { return sCompletedKingdomMoonCollectedEnabled; }
    void setCompletedKingdomMoonCollectedEnabled(bool v) { sCompletedKingdomMoonCollectedEnabled = v; }

    bool isRandomizeOutfitsEnabled() { return sRandomizeOutfitsEnabled; }
    void setRandomizeOutfitsEnabled(bool v) { sRandomizeOutfitsEnabled = v; }

    bool isAlwaysNeutralCapThrowEnabled() { return sAlwaysNeutralCapThrowEnabled; }
    void setAlwaysNeutralCapThrowEnabled(bool v) { sAlwaysNeutralCapThrowEnabled = v; }

    TrackerCaveSkipMode getTrackerCaveSkipMode() { return sTrackerCaveSkipMode; }
    void setTrackerCaveSkipMode(TrackerCaveSkipMode mode) { sTrackerCaveSkipMode = mode; }

    const char* getTrackerCaveSkipModeName(TrackerCaveSkipMode mode) {
        switch (mode) {
        case TrackerCaveSkipMode::Easy:
            return "Easy";
        case TrackerCaveSkipMode::Hard:
            return "Hard";
        case TrackerCaveSkipMode::None:
        default:
            return "None";
        }
    }

    bool isTrackerOverlayEnabled() { return sTrackerOverlayMode != TrackerOverlayMode::None; }
    TrackerOverlayMode getTrackerOverlayMode() { return sTrackerOverlayMode; }
    void setTrackerOverlayMode(TrackerOverlayMode mode) { sTrackerOverlayMode = mode; }

    const char* getTrackerOverlayModeName(TrackerOverlayMode mode) {
        switch (mode) {
        case TrackerOverlayMode::Basic:
            return "Basic";
        case TrackerOverlayMode::Extended:
            return "Extended";
        case TrackerOverlayMode::Hints:
            return "Hints";
        case TrackerOverlayMode::Guided:
            return "Guided";
        case TrackerOverlayMode::None:
        default:
            return "None";
        }
    }

    TrackerMoonLimitMode getTrackerMoonLimitMode() { return sTrackerMoonLimitMode; }
    void setTrackerMoonLimitMode(TrackerMoonLimitMode mode) { sTrackerMoonLimitMode = mode; }

    const char* getTrackerMoonLimitModeName(TrackerMoonLimitMode mode) {
        switch (mode) {
        case TrackerMoonLimitMode::Upper:
            return "Upper";
        case TrackerMoonLimitMode::Full:
            return "Full";
        case TrackerMoonLimitMode::None:
        default:
            return "None";
        }
    }

    BoardScale getBoardScale() { return sBoardScale; }
    void setBoardScale(BoardScale scale) {
        sBoardScale = scale <= BoardScale::Large ? scale : BoardScale::Medium;
    }

    const char* getBoardScaleName(BoardScale scale) {
        switch (scale) {
        case BoardScale::Small:
            return "Small";
        case BoardScale::Large:
            return "Large";
        case BoardScale::Medium:
        default:
            return "Medium";
        }
    }

    float getBoardScaleFactor() {
        switch (sBoardScale) {
        case BoardScale::Small:
            return 0.8f;
        case BoardScale::Large:
            return 1.2f;
        case BoardScale::Medium:
        default:
            return 1.0f;
        }
    }

    bool isAreaNameOverlayEnabled() { return sAreaNameOverlayEnabled; }
    void setAreaNameOverlayEnabled(bool v) { sAreaNameOverlayEnabled = v; }

    s32 getPurpleShopAbilityCount() { return sPurpleShopAbilityCount; }
    void setPurpleShopAbilityCount(s32 count) {
        sPurpleShopAbilityCount = count < 0 ? 0 : (count > 3 ? 3 : count);
    }
    s32 getPurpleShopCaptureCount() { return sPurpleShopCaptureCount; }
    void setPurpleShopCaptureCount(s32 count) {
        sPurpleShopCaptureCount = count < 0 ? 0 : (count > 3 ? 3 : count);
    }
    bool arePurpleShopMoonsEnabled() { return sPurpleShopMoonsEnabled; }
    void setPurpleShopMoonsEnabled(bool enabled) { sPurpleShopMoonsEnabled = enabled; }
    PurpleShopPrice getPurpleShopPrice() { return sPurpleShopPrice; }
    void setPurpleShopPrice(PurpleShopPrice price) {
        sPurpleShopPrice = price <= PurpleShopPrice::Custom
            ? price
            : PurpleShopPrice::Purple50;
    }
    s32 getPurpleShopPriceValue() {
        switch (sPurpleShopPrice) {
        case PurpleShopPrice::Purple10:
            return 10;
        case PurpleShopPrice::Purple20:
            return 20;
        case PurpleShopPrice::Purple25:
            return 25;
        case PurpleShopPrice::Purple100:
            return 100;
        case PurpleShopPrice::Custom:
            return sPurpleShopCustomPrice;
        // Random is per kingdom and has no flat amount; callers without a
        // kingdom in hand (settings summaries, the legacy log line) read the
        // default rather than a number that would be wrong everywhere.
        case PurpleShopPrice::Purple50:
        default:
            return 50;
        }
    }
    const char* getPurpleShopPriceName(PurpleShopPrice price) {
        switch (price) {
        case PurpleShopPrice::Purple10:
            return "10";
        case PurpleShopPrice::Purple20:
            return "20";
        case PurpleShopPrice::Purple25:
            return "25";
        case PurpleShopPrice::Purple100:
            return "100";
        case PurpleShopPrice::Random:
            return "Random";
        case PurpleShopPrice::Custom:
            return "Custom";
        case PurpleShopPrice::Purple50:
        default:
            return "50";
        }
    }

    s32 getPurpleShopCustomPrice() { return sPurpleShopCustomPrice; }

    void setPurpleShopCustomPrice(s32 price) {
        if (price < 1)
            price = 1;
        if (price > 100)
            price = 100;
        sPurpleShopCustomPrice = price;
    }

    bool isPurpleShopPriceCustom() {
        return sPurpleShopPrice == PurpleShopPrice::Custom;
    }

    bool isProportionalKingdomPricingEnabled() { return sProportionalKingdomPricing; }
    void setProportionalKingdomPricingEnabled(bool enabled) {
        sProportionalKingdomPricing = enabled;
    }

    s32 resolvePurpleShopPrice(s32 worldId, s32 purpleCoinTotal, u32 offerKey) {
        const s32 flat = getPurpleShopPriceValue();
        // Cap, Cloud, Moon and Mushroom have no regional coins of their own,
        // and a remapped shop can land on one of them. Neither kingdom-relative
        // mode has anything to scale against there, so both fall back to the
        // flat selection rather than quoting a price of 1.
        if (purpleCoinTotal <= 0)
            return flat;

        if (sPurpleShopPrice == PurpleShopPrice::Random) {
            // Keyed by kingdom and offer rather than drawn in sequence, so
            // re-entering a kingdom re-quotes the prices it already had, the
            // order shops are visited in cannot change any of them, and the
            // items on one shelf are priced independently of each other.
            Rng rng = makeRng(
                sSeed ^ (static_cast<u32>(worldId + 1) * 0x9e3779b9u) ^ offerKey,
                RngStream::ShopPrice);
            return static_cast<s32>(rng.nextModulo(static_cast<u32>(purpleCoinTotal))) + 1;
        }

        if (sProportionalKingdomPricing) {
            const s32 scaled = flat * purpleCoinTotal / 100;
            if (scaled < 1)
                return 1;
            return scaled > purpleCoinTotal ? purpleCoinTotal : scaled;
        }

        return flat;
    }

    static u32 sVisitedKingdoms = 0;
    u32 getVisitedKingdoms() { return sVisitedKingdoms; }
    void setVisitedKingdoms(u32 v) {
        if (v != sVisitedKingdoms)
            logPush("TRACK visited-mask %08x -> %08x", sVisitedKingdoms, v);
        sVisitedKingdoms = v;
    }

    // Sequence a kingdom was first physically visited in. -1 = unranked.
    static s8 sVisitOrder[WorldMeta::sWorldCount] = {
        -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
    };
    static s32 sNextVisitSequence = 0;

    // Shiveria Town is the only Snow Kingdom sub-area whose randomizable
    // entrances (SnowCostumeEx, ShopDoor) can drop the player there directly
    // from an unrelated kingdom, bypassing SnowWorldHomeStage entirely - so it
    // is the only sub-stage that needs an explicit kingdom mapping here.
    static s32 findKingdomIdForAnyStage(const char* stageName) {
        if (stageName && std::strcmp(stageName, "SnowWorldTownStage") == 0)
            return WorldMeta::WorldId_Snow;
        const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
        return entry ? entry->worldId : -1;
    }

    void recordKingdomVisitById(s32 worldId) {
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        if ((sVisitedKingdoms & (1u << worldId)) != 0)
            return;
        setVisitedKingdoms(sVisitedKingdoms | (1u << worldId));
        sVisitOrder[worldId] = static_cast<s8>(sNextVisitSequence < 127 ? sNextVisitSequence : 127);
        sNextVisitSequence++;
    }

    void recordKingdomVisit(const char* stageName) {
        recordKingdomVisitById(findKingdomIdForAnyStage(stageName));
    }

    s8 getKingdomVisitOrder(s32 worldId) {
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return -1;
        return sVisitOrder[worldId];
    }

    void setKingdomVisitOrder(s32 worldId, s8 order) {
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        sVisitOrder[worldId] = order;
        if (order >= 0 && order >= sNextVisitSequence)
            sNextVisitSequence = order + 1;
    }

    void resetKingdomVisitOrder() {
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            sVisitOrder[i] = -1;
        sNextVisitSequence = 0;
    }

    void applyUnlockedKingdomsMapStart() {
        if (sVisitedKingdomsMapMode != VisitedKingdomsMapMode_Unlocked
            && sVisitedKingdomsMapMode != VisitedKingdomsMapMode_Custom)
            return;

        // The starting kingdom is already the first entry of the progression
        // route, so it never needs to be one of the map's visited extras.
        const s32 startWorldId = KingdomRando::getStartingWorldId();
        u32 visited = sVisitedKingdoms;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++) {
            const WorldMeta::Entry& world = WorldMeta::sWorlds[i];
            if (world.worldId == startWorldId)
                continue;
            const bool selected = sVisitedKingdomsMapMode == VisitedKingdomsMapMode_Unlocked
                || (sStartKingdomsMask & (1u << world.worldId)) != 0;
            if (!selected)
                continue;
            visited |= 1u << world.worldId;
            // Rank by scenarioWorldId, which is exactly the vanilla kingdom
            // order (Lake before Wooded, Snow before Seaside) the map extras
            // are sorted by. Ranks already recorded by real visits are
            // deliberately overwritten - this mode always presents the map in
            // vanilla order - while the sequence bump below keeps any later
            // real visit (the starting kingdom's) ranked after all of them.
            sVisitOrder[world.worldId] = static_cast<s8>(world.scenarioWorldId);
            if (world.scenarioWorldId >= sNextVisitSequence)
                sNextVisitSequence = world.scenarioWorldId + 1;
        }
        setVisitedKingdoms(visited);
    }

    static s32 sScenarioOverrideWorldId = -1;
    static s32 sScenarioOverrideValue = -1;
    s32 getScenarioOverrideWorldId() { return sScenarioOverrideWorldId; }
    s32 getScenarioOverrideValue() { return sScenarioOverrideValue; }
    void setScenarioOverride(s32 worldId, s32 scenarioNo) {
        sScenarioOverrideWorldId = worldId;
        sScenarioOverrideValue = scenarioNo;
        logPush("Scenario override set: world=%d scenario=%d", worldId, scenarioNo);
    }
    void clearScenarioOverride() {
        sScenarioOverrideWorldId = -1;
        sScenarioOverrideValue = -1;
        logPush("Scenario override cleared");
    }

    // Starting equipment (fed to logic) + capture/ability duplication difficulty
    // (NOT fed to logic). Stored here so the new-file menu and generator agree;
    // the gameplay wiring (starting kit grant + duplicate placement) is applied by
    // the capture/ability systems that read these getters.
    static StartingEquipment sStartingEquipment = StartingEquipment_Normal;
    static CaptureAbilityDifficulty sCaptureAbilityDifficulty = CaptureAbilityDifficulty_Normal;
    static s32 sStartingCaptureOverride = -1;
    static s32 sStartingThrowOverride = -1;
    static s32 sStartingAbilityOverride = -1;

    static s32 presetStartingCaptureCount(StartingEquipment value) {
        switch (value) {
        case StartingEquipment_Plentiful:
            return 4;
        case StartingEquipment_Normal:
            return 2;
        case StartingEquipment_Scarce:
        default:
            return 0;
        }
    }

    static s32 presetStartingThrowCount(StartingEquipment) { return 1; }

    static s32 presetStartingAbilityCount(StartingEquipment value) {
        switch (value) {
        case StartingEquipment_Plentiful:
            return 2;
        case StartingEquipment_Normal:
            return 1;
        case StartingEquipment_Scarce:
        default:
            return 0;
        }
    }

    static s32 clampStartingCaptureCount(s32 count) {
        return count < 0 ? 0 : (count > 4 ? 4 : count);
    }

    static s32 clampStartingThrowCount(s32 count) {
        return count < 1 ? 1 : (count > 4 ? 4 : count);
    }

    static s32 clampStartingAbilityCount(s32 count) {
        return count < 0 ? 0 : (count > 2 ? 2 : count);
    }

    StartingEquipment getStartingEquipment() { return sStartingEquipment; }
    void setStartingEquipment(StartingEquipment v) {
        if (v <= StartingEquipment_Plentiful) {
            sStartingEquipment = v;
            sStartingCaptureOverride = -1;
            sStartingThrowOverride = -1;
            sStartingAbilityOverride = -1;
        }
    }

    const char* getStartingEquipmentName(StartingEquipment value) {
        switch (value) {
        case StartingEquipment_Scarce:
            return "Scarce";
        case StartingEquipment_Plentiful:
            return "Plentiful";
        case StartingEquipment_Normal:
        default:
            return "Normal";
        }
    }

    s32 getStartingCaptureCount() {
        return sStartingCaptureOverride >= 0
            ? sStartingCaptureOverride
            : presetStartingCaptureCount(sStartingEquipment);
    }

    s32 getStartingThrowCount() {
        return sStartingThrowOverride >= 0
            ? sStartingThrowOverride
            : presetStartingThrowCount(sStartingEquipment);
    }

    s32 getStartingAbilityCount() {
        return sStartingAbilityOverride >= 0
            ? sStartingAbilityOverride
            : presetStartingAbilityCount(sStartingEquipment);
    }

    bool isStartingEquipmentCustom() {
        return sStartingCaptureOverride >= 0
            || sStartingThrowOverride >= 0
            || sStartingAbilityOverride >= 0;
    }

    void setStartingCaptureCount(s32 count) {
        sStartingCaptureOverride = clampStartingCaptureCount(count);
    }

    void setStartingThrowCount(s32 count) {
        sStartingThrowOverride = clampStartingThrowCount(count);
    }

    void setStartingAbilityCount(s32 count) {
        sStartingAbilityOverride = clampStartingAbilityCount(count);
    }

    void setStartingEquipmentOverrides(s32 captures, s32 throws, s32 abilities) {
        sStartingCaptureOverride = captures < 0 ? -1 : clampStartingCaptureCount(captures);
        sStartingThrowOverride = throws < 0 ? -1 : clampStartingThrowCount(throws);
        sStartingAbilityOverride = abilities < 0 ? -1 : clampStartingAbilityCount(abilities);
    }

    CaptureAbilityDifficulty getCaptureAbilityDifficulty() { return sCaptureAbilityDifficulty; }
    void setCaptureAbilityDifficulty(CaptureAbilityDifficulty v) {
        if (v == CaptureAbilityDifficulty_Plentiful)
            v = CaptureAbilityDifficulty_Normal;
        if (v <= CaptureAbilityDifficulty_Normal)
            sCaptureAbilityDifficulty = v;
    }

    const char* getCaptureAbilityDifficultyName(CaptureAbilityDifficulty value) {
        switch (value) {
        case CaptureAbilityDifficulty_Scarce:
            return "Scarce";
        case CaptureAbilityDifficulty_Normal:
        default:
            return "Normal";
        }
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ lifecycle Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

    static void narrateMoonAllocationSummary(u32 seed, u32 attempt) {
        if (!isMoonAllocationActive())
            return;

        // reinit() runs again for stage-scene retries. Use the shared build guard
        // so the 17-row table is emitted once for a seed/attempt pair.
        static u8 sMoonAllocationDiagnosticsTag = 0;
        if (!shouldNarrateBuild(&sMoonAllocationDiagnosticsTag, seed, attempt))
            return;

        logPush("moon allocation table: seed=%u attempt=%u difficulty=%s coins=%s",
            seed, attempt,
            getMoonAllocationDifficultyName(getMoonAllocationDifficulty()),
            getMoonCoinPolicyName(getMoonCoinPolicy()));

        for (s32 kingdom = 0; kingdom < LogicData::sKingdomCount; kingdom++) {
            MoonRando::KingdomAllocationSummary summary = {};
            const char* kingdomName = WorldMeta::getDisplayNameForWorldId(kingdom);
            if (!MoonRando::getKingdomAllocationSummary(kingdom, &summary)) {
                logPush("moon allocation %s: summary unavailable", kingdomName);
                continue;
            }

            const char* profileRole = summary.targetTight
                ? " profile=Target-tight"
                : (summary.concentration ? " profile=Concentration" : "");
            const char* specialName = nullptr;
            switch (summary.specialKind) {
            case MoonRando::AllocationConstraintKind::CaptureSlots:
                specialName = "capture-capacity";
                break;
            case MoonRando::AllocationConstraintKind::PaintingEntries:
                specialName = "painting-entries";
                break;
            case MoonRando::AllocationConstraintKind::AbilitySlotsCombined:
                specialName = "ability-capacity-combined";
                break;
            case MoonRando::AllocationConstraintKind::None:
            default:
                break;
            }

            if (specialName) {
                logPush("moon allocation %s: list-rows=%d moon-value=%d base=%d excess=%d target-value=%d minimum=%d%s %s=%d/%d",
                    kingdomName, summary.assignedEntries, summary.assignedValue,
                    summary.baseRequirement, summary.excessValue,
                    summary.targetValue, summary.minimumValue, profileRole, specialName,
                    summary.specialCapacity, summary.specialMinimum);
            } else {
                logPush("moon allocation %s: list-rows=%d moon-value=%d base=%d excess=%d target-value=%d minimum=%d%s",
                    kingdomName, summary.assignedEntries, summary.assignedValue,
                    summary.baseRequirement, summary.excessValue,
                    summary.targetValue, summary.minimumValue, profileRole);
            }
        }
    }

    static void logMoonAllocationSettings(u32 seed, u32 attempt, bool includeAttempt) {
        if (includeAttempt) {
            logPush("moon settings: seed=%u attempt=%u master=%d randomize=%d active=%d diff=%s coin=%s triple=%d/%d",
                seed, attempt, isMoonReallocationEnabled(),
                isRandomizeMoonAllocationEnabled(), isMoonAllocationActive(),
                getMoonAllocationDifficultyName(getMoonAllocationDifficulty()),
                getMoonCoinPolicyName(getMoonCoinPolicy()),
                isTripleMoonsAnywhereEnabled(), isRandomizeTripleMoonsEnabled());
        } else {
            logPush("moon settings: seed=%u master=%d randomize=%d active=%d diff=%s coin=%s triple=%d/%d",
                seed, isMoonReallocationEnabled(),
                isRandomizeMoonAllocationEnabled(), isMoonAllocationActive(),
                getMoonAllocationDifficultyName(getMoonAllocationDifficulty()),
                getMoonCoinPolicyName(getMoonCoinPolicy()),
                isTripleMoonsAnywhereEnabled(), isRandomizeTripleMoonsEnabled());
        }
    }

    bool reinit() {
        const bool captureRandoEnabled = isCaptureRandoEnabled();
        const bool lockedAbilitiesEnabled = isLockedAbilitiesEnabled();
        const bool logicRandoEnabled = isLogicRandoEnabled();

        SeedGenerator::reportProgress(10, "Building kingdom route");
        if (sKingdomRandoEnabled)
            KingdomRando::rebuild(sSeed);
        else
            KingdomRando::clear();

        // Requirement caps must see the final per-kingdom moon values.  Prepare
        // full allocation before the initial requirement build; MoonRando will
        // deterministically repeat this while building/restoring placements.
        if (isMoonAllocationActive()
            && !MoonRando::prepareKingdomRedistribution(sSeed)) {
            logPush("seed generation rejected while preparing moon allocation: %s",
                MoonRando::getFailureMessage());
            return false;
        }

        SeedGenerator::reportProgress(16, "Calculating requirements");
        RequirementRando::rebuild(sSeed);

        // Always build: with Painting Rando off the table acts as an identity map.
        // The painting hooks must stay active either way, because the QoL patches
        // force warp paintings open pre-endgame and the vanilla game resolves their
        // destinations from post-game save data that does not exist yet (leaving
        // the painting closed, or crashing if forced without a destination).
        SeedGenerator::reportProgress(22, "Shuffling paintings");
        PaintingRando::buildRemapTable(sSeed);

        SeedGenerator::reportProgress(28, "Preparing captures");
        if (captureRandoEnabled)
            CaptureRando::buildRemapTable(sSeed);
        else
            CaptureRando::clear();

            // Deterministic either way: with the option off the table resolves to
            // identity so lookups stay cheap and consistent.
#if SMOR_FEATURE_CHAOS_ANY
        const bool enemySwapSucceeded = EnemySwapRando::buildRemapTable(sSeed);
#else
        const bool enemySwapSucceeded = true;
#endif

        SeedGenerator::reportProgress(34, "Preparing abilities");
        if (lockedAbilitiesEnabled)
            AbilityRando::buildRemapTable(sSeed);
        else
            AbilityRando::clear();

        PurpleCoinRewards::rebuildShopInventories();

        SeedGenerator::reportProgress(40, "Shuffling entrances");
        // With Moon Rando on, MoonRando::buildRemapTable owns the entrance
        // lifecycle: it opens an incremental build per attempt and the fill
        // decides the wiring door by door. Building a whole table here would
        // just be thrown away, and worse, the attempt that actually wins would
        // rebuild it anyway. The one-shot builder is only for the Moon Rando
        // off path, where there is no placement engine to interleave with.
        if (!isEntranceRandoEnabled())
            EntranceRando::reset();
        else if (!sMoonRandoEnabled)
            EntranceRando::buildRemapTable(sSeed);

        // Only for the Moon Rando off path, which never enters MoonRando's
        // per-attempt dependency loop. With Moon Rando on, buildRemapTable below
        // rebuilds this with the attempt that actually succeeded - doing it here
        // too would derive it twice per stage-scene init (reinit() runs on every
        // one), and on a seed that needed attempt > 0 the two calls would use
        // different attempt numbers and defeat the cache outright.
        if (!sMoonRandoEnabled)
            StartPositionRando::rebuild(sSeed, 0);

        SeedGenerator::reportProgress(46, "Placing moons");
        bool generationSucceeded = true;
        if (sMoonRandoEnabled) {
            generationSucceeded = MoonRando::buildRemapTable(sSeed);
        } else {
            MoonRando::clear();
            generationSucceeded = enemySwapSucceeded;
        }

        if (!generationSucceeded) {
            if (!sMoonRandoEnabled && !enemySwapSucceeded) {
                const EnemySwapRando::FailureCode failure = EnemySwapRando::getFailureCode();
                logPush(
                    "seed generation rejected: unsatisfied "
                    "enemy/capture placement constraints (code=%d)",
                    static_cast<s32>(failure));
                if (failure == EnemySwapRando::FailureCode::MemoryBudgetExceeded) {
                    const char* stage = nullptr;
                    s16 scenario = -1;
                    u32 extraResource = 0;
                    u32 maxResource = 0;
                    u32 extraScene = 0;
                    u32 maxScene = 0;
                    EnemySwapRando::getMemoryFailure(
                        &stage, &scenario, &extraResource, &maxResource,
                        &extraScene, &maxScene);
                    logPush(
                        "seed generation memory rejection: stage=%s scenario=%d "
                        "resource=%u/%u scene=%u/%u",
                        stage ? stage : "?", scenario, extraResource,
                        maxResource, extraScene, maxScene);
                }
                return false;
            }
            // The one case anybody needs to debug used to leave nothing on
            // the SD card: this path returns before the SpoilerLog::generate
            // call below, and generate() would bail on !isInitialized()
            // anyway. Write the settings + attempt table + failure message.
            SpoilerLog::generateFailureReport();
            logPush("seed generation rejected: code=%d message=%s",
                static_cast<s32>(MoonRando::getFailureCode()),
                MoonRando::getFailureMessage());
            return false;
        }

        const u32 generationAttempt = MoonRando::getLastEntranceAttempt();
        logMoonAllocationSettings(sSeed, generationAttempt, false);
        narrateMoonAllocationSummary(sSeed, generationAttempt);

        SeedGenerator::reportProgress(82, "Applying cosmetic tables");
        if (sSkyboxRandoMode != SkyboxRandoMode_None)
            SkyboxRando::reinit();
        else
            SkyboxRando::clear();

        if (isMusicRandoEnabled())
            MusicRando::buildRemapTable(sSeed);
        else
            MusicRando::clear();

        SphinxRando::clear();

        SeedGenerator::reportProgress(88, "Writing spoiler data");
        if (sMoonRandoEnabled)
            SpoilerLog::generate();

        logPush("seed=%u moonReq=%u difficulty=%s logicDifficulty=%s pacing=%s open=%s seedDiff=%s mkReq=%d peaceReq=%d cloudPaint=%d moonRando=%d allocMaster=%d allocRandomize=%d allocDifficulty=%s coinPolicy=%s entrance=%s painting=%d kingdom=%d logic=%d sky=%s capture=%d music=%s overlay=%d hintTier=%d sphinx=%d locked=%d startEquip=%s itemScarcity=%s purple=A%d C%d M%d P%s",
            sSeed,
            getMoonRequirement(),
            getMoonDifficultyName(sMoonDifficulty),
            getLogicDifficultyName(sLogicDifficulty),
            getUnlockPacingName(getResolvedUnlockPacing()),
            getSeedOpennessName(getResolvedSeedOpenness()),
            getSeedDifficultyName(getResolvedSeedDifficulty()),
            isMoonKingdomRequirementsEnabled(),
            static_cast<s32>(sWorldPeaceRequirementMode),
            isCloudKingdomMoonsUnlockPaintingsEnabled(),
            sMoonRandoEnabled,
            sMoonReallocationEnabled,
            sRandomizeMoonAllocationEnabled,
            getMoonAllocationDifficultyName(sMoonAllocationDifficulty),
            getMoonCoinPolicyName(sMoonCoinPolicy),
            getCurrentEntranceRandoModeName(),
            sPaintingRandoEnabled,
            sKingdomRandoEnabled,
            logicRandoEnabled,
            getSkyboxRandoModeName(sSkyboxRandoMode),
            captureRandoEnabled,
            getMusicRandoModeName(sMusicRandoMode), sMusicOverlayEnabled,
            static_cast<s32>(getHintPriceTier()),
            sSphinxRandoEnabled,
            lockedAbilitiesEnabled,
            getStartingEquipmentName(sStartingEquipment),
            getCaptureAbilityDifficultyName(sCaptureAbilityDifficulty),
            sPurpleShopAbilityCount,
            sPurpleShopCaptureCount,
            sPurpleShopMoonsEnabled,
            getPurpleShopPriceName(sPurpleShopPrice));
        return true;
    }

    bool reinitDeterministic(u32 attempt) {
        const bool captureRandoEnabled = isCaptureRandoEnabled();
        const bool lockedAbilitiesEnabled = isLockedAbilitiesEnabled();
        const bool logicRandoEnabled = isLogicRandoEnabled();

        logMoonAllocationSettings(sSeed, attempt, true);

        if (sKingdomRandoEnabled)
            KingdomRando::rebuild(sSeed, attempt);
        else
            KingdomRando::clear();

        // The moon->kingdom redistribution (Randomize Triple Moons) must be in place
        // before requirements and captures/abilities are assigned. Requirements
        // use each redistributed kingdom's credited value as their hard maximum,
        // while roles ride on the reshuffled pool (a moon reassigned to Mushroom
        // is what earns a capture).
        // On a fresh run buildRemapTable does this internally; here (save restore)
        // restorePlacements only computes it afterwards, which would leave restored
        // captures bound to the home kingdoms while everything else uses the assigned
        // ones -- so a Mushroom-looking moon would grant no capture. Compute it first.
        // Gated on moon rando, since the redistribution is only realized by placement.
        if (isMoonRandoEnabled()
            && !MoonRando::prepareKingdomRedistribution(sSeed)) {
            logPush("deterministic restore failed: moon allocation: %s",
                MoonRando::getFailureMessage());
            return false;
        }

        RequirementRando::rebuild(sSeed, attempt);
        PaintingRando::buildRemapTable(sSeed, attempt);

        if (captureRandoEnabled)
            CaptureRando::buildRemapTable(sSeed, attempt);
        else
            CaptureRando::clear();

#if SMOR_FEATURE_CHAOS_ANY
        if (!EnemySwapRando::buildRemapTable(sSeed, attempt)) {
            logPush(
                "deterministic restore failed: unsatisfied "
                "enemy/capture constraints attempt=%u code=%d",
                attempt,
                static_cast<s32>(
                    EnemySwapRando::getFailureCode()));
            if (EnemySwapRando::getFailureCode()
                == EnemySwapRando::FailureCode::MemoryBudgetExceeded) {
                const char* stage = nullptr;
                s16 scenario = -1;
                u32 extraResource = 0;
                u32 maxResource = 0;
                u32 extraScene = 0;
                u32 maxScene = 0;
                EnemySwapRando::getMemoryFailure(
                    &stage, &scenario, &extraResource, &maxResource,
                    &extraScene, &maxScene);
                logPush(
                    "deterministic restore memory rejection: stage=%s "
                    "scenario=%d resource=%u/%u scene=%u/%u",
                    stage ? stage : "?", scenario, extraResource,
                    maxResource, extraScene, maxScene);
            }
        }
#endif

        if (lockedAbilitiesEnabled)
            AbilityRando::buildRemapTable(sSeed, attempt);
        else
            AbilityRando::clear();

        PurpleCoinRewards::rebuildShopInventories();

        if (sSkyboxRandoMode != SkyboxRandoMode_None)
            SkyboxRando::reinit();
        else
            SkyboxRando::clear();

        if (isMusicRandoEnabled())
            MusicRando::buildRemapTable(sSeed);
        else
            MusicRando::clear();

        SphinxRando::clear();

        narrateMoonAllocationSummary(sSeed, attempt);

        logPush("deterministic restore deps: seed=%u attempt=%u moonReq=%u difficulty=%s logicDifficulty=%s pacing=%s open=%s seedDiff=%s moonRando=%d allocMaster=%d allocRandomize=%d allocDifficulty=%s coinPolicy=%s entrance=%s painting=%d kingdom=%d logic=%d sky=%s capture=%d music=%s overlay=%d hintTier=%d sphinx=%d locked=%d startEquip=%s itemScarcity=%s purple=A%d C%d M%d P%s",
            sSeed,
            attempt,
            getMoonRequirement(),
            getMoonDifficultyName(sMoonDifficulty),
            getLogicDifficultyName(sLogicDifficulty),
            getUnlockPacingName(getResolvedUnlockPacing()),
            getSeedOpennessName(getResolvedSeedOpenness()),
            getSeedDifficultyName(getResolvedSeedDifficulty()),
            sMoonRandoEnabled,
            sMoonReallocationEnabled,
            sRandomizeMoonAllocationEnabled,
            getMoonAllocationDifficultyName(sMoonAllocationDifficulty),
            getMoonCoinPolicyName(sMoonCoinPolicy),
            getCurrentEntranceRandoModeName(),
            sPaintingRandoEnabled,
            sKingdomRandoEnabled,
            logicRandoEnabled,
            getSkyboxRandoModeName(sSkyboxRandoMode),
            captureRandoEnabled,
            getMusicRandoModeName(sMusicRandoMode), sMusicOverlayEnabled,
            static_cast<s32>(getHintPriceTier()),
            sSphinxRandoEnabled,
            lockedAbilitiesEnabled,
            getStartingEquipmentName(sStartingEquipment),
            getCaptureAbilityDifficultyName(sCaptureAbilityDifficulty),
            sPurpleShopAbilityCount,
            sPurpleShopCaptureCount,
            sPurpleShopMoonsEnabled,
            getPurpleShopPriceName(sPurpleShopPrice));
        return true;
    }

    // Ã¢â€â‚¬Ã¢â€â‚¬ log Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬Ã¢â€â‚¬

    static char sLog[kLogMax][128] = {};
    static s32 sLogHead = 0;
    static s32 sLogCount = 0;
    static u32 sLogSequence = 0;

    bool shouldNarrateBuild(const void* tag, u32 seed, u32 attempt) {
        // One shared FIFO across every call site. Sized for a handful of tags
        // times the two or three attempts a session actually rebuilds; the
        // oldest entry is evicted, so the worst case is one repeated narration
        // rather than unbounded growth.
        static constexpr s32 cCapacity = 32;
        struct Entry {
            const void* tag;
            u32 seed;
            u32 attempt;
        };
        static Entry sEntries[cCapacity] = {};
        static s32 sCount = 0;
        static s32 sNext = 0;

        for (s32 i = 0; i < sCount; i++) {
            if (sEntries[i].tag == tag && sEntries[i].seed == seed
                && sEntries[i].attempt == attempt)
                return false;
        }

        sEntries[sNext] = { tag, seed, attempt };
        sNext = (sNext + 1) % cCapacity;
        if (sCount < cCapacity)
            sCount++;
        return true;
    }

    void logPush(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(sLog[sLogHead], 128, fmt, args);
        va_end(args);

        char output[160];
        const u32 sequence = ++sLogSequence;
        s32 outputLen = nn::util::SNPrintf(
            output, sizeof(output), "[SMOR #%06u] %s\n",
            sequence, sLog[sLogHead]);
        if (outputLen > 0) {
            if (outputLen >= (s32)sizeof(output))
                outputLen = (s32)sizeof(output) - 1;
            hk::diag::hkLogSink(output, (size)outputLen);
        }

        sLogHead = (sLogHead + 1) % kLogMax;
        if (sLogCount < kLogMax)
            sLogCount++;
    }

    const char* logGet(s32 index) {
        return sLog[(sLogHead - sLogCount + index + kLogMax) % kLogMax];
    }

    s32 logCount() { return sLogCount; }

} // namespace Randomizer
