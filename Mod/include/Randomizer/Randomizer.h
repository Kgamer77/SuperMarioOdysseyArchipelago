#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/WorldMeta.h"

namespace Randomizer {

    enum class RunMode : u8
    {
        Standard = 0,
        Bingo = 1,
        Ascend = 2,
        // The two-bit run-mode field reserves its final value for Toadette Medal Hunt.
        ToadetteMedals = 3,
        // These append after the legacy modes.  The high bit is persisted in a
        // previously unused Meta bit so old slots remain readable.
        Nuzlocke = 4,
        SoftNuzlocke = 5,
        // Appended after the Nuzlocke pair.  Meta::reserved[2] already carries a
        // three-bit run-mode field, so 6 needs no save-format change.
        Timer = 6,
    };

    RunMode getRunMode();
    void setRunMode(RunMode mode);
    const char* getRunModeName(RunMode mode);
    bool isBoardRunMode();
    bool isNuzlockeRunMode();
    bool isSoftNuzlockeRunMode();
    bool isNuzlockeModesFeatureEnabled();
    // Extreme Nuzlocke keeps Mario at one effective hit point.  This is a
    // run-mode setting rather than a separate RunMode value, so it can be
    // combined with either hard or Soft Nuzlocke.
    bool getExtremeNuzlockeEnabled();
    void setExtremeNuzlockeEnabled(bool enabled);
    bool isTimerRunMode();
    bool isTimerModeFeatureEnabled();

    // Seed-owned multiplayer behavior. Stored in rando.bin v11; old saves
    // explicitly load it as disabled.
    bool isDeathLinkEnabled();
    void setDeathLinkEnabled(bool enabled);

    // Whether opposing teams can see each other's players. Seed-owned like
    // death link, because it becomes one of the ROOM's flags: the host sets it
    // on the file select and the server mirrors it to everyone.
    bool isSyncOtherTeamsEnabled();
    void setSyncOtherTeamsEnabled(bool enabled);

    // Which team this player joins with. 0-based, so 0 is "Team 1".
    //
    // Deliberately NOT in rando.bin, unlike the setting above: rando.bin IS the
    // room snapshot, so a team stored there would be handed to every guest that
    // downloads the host's seed and would put the whole room on the host's
    // team. It lives in the per-console GlobalConfig instead, which is also
    // what makes the last pick stick between sessions.
    u8 getSelectedTeam();
    void setSelectedTeam(u8 team);

    // ── game mode settings ────────────────────────────────────────────────────────
    // Everything on the run-mode-specific second settings page. These are seed
    // settings (chosen before generation, persisted per-slot), not live toggles.

    // Shared sentinel for selectors whose normal roll table can be replaced by
    // a numeric value. It is intentionally negative so it cannot collide with
    // any existing table index.
    static constexpr s32 kCustomIdx = -1;

    // Share of the collected moons a Soft Nuzlocke death surrenders. Half is the
    // original behavior and stays the default.
    enum class SoftNuzlockeLoss : u8
    {
        Quarter = 0,
        Half = 1,
        ThreeQuarters = 2,
        Custom = 0xff,
    };

    SoftNuzlockeLoss getSoftNuzlockeLoss();
    void setSoftNuzlockeLoss(SoftNuzlockeLoss loss);
    const char* getSoftNuzlockeLossName(SoftNuzlockeLoss loss);
    // Percentage form, for the penalty itself (25 / 50 / 75).
    s32 getSoftNuzlockeLossPercent();
    bool isSoftNuzlockeLossCustom();
    s32 getSoftNuzlockeLossCustomPercent();
    void setSoftNuzlockeLossCustomPercent(s32 percent);

    // Captures and abilities are surrendered on their own dials rather than on
    // the moon one.  They are the part of a run that decides what is reachable,
    // so a table that reads well for moons does not necessarily read well for
    // them - and either can now be turned off entirely, or taken in full.
    enum class SoftNuzlockeEquipLoss : u8
    {
        None = 0,
        Quarter = 1,
        Half = 2,
        ThreeQuarters = 3,
        All = 4,
        Custom = 0xff,
    };

    SoftNuzlockeEquipLoss getSoftNuzlockeAbilityLoss();
    void setSoftNuzlockeAbilityLoss(SoftNuzlockeEquipLoss loss);
    SoftNuzlockeEquipLoss getSoftNuzlockeCaptureLoss();
    void setSoftNuzlockeCaptureLoss(SoftNuzlockeEquipLoss loss);
    // Percentage form (0 / 25 / 50 / 75 / 100).
    s32 getSoftNuzlockeEquipLossPercent(SoftNuzlockeEquipLoss loss);
    s32 getSoftNuzlockeAbilityLossPercent();
    s32 getSoftNuzlockeCaptureLossPercent();
    bool isSoftNuzlockeAbilityLossCustom();
    bool isSoftNuzlockeCaptureLossCustom();
    s32 getSoftNuzlockeAbilityLossCustomPercent();
    s32 getSoftNuzlockeCaptureLossCustomPercent();
    void setSoftNuzlockeAbilityLossCustomPercent(s32 percent);
    void setSoftNuzlockeCaptureLossCustomPercent(s32 percent);

    // Timer Mode: the clock the run starts with, and what a pickup adds to it.
    // All three are stored as indices into fixed tables (see Randomizer.cpp),
    // matching the roll-setting convention used by the other selectors.
    static constexpr s32 kTimerStartCount = 4; // 1 / 2 / 3 / 5 minutes
    static constexpr s32 kTimerMoonBonusCount = 4; // 20 / 30 / 45 / 60 seconds
    static constexpr s32 kTimerTripleBonusCount = 5; // 30 / 45 / 60 / 90 / 120 seconds
    static constexpr s32 kTimerStartDefaultIdx = kTimerStartCount / 2;
    static constexpr s32 kTimerMoonBonusDefaultIdx = kTimerMoonBonusCount / 2;
    static constexpr s32 kTimerTripleBonusDefaultIdx = kTimerTripleBonusCount / 2;

    s32 getTimerStartIdx();
    void setTimerStartIdx(s32 idx);
    s32 getTimerMoonBonusIdx();
    void setTimerMoonBonusIdx(s32 idx);
    s32 getTimerTripleBonusIdx();
    void setTimerTripleBonusIdx(s32 idx);

    // Timer Mode penalties: what the clock loses when Mario is hurt or killed.
    // Index 0 is "Off" on both, so a save written before these existed - and a
    // player who never touches them - runs exactly as the mode always has.
    static constexpr s32 kTimerDamagePenaltyCount = 4; // Off / 1 / 5 / 15 seconds
    static constexpr s32 kTimerDeathPenaltyCount = 4; // Off / 15 / 30 / 45 seconds

    s32 getTimerDamagePenaltyIdx();
    void setTimerDamagePenaltyIdx(s32 idx);
    s32 getTimerDeathPenaltyIdx();
    void setTimerDeathPenaltyIdx(s32 idx);

    // Timer Mode award decay: how long a run has to last for a moon to be worth
    // half of what it paid at the start.  The decay is exponential, so twice the
    // half-life is a quarter, three times is an eighth, and it never reaches
    // zero.  Index 0 is "Disabled" for the same reason the penalties above start
    // there - the mode keeps paying its flat bonuses until a player asks for it.
    static constexpr s32 kTimerHalfLifeCount = 4; // Disabled / 1 / 1.5 / 2 hours

    s32 getTimerHalfLifeIdx();
    void setTimerHalfLifeIdx(s32 idx);

    // Resolved values in seconds. A triple moon awards the triple bonus INSTEAD
    // of the per-moon bonus, not on top of it.
    s32 getTimerStartSeconds();
    s32 getTimerMoonBonusSeconds();
    s32 getTimerTripleBonusSeconds();
    bool isTimerStartCustom();
    bool isTimerMoonBonusCustom();
    bool isTimerTripleBonusCustom();
    s32 getTimerStartCustomSeconds();
    s32 getTimerMoonBonusCustomSeconds();
    s32 getTimerTripleBonusCustomSeconds();
    void setTimerStartCustomSeconds(s32 seconds);
    void setTimerMoonBonusCustomSeconds(s32 seconds);
    void setTimerTripleBonusCustomSeconds(s32 seconds);
    // Both are 0 when the matching penalty is switched off.
    s32 getTimerDamagePenaltySeconds();
    s32 getTimerDeathPenaltySeconds();
    bool isTimerDamagePenaltyCustom();
    bool isTimerDeathPenaltyCustom();
    s32 getTimerDamagePenaltyCustomSeconds();
    s32 getTimerDeathPenaltyCustomSeconds();
    void setTimerDamagePenaltyCustomSeconds(s32 seconds);
    void setTimerDeathPenaltyCustomSeconds(s32 seconds);
    // 0 when the decay is switched off.  Measured in the same clock time the
    // countdown itself spends, so cutscenes and the pause menu do not age a run.
    s32 getTimerHalfLifeSeconds();
    bool isTimerHalfLifeCustom();
    s32 getTimerHalfLifeCustomSeconds();
    void setTimerHalfLifeCustomSeconds(s32 seconds);

    // ── constants ─────────────────────────────────────────────────────────────────

    // Temporarily large while authoritative kingdom progression is being traced.
    static constexpr s32 kLogMax = 1000;

    // ── seed ──────────────────────────────────────────────────────────────────────

    u32 getSeed();
    void setSeed(u32 seed);

    // ── moon requirement ──────────────────────────────────────────────────────────

    enum MoonDifficulty : u8
    {
        MoonDifficulty_Easy = 0,
        MoonDifficulty_Medium = 1,
        MoonDifficulty_Hard = 2,
        // 3 is the legacy "Insanity" save value; it stays the fully unbounded mode.
        MoonDifficulty_Max = 3,
        MoonDifficulty_Harder = 4,
        MoonDifficulty_Hardest = 5,
        MoonDifficulty_Insane = 6,
    };

    enum MoonRequirement : u8
    {
        MoonRequirement_64 = 0,
        MoonRequirement_128 = 1, // legacy save value only; remapped to 124 on set.
        MoonRequirement_256 = 2,
        MoonRequirement_124 = 3, // appended to keep saved values 0/1/2 stable.
    };

    enum LogicDifficulty : u8
    {
        LogicDifficulty_Casual = 0,
        LogicDifficulty_Advanced = 1,
        LogicDifficulty_Expert = 2,
        // Cumulative: Glitched allows every Expert method plus glitch-only route
        // options. Kept index-parallel with LogicData::RouteDifficulty.
        LogicDifficulty_Glitched = 3,
    };

    // Retired as a user-facing setting: Seed Style is now three independent
    // axes (below). The enum survives only so legacy save/config/preset values
    // can be migrated - see applyLegacySeedStyle().
    enum SeedStyle : u8
    {
        SeedStyle_Standard = 0,
        SeedStyle_AbilityRush = 1,
        SeedStyle_FocusedRoute = 2,
        SeedStyle_Explorer = 3,
        SeedStyle_Mystery = 4,
    };

    // The three placement axes change pacing, spread and depth WITHOUT changing
    // which movement tricks Logic Difficulty permits. Index 2 on every axis is
    // Standard and reproduces the historical buffer/sphere behavior exactly.
    // Index 5 (Random) resolves from the seed alone and consumes no generator
    // RNG, so it stays reproducible on reload.

    // How early captures/abilities/cloud paintings enter the critical path.
    enum UnlockPacing : u8
    {
        UnlockPacing_FrontLoaded = 0,
        UnlockPacing_Early = 1,
        UnlockPacing_Standard = 2,
        UnlockPacing_Late = 3,
        UnlockPacing_BackLoaded = 4,
        UnlockPacing_Random = 5,
    };

    // How many simultaneous options the player has: buffer width, chain length
    // and which kind of door the entrance fill prefers to wire.
    enum SeedOpenness : u8
    {
        SeedOpenness_Claustrophobic = 0,
        SeedOpenness_Compact = 1,
        SeedOpenness_Standard = 2,
        SeedOpenness_Open = 3,
        SeedOpenness_WideOpen = 4,
        SeedOpenness_Random = 5,
    };

    // How long and how demanding the completion path is: sphere depth, how far
    // an unlock sits from the gate it opens, and how much of the allowed harder
    // movement the fill actually requires (this absorbed the old
    // "Prioritize Difficult Movement" checkbox).
    enum SeedDifficulty : u8
    {
        SeedDifficulty_Relaxed = 0,
        SeedDifficulty_Easy = 1,
        SeedDifficulty_Standard = 2,
        SeedDifficulty_Hard = 3,
        SeedDifficulty_Brutal = 4,
        SeedDifficulty_Random = 5,
    };

    // Every axis has exactly this many selectable entries (5 levels + Random).
    static constexpr u8 cSeedAxisOptionCount = 6;
    // ...of which this many are concrete levels; Random resolves into [0, 5).
    static constexpr u8 cSeedAxisLevelCount = 5;

    MoonDifficulty getMoonDifficulty();
    void setMoonDifficulty(MoonDifficulty difficulty);
    void cycleMoonDifficulty();
    const char* getMoonDifficultyName(MoonDifficulty difficulty);
    const char* getMoonDifficultyLabel(MoonDifficulty difficulty);
    const char* getCurrentMoonDifficultyLabel();

    LogicDifficulty getLogicDifficulty();
    void setLogicDifficulty(LogicDifficulty difficulty);
    void cycleLogicDifficulty();
    const char* getLogicDifficultyName(LogicDifficulty difficulty);
    const char* getLogicDifficultyLabel(LogicDifficulty difficulty);
    const char* getCurrentLogicDifficultyLabel();
    const char* getCurrentLogicModeLabel();
    void cycleLogicMode();

    UnlockPacing getUnlockPacing();
    void setUnlockPacing(UnlockPacing pacing);
    const char* getUnlockPacingName(UnlockPacing pacing);

    SeedOpenness getSeedOpenness();
    void setSeedOpenness(SeedOpenness openness);
    const char* getSeedOpennessName(SeedOpenness openness);

    SeedDifficulty getSeedDifficulty();
    void setSeedDifficulty(SeedDifficulty difficulty);
    const char* getSeedDifficultyName(SeedDifficulty difficulty);

    // The value the generator actually uses: identical to the stored setting
    // unless it is Random, which resolves from the seed alone (pure function,
    // no generator RNG consumed, stable across reloads).
    UnlockPacing getResolvedUnlockPacing();
    SeedOpenness getResolvedSeedOpenness();
    SeedDifficulty getResolvedSeedDifficulty();

    // Convenience for the many placement tables keyed on an axis: the resolved
    // level as a plain 0..4 index.
    s32 getResolvedUnlockPacingLevel();
    s32 getResolvedSeedOpennessLevel();
    s32 getResolvedSeedDifficultyLevel();

    // Compact representation used by both Common config and per-slot saves.
    // Bit 15 marks the three-axis format; a value without it is a legacy
    // SeedStyle index and is migrated by setSeedAxesPacked().
    static constexpr u16 cSeedAxisPackedTag = 0x8000;
    u16 getSeedAxesPacked();
    void setSeedAxesPacked(u16 packed);

    // Migrates a legacy SeedStyle into the three axes. Exposed so the save,
    // Common config and user-preset readers all share one mapping.
    void applyLegacySeedStyle(SeedStyle style);
    // Legacy "Prioritize Difficult Movement" was a boolean that behaved exactly
    // like SeedDifficulty_Hard. Only meaningful while reading an old record.
    void applyLegacyPrioritizeDifficultMovement(bool enabled);

    MoonRequirement getMoonRequirementLevel();
    void setMoonRequirementLevel(MoonRequirement requirement);
    void cycleMoonRequirement();
    u32 getMoonRequirementTotal(MoonRequirement requirement);
    const char* getMoonRequirementLabel(MoonRequirement requirement);
    const char* getCurrentMoonRequirementLabel();
    u32 getMoonRequirement();
    void setMoonRequirement(u32 req);

    enum SpecialMoonUse : u8
    {
        SpecialMoonUse_Coins = 0,
        // Kept at its old value so existing saves retain their exclusive
        // ability/capture distribution after the selector was split.
        SpecialMoonUse_AbilitiesOrCaptures = 1,
        SpecialMoonUse_KingdomUnlock = 2,
        SpecialMoonUse_PaintingUnlock = 3,
        SpecialMoonUse_Abilities = 4,
        SpecialMoonUse_Captures = 5,
        // Appended: values above are persisted by existing config/save data.
        SpecialMoonUse_HintTokens = 6,
    };

    static_assert(SpecialMoonUse_Coins == 0, "Special moon use values are serialized");
    static_assert(SpecialMoonUse_AbilitiesOrCaptures == 1, "Special moon use values are serialized");
    static_assert(SpecialMoonUse_Captures == 5, "Special moon use values are serialized");
    static_assert(SpecialMoonUse_HintTokens == 6, "Hint Tokens must append to special moon uses");

    // What a kingdom moons hand out, one mask per kingdom. A mask that would
    // end up empty falls back to that kingdom default rather than to nothing.
    u8 getKingdomMoonUseMask(s32 worldId);
    void setKingdomMoonUseMask(s32 worldId, u8 mask);
    bool isKingdomMoonUseSelected(s32 worldId, SpecialMoonUse use);
    // Lowest set bit - the compatibility view for callers that still want one
    // value rather than the whole set.
    SpecialMoonUse getKingdomMoonUse(s32 worldId);
    void setKingdomMoonUse(s32 worldId, SpecialMoonUse use);

    // Does this kingdom host a given reward? "Captures or Abilities" counts for
    // both of the first two. Every module that picks the moons carrying a
    // reward asks through here, so the Kingdom Moons page moves all of them
    // together instead of each one keeping its own list of kingdoms.
    // What the kingdom hands out before anyone changes it: Hint Tokens for Cap,
    // Painting Unlock for Cloud, Captures for Mushroom, Abilities for the two
    // Side kingdoms, Progress everywhere else. This is what Reset Defaults on
    // the advanced page writes.
    u8 getDefaultKingdomMoonUseMask(s32 worldId);
    // Display name for the kingdom's lowest selected reward, or "None" when it
    // has none selected at all.
    const char* getKingdomMoonUseName(s32 worldId);

    bool isCaptureRewardKingdom(s32 worldId);
    bool isAbilityRewardKingdom(s32 worldId);
    bool isPaintingRewardKingdom(s32 worldId);
    bool isHintTokenRewardKingdom(s32 worldId);

    // The six kingdoms that had named accessors before every kingdom did.
    // These forward to the generic pair above.
    SpecialMoonUse getCapKingdomMoonUse();
    void setCapKingdomMoonUse(SpecialMoonUse use);
    u8 getCapKingdomMoonUseMask();
    void setCapKingdomMoonUseMask(u8 mask);
    bool isCapKingdomMoonUseSelected(SpecialMoonUse use);
    SpecialMoonUse getCloudKingdomMoonUse();
    void setCloudKingdomMoonUse(SpecialMoonUse use);
    u8 getCloudKingdomMoonUseMask();
    void setCloudKingdomMoonUseMask(u8 mask);
    bool isCloudKingdomMoonUseSelected(SpecialMoonUse use);
    SpecialMoonUse getMoonKingdomMoonUse();
    void setMoonKingdomMoonUse(SpecialMoonUse use);
    u8 getMoonKingdomMoonUseMask();
    void setMoonKingdomMoonUseMask(u8 mask);
    bool isMoonKingdomMoonUseSelected(SpecialMoonUse use);
    SpecialMoonUse getDarkKingdomMoonUse();
    void setDarkKingdomMoonUse(SpecialMoonUse use);
    u8 getDarkKingdomMoonUseMask();
    void setDarkKingdomMoonUseMask(u8 mask);
    bool isDarkKingdomMoonUseSelected(SpecialMoonUse use);
    SpecialMoonUse getDarkerKingdomMoonUse();
    void setDarkerKingdomMoonUse(SpecialMoonUse use);
    u8 getDarkerKingdomMoonUseMask();
    void setDarkerKingdomMoonUseMask(u8 mask);
    bool isDarkerKingdomMoonUseSelected(SpecialMoonUse use);
    SpecialMoonUse getMushroomKingdomMoonUse();
    void setMushroomKingdomMoonUse(SpecialMoonUse use);
    u8 getMushroomKingdomMoonUseMask();
    void setMushroomKingdomMoonUseMask(u8 mask);
    bool isMushroomKingdomMoonUseSelected(SpecialMoonUse use);
    const char* getSpecialMoonUseName(SpecialMoonUse use);

    // The hint-shop currency is intentionally derived from the selected special
    // moon rewards.  It is not a separate serialized setting: selecting Hint
    // Tokens for any eligible kingdom switches the entire shop to tokens.
    enum class HintCurrencyMode : u8
    {
        Coins = 0,
        HintTokens = 1,
    };

    HintCurrencyMode getHintCurrencyMode();
    bool isHintTokenMode();
    const char* getHintCurrencyModeName(HintCurrencyMode mode);

    // Win Condition: how the game is actually won. DefeatBowser is the vanilla
    // chapel-door -> Bowser fight flow, unchanged. ToadetteMedals replaces it with
    // an alternate goal: after collecting enough randomly-designated "Toadette
    // Medal" moons (see MoonRando), Cappy must strike the Castle Interior
    // tombstone to warp to the final Bowser encounter. ToadetteMedalsAndDefeat is
    // a hybrid: medals additionally gate the chapel door, then Bowser must still
    // be defeated as normal.
    enum BowserWinCondition : u8
    {
        BowserWinCondition_DefeatBowser = 0,
        BowserWinCondition_ToadetteMedals = 1,
        BowserWinCondition_ToadetteMedalsAndDefeat = 2,
    };

    BowserWinCondition getBowserWinCondition();
    void setBowserWinCondition(BowserWinCondition condition);
    const char* getBowserWinConditionName(BowserWinCondition condition);
    // Convenience: any mode other than DefeatBowser designates/tracks Toadette Medals.
    bool isToadetteMedalModeEnabled();

    // Toadette Medal ratio: how many medals are required out of how many exist in
    // the pool. Stored as an index into a fixed required/total table (see
    // Randomizer.cpp) rather than the raw values, matching the roll-setting
    // convention used elsewhere (e.g. MoonRequirement/HintPriceTier).
    struct ToadetteMedalRatio {
        u8 required;
        u8 total;
    };

    static constexpr s32 kToadetteMedalRatioCount = 11;

    s32 getToadetteMedalRatioIdx();
    void setToadetteMedalRatioIdx(s32 idx);
    ToadetteMedalRatio getToadetteMedalRatio();

    // "Custom Toadette Medal Counts" replaces the fixed ratio table with two
    // hand-entered values. getToadetteMedalRatio() is the single funnel every
    // consumer already reads, so it simply returns the custom pair while this is
    // on and the ratio roll is disabled in the menu.
    static constexpr s32 kToadetteMedalCustomMax = 255;

    bool isCustomToadetteMedalCountsEnabled();
    void setCustomToadetteMedalCountsEnabled(bool enabled);
    s32 getCustomToadetteMedalAvailable();
    void setCustomToadetteMedalAvailable(s32 count);
    s32 getCustomToadetteMedalRequired();
    void setCustomToadetteMedalRequired(s32 count);

    // Compatibility helpers retained for old save flags and existing gameplay
    // call sites. These now reflect the corresponding selector value.
    bool isMoonKingdomRequirementsEnabled();
    void setMoonKingdomRequirementsEnabled(bool enabled);
    bool isCloudKingdomMoonsUnlockPaintingsEnabled();
    void setCloudKingdomMoonsUnlockPaintingsEnabled(bool enabled);

    // How strictly "world peace" (every route kingdom reaching its peace
    // scenario) is enforced. StoryProgression: no special gate, the chapel
    // door only needs the normal moon requirement. OnKingdomComplete: the
    // chapel door still needs every kingdom at peace, but leaving a kingdom
    // for a different one automatically advances it there (see
    // ScenarioPatcher::calcActualScenario's forced-peace floor), so it's
    // satisfied by ordinary play rather than finishing every story. Required:
    // today's original behavior — every kingdom's story must be finished
    // manually; the logic solver must also prove this is achievable.
    enum WorldPeaceRequirementMode : u8
    {
        WorldPeaceRequirementMode_StoryProgression = 0,
        WorldPeaceRequirementMode_OnKingdomComplete = 1,
        WorldPeaceRequirementMode_Required = 2,
    };

    WorldPeaceRequirementMode getWorldPeaceRequirementMode();
    void setWorldPeaceRequirementMode(WorldPeaceRequirementMode mode);
    const char* getWorldPeaceRequirementModeName(WorldPeaceRequirementMode mode);
    const char* getCurrentWorldPeaceRequirementModeName();

    // Compatibility helpers retained for old save flags and existing gameplay
    // call sites that only care about the strict "Required" behavior. The
    // setter can only select StoryProgression or Required — never
    // OnKingdomComplete, which older callers don't know about.
    bool isWorldPeaceRequirementEnabled();
    void setWorldPeaceRequirementEnabled(bool enabled);

    // Per-kingdom: has this kingdom been left for a different one while
    // WorldPeaceRequirementMode_OnKingdomComplete was active? Consulted by
    // ScenarioPatcher::calcActualScenario to floor the kingdom's scenario at
    // its post-game value. Persisted per-save (RandoSaveFile::Meta).
    u32 getForcedPeaceKingdoms();
    void setForcedPeaceKingdoms(u32 mask);
    bool isKingdomForcedPeace(s32 worldId);
    void setKingdomForcedPeace(s32 worldId, bool forced);

    // Generation-time setting (Additional Options page, not a live QoL toggle):
    // how much of the World Map the player gets to see. Vanilla: only the
    // kingdoms progression has unlocked, untinted. Visited: kingdom names are
    // color-coded (white = previously completed, green = current kingdom,
    // orange = visited but not yet progression-unlocked) and visited-but-not-
    // unlocked kingdoms are appended to the map in visit order. Unlocked: the
    // same display, except every kingdom except the run's starting one counts
    // as visited from the first frame (see applyUnlockedKingdomsMapStart), so
    // the whole map is travel-able immediately; kingdoms still graduate to the
    // real progression list as they are unlocked normally.
    // Numeric values are stable save-file contents - Vanilla/Visited keep the
    // 0/1 the old boolean stored so old saves and presets reinterpret the same
    // way.
    enum VisitedKingdomsMapMode : u8
    {
        VisitedKingdomsMapMode_Vanilla = 0,
        VisitedKingdomsMapMode_Visited = 1,
        VisitedKingdomsMapMode_Unlocked = 2,
        // Append-only: Custom is stored in the Wave C payload record because
        // the legacy header only had a Vanilla/Visited plus Unlocked bit.
        VisitedKingdomsMapMode_Custom = 3,
    };

    VisitedKingdomsMapMode getVisitedKingdomsMapMode();
    void setVisitedKingdomsMapMode(VisitedKingdomsMapMode mode);
    const char* getVisitedKingdomsMapModeName(VisitedKingdomsMapMode mode);

    // Compatibility helper retained for the call sites that only care whether
    // the map shows more than the vanilla unlock list. The setter can only
    // select Vanilla or Visited - never Unlocked, which older callers (old save
    // flags, old presets) don't know about.
    bool isVisitedKingdomsMapEnabled();
    void setVisitedKingdomsMapEnabled(bool enabled);

    // Initial map visibility for VisitedKingdomsMapMode_Custom. Bit indexes are
    // WorldMeta::WorldId values (0..16). At least one kingdom is always kept so
    // a malformed preset cannot produce an empty map.
    u32 getStartKingdomsMask();
    void setStartKingdomsMask(u32 mask);
    bool isStartKingdomSelected(s32 worldId);

    // Per-kingdom signed adjustment applied after the generated Moon Requirement
    // value is computed. The values are intentionally raw s8s in the Wave C
    // record so -20..20 remains an append-only, compact setting.
    // The spread Normal describes, and what clearMoonRequirementDeviations()
    // resets to. Each named difficulty carries its own spread - see
    // RequirementRando::getDefaultDeviation - and anything else makes the Req
    // Difficulty select read "Custom".
    constexpr s16 kDefaultMoonRequirementDeviation = 5;
    s16 getMoonRequirementDeviation();
    void setMoonRequirementDeviation(s16 deviation);
    void clearMoonRequirementDeviations();

    bool isMoonRockKeyEnabled();
    void setMoonRockKeyEnabled(bool enabled);

    enum class KeyScope : u8
    {
        AnyMoon = 0,
        WithinKingdom = 1,
    };

    KeyScope getMoonRockKeyScope();
    void setMoonRockKeyScope(KeyScope scope);
    KeyScope getSubareaKeyScope();
    void setSubareaKeyScope(KeyScope scope);

    // Subarea Keys lock a kingdom's optional sub-areas behind a found item, by
    // the kind of door you walk into (see EntranceDB2::SubareaType). The mode
    // picks how finely the keys are cut; the values are persisted directly, so
    // never renumber them.
    enum SubareaUnlockMode : u8
    {
        SubareaUnlockMode_Unlocked = 0, // feature off
        SubareaUnlockMode_PerType = 1, // 5 keys: one per door kind, all kingdoms
        SubareaUnlockMode_PerKingdom = 2, // 16 keys: one per kingdom, all kinds
        SubareaUnlockMode_PerKingdomAndType = 3, // 46 keys: one per (kingdom, kind)
    };

    // Which kingdom's moons carry the keys. Their collection-list page is taken
    // over by the key list and the leftover moons merge onto the other one, so
    // this also decides which two pages the shine list rearranges.
    enum SubareaUnlockHost : u8
    {
        SubareaUnlockHost_Cap = 0,
        SubareaUnlockHost_Moon = 1,
        SubareaUnlockHost_CapAndMoon = 2,
        // Append-only: All Kingdoms is the fourth host value in the Wave C
        // settings record/header projection.
        SubareaUnlockHost_AllKingdoms = 3,
    };

    SubareaUnlockMode getSubareaUnlockMode();
    void setSubareaUnlockMode(SubareaUnlockMode mode);
    bool isSubareaUnlockEnabled();
    const char* getSubareaUnlockModeName(SubareaUnlockMode mode);

    SubareaUnlockHost getSubareaUnlockHost();
    void setSubareaUnlockHost(SubareaUnlockHost host);
    const char* getSubareaUnlockHostName(SubareaUnlockHost host);

    // A host can only carry a mode whose key count fits its moon pool (Cap has
    // 31 moons, Moon 38, both together 69). Everything that offers or validates
    // the pair - the file select, the mod config readout, the seed builder -
    // goes through these so they cannot disagree.
    bool subareaHostFits(SubareaUnlockMode mode, SubareaUnlockHost host);
    SubareaUnlockHost clampSubareaUnlockHost(SubareaUnlockMode mode, SubareaUnlockHost host);

    // When enabled, each kingdom starts in a seed-selected state: its normal
    // pre-peace state, post-peace state, or Moon Rock ("moon cube") state when
    // that kingdom has one. The choice is a pure function of the seed and
    // kingdom, so generation and the new-game save always agree.
    bool isStartKingdomStateRandoEnabled();
    void setStartKingdomStateRandoEnabled(bool enabled);
    s32 getInitialKingdomScenario(s32 worldId);

    // Where a brand new save drops the player. Vanilla places him at the
    // starting kingdom's default Odyssey spawn. Overworld places him at the
    // overworld-side endpoint of a seed-chosen sub-area door in that kingdom.
    // Max additionally allows any coordinate-bearing logic node inside a
    // sub-area chain, as long as the logic can prove he walks back out to the
    // starting kingdom's overworld with only his starting equipment.
    //
    // Odyssey is not a roll at all: the file always opens inside the Odyssey's
    // interior (HomeShipInsideStage), whose only exit is the unshuffled
    // CurrentWorldHome door back to the starting kingdom's overworld. The
    // reachable set is therefore identical to Vanilla - the point is that the
    // player does not see which kingdom he is in until he walks out.
    //
    // The opening walk is modeled as TEMPORARY access: leaving the area gives
    // it up again unless a checkpoint flag inside it was touched (see
    // StartPositionRando and MoonRando::initState). Numeric values are stable
    // save-file contents (2 bits in the save header, so 3 is the last value
    // that fits without a format bump).
    enum StartPositionMode : u8
    {
        StartPositionMode_Vanilla = 0,
        StartPositionMode_Overworld = 1,
        StartPositionMode_Max = 2,
        StartPositionMode_Odyssey = 3,
    };

    StartPositionMode getStartPositionMode();
    void setStartPositionMode(StartPositionMode mode);
    const char* getStartPositionModeName(StartPositionMode mode);

    // Shuffles the order the chests must be opened in inside the three
    // sequential-chest rooms (Wooded, Mushroom, Seaside). The order is a pure
    // function of the seed and the room's stage, so leaving and re-entering the
    // room always reproduces the same order.
    bool isChestOrderRandoEnabled();
    void setChestOrderRandoEnabled(bool enabled);

    // Deterministically permutes standalone regular-coin and purple-coin
    // identities inside each authored map. Purple save identity follows the
    // shuffled coin, so collection remains one-to-one across reloads.
    bool isCoinRandoEnabled();
    void setCoinRandoEnabled(bool enabled);

    // Replaces every gold-coin price in Crazy Cap's catalogue with a
    // seed-derived 1-1000. Outfits and caps are one global catalogue, so they
    // are priced once and cost the same in every kingdom; the Life-Up Heart
    // and the shop Power Moon are re-rolled per kingdom. Regional-coin
    // souvenirs and stickers keep their vanilla prices - see
    // ShopPriceRando.cpp.
    bool isShopPriceRandoEnabled();
    void setShopPriceRandoEnabled(bool enabled);

    // Allows triple (multi) moon items at ordinary locations and vice versa.
    // Physical story/scenario progression still follows the location.
    // Both triple moon options report false in builds without
    // SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION and their setters are no-ops.
    bool isTripleMoonsAnywhereEnabled();
    void setTripleMoonsAnywhereEnabled(bool enabled);

    // Reshuffles which moons are triple/grand rewards (give 3), keeping the same
    // total count as vanilla but redistributing them across any main kingdom.
    // This only randomizes the reward magnitude/visual; scenario progression stays
    // pinned to the vanilla grand locations (see MoonRando reward-multi override).
    bool isRandomizeTripleMoonsEnabled();
    void setRandomizeTripleMoonsEnabled(bool enabled);

    enum class MoonAllocationDifficulty : u8
    {
        Easy = 0,
        Vanilla = 1,
        Hard = 2,
        Expert = 3,
        Max = 4,
    };

    enum class MoonCoinPolicy : u8
    {
        Vanilla = 0,
        Standard = 1,
        AbilityRush = 2,
    };

    bool isMoonReallocationEnabled();
    void setMoonReallocationEnabled(bool enabled);
    bool isRandomizeMoonAllocationEnabled();
    void setRandomizeMoonAllocationEnabled(bool enabled);
    MoonAllocationDifficulty getMoonAllocationDifficulty();
    void setMoonAllocationDifficulty(MoonAllocationDifficulty difficulty);
    const char* getMoonAllocationDifficultyName(MoonAllocationDifficulty difficulty);
    MoonCoinPolicy getMoonCoinPolicy();
    void setMoonCoinPolicy(MoonCoinPolicy policy);
    const char* getMoonCoinPolicyName(MoonCoinPolicy policy);
    bool isMoonAllocationActive();

    // Optional per-kingdom allocation targets. A value of -1 leaves that
    // kingdom on the generator's automatic distribution. The arrays are
    // deliberately independent of the legacy allocation booleans so old
    // saves and presets keep their original meaning.
    static constexpr s32 kKingdomCount = WorldMeta::sWorldCount;
    s16 getKingdomMoonTarget(s32 worldId);
    void setKingdomMoonTarget(s32 worldId, s16 count);
    bool isKingdomMoonTargetCustom();
    void clearKingdomMoonTargets();

    // Triple-moon distribution uses the same auto sentinel. The explicit
    // custom-state query is used by the derived menu roll; it is not a second
    // persisted mode flag.
    s8 getKingdomTripleMoonTarget(s32 worldId);
    void setKingdomTripleMoonTarget(s32 worldId, s8 count);
    bool isKingdomTripleMoonTargetCustom();
    void clearKingdomTripleMoonTargets();

    // ── toggles ───────────────────────────────────────────────────────────────────

    bool isGameModesFeatureEnabled();
    bool isTripleMoonRandomizationFeatureEnabled();

    // Legacy checks retained as permanent-on behavior so existing settings and
    // gameplay paths retain their current behavior.
    bool isCaptureLockFeatureEnabled();
    bool isLogicFeatureEnabled();
    bool isAbilityLockFeatureEnabled();

    bool isMoonRandoEnabled();
    void setMoonRandoEnabled(bool enabled);

    // SmallChains/Standard/LongChains are all the "coupled" chain-fill v4
    // builder (EntranceRando::buildConnections); they differ only in how
    // readily a sub-area chain keeps extending versus handing back to a
    // fresh kingdom entrance (see buildConnections for the two-pool draw
    // this controls). Limited ties every sub-area straight to its own
    // kingdom and never chains; Decoupled shuffles entrances and exits
    // independently. Numeric values are stable save-file contents (used
    // directly in RandoSaveFile::Header::reserved and the byaml config) -
    // Off/Standard/Decoupled/Limited keep their original 0-3 so old saves
    // reinterpret the same way; SmallChains/LongChains are new values, not
    // an old one repurposed. The player-facing menu shows a different order
    // (Off, Small Chains, Standard, Long Chains, Limited, Decoupled) - see
    // StageSceneStateFileSelect.cpp's entrance-mode UI order table.
    enum EntranceRandoMode : u8
    {
        EntranceRandoMode_Off = 0,
        EntranceRandoMode_Standard = 1,
        EntranceRandoMode_Decoupled = 2,
        EntranceRandoMode_Limited = 3,
        // RETIRED. Chain length is Seed Openness now, so these are no longer
        // offered in the menu. The values stay so an old save/config/preset
        // still parses; applyLegacyEntranceRandoMode() converts one into
        // Standard plus the openness level that reproduces it.
        EntranceRandoMode_SmallChains = 4,
        EntranceRandoMode_LongChains = 5,
    };

    EntranceRandoMode getEntranceRandoMode();
    void setEntranceRandoMode(EntranceRandoMode mode);
    bool isEntranceRandoEnabled();
    bool isEntranceRandoDecoupled();
    void setEntranceRandoEnabled(bool enabled);
    const char* getEntranceRandoModeName(EntranceRandoMode mode);
    // Converts a stored mode into the current model: the two retired
    // chain-length modes become Standard plus the Seed Openness level that
    // reproduces them. Call this from every LOAD path (save, Common config,
    // user preset) - never from the menu setter, or an explicit openness choice
    // would be overwritten every time the player touches Loading Zones.
    void applyLegacyEntranceRandoMode(EntranceRandoMode mode);
    const char* getCurrentEntranceRandoModeName();

    // How the entrance shuffle treats the Crazy Cap shop doors. This only bites
    // while Loading Zones randomization is on: with it off nothing is shuffled,
    // so all three values leave the shops vanilla.
    //
    // Randomized: the shop doors are ordinary loading zones and shuffle with
    //   everything else - the default, and what the shuffle did before this
    //   setting existed.
    // Swap: the shop doors leave the main pool and are permuted among
    //   themselves, so a shop door always leads to some shop.
    // Vanilla: the shop doors keep their original destinations.
    //
    // Two shops are shaped oddly and the setting treats them specially:
    //
    // - Sand's shop has a front door and a back way in over the roof. Only the
    //   door counts as a shop entrance here; the back entrance stays an
    //   ordinary loading zone in every mode, so it keeps shuffling normally.
    // - Metro's shop has two street doors into one interior. They are one
    //   player-facing interface, so under Swap both street doors lead to the
    //   same shop and both doors inside the Metro shop lead back to the same
    //   place.
    //
    // Numeric values are stable save-file contents (2 bits in the entrance
    // reserved byte), so they must never be reordered.
    enum ShopLocationMode : u8
    {
        ShopLocationMode_Randomized = 0,
        ShopLocationMode_Swap = 1,
        ShopLocationMode_Vanilla = 2,
    };

    ShopLocationMode getShopLocationMode();
    void setShopLocationMode(ShopLocationMode mode);
    const char* getShopLocationModeName(ShopLocationMode mode);
    const char* getCurrentShopLocationModeName();

    bool isPaintingRandoEnabled();
    void setPaintingRandoEnabled(bool enabled);

    bool isKingdomRandoEnabled();
    void setKingdomRandoEnabled(bool enabled);

    bool isRequirementRandoEnabled();
    void setRequirementRandoEnabled(bool enabled);

    bool isLogicRandoEnabled();
    void setLogicRandoEnabled(bool enabled);

    // Skybox randomization mode. Two axes: which areas get a new sky (kingdoms,
    // sub-areas, or both) and which pool they draw from (own-type only vs anything).
    enum SkyboxRandoMode : u8
    {
        SkyboxRandoMode_None = 0, // no skybox randomization
        SkyboxRandoMode_Kingdoms = 1, // kingdoms only, from kingdom skyboxes
        SkyboxRandoMode_Subareas = 2, // sub-areas only, from sub-area skyboxes
        SkyboxRandoMode_OwnAreas = 3, // both, but kingdoms↔kingdoms and sub-areas↔sub-areas
        SkyboxRandoMode_Max = 4, // both, anything-goes (full pool, no type/compat limit)
    };

    SkyboxRandoMode getSkyboxRandoMode();
    void setSkyboxRandoMode(SkyboxRandoMode mode);
    const char* getSkyboxRandoModeName(SkyboxRandoMode mode);

    bool isSkyboxRandoEnabled(); // mode != None
    bool isKingdomSkyboxRandoEnabled(); // kingdoms get a new sky (Kingdoms/OwnAreas/Max)
    bool isSubAreaSkyboxRandoEnabled(); // sub-areas get a new sky (Subareas/OwnAreas/Max)

    // When swapping skyboxes, keep each kingdom's native lighting/HDR/fog and only
    // re-skin the visible sky dome (avoids water/lava blowouts). Default on.
    bool isStabilizeShadersEnabled();
    void setStabilizeShadersEnabled(bool enabled);

    bool isCaptureRandoEnabled();
    void setCaptureRandoEnabled(bool enabled);

    // Chaos randomization is built in two halves. The enemy/capture, NPC and
    // 2D accessors below are compiled in with SMOR_FEATURE_ENEMY_RANDOMIZATION;
    // the prop accessors with SMOR_FEATURE_PROP_RANDOMIZATION. Every one of
    // them is declared unconditionally and reads off / writes nothing when its
    // half is compiled out, so saves, presets and the seed inspector keep the
    // same shape whichever halves a build carries.

    // Enemies / Captures: replaces configured placed enemy/capture instances
    // with another full actor (see config/enemy_swap_rules.csv and
    // src/Randomizer/EnemySwapRando.cpp). Swappable captures are unavailable
    // to logic; an authored capture gate is what opts one back in.
    bool isEnemySwapRandoEnabled();
    void setEnemySwapRandoEnabled(bool enabled);

    // Combined UI setting for the inspector/seed configuration. The legacy
    // enemy and NPC accessors remain available for save/preset compatibility,
    // but the public configuration toggle enables both categories together.
    bool isEnemyNpcSwapRandoEnabled();
    void setEnemyNpcSwapRandoEnabled(bool enabled);

    // What an eligible source may turn into. The two chaos categories share
    // this shape: Off leaves them vanilla, SameCategory keeps the swap inside
    // the source's own category, and WithCombat additionally admits the combat
    // pool (capture/enemy/generator) -- the same pool the Enemies/Captures
    // toggle uses. Ordering matters: the values are persisted directly in the
    // save header. The first three values only widen the target pool; the
    // prop-only fourth value also enables deterministic suppression.
    enum class ChaosSwapMode : u8
    {
        Off = 0,
        SameCategory = 1,
        WithCombat = 2,
        // Prop-only option: the normal prop + combat pool remains intact,
        // but a participating prop may instead be suppressed entirely.
        // Value 3 fits the existing two-bit persisted mode field.
        WithDeletion = 3,
    };

    // NPC chaos uses the target's full actor identity. Interaction-driven NPCs
    // remain vanilla in every mode.
    ChaosSwapMode getNpcSwapMode();
    void setNpcSwapMode(ChaosSwapMode mode);

    // Incidental scenery may become other props, or combat actors, per the
    // mode. Moon-linked props and functional gameplay infrastructure remain
    // vanilla; the chance below selects a deterministic subset of the rest.
    ChaosSwapMode getPropSwapMode();
    void setPropSwapMode(ChaosSwapMode mode);

    // Legacy boolean accessors. Retained because saves, presets and the seed
    // inspector predate the modes; "enabled" means anything other than Off,
    // and setting true selects WithCombat, which is what those builds did.
    bool isNpcSwapRandoEnabled();
    void setNpcSwapRandoEnabled(bool enabled);
    bool isPropSwapRandoEnabled();
    void setPropSwapRandoEnabled(bool enabled);

    u8 getPropSwapChancePercent();
    void setPropSwapChancePercent(u8 percent);

    // 2D section enemies. A boolean rather than a ChaosSwapMode because there
    // is no wider pool to offer: a 3D actor dropped into a flat section has no
    // 2D behaviour, and the section's camera and collision are built for a
    // plane it would ignore. 2D only ever becomes 2D.
    bool isTwoDSwapRandoEnabled();
    void setTwoDSwapRandoEnabled(bool enabled);

    // True when any chaos category is configured on, whatever the force
    // disable below says. Callers that want to know whether chaos is actually
    // running should ask isChaosRandomizationActive() instead.
    bool isAnyChaosRandomizationConfigured();

    // In-game escape hatch for a seed that crashes on a particular stage or
    // scenario: every placement builds its vanilla actor while this is set.
    // Read where a placement asks for its replacement, not where the swap
    // assignment is built, so the seed -- its assignment, its capture-gate
    // witnesses, and its spoiler log -- is completely unaffected and the
    // placements come back unchanged when it is cleared. Persisted in the
    // global config rather than the save, since it describes this playthrough
    // rather than the seed.
    bool isChaosForceDisabled();
    void setChaosForceDisabled(bool disabled);

    // Chaos categories configured on AND not force-disabled.
    bool isChaosRandomizationActive();

    // The hint system is always active; this selects how expensive hints/buys are.
    enum class HintPriceTier
    {
        Cheap,
        Normal,
        Expensive,
    };

    bool isHintSystemEnabled();
    HintPriceTier getHintPriceTier();
    void setHintPriceTier(HintPriceTier tier);
    void cycleHintPriceTier();
    const char* getHintPriceTierName(HintPriceTier tier);

    // Five independently-configurable costs back both kinds of shop actions.
    // Purchases is shared by direct moon, capture, and ability purchases.
    enum class HintPriceCategory : u8
    {
        Moons = 0,
        Captures = 1,
        Abilities = 2,
        Path = 3,
        Purchases = 4,
        Count = 5,
    };

    static constexpr u8 kHintPriceCategoryCount = static_cast<u8>(HintPriceCategory::Count);

    // These are option indices, deliberately separate from their displayed
    // amounts so the compact profiles can be persisted independently.
    enum class HintCoinPrice : u8
    {
        Disabled = 0,
        Coins100 = 1,
        Coins200 = 2,
        Coins250 = 3,
        Coins500 = 4,
        Coins1000 = 5,
        Coins2000 = 6,
        Coins2500 = 7,
        Progressive = 8,
    };

    enum class HintTokenPrice : u8
    {
        Disabled = 0,
        Tokens1 = 1,
        Tokens2 = 2,
        Tokens3 = 3,
        Tokens5 = 4,
        Progressive = 5,
    };

    // Persisted separately from the five base prices.  Zero disables price
    // increases; the remaining values are additive increases per successful
    // purchase in that category.
    enum class HintCoinProgressiveIncrement : u8
    {
        Disabled = 0,
        Coins100 = 1,
        Coins200 = 2,
        Coins250 = 3,
        Coins500 = 4,
        Coins1000 = 5,
        Coins2000 = 6,
        Coins2500 = 7,
    };

    enum class HintTokenProgressiveIncrement : u8
    {
        Disabled = 0,
        Tokens1 = 1,
        Tokens2 = 2,
        Tokens3 = 3,
        Tokens5 = 4,
    };

    struct HintPriceProfile {
        u8 values[kHintPriceCategoryCount];
        // Sixth nibble in the compact profile (bits 20-23).
        u8 progressiveIncrement;
        // Bit 24 of the compact profile.  While set, one price ladder is
        // shared by every category: the increment multiplies the total number
        // of hint purchases on the save rather than that category's own count,
        // so any purchase makes every later hint more expensive.
        bool progressiveGlobal;
    };

    struct HintPriceQuote {
        HintCurrencyMode currency;
        s32 amount;
        bool enabled;
        bool progressive;
    };

    static_assert(static_cast<u8>(HintPriceCategory::Count) == 5,
        "Hint price profile packing assumes five categories");
    static_assert(static_cast<u8>(HintCoinPrice::Progressive) == 8,
        "Coin price options are persisted as compact indices");
    static_assert(static_cast<u8>(HintTokenPrice::Progressive) == 5,
        "Token price options are persisted as compact indices");

    bool isValidHintPriceCategory(HintPriceCategory category);
    bool isValidHintCoinPrice(u8 option);
    bool isValidHintTokenPrice(u8 option);
    bool isValidHintCoinProgressiveIncrement(u8 option);
    bool isValidHintTokenProgressiveIncrement(u8 option);

    HintPriceProfile getHintCoinPriceProfile();
    void setHintCoinPriceProfile(HintPriceProfile profile);
    HintPriceProfile getHintTokenPriceProfile();
    void setHintTokenPriceProfile(HintPriceProfile profile);

    u8 getHintCoinPrice(HintPriceCategory category);
    void setHintCoinPrice(HintPriceCategory category, u8 option);
    u8 getHintTokenPrice(HintPriceCategory category);
    void setHintTokenPrice(HintPriceCategory category, u8 option);
    // Gets/sets the profile selected by getHintCurrencyMode().
    u8 getActiveHintPrice(HintPriceCategory category);
    void setActiveHintPrice(HintPriceCategory category, u8 option);
    u8 getHintCoinProgressiveIncrement();
    void setHintCoinProgressiveIncrement(u8 option);
    u8 getHintTokenProgressiveIncrement();
    void setHintTokenProgressiveIncrement(u8 option);
    u8 getActiveHintProgressiveIncrement();
    void setActiveHintProgressiveIncrement(u8 option);
    bool isHintCoinProgressiveGlobal();
    void setHintCoinProgressiveGlobal(bool global);
    bool isHintTokenProgressiveGlobal();
    void setHintTokenProgressiveGlobal(bool global);
    bool isActiveHintProgressiveGlobal();
    void setActiveHintProgressiveGlobal(bool global);

    // Per-category advanced hint entries. A custom entry can use coins while
    // the global Hint Tokens mode is active; its explicit price is in the
    // currency selected by that override. The custom marker is separate from
    // the profile index so a price of zero can still be represented safely.
    bool isHintPriceCustom(HintPriceCategory category);
    u16 getHintCustomPrice(HintPriceCategory category);
    void setHintCustomPrice(HintPriceCategory category, u16 price);
    bool isHintCoinOverride(HintPriceCategory category);
    void setHintCoinOverride(HintPriceCategory category, bool enabled);
    void clearHintPriceOverride(HintPriceCategory category);
    void clearHintPriceOverrides();

    // Six nibbles: five categories followed by progressive increment, then the
    // global-ladder flag at bit 24. Invalid input is normalized to the
    // appropriate profile defaults, making legacy/corrupt config data safe.
    u32 packHintPriceProfile(HintPriceProfile profile);
    HintPriceProfile unpackHintCoinPriceProfile(u32 packed);
    HintPriceProfile unpackHintTokenPriceProfile(u32 packed);

    // A Disabled option is represented explicitly by enabled == false; amount
    // is never a free/zero price.  Progressive prices are calculated from the
    // caller's ladder position - this category's successful purchase count, or
    // the save's total across every category while the global ladder is on.
    // Which currency this category is priced in. Hint Tokens are only a
    // choice while some kingdom hands them out; otherwise every category is
    // priced in coins whatever the per-category flag says.
    HintCurrencyMode getHintCategoryCurrency(HintPriceCategory category);
    // Sets it. Unlike setHintCoinOverride, this only picks the bank the price
    // is read from - it never turns the category into a typed custom price.
    void setHintCategoryCurrency(HintPriceCategory category, HintCurrencyMode currency);
    // The shared-ladder flag belonging to that category currency.
    bool isHintCategoryProgressiveGlobal(HintPriceCategory category);
    HintPriceQuote getHintPriceQuote(HintPriceCategory category, u32 successfulPurchaseCount);

    // Retired compatibility encoding. Older settings made access cumulative:
    // Basic sold location hints, Extended also sold routes into subareas, and
    // Purchases additionally sold the item directly. Reading one translates
    // those gates into Disabled price categories before discarding the mode.
    enum class HintMode : u8
    {
        Basic,
        Extended,
        Purchases,
    };

    HintMode getHintMode();
    void setHintMode(HintMode mode);
    const char* getHintModeName(HintMode mode);

    // additional toggles

    enum class MusicRandoMode : u8
    {
        Off = 0,
        On = 1, // Standard
        Max = 2,
        // Stable per-area/per-source mappings. Appended after Max so existing
        // save/config values keep their original meanings.
        Extended = 3,
    };

    MusicRandoMode getMusicRandoMode();
    void setMusicRandoMode(MusicRandoMode mode);
    const char* getMusicRandoModeName(MusicRandoMode mode);
    bool isMusicRandoEnabled(); // compatibility: mode != Off
    void setMusicRandoEnabled(bool enabled); // compatibility: On / Off

    bool isMusicOverlayEnabled();
    void setMusicOverlayEnabled(bool enabled);

    // Removes Japanese-language theme variants from the music randomizer's
    // destination pool. Vanilla requests for those tracks remain untouched.
    bool isStreamerModeEnabled();
    void setStreamerModeEnabled(bool enabled);

    // Music pool filters. Each removes a category from the shuffled pool, so a
    // filtered track is neither drawn as a replacement nor itself randomized.
    // 2D themes are the 8-bit variants; extra themes are the non
    // overworld/sub-area one-offs (titles, credits, minigames, parades...);
    // boss themes are MusicDB::sBossResourceTable.
    bool isMusic2DThemesDisabled();
    void setMusic2DThemesDisabled(bool disabled);
    bool isMusicExtraThemesDisabled();
    void setMusicExtraThemesDisabled(bool disabled);
    bool isMusicBossThemesDisabled();
    void setMusicBossThemesDisabled(bool disabled);
    // Where background music comes from. Orthogonal to MusicRandoMode, which
    // only decides how hard the *vanilla* track pool is shuffled.
    enum class MusicSourceMode : u8
    {
        Off = 0, // no background music at all
        Vanilla = 1, // the game's own tracks (the default)
        Custom = 2, // only MP3s from sd:/SMO-Randomizer/Music
        Both = 3, // each cue draws vanilla or custom, fixed per seed
    };

    MusicSourceMode getMusicSourceMode();
    void setMusicSourceMode(MusicSourceMode mode);
    const char* getMusicSourceModeName(MusicSourceMode mode);
    const char* getMusicSourceModeDescription(MusicSourceMode mode);
    // True for the modes that can play an MP3, so callers can avoid bringing up
    // the audio backend when nothing will ever use it.
    bool isCustomMusicPossible();

    // Stable theme switches exposed by MusicDB::sThemes. The mask is stored in
    // the generated slot sidecar, so a loaded seed gets the same pool even if
    // the player's current menu preferences differ.
    u64 getMusicThemeMask();
    void setMusicThemeMask(u64 mask);
    bool isMusicThemeEnabled(s32 themeId);
    void setMusicThemeEnabled(s32 themeId, bool enabled);

    bool isSphinxRandoEnabled();
    void setSphinxRandoEnabled(bool enabled);

    bool isLockedAbilitiesEnabled();
    void setLockedAbilitiesEnabled(bool enabled);

    // Off grants a multi-stage move in its final form the first time it is
    // unlocked - the triple jump arrives whole rather than as double then
    // triple. Purely how a pool member is handed over; it does not change
    // which abilities are in the pool.
    bool isProgressiveAbilitiesEnabled();
    void setProgressiveAbilitiesEnabled(bool enabled);

    // Stable Wave B pool membership. The masks are independent of the master
    // lock toggles so turning a system off and on again restores its choices.
    bool isCaptureInLockPool(s32 captureId);
    void setCaptureInLockPool(s32 captureId, bool enabled);
    bool isAbilityInLockPool(s32 abilityId);
    void setAbilityInLockPool(s32 abilityId, bool enabled);
    u64 getCaptureLockPoolMask(s32 bank);
    void setCaptureLockPoolMask(s32 bank, u64 mask);
    u64 getAbilityLockPoolMask();
    void setAbilityLockPoolMask(u64 mask);
    void resetLockPools();

    // Plays the "YOU GOT A MOON" get sequence at greatly increased speed (the demo
    // is pumped many times per frame) so moons are collected almost instantly.
    bool isFastMoonDemosEnabled();
    void setFastMoonDemosEnabled(bool enabled);

    // Button-only controls for the motion-only actions: up-throw, down-throw
    // (suppresses the normal B jump while airborne), and spin/shake (cap spin,
    // captures, and rolling boost). Each role is independently remappable to
    // any face/shoulder button - see ControllerButton below.
    bool isMotionFreeControlsEnabled();
    void setMotionFreeControlsEnabled(bool enabled);

    enum class ControllerButton : u8
    {
        A,
        B,
        X,
        Y,
        L,
        R,
        ZL,
        ZR,
    };
    constexpr s32 cControllerButtonCount = static_cast<s32>(ControllerButton::ZR) + 1;

    const char16_t* getControllerButtonName(ControllerButton button);

    ControllerButton getUpThrowButton();
    void setUpThrowButton(ControllerButton button);
    ControllerButton getDownThrowJumpButton();
    void setDownThrowJumpButton(ControllerButton button);
    ControllerButton getSpinShakeButton();
    void setSpinShakeButton(ControllerButton button);

    // QoL: skip the demo camera pan AppearSwitchTimer plays when a switch reveals
    // something. Defaults on (was previously a hardcoded always-on patch).
    bool isSkipCutsceneCamerasEnabled();
    void setSkipCutsceneCamerasEnabled(bool enabled);

    // Costume-gated doors are kept permanently open unconditionally (see
    // unlockCostumeDoorsHook / unlockCostumeDoorMetroHook in QoL.cpp); there is
    // no setting for it.

    // QoL: costume-check NPCs give their moon without requiring the matching
    // cap and outfit. Defaults off.
    bool isFreeCostumeMoonEnabled();
    void setFreeCostumeMoonEnabled(bool enabled);

    // QoL: sheep-herding moons spawn after the configured number of sheep
    // reaches the pen. The original bool API remains for old callers/presets.
    bool isOneSheepHerdingEnabled();
    void setOneSheepHerdingEnabled(bool enabled);
    static constexpr s32 kSheepHerdingMinCount = 1;
    static constexpr s32 kSheepHerdingMaxCount = 4;
    static constexpr s32 kSheepHerdingDefaultCount = 1;
    s32 getSheepHerdingCount();
    void setSheepHerdingCount(s32 count);

    // QoL: lower both jump-rope and volleyball moon thresholds to a custom
    // low/high pair (the historical defaults are 10 and 25).
    static constexpr s32 kEasyMinigameDefaultLow = 10;
    static constexpr s32 kEasyMinigameDefaultHigh = 25;
    static constexpr s32 kEasyMinigameMin = 1;
    static constexpr s32 kEasyMinigameMax = 9999;
    bool isEasyMinigamesEnabled();
    void setEasyMinigamesEnabled(bool enabled);
    s32 getEasyMinigameLow();
    s32 getEasyMinigameHigh();
    void setEasyMinigameLow(s32 value);
    void setEasyMinigameHigh(s32 value);

    // QoL: the dig-spot dog (Shibaken) heads straight for the dig point holding
    // the Power Moon instead of walking its chain of coin spots, drops the
    // wandering fake digs it does around each one, cuts short its sniffing
    // pause, and moves slightly faster. Defaults off.
    bool isFastDogEnabled();
    void setFastDogEnabled(bool enabled);

    // QoL: removes vanilla's 9999 coin ceiling. GameDataFile::addCoin clamps
    // the running total with `csel w9, w9, w10, lt` against a literal 9999
    // (verified in the retail binary, see CoinUncap.cpp); with this on, that
    // clamp is undone and coins accumulate to cMaxCoins instead. Spending goes
    // through the same function - GameDataFunction::subCoin is literally
    // `neg w1` then a tail call into it - so shops keep working untouched.
    // Defaults off.
    bool isUncappedCoinsEnabled();
    void setUncappedCoinsEnabled(bool enabled);

    // QoL: treat every Hint Art photo as already viewed, allowing its dig-point
    // moon to be collected without first visiting the artwork.
    bool isUnlockHintArtEnabled();
    void setUnlockHintArtEnabled(bool enabled);

    // Debug/QoL: enables the ZL + ZR + D-Pad Up No Clip toggle. Defaults off.
    bool isNoClipEnabled();
    void setNoClipEnabled(bool enabled);

    // QoL: choose which side of the screen the tracker is anchored to. Defaults
    // to the established left-side layout.
    bool isTrackerRightAligned();
    void setTrackerRightAligned(bool enabled);

    // QoL: once a kingdom has been visited and its moon requirement has been
    // met, ordinary physical moon pickups show up already collected and pay
    // out as repeat pickups. Physical native NPC/event awards, story/multi
    // locations, and progression or special rewards remain normal. The
    // collection is still committed to the save.
    bool isCompletedKingdomMoonCollectedEnabled();
    void setCompletedKingdomMoonCollectedEnabled(bool enabled);

    // QoL: change to a random outfit every time you die.
    bool isRandomizeOutfitsEnabled();
    void setRandomizeOutfitsEnabled(bool enabled);

    // QoL: pin the seed's free starting cap throw to Neutral Throw instead of
    // letting generation roll one of the four. Read by AbilityRando while it
    // builds the seed, so logic solves against a guaranteed Neutral Throw.
    bool isAlwaysNeutralCapThrowEnabled();
    void setAlwaysNeutralCapThrowEnabled(bool enabled);

    // Optional movement checklist for the Moon Kingdom cave skip in the tracker.
    // Easy requires either Ground Pound Jump or Backflip, plus Wall Jump and Dive;
    // Hard only requires Wall Jump.
    enum class TrackerCaveSkipMode
    {
        None,
        Easy,
        Hard,
    };

    TrackerCaveSkipMode getTrackerCaveSkipMode();
    void setTrackerCaveSkipMode(TrackerCaveSkipMode mode);
    const char* getTrackerCaveSkipModeName(TrackerCaveSkipMode mode);

    // QoL: on-screen progress tracker overlay (per-kingdom moons / requirements /
    // world-peace + moon-cube state). Standard omits the playtime row; Complete
    // adds it. Hints and Guided both add a live per-kingdom reachability dot on
    // top of Complete: Hints only reports whether anything is reachable there
    // (no judgment of importance); Guided additionally colors/counts by whether
    // what's reachable is progression-critical. Defaults to None (opt-in HUD).
    enum class TrackerOverlayMode
    {
        None,
        Basic,
        Extended,
        Hints,
        Guided,
    };

    bool isTrackerOverlayEnabled(); // convenience: mode != None
    TrackerOverlayMode getTrackerOverlayMode();
    void setTrackerOverlayMode(TrackerOverlayMode mode);
    const char* getTrackerOverlayModeName(TrackerOverlayMode mode);

    // Tracker: color a kingdom's moon counter against the requirement range that
    // kingdom could have been given under the current settings (its base value
    // -/+ the allowed offset), never against the value the seed actually rolled.
    // Upper only marks "at or past the highest possible requirement" green; Full
    // also marks below-the-lowest-possible red and in-between yellow. Both stop
    // coloring once the kingdom is visited and its real requirement is known.
    enum class TrackerMoonLimitMode
    {
        None,
        Upper,
        Full,
    };

    TrackerMoonLimitMode getTrackerMoonLimitMode();
    void setTrackerMoonLimitMode(TrackerMoonLimitMode mode);
    const char* getTrackerMoonLimitModeName(TrackerMoonLimitMode mode);

    bool isTrackerTimerEnabled();
    void setTrackerTimerEnabled(bool value);

    // Board HUD size. Medium preserves the original board layout; Small and
    // Large scale the complete board, including its labels and cell feedback.
    enum class BoardScale
    {
        Small,
        Medium,
        Large,
    };

    BoardScale getBoardScale();
    void setBoardScale(BoardScale scale);
    const char* getBoardScaleName(BoardScale scale);
    float getBoardScaleFactor();

    // QoL: show a brief banner with the current area's name when entering an
    // overworld kingdom or a subarea, similar to the Music Overlay. Defaults on.
    bool isAreaNameOverlayEnabled();
    void setAreaNameOverlayEnabled(bool enabled);

    enum class PurpleShopPrice : u8
    {
        Purple10 = 0,
        Purple20,
        Purple25,
        Purple50,
        Purple100,
        // Per item, 1 to that kingdom's purple-coin total: every offer on a
        // shop's shelf is priced on its own, so the same shop can want 7 coins
        // for one thing and 84 for the next. Appended so the three persisted
        // encodings (config "PSP", the sidecar's 3-bit field, and preset roll
        // indices) keep their existing meanings.
        Random,
        // Directly entered flat/percentage value. The value is stored in the
        // append-only advanced settings record; this enum value is appended so
        // all existing encodings retain their meanings.
        Custom,
    };

    s32 getPurpleShopAbilityCount();
    void setPurpleShopAbilityCount(s32 count);
    s32 getPurpleShopCaptureCount();
    void setPurpleShopCaptureCount(s32 count);
    bool arePurpleShopMoonsEnabled();
    void setPurpleShopMoonsEnabled(bool enabled);
    PurpleShopPrice getPurpleShopPrice();
    void setPurpleShopPrice(PurpleShopPrice price);
    // The flat selection, in coins. Random has no flat value and reports the
    // Purple50 default, which is also what every mode falls back to for a
    // kingdom with no purple coins of its own.
    s32 getPurpleShopPriceValue();
    const char* getPurpleShopPriceName(PurpleShopPrice price);
    s32 getPurpleShopCustomPrice();
    void setPurpleShopCustomPrice(s32 price);
    bool isPurpleShopPriceCustom();

    // Reads the selection as a share of the kingdom's purple-coin total rather
    // than a flat amount: 25 means 25%, so the same setting costs less in a
    // 50-coin kingdom than in a 100-coin one. Ignored under Random, which is
    // already scaled to the kingdom.
    bool isProportionalKingdomPricingEnabled();
    void setProportionalKingdomPricingEnabled(bool enabled);

    // What one purple-shop offer costs in the kingdom it is sold in.
    // purpleCoinTotal is that kingdom's full regional-coin count (0 when it
    // has none, which falls back to the flat selection). offerKey identifies
    // the individual offer and is what separates two items sharing one shelf;
    // the flat and Proportional modes ignore it, because there the price is a
    // property of the setting rather than of the item. The result is always at
    // least 1, and Random is a pure function of the seed, the kingdom and that
    // key, so a shop re-priced on a later visit quotes every item the same
    // number again.
    s32 resolvePurpleShopPrice(s32 worldId, s32 purpleCoinTotal, u32 offerKey);

    u32 getVisitedKingdoms();
    void setVisitedKingdoms(u32 visited);

    // Ordered visit tracking: records the sequence a kingdom was first physically
    // entered in, independent of any overlay toggle. Used to append visited-but-
    // not-yet-unlocked kingdoms to the world map in visit order (see QoL toggle
    // below). -1 = not yet visited / no recorded order.
    void recordKingdomVisit(const char* stageName);
    // Same thing keyed by kingdom rather than by the stage that was entered,
    // for a visit that arrives from a team-mate over the wire - there is no
    // stage name on that path, and the sender already did the mapping.
    void recordKingdomVisitById(s32 worldId);
    s8 getKingdomVisitOrder(s32 worldId);
    void setKingdomVisitOrder(s32 worldId, s8 order);
    void resetKingdomVisitOrder();

    // Seeds the visit record for VisitedKingdomsMapMode_Unlocked: every kingdom
    // except the run's starting one is marked visited and ranked in vanilla
    // kingdom order, which is what puts them all on the World Map from the
    // start. Idempotent (re-applied on every load) and a no-op in the other two
    // modes. Must be called after the seed's kingdom route is built, since the
    // starting kingdom comes from it.
    void applyUnlockedKingdomsMapStart();

    // Debug: force a specific scenario number for one kingdom (WorldMeta::WorldId_*),
    // bypassing all normal scenario computation for it. worldId < 0 means no
    // override is active. Session-only (not saved), set from the "Scenario
    // Override" testing menu.
    s32 getScenarioOverrideWorldId();
    s32 getScenarioOverrideValue();
    void setScenarioOverride(s32 worldId, s32 scenarioNo);
    void clearScenarioOverride();

    // ── starting equipment / capture-ability difficulty ───────────────────────────
    // How much the player starts with. Fed to logic (a Plentiful start can satisfy
    // more checks from the beginning).
    enum StartingEquipment : u8
    {
        StartingEquipment_Scarce = 0, // just a jump + cap throw
        StartingEquipment_Normal = 1, // + 2 captures + 1 extra ability
        StartingEquipment_Plentiful = 2, // + 4 captures + 2 extra abilities
    };

    // How many duplicate captures/abilities exist on Mushroom and Dark/Darker
    // Side moons. Special-kingdom items are controlled by their Moons Use selectors.
    enum CaptureAbilityDifficulty : u8
    {
        CaptureAbilityDifficulty_Scarce = 0, // one instance of each
        CaptureAbilityDifficulty_Normal = 1, // some duplicated
        CaptureAbilityDifficulty_Plentiful = 2, // legacy save value; remapped to Normal
    };

    StartingEquipment getStartingEquipment();
    void setStartingEquipment(StartingEquipment value);
    const char* getStartingEquipmentName(StartingEquipment value);
    s32 getStartingCaptureCount();
    s32 getStartingThrowCount();
    s32 getStartingAbilityCount();
    bool isStartingEquipmentCustom();
    void setStartingCaptureCount(s32 count);
    void setStartingThrowCount(s32 count);
    void setStartingAbilityCount(s32 count);
    // A negative field means that field follows the selected preset. This is
    // used by sidecar and preset restore so a Custom selection can be exact.
    void setStartingEquipmentOverrides(s32 captures, s32 throws, s32 abilities);

    CaptureAbilityDifficulty getCaptureAbilityDifficulty();
    void setCaptureAbilityDifficulty(CaptureAbilityDifficulty value);
    const char* getCaptureAbilityDifficultyName(CaptureAbilityDifficulty value);

    // ── movement abilities (Ability Lock sub-page) ────────────────────────────────
    // Five extra abilities that gate vanilla traversal verbs rather than a move:
    // Moon Kingdom's low gravity, swimming, picking objects up, riding Jaxi and
    // riding the Motor Scooter. Each one is off by default, and the whole page is
    // hidden (and every dial forced to its granted state) unless Locked Abilities
    // is on - they are sub-options of it, not an independent system.
    //
    // Low gravity has four ways to arrive rather than a plain on/off:
    //   Unlocked              - vanilla; Moon Kingdom is low-gravity from the start.
    //   AbilityLock           - a normal ability-pool item, hosted on a Dark/Darker
    //                           Side moon like every other locked ability.
    //   MoonRequirements      - granted once Moon Kingdom's own moon requirement is
    //                           met (the same count the chapel door asks for).
    //   ExtraMoonRequirements - as above plus a seeded 5-10 extra moons on top.
    //
    // Numeric values are stable save contents: they are written verbatim into
    // the byaml config's packed "MvA" int and into the version-12 movement
    // ability record in the rando sidecar, so never renumber them.
    enum class MoonGravityMode : u8
    {
        Unlocked = 0,
        AbilityLock = 1,
        MoonRequirements = 2,
        ExtraMoonRequirements = 3,
    };

    MoonGravityMode getMoonGravityMode();
    void setMoonGravityMode(MoonGravityMode mode);
    const char* getMoonGravityModeName(MoonGravityMode mode);
    // True when low gravity is withheld at all - i.e. any mode but Unlocked, and
    // only while Locked Abilities is on.
    bool isMoonGravityLocked();
    // True only for the mode that puts low gravity in the ability reward pool.
    bool isMoonGravityAbilityLocked();
    // True for the two moon-count modes, which unlock it outside the item pool.
    bool isMoonGravityCountLocked();
    // Extra moons the ExtraMoonRequirements mode stacks on top of Moon Kingdom's
    // own requirement. Rolled from the seed (5-10 inclusive); 0 in every other
    // mode. Reproducible from (seed, attempt) alone, never from live progress.
    s32 getMoonGravityExtraRequirement();
    // Moon Kingdom moons needed for low gravity under either count mode: the
    // kingdom's own requirement plus the extra above. 0 when no count mode is on.
    s32 getMoonGravityMoonRequirement();

    bool isSwimLockEnabled();
    void setSwimLockEnabled(bool enabled);
    bool isGrabLockEnabled();
    void setGrabLockEnabled(bool enabled);
    bool isJaxiLockEnabled();
    void setJaxiLockEnabled(bool enabled);
    bool isScooterLockEnabled();
    void setScooterLockEnabled(bool enabled);
    bool isRocketFlowerLockEnabled();
    void setRocketFlowerLockEnabled(bool enabled);
    bool isNpcTalkLockEnabled();
    void setNpcTalkLockEnabled(bool enabled);
    bool isPipe2DLockEnabled();
    void setPipe2DLockEnabled(bool enabled);
    bool isTimerChallengeLockEnabled();
    void setTimerChallengeLockEnabled(bool enabled);

    // All the dials in one int, so the byaml config (whose hash must stay small
    // - see the truncation note in main.cpp) and the sidecar record can each
    // carry them in a single field. Bits 0-1 are MoonGravityMode, bits 2-9 are
    // swim/grab/jaxi/scooter/rocket flower/npc talk/2D pipes/timer challenges. 0 is
    // the all-granted default, so a save or preset written before this page existed
    // decodes to its old behavior.
    u32 packMovementAbilitySettings();
    void applyMovementAbilitySettings(u32 packed);

    // ── lifecycle ─────────────────────────────────────────────────────────────────

    bool reinit();
    bool reinitDeterministic(u32 attempt = 0);

    // ── log ───────────────────────────────────────────────────────────────────────

    void logPush(const char* fmt, ...);

    // Guard for the multi-line "here is the table I just built" narrations.
    //
    // reinit() re-runs the whole dependency chain on every stage-scene init,
    // and each run builds more than one (seed, attempt) pair - typically the
    // generation attempt followed by the saved one. A caller that remembers
    // only its own last pair therefore sees a different pair every call and
    // never suppresses anything, which is how a single stage load ended up
    // dumping the painting, entrance, capture and ability tables twice each.
    //
    // Returns true the first time this tag sees this pair. `tag` just has to be
    // a stable address unique to the call site (`&sSomeStatic`, a literal).
    bool shouldNarrateBuild(const void* tag, u32 seed, u32 attempt);
    const char* logGet(s32 index);
    s32 logCount();

} // namespace Randomizer
