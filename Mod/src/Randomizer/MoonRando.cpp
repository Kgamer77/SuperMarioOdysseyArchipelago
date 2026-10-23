#include "Randomizer/MoonRando.h"

#include "EntranceDB.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/ArrivalLog.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EnemySwapRando.h"
#include "Randomizer/EntranceFallbacks.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/RandoSaveFile.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/Rng.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SeedGenerator.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"

#include "nn/fs.h"
#include "nn/os.h"
#include "nn/util.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <stdint.h>

namespace MoonRando {
    struct TraceBuf {
        static constexpr size_t kCapacity = 1024 * 1024;
        static char data[kCapacity];
        static size_t used;
        static bool enabled;

        static void reset() { used = 0; }

        static void append(const char* str) {
            if (!enabled)
                return;
            size_t len = std::strlen(str);
            if (used + len >= kCapacity)
                return;
            std::memcpy(data + used, str, len);
            used += len;
        }

        static void appendf(const char* fmt, ...) {
            if (!enabled || used >= kCapacity)
                return;
            char tmp[512];
            va_list args;
            va_start(args, fmt);
            s32 n = nn::util::VSNPrintf(tmp, sizeof(tmp), fmt, args);
            va_end(args);
            if (n > 0)
                append(tmp);
        }

        static void dumpToFile(u32 seed) {
            if (!enabled)
                return;
            char path[128];
            nn::util::SNPrintf(path, sizeof(path), "sd:/SMO-Randomizer/Spoiler-Logs/BufferSphereTrace - %u.txt", seed);
            nn::fs::DeleteFile(path);
            if (nn::fs::CreateFile(path, (s64)used).IsSuccess()) {
                nn::fs::FileHandle handle;
                if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsSuccess()) {
                    nn::fs::WriteFile(handle, 0, data, used, nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush));
                    nn::fs::CloseFile(handle);
                }
            }
        }
    };
    char TraceBuf::data[TraceBuf::kCapacity];
    size_t TraceBuf::used = 0;
    bool TraceBuf::enabled = false;

    namespace LD = Randomizer::LogicData;

    static_assert(EntranceDB::sLinkCount == LD::sEntranceCount,
        "Runtime logic entrance table must match EntranceDB active link count.");

    static constexpr s32 kMaxWarnings = 8;
    static constexpr s32 kWarningSize = 160;
    static constexpr s32 kMaxUnlockTokensPerMoon = 8;
    // Keep the load factor below 0.5 for the uid+stage identity cache below.
    // LogicData currently has 775 moon items, so 2048 also leaves room for
    // generated-table growth while making the mask/probe path trivial.
    static constexpr s32 kMoonIdentityCacheCapacity = 2048;
    static_assert((kMoonIdentityCacheCapacity
                      & (kMoonIdentityCacheCapacity - 1))
            == 0,
        "Moon identity cache capacity must be a power of two.");
    static_assert(kMoonIdentityCacheCapacity >= LD::sMoonCount * 2,
        "Moon identity cache must stay below a 0.5 load factor.");
    static_assert(LD::sMoonCount <= 0x7fff,
        "Moon identity cache item indices must fit in s16.");
    // There is one Cloud-moon reward slot per non-free painting link.  Keep the
    // assignment in LogicState rather than in the global painting table while
    // probing bundles, so rejected branches cannot leak a link choice.
    static constexpr s32 kCloudPaintingSlotCount = LD::sPaintingLinkCount;
    static constexpr s32 kMaxCloudPaintingUnlocksPerMoon = 3;
    // Upper bound on Subarea Key slots: the finest mode cuts one key per
    // (kingdom, gateable door kind) pair that actually exists. Sized to the
    // full cross product so the mode can never overrun the state array.
    static constexpr s32 kMaxSubareaKeySlots
        = LD::sKingdomCount * SubareaKeyDB::sGatedTypeCount;
    static constexpr s32 kMaxAttempts = 32;
    static constexpr s32 kMaxSearchNodes = 8192;
    static constexpr s32 kMaxPlacementEntries = LD::sMoonCount > ShineDB::sShineTableSize ? LD::sMoonCount : ShineDB::sShineTableSize;
    static constexpr s32 kMoonAllocationMaxFloorRetries = 32;
    static constexpr s32 kMoonAllocationProfileShuffleRetries = 512;
    static constexpr s32 kMoonAllocationCloudCarrierMinimum = 10;
    static constexpr s32 kMoonAllocationTightExcessMax = 20;
    // Neither ability kingdom may be emptied when the pair's vanilla total is
    // split between them. Three is one triple, or three singles.
    static constexpr s32 kMoonAllocationAbilityKingdomMinimum = 3;
    static u32 captureCarrierKingdomMask();
    static u32 abilityCarrierKingdomMask();
    static u32 paintingCarrierKingdomMask();
    // A kingdom whose moons carry a capture or ability reward rather than being
    // ordinary collectibles. Used to be the fixed Mushroom/Dark/Darker triple.
    static bool isRewardCarrierKingdom(s32 kingdom);

    static constexpr u32 kMoonAllocationAllKingdomsMask
        = (1u << LD::sKingdomCount) - 1u;
    static constexpr s32 kMoonAllocationNativeTarget[LD::sKingdomCount] = {
        31,
        42,
        93,
        80,
        44,
        9,
        35,
        85,
        73,
        57,
        72,
        12,
        64,
        38,
        55,
        26,
        3,
    };
    static constexpr s32 kMoonAllocationMinimumValue[LD::sKingdomCount] = {
        5,
        15,
        26,
        26,
        18,
        5,
        20,
        30,
        20,
        20,
        28,
        13,
        18,
        5,
        5,
        5,
        5,
    };

    // Easy keeps the original distributed allocator's extra safety headroom.
    // Vanilla and harder profiles use LD::sMoonRequirementsByKingdom directly
    // so their shuffled excess means total credited moons minus the base gate.
    static s32 moonAllocationMinimumValue(s32 kingdom) {
        const s32 defaultMediumMaximum
            = RequirementRando::getDefaultMediumMaximum(kingdom);
        if (defaultMediumMaximum > 0)
            return defaultMediumMaximum + 5;
        return kMoonAllocationMinimumValue[kingdom];
    }
    static constexpr s32 kHighBufferThreshold = 10;
    static constexpr s32 kLowBufferThreshold = 3;
    // Even at low buffer, a large backlog of required moons is worth trying
    // to spread across an expanded frontier rather than drawing repeatedly
    // from whatever small pool is currently reachable.
    static constexpr s32 kLowBufferExpansionThreshold = 6;

    // ---------------------------------------------------------------
    // Seed axis tables
    //
    // Every table below is indexed by the RESOLVED axis level 0..4
    // (Randomizer::cSeedAxisLevelCount), and index 2 is always the historical
    // Standard value. Random is resolved by Randomizer before it reaches here,
    // so nothing in the fill ever sees index 5.
    // ---------------------------------------------------------------

    static s32 clampAxisLevel(s32 level) {
        if (level < 0)
            return 0;
        if (level >= Randomizer::cSeedAxisLevelCount)
            return Randomizer::cSeedAxisLevelCount - 1;
        return level;
    }

    static s32 opennessLevel() {
        return clampAxisLevel(Randomizer::getResolvedSeedOpennessLevel());
    }

    static s32 unlockPacingLevel() {
        return clampAxisLevel(Randomizer::getResolvedUnlockPacingLevel());
    }

    static s32 seedDifficultyLevel() {
        return clampAxisLevel(Randomizer::getResolvedSeedDifficultyLevel());
    }

    // Buffer width: how much slack the fill insists on before it spends a
    // placement. A low-openness seed keeps working inside the pool it already
    // has; a high-openness seed pushes the frontier outward constantly.
    static s32 highBufferThreshold() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount]
            = { 12, 10, kHighBufferThreshold, 14, 16 };
        return kByOpenness[opennessLevel()];
    }

    static s32 lowBufferThreshold() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount]
            = { 8, 6, kLowBufferThreshold, 2, 1 };
        return kByOpenness[opennessLevel()];
    }

    static s32 lowBufferExpansionThreshold() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount]
            = { 12, 10, kLowBufferExpansionThreshold, 4, 3 };
        s32 value = kByOpenness[opennessLevel()];
        // Front-loaded pacing wants unlocks placed eagerly, which means
        // expanding rather than consuming a required moon far more often; a
        // back-loaded seed wants the opposite. Scale, then keep it >= 1 so the
        // branch can still fire at all.
        //
        // This is a THRESHOLD the backlog has to clear (see
        // shouldExpandBeforeRequiredMoon: unplacedRequirements > E), so
        // "expand more eagerly" means a SMALLER E. Front-loaded therefore
        // DIVIDES and back-loaded MULTIPLIES - the tables read backwards from
        // the eagerness they produce, and swapping them silently inverts the
        // whole axis.
        static constexpr s32 kNumByPacing[Randomizer::cSeedAxisLevelCount] = { 1, 1, 1, 2, 4 };
        static constexpr s32 kDenByPacing[Randomizer::cSeedAxisLevelCount] = { 4, 2, 1, 1, 1 };
        const s32 pacing = unlockPacingLevel();
        value = value * kNumByPacing[pacing] / kDenByPacing[pacing];
        return value < 1 ? 1 : value;
    }

    // How many reachable-but-unwired entrance sources the fill refuses to
    // consume on a discretionary wiring, so the player always has this many
    // unexplored loading zones standing open. An Expand move ignores it -
    // being stuck outranks keeping options in reserve.
    static s32 openDoorReserve() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount] = { 0, 1, 2, 4, 6 };
        return kByOpenness[opennessLevel()];
    }

    // Weight on drawing the next wiring source from the chain-exit pool rather
    // than a fresh kingdom entrance, as num:den. Long chains are LESS open:
    // with a fixed door count, short chains mean more chains, so more
    // independent overworld entry points and more direct kingdom-to-kingdom
    // doors. Compact reproduces the retired Long Chains mode and Open the
    // retired Small Chains mode.
    static void chainBiasForOpenness(s32* biasNum, s32* biasDen) {
        static constexpr s32 kNum[Randomizer::cSeedAxisLevelCount] = { 4, 3, 1, 1, 1 };
        static constexpr s32 kDen[Randomizer::cSeedAxisLevelCount] = { 1, 1, 1, 3, 4 };
        const s32 level = opennessLevel();
        *biasNum = kNum[level];
        *biasDen = kDen[level];
    }

    static bool isMoonKingdomLink(s32 index) {
        return index >= 0 && index < EntranceDB::sLinkCount && std::strcmp(EntranceDB::sLinks[index].homeStage, "MoonWorldHomeStage") == 0;
    }

    static bool isCloudKingdomLink(s32 index) {
        return index >= 0 && index < EntranceDB::sLinkCount && std::strcmp(EntranceDB::sLinks[index].homeStage, "CloudWorldHomeStage") == 0;
    }

    // The graph editor only exposes scenario toggles 1-5, so an authored route
    // edge's option scenarios can never restrict beyond 5. Any kingdom scenario
    // greater than this (e.g. Moon Rock scenarios like 8) is therefore treated as
    // available for that path. Mirrors randomizer_logic.py MAX_EDITOR_SCENARIO.
    // Scenario masks are honoured at every scenario. There used to be a
    // kMaxEditorScenario = 5 ceiling here: the graph editor only exposed toggles
    // 1-5, so any kingdom scenario above 5 ignored the mask entirely on the
    // grounds that the author could not have excluded it. The editor now offers
    // every scenario a kingdom can reach, and open-endedness is baked into the
    // mask itself by randomizer_logic.extend_top_scenario ("sticky top": the
    // highest checked scenario and everything above it stay set). Gaps below the
    // top are deliberate and are preserved - Metro shuts these routes during its
    // scenario-3 festival state, Luncheon and Sand during scenario 2.

    static bool isEntranceRandomizable(s32 index) {
        if (index < 0 || index >= EntranceDB::sLinkCount || index >= LD::sEntranceCount)
            return false;
        if (EntranceRando::isLinkPinnedToVanilla(index))
            return false;
        if (isMoonKingdomLink(index) && !Randomizer::isMoonKingdomRequirementsEnabled())
            return false;
        if (isCloudKingdomLink(index) && !Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return false;
        return LD::sEntrances[index].randomizable;
    }

    // Whether a door participates in reachability at all. Every loading zone is a
    // two-way connection to its (vanilla or shuffled) destination, so this includes
    // FIXED/vanilla doors that isEntranceRandomizable() excludes (randomizable is
    // only about shuffle-pool membership). Directional one-way restrictions are
    // expressed on the nodes' route edges ("out free, in blocked"), not here.
    static bool isEntranceConnected(s32 index) {
        if (index < 0 || index >= EntranceDB::sLinkCount || index >= LD::sEntranceCount)
            return false;
        if (!EntranceDB::isRuntimeUsableLink(index))
            return false;
        if (isMoonKingdomLink(index) && !Randomizer::isMoonKingdomRequirementsEnabled())
            return false;
        if (isCloudKingdomLink(index) && !Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return false;
        return true;
    }

    struct CreditEntry {
        s32 srcUid;
        const char* srcStage;
        const ShineDB::ShineEntry* dest;
    };

    enum StageTraceKind : u8
    {
        StageTrace_None,
        StageTrace_Odyssey,
        StageTrace_Entrance,
        StageTrace_Painting,
        StageTrace_Inferred,
    };

    enum NodeSeedKind : u8
    {
        NodeSeed_None,
        NodeSeed_StageStart,
        NodeSeed_Arrival,
        NodeSeed_FreeStage,
    };

    struct StageTrace {
        StageTraceKind kind;
        s16 sourceStage;
        s16 sourceEntrance;
        s16 destEntrance;
        s16 entranceChainFirstLink;
        u8 entranceChainFirstKind;
    };

    struct NodeTrace {
        bool reached;
        s16 previousNode;
        s16 edgeIndex;
        NodeSeedKind seedKind;
    };

    struct LogicState {
        bool accessibleKingdoms[LD::sKingdomCount];
        bool accessibleStages[LD::sStageCount];
        bool accessibleNodes[LD::sNodeCount];
        bool accessibleSubEntryNear[EntranceDB::sLinkCount];
        bool accessibleSubEntryFar[EntranceDB::sLinkCount];
        bool tokens[LD::sTokenCount];
        bool unlockedPaintings[LD::sPaintingLinkCount];
        s16 cloudPaintingLinkBySlot[kCloudPaintingSlotCount];
        bool moonRockKeysCollected[LD::sKingdomCount];
        // One flag per Subarea Key slot (see SubareaKeyDB::keySlotAt). The mode
        // decides how coarsely a slot covers (kingdom, door kind) pairs, so
        // every consumer can just ask hasSubareaKey and stay mode-agnostic.
        bool subareaKeysCollected[kMaxSubareaKeySlots];
        bool collectedItems[LD::sMoonCount];
        bool collectedLocations[LD::sMoonCount];
        // Randomized Starting Position: the stage the run opens in, or -1. Both of
        // that stage's doors count as exits there; see expandStageAccessFromEntrances.
        s16 startExitStage;
        s16 kingdomScenarios[LD::sKingdomCount];
        s16 moonCounts[LD::sKingdomCount];
        s16 storyCounts[LD::sKingdomCount];
        StageTrace stageTraces[LD::sStageCount];
    };

    static const ShineDB::ShineEntry* sMoonEntries[LD::sMoonCount];
    // Inverses of sMoonEntries for canonical ShineDB pointers and uid+stage
    // identities. Role assignment scans (notably Cloud painting slots) query
    // the assigned kingdom repeatedly; keep those lookups O(1) instead of
    // nesting a moon-table scan inside every ShineDB-table sweep. The identity
    // cache is required across translation units because ShineDB's static table
    // has one header-local instance per source file.
    static s16 sMoonItemByShineTableIndex[ShineDB::sShineTableSize];
    static s16 sMoonItemByIdentitySlot[kMoonIdentityCacheCapacity];
    static bool sMoonEntryIndexCacheReady = false;
    // "Randomize Triple Moons": per-seed reshuffle of which moons pay out as a
    // triple/grand (gives 3). When active, sRewardMultiOverride replaces the
    // vanilla ShineFlag_Multi for reward/visual/pool/placement purposes only.
    // Scenario/story progression stays on the vanilla flag (isVanillaMultiMoon).
    static bool sRewardMultiOverride[LD::sMoonCount];
    static bool sRewardMultiOverrideActive = false;
    // Generic moon->kingdom redistribution mapping. A pre-processing step (before
    // any role assignment) that reassigns which kingdom each moon belongs to, so a
    // kingdom's moon list/counts/requirements reflect the reshuffled pool rather
    // than vanilla home kingdoms. Default = home kingdom. It is driven by the
    // full Moon Allocation engine when active, or by the legacy Randomize Triple
    // Moons rebalance otherwise.
    static s16 sMoonAssignedKingdom[LD::sMoonCount];
    static bool sKingdomAssignmentActive = false;
    static KingdomAllocationSummary sKingdomAllocationSummaries[LD::sKingdomCount];
    static bool sKingdomAllocationSummaryReady = false;
    static u32 sMoonRoleLayoutHash = 0;
    static s16 sMoonUnlockTokens[LD::sMoonCount][kMaxUnlockTokensPerMoon];
    static s8 sMoonUnlockTokenCounts[LD::sMoonCount];
    static s16 sMoonCloudPaintingSlots[LD::sMoonCount][kMaxCloudPaintingUnlocksPerMoon];
    static s16 sMoonPaintingUnlocks[LD::sMoonCount][kMaxCloudPaintingUnlocksPerMoon];
    static s8 sMoonCloudPaintingSlotCounts[LD::sMoonCount];
    static bool sFreePaintingLinks[LD::sPaintingLinkCount];
    static s16 sFinalCloudPaintingLinkBySlot[kCloudPaintingSlotCount];
    static bool sFinalFreePaintingLinks[LD::sPaintingLinkCount];
    static bool sCloudPaintingAssignmentsReady = false;
    static u32 sCloudPaintingAssignmentTieSeed = 0;
    static s16 sMoonRockKeyItemByKingdom[LD::sKingdomCount];
    static s16 sMoonRockKeyKingdomByItem[LD::sMoonCount];
    // Subarea Keys, keyed by SubareaKeyDB slot. Like Moon Rock Keys these are
    // riders: the designated moon still pays out as a moon, it just also grants
    // its key, so designating carriers costs the pool nothing.
    static s16 sSubareaKeyItemBySlot[kMaxSubareaKeySlots];
    static s16 sSubareaKeySlotByItem[LD::sMoonCount];
    // Every slot the current mode asks for holds a distinct carrier and the two
    // tables above agree about it. A layout that is anything less is broken, not
    // partially usable: presentation refuses to show it at all, and the gameplay
    // gate falls open rather than sealing doors nothing can ever unlock.
    static bool sSubareaKeyLayoutComplete = false;
    static bool sIsToadetteMedalItem[LD::sMoonCount];
    // Guards the one-shot warp in pure ToadetteMedals mode against re-triggering
    // on every later medal pickup once the threshold is first reached.
    static bool sToadetteMedalWinTriggered = false;

    static s32 sPlacementItemForLocation[LD::sMoonCount];
    // Logical depth at which each location first joined a reachable frontier.
    // Unlike the old placement-step counter, this advances only when reachability
    // genuinely exposes at least one new location.
    static s16 sLocationSphere[LD::sMoonCount];
    static Placement sPlacements[kMaxPlacementEntries];
    static bool sPlacementCritical[kMaxPlacementEntries];
    static s32 sPlacementCount = 0;
    // Inverse of sPlacements, by moon-table index. getPlacementForItem and
    // getItemForLocation are per-row presentation lookups (list row text, hint
    // footers, the tracker), so the shine list ran their linear scan for every
    // visible row every frame. Moon identity already has an O(1) index
    // (findMoonItemByIdentity), so these only need the last hop.
    static s16 sPlacementIndexForItem[LD::sMoonCount];
    static s16 sPlacementIndexForLocation[LD::sMoonCount];
    // Locations filled by the buffer engine before even cleanup drops the leftover
    // moons. Captured during generation; on a
    // restored save (where the solver is not re-run) sCriticalPathValid stays false and
    // the critical flag falls back to a heuristic on the placed item.
    static bool sCriticalPathLocation[LD::sMoonCount];
    static bool sCriticalPathValid = false;
    static s16 sAutoGrantItems[kMaxAutoGrants];
    static s32 sAutoGrantCount = 0;
    static PathStep sPathSteps[LD::sMoonCount];
    static s32 sPathStepCount = 0;

    static PathStep sOptimizedPathSteps[LD::sMoonCount];
    static s32 sOptimizedPathStepCount = 0;
    static SphereInfo sSpheres[kMaxSpheres];
    static s32 sSphereCount = 0;
    static CreditEntry sCreditMap[kMaxPlacementEntries];
    static s32 sCreditMapCount = 0;
    static char sWarnings[kMaxWarnings][kWarningSize];
    static s32 sWarningCount = 0;
    static char sFinalObjectiveNote[kWarningSize];
    static bool sFinalObjectiveReached = false;
    static FailureCode sFailureCode = FailureCode::None;
    static char sFailureMessage[kWarningSize] = "No generation failure.";
    static s32 sSearchNodes = 0;
    static s32 sSearchBundlesTried = 0;
    static GenerationAttemptInfo sGenerationAttempts[kMaxAttempts] = {};
    static s32 sGenerationAttemptCount = 0;
    static u32 sGenerationDurationMillis = 0;

    static bool sInitialized = false;
    static bool sUsedLogicPlacement = false;
    static bool sLogicSucceeded = false;
    // No-logic mode runs the same buffer-managed fill but ignores node restrictions
    // (route-edge requirements, per-node reachability, moon ability/capture
    // requirements). Kingdom access, scenarios and story order still apply, so the
    // fill can still verify enough accessible moons exist per kingdom.
    static bool sNoLogicFill = false;
    // Seed Difficulty C3, the graded successor to the Prioritize Difficult
    // Movement checkbox. While the fill runs, requirements offering both a
    // casual and a harder alternative within the selected logic difficulty can
    // be forced onto the harder one, so the progression chain is built through
    // difficult movement. sCasualFallbackActive temporarily re-allows the casual
    // alternatives when the fill is completely stuck.
    //
    // 0 Relaxed / 1 Easy   - do not mask; PREFER casual expansions instead.
    // 2 Standard           - neutral, the historical default.
    // 3 Hard               - mask casual (the old checkbox, on).
    // 4 Brutal             - mask casual and expand through the hardest tier first.
    //
    // LOAD-BEARING: this is construction-only. It is cleared before final
    // validation and every post-generation consumer (tracker, hints, spoiler
    // replay), which must evaluate with ALL allowed methods so a beatable seed
    // is never rejected. -1 means "not in a fill".
    static s32 sDifficultFillLevel = -1;
    static bool sCasualFallbackActive = false;

    // True when the fill should refuse casual alternatives outright.
    static bool difficultFillMasks() { return sDifficultFillLevel >= 3; }
    // True when the fill should merely prefer harder expansions in its ordering.
    static bool difficultFillPrefersHard() { return sDifficultFillLevel >= 3; }
    // True when the fill should prefer expansions that work casually.
    static bool difficultFillPrefersCasual() {
        return sDifficultFillLevel >= 0 && sDifficultFillLevel <= 1;
    }
    struct LogicSettingsSnapshot {
        Randomizer::EntranceRandoMode entranceMode;
        Randomizer::ShopLocationMode shopLocationMode;
        Randomizer::MoonDifficulty moonDifficulty;
        Randomizer::LogicDifficulty logicDifficulty;
        u32 moonRequirement;
        Randomizer::StartingEquipment startingEquipment;
        Randomizer::CaptureAbilityDifficulty captureAbilityDifficulty;
        // All three seed axes, tagged. Covers Unlock Pacing, Seed Openness and
        // Seed Difficulty in one field, so no axis can change without
        // invalidating a cached placement.
        u16 seedAxesPacked;
        Randomizer::SpecialMoonUse capMoonUse;
        Randomizer::SpecialMoonUse cloudMoonUse;
        Randomizer::SpecialMoonUse moonMoonUse;
        Randomizer::SpecialMoonUse darkMoonUse;
        Randomizer::SpecialMoonUse darkerMoonUse;
        Randomizer::SpecialMoonUse mushroomMoonUse;
        u8 capMoonUseMask;
        u8 cloudMoonUseMask;
        u8 moonMoonUseMask;
        u8 darkMoonUseMask;
        u8 darkerMoonUseMask;
        u8 mushroomMoonUseMask;
        bool moonReallocation;
        bool randomizeMoonAllocation;
        Randomizer::MoonAllocationDifficulty moonAllocationDifficulty;
        Randomizer::MoonCoinPolicy moonCoinPolicy;
        bool captureRando;
        bool abilityRando;
        bool paintingRando;
        bool kingdomRando;
        bool logicRando;
        bool moonKingdomRequirements;
        Randomizer::WorldPeaceRequirementMode worldPeaceRequirement;
        bool cloudPaintingRequirement;
        bool moonRockKey;
        Randomizer::KeyScope moonRockKeyScope;
        Randomizer::SubareaUnlockMode subareaUnlockMode;
        Randomizer::SubareaUnlockHost subareaUnlockHost;
        Randomizer::KeyScope subareaKeyScope;
        bool startKingdomStateRando;
        Randomizer::VisitedKingdomsMapMode visitedKingdomsMapMode;
        u32 startKingdomsMask;
        s16 moonRequirementDeviation;
        Randomizer::StartPositionMode startPositionMode;
        bool tripleMoonsAnywhere;
        bool randomizeTripleMoons;
        bool enemySwapRando;
    };

    static u32 sLastSeed = 0xFFFFFFFF;
    static u32 sLastEntranceAttempt = 0;
    static bool sLastCaptureRandoEnabled = false;
    static bool sLastAbilityRandoEnabled = false;
    static bool sLastEntranceRandoEnabled = false;
    static bool sLastPaintingRandoEnabled = false;
    static bool sLastKingdomRandoEnabled = false;
    static bool sLastLogicRandoEnabled = false;
    static Randomizer::MoonDifficulty sLastMoonDifficulty = Randomizer::MoonDifficulty_Medium;
    static Randomizer::LogicDifficulty sLastLogicDifficulty = Randomizer::LogicDifficulty_Casual;
    static LogicSettingsSnapshot sLastSettings = {};
    static bool sHasLastSettings = false;

    // Cache of whether each link's sub-area can be crossed entry->exit, per entry
    // side. Invalidated when the logic difficulty changes (it gates which edges are
    // usable). -1 = unknown, 0 = no, 1 = yes.
    static s8 sSubTraversalCache[EntranceDB::sLinkCount][2];
    static s32 sSubTraversalCacheDifficulty = -1;

    static bool strEq(const char* left, const char* right) {
        if (!left || !right)
            return false;
        return std::strcmp(left, right) == 0;
    }

    static void addWarning(const char* fmt, ...) {
        if (sWarningCount >= kMaxWarnings)
            return;

        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(sWarnings[sWarningCount], kWarningSize, fmt, args);
        va_end(args);

        Randomizer::logPush("%s", sWarnings[sWarningCount]);
        sWarningCount++;
    }

    static void setFailure(FailureCode code, const char* fmt, ...) {
        sFailureCode = code;
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(sFailureMessage, sizeof(sFailureMessage), fmt, args);
        va_end(args);
        Randomizer::logPush("moon generation failure[%d]: %s", static_cast<s32>(code), sFailureMessage);
    }

    static u32 elapsedMillisSince(u64 startTick) {
        // Horizon's system tick is the 19.2 MHz ARM generic counter.  The title
        // exports GetSystemTick(), but not the SDK's GetSystemTickFrequency()
        // wrapper, so keep the conversion local rather than introducing an
        // unresolved SDK symbol.
        static constexpr u64 kSystemTickFrequency = 19200000ull;
        const u64 now = nn::os::GetSystemTick().value;
        if (now < startTick)
            return 0;

        const u64 elapsedTicks = now - startTick;
        const u64 millis = (elapsedTicks / kSystemTickFrequency) * 1000u
            + ((elapsedTicks % kSystemTickFrequency) * 1000u)
                / kSystemTickFrequency;
        return millis > 0xFFFFFFFFull ? 0xFFFFFFFFu : static_cast<u32>(millis);
    }

    static void recordGenerationAttempt(
        u32 attempt,
        u32 dependencyMillis,
        u32 placementMillis,
        bool succeeded) {
        if (sGenerationAttemptCount >= kMaxAttempts)
            return;

        GenerationAttemptInfo& info = sGenerationAttempts[sGenerationAttemptCount++];
        std::memset(&info, 0, sizeof(info));
        info.attempt = attempt;
        info.dependencyMillis = dependencyMillis;
        info.placementMillis = placementMillis;
        info.searchNodes = sSearchNodes;
        info.searchBundles = sSearchBundlesTried;
        info.result = succeeded ? FailureCode::None : sFailureCode;
        info.succeeded = succeeded;
        const char* message = succeeded ? "Succeeded." : sFailureMessage;
        std::strncpy(info.message, message ? message : "No failure message.",
            sizeof(info.message) - 1);
        info.message[sizeof(info.message) - 1] = '\0';

        Randomizer::logPush(
            "logic attempt=%u result=%s deps_ms=%u placement_ms=%u nodes=%d bundles=%d reason=%s",
            attempt, succeeded ? "success" : "rejected", dependencyMillis,
            placementMillis, info.searchNodes, info.searchBundles,
            info.message);
    }

    static LogicSettingsSnapshot captureLogicSettings() {
        LogicSettingsSnapshot settings = {};
        settings.entranceMode = Randomizer::getEntranceRandoMode();
        settings.shopLocationMode = Randomizer::getShopLocationMode();
        settings.moonDifficulty = Randomizer::getMoonDifficulty();
        settings.logicDifficulty = Randomizer::getLogicDifficulty();
        settings.moonRequirement = Randomizer::getMoonRequirement();
        settings.startingEquipment = Randomizer::getStartingEquipment();
        settings.captureAbilityDifficulty = Randomizer::getCaptureAbilityDifficulty();
        settings.seedAxesPacked = Randomizer::getSeedAxesPacked();
        settings.capMoonUse = Randomizer::getCapKingdomMoonUse();
        settings.cloudMoonUse = Randomizer::getCloudKingdomMoonUse();
        settings.moonMoonUse = Randomizer::getMoonKingdomMoonUse();
        settings.capMoonUseMask = Randomizer::getCapKingdomMoonUseMask();
        settings.cloudMoonUseMask = Randomizer::getCloudKingdomMoonUseMask();
        settings.moonMoonUseMask = Randomizer::getMoonKingdomMoonUseMask();
        settings.darkMoonUse = Randomizer::getDarkKingdomMoonUse();
        settings.darkerMoonUse = Randomizer::getDarkerKingdomMoonUse();
        settings.mushroomMoonUse = Randomizer::getMushroomKingdomMoonUse();
        settings.darkMoonUseMask = Randomizer::getDarkKingdomMoonUseMask();
        settings.darkerMoonUseMask = Randomizer::getDarkerKingdomMoonUseMask();
        settings.mushroomMoonUseMask = Randomizer::getMushroomKingdomMoonUseMask();
        settings.moonReallocation = Randomizer::isMoonReallocationEnabled();
        settings.randomizeMoonAllocation = Randomizer::isRandomizeMoonAllocationEnabled();
        settings.moonAllocationDifficulty = Randomizer::getMoonAllocationDifficulty();
        settings.moonCoinPolicy = Randomizer::getMoonCoinPolicy();
        settings.captureRando = Randomizer::isCaptureRandoEnabled();
        settings.abilityRando = Randomizer::isLockedAbilitiesEnabled();
        settings.paintingRando = Randomizer::isPaintingRandoEnabled();
        settings.kingdomRando = Randomizer::isKingdomRandoEnabled();
        settings.logicRando = Randomizer::isLogicRandoEnabled();
        settings.moonKingdomRequirements = Randomizer::isMoonKingdomRequirementsEnabled();
        settings.worldPeaceRequirement = Randomizer::getWorldPeaceRequirementMode();
        settings.cloudPaintingRequirement = Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled();
        settings.moonRockKey = Randomizer::isMoonRockKeyEnabled();
        settings.moonRockKeyScope = Randomizer::getMoonRockKeyScope();
        settings.subareaUnlockMode = Randomizer::getSubareaUnlockMode();
        settings.subareaUnlockHost = Randomizer::getSubareaUnlockHost();
        settings.subareaKeyScope = Randomizer::getSubareaKeyScope();
        settings.startKingdomStateRando = Randomizer::isStartKingdomStateRandoEnabled();
        settings.visitedKingdomsMapMode = Randomizer::getVisitedKingdomsMapMode();
        settings.startKingdomsMask = Randomizer::getStartKingdomsMask();
        settings.moonRequirementDeviation = Randomizer::getMoonRequirementDeviation();
        settings.startPositionMode = Randomizer::getStartPositionMode();
        settings.tripleMoonsAnywhere = Randomizer::isTripleMoonsAnywhereEnabled();
        settings.randomizeTripleMoons = Randomizer::isRandomizeTripleMoonsEnabled();
        settings.enemySwapRando = Randomizer::isEnemySwapRandoEnabled();
        return settings;
    }

    static bool settingsEqual(const LogicSettingsSnapshot& left, const LogicSettingsSnapshot& right) {
        const bool equal = left.entranceMode == right.entranceMode
            && left.shopLocationMode == right.shopLocationMode
            && left.moonDifficulty == right.moonDifficulty
            && left.logicDifficulty == right.logicDifficulty
            && left.moonRequirement == right.moonRequirement
            && left.startingEquipment == right.startingEquipment
            && left.captureAbilityDifficulty == right.captureAbilityDifficulty
            && left.seedAxesPacked == right.seedAxesPacked
            && left.capMoonUse == right.capMoonUse
            && left.cloudMoonUse == right.cloudMoonUse
            && left.moonMoonUse == right.moonMoonUse
            && left.capMoonUseMask == right.capMoonUseMask
            && left.cloudMoonUseMask == right.cloudMoonUseMask
            && left.moonMoonUseMask == right.moonMoonUseMask
            && left.moonReallocation == right.moonReallocation
            && left.randomizeMoonAllocation == right.randomizeMoonAllocation
            && left.moonAllocationDifficulty == right.moonAllocationDifficulty
            && left.moonCoinPolicy == right.moonCoinPolicy
            && left.captureRando == right.captureRando
            && left.abilityRando == right.abilityRando
            && left.paintingRando == right.paintingRando
            && left.kingdomRando == right.kingdomRando
            && left.logicRando == right.logicRando
            && left.moonKingdomRequirements == right.moonKingdomRequirements
            && left.worldPeaceRequirement == right.worldPeaceRequirement
            && left.cloudPaintingRequirement == right.cloudPaintingRequirement
            && left.moonRockKey == right.moonRockKey
            && left.moonRockKeyScope == right.moonRockKeyScope
            && left.subareaUnlockMode == right.subareaUnlockMode
            && left.subareaUnlockHost == right.subareaUnlockHost
            && left.subareaKeyScope == right.subareaKeyScope
            && left.startKingdomStateRando == right.startKingdomStateRando
            && left.visitedKingdomsMapMode == right.visitedKingdomsMapMode
            && left.startKingdomsMask == right.startKingdomsMask
            && left.startPositionMode == right.startPositionMode
            && left.tripleMoonsAnywhere == right.tripleMoonsAnywhere
            && left.randomizeTripleMoons == right.randomizeTripleMoons
            && left.enemySwapRando == right.enemySwapRando;
        if (!equal)
            return false;
        if (left.moonRequirementDeviation != right.moonRequirementDeviation)
            return false;
        return true;
    }

    static s32 findStageIndex(const char* stage) {
        if (!stage || !stage[0])
            return -1;
        for (s32 i = 0; i < LD::sStageCount; i++)
            if (strEq(LD::sStages[i].stage, stage))
                return i;
        return -1;
    }

    static s32 findTokenIndex(const char* key) {
        if (!key || !key[0])
            return -1;
        for (s32 i = 0; i < LD::sTokenCount; i++)
            if (strEq(LD::sTokens[i].key, key))
                return i;
        return -1;
    }

    static s16 sLinkHomeStageIndex[EntranceDB::sLinkCount];
    static s16 sLinkSubStageIndex[EntranceDB::sLinkCount];
    static bool sLinkStageIndexesReady = false;

    static void ensureLinkStageIndexes() {
        if (sLinkStageIndexesReady)
            return;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            sLinkHomeStageIndex[i] = (s16)findStageIndex(EntranceDB::sLinks[i].homeStage);
            sLinkSubStageIndex[i] = (s16)findStageIndex(EntranceDB::sLinks[i].subStage);
        }
        sLinkStageIndexesReady = true;
    }

    static bool isValidKingdom(s32 kingdom) {
        return kingdom >= 0 && kingdom < LD::sKingdomCount;
    }

    // Cached because the entrance fill asks per candidate door, per wiring.
    // -1 = not computed yet.
    static s8 sLinkSubHasLocations[EntranceDB::sLinkCount];
    static bool sLinkSubHasLocationsReady = false;

    static bool isValidStage(s32 stage) {
        return stage >= 0 && stage < LD::sStageCount;
    }

    static bool isValidMoon(s32 moon) {
        return moon >= 0 && moon < LD::sMoonCount && sMoonEntries[moon];
    }

    static s32 routeKingdomCount() {
        return KingdomRando::getRouteWorldCount();
    }

    static s32 routeKingdomAt(s32 index) {
        return KingdomRando::getRouteWorldId(index);
    }

    // Reward-multi status of an ITEM: whether collecting this moon pays out as a
    // triple (gives 3, grand visual). Honors the "Randomize Triple Moons" override
    // when active; otherwise the vanilla ShineFlag_Multi. Drives reward magnitude,
    // moonValue and which items are the triples in the pool. This is the item/
    // reward role only -- physical triple SLOTS use isMultiSlot (vanilla-fixed).
    static bool isMultiMoon(s32 moon) {
        if (!isValidMoon(moon))
            return false;
        if (sRewardMultiOverrideActive)
            return sRewardMultiOverride[moon];
        return (sMoonEntries[moon]->flags & ShineDB::ShineFlag_Multi) != 0;
    }

    // Vanilla grand status of the physical location, independent of the reward
    // override. Scenario/story progression stays pinned to these locations even
    // when "Randomize Triple Moons" moves the gives-3 reward elsewhere.
    static bool isVanillaMultiMoon(s32 moon) {
        return isValidMoon(moon) && (sMoonEntries[moon]->flags & ShineDB::ShineFlag_Multi);
    }

    // Whether a physical LOCATION is a triple/grand slot. Fixed to the vanilla
    // grand spots: "Randomize Triple Moons" reshuffles which items are triples
    // (the pool), NOT which locations are grand, so the solver still places the
    // (randomized) triple items into the same 22 vanilla grand locations.
    static bool isMultiSlot(s32 location) {
        return isVanillaMultiMoon(location);
    }

    static bool canPlaceMoonItemAtLocation(s32 item, s32 location) {
        return Randomizer::isTripleMoonsAnywhereEnabled()
            || isMultiMoon(item) == isMultiSlot(location);
    }

    static bool isStoryMoon(s32 moon) {
        return isValidMoon(moon) && (sMoonEntries[moon]->flags & ShineDB::ShineFlag_Story);
    }

    // Scripted scenario exceptions: a single (non-triple) story beat that advances
    // ScenarioNo without a grand moon. Metro's festival is scripted to begin once
    // the player has the Power Plant beat ("Powering Up the Station") + Pauline,
    // putting the kingdom into scenario 3. Mirrors randomizer_logic.py
    // SCRIPTED_SCENARIO_ADVANCE.
    static bool isScriptedScenarioAdvance(s32 kingdom, s32 target) {
        return kingdom == WorldMeta::WorldId_Metro && target == 3;
    }

    // Snow's Grand Prix moon is reached through a race sequence that begins in
    // scenario 1, but it is story order 4 after the four town barriers.  The
    // race itself must still promote Snow to scenario 3 even when those barriers
    // were collected out of order, matching the runtime scenario patcher.
    static bool isSnowGrandPrixScenarioAdvance(s32 location) {
        if (!isValidMoon(location))
            return false;
        const LD::MoonAccess& moon = LD::sMoons[location];
        return moon.kingdom == WorldMeta::WorldId_Snow && moon.uid == 1020;
    }

    static s32 moonValue(s32 moon) {
        return isMultiMoon(moon) ? 3 : 1;
    }

    static bool scenarioMaskContains(u32 mask, s16 scenario) {
        return scenario >= 0 && scenario < 32 && (mask & (1u << scenario)) != 0;
    }

    static bool isScenarioBlocked(s32 kingdom, s16 scenario) {
        if (!isValidKingdom(kingdom) || scenario < 0 || scenario >= 32)
            return false;
        if (LD::sMaxLogicScenarioByKingdom[kingdom] >= 0 && scenario > LD::sMaxLogicScenarioByKingdom[kingdom])
            return true;
        return (LD::sBlockedScenarioMaskByKingdom[kingdom] & (1u << scenario)) != 0;
    }

    static s16 normalizeScenarioTarget(s32 kingdom, s16 scenario) {
        if (!isValidKingdom(kingdom) || scenario <= 0)
            return -1;
        if (scenario == LD::sPeaceScenarioByKingdom[kingdom])
            scenario = LD::sPostGameScenarioByKingdom[kingdom];
        if (LD::sMaxLogicScenarioByKingdom[kingdom] >= 0 && scenario > LD::sMaxLogicScenarioByKingdom[kingdom])
            scenario = LD::sMaxLogicScenarioByKingdom[kingdom];
        if (isScenarioBlocked(kingdom, scenario))
            return -1;
        return scenario;
    }

    static bool advanceKingdomScenario(LogicState& state, s32 kingdom, s16 scenario) {
        scenario = normalizeScenarioTarget(kingdom, scenario);
        if (!isValidKingdom(kingdom) || scenario <= state.kingdomScenarios[kingdom])
            return false;
        state.kingdomScenarios[kingdom] = scenario;
        return true;
    }

    // Achievement (Toadette) moons are the only category excluded from the pool.
    // Hint-art moons and the Cloud/Moon/Mushroom/Dark-Side moons are always shuffled;
    // when a special kingdom's feature toggle is off, its moon items pay filler coins
    // on collection instead of granting their unlock (see MoonHooks).
    static bool isMoonPoolEligible(const ShineDB::ShineEntry& entry) {
        if (entry.uid == 0 || entry.objId < 0)
            return false;
        if (entry.flags & ShineDB::ShineFlag_Toadette)
            return false;
        return true;
    }

    static bool kingdomHasRealMoonRock(s32 kingdom) {
        return isValidKingdom(kingdom)
            && LD::sMoonRockScenarioByKingdom[kingdom] >= 0
            && LD::sMoonRockTokenByKingdom[kingdom] >= 0;
    }

    static void clearMoonRockKeys() {
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            sMoonRockKeyItemByKingdom[kingdom] = -1;
        for (s32 item = 0; item < LD::sMoonCount; item++)
            sMoonRockKeyKingdomByItem[item] = -1;
    }

    // Kingdom a moon belongs to after redistribution (home kingdom when off). Use
    // this for roster/list/count decisions; keep LD::sMoons[].kingdom for the
    // moon's physical/logic kingdom (reachability, scenarios).
    static s32 assignedKingdomOf(s32 moon) {
        if (!isValidMoon(moon))
            return -1;
        return sKingdomAssignmentActive ? sMoonAssignedKingdom[moon] : LD::sMoons[moon].kingdom;
    }

    static bool isMoonRockKeyCandidate(s32 item, s32 kingdom) {
        if (!Randomizer::isMoonRockKeyEnabled() || !isValidMoon(item))
            return false;
        // A kingdom that starts on its Moon Rock/Moon Cube already has that
        // progression event. Do not manufacture a key for a state the player
        // has already reached.
        if (LD::sMoonRockScenarioByKingdom[kingdom] > 0
            && Randomizer::getInitialKingdomScenario(kingdom)
                >= LD::sMoonRockScenarioByKingdom[kingdom])
            return false;
        // Roster decision: the rock key comes from the kingdom's reassigned pool so
        // it shows on that kingdom's page.
        if (Randomizer::getMoonRockKeyScope() == Randomizer::KeyScope::WithinKingdom
            && assignedKingdomOf(item) != kingdom)
            return false;
        if (sMoonRockKeyKingdomByItem[item] >= 0)
            return false;
        if (!kingdomHasRealMoonRock(kingdom))
            return false;
        return isMoonPoolEligible(*sMoonEntries[item]);
    }

    static void buildMoonRockKeys(u32 seed, u32 attempt) {
        clearMoonRockKeys();
        if (!Randomizer::isMoonRockKeyEnabled())
            return;

        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ (attempt * 0xA511E9B3u) ^ 0x4D524B59u,
            Randomizer::RngStream::Moon);

        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (!kingdomHasRealMoonRock(kingdom))
                continue;

            s32 candidates[LD::sMoonCount];
            s32 candidateCount = 0;
            for (s32 item = 0; item < LD::sMoonCount; item++) {
                if (isMoonRockKeyCandidate(item, kingdom))
                    candidates[candidateCount++] = item;
            }
            if (candidateCount <= 0)
                continue;

            const s32 item = candidates[rng.nextModulo(static_cast<u32>(candidateCount))];
            sMoonRockKeyItemByKingdom[kingdom] = static_cast<s16>(item);
            sMoonRockKeyKingdomByItem[item] = static_cast<s16>(kingdom);
        }
    }

    static s32 moonRockKeyKingdomForItem(s32 item) {
        if (!Randomizer::isMoonRockKeyEnabled() || item < 0 || item >= LD::sMoonCount)
            return -1;
        const s32 kingdom = sMoonRockKeyKingdomByItem[item];
        return kingdomHasRealMoonRock(kingdom) ? kingdom : -1;
    }

    static bool itemIsMoonRockKey(s32 item) {
        return moonRockKeyKingdomForItem(item) >= 0;
    }

    static void shuffle(s32* values, s32 count, Randomizer::Rng& rng) {
        for (s32 i = count - 1; i > 0; i--) {
            const s32 other = (s32)rng.nextModulo((u32)(i + 1));
            const s32 tmp = values[i];
            values[i] = values[other];
            values[other] = tmp;
        }
    }

    static void clearToadetteMedals() {
        for (s32 item = 0; item < LD::sMoonCount; item++)
            sIsToadetteMedalItem[item] = false;
    }

    // Eligible for the medal pool: a normal shuffle-pool moon whose *credited*
    // kingdom (which can differ from its home kingdom after redistribution,
    // independent of where it's physically placed) isn't Mushroom,
    // Dark Side or Darker Side. Moon Kingdom moons are eligible.
    static bool isToadetteMedalCandidate(s32 item) {
        if (!Randomizer::isToadetteMedalModeEnabled() || !isValidMoon(item))
            return false;
        const s32 kingdom = assignedKingdomOf(item);
        if (isRewardCarrierKingdom(kingdom))
            return false;
        return isMoonPoolEligible(*sMoonEntries[item]);
    }

    // How many medals one medal ITEM is worth. A triple pays out three moons, so
    // it pays out three medals; anything else is worth one. isMultiMoon is the
    // reward role (it honors "Randomize Triple Moons"), which is the same
    // predicate the get-demo uses to pick the grand model and payout, so the
    // medal a player is shown always matches the medals they are credited.
    static s32 toadetteMedalValueOf(s32 item) {
        return isMultiMoon(item) ? 3 : 1;
    }

    // Picks the medal pool from every eligible candidate via a seeded shuffle,
    // mirroring buildMoonRockKeys's per-seed selection. A pure function of
    // (seed, attempt, enabled, ratio) - no per-item save bytes are needed.
    //
    // The "total" half of the ratio setting counts MEDALS, not items: a triple
    // in the pool contributes three of them, so it consumes three of the
    // requested total rather than one. Candidates are taken in shuffled order
    // and any that would overshoot the target is skipped (there are always far
    // more single candidates left to finish the count exactly), so a seed with
    // triples in it still generates exactly as many medals as configured.
    static void buildToadetteMedals(u32 seed, u32 attempt) {
        clearToadetteMedals();
        if (!Randomizer::isToadetteMedalModeEnabled())
            return;

        s32 candidates[LD::sMoonCount];
        s32 candidateCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (isToadetteMedalCandidate(item))
                candidates[candidateCount++] = item;
        }
        if (candidateCount <= 0)
            return;

        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ (attempt * 0xB7E15163u) ^ 0x53484152u, // "SHAR"
            Randomizer::RngStream::Moon);
        shuffle(candidates, candidateCount, rng);

        const s32 target = Randomizer::getToadetteMedalRatio().total;
        s32 medals = 0;
        for (s32 i = 0; i < candidateCount && medals < target; i++) {
            const s32 value = toadetteMedalValueOf(candidates[i]);
            if (medals + value > target)
                continue;
            sIsToadetteMedalItem[candidates[i]] = true;
            medals += value;
        }
    }

    static void clearSubareaKeys() {
        for (s32 slot = 0; slot < kMaxSubareaKeySlots; slot++)
            sSubareaKeyItemBySlot[slot] = -1;
        for (s32 item = 0; item < LD::sMoonCount; item++)
            sSubareaKeySlotByItem[item] = -1;
        sSubareaKeyLayoutComplete = false;
    }

    // Subarea Keys ride on the host kingdom's moons (Cap and/or Moon): the
    // designated moon still pays out as a moon and simply also grants its key,
    // so this costs the pool nothing. The host choice decides which kingdom's
    // collection page the key lists on; the fill stays free to place the reward
    // anywhere reachable.
    static bool isSubareaKeyCandidate(s32 item) {
        if (!Randomizer::isSubareaUnlockEnabled() || !isValidMoon(item))
            return false;
        if (Randomizer::getSubareaKeyScope() == Randomizer::KeyScope::WithinKingdom
            && !SubareaKeyDB::isHostKingdom(assignedKingdomOf(item)))
            return false;
        // Reward roles stack. A moon may be a Hint Token source, a Moon Rock
        // Key, a Toadette Medal and a capture/ability carrier all at once - the
        // pickup overlay numbers them as one run and each collection list keys
        // off the moon's own identity - so a Subarea Key is free to share a
        // carrier with any of them. Only one Subarea Key SLOT per moon, which
        // buildSubareaKeys guarantees by dealing from a shuffled distinct set.
        // Non-randomized vanilla Toadette entries stay out via isMoonPoolEligible.
        return isMoonPoolEligible(*sMoonEntries[item]);
    }

    // Mirrors buildMoonRockKeys: a seeded shuffle over the eligible carriers, so
    // no per-key save bytes are needed and the assignment is a pure function of
    // (seed, attempt, mode, host).
    //
    // Returns false when the mode's slots cannot all be filled. There is no
    // usable middle ground: an unassigned slot reads back as ungated, so a
    // partial table is a seed whose doors silently disagree with its key list.
    // Randomizer::subareaHostFits already stops the file select from offering a
    // host too small for the mode, so reaching a failure here means the data or
    // a hand-edited save is wrong, and the caller must reject rather than ship it.
    static bool buildSubareaKeys(u32 seed, u32 attempt) {
        clearSubareaKeys();
        if (!Randomizer::isSubareaUnlockEnabled())
            return true;

        s32 candidates[LD::sMoonCount];
        s32 candidateCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (isSubareaKeyCandidate(item))
                candidates[candidateCount++] = item;
        }

        // hostMoonCapacity (which Randomizer::subareaHostFits clamps the file
        // select against) counts a kingdom's moons statically. Moon reallocation
        // can move moons off the host kingdoms afterwards, so the static promise
        // can still come up short here - name the mode and host so the readout
        // says which pair to widen rather than printing two raw enum values.
        const s32 slotCount = SubareaKeyDB::keySlotCount();
        if (slotCount > kMaxSubareaKeySlots || slotCount > candidateCount) {
            setFailure(FailureCode::CapacityFailure,
                "Subarea Keys (%s) need %d carriers but %s moons offer only %d.",
                Randomizer::getSubareaUnlockModeName(Randomizer::getSubareaUnlockMode()),
                slotCount,
                Randomizer::getSubareaUnlockHostName(Randomizer::getSubareaUnlockHost()),
                candidateCount);
            clearSubareaKeys();
            return false;
        }

        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ (attempt * 0x9E3779B1u) ^ 0x53554241u, // "SUBA"
            Randomizer::RngStream::Moon);
        shuffle(candidates, candidateCount, rng);

        for (s32 slot = 0; slot < slotCount; slot++) {
            const s32 item = candidates[slot];
            sSubareaKeyItemBySlot[slot] = static_cast<s16>(item);
            sSubareaKeySlotByItem[item] = static_cast<s16>(slot);
        }

        // The shuffle deals distinct items, so this can only trip on a data or
        // arithmetic fault - but the whole feature reads through these two
        // tables, so prove they agree before anything is allowed to trust them.
        for (s32 slot = 0; slot < slotCount; slot++) {
            const s32 item = sSubareaKeyItemBySlot[slot];
            if (isValidMoon(item) && sSubareaKeySlotByItem[item] == slot)
                continue;
            setFailure(FailureCode::DataError,
                "Subarea Key slot %d of %d has no consistent carrier "
                "(item %d, %s on %s moons, %d candidates).",
                slot, slotCount, item,
                Randomizer::getSubareaUnlockModeName(Randomizer::getSubareaUnlockMode()),
                Randomizer::getSubareaUnlockHostName(Randomizer::getSubareaUnlockHost()),
                candidateCount);
            clearSubareaKeys();
            return false;
        }

        sSubareaKeyLayoutComplete = true;
        return true;
    }

    static s32 subareaKeySlotForItem(s32 item) {
        if (!Randomizer::isSubareaUnlockEnabled() || item < 0 || item >= LD::sMoonCount)
            return -1;
        return sSubareaKeySlotByItem[item];
    }

    static bool itemIsSubareaKey(s32 item) { return subareaKeySlotForItem(item) >= 0; }

    // Whether doors of this kind are passable in this kingdom. An unassigned
    // slot (feature off, a pair with no gateable doors, or a layout that failed
    // validation) is always open - the same fail-open rule the live save-side
    // query uses, so the two sides never disagree about a broken table.
    static bool stateHasSubareaKey(const LogicState& state, s32 kingdom,
        SubareaKeyDB::SubareaType type) {
        if (!Randomizer::isSubareaUnlockEnabled() || !sSubareaKeyLayoutComplete)
            return true;
        const s32 slot = SubareaKeyDB::findKeySlot(kingdom, type);
        if (slot < 0 || slot >= kMaxSubareaKeySlots || sSubareaKeyItemBySlot[slot] < 0)
            return true;
        return state.subareaKeysCollected[slot];
    }

    // Entrance-side gate shared by both traversal paths. Keys on the link's HOME
    // side - the door the player physically touches - so entrance rando never
    // changes which key a door asks for.
    static bool stateCanEnterSubareaLink(const LogicState& state, s32 linkIndex) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return true;
        if (!SubareaKeyDB::isGatedLink(linkIndex))
            return true;
        return stateHasSubareaKey(
            state, SubareaKeyDB::kingdomForLink(linkIndex), SubareaKeyDB::typeForLink(linkIndex));
    }

    static u32 hashMoonItemIdentity(s32 uid, const char* stage) {
        u32 hash = 2166136261u;
        hash ^= static_cast<u32>(uid);
        hash *= 16777619u;
        while (stage && *stage) {
            hash ^= static_cast<u8>(*stage++);
            hash *= 16777619u;
        }
        return hash;
    }

    static s32 findMoonItemByIdentityCache(s32 uid, const char* stage) {
        if (!sMoonEntryIndexCacheReady || uid < 0 || !stage)
            return -1;

        s32 slot = static_cast<s32>(hashMoonItemIdentity(uid, stage)
            & static_cast<u32>(kMoonIdentityCacheCapacity - 1));
        for (s32 probe = 0; probe < kMoonIdentityCacheCapacity; probe++) {
            const s32 item = sMoonItemByIdentitySlot[slot];
            if (item < 0)
                return -1;
            const ShineDB::ShineEntry* entry = sMoonEntries[item];
            if (entry && entry->uid == uid && strEq(entry->stage, stage))
                return item;
            slot = (slot + 1) & (kMoonIdentityCacheCapacity - 1);
        }
        return -1;
    }

    static s32 findMoonItemByEntryPointer(const ShineDB::ShineEntry* entry) {
        if (!entry || !sMoonEntryIndexCacheReady)
            return -1;

        // Callers normally pass a pointer into ShineDB::sShineTable.  Do the
        // address-range check before indexing so copied/external entries fall
        // through to the identity lookup below.
        const uintptr_t tableBegin = reinterpret_cast<uintptr_t>(&ShineDB::sShineTable[0]);
        const uintptr_t entryAddress = reinterpret_cast<uintptr_t>(entry);
        if (entryAddress < tableBegin)
            return -1;
        const uintptr_t byteOffset = entryAddress - tableBegin;
        if (byteOffset >= sizeof(ShineDB::sShineTable)
            || byteOffset % sizeof(ShineDB::ShineEntry) != 0)
            return -1;

        const s32 tableIndex = static_cast<s32>(
            byteOffset / sizeof(ShineDB::ShineEntry));
        const s32 item = sMoonItemByShineTableIndex[tableIndex];
        return item >= 0 && item < LD::sMoonCount && sMoonEntries[item] == entry
            ? item
            : -1;
    }

    static s32 findMoonItemByIdentity(s32 uid, const char* stage) {
        if (uid < 0 || !stage)
            return -1;
        const s32 cachedItem = findMoonItemByIdentityCache(uid, stage);
        if (cachedItem >= 0)
            return cachedItem;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const ShineDB::ShineEntry* entry = sMoonEntries[item];
            if (entry && entry->uid == uid && strEq(entry->stage, stage))
                return item;
        }
        return -1;
    }

    static bool hasToken(const LogicState& state, s32 tokenIndex) {
        if (tokenIndex < 0 || tokenIndex >= LD::sTokenCount)
            return false;

        const LD::Token& token = LD::sTokens[tokenIndex];
        if (token.type == LD::TokenType_Event)
            return state.tokens[tokenIndex];

        if (!state.tokens[tokenIndex])
            return false;

        if (token.prerequisite >= 0 && !hasToken(state, token.prerequisite))
            return false;

        if (token.requiresAnyCapThrow) {
            for (s32 i = 0; i < LD::sTokenCount; i++)
                if (LD::sTokens[i].canStartWithSeed && state.tokens[i])
                    return true;
            return false;
        }

        return true;
    }

    static bool requirementAlternativeEnabled(s16 stage, const LD::ReqAlternative& alt) {
        // A full-pool Enemy Swap group that resolves to a token-less actor
        // remaps its original capture to -1. Ignore the whole alternative so
        // its other tokens cannot make an unreachable path look useful during
        // progression placement.
        for (s32 offset = 0; offset < alt.tokenCount; offset++) {
            if (EnemySwapRando::remapLogicToken(
                    stage, LD::sReqTokens[alt.tokenOffset + offset])
                < 0)
                return false;
        }
        return true;
    }

    // stage: LogicData stage the requirement is evaluated on. Enemy Swap can
    // replace a stage's logic-relevant capture with another one, in which case
    // the requirement follows the swap (EnemySwapRando::remapLogicToken).
    static bool requirementSatisfied(const LogicState& state, u16 requirementIndex, s16 stage) {
        if (requirementIndex >= LD::sRequirementCount)
            return true;

        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode == LD::RequirementMode_Unset || requirement.mode == LD::RequirementMode_Free)
            return true;
        if (requirement.mode == LD::RequirementMode_Raw)
            return true;
        if (requirement.mode == LD::RequirementMode_Impossible)
            return false;

        for (s32 altIdx = 0; altIdx < requirement.altCount; altIdx++) {
            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIdx];
            bool all = true;
            for (s32 tokenOffset = 0; tokenOffset < alt.tokenCount; tokenOffset++) {
                const s32 token = EnemySwapRando::remapLogicToken(
                    stage, LD::sReqTokens[alt.tokenOffset + tokenOffset]);
                if (!hasToken(state, token)) {
                    all = false;
                    break;
                }
            }
            if (all)
                return true;
        }

        return false;
    }

    static void markKingdomReached(LogicState& state, s32 kingdom) {
        if (kingdom == WorldMeta::WorldId_Mushroom && LD::sMushroomReachedToken >= 0)
            state.tokens[LD::sMushroomReachedToken] = true;
    }

    static bool stageAccessIsRooted(
        const LogicState& state,
        s32 stage,
        bool* visiting);
    static void computeRootedStageMap(
        const LogicState& state,
        bool* rootedStages);

    static void setStageTrace(
        LogicState& state,
        s32 stage,
        StageTraceKind kind,
        s32 sourceStage,
        s32 sourceEntrance,
        s32 destEntrance,
        s32 entranceChainFirstLink = -1,
        s32 entranceChainFirstKind = EntranceRando::ConnKind_None) {
        if (!isValidStage(stage))
            return;
        StageTrace& trace = state.stageTraces[stage];
        // A graph-derived stage seed can be discovered just before the entrance or
        // painting that actually explains it. Preserve the first concrete route by
        // allowing real provenance to replace that weaker inferred placeholder.
        bool replace = trace.kind == StageTrace_None
            || (trace.kind == StageTrace_Inferred && kind != StageTrace_Inferred);

        // A closed shuffled-entrance component can temporarily discover concrete
        // traces that only point back into that same component. If a later real
        // route (for example Seaside -> Gushen -> painting -> Sand) reaches it,
        // replace the cyclic provenance so the newly rooted access can be used by
        // the placement engine.
        if (!replace && kind != StageTrace_Inferred) {
            bool currentVisiting[LD::sStageCount] = {};
            const bool currentRooted = stageAccessIsRooted(
                state, stage, currentVisiting);
            bool candidateRooted = kind == StageTrace_Odyssey;
            if (!candidateRooted
                && (kind == StageTrace_Entrance || kind == StageTrace_Painting)) {
                bool sourceVisiting[LD::sStageCount] = {};
                candidateRooted = stageAccessIsRooted(
                    state, sourceStage, sourceVisiting);
            }
            replace = candidateRooted && !currentRooted;
        }

        if (!replace)
            return;

        trace.kind = kind;
        trace.sourceStage = (s16)sourceStage;
        trace.sourceEntrance = (s16)sourceEntrance;
        trace.destEntrance = (s16)destEntrance;
        trace.entranceChainFirstLink = (s16)entranceChainFirstLink;
        trace.entranceChainFirstKind = (u8)entranceChainFirstKind;
    }

    static bool markKingdomAccessible(LogicState& state, s32 kingdom) {
        if (!isValidKingdom(kingdom))
            return false;

        bool changed = false;
        if (!state.accessibleKingdoms[kingdom]) {
            state.accessibleKingdoms[kingdom] = true;
            changed = true;
        }

        // Authored routes may use KingdomAccess_* as an explicit alternative.
        // Keep that event token inseparable from physical kingdom access so the
        // construction pass, validator, entrances, and paintings see one state.
        const s32 accessToken = LD::sKingdomAccessTokenByKingdom[kingdom];
        if (accessToken >= 0 && accessToken < LD::sTokenCount
            && !state.tokens[accessToken]) {
            state.tokens[accessToken] = true;
            changed = true;
        }
        markKingdomReached(state, kingdom);
        return changed;
    }

    static bool addAccessibleStageWithTrace(
        LogicState& state,
        s32 stage,
        StageTraceKind traceKind,
        s32 sourceStage,
        s32 sourceEntrance,
        s32 destEntrance,
        s32 entranceChainFirstLink = -1,
        s32 entranceChainFirstKind = EntranceRando::ConnKind_None) {
        if (!isValidStage(stage))
            return false;

        bool changed = false;
        if (!state.accessibleStages[stage]) {
            state.accessibleStages[stage] = true;
            changed = true;
        }
        setStageTrace(
            state, stage, traceKind, sourceStage, sourceEntrance, destEntrance,
            entranceChainFirstLink, entranceChainFirstKind);
        const s32 kingdom = LD::sStages[stage].kingdom;
        if (isValidKingdom(kingdom) && stage == LD::sMainStageByKingdom[kingdom])
            changed |= markKingdomAccessible(state, kingdom);

        return changed;
    }

    static bool addAccessibleStage(LogicState& state, s32 stage) {
        return addAccessibleStageWithTrace(state, stage, StageTrace_Inferred, -1, -1, -1);
    }

    // The route (or the opening) granting a kingdom means the Odyssey can fly
    // there, and StageTrace_Odyssey is the only unconditional root in the trace
    // model - stageAccessIsRooted returns true for it outright, while an
    // Entrance/Painting trace is rooted only if ITS source chain reaches one.
    //
    // The Odyssey trace is therefore applied UNCONDITIONALLY, even when the
    // kingdom already counted as accessible. It used to early-return in that
    // case, and the bug that hid behind it was severe: any earlier discovery of
    // the kingdom's main stage - a shuffled entrance chain arriving there, a
    // painting - calls markKingdomAccessible as a side effect
    // (addAccessibleStageWithTrace does it for a main stage). So by the time the
    // ROUTE unlocked that kingdom, wasAccessible was already true, the early
    // return fired, and the main stage kept whatever weaker trace discovered it.
    // If that trace pointed back into a closed shuffled-entrance component it is
    // not rooted, so computeRootedStageMap left the whole overworld unrooted,
    // reachableLocations skipped every location in it, and the buffer engine saw
    // "0 compatible positions" for a kingdom the player is standing in with its
    // moons in plain sight. The same skip in validateBranchOrder made the
    // forward replay unable to collect moons construction had already proven.
    //
    // setStageTrace is built for exactly this upgrade and is idempotent: it
    // replaces a trace only when the candidate is rooted and the current one is
    // not, so re-rooting an already-rooted stage is a no-op.
    static bool unlockKingdom(LogicState& state, s32 kingdom) {
        if (!isValidKingdom(kingdom))
            return false;

        bool changed = markKingdomAccessible(state, kingdom);
        if (LD::sMainStageByKingdom[kingdom] >= 0)
            changed |= addAccessibleStageWithTrace(state, LD::sMainStageByKingdom[kingdom],
                StageTrace_Odyssey, -1, -1, -1);
        return changed;
    }

    static void grantStartingAbilityToken(LogicState& state, const AbilityRando::AbilityEntry* ability) {
        if (!ability)
            return;
        const s32 token = findTokenIndex(ability->key);
        if (token >= 0)
            state.tokens[token] = true;
    }

    static void grantStartingCaptureToken(LogicState& state, const CaptureRando::CaptureEntry* capture) {
        const s32 token = capture ? findTokenIndex(capture->hackName) : -1;
        if (token >= 0)
            state.tokens[token] = true;
    }

    static u64 sCarriedStartingCaptures = 0;
    static u64 sCarriedStartingAbilities = 0;

    // Deliberately outside the includeConfiguredStartingEquipment gate: carried
    // equipment is owned no matter which opener the seed ends up rolling, so the
    // opener-selection pass has to see it too. That pass is the whole reason
    // this exists - it reserves slots to widen a thin opening frontier, and a
    // reroll's frontier is not thin if the run kept half its captures.
    static void grantCarriedStartingEquipment(LogicState& state) {
        if (sCarriedStartingAbilities && Randomizer::isLockedAbilitiesEnabled()) {
            for (s32 i = 0; i < AbilityRando::getAbilityCount(); i++) {
                const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(i);
                const s32 id = ability ? static_cast<s32>(ability->id) : -1;
                if (id >= 0 && id < 64 && (sCarriedStartingAbilities & (1ull << id)))
                    grantStartingAbilityToken(state, ability);
            }
        }

        if (sCarriedStartingCaptures && Randomizer::isCaptureRandoEnabled()) {
            for (s32 i = 0; i < CaptureRando::getCaptureCount() && i < 64; i++) {
                if (sCarriedStartingCaptures & (1ull << i))
                    grantStartingCaptureToken(state, CaptureRando::getCapture(i));
            }
        }
    }

    // Randomized Starting Position. Defined after the reachability machinery it
    // needs (expandReachabilityFixedPoint / isLocationReachable), but applied at
    // the very end of initState so the opening walk is measured against the real
    // starting equipment and scenarios.
    static void applyRandomizedStartPosition(LogicState& state);

    static void initState(LogicState& state, bool includeConfiguredStartingEquipment = true) {
        std::memset(&state, 0, sizeof(state));
        state.startExitStage = -1; // 0 is a real stage index; the memset above is not enough

        for (s32 i = 0; i < kCloudPaintingSlotCount; i++)
            state.cloudPaintingLinkBySlot[i] = sCloudPaintingAssignmentsReady
                ? sFinalCloudPaintingLinkBySlot[i]
                : -1;

        for (s32 i = 0; i < LD::sKingdomCount; i++)
            state.kingdomScenarios[i] = Randomizer::getInitialKingdomScenario(i);

        for (s32 i = 0; i < LD::sTokenCount; i++) {
            const LD::Token& token = LD::sTokens[i];
            if (token.alwaysUnlocked)
                state.tokens[i] = true;
            if (!Randomizer::isLockedAbilitiesEnabled() && token.type == LD::TokenType_Ability)
                state.tokens[i] = true;
            if (!Randomizer::isCaptureRandoEnabled() && token.type == LD::TokenType_Capture)
                state.tokens[i] = true;
            // The Movement Abilities answer to their own dial rather than to
            // Locked Abilities alone, so a token whose dial is off has to be granted
            // here even with the master toggle on - otherwise the fill would treat
            // an ability nobody is withholding as a lock and route around it.
            if (token.type == LD::TokenType_Ability) {
                const AbilityRando::AbilityEntry* ability
                    = AbilityRando::findAbilityByKey(token.key);
                if (ability && AbilityRando::isMovementAbility(ability->id)
                    && AbilityRando::isMovementAbilityGranted(ability->id))
                    state.tokens[i] = true;
            }
        }

        // A kingdom with no story moons has world peace trivially (Cap, Cloud, Lost,
        // Moon start post-peace). Grant its WorldPeace token so post-game state
        // requirements and the Moon Rock advance (gated on world peace) fire once the
        // Moon Rock node is reached. Mirrors randomizer_logic.py.
        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            const s32 initialScenario = state.kingdomScenarios[i];
            if (initialScenario >= LD::sPostGameScenarioByKingdom[i]) {
                const s32 peaceToken = LD::sWorldPeaceTokenByKingdom[i];
                if (peaceToken >= 0 && peaceToken < LD::sTokenCount)
                    state.tokens[peaceToken] = true;
                state.storyCounts[i] = LD::sStoryTotalsByKingdom[i];
            }
            if (LD::sMoonRockScenarioByKingdom[i] > 0
                && initialScenario >= LD::sMoonRockScenarioByKingdom[i]) {
                const s32 moonRockToken = LD::sMoonRockTokenByKingdom[i];
                if (moonRockToken >= 0 && moonRockToken < LD::sTokenCount)
                    state.tokens[moonRockToken] = true;
                // The key is a prerequisite only for reaching the Moon Rock;
                // a Moon Cube start has already satisfied that prerequisite.
                state.moonRockKeysCollected[i] = true;
            }

            if (LD::sStoryTotalsByKingdom[i] <= 0) {
                const s32 peaceToken = LD::sWorldPeaceTokenByKingdom[i];
                if (peaceToken >= 0 && peaceToken < LD::sTokenCount)
                    state.tokens[peaceToken] = true;
            }
        }

        if (Randomizer::isLockedAbilitiesEnabled()) {
            const s32 startingCapThrowCount = AbilityRando::getStartingCapThrowCount();
            for (s32 i = 0; i < startingCapThrowCount; i++)
                grantStartingAbilityToken(state, AbilityRando::getStartingCapThrow(i));
            if (includeConfiguredStartingEquipment) {
                const s32 startingAbilityCount = AbilityRando::getStartingAbilityCount();
                for (s32 i = 0; i < startingAbilityCount; i++)
                    grantStartingAbilityToken(state, AbilityRando::getStartingAbility(i));
            }
        }

        if (includeConfiguredStartingEquipment && Randomizer::isCaptureRandoEnabled()) {
            const s32 startingCaptureCount = CaptureRando::getStartingCaptureCount();
            for (s32 i = 0; i < startingCaptureCount; i++)
                grantStartingCaptureToken(state, CaptureRando::getStartingCapture(i));
        }

        grantCarriedStartingEquipment(state);

        if (Randomizer::getVisitedKingdomsMapMode()
            == Randomizer::VisitedKingdomsMapMode_Unlocked) {
            // This option grants physical logic access to every kingdom from
            // the opening state. Route-tier completion is still checked by
            // advanceKingdomOrder()/validateBranchOrder(), and the gameplay
            // progression mask remains ordered independently of this state.
            for (s32 i = 0; i < LD::sKingdomCount; i++)
                unlockKingdom(state, i);
        } else if (Randomizer::getVisitedKingdomsMapMode()
            == Randomizer::VisitedKingdomsMapMode_Custom) {
            // Custom map visibility is also an opening-access choice: every
            // selected kingdom is physically reachable from the Odyssey, while
            // the normal progression route remains independent.
            for (s32 i = 0; i < LD::sKingdomCount; i++) {
                if (Randomizer::isStartKingdomSelected(i))
                    unlockKingdom(state, i);
            }
            unlockKingdom(state, KingdomRando::getStartingWorldId());
        } else if (Randomizer::isKingdomRandoEnabled()) {
            unlockKingdom(state, KingdomRando::getStartingWorldId());
        } else {
            unlockKingdom(state, WorldMeta::WorldId_Cap);
            // Vanilla normally treats Cascade as immediately reachable because Cap
            // has no departure requirement. Keep it behind the route frontier when
            // Cap moons are explicitly being used to unlock the next kingdom.
            if (!Randomizer::isCapKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_KingdomUnlock))
                unlockKingdom(state, WorldMeta::WorldId_Cascade);
        }

        applyRandomizedStartPosition(state);
    }

    // The MAXIMAL state: everything a player could ever hold, used to ask
    // structural questions like "is this sub-area reachable from any overworld
    // entrance at all". Anything left out of it reads as a permanent gate and
    // turns a perfectly reachable link into a reported orphan.
    static void initEntranceProofState(LogicState& state) {
        std::memset(&state, 0, sizeof(state));

        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            const s16 maxScenario = LD::sMaxLogicScenarioByKingdom[i];
            state.kingdomScenarios[i] = maxScenario > 0 ? maxScenario : LD::sStartScenarioByKingdom[i];
            unlockKingdom(state, i);
        }

        for (s32 i = 0; i < LD::sTokenCount; i++)
            state.tokens[i] = true;

        // Keys are NOT tokens: they live in their own arrays, so the token loop
        // above never granted them. With Subarea Unlocks on, every gated link
        // then failed stateCanEnterSubareaLink and the audit declared almost the
        // whole graph unreachable - 115 of 123 links on a Per Kingdom + Type
        // seed, against 3 on the same build with the option off.
        //
        // That was never only a noisy warning. repairUnreachableSubAreas consumes
        // this list and BREAKS existing pairings to re-home the "orphans", so a
        // false list makes it rewire a graph the fill had already proven its
        // placements against.
        for (s32 i = 0; i < LD::sKingdomCount; i++)
            state.moonRockKeysCollected[i] = true;
        for (s32 i = 0; i < kMaxSubareaKeySlots; i++)
            state.subareaKeysCollected[i] = true;
    }

    static void precomputeMoonEntries() {
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++)
            sMoonItemByShineTableIndex[i] = -1;
        for (s32 i = 0; i < kMoonIdentityCacheCapacity; i++)
            sMoonItemByIdentitySlot[i] = -1;
        sMoonEntryIndexCacheReady = false;

        for (s32 i = 0; i < LD::sMoonCount; i++) {
            const ShineDB::ShineEntry* entry = ShineDB::findEntry(
                LD::sMoons[i].uid, LD::sMoons[i].stage);
            sMoonEntries[i] = entry;
            if (!entry)
                continue;

            // findEntry returns a pointer into the canonical static table, so
            // pointer subtraction is well-defined here. Keep the first item if
            // malformed logic data ever repeats an entry, matching the old
            // linear lookup's behavior.
            const s32 tableIndex = static_cast<s32>(entry - ShineDB::sShineTable);
            if (tableIndex >= 0 && tableIndex < ShineDB::sShineTableSize
                && sMoonItemByShineTableIndex[tableIndex] < 0)
                sMoonItemByShineTableIndex[tableIndex] = static_cast<s16>(i);

            s32 slot = static_cast<s32>(hashMoonItemIdentity(entry->uid, entry->stage)
                & static_cast<u32>(kMoonIdentityCacheCapacity - 1));
            for (s32 probe = 0; probe < kMoonIdentityCacheCapacity; probe++) {
                const s32 existing = sMoonItemByIdentitySlot[slot];
                if (existing < 0) {
                    sMoonItemByIdentitySlot[slot] = static_cast<s16>(i);
                    break;
                }
                const ShineDB::ShineEntry* existingEntry = sMoonEntries[existing];
                if (existingEntry && existingEntry->uid == entry->uid
                    && strEq(existingEntry->stage, entry->stage))
                    break;
                slot = (slot + 1) & (kMoonIdentityCacheCapacity - 1);
            }
        }
        sMoonEntryIndexCacheReady = true;
    }

    // Per-seed reshuffle of which ITEMS are triples (the pool). The physical
    // triple SLOTS stay the vanilla grand spots (see isMultiSlot); the solver
    // then places these randomized triple items into those fixed slots, so the
    // vanilla grand locations still pay 3 but the moon you get there can be from
    // any kingdom. Selects exactly as many triple items as there are vanilla
    // grand slots so the slot/item counts match. Deterministic from the seed
    // alone, so it is stable across generation attempts and save reloads.
    // Requires sMoonEntries to be populated first (call after precomputeMoonEntries).
    static void computeRewardMultiOverride(u32 seed) {
        for (s32 i = 0; i < LD::sMoonCount; i++)
            sRewardMultiOverride[i] = false;

        if (!Randomizer::isRandomizeTripleMoonsEnabled()) {
            sRewardMultiOverrideActive = false;
            return;
        }

        // Match the number of vanilla grand slots exactly.
        s32 target = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++)
            if (sMoonEntries[i] && (sMoonEntries[i]->flags & ShineDB::ShineFlag_Multi))
                target++;

        // Candidate triple items: any normal collectible moon, in any kingdom
        // (Mushroom, Dark and Darker Side included). Story, hint-art and Toadette
        // moons are excluded so they keep their own presentation/handling;
        // everything else is fair game so a triple can be from any kingdom.
        s32 candidates[LD::sMoonCount];
        s32 candidateCount = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            const ShineDB::ShineEntry* moon = sMoonEntries[i];
            if (!moon || !isMoonPoolEligible(*moon))
                continue;
            if (moon->flags & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_HintArt))
                continue;
            candidates[candidateCount++] = i;
        }

        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ 0x54524D33u, Randomizer::RngStream::Moon);
        shuffle(candidates, candidateCount, rng);

        const s32 chosen = target < candidateCount ? target : candidateCount;
        for (s32 k = 0; k < chosen; k++)
            sRewardMultiOverride[candidates[k]] = true;

        sRewardMultiOverrideActive = true;
    }

    // Whether a moon may be reassigned to a different kingdom by the redistribution
    // pre-processing. Story and hint-art moons are location/placement-based and
    // stay in their home kingdom; everything else in the pool is fair game. Role
    // assignments (Moon Rock Key, Toadette, capture, ability) happen AFTER this
    // step, so they are not excluded here -- they ride on the reassigned pool.
    static bool isRedistributableMoon(s32 moon) {
        if (!isValidMoon(moon) || !isMoonPoolEligible(*sMoonEntries[moon]))
            return false;
        return (sMoonEntries[moon]->flags
                   & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_HintArt))
            == 0;
    }

    // Vanilla moon value of a kingdom (grands count 3), used as the rebalance
    // target so every kingdom keeps its original total after redistribution.
    static s32 vanillaKingdomValue(s32 kingdom) {
        s32 value = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++)
            if (LD::sMoons[i].kingdom == kingdom)
                value += isVanillaMultiMoon(i) ? 3 : 1;
        return value;
    }

    static u32 kingdomMask(s32 kingdom) {
        return isValidKingdom(kingdom) ? 1u << kingdom : 0;
    }

    static s32 pickCarrierDestination(u32 itemDestinationMask, u32 carrierMask,
        const s32* loads, const s32* kingdomTieRank) {
        s32 best = -1;
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (!(carrierMask & kingdomMask(kingdom)))
                continue;
            if (!(itemDestinationMask & kingdomMask(kingdom)))
                continue;
            if (best < 0 || loads[kingdom] < loads[best]
                || (loads[kingdom] == loads[best]
                    && kingdomTieRank[kingdom] < kingdomTieRank[best]))
                best = kingdom;
        }
        return best;
    }

    // Which kingdoms may hold each reward kind, from the per-kingdom masks.
    // Mirrors capture_/ability_/painting_carrier_kingdoms() in
    // logic/moon_allocation.py - keep the two in step.
    static u32 captureCarrierKingdomMask() {
        if (!Randomizer::isCaptureRandoEnabled())
            return 0;
        u32 mask = 0;
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            if (Randomizer::isCaptureRewardKingdom(kingdom))
                mask |= kingdomMask(kingdom);
        return mask;
    }

    static u32 abilityCarrierKingdomMask() {
        if (!Randomizer::isLockedAbilitiesEnabled())
            return 0;
        u32 mask = 0;
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            if (Randomizer::isAbilityRewardKingdom(kingdom))
                mask |= kingdomMask(kingdom);
        return mask;
    }

    static u32 paintingCarrierKingdomMask() {
        u32 mask = 0;
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            if (Randomizer::isPaintingRewardKingdom(kingdom))
                mask |= kingdomMask(kingdom);
        return mask;
    }

    static bool isRewardCarrierKingdom(s32 kingdom) {
        return ((captureCarrierKingdomMask() | abilityCarrierKingdomMask())
                   & kingdomMask(kingdom))
            != 0;
    }

    struct MoonAllocationItem {
        s32 homeKingdom;
        s32 value;
        u32 destinationMask;
        bool movable;
        bool fixed;
        bool captureCarrier;
        bool abilityCarrier;
        bool paintingCarrier;
        // The kingdoms each carrier flag actually counts in. Every item in one
        // allocation carries the same three; they ride on the item so the
        // recompute paths do not have to re-read the settings.
        u32 captureDestinations;
        u32 abilityDestinations;
        u32 paintingDestinations;
        // Can hold a Subarea Key. Same predicate as the three carrier flags
        // above, but counted per ITEM rather than per reward value, so it is
        // named separately instead of borrowing one of them.
        bool subareaKeyCarrier;
        bool moonCoin;
    };

    static s32 allocationItemValue(s32 item) {
        if (sRewardMultiOverrideActive)
            return sRewardMultiOverride[item] ? 3 : 1;
        return sMoonEntries[item]
                && (sMoonEntries[item]->flags & ShineDB::ShineFlag_Multi)
            ? 3
            : 1;
    }

    static s32 maskBitCount(u32 mask) {
        s32 count = 0;
        while (mask) {
            count += (mask & 1u) != 0;
            mask >>= 1;
        }
        return count;
    }

    // Currency reward items need the same allocation protections as the old
    // filler-coin items.  Hint Tokens are a reward role, never a capture or
    // ability carrier, so allowing one into a special carrier kingdom would
    // silently remove required progression from the pool.
    static bool isMoonCurrencyRewardByHomeKingdom(s32 item, s32 homeKingdom) {
        const ShineDB::ShineEntry* entry = sMoonEntries[item];
        if (!entry || !isMoonPoolEligible(*entry)
            || (entry->flags & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_HintArt)))
            return false;

        switch (homeKingdom) {
        case WorldMeta::WorldId_Cap:
            return Randomizer::isCapKingdomMoonUseSelected(
                       Randomizer::SpecialMoonUse_Coins)
                || Randomizer::isCapKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
        case WorldMeta::WorldId_Cloud:
            return Randomizer::isCloudKingdomMoonUseSelected(
                       Randomizer::SpecialMoonUse_Coins)
                || Randomizer::isCloudKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
        case WorldMeta::WorldId_Moon:
            return Randomizer::isMoonKingdomMoonUseSelected(
                       Randomizer::SpecialMoonUse_Coins)
                || Randomizer::isMoonKingdomMoonUseSelected(
                    Randomizer::SpecialMoonUse_HintTokens);
        case WorldMeta::WorldId_Mushroom:
            return !Randomizer::isCaptureRandoEnabled();
        case WorldMeta::WorldId_Dark:
        case WorldMeta::WorldId_Darker:
            return !Randomizer::isLockedAbilitiesEnabled();
        default:
            return false;
        }
    }

    static u32 calculateMoonRoleLayoutHash() {
        // Stable order and explicit scalar mixing are intentional: this value is
        // persisted/diagnostic data, not a hash of an in-memory object layout.
        u32 hash = 2166136261u ^ 0x4D524C59u; // "MRLY"
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const u32 kingdom = static_cast<u32>(sMoonAssignedKingdom[item] + 2);
            const u32 reward = allocationItemValue(item) == 3 ? 1u : 0u;
            hash ^= kingdom;
            hash *= 16777619u;
            hash ^= reward;
            hash *= 16777619u;
        }
        return hash == 0 ? 1u : hash;
    }

    static bool failMoonAllocation(const char* constraint, s32 required,
        s32 available, const char* lockSetting) {
        setFailure(
            FailureCode::CapacityFailure,
            "Moon allocation capacity failure: constraint=%s required=%d available=%d difficulty=%s coinPolicy=%s lock=%s",
            constraint ? constraint : "unknown", required, available,
            Randomizer::getMoonAllocationDifficultyName(
                Randomizer::getMoonAllocationDifficulty()),
            Randomizer::getMoonCoinPolicyName(Randomizer::getMoonCoinPolicy()),
            lockSetting ? lockSetting : "none");
        sKingdomAssignmentActive = false;
        sMoonRoleLayoutHash = 0;
        sKingdomAllocationSummaryReady = false;
        sInitialized = false;
        return false;
    }

    static bool buildLegacyTripleAssignment(u32 seed) {
        // Preserve the pre-allocation Randomize Triple Moons behavior. The full
        // allocator below is deliberately a separate path so the legacy setting
        // remains byte-for-byte stable wherever the old behavior was feasible.
        s32 need[LD::sKingdomCount];
        for (s32 k = 0; k < LD::sKingdomCount; k++)
            need[k] = vanillaKingdomValue(k);

        s32 movableTriples[LD::sMoonCount];
        s32 movableSingles[LD::sMoonCount];
        s32 tripleCount = 0;
        s32 singleCount = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            if (!sMoonEntries[i])
                continue;
            const s32 home = LD::sMoons[i].kingdom;
            if (!isValidKingdom(home))
                return failMoonAllocation("legacy-triple-home", 1, 0, "none");
            if (!isRedistributableMoon(i)) {
                need[home] -= moonValue(i);
                continue;
            }
            if (isMultiMoon(i))
                movableTriples[tripleCount++] = i;
            else
                movableSingles[singleCount++] = i;
        }

        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ 0x4B494E47u, Randomizer::RngStream::Moon);
        s32 slots[LD::sMoonCount];
        s32 slotCount = 0;
        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            const s32 capacity = need[k] > 0 ? need[k] / 3 : 0;
            for (s32 c = 0; c < capacity && slotCount < LD::sMoonCount; c++)
                slots[slotCount++] = k;
        }
        shuffle(slots, slotCount, rng);

        s32 triplesPerKingdom[LD::sKingdomCount] = {};
        const s32 placeTriples = tripleCount < slotCount ? tripleCount : slotCount;
        for (s32 s = 0; s < placeTriples; s++)
            triplesPerKingdom[slots[s]]++;

        shuffle(movableTriples, tripleCount, rng);
        shuffle(movableSingles, singleCount, rng);

        s32 tripleIdx = 0;
        s32 singleIdx = 0;
        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            for (s32 c = 0; c < triplesPerKingdom[k] && tripleIdx < tripleCount; c++) {
                sMoonAssignedKingdom[movableTriples[tripleIdx++]] = static_cast<s16>(k);
                need[k] -= 3;
            }
        }
        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            while (need[k] > 0 && singleIdx < singleCount) {
                sMoonAssignedKingdom[movableSingles[singleIdx++]] = static_cast<s16>(k);
                need[k]--;
            }
        }

        sKingdomAssignmentActive = true;
        s32 assignedValues[LD::sKingdomCount] = {};
        s32 expectedValues[LD::sKingdomCount] = {};
        s32 totalValue = 0;
        bool valid = true;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const s32 home = LD::sMoons[item].kingdom;
            const s32 assigned = sMoonAssignedKingdom[item];
            const s32 value = allocationItemValue(item);
            totalValue += value;
            if (!isValidKingdom(home) || !isValidKingdom(assigned)) {
                valid = false;
                continue;
            }
            expectedValues[home] += isVanillaMultiMoon(item) ? 3 : 1;
            assignedValues[assigned] += value;
        }
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (assignedValues[kingdom] != expectedValues[kingdom])
                valid = false;
        }
        if (!valid) {
            return failMoonAllocation("legacy-triple-total", totalValue,
                totalValue, "none");
        }

        sMoonRoleLayoutHash = calculateMoonRoleLayoutHash();
        return true;
    }

    // Generic deterministic allocator. It runs after the reward-multi override
    // and before capture/ability/painting role builders, so every downstream role
    // sees the final kingdom assignment rather than the physical home kingdom.
    static bool computeMoonKingdomAssignment(u32 seed) {
        MoonAllocationItem items[LD::sMoonCount] = {};
        s32 baseAssigned[LD::sMoonCount];
        s32 baselineAssigned[LD::sMoonCount];
        s32 workingAssigned[LD::sMoonCount];
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            baseAssigned[i] = -1;
            baselineAssigned[i] = -1;
            workingAssigned[i] = -1;
            sMoonAssignedKingdom[i] = LD::sMoons[i].kingdom;
        }
        sKingdomAssignmentActive = false;
        sKingdomAllocationSummaryReady = false;
        sMoonRoleLayoutHash = 0;

        const bool fullAllocation = Randomizer::isMoonAllocationActive();
        if (!fullAllocation && !Randomizer::isRandomizeTripleMoonsEnabled()) {
            sMoonRoleLayoutHash = calculateMoonRoleLayoutHash();
            return true;
        }
        if (!fullAllocation)
            return buildLegacyTripleAssignment(seed);

        const u32 captureDestinations = captureCarrierKingdomMask();
        const u32 abilityDestinations = abilityCarrierKingdomMask();
        const u32 paintingDestinations = paintingCarrierKingdomMask();
        // Captures and abilities only: the painting carrier never excluded
        // Moon Coins, and Cloud stays an ordinary destination for them.
        const u32 rewardCarrierMask = captureDestinations | abilityDestinations;

        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const ShineDB::ShineEntry* entry = sMoonEntries[item];
            const s32 home = LD::sMoons[item].kingdom;
            if (!isValidKingdom(home)) {
                setFailure(FailureCode::ValidationFailure,
                    "Moon allocation item %d has invalid home kingdom %d.",
                    item, home);
                return false;
            }

            const bool poolEligible = entry && isMoonPoolEligible(*entry);
            const bool storyOrHint = entry
                && (entry->flags & (ShineDB::ShineFlag_Story | ShineDB::ShineFlag_HintArt));
            const bool moonCoin = isMoonCurrencyRewardByHomeKingdom(item, home);
            bool fixed = !poolEligible || storyOrHint;
            if (moonCoin && Randomizer::getMoonCoinPolicy() == Randomizer::MoonCoinPolicy::Vanilla)
                fixed = true;

            items[item].homeKingdom = home;
            items[item].value = allocationItemValue(item);
            items[item].fixed = fixed;
            items[item].movable = !fixed;
            items[item].captureCarrier = poolEligible;
            items[item].abilityCarrier = poolEligible;
            items[item].paintingCarrier = poolEligible;
            items[item].captureDestinations = captureDestinations;
            items[item].abilityDestinations = abilityDestinations;
            items[item].paintingDestinations = paintingDestinations;
            items[item].subareaKeyCarrier = poolEligible;
            items[item].moonCoin = moonCoin;
            if (fixed) {
                items[item].destinationMask = kingdomMask(home);
                baseAssigned[item] = home;
            } else if (moonCoin && Randomizer::getMoonCoinPolicy() == Randomizer::MoonCoinPolicy::Standard) {
                // Standard-policy coins may reach anywhere that is not holding a
                // capture or ability reward - a coin landing there would
                // displace one. This was the fixed range up to Moon Kingdom,
                // which is the same set while the carriers are the default
                // Mushroom/Dark/Darker.
                items[item].destinationMask
                    = kMoonAllocationAllKingdomsMask & ~rewardCarrierMask;
            } else {
                items[item].destinationMask = kMoonAllocationAllKingdomsMask;
            }
        }

        s32 kingdomOrder[LD::sKingdomCount];
        s32 kingdomTieRank[LD::sKingdomCount];
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            kingdomOrder[kingdom] = kingdom;
        Randomizer::Rng targetRng = Randomizer::makeMoonAllocationRng(
            seed, Randomizer::kMoonAllocationTargetSeedSalt);
        shuffle(kingdomOrder, LD::sKingdomCount, targetRng);
        for (s32 rank = 0; rank < LD::sKingdomCount; rank++)
            kingdomTieRank[kingdomOrder[rank]] = rank;

        s32 carrierOrder[LD::sMoonCount];
        s32 carrierCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (items[item].movable && (items[item].captureCarrier || items[item].abilityCarrier || items[item].paintingCarrier))
                carrierOrder[carrierCount++] = item;
        }
        Randomizer::Rng carrierRng = Randomizer::makeMoonAllocationRng(
            seed, Randomizer::kMoonAllocationCarrierSeedSalt);
        shuffle(carrierOrder, carrierCount, carrierRng);

        s32 fixedCaptureValue = 0;
        s32 fixedAbilityValue[LD::sKingdomCount] = {};
        s32 fixedPaintingEntries = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const s32 destination = baseAssigned[item];
            if (destination < 0)
                continue;
            if (items[item].captureCarrier
                && (items[item].captureDestinations & kingdomMask(destination)))
                fixedCaptureValue += items[item].value;
            if (items[item].abilityCarrier
                && (items[item].abilityDestinations & kingdomMask(destination)))
                fixedAbilityValue[destination] += items[item].value;
            if (items[item].paintingCarrier
                && (items[item].paintingDestinations & kingdomMask(destination)))
                fixedPaintingEntries += items[item].value;
        }

        const Randomizer::MoonCoinPolicy coinPolicy = Randomizer::getMoonCoinPolicy();
        s32 paintingValue[LD::sKingdomCount] = {};
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const s32 destination = baseAssigned[item];
            if (destination >= 0 && items[item].paintingCarrier
                && (items[item].paintingDestinations & kingdomMask(destination)))
                paintingValue[destination] += items[item].value;
        }
        if (paintingDestinations != 0
            && fixedPaintingEntries < kMoonAllocationCloudCarrierMinimum) {
            s32 available = fixedPaintingEntries;
            for (s32 pos = 0; pos < carrierCount; pos++) {
                const s32 item = carrierOrder[pos];
                if (baseAssigned[item] < 0 && items[item].paintingCarrier
                    && (items[item].destinationMask & paintingDestinations))
                    available += items[item].value;
            }
            if (available < kMoonAllocationCloudCarrierMinimum)
                return failMoonAllocation("painting-entries",
                    kMoonAllocationCloudCarrierMinimum, available,
                    "cloud-painting");

            // Painting capacity follows reward value: a triple opens three links.
            // Prefer singles so the reservation does not overshoot ten credits
            // unless the remaining pool makes a triple necessary.
            while (fixedPaintingEntries < kMoonAllocationCloudCarrierMinimum) {
                s32 chosen = -1;
                for (s32 valueClass = 1; valueClass <= 3 && chosen < 0; valueClass += 2) {
                    for (s32 pos = 0; pos < carrierCount; pos++) {
                        const s32 item = carrierOrder[pos];
                        if (baseAssigned[item] >= 0 || !items[item].paintingCarrier
                            || items[item].value != valueClass
                            || !(items[item].destinationMask & paintingDestinations))
                            continue;
                        chosen = item;
                        break;
                    }
                }
                const s32 paintingDestination = chosen < 0 ? -1
                                                           : pickCarrierDestination(items[chosen].destinationMask,
                                                                 paintingDestinations, paintingValue, kingdomTieRank);
                if (chosen < 0 || paintingDestination < 0)
                    return failMoonAllocation("painting-entries",
                        kMoonAllocationCloudCarrierMinimum,
                        fixedPaintingEntries, "cloud-painting");
                baseAssigned[chosen] = paintingDestination;
                paintingValue[paintingDestination] += items[chosen].value;
                fixedPaintingEntries += items[chosen].value;
            }
        }

        if (Randomizer::isCaptureRandoEnabled()) {
            const s32 required = CaptureRando::getRequiredProgressionSlotCount();
            s32 available = fixedCaptureValue;
            for (s32 pos = 0; pos < carrierCount; pos++) {
                const s32 item = carrierOrder[pos];
                if (baseAssigned[item] < 0 && items[item].captureCarrier
                    && (items[item].destinationMask & captureDestinations))
                    available += items[item].value;
            }
            if (available < required)
                return failMoonAllocation("capture-slots", required, available,
                    "capture-lock");

            s32 captureValue[LD::sKingdomCount] = {};
            for (s32 item = 0; item < LD::sMoonCount; item++) {
                const s32 destination = baseAssigned[item];
                if (destination >= 0 && items[item].captureCarrier
                    && (items[item].captureDestinations & kingdomMask(destination)))
                    captureValue[destination] += items[item].value;
            }
            s32 current = fixedCaptureValue;
            while (current < required) {
                const s32 deficit = required - current;
                const s32 preferredValue = deficit >= 3 ? 3 : 1;
                s32 chosen = -1;
                for (s32 pass = 0; pass < 2 && chosen < 0; pass++) {
                    const s32 valueClass = pass == 0 ? preferredValue
                                                     : (preferredValue == 3 ? 1 : 3);
                    for (s32 pos = 0; pos < carrierCount; pos++) {
                        const s32 item = carrierOrder[pos];
                        if (baseAssigned[item] >= 0 || !items[item].captureCarrier
                            || items[item].value != valueClass
                            || !(items[item].destinationMask & captureDestinations))
                            continue;
                        chosen = item;
                        break;
                    }
                }
                const s32 captureDestination = chosen < 0 ? -1
                                                          : pickCarrierDestination(items[chosen].destinationMask,
                                                                captureDestinations, captureValue, kingdomTieRank);
                if (chosen < 0 || captureDestination < 0)
                    return failMoonAllocation("capture-slots", required, current,
                        "capture-lock");
                baseAssigned[chosen] = captureDestination;
                captureValue[captureDestination] += items[chosen].value;
                current += items[chosen].value;
            }
        }

        if (Randomizer::isLockedAbilitiesEnabled()) {
            const s32 required = AbilityRando::getRequiredProgressionSlotCount();
            s32 available = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                available += fixedAbilityValue[kingdom];
            for (s32 pos = 0; pos < carrierCount; pos++) {
                const s32 item = carrierOrder[pos];
                if (baseAssigned[item] >= 0 || !items[item].abilityCarrier)
                    continue;
                if (items[item].destinationMask & abilityDestinations)
                    available += items[item].value;
            }
            if (available < required)
                return failMoonAllocation("ability-slots-combined", required,
                    available, "ability-lock");

            s32 current = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                current += fixedAbilityValue[kingdom];
            s32 abilityValue[LD::sKingdomCount] = {};
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                abilityValue[kingdom] = fixedAbilityValue[kingdom];
            while (current < required) {
                s32 chosen = -1;
                s32 destination = -1;
                for (s32 pos = 0; pos < carrierCount; pos++) {
                    const s32 item = carrierOrder[pos];
                    if (baseAssigned[item] >= 0 || !items[item].abilityCarrier)
                        continue;
                    const s32 candidateDestination = pickCarrierDestination(
                        items[item].destinationMask, abilityDestinations,
                        abilityValue, kingdomTieRank);
                    if (candidateDestination < 0)
                        continue;
                    chosen = item;
                    destination = candidateDestination;
                    break;
                }
                if (chosen < 0)
                    return failMoonAllocation("ability-slots-combined", required,
                        current, "ability-lock");
                baseAssigned[chosen] = destination;
                abilityValue[destination] += items[chosen].value;
                current += items[chosen].value;
            }
        }

        // Subarea Keys ride on the host kingdoms' moons, one distinct carrier
        // per key slot. Without a reservation here the allocator is free to
        // drain Cap and Moon down to kMoonAllocationMinimumValue (5 each) while
        // the mode still asks for up to 47 keys, and buildSubareaKeys would then
        // find fewer carriers than slots. Randomizer::subareaHostFits cannot
        // catch that: it runs on the file select, before any allocation exists,
        // against the STATIC per-kingdom moon counts.
        //
        // Note this counts ITEMS, not reward value like the capture/painting
        // reservations above - a triple moon still carries exactly one key.
        if (Randomizer::isSubareaUnlockEnabled()) {
            const s32 required = SubareaKeyDB::keySlotCount();
            u32 hostMask = 0;
            if (Randomizer::getSubareaKeyScope() == Randomizer::KeyScope::AnyMoon) {
                hostMask = kMoonAllocationAllKingdomsMask;
            } else {
                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                    if (SubareaKeyDB::isHostKingdom(kingdom))
                        hostMask |= kingdomMask(kingdom);
                }
            }

            s32 hostCarriers[LD::sKingdomCount] = {};
            s32 current = 0;
            for (s32 item = 0; item < LD::sMoonCount; item++) {
                const s32 destination = baseAssigned[item];
                if (destination < 0 || !items[item].subareaKeyCarrier
                    || !(kingdomMask(destination) & hostMask))
                    continue;
                hostCarriers[destination]++;
                current++;
            }

            s32 available = current;
            for (s32 pos = 0; pos < carrierCount; pos++) {
                const s32 item = carrierOrder[pos];
                if (baseAssigned[item] < 0 && items[item].subareaKeyCarrier
                    && (items[item].destinationMask & hostMask))
                    available++;
            }
            if (available < required)
                return failMoonAllocation("subarea-key-carriers", required,
                    available, "subarea-keys");

            // carrierOrder is already shuffled, so taking it in order keeps the
            // choice seed-deterministic.
            //
            // Two passes, and the order is what keeps this cheap: pass 0 only
            // pins moons already native to a host kingdom, holding them where
            // they started. Cap and Moon own 69 pool-eligible moons between
            // them, so even the finest mode's 47 keys are normally satisfied
            // without importing anything, and the planner's per-kingdom value
            // profile is left almost untouched. Pass 1 only runs when the
            // allocator has already moved so much off the hosts that pass 0
            // came up short.
            for (s32 pass = 0; pass < 2 && current < required; pass++) {
                for (s32 pos = 0; pos < carrierCount && current < required; pos++) {
                    const s32 item = carrierOrder[pos];
                    if (baseAssigned[item] >= 0 || !items[item].subareaKeyCarrier)
                        continue;
                    const bool native = (kingdomMask(items[item].homeKingdom) & hostMask) != 0;
                    if ((pass == 0) != native)
                        continue;

                    s32 destination = -1;
                    if (pass == 0
                        && (items[item].destinationMask
                            & kingdomMask(items[item].homeKingdom))) {
                        destination = items[item].homeKingdom;
                    } else {
                        // When both kingdoms host, fill the one holding fewer
                        // keys so neither collection page ends up empty.
                        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                            if (!(hostMask & kingdomMask(kingdom))
                                || !(items[item].destinationMask & kingdomMask(kingdom)))
                                continue;
                            if (destination < 0
                                || hostCarriers[kingdom] < hostCarriers[destination])
                                destination = kingdom;
                        }
                    }
                    if (destination < 0)
                        continue;

                    baseAssigned[item] = destination;
                    hostCarriers[destination]++;
                    current++;
                }
            }
            if (current < required)
                return failMoonAllocation("subarea-key-carriers", required,
                    current, "subarea-keys");
        }

        auto recomputeState = [&](const s32* assigned, s32* values,
                                  s32* entries, s32* captureValue, s32* abilityValues,
                                  s32* paintingEntries) {
            std::memset(values, 0, sizeof(s32) * LD::sKingdomCount);
            std::memset(entries, 0, sizeof(s32) * LD::sKingdomCount);
            std::memset(abilityValues, 0, sizeof(s32) * LD::sKingdomCount);
            *captureValue = 0;
            *paintingEntries = 0;
            for (s32 item = 0; item < LD::sMoonCount; item++) {
                const s32 destination = assigned[item];
                if (!isValidKingdom(destination))
                    continue;
                values[destination] += items[item].value;
                entries[destination]++;
                if (items[item].captureCarrier
                    && (items[item].captureDestinations & kingdomMask(destination)))
                    *captureValue += items[item].value;
                if (items[item].abilityCarrier
                    && (items[item].abilityDestinations & kingdomMask(destination)))
                    abilityValues[destination] += items[item].value;
                if (items[item].paintingCarrier
                    && (items[item].paintingDestinations & kingdomMask(destination)))
                    *paintingEntries += items[item].value;
            }
        };

        const auto hasEnoughRemainingValue = [&](const s32* assigned,
                                                 s32 candidate, s32 target, const s32* deficits) {
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                s32 required = deficits[kingdom];
                if (kingdom == target) {
                    required -= items[candidate].value;
                    if (required < 0)
                        required = 0;
                }
                s32 available = 0;
                for (s32 item = 0; item < LD::sMoonCount; item++) {
                    if (item == candidate || assigned[item] >= 0)
                        continue;
                    if (items[item].destinationMask & kingdomMask(kingdom))
                        available += items[item].value;
                }
                if (available < required)
                    return false;
            }
            return true;
        };

        const Randomizer::MoonAllocationDifficulty allocationDifficulty
            = Randomizer::getMoonAllocationDifficulty();

        // ── Pinned kingdoms ──────────────────────────────────────────────────
        // Mushroom, Cloud, and Dark plus Darker keep their vanilla credited
        // value; only the other thirteen kingdoms shuffle their excess. These
        // four have no moon requirement, so under a free shuffle their entire
        // list was one draw from the excess multiset and could land anywhere
        // from 3 to 77 - which is how Darker Side ended up many times its
        // vanilla size. Their moons are reward channels (captures, painting
        // links, abilities) rather than requirement fodder, so their size
        // should track their reward pool, not the shuffle.
        //
        // This pins VALUE, not row count. allocationItemValue already follows
        // the Randomize Triple Moons override, so the kingdom keeps its vanilla
        // moon count while the number of list rows moves as moons merge into
        // triples or split back into singles.
        s32 reservedValues[LD::sKingdomCount] = {};
        {
            s32 reservedEntries[LD::sKingdomCount] = {};
            s32 reservedCaptureValue = 0;
            s32 reservedAbilityValues[LD::sKingdomCount] = {};
            s32 reservedPaintingEntries = 0;
            recomputeState(baseAssigned, reservedValues, reservedEntries,
                &reservedCaptureValue, reservedAbilityValues,
                &reservedPaintingEntries);
        }

        bool pinnedKingdom[LD::sKingdomCount] = {};
        s32 pinnedTarget[LD::sKingdomCount] = {};
        pinnedKingdom[WorldMeta::WorldId_Mushroom] = true;
        pinnedTarget[WorldMeta::WorldId_Mushroom]
            = kMoonAllocationNativeTarget[WorldMeta::WorldId_Mushroom];
        pinnedKingdom[WorldMeta::WorldId_Cloud] = true;
        pinnedTarget[WorldMeta::WorldId_Cloud]
            = kMoonAllocationNativeTarget[WorldMeta::WorldId_Cloud];
        // Cloud's vanilla nine credits cannot host ten painting links, so the pin
        // rises to the link count when that option is on. Triple rewards provide
        // three of those credits without requiring three distinct list rows.
        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
            && pinnedTarget[WorldMeta::WorldId_Cloud]
                < kMoonAllocationCloudCarrierMinimum)
            pinnedTarget[WorldMeta::WorldId_Cloud]
                = kMoonAllocationCloudCarrierMinimum;
        pinnedKingdom[WorldMeta::WorldId_Dark] = true;
        pinnedKingdom[WorldMeta::WorldId_Darker] = true;

        // A pin below what the carrier reservations already placed there is
        // unsatisfiable - the planner reads a negative deficit and rejects every
        // target vector. Reserving ten painting links out of singles normally
        // lands on ten exactly, but a triple among them overshoots, and the same
        // is true of a large capture pool. Yield to the reservation in that case
        // rather than failing the seed; the pair floors below already do.
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
            if (pinnedKingdom[kingdom] && kingdom != WorldMeta::WorldId_Dark
                && kingdom != WorldMeta::WorldId_Darker
                && pinnedTarget[kingdom] < reservedValues[kingdom])
                pinnedTarget[kingdom] = reservedValues[kingdom];

        // The ability pair is pinned as a pair: the total is vanilla, the split
        // between the two is drawn per seed. Each side floors at whatever the
        // ability reservation already put there, so a split the reserved
        // carriers cannot fit is never proposed.
        const s32 abilityPairTotal
            = kMoonAllocationNativeTarget[WorldMeta::WorldId_Dark]
            + kMoonAllocationNativeTarget[WorldMeta::WorldId_Darker];
        const auto abilityKingdomFloor = [&](s32 kingdom) -> s32 {
            return reservedValues[kingdom] > kMoonAllocationAbilityKingdomMinimum
                ? reservedValues[kingdom]
                : kMoonAllocationAbilityKingdomMinimum;
        };
        const s32 darkFloor = abilityKingdomFloor(WorldMeta::WorldId_Dark);
        const s32 darkerFloor = abilityKingdomFloor(WorldMeta::WorldId_Darker);
        if (darkFloor + darkerFloor > abilityPairTotal)
            return failMoonAllocation("ability-pair-split", darkFloor + darkerFloor,
                abilityPairTotal, "ability-lock");
        const auto setAbilityPairSplit = [&](s32 darkShare) {
            pinnedTarget[WorldMeta::WorldId_Dark] = darkShare;
            pinnedTarget[WorldMeta::WorldId_Darker] = abilityPairTotal - darkShare;
        };
        // The vanilla split, when the reservations leave room for it. Used by
        // Easy, which draws no split of its own, and by the exact-profile
        // fallback.
        const s32 nativeAbilitySplit
            = kMoonAllocationNativeTarget[WorldMeta::WorldId_Dark] >= darkFloor
                && abilityPairTotal
                        - kMoonAllocationNativeTarget[WorldMeta::WorldId_Dark]
                    >= darkerFloor
            ? kMoonAllocationNativeTarget[WorldMeta::WorldId_Dark]
            : darkFloor;
        setAbilityPairSplit(nativeAbilitySplit);

        // Easy has no exact profile to hit, so its pins ride the ordinary floor
        // and a cap in its greedy fill below. The floor pass places whole items,
        // so a triple can carry a pinned kingdom up to two over its pin there;
        // the exact-profile difficulties land on it precisely.
        const auto allocationFloorValue = [&](s32 kingdom) -> s32 {
            if (allocationDifficulty
                == Randomizer::MoonAllocationDifficulty::Easy)
                return pinnedKingdom[kingdom] ? pinnedTarget[kingdom]
                                              : moonAllocationMinimumValue(kingdom);
            return LD::sMoonRequirementsByKingdom[kingdom];
        };

        bool floorSucceeded = false;
        s32 assignedValues[LD::sKingdomCount] = {};
        s32 assignedEntries[LD::sKingdomCount] = {};
        s32 assignedCaptureValue = 0;
        s32 assignedAbilityValue[LD::sKingdomCount] = {};
        s32 assignedPaintingEntries = 0;
        s32 ordinaryOrder[LD::sMoonCount];
        s32 ordinaryCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (baseAssigned[item] < 0)
                ordinaryOrder[ordinaryCount++] = item;
        }

        for (s32 retry = 0; retry < kMoonAllocationMaxFloorRetries; retry++) {
            for (s32 item = 0; item < LD::sMoonCount; item++)
                workingAssigned[item] = baseAssigned[item];
            recomputeState(workingAssigned, assignedValues, assignedEntries,
                &assignedCaptureValue, assignedAbilityValue,
                &assignedPaintingEntries);

            s32 deficits[LD::sKingdomCount] = {};
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                const s32 deficit = allocationFloorValue(kingdom)
                    - assignedValues[kingdom];
                deficits[kingdom] = deficit > 0 ? deficit : 0;
            }

            shuffle(ordinaryOrder, ordinaryCount, targetRng);
            s32 remainingDeficits = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                remainingDeficits += deficits[kingdom];

            while (remainingDeficits > 0) {
                bool madeProgress = false;
                for (s32 orderIndex = 0; orderIndex < LD::sKingdomCount; orderIndex++) {
                    const s32 target = kingdomOrder[orderIndex];
                    if (deficits[target] <= 0)
                        continue;

                    s32 chosen = -1;
                    s32 chosenMaskWidth = 0x7FFFFFFF;
                    s32 chosenValueRank = 0x7FFFFFFF;
                    s32 chosenOrderRank = 0x7FFFFFFF;
                    for (s32 orderRank = 0; orderRank < ordinaryCount; orderRank++) {
                        const s32 item = ordinaryOrder[orderRank];
                        if (workingAssigned[item] >= 0
                            || !(items[item].destinationMask & kingdomMask(target)))
                            continue;
                        const s32 desiredValue = deficits[target] >= 3 ? 3 : 1;
                        const s32 valueRank = items[item].value == desiredValue ? 0 : 1;
                        if (valueRank != 0 && items[item].value != 3
                            && items[item].value != 1)
                            continue;
                        if (!hasEnoughRemainingValue(workingAssigned, item, target,
                                deficits))
                            continue;
                        const s32 maskWidth = maskBitCount(items[item].destinationMask);
                        if (chosen < 0 || maskWidth < chosenMaskWidth
                            || (maskWidth == chosenMaskWidth && valueRank < chosenValueRank)
                            || (maskWidth == chosenMaskWidth
                                && valueRank == chosenValueRank
                                && orderRank < chosenOrderRank)) {
                            chosen = item;
                            chosenMaskWidth = maskWidth;
                            chosenValueRank = valueRank;
                            chosenOrderRank = orderRank;
                        }
                    }
                    if (chosen < 0)
                        continue;

                    workingAssigned[chosen] = target;
                    const s32 before = deficits[target];
                    deficits[target] = before > items[chosen].value
                        ? before - items[chosen].value
                        : 0;
                    remainingDeficits -= before - deficits[target];
                    madeProgress = true;
                    break;
                }
                if (!madeProgress)
                    break;
            }
            if (remainingDeficits == 0) {
                floorSucceeded = true;
                break;
            }
        }

        if (!floorSucceeded) {
            s32 required = 0;
            s32 available = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                required += allocationFloorValue(kingdom);
            for (s32 item = 0; item < LD::sMoonCount; item++)
                if (baseAssigned[item] < 0)
                    available += items[item].value;
            return failMoonAllocation("ordinary-floors", required, available, "none");
        }

        // The feasible mandatory baseline is the boundary between ordinary-floor
        // assignments and optional profile fill.
        for (s32 item = 0; item < LD::sMoonCount; item++)
            baselineAssigned[item] = workingAssigned[item];

        s32 optionalItems[LD::sMoonCount];
        s32 optionalCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (workingAssigned[item] < 0)
                optionalItems[optionalCount++] = item;
        }
        Randomizer::Rng assignmentRng = Randomizer::makeMoonAllocationRng(
            seed, Randomizer::kMoonAllocationAssignmentSeedSalt);
        shuffle(optionalItems, optionalCount, assignmentRng);

        s32 profileTarget[LD::sKingdomCount] = {};
        bool targetTight[LD::sKingdomCount] = {};
        bool concentration[LD::sKingdomCount] = {};

        const auto planExactProfileAssignment = [&](const s32* target,
                                                    s32* outAssigned) -> bool {
            s32 baselineValues[LD::sKingdomCount] = {};
            s32 baselineEntries[LD::sKingdomCount] = {};
            s32 baselineCaptureValue = 0;
            s32 baselineAbilityValues[LD::sKingdomCount] = {};
            s32 baselinePaintingEntries = 0;
            recomputeState(baselineAssigned, baselineValues, baselineEntries,
                &baselineCaptureValue, baselineAbilityValues,
                &baselinePaintingEntries);

            s32 deficits[LD::sKingdomCount] = {};
            s32 optionalValue = 0;
            s32 deficitValue = 0;
            s32 restrictedTripleCount = 0;
            s32 unrestrictedTripleCount = 0;
            s32 restrictedSingleCount = 0;
            // "Special" is the set of kingdoms holding a capture or ability
            // reward - a Standard-policy Moon Coin may not land there, so the
            // triples that can are counted apart from the ones that cannot.
            // This was the fixed Mushroom/Dark/Darker triple, which is the same
            // set while those three are the carriers.
            const u32 specialMask = rewardCarrierMask;
            const u32 ordinaryMask
                = kMoonAllocationAllKingdomsMask & ~specialMask;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                deficits[kingdom] = target[kingdom] - baselineValues[kingdom];
                if (deficits[kingdom] < 0)
                    return false;
                deficitValue += deficits[kingdom];
            }
            for (s32 i = 0; i < optionalCount; i++) {
                const s32 item = optionalItems[i];
                if (baselineAssigned[item] >= 0)
                    continue;
                if (items[item].value != 1 && items[item].value != 3)
                    return false;
                optionalValue += items[item].value;
                const bool restricted = !(items[item].destinationMask & specialMask);
                if (items[item].value == 3) {
                    if (restricted)
                        restrictedTripleCount++;
                    else
                        unrestrictedTripleCount++;
                } else if (restricted) {
                    restrictedSingleCount++;
                }
            }
            if (deficitValue != optionalValue)
                return false;

            s32 minimumTriples[LD::sKingdomCount] = {};
            s32 maximumTriples[LD::sKingdomCount] = {};
            s32 ordinaryDeficit = 0;
            s32 ordinaryMinimum = 0;
            s32 ordinaryMaximum = 0;
            s32 specialMinimum = 0;
            s32 specialMaximum = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                const s32 overRows = baselineEntries[kingdom] + deficits[kingdom]
                    - MoonRando::kMaxShineListRows;
                minimumTriples[kingdom] = overRows > 0 ? (overRows + 1) / 2 : 0;
                maximumTriples[kingdom] = deficits[kingdom] / 3;
                if (minimumTriples[kingdom] > maximumTriples[kingdom])
                    return false;
                if (ordinaryMask & kingdomMask(kingdom)) {
                    ordinaryDeficit += deficits[kingdom];
                    ordinaryMinimum += minimumTriples[kingdom];
                    ordinaryMaximum += maximumTriples[kingdom];
                } else {
                    specialMinimum += minimumTriples[kingdom];
                    specialMaximum += maximumTriples[kingdom];
                }
            }

            const s32 totalTriples = restrictedTripleCount
                + unrestrictedTripleCount;
            s32 ordinaryLower = restrictedTripleCount;
            if (ordinaryMinimum > ordinaryLower)
                ordinaryLower = ordinaryMinimum;
            if (totalTriples - specialMaximum > ordinaryLower)
                ordinaryLower = totalTriples - specialMaximum;
            s32 ordinaryUpper = ordinaryMaximum;
            if (totalTriples - specialMinimum < ordinaryUpper)
                ordinaryUpper = totalTriples - specialMinimum;
            if (ordinaryDeficit < restrictedSingleCount)
                return false;
            const s32 ordinarySingleUpper
                = (ordinaryDeficit - restrictedSingleCount) / 3;
            if (ordinarySingleUpper < ordinaryUpper)
                ordinaryUpper = ordinarySingleUpper;
            if (ordinaryLower > ordinaryUpper)
                return false;

            s32 tripleCounts[LD::sKingdomCount];
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                tripleCounts[kingdom] = minimumTriples[kingdom];

            const auto fillGroup = [&](u32 candidateMask,
                                       s32 wantedTotal) -> bool {
                while (true) {
                    s32 current = 0;
                    for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                        if (candidateMask & kingdomMask(kingdom))
                            current += tripleCounts[kingdom];
                    if (current >= wantedTotal)
                        return current == wantedTotal;
                    s32 destination = -1;
                    for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                        if (!(candidateMask & kingdomMask(kingdom)))
                            continue;
                        if (tripleCounts[kingdom] >= maximumTriples[kingdom])
                            continue;
                        if (destination < 0
                            || deficits[kingdom] - 3 * tripleCounts[kingdom]
                                > deficits[destination] - 3 * tripleCounts[destination]
                            || (deficits[kingdom] - 3 * tripleCounts[kingdom]
                                    == deficits[destination] - 3 * tripleCounts[destination]
                                && kingdomTieRank[kingdom]
                                    < kingdomTieRank[destination]))
                            destination = kingdom;
                    }
                    if (destination < 0)
                        return false;
                    tripleCounts[destination]++;
                }
            };
            if (!fillGroup(ordinaryMask, ordinaryLower)
                || !fillGroup(specialMask, totalTriples - ordinaryLower))
                return false;

            for (s32 item = 0; item < LD::sMoonCount; item++)
                outAssigned[item] = baselineAssigned[item];
            s32 remainingTriples[LD::sKingdomCount];
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                remainingTriples[kingdom] = tripleCounts[kingdom];

            const auto takeDestination = [&](s32 item, s32* remaining,
                                             u32 candidateMask) -> s32 {
                s32 destination = -1;
                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                    if (!(candidateMask & kingdomMask(kingdom)))
                        continue;
                    if (remaining[kingdom] <= 0
                        || !(items[item].destinationMask & kingdomMask(kingdom)))
                        continue;
                    if (destination < 0
                        || remaining[kingdom] > remaining[destination]
                        || (remaining[kingdom] == remaining[destination]
                            && kingdomTieRank[kingdom]
                                < kingdomTieRank[destination]))
                        destination = kingdom;
                }
                if (destination >= 0)
                    remaining[destination]--;
                return destination;
            };

            for (s32 pass = 0; pass < 2; pass++) {
                for (s32 i = 0; i < optionalCount; i++) {
                    const s32 item = optionalItems[i];
                    const bool restricted
                        = !(items[item].destinationMask & specialMask);
                    if (items[item].value != 3
                        || restricted != (pass == 0))
                        continue;
                    const s32 destination = takeDestination(item,
                        remainingTriples,
                        restricted ? ordinaryMask
                                   : kMoonAllocationAllKingdomsMask);
                    if (!isValidKingdom(destination))
                        return false;
                    outAssigned[item] = destination;
                }
            }
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (remainingTriples[kingdom] != 0)
                    return false;

            s32 remainingSingles[LD::sKingdomCount];
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                remainingSingles[kingdom]
                    = deficits[kingdom] - 3 * tripleCounts[kingdom];
            for (s32 pass = 0; pass < 2; pass++) {
                for (s32 i = 0; i < optionalCount; i++) {
                    const s32 item = optionalItems[i];
                    const bool restricted
                        = !(items[item].destinationMask & specialMask);
                    if (items[item].value != 1
                        || restricted != (pass == 0))
                        continue;
                    const s32 destination = takeDestination(item,
                        remainingSingles,
                        restricted ? ordinaryMask
                                   : kMoonAllocationAllKingdomsMask);
                    if (!isValidKingdom(destination))
                        return false;
                    outAssigned[item] = destination;
                }
            }
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (remainingSingles[kingdom] != 0)
                    return false;

            s32 finalValues[LD::sKingdomCount] = {};
            s32 finalEntries[LD::sKingdomCount] = {};
            s32 finalCaptureValue = 0;
            s32 finalAbilityValues[LD::sKingdomCount] = {};
            s32 finalPaintingEntries = 0;
            recomputeState(outAssigned, finalValues, finalEntries,
                &finalCaptureValue, finalAbilityValues,
                &finalPaintingEntries);
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (finalValues[kingdom] != target[kingdom]
                    || finalEntries[kingdom] > MoonRando::kMaxShineListRows)
                    return false;
            return true;
        };

        if (allocationDifficulty
            == Randomizer::MoonAllocationDifficulty::Easy) {
            s32 rowEntries[LD::sKingdomCount] = {};
            s32 rowValues[LD::sKingdomCount] = {};
            s32 rowCaptureValue = 0;
            s32 rowAbilityValues[LD::sKingdomCount] = {};
            s32 rowPaintingEntries = 0;
            recomputeState(workingAssigned, rowValues, rowEntries,
                &rowCaptureValue, rowAbilityValues, &rowPaintingEntries);
            for (s32 optionalIndex = 0; optionalIndex < optionalCount;
                 optionalIndex++) {
                const s32 item = optionalItems[optionalIndex];
                const u32 allowed = items[item].destinationMask;
                s32 destination = -1;
                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                    if (!(allowed & kingdomMask(kingdom))
                        || rowEntries[kingdom] >= MoonRando::kMaxShineListRows)
                        continue;
                    // A pinned kingdom is full at its pin; the balance pass
                    // would otherwise level it up with everyone else.
                    if (pinnedKingdom[kingdom]
                        && assignedValues[kingdom] >= pinnedTarget[kingdom])
                        continue;
                    if (destination < 0
                        || assignedValues[kingdom] < assignedValues[destination]
                        || (assignedValues[kingdom] == assignedValues[destination]
                            && kingdomTieRank[kingdom]
                                < kingdomTieRank[destination]))
                        destination = kingdom;
                }
                if (!isValidKingdom(destination))
                    return failMoonAllocation("shine-list-rows", optionalCount,
                        LD::sKingdomCount * MoonRando::kMaxShineListRows,
                        "none");
                workingAssigned[item] = destination;
                assignedValues[destination] += items[item].value;
                assignedEntries[destination]++;
                rowEntries[destination]++;
            }
            recomputeState(workingAssigned, profileTarget, rowEntries,
                &rowCaptureValue, rowAbilityValues, &rowPaintingEntries);
        } else {
            Randomizer::Rng profileRng = Randomizer::makeMoonAllocationRng(
                seed, Randomizer::kMoonAllocationProfileSeedSalt);

            // Only the unpinned kingdoms trade excess, and they trade it only
            // among themselves, so the grand total still comes out at the whole
            // moon pool - planExactProfileAssignment rejects any target vector
            // that does not.
            s32 freeKingdoms[LD::sKingdomCount];
            s32 freeExcess[LD::sKingdomCount];
            s32 freeCount = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                if (pinnedKingdom[kingdom])
                    continue;
                freeKingdoms[freeCount] = kingdom;
                freeExcess[freeCount] = kMoonAllocationNativeTarget[kingdom]
                    - LD::sMoonRequirementsByKingdom[kingdom];
                freeCount++;
            }

            // Whatever the pins claim beyond their vanilla value - only the
            // Cloud painting bump does - has to come out of the free pool, or
            // the targets stop adding up to the moons that exist. Take it off
            // the largest excess, which is always the one that can spare it.
            s32 pinnedBorrow = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (pinnedKingdom[kingdom] && kingdom != WorldMeta::WorldId_Dark
                    && kingdom != WorldMeta::WorldId_Darker)
                    pinnedBorrow += pinnedTarget[kingdom]
                        - kMoonAllocationNativeTarget[kingdom];
            while (pinnedBorrow > 0) {
                s32 largest = 0;
                for (s32 i = 1; i < freeCount; i++)
                    if (freeExcess[i] > freeExcess[largest])
                        largest = i;
                if (freeExcess[largest] <= 0)
                    return failMoonAllocation("pinned-borrow", pinnedBorrow, 0,
                        "cloud-painting");
                freeExcess[largest]--;
                pinnedBorrow--;
            }

            s32 nativeFreeExcess[LD::sKingdomCount];
            for (s32 i = 0; i < freeCount; i++)
                nativeFreeExcess[i] = freeExcess[i];

            const s32 abilitySplitSpan
                = abilityPairTotal - darkFloor - darkerFloor;
            const auto applyProfileTargets = [&]() {
                for (s32 i = 0; i < freeCount; i++)
                    profileTarget[freeKingdoms[i]]
                        = LD::sMoonRequirementsByKingdom[freeKingdoms[i]]
                        + freeExcess[i];
                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                    if (pinnedKingdom[kingdom])
                        profileTarget[kingdom] = pinnedTarget[kingdom];
            };

            bool profilePlanned = false;
            for (s32 attempt = 0;
                 attempt < kMoonAllocationProfileShuffleRetries; attempt++) {
                shuffle(freeExcess, freeCount, profileRng);
                // Redrawn alongside the shuffle so a split this seed's reserved
                // carriers cannot host is retried, not fatal.
                setAbilityPairSplit(darkFloor
                    + static_cast<s32>(
                        profileRng.nextModulo((u32)abilitySplitSpan + 1)));
                applyProfileTargets();
                if (planExactProfileAssignment(profileTarget, workingAssigned)) {
                    profilePlanned = true;
                    break;
                }
            }
            // The native mapping is an exact member of the same Vanilla excess
            // multiset.  It is only a fallback for a highly constrained set of
            // fixed/reserved carriers; normal seeds use the shuffled profile.
            // The pins hold on this path too: native already is each pinned
            // kingdom's vanilla value, bar the Cloud painting bump.
            if (!profilePlanned) {
                for (s32 i = 0; i < freeCount; i++)
                    freeExcess[i] = nativeFreeExcess[i];
                setAbilityPairSplit(nativeAbilitySplit);
                applyProfileTargets();
                profilePlanned = planExactProfileAssignment(profileTarget,
                    workingAssigned);
            }
            if (!profilePlanned)
                return failMoonAllocation("profile-target", 819, 0, "none");

            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                targetTight[kingdom]
                    = profileTarget[kingdom]
                        - LD::sMoonRequirementsByKingdom[kingdom]
                    <= kMoonAllocationTightExcessMax;

            // Draw both cumulative tier sizes before changing the profile so
            // Expert's first steps are exactly the same steps Hard would use.
            const s32 hardTightenCount
                = 1 + static_cast<s32>(profileRng.nextModulo(2));
            const s32 expertExtraCount
                = 1 + static_cast<s32>(profileRng.nextModulo(2));
            const s32 maxTightenCount
                = 1 + static_cast<s32>(profileRng.nextModulo(7));
            s32 tightenCount = 0;
            switch (allocationDifficulty) {
            case Randomizer::MoonAllocationDifficulty::Hard:
                tightenCount = hardTightenCount;
                break;
            case Randomizer::MoonAllocationDifficulty::Expert:
                tightenCount = hardTightenCount + expertExtraCount;
                break;
            case Randomizer::MoonAllocationDifficulty::Max:
                tightenCount = maxTightenCount;
                break;
            case Randomizer::MoonAllocationDifficulty::Easy:
            case Randomizer::MoonAllocationDifficulty::Vanilla:
            default:
                break;
            }

            s32 completedTightenCount = 0;
            for (s32 step = 0; step < tightenCount; step++) {
                s32 candidateOrder[LD::sKingdomCount];
                s32 candidateCount = 0;
                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                    if (!pinnedKingdom[kingdom] && !targetTight[kingdom]
                        && !concentration[kingdom]
                        && profileTarget[kingdom]
                                - LD::sMoonRequirementsByKingdom[kingdom]
                            > kMoonAllocationTightExcessMax)
                        candidateOrder[candidateCount++] = kingdom;
                shuffle(candidateOrder, candidateCount, profileRng);
                bool moved = false;
                for (s32 candidateIndex = 0;
                     candidateIndex < candidateCount && !moved;
                     candidateIndex++) {
                    const s32 candidate = candidateOrder[candidateIndex];
                    const s32 oldExcess = profileTarget[candidate]
                        - LD::sMoonRequirementsByKingdom[candidate];
                    const s32 tightStart = static_cast<s32>(
                        profileRng.nextModulo(kMoonAllocationTightExcessMax + 1));

                    s32 sinkOrder[LD::sKingdomCount];
                    s32 sinkCount = 0;
                    for (s32 kingdom = 0; kingdom < LD::sKingdomCount;
                         kingdom++)
                        if (kingdom != candidate && !pinnedKingdom[kingdom]
                            && !targetTight[kingdom])
                            sinkOrder[sinkCount++] = kingdom;
                    for (s32 a = 0; a < sinkCount; a++) {
                        s32 best = a;
                        for (s32 b = a + 1; b < sinkCount; b++) {
                            const s32 lhs = sinkOrder[b];
                            const s32 rhs = sinkOrder[best];
                            if (concentration[lhs] > concentration[rhs]
                                || (concentration[lhs] == concentration[rhs]
                                    && kingdomTieRank[lhs]
                                        < kingdomTieRank[rhs]))
                                best = b;
                        }
                        const s32 tmp = sinkOrder[a];
                        sinkOrder[a] = sinkOrder[best];
                        sinkOrder[best] = tmp;
                    }

                    for (s32 tightOffset = 0;
                         tightOffset <= kMoonAllocationTightExcessMax && !moved;
                         tightOffset++) {
                        const s32 newExcess = (tightStart + tightOffset)
                            % (kMoonAllocationTightExcessMax + 1);
                        const s32 displaced = oldExcess - newExcess;
                        for (s32 sinkIndex = 0;
                             sinkIndex < sinkCount && !moved; sinkIndex++) {
                            const s32 sink = sinkOrder[sinkIndex];
                            s32 candidateTarget[LD::sKingdomCount];
                            for (s32 kingdom = 0;
                                 kingdom < LD::sKingdomCount; kingdom++)
                                candidateTarget[kingdom]
                                    = profileTarget[kingdom];
                            candidateTarget[candidate]
                                = LD::sMoonRequirementsByKingdom[candidate]
                                + newExcess;
                            candidateTarget[sink] += displaced;
                            s32 candidateAssigned[LD::sMoonCount];
                            if (!planExactProfileAssignment(candidateTarget,
                                    candidateAssigned))
                                continue;
                            for (s32 kingdom = 0;
                                 kingdom < LD::sKingdomCount; kingdom++)
                                profileTarget[kingdom]
                                    = candidateTarget[kingdom];
                            for (s32 item = 0; item < LD::sMoonCount; item++)
                                workingAssigned[item]
                                    = candidateAssigned[item];
                            targetTight[candidate] = true;
                            concentration[sink] = true;
                            completedTightenCount++;
                            moved = true;
                        }
                    }
                }
                if (!moved)
                    break;
            }
            if (completedTightenCount != tightenCount)
                return failMoonAllocation("tight-profile", tightenCount,
                    completedTightenCount, "none");
        }

        bool valid = true;
        s32 totalValue = 0;
        s32 finalValue = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            totalValue += items[item].value;
            const s32 destination = workingAssigned[item];
            if (!isValidKingdom(destination)
                || (items[item].fixed && destination != items[item].homeKingdom)
                || (items[item].moonCoin && coinPolicy == Randomizer::MoonCoinPolicy::Standard
                    && isValidKingdom(destination)
                    && (rewardCarrierMask & kingdomMask(destination))))
                valid = false;
            if (isValidKingdom(destination))
                finalValue += items[item].value;
        }

        recomputeState(workingAssigned, assignedValues, assignedEntries,
            &assignedCaptureValue, assignedAbilityValue,
            &assignedPaintingEntries);
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (assignedValues[kingdom] < allocationFloorValue(kingdom))
                valid = false;
            if (assignedEntries[kingdom] > MoonRando::kMaxShineListRows)
                valid = false;
        }
        // The pins are the point of the whole exercise, so assert them on the
        // finished assignment rather than trusting the planner. Easy has no
        // exact profile and can sit up to a triple over, so it only has to hold
        // the pair total and the per-kingdom floor checked above.
        if (allocationDifficulty != Randomizer::MoonAllocationDifficulty::Easy) {
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (pinnedKingdom[kingdom]
                    && assignedValues[kingdom] != pinnedTarget[kingdom])
                    valid = false;
        }
        if (assignedValues[WorldMeta::WorldId_Dark]
                + assignedValues[WorldMeta::WorldId_Darker]
            < abilityPairTotal)
            valid = false;
        if (Randomizer::isCaptureRandoEnabled()
            && assignedCaptureValue < CaptureRando::getRequiredProgressionSlotCount())
            valid = false;
        if (Randomizer::isLockedAbilitiesEnabled()) {
            s32 assignedAbilityTotal = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (abilityDestinations & kingdomMask(kingdom))
                    assignedAbilityTotal += assignedAbilityValue[kingdom];
            if (assignedAbilityTotal
                < AbilityRando::getRequiredProgressionSlotCount())
                valid = false;
        }
        if (paintingDestinations != 0
            && assignedPaintingEntries < kMoonAllocationCloudCarrierMinimum)
            valid = false;
        // Same assertion for the Subarea Key reservation, counted per item on
        // whichever kingdoms host keys this seed. buildSubareaKeys refuses to
        // build a partial table, so a planner that quietly gave these moons away
        // would turn into a rejected seed much later with a far less specific
        // message than the one failMoonAllocation prints here.
        if (Randomizer::isSubareaUnlockEnabled()) {
            s32 assignedSubareaKeyCarriers = 0;
            for (s32 item = 0; item < LD::sMoonCount; item++) {
                const s32 destination = workingAssigned[item];
                if (isValidKingdom(destination) && items[item].subareaKeyCarrier
                    && SubareaKeyDB::isHostKingdom(destination))
                    assignedSubareaKeyCarriers++;
            }
            if (assignedSubareaKeyCarriers < SubareaKeyDB::keySlotCount())
                valid = false;
        }
        if (finalValue != totalValue) {
            valid = false;
        }
        if (!valid) {
            return failMoonAllocation("validation", totalValue, finalValue, "none");
        }

        for (s32 item = 0; item < LD::sMoonCount; item++)
            sMoonAssignedKingdom[item] = static_cast<s16>(workingAssigned[item]);
        sKingdomAssignmentActive = true;

        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            sKingdomAllocationSummaries[kingdom] = {};
            sKingdomAllocationSummaries[kingdom].assignedEntries = assignedEntries[kingdom];
            sKingdomAllocationSummaries[kingdom].assignedValue = assignedValues[kingdom];
            sKingdomAllocationSummaries[kingdom].baseRequirement
                = LD::sMoonRequirementsByKingdom[kingdom];
            sKingdomAllocationSummaries[kingdom].excessValue
                = assignedValues[kingdom]
                - LD::sMoonRequirementsByKingdom[kingdom];
            sKingdomAllocationSummaries[kingdom].minimumValue
                = allocationFloorValue(kingdom);
            sKingdomAllocationSummaries[kingdom].targetValue
                = profileTarget[kingdom];
            sKingdomAllocationSummaries[kingdom].targetTight
                = targetTight[kingdom];
            sKingdomAllocationSummaries[kingdom].concentration
                = concentration[kingdom];
            sKingdomAllocationSummaries[kingdom].pinned = pinnedKingdom[kingdom];
        }
        // The capacity and the minimum are the run's, not one kingdom's: the
        // requirement is "enough slots somewhere" and the carriers share it, so
        // every carrier reports the same pair and the readout says the same
        // thing whichever kingdom the player inspects.
        {
            s32 abilityTotal = 0;
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++)
                if (abilityDestinations & kingdomMask(kingdom))
                    abilityTotal += assignedAbilityValue[kingdom];
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                const u32 mask = kingdomMask(kingdom);
                if (captureDestinations & mask) {
                    sKingdomAllocationSummaries[kingdom].specialKind
                        = AllocationConstraintKind::CaptureSlots;
                    sKingdomAllocationSummaries[kingdom].specialCapacity
                        = assignedCaptureValue;
                    sKingdomAllocationSummaries[kingdom].specialMinimum
                        = CaptureRando::getRequiredProgressionSlotCount();
                }
                if (paintingDestinations & mask) {
                    sKingdomAllocationSummaries[kingdom].specialKind
                        = AllocationConstraintKind::PaintingEntries;
                    sKingdomAllocationSummaries[kingdom].specialCapacity
                        = assignedPaintingEntries;
                    sKingdomAllocationSummaries[kingdom].specialMinimum
                        = kMoonAllocationCloudCarrierMinimum;
                }
                if (abilityDestinations & mask) {
                    sKingdomAllocationSummaries[kingdom].specialKind
                        = AllocationConstraintKind::AbilitySlotsCombined;
                    sKingdomAllocationSummaries[kingdom].specialCapacity
                        = abilityTotal;
                    sKingdomAllocationSummaries[kingdom].specialMinimum
                        = AbilityRando::getRequiredProgressionSlotCount();
                }
            }
        }
        sKingdomAllocationSummaryReady = true;
        sMoonRoleLayoutHash = calculateMoonRoleLayoutHash();
        return sMoonRoleLayoutHash != 0;
    }

    static void addMoonUnlockToken(s32 moon, s32 token) {
        if (!isValidMoon(moon) || token < 0 || token >= LD::sTokenCount)
            return;
        if (sMoonUnlockTokenCounts[moon] >= kMaxUnlockTokensPerMoon)
            return;
        sMoonUnlockTokens[moon][sMoonUnlockTokenCounts[moon]++] = (s16)token;
    }

    static void precomputeMoonUnlocks() {
        std::memset(sMoonUnlockTokenCounts, 0, sizeof(sMoonUnlockTokenCounts));
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            sMoonCloudPaintingSlotCounts[i] = 0;
            for (s32 j = 0; j < kMaxCloudPaintingUnlocksPerMoon; j++) {
                sMoonCloudPaintingSlots[i][j] = -1;
                sMoonPaintingUnlocks[i][j] = -1;
            }
        }
        std::memset(sFreePaintingLinks, 0, sizeof(sFreePaintingLinks));

        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
            && sCloudPaintingAssignmentsReady) {
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                sFreePaintingLinks[i] = sFinalFreePaintingLinks[i];
        }

        for (s32 i = 0; i < LD::sMoonCount; i++) {
            const ShineDB::ShineEntry* moon = sMoonEntries[i];
            if (!moon)
                continue;

            // Cloud links are assigned as their moon items are committed to a
            // logic branch.  Cache the stable Cloud slot here; the selected link
            // remains in the LogicState until a successful placement is finalized.
            if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()) {
                // PaintingRando owns the definition of a Cloud reward.  In
                // particular, it follows the triple-redistributed kingdom, not
                // the immutable source world.  Keeping this solver cache on the
                // same predicate makes its proof and the runtime painting gate
                // select the same moon.
                const s32 firstCloudSlot = PaintingRando::getCloudMoonSlot(*moon);
                const s32 unlockCount = PaintingRando::getCloudMoonPaintingUnlockCount(*moon);
                for (s32 unlock = 0;
                     unlock < unlockCount
                     && unlock < kMaxCloudPaintingUnlocksPerMoon;
                     unlock++) {
                    const s32 cloudSlot = firstCloudSlot + unlock;
                    // Credits beyond the ten real links are runtime duplicates,
                    // not progression slots the logic solver needs to place.
                    if (cloudSlot < 0 || cloudSlot >= kCloudPaintingSlotCount)
                        continue;
                    const s32 slotIndex = sMoonCloudPaintingSlotCounts[i]++;
                    sMoonCloudPaintingSlots[i][slotIndex]
                        = static_cast<s16>(cloudSlot);
                    if (sCloudPaintingAssignmentsReady)
                        sMoonPaintingUnlocks[i][slotIndex]
                            = sFinalCloudPaintingLinkBySlot[cloudSlot];
                }
            }

            if (Randomizer::isCaptureRandoEnabled()) {
                const s32 captureCount = CaptureRando::getProgressionCaptureCountForMoon(*moon);
                for (s32 j = 0; j < captureCount; j++) {
                    const CaptureRando::CaptureEntry* capture = CaptureRando::getProgressionCaptureForMoon(*moon, j);
                    addMoonUnlockToken(i, capture ? findTokenIndex(capture->hackName) : -1);
                }
            }

            if (Randomizer::isLockedAbilitiesEnabled()) {
                const s32 abilityCount = AbilityRando::getProgressionAbilityCountForMoon(*moon);
                for (s32 j = 0; j < abilityCount; j++) {
                    const AbilityRando::AbilityEntry* ability = AbilityRando::getProgressionAbilityForMoon(*moon, j);
                    addMoonUnlockToken(i, ability ? findTokenIndex(ability->key) : -1);
                }
            }
        }
    }

    static bool moonUnlocksCloudPainting(s32 item) {
        return Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
            && isValidMoon(item)
            && sMoonCloudPaintingSlotCounts[item] > 0;
    }

    static bool itemHasProgressUnlock(s32 item) {
        if (item < 0 || item >= LD::sMoonCount)
            return false;

        // Enemy/Capture chaos makes every swappable capture unavailable to
        // route logic. Do not force its Capture Lock item into the mandatory
        // progression tail: it cannot open a usable route. Captures that never
        // randomize away (Mini Rocket / Spark Pylon) remain mandatory alongside
        // ability unlocks, paintings, and Moon Rock keys.
        for (s32 i = 0; i < sMoonUnlockTokenCounts[item]; i++) {
            const s32 token = sMoonUnlockTokens[item][i];
            if (token < 0 || token >= LD::sTokenCount)
                continue;
            if (LD::sTokens[token].type != LD::TokenType_Capture
                || !Randomizer::isEnemySwapRandoEnabled()
                || EnemySwapRando::isReliableCaptureToken(nullptr, LD::sTokens[token].key))
                return true;
        }

        return moonUnlocksCloudPainting(item) || itemIsMoonRockKey(item)
            || itemIsSubareaKey(item);
    }

    static bool routeDifficultyAllowed(LD::RouteDifficulty difficulty) {
        return (s32)difficulty <= (s32)Randomizer::getLogicDifficulty();
    }

    static LD::RouteDifficulty optionDifficultyAt(u16 offset, u8 count, s32 optionIndex) {
        if (count <= 0)
            return LD::RouteDifficulty_Casual;

        s32 difficultyIndex = offset;
        if (count > 1)
            difficultyIndex += optionIndex;

        if (difficultyIndex < 0 || difficultyIndex >= LD::sRouteOptionDifficultyCount)
            return LD::RouteDifficulty_Casual;

        return LD::sRouteOptionDifficulties[difficultyIndex];
    }

    static bool routeOptionDifficultyAllowed(const LD::RouteEdge& edge, s32 optionIndex) {
        return routeDifficultyAllowed(
            optionDifficultyAt(edge.difficultyOffset, edge.difficultyCount, optionIndex));
    }

    static bool optionScenarioAllowed(u16 offset, u8 count, s32 optionIndex, s16 scenario) {
        if (count <= 0 || scenario < 0)
            return true;

        s32 maskIndex = offset;
        if (count > 1)
            maskIndex += optionIndex;
        if (maskIndex < 0 || maskIndex >= LD::sOptionScenarioMaskCount)
            return true;

        const u32 mask = LD::sOptionScenarioMasks[maskIndex];
        return mask == 0 || scenarioMaskContains(mask, scenario);
    }

    static bool routeOptionScenarioAllowed(
        const LD::RouteEdge& edge, s32 optionIndex, s16 scenario) {
        return optionScenarioAllowed(
            edge.optionScenarioOffset, edge.optionScenarioCount, optionIndex, scenario);
    }

    // Node-access prerequisites: an OR option can require the player to be able to
    // reach other authored nodes (@<node_id> in the editor). reachableNodes == nullptr
    // means the caller has no reachability set - the maximal-state "is this
    // physically crossable at all" proofs, which already grant every token, so they
    // grant node access too rather than going silently impossible.
    static bool altNodeRequirementsSatisfied(
        const LD::ReqAlternative& alt, const bool* reachableNodes) {
        if (LD::sReqNodeCount == 0 || alt.nodeCount == 0 || !reachableNodes)
            return true;
        for (s32 i = 0; i < alt.nodeCount; i++) {
            const s32 node = LD::sReqNodes[alt.nodeOffset + i];
            if (node < 0 || node >= LD::sNodeCount || !reachableNodes[node])
                return false;
        }
        return true;
    }

    static bool altNodeRequirementsSatisfied(
        u16 requirementIndex, s32 altIndex, const bool* reachableNodes) {
        if (LD::sReqNodeCount == 0 || !reachableNodes || requirementIndex >= LD::sRequirementCount)
            return true;
        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode != LD::RequirementMode_Custom || altIndex >= requirement.altCount)
            return true;
        return altNodeRequirementsSatisfied(
            LD::sReqAlternatives[requirement.altOffset + altIndex], reachableNodes);
    }

    static bool requirementHasNodeRequirements(u16 requirementIndex) {
        // sReqNodeCount is a compile-time constant, so when the authored data has
        // no node requirements at all this folds to `false` and the caller's whole
        // deferred-retry path optimizes out.
        if (LD::sReqNodeCount == 0 || requirementIndex >= LD::sRequirementCount)
            return false;
        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode != LD::RequirementMode_Custom)
            return false;
        for (s32 altIdx = 0; altIdx < requirement.altCount; altIdx++)
            if (LD::sReqAlternatives[requirement.altOffset + altIdx].nodeCount > 0)
                return true;
        return false;
    }

    static bool maskingCasualOptions() {
        return difficultFillMasks() && !sCasualFallbackActive && !sNoLogicFill
            && Randomizer::getLogicDifficulty() != Randomizer::LogicDifficulty_Casual;
    }

    // Whether this requirement evaluation offers a scenario-eligible alternative
    // that is harder than casual yet still within the selected logic difficulty.
    // Only then does Prioritize Difficult Movement ignore the casual alternatives:
    // Advanced never consults Expert methods, while Expert keeps Advanced ones.
    static bool hasEligibleDifficultOption(
        u16 requirementIndex,
        u16 difficultyOffset,
        u8 difficultyCount,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario,
        const bool* reachableNodes) {
        s32 optionCount = 1;
        if (requirementIndex < LD::sRequirementCount) {
            const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
            if (requirement.mode == LD::RequirementMode_Custom)
                optionCount = requirement.altCount;
        }
        for (s32 optionIndex = 0; optionIndex < optionCount; optionIndex++) {
            const LD::RouteDifficulty difficulty = optionDifficultyAt(difficultyOffset, difficultyCount, optionIndex);
            if (difficulty == LD::RouteDifficulty_Casual
                || !routeDifficultyAllowed(difficulty))
                continue;
            // A harder option still waiting on an unreachable node cannot stand in
            // for the casual ones, so it must not mask them.
            if (!altNodeRequirementsSatisfied(requirementIndex, optionIndex, reachableNodes))
                continue;
            if (optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount, optionIndex, scenario))
                return true;
        }
        return false;
    }

    static s16 routeEdgeStage(const LD::RouteEdge& edge) {
        if (edge.fromNode >= 0 && edge.fromNode < LD::sNodeCount)
            return LD::sNodes[edge.fromNode].stage;
        return -1;
    }

    // reachableNodes is required rather than defaulted: passing nullptr grants
    // every node-access requirement, which is right for the maximal-state proofs
    // and wrong for anything that decides real reachability. Callers must choose.
    static bool routeRequirementSatisfied(
        const LogicState& state, const LD::RouteEdge& edge, s16 scenario,
        const bool* reachableNodes) {
        if (edge.requirement >= LD::sRequirementCount)
            return routeOptionScenarioAllowed(edge, 0, scenario)
                && (sNoLogicFill || routeOptionDifficultyAllowed(edge, 0));

        const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
        if (requirement.mode == LD::RequirementMode_Unset || requirement.mode == LD::RequirementMode_Free)
            return routeOptionScenarioAllowed(edge, 0, scenario)
                && (sNoLogicFill || routeOptionDifficultyAllowed(edge, 0));
        if (requirement.mode == LD::RequirementMode_Raw)
            return routeOptionScenarioAllowed(edge, 0, scenario)
                && (sNoLogicFill || routeOptionDifficultyAllowed(edge, 0));
        if (requirement.mode == LD::RequirementMode_Impossible)
            return false;

        const s16 edgeStage = routeEdgeStage(edge);
        const bool skipCasualOptions = maskingCasualOptions()
            && hasEligibleDifficultOption(
                edge.requirement, edge.difficultyOffset, edge.difficultyCount,
                edge.optionScenarioOffset, edge.optionScenarioCount, scenario,
                reachableNodes);
        for (s32 altIdx = 0; altIdx < requirement.altCount; altIdx++) {
            if (!routeOptionScenarioAllowed(edge, altIdx, scenario))
                continue;
            // No Logic drops authored requirements wholesale, node access included,
            // matching randomizer_logic.reachable_nodes short-circuiting on
            // any_option_scenario_allowed before it ever evaluates a requirement.
            if (sNoLogicFill)
                return true;
            const LD::RouteDifficulty optionDifficulty = optionDifficultyAt(edge.difficultyOffset, edge.difficultyCount, altIdx);
            if (!routeDifficultyAllowed(optionDifficulty))
                continue;
            if (skipCasualOptions && optionDifficulty == LD::RouteDifficulty_Casual)
                continue;

            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIdx];
            if (!altNodeRequirementsSatisfied(alt, reachableNodes))
                continue;
            bool all = true;
            for (s32 tokenOffset = 0; tokenOffset < alt.tokenCount; tokenOffset++) {
                const s32 sourceToken = LD::sReqTokens[alt.tokenOffset + tokenOffset];
                bool tokenAvailable = false;
                if (sourceToken >= 0 && sourceToken < LD::sTokenCount
                    && LD::sTokens[sourceToken].type == LD::TokenType_Capture
                    && EnemySwapRando::hasRouteCaptureGates(
                        edge.edgeId, LD::sTokens[sourceToken].key)) {
                    s16 replacements[16] = {};
                    const s32 count = EnemySwapRando::getSatisfiedRouteCaptureTokens(
                        edge.edgeId, LD::sTokens[sourceToken].key,
                        scenario, replacements, 16);
                    for (s32 replacement = 0; replacement < count; replacement++)
                        if (hasToken(state, replacements[replacement])) {
                            tokenAvailable = true;
                            break;
                        }
                } else {
                    tokenAvailable = hasToken(
                        state,
                        EnemySwapRando::remapLogicToken(
                            edgeStage, sourceToken));
                }
                if (!tokenAvailable) {
                    all = false;
                    break;
                }
            }
            if (all)
                return true;
        }

        return false;
    }

    static bool requirementSatisfiedForScenario(
        const LogicState& state,
        u16 requirementIndex,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario,
        s16 stage,
        const bool* reachableNodes = nullptr) {
        if (requirementIndex >= LD::sRequirementCount)
            return optionScenarioAllowed(optionScenarioOffset, optionScenarioCount, 0, scenario);

        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode == LD::RequirementMode_Unset || requirement.mode == LD::RequirementMode_Free)
            return optionScenarioAllowed(optionScenarioOffset, optionScenarioCount, 0, scenario);
        if (requirement.mode == LD::RequirementMode_Raw)
            return optionScenarioAllowed(optionScenarioOffset, optionScenarioCount, 0, scenario);
        if (requirement.mode == LD::RequirementMode_Impossible)
            return false;

        for (s32 altIdx = 0; altIdx < requirement.altCount; altIdx++) {
            if (!optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount, altIdx, scenario))
                continue;

            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIdx];
            if (!altNodeRequirementsSatisfied(alt, reachableNodes))
                continue;
            bool all = true;
            for (s32 tokenOffset = 0; tokenOffset < alt.tokenCount; tokenOffset++) {
                const s32 token = EnemySwapRando::remapLogicToken(
                    stage, LD::sReqTokens[alt.tokenOffset + tokenOffset]);
                if (!hasToken(state, token)) {
                    all = false;
                    break;
                }
            }
            if (all)
                return true;
        }

        return false;
    }

    // Capture requirements on individual moons are represented by inert,
    // generated route targets (moon_state_<uid> / moon_local_<uid>).  This
    // keeps their physical replacement witnesses in the same solver table as
    // capture-gated traversal edges without adding reachability graph edges.
    static void makeMoonCaptureGateId(
        char* output, size_t outputSize, const LD::MoonAccess& moon,
        bool isStateRequirement) {
        std::snprintf(
            output, outputSize, "moon_%s_%d",
            isStateRequirement ? "state" : "local", moon.uid);
    }

    static bool moonRequirementSatisfiedForScenario(
        const LogicState& state,
        u16 requirementIndex,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario,
        const LD::MoonAccess& moon,
        bool isStateRequirement,
        const bool* reachableNodes) {
        if (requirementIndex >= LD::sRequirementCount)
            return optionScenarioAllowed(
                optionScenarioOffset, optionScenarioCount, 0, scenario);

        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode == LD::RequirementMode_Unset
            || requirement.mode == LD::RequirementMode_Free
            || requirement.mode == LD::RequirementMode_Raw)
            return optionScenarioAllowed(
                optionScenarioOffset, optionScenarioCount, 0, scenario);
        if (requirement.mode == LD::RequirementMode_Impossible)
            return false;

        char gateId[48] = {};
        makeMoonCaptureGateId(
            gateId, sizeof(gateId), moon, isStateRequirement);
        // The state requirement carries no difficulty data (always casual); the
        // local requirement uses the moon's per-option difficulties, mirroring the
        // host-side randomizer_logic.py moon_capture_edge.
        const u16 difficultyOffset = isStateRequirement ? 0 : moon.localDifficultyOffset;
        const u8 difficultyCount = isStateRequirement ? 0 : moon.localDifficultyCount;
        const bool skipCasualOptions = maskingCasualOptions()
            && hasEligibleDifficultOption(
                requirementIndex, difficultyOffset, difficultyCount,
                optionScenarioOffset, optionScenarioCount, scenario,
                reachableNodes);
        for (s32 altIdx = 0; altIdx < requirement.altCount; altIdx++) {
            if (!optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount,
                    altIdx, scenario))
                continue;
            if (!sNoLogicFill) {
                const LD::RouteDifficulty optionDifficulty = optionDifficultyAt(difficultyOffset, difficultyCount, altIdx);
                if (!routeDifficultyAllowed(optionDifficulty))
                    continue;
                if (skipCasualOptions
                    && optionDifficulty == LD::RouteDifficulty_Casual)
                    continue;
            }

            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIdx];
            if (!altNodeRequirementsSatisfied(alt, reachableNodes))
                continue;
            bool all = true;
            for (s32 tokenOffset = 0;
                 tokenOffset < alt.tokenCount; tokenOffset++) {
                const s32 sourceToken = LD::sReqTokens[alt.tokenOffset + tokenOffset];
                bool tokenAvailable = false;
                if (sourceToken >= 0 && sourceToken < LD::sTokenCount
                    && LD::sTokens[sourceToken].type
                        == LD::TokenType_Capture
                    && EnemySwapRando::hasRouteCaptureGates(
                        gateId, LD::sTokens[sourceToken].key)) {
                    s16 replacements[16] = {};
                    const s32 count = EnemySwapRando::
                        getSatisfiedRouteCaptureTokens(
                            gateId, LD::sTokens[sourceToken].key,
                            scenario, replacements, 16);
                    for (s32 replacement = 0;
                         replacement < count; replacement++) {
                        if (hasToken(state, replacements[replacement])) {
                            tokenAvailable = true;
                            break;
                        }
                    }
                } else {
                    tokenAvailable = hasToken(
                        state,
                        EnemySwapRando::remapLogicToken(
                            moon.stageIndex, sourceToken));
                }
                if (!tokenAvailable) {
                    all = false;
                    break;
                }
            }
            if (all)
                return true;
        }
        return false;
    }

    static bool anyRequirementOptionScenarioAllowed(
        u16 requirementIndex,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario) {
        s32 optionCount = 1;
        if (requirementIndex < LD::sRequirementCount) {
            const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
            if (requirement.mode == LD::RequirementMode_Custom)
                optionCount = requirement.altCount;
        }
        for (s32 optionIndex = 0; optionIndex < optionCount; optionIndex++)
            if (optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount, optionIndex, scenario))
                return true;
        return false;
    }

    static bool isNodeScenarioActive(const LogicState& state, s32 node) {
        // Location/entrance NODES are always present. Scenario restrictions live
        // on route edges (option scenario masks) and on moons/moon-rock, never on
        // the nodes themselves. Gating node presence by scenario created deadlocks
        // (e.g. the MoonRockActive node carries the very scenario it unlocks, so it
        // could never be reached to trigger that scenario's advance). Mirrors the
        // host-side randomizer_logic.py is_node_active.
        (void)state;
        if (node < 0 || node >= LD::sNodeCount)
            return false;
        return !LD::sNodes[node].excluded;
    }

    // Whether an entrance's loading-zone object physically exists in the kingdom's
    // CURRENT scenario, so it can be *used to leave*. This is the only place node
    // scenario data gates traversal: plain nodes, checkpoints, and arriving at a
    // node (spawning in from another subarea) are never scenario-gated -- you can
    // stand on a loading zone that does not exist as an exit yet, you just can't
    // take it out of the kingdom until its scenario arrives. Mirrors the host-side
    // randomizer_logic.py entrance_node_usable_in_scenario. Empty scenario data or
    // an unknown node => always usable.
    static bool isEntranceNodeUsableInScenario(const LogicState& state, s32 node) {
        if (node < 0 || node >= LD::sNodeCount)
            return true;
        const LD::Node& n = LD::sNodes[node];
        if (n.excluded || n.scenarioMask == 0)
            return true;
        const s32 stage = n.stage;
        if (!isValidStage(stage))
            return true;
        const s32 kingdom = LD::sStages[stage].kingdom;
        if (!isValidKingdom(kingdom))
            return true;
        const s16 current = state.kingdomScenarios[kingdom];
        if (n.scenarioFloor >= 0)
            return current >= n.scenarioFloor;
        return scenarioMaskContains(n.scenarioMask, current);
    }

    // Where an entrance-rando arrival lands when the target home-side loading
    // zone does not exist in that kingdom's current scenario.
    //
    // EntranceHooks no longer just lets the spawn lookup fail and drop Mario at
    // the stage's default start: EntranceFallbacks.h authors, per entrance and
    // per scenario, whether he is placed at the entrance anyway (an explicit
    // vanilla override, forced coordinates, or a generated fake pipe/moon pipe
    // standing at that spot) or genuinely handed back to the Odyssey. Only the
    // Odyssey case costs the player the node; the rest leave him standing on
    // it, so logic and the hint tracker must credit it.
    //
    // Note this is about ARRIVING. Using a loading zone to *leave* stays gated
    // by isEntranceNodeUsableInScenario alone - a fake pipe placed for arrivals
    // is not an exit back out.
    static bool homeArrivalPlacesPlayerAtEntrance(const LogicState& state, s32 linkIndex, bool outSide) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        if (!link)
            return false;

        ensureLinkStageIndexes();
        const s32 stage = sLinkHomeStageIndex[linkIndex];
        if (!isValidStage(stage))
            return false;
        const s32 kingdom = LD::sStages[stage].kingdom;
        if (!isValidKingdom(kingdom))
            return false;

        const char* changeStageId = outSide ? link->homeEntranceOut : link->homeEntranceIn;
        return EntranceFallbacks::arrivalPlacesPlayerAtEntrance(
            link->homeStage, changeStageId, state.kingdomScenarios[kingdom]);
    }

    static bool addReachableNode(
        const LogicState& state,
        bool* reachable,
        NodeTrace* traces,
        s32* queue,
        s32& queueEnd,
        s32 node,
        s32 previousNode,
        s32 edgeIndex,
        NodeSeedKind seedKind) {
        if (node < 0 || node >= LD::sNodeCount || reachable[node] || LD::sNodes[node].excluded)
            return false;
        if (!isNodeScenarioActive(state, node))
            return false;
        reachable[node] = true;
        if (traces) {
            traces[node].reached = true;
            traces[node].previousNode = (s16)previousNode;
            traces[node].edgeIndex = (s16)edgeIndex;
            traces[node].seedKind = seedKind;
        }
        queue[queueEnd++] = node;
        return true;
    }

    static bool shouldSeedStageStart(s32 stage) {
        if (!isValidStage(stage))
            return false;
        const s32 kingdom = LD::sStages[stage].kingdom;
        return isValidKingdom(kingdom) && LD::sMainStageByKingdom[kingdom] == stage;
    }

    static bool sNodeGraphIndexesReady = false;
    static s16 sFirstNodeByStage[LD::sStageCount];
    static s16 sNextNodeInStage[LD::sNodeCount];
    static s16 sFirstDirectedEdgeByNode[LD::sNodeCount];
    static s16 sNextDirectedEdge[LD::sRouteEdgeCount * 2];
    static s16 sDirectedEdgeToNode[LD::sRouteEdgeCount * 2];
    static s16 sDirectedEdgeRouteIndex[LD::sRouteEdgeCount * 2];
    static s16 sDirectedEdgeCount = 0;

    static void addDirectedRouteEdge(s32 fromNode, s32 toNode, s32 routeIndex) {
        if (fromNode < 0 || fromNode >= LD::sNodeCount
            || toNode < 0 || toNode >= LD::sNodeCount
            || sDirectedEdgeCount >= (s32)(LD::sRouteEdgeCount * 2))
            return;
        const s32 directed = sDirectedEdgeCount++;
        sDirectedEdgeToNode[directed] = (s16)toNode;
        sDirectedEdgeRouteIndex[directed] = (s16)routeIndex;
        sNextDirectedEdge[directed] = sFirstDirectedEdgeByNode[fromNode];
        sFirstDirectedEdgeByNode[fromNode] = (s16)directed;
    }

    static void ensureNodeGraphIndexes() {
        if (sNodeGraphIndexesReady)
            return;
        sNodeGraphIndexesReady = true;
        for (s32 stage = 0; stage < LD::sStageCount; stage++)
            sFirstNodeByStage[stage] = -1;
        for (s32 node = 0; node < LD::sNodeCount; node++) {
            sNextNodeInStage[node] = -1;
            const s32 stage = LD::sNodes[node].stage;
            if (isValidStage(stage)) {
                sNextNodeInStage[node] = sFirstNodeByStage[stage];
                sFirstNodeByStage[stage] = (s16)node;
            }
            sFirstDirectedEdgeByNode[node] = -1;
        }
        for (s32 edge = 0; edge < LD::sRouteEdgeCount; edge++) {
            addDirectedRouteEdge(LD::sRouteEdges[edge].fromNode, LD::sRouteEdges[edge].toNode, edge);
            if (LD::sRouteEdges[edge].bidirectional)
                addDirectedRouteEdge(LD::sRouteEdges[edge].toNode, LD::sRouteEdges[edge].fromNode, edge);
        }
    }

    static void computeReachableNodes(const LogicState& state, bool* reachable, NodeTrace* traces = nullptr) {
        ensureNodeGraphIndexes();
        std::memset(reachable, 0, sizeof(bool) * LD::sNodeCount);
        if (traces) {
            for (s32 i = 0; i < LD::sNodeCount; i++) {
                traces[i].reached = false;
                traces[i].previousNode = -1;
                traces[i].edgeIndex = -1;
                traces[i].seedKind = NodeSeed_None;
            }
        }

        s32 queue[LD::sNodeCount];
        s32 queueStart = 0;
        s32 queueEnd = 0;

        for (s32 node = 0; node < LD::sNodeCount; node++) {
            if (state.accessibleNodes[node])
                addReachableNode(state, reachable, traces, queue, queueEnd, node, -1, -1, NodeSeed_Arrival);
        }

        for (s32 stage = 0; stage < LD::sStageCount; stage++) {
            if (!state.accessibleStages[stage])
                continue;

            for (s32 node = sFirstNodeByStage[stage]; node >= 0; node = sNextNodeInStage[node]) {
                const LD::Node& logicNode = LD::sNodes[node];
                if (logicNode.excluded)
                    continue;

                bool isFree = logicNode.type == LD::NodeType_StageStart && shouldSeedStageStart(stage);
                if (!isFree && !LD::sStages[stage].hasRoutes)
                    isFree = true;

                if (isFree)
                    addReachableNode(state, reachable, traces, queue, queueEnd, node, -1, -1, NodeSeed_StageStart);
            }
        }

        // Source nodes that held an edge blocked only by node access. Reaching more
        // nodes can open those edges, so the sweep below re-runs them until the
        // reachable set stops growing - reachability is a monotone fixed point once
        // an @<node_id> requirement can point at a node reached later. Static, like
        // the queue tables above: these are node-sized and this runs on a small stack.
        static s32 sDeferred[LD::sNodeCount];
        static s32 sDeferredRetry[LD::sNodeCount];
        static bool sDeferredQueued[LD::sNodeCount];
        s32 deferredCount = 0;

        auto relax = [&](s32 node) {
            for (s32 directed = sFirstDirectedEdgeByNode[node];
                 directed >= 0;
                 directed = sNextDirectedEdge[directed]) {
                const s32 edgeIndex = sDirectedEdgeRouteIndex[directed];
                const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                const s32 next = sDirectedEdgeToNode[directed];

                if (next < 0 || next >= LD::sNodeCount)
                    continue;
                if (edge.scenarioMask != 0) {
                    const s32 stage = LD::sNodes[node].stage;
                    const s32 kingdom = isValidStage(stage) ? LD::sStages[stage].kingdom : -1;
                    if (isValidKingdom(kingdom)
                        && !scenarioMaskContains(edge.scenarioMask, state.kingdomScenarios[kingdom]))
                        continue;
                }
                const s32 stage = LD::sNodes[node].stage;
                const s32 kingdom = isValidStage(stage) ? LD::sStages[stage].kingdom : -1;
                const s16 scenario = isValidKingdom(kingdom) ? state.kingdomScenarios[kingdom] : -1;
                if (!routeRequirementSatisfied(state, edge, scenario, reachable)) {
                    if (!sDeferredQueued[node] && requirementHasNodeRequirements(edge.requirement)) {
                        sDeferredQueued[node] = true;
                        sDeferred[deferredCount++] = node;
                    }
                    continue;
                }
                addReachableNode(state, reachable, traces, queue, queueEnd, next, node, edgeIndex, NodeSeed_None);
            }
        };

        while (queueStart < queueEnd)
            relax(queue[queueStart++]);

        // Every reachable node is pushed exactly once, so queueEnd is the size of
        // the reachable set: a pass that does not raise it has found its fixed point.
        while (deferredCount > 0) {
            const s32 retryCount = deferredCount;
            for (s32 i = 0; i < retryCount; i++) {
                sDeferredRetry[i] = sDeferred[i];
                sDeferredQueued[sDeferredRetry[i]] = false;
            }
            deferredCount = 0;

            const s32 reachedBefore = queueEnd;
            for (s32 i = 0; i < retryCount; i++)
                relax(sDeferredRetry[i]);
            while (queueStart < queueEnd)
                relax(queue[queueStart++]);
            if (queueEnd == reachedBefore)
                break;
        }

        // Leave no stale marks for the next call: the loop above can break out with
        // entries still parked.
        for (s32 i = 0; i < deferredCount; i++)
            sDeferredQueued[sDeferred[i]] = false;
    }

    // Whether a link sub-area holds any moon location. Purely structural (it
    // reads the moon table, not the current state), so one pass over the moon
    // table per link is enough and the answer is cached for the whole session.
    bool linkSubAreaHasLocations(s32 linkIndex) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount)
            return false;
        if (!sLinkSubHasLocationsReady) {
            ensureLinkStageIndexes();
            for (s32 i = 0; i < EntranceDB::sLinkCount; i++)
                sLinkSubHasLocations[i] = 0;
            for (s32 moon = 0; moon < LD::sMoonCount; moon++) {
                const s32 stage = LD::sMoons[moon].stageIndex;
                if (!isValidStage(stage))
                    continue;
                for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
                    if (sLinkSubStageIndex[i] == stage)
                        sLinkSubHasLocations[i] = 1;
                }
            }
            sLinkSubHasLocationsReady = true;
        }
        return sLinkSubHasLocations[linkIndex] != 0;
    }

    // Whether a link's sub-area can be crossed from one door to the other, used by
    // the shuffle builder to decide if a chain may pass through a sub. Evaluated under
    // maximal state (every token, widest scenario) and restricted to the sub's own
    // stage, so it answers "is this room physically traversable at all", honoring the
    // selected logic difficulty (impossible edges stay impossible). Cached per side.
    bool canEntranceSubBeTraversed(s32 linkIndex, bool enteredFromFarSide) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount || linkIndex >= LD::sEntranceCount)
            return true;
        if (!Randomizer::isLogicRandoEnabled())
            return true;

        const s32 difficulty = (s32)Randomizer::getLogicDifficulty();
        if (difficulty != sSubTraversalCacheDifficulty) {
            std::memset(sSubTraversalCache, -1, sizeof(sSubTraversalCache));
            sSubTraversalCacheDifficulty = difficulty;
        }
        s8& cached = sSubTraversalCache[linkIndex][enteredFromFarSide ? 1 : 0];
        if (cached >= 0)
            return cached != 0;

        // This is a "physically traversable at all" proof under maximal state and
        // its cache is keyed on difficulty only; evaluate it without the difficult-
        // movement fill mask so fill-time and post-fill answers stay identical.
        const s32 savedPrioritizeFill = sDifficultFillLevel;
        sDifficultFillLevel = -1;

        const LD::EntranceAccess& access = LD::sEntrances[linkIndex];
        const s32 entry = enteredFromFarSide ? access.subFarNode : access.subNearNode;
        const s32 target = enteredFromFarSide ? access.subNearNode : access.subFarNode;

        bool result = true;
        if (entry >= 0 && target >= 0 && entry != target && entry < LD::sNodeCount && target < LD::sNodeCount) {
            const s32 stage = LD::sNodes[entry].stage;

            // Maximal state, like the entrance proof: every token, widest scenario.
            // Static — LogicState is far too large for the stack.
            static LogicState traversalState;
            std::memset(&traversalState, 0, sizeof(traversalState));
            for (s32 i = 0; i < LD::sKingdomCount; i++) {
                const s16 maxScenario = LD::sMaxLogicScenarioByKingdom[i];
                traversalState.kingdomScenarios[i] = maxScenario > 0 ? maxScenario : LD::sStartScenarioByKingdom[i];
            }
            for (s32 i = 0; i < LD::sTokenCount; i++)
                traversalState.tokens[i] = true;

            // BFS restricted to the sub's own stage.
            static bool reachable[LD::sNodeCount];
            std::memset(reachable, 0, sizeof(reachable));
            static s32 queue[LD::sNodeCount];
            s32 queueStart = 0;
            s32 queueEnd = 0;

            result = false;
            if (isNodeScenarioActive(traversalState, entry)) {
                reachable[entry] = true;
                queue[queueEnd++] = entry;
            }
            const s32 kingdom = isValidStage(stage) ? LD::sStages[stage].kingdom : -1;
            while (queueStart < queueEnd && !result) {
                const s32 node = queue[queueStart++];
                if (node == target) {
                    result = true;
                    break;
                }
                for (s32 edgeIndex = 0; edgeIndex < LD::sRouteEdgeCount; edgeIndex++) {
                    const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                    s32 next = -1;
                    if (edge.fromNode == node)
                        next = edge.toNode;
                    else if (edge.bidirectional && edge.toNode == node)
                        next = edge.fromNode;
                    else
                        continue;
                    if (next < 0 || next >= LD::sNodeCount || reachable[next] || LD::sNodes[next].excluded)
                        continue;
                    if (LD::sNodes[next].stage != stage)
                        continue;
                    if (edge.scenarioMask != 0 && isValidKingdom(kingdom)
                        && !scenarioMaskContains(edge.scenarioMask, traversalState.kingdomScenarios[kingdom]))
                        continue;
                    const s16 scenario = isValidKingdom(kingdom)
                        ? traversalState.kingdomScenarios[kingdom]
                        : -1;
                    // No reachable set: this proof already grants every token, so
                    // node-access requirements are granted with them.
                    if (!routeRequirementSatisfied(traversalState, edge, scenario, nullptr))
                        continue;
                    if (!isNodeScenarioActive(traversalState, next))
                        continue;
                    reachable[next] = true;
                    queue[queueEnd++] = next;
                }
            }
            if (!result)
                result = reachable[target];
            // Reaching the exit node via the stage's own routes isn't enough:
            // using it to actually leave requires it to be usable in scenario
            // right now, same as calcSubareaForwardExitDoor checks at proof
            // time. Without this, a door that's route-reachable but scenario-
            // gated gets treated as a working passthrough, feeding a phantom
            // chain-exit into the main loop that never actually fires later.
            if (result && !isEntranceNodeUsableInScenario(traversalState, target))
                result = false;
        }

        sDifficultFillLevel = savedPrioritizeFill;
        cached = result ? 1 : 0;
        return result;
    }

    bool isEntranceHomeStageAlwaysAccessible(s32 linkIndex) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount)
            return false;
        ensureLinkStageIndexes();
        return shouldSeedStageStart(sLinkHomeStageIndex[linkIndex]);
    }

    static s8 sHomeTraversalCache[EntranceDB::sLinkCount][2]; // [outSide]
    static s32 sHomeTraversalCacheDifficulty = -1;

    // Whether linkIndex's home-side node (home_in, or home_out if outSide) can
    // be reached from its own kingdom's default start using only that
    // kingdom's own internal route graph (no entrance/painting jumps), under
    // maximal state. isEntranceHomeStageAlwaysAccessible only proves the
    // stage IS a kingdom's own main stage - some doors nominally on that
    // stage are still gated behind other in-kingdom progression (a one-way
    // drop, a section only opened by walking out of a different sub-area)
    // and aren't actually free-standing roots the way a true overworld pipe
    // is. Mirrors canEntranceSubBeTraversed's "BFS restricted to the sub's
    // own stage" pattern, seeded from the stage's own StageStart node(s)
    // instead of a specific entry node. Cached per side.
    bool isEntranceHomeSideUnconditionallyReachable(s32 linkIndex, bool outSide) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount || linkIndex >= LD::sEntranceCount)
            return false;
        if (!isEntranceHomeStageAlwaysAccessible(linkIndex))
            return false;
        if (!Randomizer::isLogicRandoEnabled())
            return true;

        const s32 difficulty = (s32)Randomizer::getLogicDifficulty();
        if (difficulty != sHomeTraversalCacheDifficulty) {
            std::memset(sHomeTraversalCache, -1, sizeof(sHomeTraversalCache));
            sHomeTraversalCacheDifficulty = difficulty;
        }
        s8& cached = sHomeTraversalCache[linkIndex][outSide ? 1 : 0];
        if (cached >= 0)
            return cached != 0;

        // Maximal-state proof cached per difficulty only; evaluate without the
        // difficult-movement fill mask, same as canEntranceSubBeTraversed.
        const s32 savedPrioritizeFill = sDifficultFillLevel;
        sDifficultFillLevel = -1;

        ensureNodeGraphIndexes();
        const s32 stage = sLinkHomeStageIndex[linkIndex];
        const LD::EntranceAccess& access = LD::sEntrances[linkIndex];
        const s32 target = outSide ? access.homeOutNode : access.homeInNode;

        bool result = true;
        if (target >= 0 && target < LD::sNodeCount && isValidStage(stage)) {
            // Maximal state, like the entrance proof: every token, widest scenario.
            static LogicState traversalState;
            std::memset(&traversalState, 0, sizeof(traversalState));
            for (s32 i = 0; i < LD::sKingdomCount; i++) {
                const s16 maxScenario = LD::sMaxLogicScenarioByKingdom[i];
                traversalState.kingdomScenarios[i] = maxScenario > 0 ? maxScenario : LD::sStartScenarioByKingdom[i];
            }
            for (s32 i = 0; i < LD::sTokenCount; i++)
                traversalState.tokens[i] = true;

            // A door that can't presently be used as an entrance under maximal
            // state (its own scenario floor/mask isn't met) is never a free-
            // standing root no matter how reachable its node is within the
            // stage - mirrors canUseHomeEntranceSide's identical gate at proof
            // time. Checked before the hasRoutes branch so both paths agree.
            if (!isEntranceNodeUsableInScenario(traversalState, target)) {
                result = false;
            } else if (!LD::sStages[stage].hasRoutes) {
                result = isNodeScenarioActive(traversalState, target);
            } else {
                static bool reachable[LD::sNodeCount];
                std::memset(reachable, 0, sizeof(reachable));
                static s32 queue[LD::sNodeCount];
                s32 queueStart = 0;
                s32 queueEnd = 0;

                const s32 kingdom = LD::sStages[stage].kingdom;
                for (s32 node = sFirstNodeByStage[stage]; node >= 0; node = sNextNodeInStage[node]) {
                    const LD::Node& logicNode = LD::sNodes[node];
                    if (logicNode.excluded)
                        continue;
                    if (logicNode.type == LD::NodeType_StageStart && isNodeScenarioActive(traversalState, node)) {
                        reachable[node] = true;
                        queue[queueEnd++] = node;
                    }
                }

                result = false;
                while (queueStart < queueEnd && !result) {
                    const s32 node = queue[queueStart++];
                    if (node == target) {
                        result = true;
                        break;
                    }
                    for (s32 edgeIndex = 0; edgeIndex < LD::sRouteEdgeCount; edgeIndex++) {
                        const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                        s32 next = -1;
                        if (edge.fromNode == node)
                            next = edge.toNode;
                        else if (edge.bidirectional && edge.toNode == node)
                            next = edge.fromNode;
                        else
                            continue;
                        if (next < 0 || next >= LD::sNodeCount || reachable[next] || LD::sNodes[next].excluded)
                            continue;
                        if (LD::sNodes[next].stage != stage)
                            continue;
                        if (edge.scenarioMask != 0 && isValidKingdom(kingdom)
                            && !scenarioMaskContains(edge.scenarioMask, traversalState.kingdomScenarios[kingdom]))
                            continue;
                        const s16 scenario = isValidKingdom(kingdom)
                            ? traversalState.kingdomScenarios[kingdom]
                            : -1;
                        // Maximal state, as above: node access is granted too.
                        if (!routeRequirementSatisfied(traversalState, edge, scenario, nullptr))
                            continue;
                        if (!isNodeScenarioActive(traversalState, next))
                            continue;
                        reachable[next] = true;
                        queue[queueEnd++] = next;
                    }
                }
                if (!result)
                    result = reachable[target];
            }
        }

        sDifficultFillLevel = savedPrioritizeFill;
        cached = result ? 1 : 0;
        return result;
    }

    static bool addAccessibleNodeWithTrace(
        LogicState& state,
        s32 node,
        StageTraceKind traceKind,
        s32 sourceStage,
        s32 sourceEntrance,
        s32 destEntrance,
        s32 entranceChainFirstLink = -1,
        s32 entranceChainFirstKind = EntranceRando::ConnKind_None) {
        if (node < 0 || node >= LD::sNodeCount || LD::sNodes[node].excluded)
            return false;
        if (!isNodeScenarioActive(state, node))
            return false;

        bool changed = false;
        if (!state.accessibleNodes[node]) {
            state.accessibleNodes[node] = true;
            changed = true;
        }

        const s32 stage = LD::sNodes[node].stage;
        changed |= addAccessibleStageWithTrace(
            state, stage, traceKind, sourceStage, sourceEntrance, destEntrance,
            entranceChainFirstLink, entranceChainFirstKind);
        return changed;
    }

    // The logic node you arrive at by following an entrance connection, resolved
    // from the connection's kind. A connection can land you in the destination's
    // sub-area (near/far) OR surface you at the destination's overworld side
    // (home in/out) — the shuffle's phase 2 wires pipe-to-overworld and direct
    // kingdom-to-kingdom doors, so home-side arrivals are real, not just sub-areas.
    static s16 arrivalNodeForConn(EntranceRando::Conn conn) {
        if (conn.link < 0 || conn.link >= EntranceDB::sLinkCount)
            return -1;
        const LD::EntranceAccess& access = LD::sEntrances[conn.link];
        switch (conn.kind) {
        case EntranceRando::ConnKind_SubNear:
            return access.subNearNode;
        case EntranceRando::ConnKind_SubFar:
            return access.subFarNode;
        case EntranceRando::ConnKind_HomeOut:
            return access.homeOutNode;
        case EntranceRando::ConnKind_HomeIn:
            return access.homeInNode;
        default:
            return -1;
        }
    }

    static bool connIsSubArrival(EntranceRando::Conn conn) {
        return conn.kind == EntranceRando::ConnKind_SubNear || conn.kind == EntranceRando::ConnKind_SubFar;
    }

    // Mark the place an entrance connection leads to as accessible. Honors the
    // connection kind so a home-side arrival opens the destination overworld
    // rather than mis-marking its sub-area.
    static bool addAccessibleEntranceConn(
        LogicState& state,
        s32 sourceIndex,
        EntranceRando::Conn conn,
        bool continuingFromSub = false) {
        const s32 destIndex = conn.link;
        if (conn.kind == EntranceRando::ConnKind_None || destIndex < 0 || destIndex >= EntranceDB::sLinkCount)
            return false;
        if (!isEntranceRandomizable(destIndex))
            return false;

        ensureLinkStageIndexes();
        const EntranceDB::EntranceLink& dest = EntranceDB::sLinks[destIndex];
        s32 sourceStage = sourceIndex >= 0 && sourceIndex < EntranceDB::sLinkCount
            ? (continuingFromSub ? sLinkSubStageIndex[sourceIndex] : sLinkHomeStageIndex[sourceIndex])
            : -1;
        s32 sourceEntrance = sourceIndex;
        s32 entranceChainFirstLink = conn.link;
        s32 entranceChainFirstKind = conn.kind;

        // Keep the immediate source stage for itinerary layering. The entrance
        // spoiler still renders complete static chains; area spheres should reveal
        // one newly usable layer at a time.

        const bool destIsSub = connIsSubArrival(conn);
        const s16 arrivalNode = arrivalNodeForConn(conn);
        const s32 arrivalStage = destIsSub ? sLinkSubStageIndex[destIndex] : sLinkHomeStageIndex[destIndex];

        bool changed = false;
        if (!destIsSub && arrivalNode >= 0 && !isEntranceNodeUsableInScenario(state, arrivalNode)
            && !homeArrivalPlacesPlayerAtEntrance(
                state, destIndex, conn.kind == EntranceRando::ConnKind_HomeOut)) {
            // Arriving at a home-stage entrance that is absent in the target kingdom's
            // current scenario, and whose fallback hands the player to the Odyssey
            // rather than placing him at the entrance - so only the stage's default
            // start is proven, not the entrance's own node.
            changed = addAccessibleStageWithTrace(
                state, arrivalStage, StageTrace_Entrance,
                sourceStage, sourceEntrance, destIndex,
                entranceChainFirstLink, entranceChainFirstKind);
        } else {
            changed = addAccessibleNodeWithTrace(
                state,
                arrivalNode,
                StageTrace_Entrance,
                sourceStage,
                sourceEntrance,
                destIndex,
                entranceChainFirstLink,
                entranceChainFirstKind);

            if (arrivalNode < 0)
                changed |= addAccessibleStageWithTrace(
                    state,
                    arrivalStage,
                    StageTrace_Entrance,
                    sourceStage,
                    sourceEntrance,
                    destIndex,
                    entranceChainFirstLink,
                    entranceChainFirstKind);
            else if (!state.accessibleNodes[arrivalNode])
                return changed;
        }

        // Record which side of the destination's sub-area we entered, so its other
        // door can continue the chain on a later fixpoint pass. Home-side arrivals
        // surface in an overworld and need no sub-entry bookkeeping.
        if (destIsSub) {
            const bool destIsFar = conn.kind == EntranceRando::ConnKind_SubFar;
            bool& entryFlag = destIsFar ? state.accessibleSubEntryFar[destIndex] : state.accessibleSubEntryNear[destIndex];
            if (!entryFlag) {
                entryFlag = true;
                changed = true;
            }
        }

        return changed;
    }

    // Re-seed destinations Mario has physically arrived at. A kingdom's Odyssey
    // landing only proves its default start node; a shuffled door or painting can
    // instead drop Mario halfway through that overworld (or onto either side of a
    // sub-area), opening checks the default start cannot reach. ArrivalLog stores
    // the stable link/side identity and this resolves it against the current seed.
    // `kingdomFilter` is used by Kingdom Checks' local view so a selected kingdom
    // receives its own discovered entry points without importing every other
    // kingdom's position state.
    static bool recordedNodeMatchesKingdom(s32 node, s32 kingdomFilter) {
        if (kingdomFilter < 0)
            return true;
        if (node < 0 || node >= LD::sNodeCount)
            return false;
        const s32 stage = LD::sNodes[node].stage;
        return isValidStage(stage) && LD::sStages[stage].kingdom == kingdomFilter;
    }

    static void seedRecordedArrivals(LogicState& state, s32 kingdomFilter = -1) {
        ensureLinkStageIndexes();
        static constexpr EntranceRando::ConnKind cKinds[] = {
            EntranceRando::ConnKind_SubNear,
            EntranceRando::ConnKind_SubFar,
            EntranceRando::ConnKind_HomeOut,
            EntranceRando::ConnKind_HomeIn,
        };

        for (s32 link = 0; link < EntranceDB::sLinkCount; link++) {
            for (EntranceRando::ConnKind kind : cKinds) {
                if (!ArrivalLog::hasArrived(link, kind))
                    continue;
                const EntranceRando::Conn conn = { static_cast<s16>(link), kind };
                const s32 node = arrivalNodeForConn(conn);
                if (!recordedNodeMatchesKingdom(node, kingdomFilter))
                    continue;

                const bool subArrival = connIsSubArrival(conn);
                const s32 stage = subArrival ? sLinkSubStageIndex[link]
                                             : sLinkHomeStageIndex[link];
                if (node >= 0)
                    addAccessibleNodeWithTrace(
                        state, node, StageTrace_Entrance, -1, -1, link,
                        link, kind);
                else
                    addAccessibleStageWithTrace(
                        state, stage, StageTrace_Entrance, -1, -1, link,
                        link, kind);

                if (kind == EntranceRando::ConnKind_SubNear)
                    state.accessibleSubEntryNear[link] = true;
                else if (kind == EntranceRando::ConnKind_SubFar)
                    state.accessibleSubEntryFar[link] = true;
            }
        }

        const bool paintingRandoActive
            = Randomizer::isPaintingRandoEnabled() && PaintingRando::isInitialized();
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            const LD::PaintingLink& link = LD::sPaintingLinks[i];
            if (ArrivalLog::hasPaintingArrival(i, true)) {
                const s32 node = paintingRandoActive
                    ? PaintingRando::getMappedDestinationNode(i)
                    : link.destinationNode;
                if (recordedNodeMatchesKingdom(node, kingdomFilter))
                    addAccessibleNodeWithTrace(
                        state, node, StageTrace_Painting, -1,
                        link.sourceNode, node);
            }
            if (ArrivalLog::hasPaintingArrival(i, false)) {
                const s32 node = paintingRandoActive
                    ? PaintingRando::getMappedReturnSourceNode(i)
                    : link.sourceNode;
                if (recordedNodeMatchesKingdom(node, kingdomFilter))
                    addAccessibleNodeWithTrace(
                        state, node, StageTrace_Painting, -1,
                        link.destinationNode, node);
            }
        }
    }

    static bool canUseHomeEntranceSide(const LogicState& state, const bool* reachableNodes, s32 entranceIndex, bool outSide) {
        if (entranceIndex < 0 || entranceIndex >= EntranceDB::sLinkCount)
            return false;
        if (!isEntranceRandomizable(entranceIndex))
            return false;
        // Subarea Keys gate USING the door to leave, so both overworld sides of
        // a locked link are closed until the key turns up.
        if (!stateCanEnterSubareaLink(state, entranceIndex))
            return false;

        ensureLinkStageIndexes();
        const EntranceDB::EntranceLink& link = EntranceDB::sLinks[entranceIndex];
        if (!outSide && !EntranceDB::isHomeInEnterable(link))
            return false;
        if (outSide && !EntranceDB::isHomeOutEnterable(link))
            return false;
        if (outSide && (!link.homeEntranceOut || strEq(link.homeEntranceIn, link.homeEntranceOut)))
            return false;

        const s32 homeStage = sLinkHomeStageIndex[entranceIndex];
        if (!isValidStage(homeStage) || !state.accessibleStages[homeStage])
            return false;

        const LD::EntranceAccess& access = LD::sEntrances[entranceIndex];
        const s32 node = outSide ? access.homeOutNode : access.homeInNode;
        if (!isEntranceNodeUsableInScenario(state, node))
            return false;
        if (!LD::sStages[homeStage].hasRoutes)
            return node < 0 || isNodeScenarioActive(state, node);

        return node >= 0 && reachableNodes[node];
    }

    static bool canUseDeepWoodsVineExit(const LogicState& state, const bool* reachableNodes) {
        if (!reachableNodes)
            return false;

        const s32 primary = EntranceDB::findDeepWoodsPrimaryLink();
        if (primary < 0 || !isEntranceRandomizable(primary))
            return false;

        for (s32 i = 0; i < EntranceDB::sLinkCount && i < LD::sEntranceCount; i++) {
            const EntranceDB::EntranceLink& link = EntranceDB::sLinks[i];
            if (!EntranceDB::isDeepWoodsArea(link))
                continue;

            const LD::EntranceAccess& access = LD::sEntrances[i];
            if (EntranceDB::isDeepWoodsVineExit(link.subEntranceIn)
                && access.subNearNode >= 0
                && reachableNodes[access.subNearNode]
                && isEntranceNodeUsableInScenario(state, access.subNearNode))
                return true;
            if (EntranceDB::isDeepWoodsVineExit(link.subEntranceOut)
                && access.subFarNode >= 0
                && reachableNodes[access.subFarNode]
                && isEntranceNodeUsableInScenario(state, access.subFarNode))
                return true;
        }

        return false;
    }

    enum SubExitDoor : s32
    {
        SubExitDoor_None = -1,
        SubExitDoor_Near = 0,
        SubExitDoor_Far = 1,
    };

    // Once inside a link's sub-area, which door can the player leave through to
    // continue a chain? You exit the side OPPOSITE the one you entered: reaching
    // the far side exits the far door, and having entered via the far side then
    // reaching the near side exits the near door. Returns SubExitDoor_None if no
    // forward exit is usable. (Same gating as before, but now reports the door so
    // the caller can follow that door's actual connection.)
    static s32 calcSubareaForwardExitDoor(const LogicState& state, const bool* reachableNodes, s32 entranceIndex) {
        if (entranceIndex < 0 || entranceIndex >= EntranceDB::sLinkCount)
            return SubExitDoor_None;
        if (!isEntranceRandomizable(entranceIndex))
            return SubExitDoor_None;

        ensureLinkStageIndexes();
        const EntranceDB::EntranceLink& link = EntranceDB::sLinks[entranceIndex];
        if (strEq(link.subEntranceIn, link.subEntranceOut))
            return SubExitDoor_None;

        const s32 subStage = sLinkSubStageIndex[entranceIndex];
        if (!isValidStage(subStage) || !state.accessibleStages[subStage])
            return SubExitDoor_None;

        const LD::EntranceAccess& access = LD::sEntrances[entranceIndex];
        if (!LD::sStages[subStage].hasRoutes) {
            if ((access.subFarNode < 0 || isNodeScenarioActive(state, access.subFarNode))
                && isEntranceNodeUsableInScenario(state, access.subFarNode))
                return SubExitDoor_Far;
            if (state.accessibleSubEntryFar[entranceIndex]
                && (access.subNearNode < 0 || isNodeScenarioActive(state, access.subNearNode))
                && isEntranceNodeUsableInScenario(state, access.subNearNode))
                return SubExitDoor_Near;
            return SubExitDoor_None;
        }

        if (access.subFarNode >= 0 && reachableNodes[access.subFarNode]
            && isEntranceNodeUsableInScenario(state, access.subFarNode))
            return SubExitDoor_Far;
        if (state.accessibleSubEntryFar[entranceIndex] && access.subNearNode >= 0
            && reachableNodes[access.subNearNode]
            && isEntranceNodeUsableInScenario(state, access.subNearNode))
            return SubExitDoor_Near;
        return SubExitDoor_None;
    }

    static bool expandStageAccessFromEntrances(LogicState& state, const bool* reachableNodes) {
        bool changed = false;
        const bool entranceRandoActive = Randomizer::isEntranceRandoEnabled() && EntranceRando::isInitialized();

        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isEntranceRandomizable(i))
                continue;

            // Overworld entry pipe and the distinct overworld exit door follow their
            // own connections. getPipeConn / getHomeDoorConn return the vanilla wiring
            // when the shuffle is off, so this also models the unrandomized layout.
            if (canUseHomeEntranceSide(state, reachableNodes, i, false))
                changed |= addAccessibleEntranceConn(state, i, EntranceRando::getPipeConn(i));
            if (canUseHomeEntranceSide(state, reachableNodes, i, true))
                changed |= addAccessibleEntranceConn(state, i, EntranceRando::getHomeDoorConn(i, true));
        }

        // Fixed doors (vanilla, and ALL doors when the shuffle is off) are plain
        // two-way connections to their own vanilla destination: the link's home and
        // sub sides connect directly, both directions. The Conn-based loop above
        // only covers shuffle-pool doors, so fixed doors are wired here using the
        // link's own node ids. This mirrors randomizer_logic.py expand_stage_access:
        // home->sub (enter) and sub->home (exit, e.g. emerging from the inverted
        // pyramid's one-sided pipe). Directional blocks live on the nodes' route
        // edges, so we wire both sides and let route requirements gate movement.
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isEntranceConnected(i))
                continue;
            if (isEntranceRandomizable(i) && entranceRandoActive)
                continue; // shuffled door: handled by the Conn passes

            const LD::EntranceAccess& access = LD::sEntrances[i];
            // Entering (home -> sub) means using the loading zone to leave the
            // overworld, so it is scenario-gated by the home-side object and limited
            // to the sides this link actually owns. Emerging (sub -> home) is arrival:
            // never scenario-gated and never door-gated. Mirrors the host-side
            // expand_stage_access, where vanilla home->sub flows through the
            // reachable_home_entrance_sides path (which drops shadowed/return-only
            // sides before considering them).
            // Subarea Keys gate entering only. Emerging stays free on purpose:
            // entrance rando can drop the player inside a sub-area whose door
            // they have no key for, and gating the exit would strand them.
            const bool canEnter = stateCanEnterSubareaLink(state, i);
            const bool homeInReachable = canEnter
                && EntranceDB::ownsHomeEntranceSide(i, false)
                && access.homeInNode >= 0 && reachableNodes[access.homeInNode]
                && isEntranceNodeUsableInScenario(state, access.homeInNode);
            const bool homeOutReachable = canEnter
                && EntranceDB::ownsHomeEntranceSide(i, true)
                && access.homeOutNode >= 0 && reachableNodes[access.homeOutNode]
                && isEntranceNodeUsableInScenario(state, access.homeOutNode);
            const bool subNearReachable
                = access.subNearNode >= 0 && reachableNodes[access.subNearNode];
            const bool subFarReachable
                = access.subFarNode >= 0 && reachableNodes[access.subFarNode];

            // A fixed link can still be a linear room with distinct lower/upper
            // doors. Keep each physical doorway paired with its own destination
            // side; treating either home node as access to BOTH sub nodes bypasses
            // every authored traversal edge inside the room (Top-Hat Tower was the
            // visible casualty).
            if (homeInReachable && access.subNearNode >= 0)
                changed |= addAccessibleNodeWithTrace(
                    state, access.subNearNode, StageTrace_Entrance,
                    sLinkHomeStageIndex[i], i, i,
                    i, EntranceRando::ConnKind_SubNear);
            if (homeOutReachable && access.subFarNode >= 0)
                changed |= addAccessibleNodeWithTrace(
                    state, access.subFarNode, StageTrace_Entrance,
                    sLinkHomeStageIndex[i], i, i,
                    i, EntranceRando::ConnKind_SubFar);
            if (subNearReachable && access.homeInNode >= 0)
                changed |= addAccessibleNodeWithTrace(
                    state, access.homeInNode, StageTrace_Entrance,
                    sLinkSubStageIndex[i], i, i,
                    i, EntranceRando::ConnKind_HomeIn);
            if (subFarReachable && access.homeOutNode >= 0)
                changed |= addAccessibleNodeWithTrace(
                    state, access.homeOutNode, StageTrace_Entrance,
                    sLinkSubStageIndex[i], i, i,
                    i, EntranceRando::ConnKind_HomeOut);
        }

        if (!entranceRandoActive)
            return changed;

        const s32 deepWoodsPrimary = EntranceDB::findDeepWoodsPrimaryLink();
        if (canUseDeepWoodsVineExit(state, reachableNodes))
            changed |= addAccessibleEntranceConn(
                state,
                deepWoodsPrimary,
                EntranceRando::getSubDoorConn(deepWoodsPrimary, true),
                true);

        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isEntranceRandomizable(i))
                continue;

            // Deep Woods uses its dedicated directional interface above: all
            // vines share the primary far-side connection, while arrivals use
            // the primary ceiling-drop spawn.  Do not also treat its two
            // legacy rows as ordinary bidirectional sub-area doors.
            if (EntranceDB::isDeepWoodsArea(i))
                continue;

            // Continue a chain out the sub-area's far or near door, following that
            // door's own connection (not the entry pipe's).
            const s32 exitDoor = calcSubareaForwardExitDoor(state, reachableNodes, i);
            if (exitDoor == SubExitDoor_None)
                continue;

            changed |= addAccessibleEntranceConn(
                state, i,
                EntranceRando::getSubDoorConn(i, exitDoor == SubExitDoor_Far),
                true);

            // Randomized Starting Position: the player did not walk in through
            // either of this room's doors, so BOTH are exits. An ordinary chain
            // only ever continues out the forward one - correct there, because
            // backing out the way you came leads somewhere already reachable and
            // gains nothing. For a start position it is the whole point: backing
            // out is how he returns to the kingdom he started in.
            //
            // Without this, calcSubareaForwardExitDoor's far-door preference meant
            // a start at a near-side door only ever explored the far chain, and
            // the walk-back proof rejected literally every sub-area candidate
            // (measured 0/8 on Cap) - Max silently degraded to Overworld.
            if (state.startExitStage < 0 || sLinkSubStageIndex[i] != state.startExitStage)
                continue;

            const LD::EntranceAccess& startAccess = LD::sEntrances[i];
            const bool otherIsFar = exitDoor != SubExitDoor_Far;
            const s16 otherNode = otherIsFar ? startAccess.subFarNode : startAccess.subNearNode;
            if (otherNode >= 0 && reachableNodes[otherNode]
                && isEntranceNodeUsableInScenario(state, otherNode))
                changed |= addAccessibleEntranceConn(
                    state, i, EntranceRando::getSubDoorConn(i, otherIsFar), true);
        }

        return changed;
    }

    static bool canUsePaintingNode(const LogicState& state, const bool* reachableNodes, s32 node) {
        return node >= 0 && node < LD::sNodeCount && reachableNodes[node] && isNodeScenarioActive(state, node);
    }

    static bool isPaintingUnlockedForLogic(const LogicState& state, s32 paintingIndex) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return true;
        if (paintingIndex < 0 || paintingIndex >= LD::sPaintingLinkCount)
            return false;
        return sFreePaintingLinks[paintingIndex] || state.unlockedPaintings[paintingIndex];
    }

    static bool expandStageAccessFromPaintings(LogicState& state, const bool* reachableNodes) {
        bool changed = false;

        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            if (!isPaintingUnlockedForLogic(state, i))
                continue;

            const LD::PaintingLink& link = LD::sPaintingLinks[i];
            const bool paintingRandoActive = Randomizer::isPaintingRandoEnabled() && PaintingRando::isInitialized();
            const s32 destinationNode = paintingRandoActive
                ? PaintingRando::getMappedDestinationNode(i)
                : link.destinationNode;
            const s32 returnSourceNode = paintingRandoActive
                ? PaintingRando::getMappedReturnSourceNode(i)
                : link.sourceNode;

            if (canUsePaintingNode(state, reachableNodes, link.sourceNode)) {
                const s32 sourceStage = link.sourceNode >= 0 ? LD::sNodes[link.sourceNode].stage : -1;
                changed |= addAccessibleNodeWithTrace(
                    state,
                    destinationNode,
                    StageTrace_Painting,
                    sourceStage,
                    link.sourceNode,
                    destinationNode);
            }
            if (link.bidirectional && canUsePaintingNode(state, reachableNodes, link.destinationNode)) {
                const s32 sourceStage = link.destinationNode >= 0 ? LD::sNodes[link.destinationNode].stage : -1;
                changed |= addAccessibleNodeWithTrace(
                    state,
                    returnSourceNode,
                    StageTrace_Painting,
                    sourceStage,
                    link.destinationNode,
                    returnSourceNode);
            }
        }

        return changed;
    }

    static bool expandStageAccessFromReachableNodes(LogicState& state, const bool* reachableNodes) {
        bool changed = false;
        for (s32 node = 0; node < LD::sNodeCount; node++) {
            if (!reachableNodes[node])
                continue;
            changed |= addAccessibleStage(state, LD::sNodes[node].stage);
        }
        return changed;
    }

    static void expandReachabilityFixedPoint(LogicState& state, bool* reachableNodes, NodeTrace* nodeTraces = nullptr) {
        for (;;) {
            computeReachableNodes(state, reachableNodes);
            const bool changedByEntrances = expandStageAccessFromEntrances(state, reachableNodes);
            const bool changedByPaintings = expandStageAccessFromPaintings(state, reachableNodes);
            const bool changedByRoutes = expandStageAccessFromReachableNodes(state, reachableNodes);
            if (!changedByEntrances && !changedByPaintings && !changedByRoutes)
                break;
        }
        computeReachableNodes(state, reachableNodes, nodeTraces);
    }

    s32 findUnreachableEntranceLinks(s16* outLinks, s32 maxOut) {
        if (!Randomizer::isEntranceRandoEnabled() || !EntranceRando::isInitialized())
            return 0;

        LogicState proofState;
        initEntranceProofState(proofState);

        bool reachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(proofState, reachableNodes);

        s32 count = 0;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isEntranceRandomizable(i))
                continue;
            if (proofState.accessibleSubEntryNear[i] || proofState.accessibleSubEntryFar[i])
                continue;
            if (count < maxOut)
                outLinks[count] = (s16)i;
            count++;
        }
        return count;
    }

    static bool allEntranceDestinationsReachable(LogicState& state) {
        if (!Randomizer::isEntranceRandoEnabled() || !EntranceRando::isInitialized())
            return true;

        s16 missing[EntranceDB::sLinkCount];
        s32 missingCount = findUnreachableEntranceLinks(missing, EntranceDB::sLinkCount);
        if (missingCount <= 0)
            return true;

        // Repair before rejecting. Every leftover pairing at finalize joined two
        // doors that were already reachable, so breaking one of those and giving
        // half of it to an orphan costs nothing: the orphan becomes reachable,
        // and the door it displaced is picked up by the other half of the swap.
        const s32 repaired = EntranceRando::repairUnreachableSubAreas(
            missing, missingCount > EntranceDB::sLinkCount ? EntranceDB::sLinkCount : missingCount);
        if (repaired > 0) {
            Randomizer::logPush("ER repair: re-paired %d unreachable sub-area(s)", repaired);
            missingCount = findUnreachableEntranceLinks(missing, EntranceDB::sLinkCount);
            if (missingCount <= 0)
                return true;
        }

        for (s32 i = 0; i < missingCount && i < 8; i++) {
            const s32 link = missing[i];
            Randomizer::logPush(
                "Missing entrance: homeStage=%s, homeEntranceIn=%s, homeEntranceOut=%s",
                EntranceDB::sLinks[link].homeStage, EntranceDB::sLinks[link].homeEntranceIn,
                EntranceDB::sLinks[link].homeEntranceOut);
        }
        addWarning(
            "Could not prove every randomized entrance destination reachable from an overworld "
            "entrance (%d missing, first=%d).",
            missingCount, (s32)missing[0]);
        return false;
    }

    static bool hasReachableNodeInStage(const bool* reachableNodes, s32 stage) {
        ensureNodeGraphIndexes();
        if (!isValidStage(stage))
            return false;
        for (s32 node = sFirstNodeByStage[stage]; node >= 0; node = sNextNodeInStage[node])
            if (reachableNodes[node])
                return true;
        return false;
    }

    static bool hasStageAccessForLocation(const LogicState& state, const bool* reachableNodes, s32 location) {
        const LD::MoonAccess& moon = LD::sMoons[location];
        if (isValidStage(moon.stageIndex) && state.accessibleStages[moon.stageIndex])
            return true;
        if (hasReachableNodeInStage(reachableNodes, moon.stageIndex))
            return true;
        return false;
    }

    static bool isLocationReachable(const LogicState& state, const bool* reachableNodes, s32 location) {
        if (!isValidMoon(location))
            return false;

        const LD::MoonAccess& moon = LD::sMoons[location];
        if (!hasStageAccessForLocation(state, reachableNodes, location))
            return false;
        if (moon.isPrincessPeach && !hasToken(state, LD::sMushroomReachedToken))
            return false;
        if (moon.storyOrder >= 0 && isValidKingdom(moon.kingdom) && state.storyCounts[moon.kingdom] < moon.storyOrder)
            return false;
        // These explicitly promoted shuffled rooms always load their
        // multi-prize scenario and their NPC/event flow treats Moon Rock as
        // open. Do not leave their physical locations gated behind the source
        // kingdom's scenario in seed generation.
        const bool isPromotedMultiPrizeSubarea = ScenarioPatcher::getRequiredScenarioForRandomizedMultiPrizeSubarea(moon.stage) > 0;
        if (!isPromotedMultiPrizeSubarea && moon.scenarioMask != 0
            && isValidKingdom(moon.kingdom)
            && !scenarioMaskContains(moon.scenarioMask, state.kingdomScenarios[moon.kingdom]))
            return false;
        if (!isPromotedMultiPrizeSubarea && moon.scenarioFloor >= 0
            && isValidKingdom(moon.kingdom)
            && state.kingdomScenarios[moon.kingdom] < moon.scenarioFloor)
            return false;
        const bool stageHasRoutes = isValidStage(moon.stageIndex) && LD::sStages[moon.stageIndex].hasRoutes;
        if (stageHasRoutes
            && (moon.fromNode < 0 || moon.fromNode >= LD::sNodeCount || !reachableNodes[moon.fromNode]))
            return false;
        const s16 currentScenario = isValidKingdom(moon.kingdom)
            ? state.kingdomScenarios[moon.kingdom]
            : -1;
        if (!anyRequirementOptionScenarioAllowed(
                moon.localRequirement,
                moon.localOptionScenarioOffset,
                moon.localOptionScenarioCount,
                currentScenario))
            return false;
        if (sNoLogicFill)
            return true;
        if (!moonRequirementSatisfiedForScenario(
                state, moon.stateRequirement, 0, 0, currentScenario,
                moon, true, reachableNodes))
            return false;
        if (!moonRequirementSatisfiedForScenario(
                state,
                moon.localRequirement,
                moon.localOptionScenarioOffset,
                moon.localOptionScenarioCount,
                currentScenario,
                moon,
                false,
                reachableNodes))
            return false;
        return true;
    }

    // ── Randomized Starting Position ────────────────────────────────────────
    //
    // Puts the player at `startNode` in `state`: the node itself, plus - when it
    // sits inside a sub-area - that sub-area's doors as available ways out.
    // accessibleSubEntryFar is the flag calcSubareaForwardExitDoor consults
    // before letting a chain leave through a sub's NEAR door; it normally means
    // "you came in the far side, so the near door is a real exit". A player who
    // simply starts in the room has both of its doors physically available to
    // him, so set it however he got there. Route requirements still decide
    // whether he can actually walk to either door.
    static void seedStartNode(LogicState& state, s32 startNode) {
        if (startNode < 0 || startNode >= LD::sNodeCount)
            return;

        ensureLinkStageIndexes();
        addAccessibleNodeWithTrace(state, startNode, StageTrace_Odyssey, -1, -1, -1);

        const s32 stage = LD::sNodes[startNode].stage;
        if (!isValidStage(stage))
            return;

        state.startExitStage = (s16)stage;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (sLinkSubStageIndex[i] == stage)
                state.accessibleSubEntryFar[i] = true;
        }
    }

    // Stages a player standing in `kingdom`'s overworld can reach with nothing but
    // his starting equipment - i.e. the sub-area chains that actually hang off
    // that kingdom's own doors under this seed's shuffle. This is the FORWARD
    // direction, and it is what bounds the Max start pool: "linked from that
    // overworld", not merely "somewhere a path back to it exists". The two are
    // very different once entrances are shuffled - a sub-area physically located
    // in the starting kingdom can easily belong to a chain rooted in another one.
    void computeStagesLinkedFromKingdom(s32 kingdom, bool* outStages) {
        if (!outStages)
            return;
        std::memset(outStages, 0, sizeof(bool) * LD::sStageCount);
        if (!isValidKingdom(kingdom))
            return;

        const s32 mainStage = LD::sMainStageByKingdom[kingdom];
        if (!isValidStage(mainStage))
            return;

        static LogicState opening;
        initState(opening);
        // Only this kingdom's overworld: initState may unlock others (Cascade, or
        // every kingdom under the Unlocked map mode), and their chains are not
        // "linked from" the starting overworld.
        std::memset(opening.accessibleStages, 0, sizeof(opening.accessibleStages));
        std::memset(opening.accessibleKingdoms, 0, sizeof(opening.accessibleKingdoms));
        std::memset(opening.accessibleNodes, 0, sizeof(opening.accessibleNodes));
        std::memset(opening.accessibleSubEntryNear, 0, sizeof(opening.accessibleSubEntryNear));
        std::memset(opening.accessibleSubEntryFar, 0, sizeof(opening.accessibleSubEntryFar));
        unlockKingdom(opening, kingdom);

        bool reachable[LD::sNodeCount];
        expandReachabilityFixedPoint(opening, reachable);
        std::memcpy(outStages, opening.accessibleStages, sizeof(bool) * LD::sStageCount);
    }

    // True when a player dropped at `startNode` with nothing but his starting
    // equipment can prove his way back to `requiredStage` (the starting
    // kingdom's overworld). Running the ordinary fixed point is what makes this
    // honor the entrance shuffle, one-way pockets, dead-end subs and scenario
    // gates without any of that having to be re-derived here.
    bool canStartAtNode(s32 node, s32 requiredStage) {
        if (node < 0 || node >= LD::sNodeCount || !isValidStage(requiredStage))
            return false;
        if (LD::sNodes[node].excluded)
            return false;

        // This proof runs before the fill has wired a single door, so without
        // the vanilla stand-in it would see a world with no loading zones and
        // reject nearly every candidate. The start is re-checked against the
        // real wiring after finalizeIncrementalBuild (see revalidateStartNode).
        const bool assumeVanilla = EntranceRando::isIncrementalBuildActive();
        if (assumeVanilla)
            EntranceRando::setAssumeVanillaForUnwiredDoors(true);
        struct AssumeGuard {
            bool active;
            ~AssumeGuard() {
                if (active)
                    EntranceRando::setAssumeVanillaForUnwiredDoors(false);
            }
        } assumeGuard { assumeVanilla };

        static LogicState opening;
        initState(opening);
        // MUST wipe physical position first. initState unlocks the STARTING
        // KINGDOM, whose main stage is exactly the stage this function asks
        // about - so without this the answer is "yes" before the start node is
        // even seeded, every candidate passes, and the walk-back guarantee is
        // silently worthless. (Symptom: every log reading "probe 1/48", and a
        // start handed out at the far end of WaterTubeExStage, whose
        // sub_far -> sub_near edge is authored `impossible`.)
        // Tokens, scenarios and collected state stay: those are what he owns,
        // not where he is standing.
        std::memset(opening.accessibleStages, 0, sizeof(opening.accessibleStages));
        std::memset(opening.accessibleKingdoms, 0, sizeof(opening.accessibleKingdoms));
        std::memset(opening.accessibleNodes, 0, sizeof(opening.accessibleNodes));
        std::memset(opening.accessibleSubEntryNear, 0, sizeof(opening.accessibleSubEntryNear));
        std::memset(opening.accessibleSubEntryFar, 0, sizeof(opening.accessibleSubEntryFar));
        seedStartNode(opening, node);

        bool reachable[LD::sNodeCount];
        expandReachabilityFixedPoint(opening, reachable);
        return opening.accessibleStages[requiredStage];
    }

    // The start node is a permanent part of the run's reachable set: the map's
    // Return to Start warp can always put the player back on it, exactly like a
    // checkpoint flag. (This replaced an earlier "temporary opening access"
    // model - without a way back, standing somewhere at t=0 would not have let
    // the player return with items collected elsewhere.)
    static void applyRandomizedStartPosition(LogicState& state) {
        seedStartNode(state, StartPositionRando::getStartNode());
    }

    // When out buffers are provided they receive the reachable-node and rooted-stage
    // maps of the FINAL (post-event) state, letting callers skip recomputing the
    // same fixed point immediately afterwards.
    static s32 refreshReachableStateEvents(LogicState& state, s16* unlockedTokens, s32 maxUnlockedTokens,
        bool* outReachableNodes = nullptr, bool* outRootedStages = nullptr);
    static s32 collectLocation(LogicState& state, s32 location, s16* unlockedTokens, s32 maxUnlockedTokens);
    static s32 collectItem(LogicState& state, s32 item, s16* unlockedTokens, s32 maxUnlockedTokens);

    // outReachableNodes, when given, receives the node map this call already
    // had to build. The entrance fill needs exactly that map and used to
    // recompute it from scratch on every wiring attempt - a full fixed point
    // over ~1600 nodes, hundreds of times per kingdom.
    static s32 reachableLocations(
        LogicState& state,
        const bool* unfilled,
        s32* outLocations,
        bool* outReachableNodes = nullptr) {
        // The event refresh already ends on a stable fixed point; reuse its final
        // node/stage maps instead of recomputing the same traversals again.
        bool reachableNodes[LD::sNodeCount];
        bool rootedStages[LD::sStageCount];
        refreshReachableStateEvents(state, nullptr, 0, reachableNodes, rootedStages);
        if (outReachableNodes)
            std::memcpy(outReachableNodes, reachableNodes, sizeof(reachableNodes));

        s32 count = 0;
        for (s32 location = 0; location < LD::sMoonCount; location++) {
            if (!unfilled[location])
                continue;
            const s32 stage = LD::sMoons[location].stageIndex;
            if (!isValidStage(stage) || !rootedStages[stage])
                continue;
            if (isLocationReachable(state, reachableNodes, location))
                outLocations[count++] = location;
        }
        return count;
    }

    static bool grantToken(LogicState& state, s32 token, s16* unlockedTokens, s32& unlockedCount, s32 maxUnlockedTokens) {
        if (token < 0 || token >= LD::sTokenCount || state.tokens[token])
            return false;
        state.tokens[token] = true;
        if (unlockedTokens && unlockedCount < maxUnlockedTokens)
            unlockedTokens[unlockedCount++] = (s16)token;
        return true;
    }

    static bool isNextStoryMoon(const LogicState& state, s32 item) {
        if (!isStoryMoon(item))
            return false;
        const LD::MoonAccess& moon = LD::sMoons[item];
        if (!isValidKingdom(moon.kingdom))
            return false;
        return moon.storyOrder < 0 || state.storyCounts[moon.kingdom] == moon.storyOrder;
    }

    static bool tokenIsMoonRockForKingdom(s32 token, s32 kingdom) {
        return isValidKingdom(kingdom) && token == LD::sMoonRockTokenByKingdom[kingdom];
    }

    static s32 refreshReachableStateEvents(LogicState& state, s16* unlockedTokens, s32 maxUnlockedTokens,
        bool* outReachableNodes, bool* outRootedStages) {
        s32 unlockedCount = 0;
        bool localReachableNodes[LD::sNodeCount];
        bool localRootedStages[LD::sStageCount];
        bool* reachableNodes = outReachableNodes ? outReachableNodes : localReachableNodes;
        bool* rootedStages = outRootedStages ? outRootedStages : localRootedStages;
        bool changed = true;
        while (changed) {
            changed = false;

            expandReachabilityFixedPoint(state, reachableNodes);
            computeRootedStageMap(state, rootedStages);

            for (s32 node = 0; node < LD::sNodeCount; node++) {
                if (!reachableNodes[node])
                    continue;
                const s32 stage = LD::sNodes[node].stage;
                if (!isValidStage(stage) || !rootedStages[stage])
                    continue;

                const s32 token = LD::sNodes[node].eventToken;
                if (token < 0 || token >= LD::sTokenCount || state.tokens[token])
                    continue;

                for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                    if (!tokenIsMoonRockForKingdom(token, kingdom))
                        continue;
                    if (LD::sStoryTotalsByKingdom[kingdom] > 0 && !hasToken(state, LD::sWorldPeaceTokenByKingdom[kingdom]))
                        continue;
                    if (Randomizer::isMoonRockKeyEnabled()
                        && kingdomHasRealMoonRock(kingdom)
                        && !state.moonRockKeysCollected[kingdom])
                        continue;
                    if (grantToken(state, token, unlockedTokens, unlockedCount, maxUnlockedTokens)) {
                        advanceKingdomScenario(state, kingdom, LD::sMoonRockScenarioByKingdom[kingdom]);
                        changed = true;
                    }
                }
            }
        }
        return unlockedCount;
    }

    // Story/scenario progression is physical: it follows the LOCATION the player
    // collects in a kingdom (defeating Spewart advances Wooded no matter which item
    // was credited there). The randomized item only contributes its kingdom's moon
    // count and any capture/ability/painting unlocks.
    static s32 collectLocation(LogicState& state, s32 location, s16* unlockedTokens, s32 maxUnlockedTokens) {
        if (!isValidMoon(location))
            return 0;

        if (state.collectedLocations[location])
            return 0;

        state.collectedLocations[location] = true;

        s32 unlockedCount = 0;
        const LD::MoonAccess& loc = LD::sMoons[location];

        if (isValidKingdom(loc.kingdom) && loc.scenarioMoveTo >= 0 && isVanillaMultiMoon(location))
            advanceKingdomScenario(state, loc.kingdom, loc.scenarioMoveTo);

        if (isSnowGrandPrixScenarioAdvance(location)
            && state.kingdomScenarios[WorldMeta::WorldId_Snow] == 1) {
            advanceKingdomScenario(state, WorldMeta::WorldId_Snow, 3);
        }

        if (isStoryMoon(location) && isValidKingdom(loc.kingdom) && isNextStoryMoon(state, location)) {
            if (loc.storyOrder >= 0)
                state.storyCounts[loc.kingdom] = loc.storyOrder + 1;
            else
                state.storyCounts[loc.kingdom]++;

            if (loc.storyMoveTo >= 0 && isVanillaMultiMoon(location))
                advanceKingdomScenario(state, loc.kingdom, loc.storyMoveTo);
            else if (loc.storyMoveTo >= 0 && isScriptedScenarioAdvance(loc.kingdom, loc.storyMoveTo))
                // Scripted scenario exception (e.g. Metro festival): a single story
                // beat advances ScenarioNo without a triple moon.
                advanceKingdomScenario(state, loc.kingdom, loc.storyMoveTo);

            if (LD::sStoryTotalsByKingdom[loc.kingdom] > 0 && state.storyCounts[loc.kingdom] >= LD::sStoryTotalsByKingdom[loc.kingdom]) {
                grantToken(state, LD::sWorldPeaceTokenByKingdom[loc.kingdom], unlockedTokens, unlockedCount, maxUnlockedTokens);
                advanceKingdomScenario(state, loc.kingdom, LD::sPostGameScenarioByKingdom[loc.kingdom]);
            }
        }

        const s32 item = sPlacementItemForLocation[location];
        if (item >= 0) {
            unlockedCount += collectItem(
                state, item,
                unlockedTokens ? unlockedTokens + unlockedCount : nullptr,
                maxUnlockedTokens - unlockedCount);
        }

        return unlockedCount;
    }

    static bool isCloudPaintingLinkAssigned(const LogicState& state, s32 paintingIndex) {
        for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++)
            if (state.cloudPaintingLinkBySlot[slot] == paintingIndex)
                return true;
        return false;
    }

    static u32 cloudPaintingTieBreak(s32 cloudSlot, s32 paintingIndex) {
        u32 value = sCloudPaintingAssignmentTieSeed
            ^ (static_cast<u32>(cloudSlot + 1) * 0x9E3779B9u)
            ^ (static_cast<u32>(paintingIndex + 1) * 0x85EBCA6Bu);
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        return value;
    }

    // Prefer a currently usable locked painting whose arrival node is not yet
    // reachable.  This turns each Cloud item into an immediate frontier expansion
    // when possible, while the seed-derived tie-break keeps equivalent choices
    // stable without coupling them to the solver's mutable RNG stream.
    static s32 selectUsefulPaintingLink(const LogicState& state, s32 cloudSlot) {
        bool reachableNodes[LD::sNodeCount];
        // Callers refresh their state before placing an item, so one node pass is
        // enough to rank links here.  Avoid a second fixed-point expansion for
        // every speculative bundle in the progression search.
        computeReachableNodes(state, reachableNodes);

        s32 bestLink = -1;
        s32 bestScore = -1;
        u32 bestTie = 0;
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            if (sFreePaintingLinks[i] || isCloudPaintingLinkAssigned(state, i))
                continue;

            const LD::PaintingLink& link = LD::sPaintingLinks[i];
            const bool paintingRandoActive = Randomizer::isPaintingRandoEnabled()
                && PaintingRando::isInitialized();
            const s32 destinationNode = paintingRandoActive
                ? PaintingRando::getMappedDestinationNode(i)
                : link.destinationNode;
            const s32 returnSourceNode = paintingRandoActive
                ? PaintingRando::getMappedReturnSourceNode(i)
                : link.sourceNode;

            const bool forwardUsable = canUsePaintingNode(
                state, reachableNodes, link.sourceNode);
            const bool forwardExpands = forwardUsable
                && destinationNode >= 0 && destinationNode < LD::sNodeCount
                && !reachableNodes[destinationNode];
            const bool returnUsable = link.bidirectional
                && canUsePaintingNode(state, reachableNodes, link.destinationNode);
            const bool returnExpands = returnUsable
                && returnSourceNode >= 0 && returnSourceNode < LD::sNodeCount
                && !reachableNodes[returnSourceNode];

            s32 score = forwardExpands || returnExpands ? 4
                                                        : (forwardUsable || returnUsable ? 2 : 0);
            const s32 destinationStage = destinationNode >= 0
                    && destinationNode < LD::sNodeCount
                ? LD::sNodes[destinationNode].stage
                : -1;
            const s32 returnSourceStage = returnSourceNode >= 0
                    && returnSourceNode < LD::sNodeCount
                ? LD::sNodes[returnSourceNode].stage
                : -1;
            if (forwardExpands && isValidStage(destinationStage)
                && !state.accessibleStages[destinationStage])
                score += 4;
            if (returnExpands && isValidStage(returnSourceStage)
                && !state.accessibleStages[returnSourceStage])
                score += 4;

            const u32 tie = cloudPaintingTieBreak(cloudSlot, i);
            if (score > bestScore || (score == bestScore && tie > bestTie)) {
                bestLink = i;
                bestScore = score;
                bestTie = tie;
            }
        }
        return bestLink;
    }

    static void ensureCloudPaintingAssignment(LogicState& state, s32 item) {
        if (!moonUnlocksCloudPainting(item))
            return;

        const s32 unlockCount = sMoonCloudPaintingSlotCounts[item];
        for (s32 unlock = 0; unlock < unlockCount; unlock++) {
            const s32 cloudSlot = sMoonCloudPaintingSlots[item][unlock];
            if (cloudSlot < 0 || cloudSlot >= kCloudPaintingSlotCount)
                continue;

            s32 paintingIndex = state.cloudPaintingLinkBySlot[cloudSlot];
            if (paintingIndex < 0 || paintingIndex >= LD::sPaintingLinkCount) {
                paintingIndex = selectUsefulPaintingLink(state, cloudSlot);
                state.cloudPaintingLinkBySlot[cloudSlot]
                    = static_cast<s16>(paintingIndex);
            }
            if (paintingIndex >= 0 && paintingIndex < LD::sPaintingLinkCount)
                state.unlockedPaintings[paintingIndex] = true;
        }
    }

    static s32 collectItem(LogicState& state, s32 item, s16* unlockedTokens, s32 maxUnlockedTokens) {
        if (!isValidMoon(item))
            return 0;

        if (state.collectedItems[item])
            return 0;

        state.collectedItems[item] = true;

        ensureCloudPaintingAssignment(state, item);
        for (s32 unlock = 0; unlock < sMoonCloudPaintingSlotCounts[item]; unlock++) {
            const s32 cloudSlot = sMoonCloudPaintingSlots[item][unlock];
            const s32 paintingIndex
                = cloudSlot >= 0 && cloudSlot < kCloudPaintingSlotCount
                ? state.cloudPaintingLinkBySlot[cloudSlot]
                : sMoonPaintingUnlocks[item][unlock];
            if (paintingIndex >= 0 && paintingIndex < LD::sPaintingLinkCount)
                state.unlockedPaintings[paintingIndex] = true;
        }

        const s32 creditKingdom = assignedKingdomOf(item);
        if (isValidKingdom(creditKingdom))
            state.moonCounts[creditKingdom] += (s16)moonValue(item);

        const s32 keyKingdom = moonRockKeyKingdomForItem(item);
        if (isValidKingdom(keyKingdom))
            state.moonRockKeysCollected[keyKingdom] = true;

        const s32 subareaSlot = subareaKeySlotForItem(item);
        if (subareaSlot >= 0 && subareaSlot < kMaxSubareaKeySlots)
            state.subareaKeysCollected[subareaSlot] = true;

        s32 unlockedCount = 0;
        const s32 itemUnlockCount = sMoonUnlockTokenCounts[item];
        for (s32 i = 0; i < itemUnlockCount; i++) {
            const s32 token = sMoonUnlockTokens[item][i];
            grantToken(state, token, unlockedTokens, unlockedCount, maxUnlockedTokens);
        }

        return unlockedCount;
    }

    static s32 requirementForKingdom(s32 kingdom) {
        if (!isValidKingdom(kingdom))
            return 0;
        // Moon Kingdom is scaled by RequirementRando like every other kingdom whenever
        // its requirement toggle is on. Before RequirementRando has run (or after
        // it's been cleared), fall back to the generated per-kingdom table.
        if (RequirementRando::isInitialized())
            return RequirementRando::getRequirement(kingdom);
        return LD::sMoonRequirementsByKingdom[kingdom];
    }

    // Route progression no longer requires collecting Bowser's grand moon.
    // Keep this centralized for any future authored departure blockers.
    // NOTE: do NOT gate this on isWorldPeaceRequirementEnabled() — that forces a
    // full per-kingdom story proof inside every buffer-loop iteration and makes
    // generation take unacceptably long. World peace needs no generation-time
    // proof at all: validateFinalPlacement already guarantees every kingdom,
    // capture, and ability is reachable, and story chains then complete in
    // story order in-game.
    static bool requiresRouteStoryCompletion(s32 kingdom) {
        (void)kingdom;
        return false;
    }

    static bool isKingdomRequirementMet(const LogicState& state, s32 kingdom) {
        if (!isValidKingdom(kingdom)
            || !state.accessibleKingdoms[kingdom]
            || state.moonCounts[kingdom] < requirementForKingdom(kingdom))
            return false;
        return !requiresRouteStoryCompletion(kingdom)
            || state.storyCounts[kingdom] >= LD::sStoryTotalsByKingdom[kingdom];
    }

    static bool advanceCompletedKingdomToPeace(LogicState& state, s32 kingdom) {
        if (Randomizer::getWorldPeaceRequirementMode()
                != Randomizer::WorldPeaceRequirementMode_OnKingdomComplete
            || !isKingdomRequirementMet(state, kingdom))
            return false;

        const s32 postGameScenario = LD::sPostGameScenarioByKingdom[kingdom];
        if (postGameScenario <= 0
            || !advanceKingdomScenario(state, kingdom, postGameScenario))
            return false;

        // On Kingdom Complete promotes the kingdom just as a completed story
        // chain would. Mirror the initial post-peace state so World Peace and
        // Moon Rock event requirements become usable immediately in logic.
        const s32 peaceToken = LD::sWorldPeaceTokenByKingdom[kingdom];
        if (peaceToken >= 0 && peaceToken < LD::sTokenCount)
            state.tokens[peaceToken] = true;
        state.storyCounts[kingdom] = LD::sStoryTotalsByKingdom[kingdom];
        return true;
    }

    static bool isRouteTierComplete(const LogicState& state, s32 tierIndex) {
        const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
        if (!tier)
            return false;

        for (s32 i = 0; i < tier->count; i++) {
            if (!isKingdomRequirementMet(state, tier->worlds[i]))
                return false;
        }
        return true;
    }

    // Is every tier from 0 through tierIndex done?
    //
    // The route is a chain, so this - not isRouteTierComplete on the single
    // preceding tier - is what may open the tier after it. Checking one link
    // lets a shuffled entrance bridge into the middle of the route: a kingdom
    // reached early through a loading zone banks its moons normally (which is
    // legitimate and must keep working, since isKingdomRequirementMet credits
    // by assigned kingdom exactly as GameDataFile::getShineNum does), and that
    // full counter would then open the NEXT tier while an earlier one - the
    // starting kingdom included - is still short.
    //
    // The game cannot reproduce that. KingdomProgress advances strictly by
    // route index: getVisibleCount is frontier + 1, and tryCompleteFrontier
    // only completes entries[frontier], and only while the player is standing
    // in it. A route that needs tier N open while tier 0 is unmet is unplayable.
    static bool routePrefixComplete(const LogicState& state, s32 tierIndex) {
        for (s32 i = 0; i <= tierIndex; i++) {
            if (!isRouteTierComplete(state, i))
                return false;
        }
        return true;
    }

    // Promote every kingdom that has met its moon requirement to post-peace.
    //
    // This is NOT a route advance, and the distinction matters: leaving a kingdom
    // you have finished is something the player does whenever they like, in any
    // order, and it does not depend on the Odyssey having unlocked anything. It
    // used to live inside advanceKingdomOrder, which meant validateBranchOrder's
    // allowRouteAdvance=false suppressed it along with the route unlock it was
    // actually trying to suppress.
    //
    // The consequence was a construction/validation split that looked like a
    // placement bug and was not: construction promotes a kingdom at its tier
    // boundary, so it saw (say) Seaside post-peace, while the replay kept Seaside
    // at scenario 1 forever. Locations whose scenario mask starts at 2 then read
    // as unreachable to the replay alone, the items on them went uncollected, the
    // door chains needing those items stayed shut, and whole kingdoms downstream
    // reported "no stage access" - a cascade from one suppressed promotion.
    static bool advanceCompletedKingdomsToPeace(LogicState& state) {
        if (Randomizer::getWorldPeaceRequirementMode()
            != Randomizer::WorldPeaceRequirementMode_OnKingdomComplete)
            return false;

        bool anyChanged = false;
        bool changed = true;
        while (changed) {
            changed = false;
            // Repeat until stable: a promotion can open locations that complete
            // another kingdom, which is then promotable in the same pass.
            for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
                const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
                if (!tier)
                    continue;
                for (s32 i = 0; i < tier->count; i++) {
                    const bool promoted = advanceCompletedKingdomToPeace(
                        state, tier->worlds[i]);
                    changed |= promoted;
                    anyChanged |= promoted;
                }
            }
        }
        return anyChanged;
    }

    static bool advanceKingdomOrder(LogicState& state) {
        bool anyChanged = false;
        bool changed = true;
        while (changed) {
            changed = false;
            // Repeated inside the loop so a kingdom made accessible by an unlock
            // below, which already meets its requirement, is promoted in this pass.
            const bool promoted = advanceCompletedKingdomsToPeace(state);
            changed |= promoted;
            anyChanged |= promoted;

            for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
                const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
                if (!tier || tier->count <= 0)
                    continue;

                if (tierIndex == 0) {
                    if (tier->count == 1 && !state.accessibleKingdoms[tier->worlds[0]]) {
                        const bool unlocked = unlockKingdom(state, tier->worlds[0]);
                        changed |= unlocked;
                        anyChanged |= unlocked;
                    }
                    continue;
                }

                if (!routePrefixComplete(state, tierIndex - 1))
                    continue;

                if (tier->count == 1) {
                    const bool unlocked = unlockKingdom(state, tier->worlds[0]);
                    changed |= unlocked;
                    anyChanged |= unlocked;
                    continue;
                }

                if (tier->count != 2)
                    continue;

                const s32 first = tier->worlds[0];
                const s32 second = tier->worlds[1];
                const bool firstAccessible = isValidKingdom(first) && state.accessibleKingdoms[first];
                const bool secondAccessible = isValidKingdom(second) && state.accessibleKingdoms[second];

                if (firstAccessible && isKingdomRequirementMet(state, first) && !secondAccessible) {
                    const bool unlocked = unlockKingdom(state, second);
                    changed |= unlocked;
                    anyChanged |= unlocked;
                }
                if (secondAccessible && isKingdomRequirementMet(state, second) && !firstAccessible) {
                    const bool unlocked = unlockKingdom(state, first);
                    changed |= unlocked;
                    anyChanged |= unlocked;
                }
            }
        }
        return anyChanged;
    }

    // --- Tracker "Hints" mode: live per-kingdom reachability -------------------
    //
    // Everything above this point drives the one-time generation-time fill. The
    // functions below reuse that same reachability/collection machinery, but seed
    // it from what the current save has actually collected instead of a simulated
    // fill order, so the tracker can answer "is there anything worth doing in this
    // kingdom right now" live during play.

    // A location's own native uid/stage is NOT what the save marks "got" once
    // moons are shuffled: the remap swaps in the ASSIGNED ITEM's HintInfo before
    // the game ever calls setGotShine (this is exactly why CaptureRando's own
    // unlock check keys off the item's uid/stage, never a "location"). So collecting
    // this physical spot only ever flips isGet for its assigned item's native
    // identity - checking the location's own uid/stage here would just watch a
    // HintInfo entry that normal collection never touches.
    static bool isLocationPhysicallyCollected(GameDataFile* file, s32 location) {
        if (!isValidMoon(location))
            return false;
        const s32 item = sPlacementItemForLocation[location];
        if (!isValidMoon(item))
            return false;
        const LD::MoonAccess& moon = LD::sMoons[item];
        return file->customIsGotShine(moon.uid, moon.stage);
    }

    // Tracker hints are discovery-based: a logically usable entrance does not
    // reveal its destination until Mario has actually arrived there. The current
    // kingdom is included so the check list is correct during the first frame of
    // an arrival, before the visit record is persisted.
    static bool hasTrackerKingdomVisit(const GameDataFile* file, s32 kingdom) {
        if (!isValidKingdom(kingdom))
            return false;
        if (file && file->getCurrentWorldId() == kingdom)
            return true;
        return (Randomizer::getVisitedKingdoms() & (1u << kingdom)) != 0;
    }

    // Mirrors initState() plus every location the save already shows as collected,
    // replayed through the same collectLocation()/advanceKingdomOrder() path the
    // generator uses (see collectReachableToFixedPoint) so story order, scenario
    // advances, route-tier kingdom unlocks, world-peace/moon-rock tokens and
    // moon-count credit all come out consistent with actual physical progress.
    static void buildLiveLogicState(
        GameDataHolderAccessor accessor, GameDataFile* file, LogicState& state, bool* collectedLive) {
        initState(state);

        // Auto-granted items have no physical location to discover during the
        // normal placement replay, but they are still owned by the native save
        // and must contribute their moon value and unlock tokens to live logic.
        for (s32 i = 0; i < getAutoGrantCount(); i++) {
            const s32 item = getAutoGrantItemIndex(i);
            if (isValidMoon(item) && sMoonEntries[item]
                && MoonHooks::isMoonAcquiredDirect(file, sMoonEntries[item]))
                collectItem(state, item, nullptr, 0);
        }

        for (s32 i = 0; i < LD::sTokenCount; i++) {
            const LD::Token& token = LD::sTokens[i];
            if (token.type == LD::TokenType_Ability) {
                const AbilityRando::AbilityEntry* ability = AbilityRando::findAbilityByKey(token.key);
                if (ability && AbilityRando::isAbilityUnlocked(accessor, ability->id))
                    state.tokens[i] = true;
            } else if (token.type == LD::TokenType_Capture) {
                if (CaptureRando::isCaptureUnlocked(accessor, token.key))
                    state.tokens[i] = true;
            }
        }

        for (s32 location = 0; location < LD::sMoonCount; location++)
            collectedLive[location] = isLocationPhysicallyCollected(file, location);

        bool changed = true;
        s32 guard = 0;
        while (changed && guard++ < LD::sMoonCount + 16) {
            changed = false;
            advanceKingdomOrder(state);
            for (s32 location = 0; location < LD::sMoonCount; location++) {
                if (collectedLive[location] && !state.collectedLocations[location]) {
                    collectLocation(state, location, nullptr, 0);
                    changed = true;
                }
            }
            refreshReachableStateEvents(state, nullptr, 0);
        }

        // The normal replay above only opens kingdoms through the Odyssey route.
        // A kingdom reached early through an entrance or painting is physically
        // usable even though its progression unlock has not been committed, so
        // add every recorded visit as a tracker-only start stage afterwards. This
        // deliberately happens after advanceKingdomOrder(): visiting a late route
        // kingdom must not manufacture an Odyssey unlock for the next one.
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (!hasTrackerKingdomVisit(file, kingdom))
                continue;
            const s32 mainStage = LD::sMainStageByKingdom[kingdom];
            if (isValidStage(mainStage))
                addAccessibleStage(state, mainStage);
        }

        // Preserve exact loading-zone arrivals as additional permanent starts.
        // The later fixed-point expansion turns each recorded side into the full
        // set of checks and exits reachable from that side with current abilities.
        seedRecordedArrivals(state);
    }

    // Reachability "locally" from kingdom `kingdom`'s own front door plus its
    // recorded loading-zone/painting arrivals: walks the same graph as the global
    // engine, but stops at the first stage belonging to any OTHER kingdom instead
    // of continuing into it. This is the attribution rule for the tracker's
    // per-kingdom counts and the "Kingdom Checks" list: a subarea's moons belong
    // to whichever kingdom you can physically explore them from this seed, never
    // merely to the kingdom the vanilla game files tag them with. A moon can
    // legitimately count for two kingdoms if it is reachable from both.
    static void computeKingdomLocalReachability(
        const LogicState& liveState, s32 kingdom, LogicState& localState,
        bool* localReachableNodes, bool assumeKingdomAccess = false) {
        std::memset(localReachableNodes, 0, sizeof(bool) * LD::sNodeCount);
        localState = liveState;
        // Wipe current-position state (where you can physically stand right now);
        // token/scenario/collection state (what you own, what's already happened)
        // stays as-is, since that's real progress, not a function of where you're
        // exploring from.
        std::memset(localState.accessibleStages, 0, sizeof(localState.accessibleStages));
        std::memset(localState.accessibleKingdoms, 0, sizeof(localState.accessibleKingdoms));
        std::memset(localState.accessibleNodes, 0, sizeof(localState.accessibleNodes));
        std::memset(localState.accessibleSubEntryNear, 0, sizeof(localState.accessibleSubEntryNear));
        std::memset(localState.accessibleSubEntryFar, 0, sizeof(localState.accessibleSubEntryFar));

        const s32 mainStage = isValidKingdom(kingdom) ? LD::sMainStageByKingdom[kingdom] : -1;
        if (mainStage < 0 || (!assumeKingdomAccess && !liveState.accessibleKingdoms[kingdom]))
            return; // Kingdom itself isn't reachable at all right now - nothing local either.

        localState.accessibleKingdoms[kingdom] = true;
        localState.accessibleStages[mainStage] = true;

        // The randomized start node is permanently reachable (Return to Start
        // warps there), so re-seed it after the wipe - but only into the local
        // view of the kingdom it physically sits in, which is the kingdom the
        // player would be exploring from when he uses that warp.
        const s32 startNode = StartPositionRando::getStartNode();
        if (startNode >= 0 && startNode < LD::sNodeCount) {
            const s32 startStage = LD::sNodes[startNode].stage;
            if (isValidStage(startStage) && LD::sStages[startStage].kingdom == kingdom)
                seedStartNode(localState, startNode);
        }

        // Union the kingdom's Odyssey/front-door view with every entrance or
        // painting landing the save has actually discovered in this kingdom.
        // This is the key distinction from the old Kingdom Checks calculation,
        // which always started at the kingdom default and lost mid-kingdom drops.
        seedRecordedArrivals(localState, kingdom);

        // A direct entrance from this kingdom into another kingdom's main stage
        // re-satisfies the SAME expansion condition every round (the source node
        // never stops being reachable), so clamping it away each round without
        // care would just re-discover and re-clamp it forever - an infinite loop,
        // not merely a slow one. Comparing the clamped end-state of consecutive
        // rounds (rather than "did I have to clamp anything this round") sidesteps
        // that: foreign access stays false after each clamp, while genuinely new
        // nodes, entrance sides, or non-boundary stages still register as changes.
        bool prevStages[LD::sStageCount];
        bool prevNodes[LD::sNodeCount];
        bool prevSubEntryNear[EntranceDB::sLinkCount];
        bool prevSubEntryFar[EntranceDB::sLinkCount];
        s32 guard = 0;
        for (;;) {
            std::memcpy(prevStages, localState.accessibleStages, sizeof(prevStages));
            std::memcpy(prevNodes, localState.accessibleNodes, sizeof(prevNodes));
            std::memcpy(prevSubEntryNear, localState.accessibleSubEntryNear,
                sizeof(prevSubEntryNear));
            std::memcpy(prevSubEntryFar, localState.accessibleSubEntryFar,
                sizeof(prevSubEntryFar));

            computeReachableNodes(localState, localReachableNodes);
            expandStageAccessFromEntrances(localState, localReachableNodes);
            expandStageAccessFromPaintings(localState, localReachableNodes);
            expandStageAccessFromReachableNodes(localState, localReachableNodes);

            // Clamp: never continue past another kingdom's own front door.
            //
            // addAccessibleEntranceConn records both the destination stage and
            // its arrival node.  Clearing only the stage here is insufficient:
            // the retained arrival node becomes a seed on the next
            // computeReachableNodes call and can walk that kingdom's internal
            // route graph, exposing all of its checks to the source kingdom.
            // Remove both pieces of access at the boundary.  Subareas remain
            // intact, including ones whose native kingdom tag differs from the
            // source, so a local chain still counts every check it actually
            // reaches before arriving at another kingdom's overworld.
            for (s32 k = 0; k < LD::sKingdomCount; k++) {
                if (k == kingdom)
                    continue;
                const s32 foreignMainStage = LD::sMainStageByKingdom[k];
                if (foreignMainStage >= 0) {
                    localState.accessibleStages[foreignMainStage] = false;
                    localState.accessibleKingdoms[k] = false;
                    for (s32 node = 0; node < LD::sNodeCount; node++) {
                        if (LD::sNodes[node].stage == foreignMainStage)
                            localState.accessibleNodes[node] = false;
                    }
                }
            }

            // A stage can already be known while a newly-arrived side or node
            // inside it still opens another loading zone. Comparing stages only
            // makes the fixed point stop after the first such arrival (Snow's
            // Shiveria chain is the common example), so include every piece of
            // access state that the next expansion pass can consume.
            if (std::memcmp(prevStages, localState.accessibleStages, sizeof(prevStages)) == 0
                && std::memcmp(prevNodes, localState.accessibleNodes, sizeof(prevNodes)) == 0
                && std::memcmp(prevSubEntryNear, localState.accessibleSubEntryNear,
                       sizeof(prevSubEntryNear))
                    == 0
                && std::memcmp(prevSubEntryFar, localState.accessibleSubEntryFar,
                       sizeof(prevSubEntryFar))
                    == 0)
                break;
            if (++guard >= LD::sNodeCount + EntranceDB::sLinkCount * 2 + 16)
                break; // Safety net only; the comparison above should always terminate first.
        }
        computeReachableNodes(localState, localReachableNodes);
    }

    // Tracker-only variant of isLocationReachable: once a genuinely shuffled
    // subarea's entrance is traversable, everything inside it counts as
    // reachable, without ALSO requiring the home kingdom's own story/scenario
    // counters to have independently advanced - the entrance graph already
    // captures what getting there requires. EntranceDB::isSubStage is the
    // existing, precise test for "genuinely shuffled" (Active-status links
    // only); it correctly excludes Vanilla-status connections such as story
    // boss arenas, which are NOT rando-shuffled and must keep their normal
    // story/scenario gating everywhere else. Kingdom Checks uses the
    // kingdom-specific story rules below because story order controls
    // collection progression, not always physical reachability. Deliberately
    // NOT folded into isLocationReachable itself - that function is shared
    // with seed generation, and the fill algorithm's own story/scenario
    // sequencing must not change.
    static bool isKingdomStoryLocationCollected(
        const LogicState& state, s32 kingdom, s32 objId) {
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            if (!isValidMoon(i) || !isStoryMoon(i) || LD::sMoons[i].kingdom != kingdom)
                continue;
            if (sMoonEntries[i]->objId == objId)
                return state.collectedLocations[i];
        }
        return false;
    }

    static bool areKingdomStorySinglesCollected(const LogicState& state, s32 kingdom) {
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            if (!isValidMoon(i) || !isStoryMoon(i) || LD::sMoons[i].kingdom != kingdom
                || isVanillaMultiMoon(i))
                continue;
            if (!state.collectedLocations[i])
                return false;
        }
        return true;
    }

    static bool isKingdomCheckStoryOrderSatisfied(const LogicState& state, s32 location) {
        if (!isValidMoon(location) || !isStoryMoon(location))
            return true;

        const LD::MoonAccess& moon = LD::sMoons[location];
        const s32 kingdom = moon.kingdom;

        // Cascade's single story moon and triple are both physically available
        // as soon as their movement requirements are met.
        if (kingdom == WorldMeta::WorldId_Cascade)
            return true;

        // Snow and Seaside allow their four single story moons in any order,
        // but the grand/triple story moon remains the final reward.
        if (kingdom == WorldMeta::WorldId_Snow || kingdom == WorldMeta::WorldId_Seaside) {
            if (!isVanillaMultiMoon(location))
                return true;
            return areKingdomStorySinglesCollected(state, kingdom);
        }

        // Metro's four musicians are orderless. The sewer story moon comes
        // after all four, and the festival grand moon comes after the sewer.
        if (kingdom == WorldMeta::WorldId_Metro) {
            static constexpr s32 kMetroBandMemberObjIds[] = { 7046, 7051, 7058, 9072 };
            const s32 objId = sMoonEntries[location]->objId;
            bool allBandMembersCollected = true;
            for (s32 bandObjId : kMetroBandMemberObjIds) {
                if (!isKingdomStoryLocationCollected(state, kingdom, bandObjId)) {
                    allBandMembersCollected = false;
                    break;
                }
            }

            if (objId == 360) // Powering Up the Station
                return allBandMembersCollected;
            if (objId == 4110) // A Traditional Festival!
                return allBandMembersCollected
                    && isKingdomStoryLocationCollected(state, kingdom, 360);
            return true;
        }

        // Bowser, Luncheon, Sand, and Wooded retain their fully ordered story
        // progression. Kingdoms with only a triple naturally pass at order 0.
        return moon.storyOrder < 0 || state.storyCounts[kingdom] >= moon.storyOrder;
    }

    static bool isLocationReachableForTracker(const LogicState& state,
        const bool* reachableNodes, s32 location, bool useKingdomCheckStoryRules) {
        if (!isValidMoon(location))
            return false;

        const LD::MoonAccess& moon = LD::sMoons[location];
        if (!hasStageAccessForLocation(state, reachableNodes, location))
            return false;

        const bool isShuffledSubarea = isValidStage(moon.stageIndex)
            && EntranceDB::isSubStage(LD::sStages[moon.stageIndex].stage);
        if (!isShuffledSubarea) {
            if (moon.isPrincessPeach && !hasToken(state, LD::sMushroomReachedToken))
                return false;
            if ((!useKingdomCheckStoryRules && moon.storyOrder >= 0 && isValidKingdom(moon.kingdom)
                    && state.storyCounts[moon.kingdom] < moon.storyOrder)
                || (useKingdomCheckStoryRules
                    && !isKingdomCheckStoryOrderSatisfied(state, location)))
                return false;
            if (moon.scenarioMask != 0 && isValidKingdom(moon.kingdom) && !scenarioMaskContains(moon.scenarioMask, state.kingdomScenarios[moon.kingdom]))
                return false;
            if (moon.scenarioFloor >= 0 && isValidKingdom(moon.kingdom) && state.kingdomScenarios[moon.kingdom] < moon.scenarioFloor)
                return false;
        }
        const bool stageHasRoutes = isValidStage(moon.stageIndex) && LD::sStages[moon.stageIndex].hasRoutes;
        if (stageHasRoutes
            && (moon.fromNode < 0 || moon.fromNode >= LD::sNodeCount || !reachableNodes[moon.fromNode]))
            return false;
        const s16 currentScenario = isValidKingdom(moon.kingdom)
            ? state.kingdomScenarios[moon.kingdom]
            : -1;
        if (!anyRequirementOptionScenarioAllowed(
                moon.localRequirement,
                moon.localOptionScenarioOffset,
                moon.localOptionScenarioCount,
                currentScenario))
            return false;
        if (sNoLogicFill)
            return true;
        if (!moonRequirementSatisfiedForScenario(
                state, moon.stateRequirement, 0, 0, currentScenario,
                moon, true, reachableNodes))
            return false;
        if (!moonRequirementSatisfiedForScenario(
                state,
                moon.localRequirement,
                moon.localOptionScenarioOffset,
                moon.localOptionScenarioCount,
                currentScenario,
                moon,
                false,
                reachableNodes))
            return false;
        return true;
    }

    static KingdomHintStatus sKingdomHintStatus[LD::sKingdomCount];
    static s32 sKingdomHintCount[LD::sKingdomCount];
    static s32 sKingdomHintTotalCount[LD::sKingdomCount];
    static bool sKingdomHintStatusValid = false;
    static GameDataFile* sHintStatusFile = nullptr;
    static s32 sHintStatusShineSignature = -1;
    static u32 sHintStatusVisitedKingdoms = 0;
    static s32 sHintStatusCurrentWorldId = -1;
    static u32 sHintStatusRoleLayoutHash = 0;
    static u32 sHintStatusArrivalSignature = 0;
    void refreshKingdomHintStatus(GameDataHolderAccessor accessor) {
        if (!sInitialized || !accessor.mData) {
            sKingdomHintStatusValid = false;
            return;
        }
        GameDataFile* file = accessor->getGameDataFile();
        if (!file) {
            sKingdomHintStatusValid = false;
            return;
        }

        // Moon collection is the normal reachability invalidation signal, but a
        // physical arrival through a shuffled entrance/painting also makes that
        // kingdom's checks available without changing the moon total.
        const s32 shineSignature = file->getTotalShineNum();
        const u32 visitedKingdoms = Randomizer::getVisitedKingdoms();
        const s32 currentWorldId = file->getCurrentWorldId();
        const u32 roleLayoutHash = getMoonRoleLayoutHash();
        const u32 arrivalSignature = ArrivalLog::getSignature();
        if (sKingdomHintStatusValid
            && file == sHintStatusFile
            && shineSignature == sHintStatusShineSignature
            && visitedKingdoms == sHintStatusVisitedKingdoms
            && currentWorldId == sHintStatusCurrentWorldId
            && roleLayoutHash == sHintStatusRoleLayoutHash
            && arrivalSignature == sHintStatusArrivalSignature)
            return;
        sHintStatusFile = file;
        sHintStatusShineSignature = shineSignature;
        sHintStatusVisitedKingdoms = visitedKingdoms;
        sHintStatusCurrentWorldId = currentWorldId;
        sHintStatusRoleLayoutHash = roleLayoutHash;
        sHintStatusArrivalSignature = arrivalSignature;

        LogicState state;
        bool collectedLive[LD::sMoonCount];
        buildLiveLogicState(accessor, file, state, collectedLive);

        bool reachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(state, reachableNodes);

        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            sKingdomHintStatus[k] = KingdomHintStatus::Unavailable;
            sKingdomHintCount[k] = 0;
            sKingdomHintTotalCount[k] = 0;
        }

        // Critical (and counted in the "(N)" readout): this item unlocks an
        // ability/capture; or it's credited to Mushroom/Dark Side/Darker Side
        // AND actually carries that kingdom's lock reward, which has no
        // per-kingdom requirement to check against but is always worth having;
        // or the kingdom it credits (which may differ from where it's
        // physically standing) hasn't met its moon/story requirement yet, so
        // collecting it here still helps (fungible - any moon counts).
        // Non-critical reachable items still flip the dot to Orange, but aren't
        // counted in "(N)" - that's a critical-only count.
        //
        // Being credited to one of the three is not enough on its own. With its
        // lock switched off the kingdom's moons are plain coin moons, and on
        // Scarce its surplus hosts are fool moons - neither is worth routing to,
        // and both used to be counted. This deliberately does not reuse
        // itemHasProgressUnlock: that one drops swap-unreliable captures
        // because they cannot open a route, but such a capture is still a
        // reward the player wants, so it stays critical here.
        auto isItemCritical = [&state](s32 item) {
            const s32 creditKingdom = assignedKingdomOf(item);
            const bool inLockKingdom = isRewardCarrierKingdom(creditKingdom);
            const ShineDB::ShineEntry* entry = isValidMoon(item) ? sMoonEntries[item] : nullptr;
            const bool carriesLockReward = entry
                && (CaptureRando::getCaptureCountForMoon(*entry) > 0
                    || AbilityRando::getAbilityCountForMoon(*entry) > 0);
            return itemHasProgressUnlock(item)
                || (inLockKingdom && carriesLockReward)
                || (isValidKingdom(creditKingdom) && !isKingdomRequirementMet(state, creditKingdom));
        };

        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            if (!hasTrackerKingdomVisit(file, k))
                continue;

            sKingdomHintStatus[k] = KingdomHintStatus::Red;
            s32 totalCount = 0;
            s32 criticalCount = 0;

            // Everything reachable by walking out of kingdom k's own front door
            // and following every loading zone that is free with the movement
            // the player already has - through Shiveria and on into each door
            // inside it, and so on - stopping only where a branch genuinely
            // cannot be pushed further (the far end of Flower Road) or at the
            // next kingdom's front door.
            //
            // Attribution is deliberately physical rather than by the location's
            // static/vanilla kingdom tag: a Luncheon-tagged sub-area one hop
            // from Cap belongs on Cap's list this seed, and a Sand-tagged
            // sub-area the shuffle only wired to Snow does NOT belong on Sand's,
            // even though Sand is where it lives in the vanilla game. The same
            // location can legitimately count for two kingdoms when a shuffled
            // loading zone makes it reachable from both.
            LogicState localState;
            bool localReachableNodes[LD::sNodeCount];
            computeKingdomLocalReachability(state, k, localState, localReachableNodes, false);
            for (s32 location = 0; location < LD::sMoonCount; location++) {
                if (collectedLive[location])
                    continue;
                const s32 item = sPlacementItemForLocation[location];
                if (!isValidMoon(item))
                    continue;
                if (!isLocationReachableForTracker(localState, localReachableNodes, location, false))
                    continue;

                totalCount++;
                if (isItemCritical(item))
                    criticalCount++;
            }

            sKingdomHintCount[k] = criticalCount;
            sKingdomHintTotalCount[k] = totalCount;
            if (criticalCount > 0)
                sKingdomHintStatus[k] = KingdomHintStatus::Green;
            else if (totalCount > 0)
                sKingdomHintStatus[k] = KingdomHintStatus::Orange;
        }

        sKingdomHintStatusValid = true;
    }

    KingdomHintStatus getKingdomHintStatus(s32 kingdom) {
        if (!sKingdomHintStatusValid || !isValidKingdom(kingdom))
            return KingdomHintStatus::Unavailable;
        return sKingdomHintStatus[kingdom];
    }

    s32 getKingdomHintCount(s32 kingdom) {
        if (!sKingdomHintStatusValid || !isValidKingdom(kingdom))
            return 0;
        return sKingdomHintCount[kingdom];
    }

    s32 getKingdomHintTotalCount(s32 kingdom) {
        if (!sKingdomHintStatusValid || !isValidKingdom(kingdom))
            return 0;
        return sKingdomHintTotalCount[kingdom];
    }

    // Same physical attribution rule as getKingdomHintCount (static home
    // kingdom, or reachable by chaining out through subareas from this
    // kingdom's own front door), but returns the actual placements instead of
    // just a count, for the "Kingdom Checks" mod-config menu. The selected
    // kingdom is treated as an available start, and its configured story-order
    // rules are applied separately from movement requirements. Computed fresh
    // from the current save each call (menu entries are opened rarely, unlike
    // the tracker overlay's every-frame refresh, so no throttling here).
    s32 getKingdomReachableUncollectedMoons(
        GameDataHolderAccessor accessor, s32 kingdom, const ShineDB::ShineEntry** outEntries, s32 maxCount) {
        if (!sInitialized || !accessor.mData || !isValidKingdom(kingdom) || !outEntries || maxCount <= 0)
            return 0;
        GameDataFile* file = accessor->getGameDataFile();
        if (!file)
            return 0;
        LogicState state;
        bool collectedLive[LD::sMoonCount];
        buildLiveLogicState(accessor, file, state, collectedLive);

        bool reachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(state, reachableNodes);

        s32 count = 0;

        // Same physical attribution as refreshKingdomHintStatus: what the player
        // can actually walk to from this kingdom's front door right now, not
        // what the vanilla game files say lives here. Listing a check the
        // current seed only wires to a different kingdom is exactly the thing
        // this menu must not do.
        LogicState localState;
        bool localReachableNodes[LD::sNodeCount];
        // Kingdom Checks is an inspection tool: selecting a kingdom means to
        // evaluate it from its own front door, even if the save has not visited
        // that kingdom yet.
        computeKingdomLocalReachability(state, kingdom, localState, localReachableNodes, true);
        for (s32 location = 0; location < LD::sMoonCount && count < maxCount; location++) {
            if (collectedLive[location])
                continue;
            const s32 item = sPlacementItemForLocation[location];
            if (!isValidMoon(item))
                continue;
            // Apply the selected kingdom's story rules after movement and
            // scenario reachability have been established.
            if (!isLocationReachableForTracker(localState, localReachableNodes, location, true))
                continue;
            const ShineDB::ShineEntry* entry
                = ShineDB::findEntry(LD::sMoons[location].uid, LD::sMoons[location].stage);
            if (!entry)
                continue;
            outEntries[count++] = entry;
        }

        return count;
    }

    // ── Sub-area routes ─────────────────────────────────────────────────────
    //
    // A walkthrough of the loading zones that lead from a kingdom's overworld to
    // the sub-area a placement sits in THIS seed. Everything here answers a
    // question the reachability engine deliberately does not: not "can this be
    // reached", but "through which doors, in which order, and arriving at which
    // side". That last part is why this cannot simply read the global
    // reachable-node set - dropping into a sub-area's exit door does not imply
    // you can walk back to its entrance door, and a hint that ignores the
    // difference sends the player to a wall.

    static const char* stageDisplayName(s32 stage) {
        if (!isValidStage(stage))
            return "Unknown Area";
        const LD::Stage& entry = LD::sStages[stage];
        return entry.displayName && entry.displayName[0] ? entry.displayName : entry.stage;
    }

    // Everything the player can walk to inside one stage from the node they
    // arrived at, under the CURRENT save state. Mirrors canEntranceSubBeTraversed's
    // shape, but that one proves "is this room crossable at all" under maximal
    // state; this asks what can be reached right now, from where they land.
    //
    // Computed as a set rather than a from/to question because a position in the
    // route search asks about every door in the room at once, and cleared
    // through the stage's own node list rather than by wiping the whole graph -
    // stages hold tens of nodes where the graph holds over a thousand, and this
    // runs inside the search loop.
    static bool sStageWalkReachable[LD::sNodeCount];
    static s32 sStageWalkQueue[LD::sNodeCount];
    static s32 sStageWalkStage = -1;
    static s32 sStageWalkFromNode = -1;
    // Part of the cache key: node-access requirements are answered against this
    // set, so a walk computed for one route search must not be handed to another.
    static const bool* sStageWalkReachableNodes = nullptr;

    static void computeStageWalkSet(
        const LogicState& state, const bool* reachableNodes, s32 stage, s32 fromNode) {
        if (sStageWalkStage == stage && sStageWalkFromNode == fromNode
            && sStageWalkReachableNodes == reachableNodes)
            return;

        ensureNodeGraphIndexes();
        if (isValidStage(sStageWalkStage)) {
            for (s32 node = sFirstNodeByStage[sStageWalkStage]; node >= 0;
                 node = sNextNodeInStage[node])
                sStageWalkReachable[node] = false;
        }
        sStageWalkStage = stage;
        sStageWalkFromNode = fromNode;
        sStageWalkReachableNodes = reachableNodes;
        if (!isValidStage(stage) || fromNode < 0)
            return;
        for (s32 node = sFirstNodeByStage[stage]; node >= 0; node = sNextNodeInStage[node])
            sStageWalkReachable[node] = false;

        const s32 kingdom = LD::sStages[stage].kingdom;
        const s16 scenario = isValidKingdom(kingdom) ? state.kingdomScenarios[kingdom] : -1;

        s32 queueStart = 0;
        s32 queueEnd = 0;
        sStageWalkReachable[fromNode] = true;
        sStageWalkQueue[queueEnd++] = fromNode;
        while (queueStart < queueEnd) {
            const s32 node = sStageWalkQueue[queueStart++];
            for (s32 directed = sFirstDirectedEdgeByNode[node]; directed >= 0;
                 directed = sNextDirectedEdge[directed]) {
                const s32 edgeIndex = sDirectedEdgeRouteIndex[directed];
                const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                const s32 next = sDirectedEdgeToNode[directed];
                if (next < 0 || next >= LD::sNodeCount || sStageWalkReachable[next]
                    || LD::sNodes[next].excluded)
                    continue;
                if (LD::sNodes[next].stage != stage)
                    continue;
                if (edge.scenarioMask != 0 && isValidKingdom(kingdom)
                    && !scenarioMaskContains(edge.scenarioMask, state.kingdomScenarios[kingdom]))
                    continue;
                if (!routeRequirementSatisfied(state, edge, scenario, reachableNodes))
                    continue;
                if (!isNodeScenarioActive(state, next))
                    continue;
                sStageWalkReachable[next] = true;
                sStageWalkQueue[queueEnd++] = next;
            }
        }
    }

    static void resetStageWalkSet() {
        if (isValidStage(sStageWalkStage)) {
            ensureNodeGraphIndexes();
            for (s32 node = sFirstNodeByStage[sStageWalkStage]; node >= 0;
                 node = sNextNodeInStage[node])
                sStageWalkReachable[node] = false;
        }
        sStageWalkStage = -1;
        sStageWalkFromNode = -1;
        sStageWalkReachableNodes = nullptr;
    }

    static bool canWalkWithinStage(const LogicState& state, const bool* reachableNodes,
        s32 stage, s32 fromNode, s32 toNode) {
        if (!isValidStage(stage) || fromNode < 0 || toNode < 0)
            return false;
        if (fromNode == toNode)
            return true;
        if (!LD::sStages[stage].hasRoutes)
            return true;
        computeStageWalkSet(state, reachableNodes, stage, fromNode);
        return sStageWalkReachable[toNode];
    }

    struct RouteMove {
        s16 link;
        u8 kind; // EntranceRando::ConnKind describing where this move lands
        // Paintings connect graph nodes rather than door sides, so they carry
        // their arrival node directly instead of a link/kind pair.
        bool isPainting;
        s16 paintingNode;
        char text[MoonRando::kAreaRouteStepTextSize];
    };

    static constexpr s32 kMaxRouteMovesPerStep = 48;
    // Six hops is already a route nobody wants to read; going deeper only costs
    // search time to find directions that would not help.
    static constexpr s32 kMaxRouteHops = 6;
    static constexpr s32 kMaxRouteCandidates = 8;
    // Positions expanded before the walk gives up and reports "truncated". A
    // position costs one walk of the room it is in, so this is the knob that
    // keeps an A press from stalling a frame on a heavily connected seed.
    static constexpr s32 kRouteSearchBudget = 4000;

    // Move scratch is indexed by recursion depth rather than living on the
    // stack: a kingdom overworld can offer 30+ doors, and eight nested frames
    // of that array is more stack than this call is entitled to.
    static RouteMove sRouteMoves[MoonRando::kMaxAreaRouteSteps][kMaxRouteMovesPerStep];

    static void addRouteMove(RouteMove* out, s32& count, EntranceRando::Conn conn, const char* text) {
        if (count >= kMaxRouteMovesPerStep)
            return;
        if (conn.kind == EntranceRando::ConnKind_None || conn.link < 0
            || conn.link >= EntranceDB::sLinkCount)
            return;
        RouteMove& move = out[count++];
        move.link = conn.link;
        move.kind = static_cast<u8>(conn.kind);
        move.isPainting = false;
        move.paintingNode = -1;
        nn::util::SNPrintf(move.text, sizeof(move.text), "%s", text);
    }

    static void addPaintingRouteMove(RouteMove* out, s32& count, s32 destinationNode,
        const char* text) {
        if (count >= kMaxRouteMovesPerStep)
            return;
        if (destinationNode < 0 || destinationNode >= LD::sNodeCount)
            return;
        RouteMove& move = out[count++];
        move.link = -1;
        move.kind = EntranceRando::ConnKind_None;
        move.isPainting = true;
        move.paintingNode = static_cast<s16>(destinationNode);
        nn::util::SNPrintf(move.text, sizeof(move.text), "%s", text);
    }

    enum class RouteEntranceSide : u8
    {
        HomeIn,
        HomeOut,
        SubNear,
        SubFar,
    };

    static const char* routeEntranceNoun(s32 link, RouteEntranceSide side) {
        if (link < 0 || link >= EntranceDB::sLinkCount)
            return "Entrance";

        const EntranceDB::EntranceLink& entry = EntranceDB::sLinks[link];
        s32 endpointIndex = -1;
        switch (side) {
        case RouteEntranceSide::HomeIn:
            endpointIndex = entry.homeInEndpoint;
            break;
        case RouteEntranceSide::HomeOut:
            endpointIndex = entry.homeOutEndpoint;
            break;
        case RouteEntranceSide::SubNear:
            endpointIndex = entry.subInEndpoint;
            break;
        case RouteEntranceSide::SubFar:
            endpointIndex = entry.subOutEndpoint;
            break;
        }

        if (endpointIndex < 0 || endpointIndex >= EntranceDB2::sEndpointCount)
            return "Entrance";

        const char* unitConfigName = EntranceDB2::sEndpoints[endpointIndex].unitConfigName;
        if (!unitConfigName)
            return "Entrance";
        if (strEq(unitConfigName, "DokanStageChange")
            || strEq(unitConfigName, "DokanStageChange2D"))
            return "Pipe";
        if (strEq(unitConfigName, "ChangeStageArea")
            || strEq(unitConfigName, "DoorAreaChangeCap")
            || strEq(unitConfigName, "DoorWarpStageChange"))
            return "Door";
        return "Entrance";
    }

    // Loading zones are named by where they lead in the VANILLA game. That is
    // the only name the player can match against what they see on the ground -
    // the shuffled destination is precisely the thing the hint is about. The
    // noun is based on the authored loading-zone type, with an explicit fallback
    // when the database does not know it.
    //
    // `currentArea` names the place the loading zone is in, and is set for every hop
    // after the first. Without it a route that surfaces somewhere new leaves the
    // reader with a loading-zone name and no address: the line before it named
    // where that loading zone led in the vanilla game, which is exactly where the shuffle did
    // NOT send them. The opening line already names the starting kingdom, so
    // the first hop leaves it off.
    static void makeEntranceText(char* out, size_t outSize, const char* destination,
        bool outSide, const char* currentArea, const char* noun) {
        if (currentArea)
            nn::util::SNPrintf(out, outSize, outSide ? "Back %s to %s, in %s" : "%s to %s, in %s",
                noun, destination, currentArea);
        else
            nn::util::SNPrintf(out, outSize, outSide ? "Back %s to %s" : "%s to %s",
                noun, destination);
    }

    static void makeHomeEntranceText(char* out, size_t outSize, s32 link, bool outSide,
        const char* currentArea) {
        ensureLinkStageIndexes();
        makeEntranceText(out, outSize, stageDisplayName(sLinkSubStageIndex[link]), outSide,
            currentArea, routeEntranceNoun(link, outSide ? RouteEntranceSide::HomeOut : RouteEntranceSide::HomeIn));
    }

    static void makeSubEntranceText(char* out, size_t outSize, s32 link, bool farDoor,
        const char* currentArea) {
        ensureLinkStageIndexes();
        makeEntranceText(out, outSize, stageDisplayName(sLinkHomeStageIndex[link]), false,
            currentArea, routeEntranceNoun(link, farDoor ? RouteEntranceSide::SubFar : RouteEntranceSide::SubNear));
    }

    // Whether the player, standing at `fromNode` in `stage`, can walk to a door
    // on the far side of that stage. fromNode < 0 means "this is where the route
    // starts" (a kingdom's own overworld), where the global reachable set is
    // already the right answer.
    static bool canWalkToDoorNode(const LogicState& state, const bool* reachableNodes, s32 stage,
        s32 fromNode, s32 doorNode) {
        if (doorNode < 0)
            return false;
        if (fromNode < 0)
            return reachableNodes[doorNode];
        if (!LD::sStages[stage].hasRoutes || fromNode == doorNode)
            return true;
        // The set for (stage, fromNode) is built once by the caller.
        (void)state;
        return sStageWalkReachable[doorNode];
    }

    // The general reachability engine's canUseHomeEntranceSide also requires
    // the home stage to have been globally discovered. A route hint has its own
    // starting kingdom and discovers later overworlds by following the route,
    // so use the route search's stage-local walk set instead. This keeps the
    // initial seed restricted to the selected kingdom while still allowing a
    // loading-zone arrival to be followed through a newly reached overworld.
    static bool canUseRouteHomeEntranceSide(const LogicState& state,
        const bool* reachableNodes, s32 stage, s32 arrivalNode, s32 entranceIndex,
        bool outSide) {
        if (entranceIndex < 0 || entranceIndex >= EntranceDB::sLinkCount
            || !isEntranceRandomizable(entranceIndex))
            return false;

        ensureLinkStageIndexes();
        const EntranceDB::EntranceLink& link = EntranceDB::sLinks[entranceIndex];
        if (sLinkHomeStageIndex[entranceIndex] != stage)
            return false;
        if (!outSide && !EntranceDB::isHomeInEnterable(link))
            return false;
        if (outSide && !EntranceDB::isHomeOutEnterable(link))
            return false;
        if (outSide && (!link.homeEntranceOut || strEq(link.homeEntranceIn, link.homeEntranceOut)))
            return false;

        const LD::EntranceAccess& access = LD::sEntrances[entranceIndex];
        const s32 node = outSide ? access.homeOutNode : access.homeInNode;
        return isEntranceNodeUsableInScenario(state, node)
            && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, node);
    }

    static bool canUseRouteDeepWoodsVineExit(const LogicState& state,
        const bool* reachableNodes, s32 stage, s32 arrivalNode) {
        const s32 primary = EntranceDB::findDeepWoodsPrimaryLink();
        if (primary < 0 || !isEntranceRandomizable(primary))
            return false;

        for (s32 i = 0; i < EntranceDB::sLinkCount && i < LD::sEntranceCount; i++) {
            const EntranceDB::EntranceLink& link = EntranceDB::sLinks[i];
            if (!EntranceDB::isDeepWoodsArea(link)
                || sLinkSubStageIndex[i] != stage)
                continue;

            const LD::EntranceAccess& access = LD::sEntrances[i];
            if (EntranceDB::isDeepWoodsVineExit(link.subEntranceIn)
                && access.subNearNode >= 0
                && isEntranceNodeUsableInScenario(state, access.subNearNode)
                && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode,
                    access.subNearNode))
                return true;
            if (EntranceDB::isDeepWoodsVineExit(link.subEntranceOut)
                && access.subFarNode >= 0
                && isEntranceNodeUsableInScenario(state, access.subFarNode)
                && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode,
                    access.subFarNode))
                return true;
        }
        return false;
    }

    // Every loading zone (and, for present-tense routes, painting) the player
    // can use while standing in `stage`, having arrived at `arrivalNode`. A
    // stage is both sides of the database at once:
    // Shiveria Town is the sub-area behind Snow's pipe AND the home stage of the
    // shop, lobby and costume-room doors inside it, so a route that stopped at
    // "you are in Shiveria Town" would miss everything the player can walk into
    // from there without spending anything.
    static s32 collectRouteMoves(const LogicState& state, const bool* reachableNodes, s32 stage,
        s32 arrivalLink, bool arrivedFar, s32 arrivalNode, const char* currentArea,
        bool includePaintings, RouteMove* out) {
        s32 count = 0;
        if (!isValidStage(stage))
            return 0;

        ensureLinkStageIndexes();
        // One walk of this room, shared by every door in it.
        computeStageWalkSet(state, reachableNodes, stage, arrivalNode);

        char text[MoonRando::kAreaRouteStepTextSize];
        const char* areaName = stageDisplayName(stage);

        for (s32 i = 0; i < EntranceDB::sLinkCount && count < kMaxRouteMovesPerStep; i++) {
            const LD::EntranceAccess& access = LD::sEntrances[i];

            // Doors leading OUT of this stage into their own sub-area.
            if (sLinkHomeStageIndex[i] == stage) {
                if (isEntranceRandomizable(i)) {
                    const bool homeInUsable = includePaintings
                        ? canUseHomeEntranceSide(state, reachableNodes, i, false)
                            && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, access.homeInNode)
                        : canUseRouteHomeEntranceSide(state, reachableNodes, stage, arrivalNode, i, false);
                    const bool homeOutUsable = includePaintings
                        ? canUseHomeEntranceSide(state, reachableNodes, i, true)
                            && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, access.homeOutNode)
                        : canUseRouteHomeEntranceSide(state, reachableNodes, stage, arrivalNode, i, true);
                    if (homeInUsable) {
                        makeHomeEntranceText(text, sizeof(text), i, false, currentArea);
                        addRouteMove(out, count, EntranceRando::getPipeConn(i), text);
                    }
                    if (homeOutUsable) {
                        makeHomeEntranceText(text, sizeof(text), i, true, currentArea);
                        addRouteMove(out, count, EntranceRando::getHomeDoorConn(i, true), text);
                    }
                } else if (isEntranceConnected(i)) {
                    // Fixed/vanilla doors are plain two-way connections to their
                    // own sub-area, exactly as expandStageAccessFromEntrances
                    // wires them.
                    const bool homeInUsable = EntranceDB::ownsHomeEntranceSide(i, false)
                        && isEntranceNodeUsableInScenario(state, access.homeInNode)
                        && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode,
                            access.homeInNode);
                    const bool homeOutUsable
                        = (EntranceDB::ownsHomeEntranceSide(i, true)
                            && isEntranceNodeUsableInScenario(state, access.homeOutNode)
                            && canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, access.homeOutNode));
                    if (homeInUsable || homeOutUsable) {
                        makeHomeEntranceText(text, sizeof(text), i, homeOutUsable && !homeInUsable,
                            currentArea);
                        if (access.subNearNode >= 0)
                            addRouteMove(out, count, { (s16)i, EntranceRando::ConnKind_SubNear }, text);
                        if (access.subFarNode >= 0)
                            addRouteMove(out, count, { (s16)i, EntranceRando::ConnKind_SubFar }, text);
                    }
                }
            }

            if (sLinkSubStageIndex[i] != stage || !isEntranceConnected(i))
                continue;

            // Deep Woods' vines are many physical drops but one player-facing
            // exit, and the engine routes all of them through the primary link.
            // Follow that same interface rather than naming an individual vine.
            if (EntranceDB::isDeepWoodsArea(i)) {
                const s32 primary = EntranceDB::findDeepWoodsPrimaryLink();
                const bool deepWoodsUsable = includePaintings
                    ? canUseDeepWoodsVineExit(state, reachableNodes)
                    : canUseRouteDeepWoodsVineExit(state, reachableNodes, stage, arrivalNode);
                if (i == primary && deepWoodsUsable) {
                    nn::util::SNPrintf(text, sizeof(text), "Progress through %s", areaName);
                    addRouteMove(out, count, EntranceRando::getSubDoorConn(primary, true), text);
                }
                continue;
            }

            // Doors leading OUT of this stage as somebody's sub-area. Crossing
            // the room to the opposite door is the shape the player thinks in,
            // so that one is phrased as progress; any other door in the room is
            // named by where it goes in the vanilla game, like a home-side door.
            for (s32 side = 0; side < 2; side++) {
                const bool farDoor = side == 1;
                if (farDoor && !EntranceRando::linkHasFarDoor(i))
                    continue;
                const s32 doorNode = farDoor ? access.subFarNode : access.subNearNode;
                if (!isEntranceNodeUsableInScenario(state, doorNode))
                    continue;
                if (!canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, doorNode))
                    continue;

                const bool isOppositeDoor = i == arrivalLink && farDoor != arrivedFar;
                const bool isArrivalDoor = i == arrivalLink && farDoor == arrivedFar;
                if (isArrivalDoor)
                    continue; // walking straight back out the way you came in
                if (isOppositeDoor)
                    nn::util::SNPrintf(text, sizeof(text),
                        arrivedFar ? "Go back through %s" : "Progress through %s", areaName);
                else
                    makeSubEntranceText(text, sizeof(text), i, farDoor, currentArea);
                addRouteMove(out, count, EntranceRando::getSubDoorConn(i, farDoor), text);
            }
        }
        if (includePaintings) {
            // Paintings remain available to present-tense Kingdom Checks routes.
            // Seed/Extended Hint routes pass false because they describe
            // loading-zone travel only.
            for (s32 i = 0; i < LD::sPaintingLinkCount && count < kMaxRouteMovesPerStep; i++) {
                if (!isPaintingUnlockedForLogic(state, i))
                    continue;

                const LD::PaintingLink& painting = LD::sPaintingLinks[i];
                const bool paintingRandoActive
                    = Randomizer::isPaintingRandoEnabled() && PaintingRando::isInitialized();

                for (s32 direction = 0; direction < 2; direction++) {
                    const bool forward = direction == 0;
                    if (!forward && !painting.bidirectional)
                        continue;
                    const s32 sourceNode = forward ? painting.sourceNode : painting.destinationNode;
                    if (sourceNode < 0 || sourceNode >= LD::sNodeCount
                        || LD::sNodes[sourceNode].stage != stage)
                        continue;
                    if (!isNodeScenarioActive(state, sourceNode))
                        continue;
                    if (!canWalkToDoorNode(state, reachableNodes, stage, arrivalNode, sourceNode))
                        continue;

                    s32 destinationNode = forward ? painting.destinationNode : painting.sourceNode;
                    if (paintingRandoActive)
                        destinationNode = forward ? PaintingRando::getMappedDestinationNode(i)
                                                  : PaintingRando::getMappedReturnSourceNode(i);
                    if (destinationNode < 0 || destinationNode >= LD::sNodeCount)
                        continue;

                    nn::util::SNPrintf(text, sizeof(text), "Take the painting to %s",
                        stageDisplayName(LD::sNodes[destinationNode].stage));
                    addPaintingRouteMove(out, count, destinationNode, text);
                }
            }
        }
        return count;
    }

    struct RouteSearch {
        const LogicState* state;
        const bool* reachableNodes;
        s32 targetStage;
        // The moon's own graph node, so an arrival side that cannot actually
        // walk to it is rejected instead of reported as a route. -1 when the
        // stage has no authored routes and any arrival counts.
        s32 targetNode;
        // When >= 0, routes must start at this kingdom's overworld and may not
        // continue past another kingdom's front door - the same boundary the
        // "Kingdom Checks" list uses to decide what belongs to a kingdom.
        s32 startKingdom;
        // Raised one level at a time by the caller, so the shortest way in is
        // always found (and the search stopped) before a longer one is explored.
        s32 maxHops;
        s32 budget;
        s32 candidateCount;
        bool truncated;
        bool includePaintings;
        MoonRando::AreaRoute candidates[kMaxRouteCandidates];
        // Per-branch, keyed on the stage rather than the door: a route that
        // walks back into somewhere it has already been is a detour, never the
        // way in worth telling the player about.
        bool visitedStage[LD::sStageCount];
        s32 stepCount;
        char steps[MoonRando::kMaxAreaRouteSteps][MoonRando::kAreaRouteStepTextSize];
    };

    static RouteSearch sRouteSearch;

    static bool routeAlreadyCollected(const RouteSearch& search, const MoonRando::AreaRoute& route) {
        for (s32 i = 0; i < search.candidateCount; i++) {
            const MoonRando::AreaRoute& other = search.candidates[i];
            if (other.stepCount != route.stepCount)
                continue;
            bool same = true;
            for (s32 step = 0; step < route.stepCount && same; step++)
                same = strEq(other.steps[step], route.steps[step]);
            if (same)
                return true;
        }
        return false;
    }

    static void emitRoute(RouteSearch& search, s32 link, bool isSub, bool arrivedFar) {
        MoonRando::AreaRoute route = {};
        route.stepCount = 0;
        for (s32 i = 0; i < search.stepCount && route.stepCount < MoonRando::kMaxAreaRouteSteps - 1; i++)
            nn::util::SNPrintf(route.steps[route.stepCount++], MoonRando::kAreaRouteStepTextSize,
                "%s", search.steps[i]);

        const char* side = "";
        if (isSub && link >= 0 && EntranceRando::linkHasFarDoor(link))
            side = arrivedFar ? " (exit side)" : " (entrance side)";
        nn::util::SNPrintf(route.steps[route.stepCount++], MoonRando::kAreaRouteStepTextSize,
            "Reach %s%s", stageDisplayName(search.targetStage), side);

        if (routeAlreadyCollected(search, route))
            return;
        if (search.candidateCount >= kMaxRouteCandidates) {
            search.truncated = true;
            return;
        }
        search.candidates[search.candidateCount++] = route;
    }

    static bool arrivalReachesTarget(const RouteSearch& search, s32 arrivalNode) {
        if (search.targetNode < 0)
            return true;
        if (arrivalNode < 0)
            return search.reachableNodes[search.targetNode];
        return canWalkWithinStage(*search.state, search.reachableNodes, search.targetStage,
            arrivalNode, search.targetNode);
    }

    static void searchRoutesFrom(
        RouteSearch& search, s32 stage, s32 link, bool isSub, bool arrivedFar, s32 arrivalNode) {
        if (search.budget-- <= 0) {
            search.truncated = true;
            return;
        }
        // Enough ways in have been found; anything further would be dropped by
        // buildRoutesToLocation anyway.
        if (search.candidateCount >= kMaxRouteCandidates) {
            search.truncated = true;
            return;
        }
        if (!isValidStage(stage))
            return;

        if (stage == search.targetStage && search.stepCount > 1 && arrivalReachesTarget(search, arrivalNode))
            emitRoute(search, link, isSub, arrivedFar);

        // Another kingdom's overworld is the end of a kingdom-scoped route, the
        // same way it ends a kingdom-local reachability chain.
        if (!isSub && search.startKingdom >= 0 && search.stepCount > 1) {
            const s32 kingdom = LD::sStages[stage].kingdom;
            if (isValidKingdom(kingdom) && kingdom != search.startKingdom
                && LD::sMainStageByKingdom[kingdom] == stage)
                return;
        }

        if (search.stepCount >= search.maxHops + 1)
            return;

        // Marked for the duration of this branch only, not for the whole
        // search: a place already used by one route may still be part of a
        // different one, and the point of this walk is to find every way in.
        bool* visited = &search.visitedStage[stage];
        if (*visited)
            return;
        *visited = true;

        // Move scratch is owned per depth, and the recursion below only ever
        // touches deeper slots, so moves[] stays valid across the loop.
        RouteMove* moves = sRouteMoves[search.stepCount];
        // Past the opening line every door is qualified with the place it is in:
        // the previous line named a vanilla destination, not where the shuffle
        // actually dropped the player, so without this a route that surfaces
        // somewhere new hands the reader a door name and no address.
        const char* currentArea = search.stepCount > 1 ? stageDisplayName(stage) : nullptr;
        const s32 moveCount = collectRouteMoves(*search.state, search.reachableNodes, stage,
            isSub ? link : -1, arrivedFar, arrivalNode, currentArea,
            search.includePaintings, moves);

        for (s32 i = 0; i < moveCount; i++) {
            const RouteMove& move = moves[i];

            s32 destStage = -1;
            s32 destLink = -1;
            s32 destArrivalNode = -1;
            bool destIsSub = false;
            bool destIsFar = false;

            if (move.isPainting) {
                destStage = LD::sNodes[move.paintingNode].stage;
                destArrivalNode = move.paintingNode;
            } else {
                const EntranceRando::Conn conn = { move.link, (EntranceRando::ConnKind)move.kind };
                destIsSub = connIsSubArrival(conn);
                destIsFar = conn.kind == EntranceRando::ConnKind_SubFar;
                destLink = conn.link;
                destStage = destIsSub ? sLinkSubStageIndex[conn.link] : sLinkHomeStageIndex[conn.link];
                destArrivalNode = arrivalNodeForConn(conn);
            }
            if (!isValidStage(destStage))
                continue;

            nn::util::SNPrintf(search.steps[search.stepCount], MoonRando::kAreaRouteStepTextSize,
                "%s", move.text);
            search.stepCount++;
            searchRoutesFrom(search, destStage, destLink, destIsSub, destIsFar, destArrivalNode);
            search.stepCount--;
        }

        *visited = false;
    }

    static void sortRoutesByLength(MoonRando::AreaRoute* routes, s32 count) {
        for (s32 i = 1; i < count; i++) {
            MoonRando::AreaRoute key = routes[i];
            s32 j = i - 1;
            while (j >= 0 && routes[j].stepCount > key.stepCount) {
                routes[j + 1] = routes[j];
                j--;
            }
            routes[j + 1] = key;
        }
    }

    bool isLocationInSubArea(const ShineDB::ShineEntry* location) {
        if (!location || !location->stage)
            return false;
        // Anything that is not a kingdom's own overworld sits behind at least
        // one loading zone, and naming the kingdom is not enough to find it.
        // Deliberately keep fixed story rooms here as well: the route builder
        // can describe any loading-zone connection, whether or not it is in the
        // shuffle pool.
        const s32 stage = findStageIndex(location->stage);
        return isValidStage(stage) && !shouldSeedStageStart(stage);
    }

    // A LogicState that describes the SEED rather than the save: every ability
    // and capture granted, every kingdom's widest scenario, all kingdoms open.
    // Built once per seed because none of its inputs move during play. The
    // same shape canEntranceSubBeTraversed uses for its own "is this physically
    // possible at all" proofs.
    static LogicState sPhysicalRouteState;
    static bool sPhysicalRouteStateReady = false;
    static bool sRouteStartReachable[LD::sNodeCount];
    static s32 sRouteStartQueue[LD::sNodeCount];

    static void invalidatePhysicalRouteState() { sPhysicalRouteStateReady = false; }

    static void ensurePhysicalRouteState() {
        if (sPhysicalRouteStateReady)
            return;

        LogicState& state = sPhysicalRouteState;
        std::memset(&state, 0, sizeof(state));
        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            const s16 maxScenario = LD::sMaxLogicScenarioByKingdom[i];
            state.kingdomScenarios[i] = maxScenario > 0 ? maxScenario : LD::sStartScenarioByKingdom[i];
            state.accessibleKingdoms[i] = true;
            const s32 mainStage = LD::sMainStageByKingdom[i];
            if (isValidStage(mainStage))
                state.accessibleStages[mainStage] = true;
        }
        for (s32 i = 0; i < LD::sTokenCount; i++)
            state.tokens[i] = true;

        sPhysicalRouteStateReady = true;
    }

    // Build the node set available from exactly one kingdom's own overworld,
    // following only authored walking edges. Entrance links are followed later
    // by the loading-zone-only route search used by Extended Hints.
    static bool computeLoadingZoneStartReachability(const LogicState& state,
        s32 startKingdom, bool* reachableNodes) {
        std::memset(reachableNodes, 0, sizeof(bool) * LD::sNodeCount);
        if (!isValidKingdom(startKingdom) || !state.accessibleKingdoms[startKingdom])
            return false;

        const s32 mainStage = LD::sMainStageByKingdom[startKingdom];
        if (!isValidStage(mainStage) || !state.accessibleStages[mainStage])
            return false;

        ensureNodeGraphIndexes();
        s32 queueStart = 0;
        s32 queueEnd = 0;
        for (s32 node = sFirstNodeByStage[mainStage]; node >= 0;
             node = sNextNodeInStage[node]) {
            const LD::Node& logicNode = LD::sNodes[node];
            if (logicNode.excluded)
                continue;
            const bool isStart = logicNode.type == LD::NodeType_StageStart
                || !LD::sStages[mainStage].hasRoutes;
            if (!isStart || !isNodeScenarioActive(state, node))
                continue;
            reachableNodes[node] = true;
            sRouteStartQueue[queueEnd++] = node;
        }

        const s16 scenario = state.kingdomScenarios[startKingdom];
        while (queueStart < queueEnd) {
            const s32 node = sRouteStartQueue[queueStart++];
            for (s32 directed = sFirstDirectedEdgeByNode[node]; directed >= 0;
                 directed = sNextDirectedEdge[directed]) {
                const s32 edgeIndex = sDirectedEdgeRouteIndex[directed];
                const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                const s32 next = sDirectedEdgeToNode[directed];
                if (next < 0 || next >= LD::sNodeCount || reachableNodes[next]
                    || LD::sNodes[next].excluded || LD::sNodes[next].stage != mainStage)
                    continue;
                if (edge.scenarioMask != 0
                    && !scenarioMaskContains(edge.scenarioMask, scenario))
                    continue;
                // Deliberately no reachable set: this walk is confined to one
                // overworld and is only the seed for the Extended Hints route
                // search, so answering cross-stage node access against its own
                // partial set would report blocks the placement proof never saw.
                if (!routeRequirementSatisfied(state, edge, scenario, nullptr)
                    || !isNodeScenarioActive(state, next))
                    continue;
                reachableNodes[next] = true;
                sRouteStartQueue[queueEnd++] = next;
            }
        }

        for (s32 node = sFirstNodeByStage[mainStage]; node >= 0;
             node = sNextNodeInStage[node]) {
            if (reachableNodes[node])
                return true;
        }
        return false;
    }

    static bool buildRoutes(const LogicState& state, const bool* existingReachableNodes,
        s32 targetStage, s32 targetNode, const char* targetLabel, s32 startKingdom,
        bool loadingZonesOnly, bool ignoreDifficultFill, MoonRando::AreaRouteSet* out) {
        if (!out || !isValidStage(targetStage))
            return false;
        out->routeCount = 0;
        out->truncated = false;

        // A route is a physical walkthrough. When the caller supplies a
        // starting kingdom, stay within that kingdom's route scope; seed hints
        // pass -1 and try each accessible kingdom as a separate starting point.
        const s32 savedPrioritizeFill = sDifficultFillLevel;
        if (ignoreDifficultFill)
            sDifficultFillLevel = -1;

        RouteSearch& search = sRouteSearch;
        std::memset(&search, 0, sizeof(search));
        search.state = &state;
        search.includePaintings = !loadingZonesOnly;
        search.targetStage = targetStage;
        search.targetNode = targetNode;
        search.budget = kRouteSearchBudget;

        // Deepened one hop at a time so the shortest way in is always found
        // first, and the walk stops as soon as there are enough to show. A plain
        // depth-limited DFS would happily spend the whole budget on a six-hop
        // detour before noticing the one-door answer sitting next to it.
        for (search.maxHops = 1; search.maxHops <= kMaxRouteHops; search.maxHops++) {
            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                if (isValidKingdom(startKingdom) && kingdom != startKingdom)
                    continue;
                if (!state.accessibleKingdoms[kingdom])
                    continue;

                const s32 mainStage = LD::sMainStageByKingdom[kingdom];
                if (!isValidStage(mainStage) || !state.accessibleStages[mainStage])
                    continue;

                const bool* reachableNodes = existingReachableNodes;
                if (loadingZonesOnly) {
                    if (!computeLoadingZoneStartReachability(state, kingdom, sRouteStartReachable))
                        continue;
                    reachableNodes = sRouteStartReachable;
                }

                // The stage-walk memo is keyed on (stage, arrival node) only, so
                // it must not survive into a different starting kingdom/state.
                resetStageWalkSet();
                search.reachableNodes = reachableNodes;
                search.startKingdom = kingdom;
                search.stepCount = 0;
                nn::util::SNPrintf(search.steps[search.stepCount++],
                    MoonRando::kAreaRouteStepTextSize, "%s", stageDisplayName(mainStage));
                searchRoutesFrom(search, mainStage, -1, false, false, -1);

                if (search.candidateCount >= MoonRando::kMaxAreaRoutes || search.budget <= 0)
                    break;
            }

            if (search.candidateCount >= MoonRando::kMaxAreaRoutes || search.budget <= 0)
                break;
        }

        sortRoutesByLength(search.candidates, search.candidateCount);

        for (s32 i = 0; i < search.candidateCount && out->routeCount < MoonRando::kMaxAreaRoutes; i++)
            out->routes[out->routeCount++] = search.candidates[i];
        out->truncated = search.truncated || search.candidateCount > out->routeCount;

        Randomizer::logPush("route to %s (%s) startKingdom=%d routes=%d budgetLeft=%d",
            targetLabel ? targetLabel : "<none>",
            isValidStage(targetStage) ? LD::sStages[targetStage].stage : "<none>",
            startKingdom, out->routeCount,
            search.budget);
        for (s32 i = 0; i < out->routeCount; i++) {
            for (s32 step = 0; step < out->routes[i].stepCount; step++)
                Randomizer::logPush("  route %d step %d: %s", i, step, out->routes[i].steps[step]);
        }

        sDifficultFillLevel = savedPrioritizeFill;
        return out->routeCount > 0;
    }

    bool buildSeedRoutesToLocation(
        const ShineDB::ShineEntry* location, MoonRando::AreaRouteSet* out) {
        if (out) {
            out->routeCount = 0;
            out->truncated = false;
        }
        if (!sInitialized || !out || !isLocationInSubArea(location))
            return false;

        const s32 locationIndex = findMoonItemByIdentity(location->uid, location->stage);
        if (!isValidMoon(locationIndex))
            return false;
        const LD::MoonAccess& moon = LD::sMoons[locationIndex];

        ensurePhysicalRouteState();
        return buildRoutes(sPhysicalRouteState, nullptr, moon.stageIndex,
            LD::sStages[moon.stageIndex].hasRoutes ? moon.fromNode : -1,
            location->name, -1, true, true, out);
    }

    bool buildReachableRoutesToLocation(GameDataHolderAccessor accessor,
        const ShineDB::ShineEntry* location, s32 startKingdom, MoonRando::AreaRouteSet* out) {
        if (out) {
            out->routeCount = 0;
            out->truncated = false;
        }
        if (!sInitialized || !accessor.mData || !out || !isLocationInSubArea(location))
            return false;
        GameDataFile* file = accessor->getGameDataFile();
        if (!file)
            return false;

        const s32 locationIndex = findMoonItemByIdentity(location->uid, location->stage);
        if (!isValidMoon(locationIndex))
            return false;
        const LD::MoonAccess& moon = LD::sMoons[locationIndex];
        if (!isValidStage(moon.stageIndex))
            return false;

        static LogicState state;
        static LogicState localState;
        static bool collectedLive[LD::sMoonCount];
        static bool localReachableNodes[LD::sNodeCount];
        buildLiveLogicState(accessor, file, state, collectedLive);

        // The route must start with this kingdom's own front door. Using the
        // global node set here can make a door look usable only because another
        // visited kingdom exposed it, which is especially confusing when the
        // selected kingdom contains a chain of loading zones.
        computeKingdomLocalReachability(state, startKingdom, localState, localReachableNodes, true);

        return buildRoutes(localState, localReachableNodes, moon.stageIndex,
            LD::sStages[moon.stageIndex].hasRoutes ? moon.fromNode : -1,
            location->name, startKingdom, false, false, out);
    }

    bool buildReachableRoutesToKingdom(GameDataHolderAccessor accessor,
        s32 startKingdom, s32 targetKingdom, MoonRando::AreaRouteSet* out) {
        if (out) {
            out->routeCount = 0;
            out->truncated = false;
        }
        if (!sInitialized || !accessor.mData || !out
            || !isValidKingdom(startKingdom) || !isValidKingdom(targetKingdom)
            || startKingdom == targetKingdom)
            return false;

        GameDataFile* file = accessor->getGameDataFile();
        if (!file)
            return false;

        static LogicState state;
        static LogicState localState;
        static bool collectedLive[LD::sMoonCount];
        static bool localReachableNodes[LD::sNodeCount];
        buildLiveLogicState(accessor, file, state, collectedLive);
        computeKingdomLocalReachability(state, startKingdom, localState, localReachableNodes, true);

        const s32 targetStage = LD::sMainStageByKingdom[targetKingdom];
        if (!isValidStage(targetStage))
            return false;

        return buildRoutes(localState, localReachableNodes, targetStage, -1,
            WorldMeta::getDisplayNameForWorldId(targetKingdom), startKingdom,
            false, false, out);
    }

    s32 getKingdomDirectAccesses(GameDataHolderAccessor accessor, s32 kingdom,
        s16* outKingdoms, s32 maxCount) {
        if (!sInitialized || !accessor.mData || !isValidKingdom(kingdom)
            || !outKingdoms || maxCount <= 0)
            return 0;

        GameDataFile* file = accessor->getGameDataFile();
        if (!file)
            return 0;

        LogicState state;
        LogicState localState;
        bool collectedLive[LD::sMoonCount];
        bool localReachableNodes[LD::sNodeCount];
        buildLiveLogicState(accessor, file, state, collectedLive);
        // Kingdom Checks is an inspection tool: selecting a kingdom means to
        // evaluate it from its own front door, even if the save has not visited
        // that kingdom yet.
        computeKingdomLocalReachability(state, kingdom, localState, localReachableNodes, true);

        s32 count = 0;
        for (s32 target = 0; target < LD::sKingdomCount && count < maxCount; target++) {
            // Kingdom access is itself the check: once Mario has physically
            // visited the destination, do not keep presenting it as an
            // outstanding row even if a return route still exists.
            if (target == kingdom || hasTrackerKingdomVisit(file, target))
                continue;
            const s32 targetStage = LD::sMainStageByKingdom[target];
            if (!isValidStage(targetStage))
                continue;

            MoonRando::AreaRouteSet routes;
            if (!buildRoutes(localState, localReachableNodes, targetStage, -1,
                    WorldMeta::getDisplayNameForWorldId(target), kingdom,
                    false, false, &routes))
                continue;
            outKingdoms[count++] = (s16)target;
        }
        return count;
    }

    static void mergeStateProgress(LogicState& dest, const LogicState& src) {
        for (s32 slot = 0; slot < kMaxSubareaKeySlots; slot++)
            dest.subareaKeysCollected[slot]
                = dest.subareaKeysCollected[slot] || src.subareaKeysCollected[slot];

        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            dest.accessibleKingdoms[i] = dest.accessibleKingdoms[i] || src.accessibleKingdoms[i];
            dest.moonRockKeysCollected[i] = dest.moonRockKeysCollected[i] || src.moonRockKeysCollected[i];
            if (src.kingdomScenarios[i] > dest.kingdomScenarios[i])
                dest.kingdomScenarios[i] = src.kingdomScenarios[i];
            if (src.storyCounts[i] > dest.storyCounts[i])
                dest.storyCounts[i] = src.storyCounts[i];
        }

        for (s32 i = 0; i < LD::sStageCount; i++) {
            dest.accessibleStages[i] = dest.accessibleStages[i] || src.accessibleStages[i];
            if (dest.stageTraces[i].kind == StageTrace_None && src.stageTraces[i].kind != StageTrace_None)
                dest.stageTraces[i] = src.stageTraces[i];
        }

        for (s32 i = 0; i < LD::sNodeCount; i++)
            dest.accessibleNodes[i] = dest.accessibleNodes[i] || src.accessibleNodes[i];
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            dest.accessibleSubEntryNear[i] = dest.accessibleSubEntryNear[i] || src.accessibleSubEntryNear[i];
            dest.accessibleSubEntryFar[i] = dest.accessibleSubEntryFar[i] || src.accessibleSubEntryFar[i];
        }
        for (s32 i = 0; i < LD::sTokenCount; i++)
            dest.tokens[i] = dest.tokens[i] || src.tokens[i];
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
            dest.unlockedPaintings[i] = dest.unlockedPaintings[i] || src.unlockedPaintings[i];
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            dest.collectedLocations[i] = dest.collectedLocations[i] || src.collectedLocations[i];
        }

        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!src.collectedItems[item] || dest.collectedItems[item])
                continue;
            dest.collectedItems[item] = true;
            const s32 kingdom = assignedKingdomOf(item);
            if (isValidKingdom(kingdom))
                dest.moonCounts[kingdom] += (s16)moonValue(item);
        }
    }

    static bool candidateLess(s32 left, s32 right) {
        const bool leftStory = isStoryMoon(left);
        const bool rightStory = isStoryMoon(right);
        if (leftStory != rightStory)
            return leftStory;

        const bool leftUnlock = itemHasProgressUnlock(left);
        const bool rightUnlock = itemHasProgressUnlock(right);
        if (leftUnlock != rightUnlock)
            return leftUnlock;

        const s32 leftValue = moonValue(left);
        const s32 rightValue = moonValue(right);
        if (leftValue != rightValue)
            return leftValue > rightValue;

        return left < right;
    }

    static void sortProgressCandidates(s32* candidates, s32 count) {
        for (s32 i = 0; i < count; i++) {
            for (s32 j = i + 1; j < count; j++) {
                if (!candidateLess(candidates[j], candidates[i]))
                    continue;
                const s32 tmp = candidates[i];
                candidates[i] = candidates[j];
                candidates[j] = tmp;
            }
        }
    }

    static u64 nextModulo64(Randomizer::Rng& rng, u64 range) {
        if (range == 0)
            return 0;
        const u64 value = (static_cast<u64>(rng.next()) << 32) | rng.next();
        return value % range;
    }

    // Seed Difficulty C1/C2, shared by both passes of the picker below so the
    // total and the draw can never disagree.
    //
    // C1 (sphere depth): Relaxed inverts the exponent so shallow, already-open
    // locations win; Brutal cubes it so the item lands as deep as the frontier
    // allows.
    //
    // C2 (unlock/use separation): only meaningful for an item that carries a
    // progression unlock and only when the caller knows which kingdom's gate it
    // is being drawn for. Relaxed puts the unlock in the same kingdom as the
    // gate it opens; Brutal deliberately throws it somewhere else, which is
    // what produces the long back-and-forth routes.
    static u64 depthWeightForLocation(
        s32 item,
        s32 location,
        s32 gateKingdom,
        s32 minSphere,
        s32 maxRelativeDepth,
        s32 provisionalSphere) {
        const s32 locationSphere
            = sLocationSphere[location] >= 0 ? sLocationSphere[location] : provisionalSphere;
        const s32 relativeDepth = locationSphere - minSphere;

        const s32 difficulty = seedDifficultyLevel();
        s32 exponent;
        switch (difficulty) {
        case 0:
            exponent = maxRelativeDepth - relativeDepth;
            break;
        case 1:
            exponent = (maxRelativeDepth - relativeDepth) / 2;
            break;
        case 3:
            exponent = relativeDepth * 2;
            break;
        case 4:
            exponent = relativeDepth * 3;
            break;
        default:
            exponent = relativeDepth;
            break;
        }

        if (isValidKingdom(gateKingdom) && itemHasProgressUnlock(item)) {
            const s32 locationKingdom = LD::sMoons[location].kingdom;
            const bool sameKingdom = locationKingdom == gateKingdom;
            // Additive on the exponent, so it composes with the depth term
            // rather than fighting it.
            static constexpr s32 kNearBonus[Randomizer::cSeedAxisLevelCount] = { 3, 1, 0, 0, 0 };
            static constexpr s32 kFarBonus[Randomizer::cSeedAxisLevelCount] = { 0, 0, 0, 1, 3 };
            exponent += sameKingdom ? kNearBonus[difficulty] : kFarBonus[difficulty];
        }

        if (exponent < 0)
            exponent = 0;
        return 1ULL << (exponent < 30 ? exponent : 30);
    }

    // gateKingdom is the kingdom whose progression this placement is serving,
    // or -1 when the caller has no such context (the weighting then reduces to
    // pure sphere depth).
    static s32 pickDepthWeightedCompatibleLocation(
        s32 item,
        const s32* reachable,
        s32 reachableCount,
        Randomizer::Rng& rng,
        s32 gateKingdom = -1) {
        s32 minSphere = 0x7FFFFFFF;
        s32 maxSphere = -1;
        bool hasCompatible = false;
        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!canPlaceMoonItemAtLocation(item, location))
                continue;
            hasCompatible = true;
            if (sLocationSphere[location] >= 0 && sLocationSphere[location] < minSphere)
                minSphere = sLocationSphere[location];
            if (sLocationSphere[location] > maxSphere)
                maxSphere = sLocationSphere[location];
        }
        if (!hasCompatible)
            return -1;
        if (minSphere == 0x7FFFFFFF)
            minSphere = 0;
        const s32 provisionalSphere = maxSphere >= 0 ? maxSphere + 1 : minSphere;
        s32 maxRelativeDepth = 0;
        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!canPlaceMoonItemAtLocation(item, location))
                continue;
            const s32 relativeDepth = (sLocationSphere[location] >= 0
                                              ? sLocationSphere[location]
                                              : provisionalSphere)
                - minSphere;
            if (relativeDepth > maxRelativeDepth)
                maxRelativeDepth = relativeDepth;
        }

        u64 totalWeight = 0;
        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!canPlaceMoonItemAtLocation(item, location))
                continue;
            totalWeight += depthWeightForLocation(
                item, location, gateKingdom, minSphere, maxRelativeDepth, provisionalSphere);
        }

        u64 pick = nextModulo64(rng, totalWeight);
        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!canPlaceMoonItemAtLocation(item, location))
                continue;
            const u64 weight = depthWeightForLocation(
                item, location, gateKingdom, minSphere, maxRelativeDepth, provisionalSphere);
            if (pick < weight)
                return location;
            pick -= weight;
        }
        return -1;
    }

    // Tail placements deliberately avoid depth weighting. Prefer the host kingdom
    // currently holding the fewest items from this item's credited kingdom, then pick
    // randomly among ties. This scatters leftover moon sets and unlock carriers.
    static s32 pickEvenlyDistributedCompatibleLocation(
        s32 item, const s32* reachable, s32 reachableCount, Randomizer::Rng& rng) {
        const s32 itemKingdom = assignedKingdomOf(item);
        s16 distribution[LD::sKingdomCount][LD::sKingdomCount] = {};
        for (s32 placedLocation = 0; placedLocation < LD::sMoonCount; placedLocation++) {
            const s32 placedItem = sPlacementItemForLocation[placedLocation];
            if (!isValidMoon(placedItem))
                continue;
            const s32 host = LD::sMoons[placedLocation].kingdom;
            const s32 credited = assignedKingdomOf(placedItem);
            if (isValidKingdom(host) && isValidKingdom(credited))
                distribution[host][credited]++;
        }
        s32 bestCount = 0x7FFFFFFF;
        s32 candidates[LD::sMoonCount];
        s32 candidateCount = 0;

        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!canPlaceMoonItemAtLocation(item, location))
                continue;
            const s32 hostKingdom = LD::sMoons[location].kingdom;
            const s32 pairCount = isValidKingdom(hostKingdom) && isValidKingdom(itemKingdom)
                ? distribution[hostKingdom][itemKingdom]
                : 0;
            if (pairCount < bestCount) {
                bestCount = pairCount;
                candidateCount = 0;
            }
            if (pairCount == bestCount)
                candidates[candidateCount++] = location;
        }
        if (candidateCount <= 0)
            return -1;
        return candidates[rng.nextModulo((u32)candidateCount)];
    }

    static const char* stageName(s32 stage) {
        return isValidStage(stage) ? LD::sStages[stage].stage : "unknown stage";
    }

    static const char* nodeName(s32 node) {
        return node >= 0 && node < LD::sNodeCount && LD::sNodes[node].nodeId ? LD::sNodes[node].nodeId : "unknown node";
    }

    static const char* kingdomName(s32 kingdom) {
        return isValidKingdom(kingdom) ? WorldMeta::getDisplayNameForWorldId(kingdom) : "unknown kingdom";
    }

    static void appendTravelStep(PathStep& step, const char* fmt, ...) {
        if (step.travelStepCount >= kMaxPathTravelSteps)
            return;

        TravelStep& travel = step.travelSteps[step.travelStepCount++];
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(travel.text, kPathTravelStepTextSize, fmt, args);
        va_end(args);
    }

    static bool entranceConnMatches(
        EntranceRando::Conn conn,
        s32 link,
        EntranceRando::ConnKind kind) {
        return conn.link == link && conn.kind == kind;
    }

    static void appendStageTravel(PathStep& step, const LogicState& state, s32 stage, s32 depth) {
        if (!isValidStage(stage) || depth > 3)
            return;

        const StageTrace& trace = state.stageTraces[stage];
        const s32 kingdom = LD::sStages[stage].kingdom;

        if (trace.kind == StageTrace_Entrance) {
            if (isValidStage(trace.sourceStage))
                appendStageTravel(step, state, trace.sourceStage, depth + 1);

            if (trace.sourceEntrance >= 0 && trace.sourceEntrance < EntranceDB::sLinkCount && trace.destEntrance >= 0 && trace.destEntrance < EntranceDB::sLinkCount) {
                const EntranceDB::EntranceLink& src = EntranceDB::sLinks[trace.sourceEntrance];
                const EntranceDB::EntranceLink& dest = EntranceDB::sLinks[trace.destEntrance];
                const EntranceRando::ConnKind destKind = (EntranceRando::ConnKind)trace.entranceChainFirstKind;

                const bool sourceIsSub = src.subStage
                    && strEq(stageName(trace.sourceStage), src.subStage);
                const char* sourceDoor = src.homeEntranceIn;
                if (sourceIsSub) {
                    sourceDoor = src.subEntranceIn;
                    if (entranceConnMatches(
                            EntranceRando::getSubDoorConn(trace.sourceEntrance, true),
                            trace.destEntrance,
                            destKind))
                        sourceDoor = src.subEntranceOut;
                } else if (entranceConnMatches(
                               EntranceRando::getHomeDoorConn(trace.sourceEntrance, true),
                               trace.destEntrance,
                               destKind)) {
                    sourceDoor = src.homeEntranceOut;
                }

                const char* destinationStage = dest.subStage;
                const char* destinationDoor = dest.subEntranceIn;
                if (destKind == EntranceRando::ConnKind_SubFar) {
                    destinationDoor = dest.subEntranceOut;
                } else if (destKind == EntranceRando::ConnKind_HomeIn) {
                    destinationStage = dest.homeStage;
                    destinationDoor = dest.homeEntranceIn;
                } else if (destKind == EntranceRando::ConnKind_HomeOut) {
                    destinationStage = dest.homeStage;
                    destinationDoor = dest.homeEntranceOut;
                }

                appendTravelStep(step, "Use %s in %s -> %s (%s)",
                    sourceDoor ? sourceDoor : "entrance",
                    stageName(trace.sourceStage),
                    destinationStage ? destinationStage : "destination",
                    destinationDoor ? destinationDoor : "arrival");
                return;
            }
        }

        if (trace.kind == StageTrace_Painting) {
            if (isValidStage(trace.sourceStage))
                appendStageTravel(step, state, trace.sourceStage, depth + 1);

            appendTravelStep(step, "Use painting %s -> %s",
                nodeName(trace.sourceEntrance),
                nodeName(trace.destEntrance));
            return;
        }

        if (trace.kind == StageTrace_Odyssey) {
            appendTravelStep(step, "Fly to %s with the Odyssey (%s)", kingdomName(kingdom), stageName(stage));
            return;
        }

        if (trace.kind == StageTrace_Inferred) {
            appendTravelStep(step, "Reach %s; stage access was inferred from current kingdom/graph data", stageName(stage));
            return;
        }

        if (state.accessibleStages[stage])
            appendTravelStep(step, "Reach %s; no route provenance was recorded", stageName(stage));
    }

    static void appendNodeTravel(PathStep& step, LogicState& state, s32 location) {
        if (!isValidMoon(location))
            return;

        const LD::MoonAccess& moon = LD::sMoons[location];
        if (!isValidStage(moon.stageIndex))
            return;

        if (!LD::sStages[moon.stageIndex].hasRoutes) {
            appendTravelStep(step, "Local access in %s is treated as free; no authored routes for this area yet", stageName(moon.stageIndex));
            return;
        }

        if (moon.fromNode < 0) {
            appendTravelStep(step, "Moon has no authored graph node; local access in %s is treated as free", stageName(moon.stageIndex));
            return;
        }

        bool reachableNodes[LD::sNodeCount];
        NodeTrace nodeTraces[LD::sNodeCount];
        expandReachabilityFixedPoint(state, reachableNodes, nodeTraces);

        if (!reachableNodes[moon.fromNode]) {
            appendTravelStep(step, "Moon node %s was accepted by inferred access, but no node route was recorded",
                LD::sNodes[moon.fromNode].nodeId ? LD::sNodes[moon.fromNode].nodeId : "unknown node");
            return;
        }

        s32 startNode = moon.fromNode;
        s32 edgeCount = 0;
        while (startNode >= 0 && startNode < LD::sNodeCount && nodeTraces[startNode].previousNode >= 0 && edgeCount < 16) {
            startNode = nodeTraces[startNode].previousNode;
            edgeCount++;
        }

        if (edgeCount > 0) {
            appendTravelStep(step, "Follow authored route from %s to %s",
                LD::sNodes[startNode].nodeId ? LD::sNodes[startNode].nodeId : "start node",
                LD::sNodes[moon.fromNode].nodeId ? LD::sNodes[moon.fromNode].nodeId : "moon node");
            return;
        }

        const NodeSeedKind seedKind = nodeTraces[moon.fromNode].seedKind;
        if (seedKind == NodeSeed_StageStart)
            appendTravelStep(step, "Reach moon node %s from the area start", LD::sNodes[moon.fromNode].nodeId);
        else if (seedKind == NodeSeed_Arrival)
            appendTravelStep(step, "Reach moon node %s from an entrance or return side", LD::sNodes[moon.fromNode].nodeId);
        else if (seedKind == NodeSeed_FreeStage)
            appendTravelStep(step, "Reach moon node %s through free local access", LD::sNodes[moon.fromNode].nodeId);
        else
            appendTravelStep(step, "Reach moon node %s", LD::sNodes[moon.fromNode].nodeId);
    }

    static void buildTravelForLocation(LogicState& state, s32 location, PathStep& step) {
        if (!isValidMoon(location))
            return;

        const LD::MoonAccess& moon = LD::sMoons[location];
        appendStageTravel(step, state, moon.stageIndex, 0);
        appendNodeTravel(step, state, location);
    }

    // Live progress for the generation UI: real placements committed vs pool size.
    // The total is set per attempt; simulation probes never touch the counter
    // because they bypass recordPlacement.
    static s32 sProgressPlacedCount = 0;
    static s32 sProgressTotalCount = 0;

    static void reportPlacementProgress() {
        if (sProgressTotalCount <= 0)
            return;
        char progressStatus[64];
        nn::util::SNPrintf(progressStatus, sizeof(progressStatus),
            "Placing moons (%d of %d)", sProgressPlacedCount, sProgressTotalCount);
        SeedGenerator::reportProgress(
            46 + (sProgressPlacedCount * 30) / sProgressTotalCount, progressStatus);
    }

#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
    // Which locations supplied each kingdom's moon requirement during
    // CONSTRUCTION. The forward replay reports only a count ("moons 8/12"), so
    // without this the difference between what construction banked and what the
    // replay could collect has to be inferred - and inferring it is how three
    // separate wrong theories got written down before the real cause turned up.
    // Recorded in recordPlacement, which is construction-only: collectLocation
    // also runs for the validator, the tracker and the spoiler replays, and
    // recording there would mix all four together.
    //
    // The phase and tier are the point of the whole thing. "Banked at
    // SandWorldHomeStage" is ambiguous between "the Lake tier loop needed it and
    // believed it was reachable" and "a later tail phase scattered filler there";
    // those are completely different bugs and only the phase distinguishes them.
    // runningCount is the requirement counter AFTER this credit, so the moment a
    // kingdom's tier loop believed itself satisfied is visible directly.
    enum DiagPhase : s16
    {
        DiagPhase_Setup = 0,
        DiagPhase_TierFill,
        DiagPhase_ProgressTail,
        DiagPhase_EvenFill,
    };

    static const char* diagPhaseName(s16 phase) {
        switch (phase) {
        case DiagPhase_TierFill:
            return "tier";
        case DiagPhase_ProgressTail:
            return "tail";
        case DiagPhase_EvenFill:
            return "filler";
        default:
            return "setup";
        }
    }

    struct CreditRecord {
        s16 location;
        s16 phase;
        s16 tier;
        s16 value; // moonValue(item): a multi counts for three
        s16 runningCount; // requirement counter after this credit landed
    };

    static constexpr s32 kMaxCreditRecord = 64;
    static CreditRecord sKingdomCredit[LD::sKingdomCount][kMaxCreditRecord];
    static s16 sKingdomCreditCount[LD::sKingdomCount];
    static s16 sDiagPhase = DiagPhase_Setup;
    static s16 sDiagTier = -1;

    static void resetKingdomCreditLedger() {
        sDiagPhase = DiagPhase_Setup;
        sDiagTier = -1;
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            sKingdomCreditCount[kingdom] = 0;
            for (s32 i = 0; i < kMaxCreditRecord; i++)
                sKingdomCredit[kingdom][i] = { -1, DiagPhase_Setup, -1, 0, 0 };
        }
    }

    static void noteKingdomCredit(const LogicState& state, s32 location, s32 item) {
        const s32 kingdom = assignedKingdomOf(item);
        if (!isValidKingdom(kingdom))
            return;
        s16& count = sKingdomCreditCount[kingdom];
        if (count < kMaxCreditRecord) {
            sKingdomCredit[kingdom][count] = {
                (s16)location, sDiagPhase, sDiagTier, (s16)moonValue(item),
                state.moonCounts[kingdom]
            };
        }
        count++; // keep counting past the cap so the log can say it truncated
    }

    // Says what the tier loop thought it had achieved, at the sphere it stopped.
    // A kingdom that closes far below its requirement, or at a sphere nowhere
    // near its tier, is the disagreement rather than a symptom of it.
    static void logKingdomClosed(const LogicState& state, s32 kingdom, s32 sphere) {
        Randomizer::logPush(
            "tier-close %s: tier=%d sphere=%d moons=%d/%d",
            WorldMeta::getDisplayNameForWorldId(kingdom), (s32)sDiagTier, sphere,
            isValidKingdom(kingdom) ? (s32)state.moonCounts[kingdom] : -1,
            requirementForKingdom(kingdom));
    }
#endif

    static void recordPlacement(
        LogicState& state,
        bool* unfilled,
        bool* unplaced,
        s32 location,
        s32 item,
        s32 sphere,
        const char* reason) {
        sPlacementItemForLocation[location] = item;
        unfilled[location] = false;
        unplaced[item] = false;
        sProgressPlacedCount++;
        reportPlacementProgress();

        PathStep* step = nullptr;
        if (sPathStepCount < LD::sMoonCount) {
            step = &sPathSteps[sPathStepCount++];
            step->location = sMoonEntries[location];
            step->item = sMoonEntries[item];
            step->reason = reason;
            step->sphere = sphere;
            step->unlockedTokenCount = 0;
            step->travelStepCount = 0;
            step->locationStageIdx = LD::sMoons[location].stageIndex;
            step->locationKingdomIdx = LD::sMoons[location].kingdom;
            step->hostKingdomIdx = LD::sMoons[location].kingdom;
            step->scenarioAdvanceKingdom = -1;
            step->scenarioAdvanceTo = -1;
            step->scenarioAdvanceIsMoonRock = false;
            step->isStoryStep = isStoryMoon(location);
            step->grantedWorldPeace = false;
            for (s32 i = 0; i < kMaxUnlockTokensPerMoon; i++)
                step->unlockedTokens[i] = nullptr;
            buildTravelForLocation(state, location, *step);
        }

        const s32 locationKingdom = LD::sMoons[location].kingdom;
        const s16 scenarioBefore = isValidKingdom(locationKingdom)
            ? state.kingdomScenarios[locationKingdom]
            : -1;
        s16 unlockedTokens[kMaxUnlockTokensPerMoon];
        s32 unlockedCount = collectLocation(state, location, unlockedTokens, kMaxUnlockTokensPerMoon);
        unlockedCount += refreshReachableStateEvents(
            state,
            unlockedTokens + unlockedCount,
            kMaxUnlockTokensPerMoon - unlockedCount);
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        // Deliberately after collectLocation: the running requirement counter is
        // only meaningful once this credit has actually been granted.
        noteKingdomCredit(state, location, item);
#endif

        if (step) {
            step->unlockedTokenCount = unlockedCount;
            for (s32 i = 0; i < unlockedCount && i < kMaxUnlockTokensPerMoon; i++) {
                step->unlockedTokens[i] = LD::sTokens[unlockedTokens[i]].displayName;
                if (isValidKingdom(locationKingdom)
                    && unlockedTokens[i] == LD::sWorldPeaceTokenByKingdom[locationKingdom])
                    step->grantedWorldPeace = true;
            }
            if (isValidKingdom(locationKingdom)
                && state.kingdomScenarios[locationKingdom] > scenarioBefore) {
                step->scenarioAdvanceKingdom = (s16)locationKingdom;
                step->scenarioAdvanceTo = state.kingdomScenarios[locationKingdom];
            }
        }
    }

    static s32 updateFrontierSpheres(
        LogicState& state,
        const bool* unfilled,
        s32& currentSphere,
        bool& frontierInitialized,
        s32* reachable,
        bool* outReachableNodes = nullptr) {
        const s32 reachableCount
            = reachableLocations(state, unfilled, reachable, outReachableNodes);
        bool foundNew = false;
        for (s32 i = 0; i < reachableCount; i++) {
            if (sLocationSphere[reachable[i]] < 0) {
                foundNew = true;
                break;
            }
        }

        if (foundNew) {
            if (frontierInitialized)
                currentSphere++;
            for (s32 i = 0; i < reachableCount; i++) {
                const s32 location = reachable[i];
                if (sLocationSphere[location] < 0)
                    sLocationSphere[location] = (s16)currentSphere;
            }
        }
        if (reachableCount > 0)
            frontierInitialized = true;
        return reachableCount;
    }

    static s32 countCompatibleReachableLocations(
        const s32* reachable,
        s32 reachableCount,
        const bool* unplaced) {
        bool hasSingleItem = false;
        bool hasMultiItem = false;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!unplaced[item])
                continue;
            if (isMultiMoon(item))
                hasMultiItem = true;
            else
                hasSingleItem = true;
        }

        if (Randomizer::isTripleMoonsAnywhereEnabled())
            return hasSingleItem || hasMultiItem ? reachableCount : 0;

        s32 count = 0;
        for (s32 i = 0; i < reachableCount; i++) {
            if (isMultiSlot(reachable[i]) ? hasMultiItem : hasSingleItem)
                count++;
        }
        return count;
    }

    static s32 countUnplacedRequirements(const LogicState& state, s32 kingdom) {
        const s32 remaining = requirementForKingdom(kingdom) - state.moonCounts[kingdom];
        return remaining > 0 ? remaining : 0;
    }

    static s32 pickRandomCompatibleItem(
        const bool* unplaced,
        const s32* reachable,
        s32 reachableCount,
        Randomizer::Rng& rng,
        bool progressionOnly) {
        if (reachableCount <= 0)
            return -1;

        const bool tripleMoonsAnywhere = Randomizer::isTripleMoonsAnywhereEnabled();
        bool hasSingleSlot = false;
        bool hasMultiSlot = false;
        for (s32 i = 0; i < reachableCount; i++) {
            if (isMultiSlot(reachable[i]))
                hasMultiSlot = true;
            else
                hasSingleSlot = true;
        }

        s32 candidates[LD::sMoonCount];
        s32 count = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!unplaced[item] || (progressionOnly && !itemHasProgressUnlock(item)))
                continue;
            if (tripleMoonsAnywhere
                || (isMultiMoon(item) && hasMultiSlot)
                || (!isMultiMoon(item) && hasSingleSlot))
                candidates[count++] = item;
        }
        if (count <= 0)
            return -1;
        return candidates[rng.nextModulo((u32)count)];
    }

    // How many unlock carriers this sphere has already taken, and which sphere
    // that count belongs to. Late/Back-Loaded pacing caps carriers per sphere,
    // which is what actually stretches the moveset out - a weight alone still
    // lets a lucky run of draws front-load it. Reset with the placement state.
    static s32 sPacingCarrierSphere = -1;
    static s32 sPacingCarriersThisSphere = 0;

    // Whether pacing still allows a discretionary unlock carrier right now.
    // FrontLoaded/Early/Standard never cap; Late allows one per sphere and
    // Back-Loaded one per two spheres.
    static bool pacingAllowsCarrier(s32 currentSphere) {
        static constexpr s32 kSpheresPerCarrier[Randomizer::cSeedAxisLevelCount]
            = { 0, 0, 0, 1, 2 };
        const s32 spheres = kSpheresPerCarrier[unlockPacingLevel()];
        if (spheres <= 0)
            return true;
        if (sPacingCarrierSphere < 0)
            return true;
        // One carrier per `spheres` spheres: block until the sphere counter has
        // moved on far enough from the last one taken.
        return currentSphere - sPacingCarrierSphere >= spheres;
    }

    static void notePacingCarrierPlaced(s32 item, s32 currentSphere) {
        if (!isValidMoon(item) || sMoonUnlockTokenCounts[item] <= 0)
            return;
        sPacingCarrierSphere = currentSphere;
        sPacingCarriersThisSphere++;
    }

    // Unlock Pacing only biases discretionary high-buffer draws. Required native
    // moons and reachability unlocks keep their safety-first paths below, so no
    // pacing level can ever make a seed unbeatable - it only changes when the
    // moveset shows up along a route that was already proven.
    static s32 pickHighBufferCompatibleItem(
        const bool* unplaced,
        const s32* reachable,
        s32 reachableCount,
        Randomizer::Rng& rng,
        s32 currentSphere) {
        const s32 pacing = unlockPacingLevel();
        if (pacing == 2)
            return pickRandomCompatibleItem(
                unplaced, reachable, reachableCount, rng, false);

        if (reachableCount <= 0)
            return -1;

        const bool tripleMoonsAnywhere = Randomizer::isTripleMoonsAnywhereEnabled();
        bool hasSingleSlot = false;
        bool hasMultiSlot = false;
        for (s32 i = 0; i < reachableCount; i++) {
            if (isMultiSlot(reachable[i]))
                hasMultiSlot = true;
            else
                hasSingleSlot = true;
        }

        // Relative weight of an unlock carrier against an ordinary moon, as
        // num:den. Front-Loaded pulls the moveset into the first few spheres;
        // Back-Loaded leaves most of it for the last third.
        static constexpr u32 kCarrierNum[Randomizer::cSeedAxisLevelCount] = { 8, 3, 1, 1, 1 };
        static constexpr u32 kCarrierDen[Randomizer::cSeedAxisLevelCount] = { 1, 1, 1, 3, 8 };
        const u32 carrierWeight = kCarrierNum[pacing];
        const u32 plainWeight = kCarrierDen[pacing];
        const bool allowCarrier = pacingAllowsCarrier(currentSphere);

        s32 candidates[LD::sMoonCount];
        u32 weights[LD::sMoonCount];
        s32 count = 0;
        u64 totalWeight = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!unplaced[item])
                continue;
            const bool hasCompatibleSlot = tripleMoonsAnywhere
                || (isMultiMoon(item) ? hasMultiSlot : hasSingleSlot);
            if (!hasCompatibleSlot)
                continue;
            const bool unlockCarrier = sMoonUnlockTokenCounts[item] > 0;
            if (unlockCarrier && !allowCarrier)
                continue;
            const u32 weight = unlockCarrier ? carrierWeight : plainWeight;
            candidates[count] = item;
            weights[count] = weight;
            totalWeight += weight;
            count++;
        }

        if (count > 0 && totalWeight > 0) {
            u64 pick = nextModulo64(rng, totalWeight);
            for (s32 i = 0; i < count; i++) {
                if (pick < weights[i]) {
                    notePacingCarrierPlaced(candidates[i], currentSphere);
                    return candidates[i];
                }
                pick -= weights[i];
            }
        }
        // The cap or the slot filter left nothing; never stall the fill over a
        // pacing preference.
        return pickRandomCompatibleItem(
            unplaced, reachable, reachableCount, rng, false);
    }

    // Select one native moon from an optimal single/multi combination for the
    // remaining gate value. This preserves the old solver's multi-moon capacity
    // reasoning while placing only one item per buffer-machine transition.
    static s32 pickRequiredMoonForKingdom(
        const LogicState& state,
        s32 kingdom,
        const bool* unplaced,
        const s32* reachable,
        s32 reachableCount,
        Randomizer::Rng& rng) {
        const s32 requiredRemaining = countUnplacedRequirements(state, kingdom);
        if (requiredRemaining <= 0)
            return -1;

        const bool tripleMoonsAnywhere = Randomizer::isTripleMoonsAnywhereEnabled();
        s32 singleSlots = 0;
        s32 multiSlots = 0;
        for (s32 i = 0; i < reachableCount; i++) {
            if (isMultiSlot(reachable[i]))
                multiSlots++;
            else
                singleSlots++;
        }

        s32 singleItems[LD::sMoonCount];
        s32 multiItems[LD::sMoonCount];
        s32 singleItemCount = 0;
        s32 multiItemCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!unplaced[item] || assignedKingdomOf(item) != kingdom)
                continue;
            if (isMultiMoon(item))
                multiItems[multiItemCount++] = item;
            else
                singleItems[singleItemCount++] = item;
        }
        shuffle(singleItems, singleItemCount, rng);
        shuffle(multiItems, multiItemCount, rng);

        const s32 totalSlots = reachableCount;
        const s32 availableMultiSlots = tripleMoonsAnywhere ? totalSlots : multiSlots;
        const s32 maxMulti = availableMultiSlots < multiItemCount
            ? availableMultiSlots
            : multiItemCount;
        const s32 maxSingle = singleSlots < singleItemCount ? singleSlots : singleItemCount;
        s32 bestMulti = -1;
        s32 bestSingle = -1;
        s32 bestSlots = 0x7FFFFFFF;
        s32 bestOvershoot = 0x7FFFFFFF;
        for (s32 multi = 0; multi <= maxMulti; multi++) {
            const s32 remainingSlots = totalSlots - multi;
            const s32 allowedSingles = tripleMoonsAnywhere
                ? (remainingSlots < singleItemCount ? remainingSlots : singleItemCount)
                : maxSingle;
            for (s32 single = 0; single <= allowedSingles; single++) {
                const s32 value = multi * 3 + single;
                if (value < requiredRemaining)
                    continue;
                const s32 slots = multi + single;
                const s32 overshoot = value - requiredRemaining;
                if (slots < bestSlots
                    || (slots == bestSlots && overshoot < bestOvershoot)) {
                    bestMulti = multi;
                    bestSingle = single;
                    bestSlots = slots;
                    bestOvershoot = overshoot;
                }
            }
        }
        if (bestMulti < 0)
            return -1;

        const s32 plannedSlots = bestMulti + bestSingle;
        const bool chooseMulti = bestMulti > 0
            && (bestSingle <= 0
                || (s32)rng.nextModulo((u32)plannedSlots) < bestMulti);
        if (chooseMulti)
            return multiItems[rng.nextModulo((u32)multiItemCount)];
        if (singleItemCount > 0)
            return singleItems[rng.nextModulo((u32)singleItemCount)];
        return -1;
    }

    static bool isKingdomStoryReachable(
        const LogicState& state,
        s32 kingdom,
        const bool* unfilled) {
        if (!requiresRouteStoryCompletion(kingdom)
            || state.storyCounts[kingdom] >= LD::sStoryTotalsByKingdom[kingdom])
            return true;

        LogicState sim = state;
        bool consider[LD::sMoonCount];
        for (s32 location = 0; location < LD::sMoonCount; location++) {
            consider[location] = !sim.collectedLocations[location]
                && (unfilled[location] || sPlacementItemForLocation[location] >= 0);
        }

        bool changed = true;
        while (changed) {
            changed = false;
            s32 reachable[LD::sMoonCount];
            const s32 count = reachableLocations(sim, consider, reachable);
            for (s32 i = 0; i < count; i++) {
                const s32 location = reachable[i];
                collectLocation(sim, location, nullptr, 0);
                consider[location] = false;
                changed = true;
            }
            refreshReachableStateEvents(sim, nullptr, 0);
        }
        return sim.storyCounts[kingdom] >= LD::sStoryTotalsByKingdom[kingdom];
    }

    static bool isStoryDriverForKingdom(
        const LogicState& state, s32 location, s32 kingdom) {
        if (!isValidMoon(location) || LD::sMoons[location].kingdom != kingdom)
            return false;
        const LD::MoonAccess& moon = LD::sMoons[location];
        if (isStoryMoon(location) && isNextStoryMoon(state, location))
            return true;
        if (isVanillaMultiMoon(location)
            && moon.scenarioMoveTo > state.kingdomScenarios[kingdom])
            return true;
        return moon.storyMoveTo > state.kingdomScenarios[kingdom];
    }

    // Once story reachability is proven, assign real items to the physical story
    // drivers and collect them into the construction state. This prevents later
    // logic from relying on an abstract story completion while putting a required
    // unlock into a location the proof already consumed.
    static bool materializeKingdomStory(
        LogicState& state,
        s32 kingdom,
        bool* unfilled,
        bool* unplaced,
        s32& currentSphere,
        bool& frontierInitialized,
        Randomizer::Rng& rng) {
        while (state.storyCounts[kingdom] < LD::sStoryTotalsByKingdom[kingdom]) {
            s32 reachable[LD::sMoonCount];
            const s32 reachableCount = updateFrontierSpheres(
                state, unfilled, currentSphere, frontierInitialized, reachable);
            s32 drivers[LD::sMoonCount];
            s32 driverCount = 0;
            for (s32 i = 0; i < reachableCount; i++) {
                if (isStoryDriverForKingdom(state, reachable[i], kingdom))
                    drivers[driverCount++] = reachable[i];
            }
            if (driverCount <= 0)
                return false;

            const s32 location = drivers[rng.nextModulo((u32)driverCount)];
            s32 compatible[1] = { location };
            const s32 item = pickRandomCompatibleItem(
                unplaced, compatible, 1, rng, false);
            if (item < 0)
                return false;
            recordPlacement(
                state, unfilled, unplaced, location, item,
                sLocationSphere[location], "story progression");
        }
        return true;
    }

    static s32 countReachableTopology(const LogicState& state) {
        s32 count = 0;
        bool rootedStages[LD::sStageCount];
        computeRootedStageMap(state, rootedStages);
        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            if (!state.accessibleKingdoms[i])
                continue;
            const s32 stage = LD::sMainStageByKingdom[i];
            count += isValidStage(stage) && rootedStages[stage] ? 1 : 0;
        }
        for (s32 i = 0; i < LD::sStageCount; i++)
            count += state.accessibleStages[i] && rootedStages[i] ? 1 : 0;
        for (s32 i = 0; i < LD::sNodeCount; i++) {
            if (!state.accessibleNodes[i])
                continue;
            const s32 stage = LD::sNodes[i].stage;
            count += isValidStage(stage) && rootedStages[stage] ? 1 : 0;
        }
        return count;
    }

    // A move that opens nothing at all.
    static constexpr s32 kNoExpansion = -1;

    // Returns how many still-unplaced locations this bundle would expose, or
    // kNoExpansion when it opens nothing.
    static s32 simulateUnlockBundle(
        const LogicState& baseState,
        const bool* unfilled,
        const s32* items,
        s32 itemCount,
        s32 kingdom,
        s32 beforeTopology,
        s32 beforeStoryCount,
        s32 beforeReachableCount,
        Randomizer::Rng& rng,
        s32* outLocations) {
        LogicState sim = baseState;
        bool simUnfilled[LD::sMoonCount];
        std::memcpy(simUnfilled, unfilled, sizeof(simUnfilled));

        s32 assigned = 0;
        for (; assigned < itemCount; assigned++) {
            s32 reachable[LD::sMoonCount];
            const s32 reachableCount = reachableLocations(sim, simUnfilled, reachable);
            const s32 location = pickDepthWeightedCompatibleLocation(
                items[assigned], reachable, reachableCount, rng, kingdom);
            if (location < 0)
                break;

            outLocations[assigned] = location;
            sPlacementItemForLocation[location] = items[assigned];
            simUnfilled[location] = false;
            collectLocation(sim, location, nullptr, 0);
            refreshReachableStateEvents(sim, nullptr, 0);
        }

        // How much this bundle opens, not merely whether it opens anything.
        // The magnitude was always computed here and thrown away, which left
        // the fill taking the first bundle that helped at all rather than the
        // one that helped most - and gave entrance wiring nothing to be
        // compared against.
        s32 gain = kNoExpansion;
        if (assigned == itemCount) {
            const s32 afterTopology = countReachableTopology(sim);
            s32 afterReachable[LD::sMoonCount];
            const s32 afterReachableCount = reachableLocations(sim, simUnfilled, afterReachable);
            const s32 locationGain = afterReachableCount - beforeReachableCount;
            const s32 topologyGain = afterTopology - beforeTopology;
            const bool storyGain = isValidKingdom(kingdom)
                && sim.storyCounts[kingdom] > beforeStoryCount;
            if (locationGain > 0 || topologyGain > 0 || storyGain) {
                // Locations are the currency the buffer is measured in, so they
                // lead. Topology and story still count when they open nothing
                // immediately - a bundle that only widens the graph is exactly
                // what breaks a deadlock a moon count cannot.
                gain = locationGain > 0 ? locationGain : 0;
                if (topologyGain > 0)
                    gain += 1;
                if (storyGain)
                    gain += 1;
            }
        }

        for (s32 i = 0; i < assigned; i++)
            sPlacementItemForLocation[outLocations[i]] = -1;
        return gain;
    }

    static void markMissingRequirementTokens(
        const LogicState& state, u16 requirementIndex, bool* relevantTokens, s16 stage) {
        if (requirementIndex >= LD::sRequirementCount)
            return;
        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode != LD::RequirementMode_Custom)
            return;
        for (s32 altIndex = 0; altIndex < requirement.altCount; altIndex++) {
            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIndex];
            if (!requirementAlternativeEnabled(stage, alt))
                continue;
            for (s32 offset = 0; offset < alt.tokenCount; offset++) {
                const s32 token = EnemySwapRando::remapLogicToken(
                    stage, LD::sReqTokens[alt.tokenOffset + offset]);
                if (token >= 0 && token < LD::sTokenCount && !hasToken(state, token))
                    relevantTokens[token] = true;
            }
        }
    }

    static void markMissingRequirementTokensForScenario(
        const LogicState& state,
        u16 requirementIndex,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario,
        bool* relevantTokens,
        s16 stage) {
        if (requirementIndex >= LD::sRequirementCount)
            return;
        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode != LD::RequirementMode_Custom)
            return;
        for (s32 altIndex = 0; altIndex < requirement.altCount; altIndex++) {
            if (!optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount, altIndex, scenario))
                continue;
            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIndex];
            if (!requirementAlternativeEnabled(stage, alt))
                continue;
            for (s32 offset = 0; offset < alt.tokenCount; offset++) {
                const s32 token = EnemySwapRando::remapLogicToken(
                    stage, LD::sReqTokens[alt.tokenOffset + offset]);
                if (token >= 0 && token < LD::sTokenCount && !hasToken(state, token))
                    relevantTokens[token] = true;
            }
        }
    }

    // Route capture requirements use the same edge-aware gate interpretation
    // as reachability. This keeps progression-item selection, tracker guidance,
    // and the actual route traversal from disagreeing about which replacement
    // capture token can unlock an edge.
    static void markMissingRouteRequirementTokens(
        const LogicState& state,
        const LD::RouteEdge& edge,
        s16 scenario,
        bool* relevantTokens,
        bool* difficultTokens,
        const bool* reachableNodes) {
        if (edge.requirement >= LD::sRequirementCount)
            return;
        const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
        if (requirement.mode != LD::RequirementMode_Custom)
            return;
        const s16 stage = routeEdgeStage(edge);
        // The same reachable set the caller proved this edge's endpoints
        // against: an option still waiting on an unreachable node cannot stand
        // in for the casual ones, so masking them here would hide the token that
        // actually unlocks this edge from progression-item selection.
        const bool skipCasualOptions = maskingCasualOptions()
            && hasEligibleDifficultOption(
                edge.requirement, edge.difficultyOffset, edge.difficultyCount,
                edge.optionScenarioOffset, edge.optionScenarioCount, scenario,
                reachableNodes);
        for (s32 altIndex = 0;
             altIndex < requirement.altCount; altIndex++) {
            if (!routeOptionScenarioAllowed(
                    edge, altIndex, scenario))
                continue;
            const LD::RouteDifficulty optionDifficulty = optionDifficultyAt(edge.difficultyOffset, edge.difficultyCount, altIndex);
            if (!sNoLogicFill
                && (!routeDifficultyAllowed(optionDifficulty)
                    || (skipCasualOptions
                        && optionDifficulty == LD::RouteDifficulty_Casual)))
                continue;
            const bool difficultOption = optionDifficulty != LD::RouteDifficulty_Casual;
            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIndex];
            for (s32 offset = 0;
                 offset < alt.tokenCount; offset++) {
                const s32 sourceToken = LD::sReqTokens[alt.tokenOffset + offset];
                if (sourceToken >= 0
                    && sourceToken < LD::sTokenCount
                    && LD::sTokens[sourceToken].type
                        == LD::TokenType_Capture
                    && EnemySwapRando::hasRouteCaptureGates(
                        edge.edgeId,
                        LD::sTokens[sourceToken].key)) {
                    s16 replacements[16] = {};
                    const s32 count = EnemySwapRando::
                        getSatisfiedRouteCaptureTokens(
                            edge.edgeId,
                            LD::sTokens[sourceToken].key,
                            scenario, replacements, 16);
                    bool alreadyAvailable = false;
                    for (s32 i = 0; i < count; i++)
                        if (hasToken(state, replacements[i])) {
                            alreadyAvailable = true;
                            break;
                        }
                    if (!alreadyAvailable)
                        for (s32 i = 0; i < count; i++)
                            if (replacements[i] >= 0
                                && replacements[i]
                                    < LD::sTokenCount) {
                                relevantTokens[replacements[i]] = true;
                                if (difficultTokens && difficultOption)
                                    difficultTokens[replacements[i]] = true;
                            }
                    continue;
                }
                const s32 token = EnemySwapRando::remapLogicToken(
                    stage, sourceToken);
                if (token >= 0 && token < LD::sTokenCount
                    && !hasToken(state, token)) {
                    relevantTokens[token] = true;
                    if (difficultTokens && difficultOption)
                        difficultTokens[token] = true;
                }
            }
        }
    }

    static void markMissingMoonRequirementTokensForScenario(
        const LogicState& state,
        u16 requirementIndex,
        u16 optionScenarioOffset,
        u8 optionScenarioCount,
        s16 scenario,
        bool* relevantTokens,
        bool* difficultTokens,
        const LD::MoonAccess& moon,
        bool isStateRequirement,
        const bool* reachableNodes) {
        if (requirementIndex >= LD::sRequirementCount)
            return;
        const LD::Requirement& requirement = LD::sRequirements[requirementIndex];
        if (requirement.mode != LD::RequirementMode_Custom)
            return;

        char gateId[48] = {};
        makeMoonCaptureGateId(
            gateId, sizeof(gateId), moon, isStateRequirement);
        const u16 difficultyOffset = isStateRequirement ? 0 : moon.localDifficultyOffset;
        const u8 difficultyCount = isStateRequirement ? 0 : moon.localDifficultyCount;
        // As in markMissingRouteRequirementTokens above.
        const bool skipCasualOptions = maskingCasualOptions()
            && hasEligibleDifficultOption(
                requirementIndex, difficultyOffset, difficultyCount,
                optionScenarioOffset, optionScenarioCount, scenario,
                reachableNodes);
        for (s32 altIndex = 0; altIndex < requirement.altCount; altIndex++) {
            if (!optionScenarioAllowed(
                    optionScenarioOffset, optionScenarioCount,
                    altIndex, scenario))
                continue;
            const LD::RouteDifficulty optionDifficulty = optionDifficultyAt(difficultyOffset, difficultyCount, altIndex);
            if (!sNoLogicFill
                && (!routeDifficultyAllowed(optionDifficulty)
                    || (skipCasualOptions
                        && optionDifficulty == LD::RouteDifficulty_Casual)))
                continue;
            const bool difficultOption = optionDifficulty != LD::RouteDifficulty_Casual;
            const LD::ReqAlternative& alt = LD::sReqAlternatives[requirement.altOffset + altIndex];
            if (!requirementAlternativeEnabled(moon.stageIndex, alt))
                continue;
            for (s32 offset = 0; offset < alt.tokenCount; offset++) {
                const s32 sourceToken = LD::sReqTokens[alt.tokenOffset + offset];
                if (sourceToken >= 0 && sourceToken < LD::sTokenCount
                    && LD::sTokens[sourceToken].type
                        == LD::TokenType_Capture
                    && EnemySwapRando::hasRouteCaptureGates(
                        gateId, LD::sTokens[sourceToken].key)) {
                    s16 replacements[16] = {};
                    const s32 count = EnemySwapRando::
                        getSatisfiedRouteCaptureTokens(
                            gateId, LD::sTokens[sourceToken].key,
                            scenario, replacements, 16);
                    bool alreadyAvailable = false;
                    for (s32 i = 0; i < count; i++) {
                        if (hasToken(state, replacements[i])) {
                            alreadyAvailable = true;
                            break;
                        }
                    }
                    if (!alreadyAvailable)
                        for (s32 i = 0; i < count; i++)
                            if (replacements[i] >= 0
                                && replacements[i] < LD::sTokenCount) {
                                relevantTokens[replacements[i]] = true;
                                if (difficultTokens && difficultOption)
                                    difficultTokens[replacements[i]] = true;
                            }
                    continue;
                }
                const s32 token = EnemySwapRando::remapLogicToken(
                    moon.stageIndex, sourceToken);
                if (token >= 0 && token < LD::sTokenCount
                    && !hasToken(state, token)) {
                    relevantTokens[token] = true;
                    if (difficultTokens && difficultOption)
                        difficultTokens[token] = true;
                }
            }
        }
    }

    static bool tokenSupportsMissingToken(s32 candidate, s32 required) {
        if (candidate == required)
            return true;
        s32 token = required;
        for (s32 guard = 0; guard < LD::sTokenCount; guard++) {
            if (token < 0 || token >= LD::sTokenCount)
                return false;
            token = LD::sTokens[token].prerequisite;
            if (token == candidate)
                return true;
        }
        return false;
    }

    static bool unlockItemSupportsTokens(
        s32 item, const bool* tokens) {
        const s32 keyKingdom = moonRockKeyKingdomForItem(item);
        if (isValidKingdom(keyKingdom)) {
            const s32 moonRockToken = LD::sMoonRockTokenByKingdom[keyKingdom];
            if (moonRockToken >= 0 && moonRockToken < LD::sTokenCount
                && tokens[moonRockToken])
                return true;
        }
        for (s32 i = 0; i < sMoonUnlockTokenCounts[item]; i++) {
            const s32 candidate = sMoonUnlockTokens[item][i];
            for (s32 required = 0; required < LD::sTokenCount; required++) {
                if (tokens[required]
                    && tokenSupportsMissingToken(candidate, required))
                    return true;
            }
        }
        return false;
    }

    static bool unlockItemRelevant(s32 item, const bool* relevantTokens,
        const bool* relevantSubareaSlots) {
        if (moonUnlocksCloudPainting(item)
            || unlockItemSupportsTokens(item, relevantTokens))
            return true;

        // A Subarea Key grants no route token. Only retain one in the expensive
        // bundle search when at least one home stage containing a door it opens
        // is already accessible. A different unlock that exposes that stage is
        // useful on its own; the next search then sees the key. This avoids
        // treating all 46 fine-mode keys as interchangeable candidates in the
        // cubic bundle enumeration.
        const s32 slot = subareaKeySlotForItem(item);
        return slot >= 0 && slot < kMaxSubareaKeySlots
            && relevantSubareaSlots[slot];
    }

    // Search whole unlock bundles. This deliberately accepts pairs/triples whose
    // individual members do nothing, which is required for AND requirements and
    // prerequisite chains. Simulation collects the physical locations, so consuming
    // the slot and any story/scenario side effects are part of the usefulness test.
    static void fnvMix(u64& hash, const void* data, size_t len) {
        const u8* bytes = static_cast<const u8*>(data);
        for (size_t i = 0; i < len; i++) {
            hash ^= bytes[i];
            hash *= 1099511628211ull;
        }
    }

    // Fingerprint of everything a bundle search's outcome can depend on: the
    // reachability-relevant state plus the unlock-candidate pool. The unfilled
    // location set is deliberately excluded — it only ever shrinks between
    // searches, and fewer open slots can never turn an exhausted search into a
    // success.
    static u64 hashUnlockSearchInputs(const LogicState& state, s32 kingdom, const bool* unplaced) {
        u64 hash = 1469598103934665603ull;
        fnvMix(hash, state.tokens, sizeof(state.tokens));
        fnvMix(hash, state.accessibleKingdoms, sizeof(state.accessibleKingdoms));
        fnvMix(hash, state.accessibleStages, sizeof(state.accessibleStages));
        fnvMix(hash, state.accessibleNodes, sizeof(state.accessibleNodes));
        fnvMix(hash, state.accessibleSubEntryNear, sizeof(state.accessibleSubEntryNear));
        fnvMix(hash, state.accessibleSubEntryFar, sizeof(state.accessibleSubEntryFar));
        fnvMix(hash, state.unlockedPaintings, sizeof(state.unlockedPaintings));
        fnvMix(hash, state.cloudPaintingLinkBySlot, sizeof(state.cloudPaintingLinkBySlot));
        // collectedItems/collectedLocations/moonCounts are deliberately NOT hashed:
        // every ordinary placement mutates them, but simulations only ever touch
        // unfilled locations and unplaced items (disjoint from collected sets) and
        // nothing in the search reads moon counts — hashing them would invalidate
        // the cache on every placement, which is exactly the repetition it exists
        // to prevent. Scenario/story state IS hashed; those are the real inputs.
        fnvMix(hash, state.moonRockKeysCollected, sizeof(state.moonRockKeysCollected));
        fnvMix(hash, state.subareaKeysCollected, sizeof(state.subareaKeysCollected));
        fnvMix(hash, state.kingdomScenarios, sizeof(state.kingdomScenarios));
        fnvMix(hash, state.storyCounts, sizeof(state.storyCounts));
        fnvMix(hash, &kingdom, sizeof(kingdom));
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (unplaced[item] && itemHasProgressUnlock(item))
                fnvMix(hash, &item, sizeof(item));
        }
        return hash;
    }

    // Last failed bundle-search fingerprint per target kingdom. The buffer loop
    // re-requests a search after every ordinary moon placement; when nothing
    // reachability-relevant changed, the whole O(candidates^3) enumeration would
    // deterministically fail again, and repeated identical searches dominated
    // slow generations (10+ seconds per kingdom re-proving the same dead end).
    static u64 sUnlockSearchFailHash[LD::sKingdomCount];

    // The bundle the unlock search would place, and how much it opens. Split
    // out of placeReachabilityUnlock so entrance wiring can be weighed against
    // it on the same scale instead of only being tried after it fails.
    struct UnlockCandidate {
        s32 items[3];
        s32 locations[3];
        s32 bundleSize;
        s32 gain; // kNoExpansion when nothing was found
    };

    static bool findReachabilityUnlock(
        LogicState& state,
        bool* unfilled,
        bool* unplaced,
        Randomizer::Rng& rng,
        s32 kingdom,
        UnlockCandidate* out) {
        out->bundleSize = 0;
        out->gain = kNoExpansion;
        const u64 searchStartTick = nn::os::GetSystemTick().value;
        const u64 searchKey = hashUnlockSearchInputs(state, kingdom, unplaced);
        if (isValidKingdom(kingdom) && sUnlockSearchFailHash[kingdom] == searchKey)
            return false;
        bool relevantTokens[LD::sTokenCount] = {};
        // Tokens missing specifically on harder-than-casual alternatives; under
        // Prioritize Difficult Movement, candidates serving these expand first.
        bool difficultTokens[LD::sTokenCount] = {};
        bool reachableNodes[LD::sNodeCount];
        computeReachableNodes(state, reachableNodes);

        bool relevantSubareaSlots[kMaxSubareaKeySlots] = {};
        if (Randomizer::isSubareaUnlockEnabled()) {
            ensureLinkStageIndexes();
            for (s32 link = 0; link < EntranceDB::sLinkCount; link++) {
                if (!SubareaKeyDB::isGatedLink(link))
                    continue;
                const s32 homeStage = sLinkHomeStageIndex[link];
                if (!isValidStage(homeStage) || !state.accessibleStages[homeStage])
                    continue;
                const s32 slot = SubareaKeyDB::findKeySlot(
                    SubareaKeyDB::kingdomForLink(link),
                    SubareaKeyDB::typeForLink(link));
                if (slot >= 0 && slot < kMaxSubareaKeySlots)
                    relevantSubareaSlots[slot] = true;
            }
        }
        for (s32 edgeIndex = 0; edgeIndex < LD::sRouteEdgeCount; edgeIndex++) {
            const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
            const s32 stage = edge.fromNode >= 0 && edge.fromNode < LD::sNodeCount
                ? LD::sNodes[edge.fromNode].stage
                : -1;
            const s32 edgeKingdom = isValidStage(stage) ? LD::sStages[stage].kingdom : -1;
            const s16 currentScenario = isValidKingdom(edgeKingdom)
                ? state.kingdomScenarios[edgeKingdom]
                : -1;
            if (edge.scenarioMask != 0
                && isValidKingdom(edgeKingdom)
                && !scenarioMaskContains(edge.scenarioMask, currentScenario))
                continue;
            if (!anyRequirementOptionScenarioAllowed(
                    edge.requirement,
                    edge.optionScenarioOffset,
                    edge.optionScenarioCount,
                    currentScenario))
                continue;
            if (edge.fromNode >= 0 && edge.fromNode < LD::sNodeCount
                && edge.toNode >= 0 && edge.toNode < LD::sNodeCount
                && reachableNodes[edge.fromNode] && !reachableNodes[edge.toNode])
                markMissingRouteRequirementTokens(
                    state, edge, currentScenario,
                    relevantTokens, difficultTokens, reachableNodes);
            if (edge.bidirectional
                && edge.toNode >= 0 && edge.toNode < LD::sNodeCount
                && edge.fromNode >= 0 && edge.fromNode < LD::sNodeCount
                && reachableNodes[edge.toNode] && !reachableNodes[edge.fromNode])
                markMissingRouteRequirementTokens(
                    state, edge, currentScenario,
                    relevantTokens, difficultTokens, reachableNodes);
        }
        for (s32 location = 0; location < LD::sMoonCount; location++) {
            if (!unfilled[location])
                continue;
            const LD::MoonAccess& moon = LD::sMoons[location];
            if (!isValidStage(moon.stageIndex) || !state.accessibleStages[moon.stageIndex])
                continue;
            const s16 currentScenario = isValidKingdom(moon.kingdom)
                ? state.kingdomScenarios[moon.kingdom]
                : -1;
            markMissingMoonRequirementTokensForScenario(
                state,
                moon.stateRequirement,
                0,
                0,
                currentScenario,
                relevantTokens,
                difficultTokens,
                moon,
                true,
                reachableNodes);
            markMissingMoonRequirementTokensForScenario(
                state,
                moon.localRequirement,
                moon.localOptionScenarioOffset,
                moon.localOptionScenarioCount,
                currentScenario,
                relevantTokens,
                difficultTokens,
                moon,
                false,
                reachableNodes);
        }

        s32 candidates[LD::sMoonCount];
        s32 candidateCount = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (unplaced[item] && itemHasProgressUnlock(item)
                && unlockItemRelevant(item, relevantTokens, relevantSubareaSlots))
                candidates[candidateCount++] = item;
        }
        if (candidateCount <= 0) {
            if (isValidKingdom(kingdom))
                sUnlockSearchFailHash[kingdom] = searchKey;
            return false;
        }
        shuffle(candidates, candidateCount, rng);
        if (difficultFillPrefersHard() || difficultFillPrefersCasual()) {
            // Order the expansion by which movement tier it opens. Hard/Brutal
            // try candidates supplying a token some Advanced/Expert alternative
            // is missing FIRST; Relaxed/Easy try them LAST, so the route grows
            // through methods the player can execute casually. Stable within the
            // shuffle either way, so the ordering stays deterministic for a
            // seed/attempt.
            const bool hardFirst = difficultFillPrefersHard();
            static s32 sOrderedCandidates[LD::sMoonCount];
            s32 orderedCount = 0;
            for (s32 i = 0; i < candidateCount; i++)
                if (unlockItemSupportsTokens(candidates[i], difficultTokens) == hardFirst)
                    sOrderedCandidates[orderedCount++] = candidates[i];
            for (s32 i = 0; i < candidateCount; i++)
                if (unlockItemSupportsTokens(candidates[i], difficultTokens) != hardFirst)
                    sOrderedCandidates[orderedCount++] = candidates[i];
            std::memcpy(candidates, sOrderedCandidates,
                orderedCount * sizeof(candidates[0]));
        }

        const s32 beforeTopology = countReachableTopology(state);
        const s32 beforeStoryCount = isValidKingdom(kingdom)
            ? state.storyCounts[kingdom]
            : 0;
        // This reachable frontier depends only on the immutable base state and
        // unfilled set for this entire bundle search.  Recomputing it inside every
        // candidate simulation dominated slow seeds (tens of thousands of identical
        // fixed-point traversals on a single successful attempt).
        s32 beforeReachable[LD::sMoonCount];
        LogicState beforeState = state;
        const s32 beforeReachableCount = reachableLocations(
            beforeState, unfilled, beforeReachable);
        s32 searchNodesThisCall = 0;

        for (s32 bundleSize = 1; bundleSize <= 3; bundleSize++) {
            for (s32 a = 0; a < candidateCount; a++) {
                const s32 bStart = bundleSize >= 2 ? a + 1 : a;
                const s32 bEnd = bundleSize >= 2 ? candidateCount : a + 1;
                for (s32 b = bStart; b < bEnd; b++) {
                    const s32 cStart = bundleSize >= 3 ? b + 1 : b;
                    const s32 cEnd = bundleSize >= 3 ? candidateCount : b + 1;
                    for (s32 c = cStart; c < cEnd; c++) {
                        sSearchNodes++;
                        if (++searchNodesThisCall > kMaxSearchNodes) {
                            Randomizer::logPush(
                                "logic unlock-search kingdom=%s candidates=%d nodes=%d result=budget ms=%u",
                                WorldMeta::getDisplayNameForWorldId(kingdom),
                                candidateCount, searchNodesThisCall,
                                elapsedMillisSince(searchStartTick));
                            // Deliberately NOT a failure. This is a COMPUTE cap,
                            // not a proof that no bundle exists: the loop is
                            // C(n,1)+C(n,2)+C(n,3) simulations, so it runs out
                            // somewhere past ~38 candidates on nothing worse than
                            // a big kingdom. Setting a failure code here made the
                            // caller in runBufferForKingdom reject the ATTEMPT -
                            // turning "this search got expensive" into "this seed
                            // is impossible", which is exactly the class of
                            // rejection the assumed fill is supposed to make
                            // unreachable. Answering "no bundle found" instead
                            // lets the caller fall through to placing a required
                            // moon, and a genuine dead end still sets its own code.
                            //
                            // Memoized like any other empty result: the budget is
                            // constant, so an identical search would truncate at
                            // exactly the same place.
                            if (isValidKingdom(kingdom))
                                sUnlockSearchFailHash[kingdom] = searchKey;
                            return false;
                        }

                        s32 items[3] = { candidates[a], candidates[b], candidates[c] };
                        s32 locations[3] = { -1, -1, -1 };
                        sSearchBundlesTried++;
                        const s32 gain = simulateUnlockBundle(
                            state, unfilled, items, bundleSize, kingdom,
                            beforeTopology, beforeStoryCount,
                            beforeReachableCount,
                            rng, locations);
                        if (gain == kNoExpansion)
                            continue;

                        // First acceptable bundle still wins. Scanning on for a
                        // BETTER one would mean many more state copies and
                        // fixed points on the most expensive path in the fill;
                        // the gain is reported so the caller can decide whether
                        // a door beats it, which is what actually needed fixing.
                        for (s32 i = 0; i < bundleSize; i++) {
                            out->items[i] = items[i];
                            out->locations[i] = locations[i];
                        }
                        out->bundleSize = bundleSize;
                        out->gain = gain;
                        if (searchNodesThisCall >= 512) {
                            Randomizer::logPush(
                                "logic unlock-search kingdom=%s candidates=%d bundle=%d nodes=%d result=success ms=%u",
                                WorldMeta::getDisplayNameForWorldId(kingdom),
                                candidateCount, bundleSize, searchNodesThisCall,
                                elapsedMillisSince(searchStartTick));
                        }
                        return true;
                    }
                }
            }
        }
        if (searchNodesThisCall >= 512) {
            Randomizer::logPush(
                "logic unlock-search kingdom=%s candidates=%d nodes=%d result=none ms=%u",
                WorldMeta::getDisplayNameForWorldId(kingdom), candidateCount,
                searchNodesThisCall, elapsedMillisSince(searchStartTick));
        }
        if (isValidKingdom(kingdom))
            sUnlockSearchFailHash[kingdom] = searchKey;
        return false;
    }

    static s32 countUnplacedProgressUnlocks(const bool* unplaced);

    // "Completely stuck" escape hatch for Prioritize Difficult Movement: when the
    // strict fill (casual alternatives masked) cannot advance at all, re-allow the
    // casual alternatives until the next successful placement, then resume the
    // strict fill. The memoized unlock-search failures are policy-dependent, so
    // both transitions clear them. Returns whether the fallback newly engaged.
    // ---------------------------------------------------------------
    // Entrance wiring as a placement move
    //
    // Under Entrance Rando the door graph is no longer decided before the fill
    // runs. EntranceRando hands over a set of provably reachable source doors
    // and a set of unpaired sub-areas, and the fill wires them one at a time -
    // the same way placeReachabilityUnlock places a capture - so it can pick
    // doors knowing exactly what they open.
    //
    // Safety is unchanged and still by construction: every source comes out of
    // a pool EntranceRando proved reachable, and commitWiring refuses anything
    // else. The only thing the fill controls is the ORDER and the CHOICE, never
    // whether a pairing is legal.
    // ---------------------------------------------------------------

    // Simulating a pairing costs one reachability fixed point, so an Expand
    // move tries at most this many candidates before giving up. Mirrors the
    // budget discipline of placeReachabilityUnlock's kMaxSearchNodes.
    static constexpr s32 kMaxWireProbes = 16;

    struct WireStep {
        s16 sourceDoor;
        s16 targetDoor;
        s16 sphere;
        u8 doorClass;
        u8 intent;
    };

    // Every door the fill wired, in order, for the spoiler log. Bounded by the
    // door count, since each door is wired at most once.
    static constexpr s32 kMaxWireSteps = EntranceDB::sLinkCount * EntranceRando::DoorSlot_Count;
    static WireStep sWireSteps[kMaxWireSteps];
    static s32 sWireStepCount = 0;

    // Which kingdom a door physically sits in: the sub stage for a sub side,
    // the home stage for an overworld side.
    static s32 doorKingdom(s32 door) {
        const s32 link = EntranceRando::doorLink(door);
        if (link < 0 || link >= EntranceDB::sLinkCount)
            return -1;
        ensureLinkStageIndexes();
        const EntranceRando::DoorSlot slot = EntranceRando::doorSlot(door);
        const s32 stage = (slot == EntranceRando::DoorSlot_SubNear
                              || slot == EntranceRando::DoorSlot_SubFar)
            ? sLinkSubStageIndex[link]
            : sLinkHomeStageIndex[link];
        return isValidStage(stage) ? LD::sStages[stage].kingdom : -1;
    }

    // Whether the player could walk through this door RIGHT NOW, given the
    // state the fill has built so far. Applies exactly the gates
    // expandStageAccessFromEntrances applies, so a door the fill treats as an
    // available source is one the reachability proof would also let you use.
    static bool isDoorCurrentlyUsable(
        const LogicState& state, const bool* reachableNodes, s32 door) {
        const s32 link = EntranceRando::doorLink(door);
        if (link < 0 || link >= EntranceDB::sLinkCount || link >= LD::sEntranceCount)
            return false;
        const LD::EntranceAccess& access = LD::sEntrances[link];
        switch (EntranceRando::doorSlot(door)) {
        case EntranceRando::DoorSlot_HomeIn:
            return canUseHomeEntranceSide(state, reachableNodes, link, false);
        case EntranceRando::DoorSlot_HomeOut:
            return canUseHomeEntranceSide(state, reachableNodes, link, true);
        case EntranceRando::DoorSlot_SubNear:
            // Leaving through a sub-side door is scenario-gated by that door,
            // the same rule canUseHomeEntranceSide applies on the overworld.
            return access.subNearNode >= 0 && reachableNodes[access.subNearNode]
                && isEntranceNodeUsableInScenario(state, access.subNearNode);
        case EntranceRando::DoorSlot_SubFar:
            return access.subFarNode >= 0 && reachableNodes[access.subFarNode]
                && isEntranceNodeUsableInScenario(state, access.subFarNode);
        default:
            return false;
        }
    }

    // Seed Difficulty C5: how much the fill wants the far side of a door to be
    // somewhere else entirely. Relaxed keeps a door and its destination in the
    // same kingdom, so exploring stays local; Brutal throws the player across
    // the map, which is what produces the long back-and-forth routes.
    static s32 wireDistanceWeight(s32 sourceDoor, s32 targetDoor) {
        static constexpr s32 kNear[Randomizer::cSeedAxisLevelCount] = { 4, 2, 1, 1, 1 };
        static constexpr s32 kFar[Randomizer::cSeedAxisLevelCount] = { 1, 1, 1, 2, 4 };
        const s32 level = seedDifficultyLevel();
        const s32 sourceKingdom = doorKingdom(sourceDoor);
        const s32 targetKingdom = doorKingdom(targetDoor);
        if (!isValidKingdom(sourceKingdom) || !isValidKingdom(targetKingdom))
            return 1;
        return sourceKingdom == targetKingdom ? kNear[level] : kFar[level];
    }

    // How many still-unplaced locations become reachable if sourceDoor is
    // wired to targetDoor. Seeds the arrival node the pairing would produce and
    // lets the ordinary fixed point propagate from it, which is precisely what
    // the reachability pass does once the pairing is real.
    static s32 countLocationsOpenedByWiring(
        const LogicState& state, const bool* unfilled, s32 sourceDoor, s32 targetDoor) {
        LogicState sim = state;
        addAccessibleEntranceConn(
            sim, EntranceRando::doorLink(sourceDoor), EntranceRando::connForDoor(targetDoor));
        s32 buffer[LD::sMoonCount];
        return reachableLocations(sim, unfilled, buffer);
    }

    static void recordWireStep(
        s32 sourceDoor, s32 targetDoor, s32 sphere, EntranceRando::DoorClass doorClass,
        EntranceRando::WireIntent intent) {
        if (sWireStepCount >= kMaxWireSteps)
            return;
        WireStep& step = sWireSteps[sWireStepCount++];
        step.sourceDoor = (s16)sourceDoor;
        step.targetDoor = (s16)targetDoor;
        step.sphere = (s16)sphere;
        step.doorClass = (u8)doorClass;
        step.intent = (u8)intent;
    }

    // Moons indexed by the stage they physically sit in, so "how many
    // locations are behind this door" is a short list walk instead of a
    // reachability fixed point. Built once; the moon table never changes shape.
    static s16 sFirstMoonByStage[LD::sStageCount];
    static s16 sNextMoonInStage[LD::sMoonCount];
    static bool sMoonStageIndexReady = false;

    static void ensureMoonStageIndex() {
        if (sMoonStageIndexReady)
            return;
        for (s32 stage = 0; stage < LD::sStageCount; stage++)
            sFirstMoonByStage[stage] = -1;
        for (s32 moon = LD::sMoonCount - 1; moon >= 0; moon--) {
            const s32 stage = LD::sMoons[moon].stageIndex;
            sNextMoonInStage[moon] = -1;
            if (!isValidStage(stage))
                continue;
            sNextMoonInStage[moon] = sFirstMoonByStage[stage];
            sFirstMoonByStage[stage] = (s16)moon;
        }
        sMoonStageIndexReady = true;
    }

    // Which stage a door lands you in.
    static s32 doorArrivalStage(s32 door) {
        const s32 link = EntranceRando::doorLink(door);
        if (link < 0 || link >= EntranceDB::sLinkCount)
            return -1;
        ensureLinkStageIndexes();
        const EntranceRando::DoorSlot slot = EntranceRando::doorSlot(door);
        return (slot == EntranceRando::DoorSlot_SubNear
                   || slot == EntranceRando::DoorSlot_SubFar)
            ? sLinkSubStageIndex[link]
            : sLinkHomeStageIndex[link];
    }

    // How much of the world this door would hand the player, as a count of
    // still-unplaced locations it exposes.
    //
    // This is THE number Seed Openness is supposed to steer, so it has to be
    // available for every wiring decision, not just the rare stuck one. It is
    // therefore computed without a reachability fixed point: a door into a
    // stage the player can already reach opens nothing new, and otherwise the
    // locations physically in that stage are what it hands over. That is an
    // approximation - some of those locations will still need tokens the player
    // does not have - but it is monotone in the right direction, exact for the
    // common "this sub-area is closed off entirely" case, and cheap enough to
    // ask about every candidate.
    //
    // A door into a kingdom the player cannot otherwise reach is worth far more
    // than its location count suggests, because it opens that kingdom's whole
    // route; kOpensNewKingdomBonus reflects that.
    static constexpr s32 kOpensNewKingdomBonus = 12;

    static s32 scoreDoorOpening(const LogicState& state, const bool* unfilled, s32 targetDoor) {
        const s32 stage = doorArrivalStage(targetDoor);
        if (!isValidStage(stage))
            return 0;
        if (state.accessibleStages[stage])
            return 0;

        ensureMoonStageIndex();
        s32 count = 0;
        for (s32 moon = sFirstMoonByStage[stage]; moon >= 0; moon = sNextMoonInStage[moon]) {
            if (unfilled[moon])
                count++;
        }

        const s32 kingdom = LD::sStages[stage].kingdom;
        if (isValidKingdom(kingdom) && !state.accessibleKingdoms[kingdom])
            count += kOpensNewKingdomBonus;
        return count;
    }

    // Turns that score into a draw weight. This is where Seed Openness stops
    // being a chain-length knob and becomes what it claims to be: how much the
    // seed hands you at once.
    //
    //   Claustrophobic / Compact  strongly prefer a door that opens nothing
    //   Standard                  mild nudge toward opening something
    //   Open / Wide Open          scale with how much it opens
    static u32 openingWeightMultiplier(s32 score) {
        // Deliberately GENTLE. This term has to matter - steering how much a
        // door hands over is the whole point of Seed Openness - but it must not
        // swamp the door-class and distance weights. An earlier version scaled
        // up to 25x and was pathological at both ends: closed seeds wired
        // nothing but dead ends and thrashed, open seeds burned every door
        // immediately. Keep the whole range inside 1..4.
        const s32 openness = opennessLevel();
        if (openness <= 1)
            return score == 0 ? 2u : 1u;
        if (openness == 2)
            return 1u;
        const s32 capped = score > 6 ? 6 : score;
        // Open: 1..2. Wide Open: 1..3.
        return (u32)(1 + capped * (openness - 2) / 3);
    }

    // Whether this high-buffer iteration should spend its slack on a door.
    // Openness sets the rate; the open-door reserve inside the wiring move is
    // what stops it running the pool dry, so this only has to decide "how
    // often", not "is it safe".
    static bool shouldWireAtHighBuffer(Randomizer::Rng& rng) {
        static constexpr s32 kChanceOutOfEight[Randomizer::cSeedAxisLevelCount]
            = { 2, 2, 3, 4, 5 };
        return (s32)rng.nextModulo(8) < kChanceOutOfEight[opennessLevel()];
    }

    // Wire one door. Returns true when something was actually paired, which the
    // caller treats exactly like a successful placement: the frontier may have
    // grown, so it loops back around and re-evaluates.
    // reachableNodes is the map the caller already built this iteration.
    // Recomputing it here was a full fixed point per attempt, hundreds of times
    // per kingdom, and was the bulk of what this move cost.
    // The pairing the entrance side would make, and how much it opens.
    struct WireCandidate {
        s32 sourceDoor;
        s32 targetDoor;
        s32 gain;
        u8 doorClass;
        u8 intent;
    };

    static bool findEntranceWiring(
        LogicState& state,
        const bool* unfilled,
        const bool* reachableNodes,
        Randomizer::Rng& rng,
        EntranceRando::WireIntent intent,
        WireCandidate* out) {
        if (!Randomizer::isEntranceRandoEnabled())
            return false;
        if (!EntranceRando::isIncrementalBuildActive())
            return false;
        if (EntranceRando::getUnwiredTargetCount() <= 0)
            return false;

        static constexpr s32 kMaxDoors
            = EntranceDB::sLinkCount * EntranceRando::DoorSlot_Count;
        s32 poolDoors[kMaxDoors];
        u8 poolIsChainExit[kMaxDoors];
        s32 poolCount = EntranceRando::listUnwiredSources(poolDoors, poolIsChainExit, kMaxDoors);
        if (poolCount > kMaxDoors)
            poolCount = kMaxDoors;

        // Only doors the player can actually stand at right now may seed a
        // pairing. A pool door that is not yet reachable stays in the pool for
        // a later sphere, or falls to the leftover pairing at finalize.
        s32 sources[kMaxDoors];
        u8 sourceIsChainExit[kMaxDoors];
        s32 sourceCount = 0;
        for (s32 i = 0; i < poolCount; i++) {
            if (!isDoorCurrentlyUsable(state, reachableNodes, poolDoors[i]))
                continue;
            sources[sourceCount] = poolDoors[i];
            sourceIsChainExit[sourceCount] = poolIsChainExit[i];
            sourceCount++;
        }
        if (sourceCount <= 0)
            return false;

        // Seed Openness keeps this many reachable doors standing unexplored.
        // Being stuck outranks keeping options in reserve, so Expand ignores it.
        if (intent != EntranceRando::WireIntent_Expand
            && sourceCount <= EntranceRando::openDoorReserve())
            return false;

        // Draw the source weighted between the two pools: a chain exit
        // continues the chain it belongs to, a kingdom entrance starts a new
        // one. This is the old chain-length bias, now owned by Seed Openness.
        s32 chainWeight = 1;
        s32 kingdomWeight = 1;
        EntranceRando::getChainBias(&chainWeight, &kingdomWeight);
        u64 sourceTotal = 0;
        for (s32 i = 0; i < sourceCount; i++)
            sourceTotal += sourceIsChainExit[i] ? chainWeight : kingdomWeight;
        if (sourceTotal == 0)
            return false;
        s32 sourceDoor = sources[0];
        {
            u64 pick = nextModulo64(rng, sourceTotal);
            for (s32 i = 0; i < sourceCount; i++) {
                const u64 weight = sourceIsChainExit[i] ? chainWeight : kingdomWeight;
                if (pick < weight) {
                    sourceDoor = sources[i];
                    break;
                }
                pick -= weight;
            }
        }

        s32 targets[kMaxDoors];
        s32 targetCount = EntranceRando::listUnwiredTargets(sourceDoor, targets, kMaxDoors);
        if (targetCount > kMaxDoors)
            targetCount = kMaxDoors;
        if (targetCount <= 0)
            return false;

        // Weight every candidate by what kind of place it is (Seed Openness),
        // how far away it is (Seed Difficulty), and - the point of the whole
        // exercise - HOW MUCH IT ACTUALLY OPENS. That last term is scored for
        // every candidate on every wiring, not just when the fill is stuck:
        // steering the number of locations a door hands over is exactly what
        // Seed Openness is for, and a weight that ignores it reduces this back
        // to a blind shuffle with extra steps.
        u32 weights[kMaxDoors];
        s32 openScore[kMaxDoors];
        u64 totalWeight = 0;
        for (s32 i = 0; i < targetCount; i++) {
            const EntranceRando::DoorClass doorClass = EntranceRando::classifyTarget(targets[i]);
            openScore[i] = scoreDoorOpening(state, unfilled, targets[i]);
            s32 weight = EntranceRando::doorClassWeight(doorClass)
                * wireDistanceWeight(sourceDoor, targets[i])
                * (s32)openingWeightMultiplier(openScore[i]);
            if (intent == EntranceRando::WireIntent_Compact) {
                // Deliberately spend the door on something that opens little or
                // nothing. Only fall back to the rest if there is nothing else.
                // Divide out the openness multiplier first - it already leans
                // this way at low openness, and applying both stacked hard
                // enough to wire nothing but dead ends.
                weight = EntranceRando::doorClassWeight(doorClass)
                    * wireDistanceWeight(sourceDoor, targets[i]);
                weight = openScore[i] == 0 ? weight * 4 : 1;
            } else if (intent == EntranceRando::WireIntent_Expand) {
                // Stuck: a door that opens nothing is no use at all here.
                weight = openScore[i] > 0 ? weight * (1 + openScore[i]) : 0;
            }
            if (weight < 0)
                weight = 0;
            if (weight < 1 && intent != EntranceRando::WireIntent_Expand)
                weight = 1;
            weights[i] = (u32)weight;
            totalWeight += weights[i];
        }
        if (totalWeight == 0)
            return false;

        // Draw in weighted order. The weights already carry the opening score,
        // so Fill and Compact can take the first draw: it was chosen knowing
        // what it opens, which is what was missing before.
        //
        // Expand still VERIFIES, because "stuck" is the one case where being
        // approximately right is not good enough - scoreDoorOpening counts
        // locations in a closed stage without proving the player can actually
        // satisfy their requirements. The weighting means the candidates it
        // reaches first are the ones most likely to pass, so the budget is
        // rarely spent.
        s32 baselineReachable = 0;
        if (intent == EntranceRando::WireIntent_Expand) {
            LogicState probeBase = state;
            s32 buffer[LD::sMoonCount];
            baselineReachable = reachableLocations(probeBase, unfilled, buffer);
        }

        bool tried[kMaxDoors] = {};
        const s32 probeBudget
            = intent == EntranceRando::WireIntent_Expand ? kMaxWireProbes : 1;
        s32 chosen = -1;
        s32 measuredGain = kNoExpansion;
        for (s32 probe = 0; probe < probeBudget && totalWeight > 0; probe++) {
            u64 pick = nextModulo64(rng, totalWeight);
            s32 index = -1;
            for (s32 i = 0; i < targetCount; i++) {
                if (tried[i] || weights[i] == 0)
                    continue;
                if (pick < weights[i]) {
                    index = i;
                    break;
                }
                pick -= weights[i];
            }
            if (index < 0)
                break;

            if (intent != EntranceRando::WireIntent_Expand) {
                chosen = index;
                break;
            }

            const s32 opened
                = countLocationsOpenedByWiring(state, unfilled, sourceDoor, targets[index]);
            if (opened > baselineReachable) {
                chosen = index;
                measuredGain = opened - baselineReachable;
                break;
            }
            // Did not open anything after all; drop it and try another.
            tried[index] = true;
            totalWeight -= weights[index];
            weights[index] = 0;
        }
        if (chosen < 0)
            return false;

        out->sourceDoor = sourceDoor;
        out->targetDoor = targets[chosen];
        out->doorClass = EntranceRando::classifyTarget(targets[chosen]);
        out->intent = intent;
        // What the caller weighs this against, and it MUST be the same
        // measurement an unlock reports: the delta in reachable locations.
        //
        // openScore is not that. It counts every unfilled location in a closed
        // stage and adds a flat bonus for opening a kingdom, so it runs 10-40
        // where an unlock delta runs 1-5. Handing it to expandFrontier made a
        // door beat an unlock essentially always - the fill then spent every
        // low buffer wiring a door, which places no moon, so the buffer never
        // recovered and the attempt died once the doors ran out. It is a fine
        // heuristic for WEIGHTING the draw, and useless as a common currency.
        out->gain = measuredGain;
        return true;
    }

    static bool commitEntranceWiring(
        LogicState& state,
        const bool* unfilled,
        s32& currentSphere,
        bool& frontierInitialized,
        const WireCandidate& candidate) {
        if (!EntranceRando::commitWiring(candidate.sourceDoor, candidate.targetDoor))
            return false;

        // The door is real now: refresh exactly as recordPlacement does, so the
        // sphere counter and the state events see the wider world immediately.
        refreshReachableStateEvents(state, nullptr, 0);
        s32 refreshed[LD::sMoonCount];
        updateFrontierSpheres(state, unfilled, currentSphere, frontierInitialized, refreshed);
        recordWireStep(candidate.sourceDoor, candidate.targetDoor, currentSphere,
            (EntranceRando::DoorClass)candidate.doorClass,
            (EntranceRando::WireIntent)candidate.intent);
        return true;
    }

    // Wire one door outright, for the callers that do not need to weigh it
    // against an unlock (the high-buffer spend and the end-of-fill tail).
    static bool wireEntranceForFrontier(
        LogicState& state,
        const bool* unfilled,
        const bool* reachableNodes,
        s32& currentSphere,
        bool& frontierInitialized,
        Randomizer::Rng& rng,
        EntranceRando::WireIntent intent) {
        WireCandidate candidate;
        if (!findEntranceWiring(state, unfilled, reachableNodes, rng, intent, &candidate))
            return false;
        return commitEntranceWiring(
            state, unfilled, currentSphere, frontierInitialized, candidate);
    }

    // ---------------------------------------------------------------
    // Frontier expansion: one decision, three kinds of move
    //
    // When the buffer runs low the fill has to widen the world, and there are
    // three ways to do it: grant an ability, grant a capture, or wire a loading
    // zone that has not been used yet. These used to be a fixed ladder - both
    // unlock kinds first, doors only if that failed - so a door never got
    // chosen while any unlock at all still helped, however little.
    //
    // They are now weighed against each other on one scale: how many
    // still-unplaced locations each would expose, given the tools the player
    // actually holds at this point. That is the question the user cares about
    // ("what will this open up"), and it is the only way the entrance side can
    // demonstrate that it is the better move.
    //
    // The two are NOT the same currency, which is the trap. Placing an unlock
    // spends a location out of a finite pool; wiring a door spends a door. On a
    // pure "what opens most" comparison doors are effectively free and win
    // every time, and the fill wires the entire graph on the first few low
    // buffers - measured, and it is pathological. Seed Openness is what prices
    // them: a closed seed pays a lot per door, an open one pays little.
    // ---------------------------------------------------------------

    // What a door has to beat, as a percentage of the unlock it is competing
    // with. Claustrophobic demands a door open four times as much before it is
    // taken over an unlock; Wide Open takes a door that is merely comparable.
    // Standard is deliberately tuned so the resulting graph keeps the character
    // the pre-fill one-shot builder produced.
    static s32 doorPricePercent() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount]
            = { 400, 250, 150, 100, 75 };
        return kByOpenness[opennessLevel()];
    }

    static void commitReachabilityUnlock(
        LogicState& state,
        bool* unfilled,
        bool* unplaced,
        s32& currentSphere,
        bool& frontierInitialized,
        const UnlockCandidate& candidate) {
        for (s32 i = 0; i < candidate.bundleSize; i++) {
            s32 actualReachable[LD::sMoonCount];
            updateFrontierSpheres(
                state, unfilled, currentSphere, frontierInitialized, actualReachable);
            recordPlacement(state, unfilled, unplaced, candidate.locations[i],
                candidate.items[i],
                sLocationSphere[candidate.locations[i]] >= 0
                    ? sLocationSphere[candidate.locations[i]]
                    : currentSphere,
                candidate.bundleSize == 1 ? "unlock" : "unlock bundle");
        }
    }

    // Widen the frontier by whichever of the three moves opens the most, priced
    // by Seed Openness. Returns false only when nothing can widen it at all.
    static bool expandFrontier(
        LogicState& state,
        bool* unfilled,
        bool* unplaced,
        const bool* reachableNodes,
        s32& currentSphere,
        bool& frontierInitialized,
        Randomizer::Rng& rng,
        s32 kingdom) {
        UnlockCandidate unlock;
        const bool haveUnlock
            = findReachabilityUnlock(state, unfilled, unplaced, rng, kingdom, &unlock);

        // Ask the entrance side what its best door would open, without
        // committing to it.
        WireCandidate wiring;
        const bool haveWiring = findEntranceWiring(
            state, unfilled, reachableNodes, rng, EntranceRando::WireIntent_Expand, &wiring);

        if (!haveUnlock && !haveWiring)
            return false;
        if (haveUnlock && !haveWiring) {
            commitReachabilityUnlock(
                state, unfilled, unplaced, currentSphere, frontierInitialized, unlock);
            return true;
        }
        if (haveWiring && !haveUnlock) {
            return commitEntranceWiring(
                state, unfilled, currentSphere, frontierInitialized, wiring);
        }

        // Both are available: the door has to clear its price to be worth a
        // door rather than a moon placement.
        const s32 unlockGain = unlock.gain > 0 ? unlock.gain : 1;
        const s32 required = unlockGain * doorPricePercent() / 100;
        if (wiring.gain >= required
            && commitEntranceWiring(
                state, unfilled, currentSphere, frontierInitialized, wiring))
            return true;

        commitReachabilityUnlock(
            state, unfilled, unplaced, currentSphere, frontierInitialized, unlock);
        return true;
    }

    static bool tryEngageCasualFallback(s32& fallbackPlacementFloor) {
        if (!maskingCasualOptions())
            return false;
        sCasualFallbackActive = true;
        fallbackPlacementFloor = sPlacementCount;
        sFailureCode = FailureCode::None;
        std::memset(sUnlockSearchFailHash, 0, sizeof(sUnlockSearchFailHash));
        Randomizer::logPush(
            "prioritize-difficult: fill stuck, casual methods re-enabled (placements=%d)",
            sPlacementCount);
        return true;
    }

    static void settleCasualFallback(s32& fallbackPlacementFloor) {
        if (!sCasualFallbackActive)
            return;
        // floor >= 0: this phase engaged the fallback; keep it until a placement
        // lands. floor < 0: a previous phase left it active; resume strict now.
        if (fallbackPlacementFloor >= 0 && sPlacementCount <= fallbackPlacementFloor)
            return;
        sCasualFallbackActive = false;
        fallbackPlacementFloor = -1;
        std::memset(sUnlockSearchFailHash, 0, sizeof(sUnlockSearchFailHash));
        Randomizer::logPush(
            "prioritize-difficult: casual fallback released (placements=%d)",
            sPlacementCount);
    }

    // Defined with the rest of the final-fill safety-net helpers below. The
    // progress tail can discover a stranded carrier before the blind even fill
    // runs, so it needs to reserve that carrier without placing it anywhere.
    static bool addAutoGrantItem(s32 item);

    static void reserveUnplacedProgressAutoGrants(bool* unplaced) {
        if (!unplaced)
            return;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!unplaced[item] || !itemHasProgressUnlock(item))
                continue;
            if (!addAutoGrantItem(item)) {
                addWarning("Auto-grant list exceeded its capacity.");
                return;
            }
            // Removing the item from the even-fill pool leaves one physical
            // location empty for the corresponding start grant.
            unplaced[item] = false;
        }
    }

    static bool runBufferForKingdom(
        LogicState& state,
        s32 kingdom,
        bool* unfilled,
        bool* unplaced,
        s32& currentSphere,
        bool& frontierInitialized,
        Randomizer::Rng& rng) {
        s32 fallbackPlacementFloor = -1;
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        sDiagPhase = DiagPhase_TierFill;
#endif
        // Built once per iteration by updateFrontierSpheres and handed to the
        // entrance wiring, which would otherwise rebuild the same fixed point.
        static bool frontierNodes[LD::sNodeCount];
        while (true) {
            settleCasualFallback(fallbackPlacementFloor);
            s32 reachable[LD::sMoonCount];
            const s32 reachableCount = updateFrontierSpheres(
                state, unfilled, currentSphere, frontierInitialized, reachable, frontierNodes);
            const bool needsMoons = countUnplacedRequirements(state, kingdom) > 0;
            const bool needsStory = requiresRouteStoryCompletion(kingdom)
                && state.storyCounts[kingdom] < LD::sStoryTotalsByKingdom[kingdom];

            if (!needsMoons && !needsStory) {
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
                logKingdomClosed(state, kingdom, currentSphere);
#endif
                return true;
            }

            if (!needsMoons && needsStory) {
                if (isKingdomStoryReachable(state, kingdom, unfilled)) {
                    if (materializeKingdomStory(
                            state, kingdom, unfilled, unplaced,
                            currentSphere, frontierInitialized, rng))
                        continue;
                    if (tryEngageCasualFallback(fallbackPlacementFloor))
                        continue;
                    setFailure(
                        FailureCode::CapacityFailure,
                        "Reachable %s story route could not be materialized.",
                        WorldMeta::getDisplayNameForWorldId(kingdom));
                    return false;
                }

                if (expandFrontier(state, unfilled, unplaced, frontierNodes,
                        currentSphere, frontierInitialized, rng, kingdom))
                    continue;
                if (tryEngageCasualFallback(fallbackPlacementFloor))
                    continue;
                if (sFailureCode == FailureCode::None) {
                    setFailure(
                        FailureCode::UnreachableObjective,
                        "No capture, ability or entrance expands the %s story frontier.",
                        WorldMeta::getDisplayNameForWorldId(kingdom));
                }
                return false;
            }

            const s32 availablePositions = countCompatibleReachableLocations(
                reachable, reachableCount, unplaced);
            const s32 unplacedRequirements = countUnplacedRequirements(state, kingdom);
            const s32 buffer = availablePositions - unplacedRequirements;

            if (buffer <= 0) {
                if (expandFrontier(state, unfilled, unplaced, frontierNodes,
                        currentSphere, frontierInitialized, rng, kingdom))
                    continue;
                if (tryEngageCasualFallback(fallbackPlacementFloor))
                    continue;
                if (sFailureCode == FailureCode::None) {
                    // reachableCount and availablePositions differ in a way
                    // that decides where to look: 0 reachable means the
                    // frontier itself collapsed (no location is walkable at
                    // all), while reachable-but-incompatible means the slots
                    // exist and are the wrong single/multi kind. Reporting
                    // only the second number made the two indistinguishable.
                    setFailure(
                        FailureCode::CapacityFailure,
                        "%s buffer exhausted: %d reachable, %d compatible, %d required (%s).",
                        WorldMeta::getDisplayNameForWorldId(kingdom),
                        reachableCount, availablePositions, unplacedRequirements,
                        reachableCount <= 0 ? "frontier empty"
                                            : "slot type mismatch");
                }
                return false;
            }

            if (buffer > highBufferThreshold()) {
                // Plenty of slack: spend some of it on a door rather than
                // another moon, so the door graph is chosen THROUGHOUT the run
                // instead of all at once in the tail. How often, and what kind
                // of door, is the whole of Seed Openness: a claustrophobic seed
                // seals pockets and dead ends, a wide-open one keeps handing out
                // ways into other kingdoms.
                if (shouldWireAtHighBuffer(rng)
                    && wireEntranceForFrontier(state, unfilled, frontierNodes, currentSphere,
                        frontierInitialized, rng,
                        opennessLevel() <= 1 ? EntranceRando::WireIntent_Compact
                                             : EntranceRando::WireIntent_Fill))
                    continue;
                const s32 item = pickHighBufferCompatibleItem(
                    unplaced, reachable, reachableCount, rng, currentSphere);
                const s32 location = item >= 0
                    ? pickDepthWeightedCompatibleLocation(
                          item, reachable, reachableCount, rng, kingdom)
                    : -1;
                if (item < 0 || location < 0) {
                    if (tryEngageCasualFallback(fallbackPlacementFloor))
                        continue;
                    setFailure(
                        FailureCode::CapacityFailure,
                        "High-buffer placement had no compatible item/location in %s.",
                        WorldMeta::getDisplayNameForWorldId(kingdom));
                    return false;
                }
                recordPlacement(
                    state, unfilled, unplaced, location, item,
                    sLocationSphere[location], "buffer placement");
                continue;
            }

            const bool shouldExpandBeforeRequiredMoon = buffer > lowBufferThreshold()
                || unplacedRequirements > lowBufferExpansionThreshold();
            if (shouldExpandBeforeRequiredMoon) {
                if (expandFrontier(state, unfilled, unplaced, frontierNodes,
                        currentSphere, frontierInitialized, rng, kingdom))
                    continue;
                if (sFailureCode != FailureCode::None) {
                    if (tryEngageCasualFallback(fallbackPlacementFloor))
                        continue;
                    return false;
                }
                // Nothing expands the frontier; consume a required moon.
            }

            const s32 item = pickRequiredMoonForKingdom(
                state, kingdom, unplaced, reachable, reachableCount, rng);
            const s32 location = item >= 0
                ? pickDepthWeightedCompatibleLocation(
                      item, reachable, reachableCount, rng, kingdom)
                : -1;
            if (item >= 0 && location >= 0) {
                recordPlacement(
                    state, unfilled, unplaced, location, item,
                    sLocationSphere[location], "moon requirement");
                continue;
            }

            // A single/multi mismatch can make the raw buffer positive while the
            // required native set is not placeable. Give an unlock, then a door,
            // one last chance to expose a compatible slot before rejecting.
            if (expandFrontier(state, unfilled, unplaced, frontierNodes,
                    currentSphere, frontierInitialized, rng, kingdom))
                continue;
            if (tryEngageCasualFallback(fallbackPlacementFloor))
                continue;
            if (sFailureCode == FailureCode::None) {
                setFailure(
                    FailureCode::CapacityFailure,
                    "No compatible required moon remains for %s (%d still needed).",
                    WorldMeta::getDisplayNameForWorldId(kingdom),
                    unplacedRequirements);
            }
            return false;
        }
    }

    static void collectPreplacedReachable(LogicState& state) {
        bool changed = true;
        while (changed) {
            changed = false;
            bool consider[LD::sMoonCount];
            for (s32 location = 0; location < LD::sMoonCount; location++) {
                consider[location] = sPlacementItemForLocation[location] >= 0
                    && !state.collectedLocations[location];
            }
            s32 reachable[LD::sMoonCount];
            const s32 reachableCount = reachableLocations(state, consider, reachable);
            for (s32 i = 0; i < reachableCount; i++) {
                collectLocation(state, reachable[i], nullptr, 0);
                changed = true;
            }
            refreshReachableStateEvents(state, nullptr, 0);
        }
    }

    static bool placeRemainingProgressEvenly(
        LogicState& state,
        bool* unfilled,
        bool* unplaced,
        s32& currentSphere,
        bool& frontierInitialized,
        Randomizer::Rng& rng) {
        s32 fallbackPlacementFloor = -1;
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        sDiagPhase = DiagPhase_ProgressTail;
        sDiagTier = -1;
#endif
        static bool frontierNodes[LD::sNodeCount];
        while (countUnplacedProgressUnlocks(unplaced) > 0) {
            settleCasualFallback(fallbackPlacementFloor);
            s32 reachable[LD::sMoonCount];
            const s32 reachableCount = updateFrontierSpheres(
                state, unfilled, currentSphere, frontierInitialized, reachable, frontierNodes);
            if (reachableCount <= 0) {
                // A door may still open somewhere to put these.
                if (wireEntranceForFrontier(state, unfilled, frontierNodes, currentSphere,
                        frontierInitialized, rng, EntranceRando::WireIntent_Expand))
                    continue;
                if (tryEngageCasualFallback(fallbackPlacementFloor))
                    continue;
                // Phase 6 owns the final safety net for required carriers that
                // have no compatible reachable location. Reserve them before
                // the even fill so it cannot hide the problem in an unreachable
                // physical placement.
                const s32 stranded = countUnplacedProgressUnlocks(unplaced);
                reserveUnplacedProgressAutoGrants(unplaced);
                addWarning(
                    "%d progress unlock item(s) had no reachable location; "
                    "they will be granted at the start.",
                    stranded);
                return true;
            }

            // Keep spending doors through the tail as well. Whatever is left at
            // finalize gets paired blind, so a door the fill can place while it
            // still has a frontier to reason about is a better door.
            if (EntranceRando::getUnwiredTargetCount() > 0
                && wireEntranceForFrontier(state, unfilled, frontierNodes, currentSphere,
                    frontierInitialized, rng, EntranceRando::WireIntent_Fill))
                continue;

            s32 item = pickRandomCompatibleItem(
                unplaced, reachable, reachableCount, rng, true);
            bool placedProgress = item >= 0;
            const s32* placementCandidates = reachable;
            s32 placementCandidateCount = reachableCount;
            s32 storyDrivers[LD::sMoonCount];
            if (item < 0) {
                // A story/scenario driver may need an ordinary moon before a
                // compatible slot for the remaining unlock type appears.
                s32 storyDriverCount = 0;
                for (s32 i = 0; i < reachableCount; i++) {
                    const s32 location = reachable[i];
                    const s32 hostKingdom = LD::sMoons[location].kingdom;
                    if (isValidKingdom(hostKingdom)
                        && isStoryDriverForKingdom(
                            state, location, hostKingdom))
                        storyDrivers[storyDriverCount++] = location;
                }
                if (storyDriverCount > 0) {
                    placementCandidates = storyDrivers;
                    placementCandidateCount = storyDriverCount;
                }
                item = pickRandomCompatibleItem(
                    unplaced, placementCandidates,
                    placementCandidateCount, rng, false);
                if (item < 0 && placementCandidates == storyDrivers) {
                    placementCandidates = reachable;
                    placementCandidateCount = reachableCount;
                    item = pickRandomCompatibleItem(
                        unplaced, reachable, reachableCount, rng, false);
                }
            }
            const s32 location = item >= 0
                ? pickEvenlyDistributedCompatibleLocation(
                      item, placementCandidates, placementCandidateCount, rng)
                : -1;
            if (item < 0 || location < 0) {
                if (tryEngageCasualFallback(fallbackPlacementFloor))
                    continue;
                // A type-mismatched or otherwise unavailable slot is exactly
                // the condition the generation safety net is meant to absorb.
                // Reserve the remaining carriers before the blind even fill so
                // it cannot put them into a location the tail already proved
                // unusable.
                const s32 stranded = countUnplacedProgressUnlocks(unplaced);
                reserveUnplacedProgressAutoGrants(unplaced);
                addWarning(
                    "%d progress unlock item(s) lack a compatible reachable slot; "
                    "they will be granted at the start.",
                    stranded);
                return true;
            }
            recordPlacement(
                state, unfilled, unplaced, location, item,
                sLocationSphere[location],
                placedProgress ? "progression tail" : "tail expansion");
            s32 expandedReachable[LD::sMoonCount];
            updateFrontierSpheres(
                state, unfilled, currentSphere,
                frontierInitialized, expandedReachable);
        }
        return true;
    }

    // Progression unlocks (captures/abilities/cloud-paintings/moon rock keys) still waiting for a slot.
    static s32 countUnplacedProgressUnlocks(const bool* unplaced) {
        s32 count = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (unplaced[item] && itemHasProgressUnlock(item))
                count++;
        }
        return count;
    }

    static void resetCloudPaintingAssignments(u32 seed, u32 attempt) {
        sCloudPaintingAssignmentsReady = false;
        sCloudPaintingAssignmentTieSeed = seed ^ (attempt * 0xD1B54A35u)
            ^ 0xC10D11A6u;
        std::memset(sFreePaintingLinks, 0, sizeof(sFreePaintingLinks));
        std::memset(sFinalFreePaintingLinks, 0, sizeof(sFinalFreePaintingLinks));
        for (s32 i = 0; i < kCloudPaintingSlotCount; i++)
            sFinalCloudPaintingLinkBySlot[i] = -1;
    }

    // Choose the free link after the initial kingdom and starting-equipment state
    // exists.  A free link now starts at a painting the player can use immediately
    // whenever one exists, instead of being a disconnected seed-time permutation.
    static void chooseInitialFreePaintingLinks() {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return;

        bool slotPresent[kCloudPaintingSlotCount] = {};
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            for (s32 unlock = 0;
                 unlock < sMoonCloudPaintingSlotCounts[item]; unlock++) {
                const s32 slot = sMoonCloudPaintingSlots[item][unlock];
                if (slot >= 0 && slot < kCloudPaintingSlotCount)
                    slotPresent[slot] = true;
            }
        }
        s32 cloudSlotCount = 0;
        for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++)
            cloudSlotCount += slotPresent[slot] ? 1 : 0;
        const s32 freeLinkCount = LD::sPaintingLinkCount > cloudSlotCount
            ? LD::sPaintingLinkCount - cloudSlotCount
            : 0;

        for (s32 freeIndex = 0; freeIndex < freeLinkCount; freeIndex++) {
            LogicState initialState;
            initState(initialState);
            // A previously chosen free link may already have opened another
            // kingdom.  Expand that small, one-time opening state before choosing
            // the next free link so the free set can form a useful short chain.
            bool initialReachableNodes[LD::sNodeCount];
            expandReachabilityFixedPoint(initialState, initialReachableNodes);
            const s32 paintingIndex = selectUsefulPaintingLink(
                initialState, -1 - freeIndex);
            if (paintingIndex < 0)
                break;
            sFreePaintingLinks[paintingIndex] = true;
            Randomizer::logPush(
                "logic-selected free painting: slot=%d link=%d",
                freeIndex, paintingIndex);
        }

        for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
            sFinalFreePaintingLinks[i] = sFreePaintingLinks[i];
    }

    static s32 countOpeningReachableLocations(LogicState& state) {
        bool unfilled[LD::sMoonCount] = {};
        for (s32 item = 0; item < LD::sMoonCount; item++)
            unfilled[item] = sMoonEntries[item]
                && isMoonPoolEligible(*sMoonEntries[item]);
        s32 reachable[LD::sMoonCount];
        return reachableLocations(state, unfilled, reachable);
    }

    static u32 logicStartTieBreak(u32 seed, u32 attempt, bool capture, s32 index) {
        u32 value = seed ^ (attempt * 0x27D4EB2Fu)
            ^ (capture ? 0xC4A7F2D1u : 0xA8111AB1u)
            ^ (static_cast<u32>(index + 1) * 0x9E3779B9u);
        value ^= value >> 16;
        value *= 0x7FEB352Du;
        value ^= value >> 15;
        return value;
    }

    // Starting equipment is normally a pure seeded shuffle.  When that shuffle
    // leaves the opening frontier at or below the low-buffer threshold, reserve
    // only the slots that immediately expand it for logic-selected starts.  Any
    // unused configured slots are still filled by the normal deterministic shuffle.
    static bool chooseLogicStartingEquipment(u32 seed, u32 attempt) {
        if (Randomizer::getStartingEquipment()
            == Randomizer::StartingEquipment_Scarce)
            return false;

        LogicState currentStart;
        initState(currentStart);
        if (countOpeningReachableLocations(currentStart) > lowBufferThreshold())
            return false;

        const s32 abilityTarget = Randomizer::isLockedAbilitiesEnabled()
            ? AbilityRando::getStartingAbilityCount()
            : 0;
        const s32 captureTarget = Randomizer::isCaptureRandoEnabled()
            ? CaptureRando::getStartingCaptureCount()
            : 0;
        if (abilityTarget <= 0 && captureTarget <= 0)
            return false;

        AbilityRando::AbilityId selectedAbilities[2] = {};
        s16 selectedCaptures[4] = {};
        s32 selectedAbilityCount = 0;
        s32 selectedCaptureCount = 0;

        // Retain the seed-selected cap throw, but assess optional starts from the
        // bare opening state so a random extra cannot hide an otherwise tight seed.
        LogicState working;
        initState(working, false);
        s32 reachableCount = countOpeningReachableLocations(working);

        while (reachableCount <= lowBufferThreshold()) {
            bool bestIsCapture = false;
            s32 bestIndex = -1;
            s32 bestReachableCount = reachableCount;
            u32 bestTie = 0;

            if (selectedAbilityCount < abilityTarget) {
                for (s32 index = 0; index < AbilityRando::getAbilityCount(); index++) {
                    const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(index);
                    if (!ability || ability->category != AbilityRando::AbilityCategory_Movement
                        || ability->alwaysUnlocked)
                        continue;

                    const AbilityRando::AbilityEntry* award = ability;
                    while (award->prerequisite >= 0) {
                        award = AbilityRando::getAbility(award->prerequisite);
                        if (!award)
                            break;
                    }
                    if (!award || award->category != AbilityRando::AbilityCategory_Movement)
                        continue;

                    bool duplicate = false;
                    for (s32 i = 0; i < selectedAbilityCount; i++)
                        duplicate |= selectedAbilities[i] == award->id;
                    const s32 token = findTokenIndex(award->key);
                    if (duplicate || token < 0 || working.tokens[token])
                        continue;

                    LogicState candidate = working;
                    candidate.tokens[token] = true;
                    const s32 candidateReachableCount = countOpeningReachableLocations(candidate);
                    const u32 tie = logicStartTieBreak(seed, attempt, false, award->id);
                    if (candidateReachableCount > bestReachableCount
                        || (candidateReachableCount == bestReachableCount
                            && candidateReachableCount > reachableCount
                            && tie > bestTie)) {
                        bestIsCapture = false;
                        bestIndex = award->id;
                        bestReachableCount = candidateReachableCount;
                        bestTie = tie;
                    }
                }
            }

            if (selectedCaptureCount < captureTarget) {
                for (s32 index = 0; index < CaptureRando::getCaptureCount(); index++) {
                    const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(index);
                    const s32 token = capture ? findTokenIndex(capture->hackName) : -1;
                    if (token < 0 || working.tokens[token])
                        continue;

                    bool duplicate = false;
                    for (s32 i = 0; i < selectedCaptureCount; i++)
                        duplicate |= selectedCaptures[i] == index;
                    if (duplicate)
                        continue;

                    LogicState candidate = working;
                    candidate.tokens[token] = true;
                    const s32 candidateReachableCount = countOpeningReachableLocations(candidate);
                    const u32 tie = logicStartTieBreak(seed, attempt, true, index);
                    if (candidateReachableCount > bestReachableCount
                        || (candidateReachableCount == bestReachableCount
                            && candidateReachableCount > reachableCount
                            && tie > bestTie)) {
                        bestIsCapture = true;
                        bestIndex = index;
                        bestReachableCount = candidateReachableCount;
                        bestTie = tie;
                    }
                }
            }

            if (bestIndex < 0 || bestReachableCount <= reachableCount)
                break;

            if (bestIsCapture) {
                selectedCaptures[selectedCaptureCount++] = static_cast<s16>(bestIndex);
                const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(bestIndex);
                working.tokens[findTokenIndex(capture->hackName)] = true;
            } else {
                selectedAbilities[selectedAbilityCount++] = static_cast<AbilityRando::AbilityId>(bestIndex);
                const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(bestIndex);
                working.tokens[findTokenIndex(ability->key)] = true;
            }
            reachableCount = bestReachableCount;
        }

        const bool changed = selectedAbilityCount > 0 || selectedCaptureCount > 0;
        if (selectedAbilityCount > 0)
            AbilityRando::setLogicStartingOverride(
                selectedAbilities, selectedAbilityCount);
        if (selectedCaptureCount > 0)
            CaptureRando::setLogicStartingOverride(
                selectedCaptures, selectedCaptureCount);
        if (changed) {
            Randomizer::logPush(
                "logic-selected starts: attempt=%u abilities=%d captures=%d opening=%d",
                attempt, selectedAbilityCount, selectedCaptureCount, reachableCount);
        }
        return changed;
    }

    static bool finalizeCloudPaintingAssignments(const LogicState& state) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return true;

        bool slotPresent[kCloudPaintingSlotCount] = {};
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            for (s32 unlock = 0;
                 unlock < sMoonCloudPaintingSlotCounts[item]; unlock++) {
                const s32 slot = sMoonCloudPaintingSlots[item][unlock];
                if (slot >= 0 && slot < kCloudPaintingSlotCount)
                    slotPresent[slot] = true;
            }
        }

        // The proof only needs to collect Cloud-assigned rewards that advance
        // its chosen route. Every such reward still needs a stable runtime
        // mapping, though, so complete any never-collected slot from the
        // remaining links rather than rejecting an otherwise valid placement.
        LogicState completedState = state;
        for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++) {
            if (!slotPresent[slot])
                continue;
            s32 link = completedState.cloudPaintingLinkBySlot[slot];
            if (link < 0 || link >= LD::sPaintingLinkCount) {
                link = selectUsefulPaintingLink(completedState, slot);
                if (link >= 0 && link < LD::sPaintingLinkCount) {
                    completedState.cloudPaintingLinkBySlot[slot] = static_cast<s16>(link);
                    Randomizer::logPush(
                        "logic-finalized deferred Cloud painting: slot=%d link=%d",
                        slot, link);
                }
            }
            if (link < 0 || link >= LD::sPaintingLinkCount) {
                setFailure(FailureCode::DataError,
                    "Cloud painting slot %d could not be assigned a link.", slot);
                return false;
            }
            sFinalCloudPaintingLinkBySlot[slot] = static_cast<s16>(link);
        }

        s16 order[LD::sPaintingLinkCount] = {};
        bool used[LD::sPaintingLinkCount] = {};
        s32 orderCount = 0;
        for (s32 link = 0; link < LD::sPaintingLinkCount; link++) {
            if (!sFinalFreePaintingLinks[link])
                continue;
            order[orderCount++] = static_cast<s16>(link);
            used[link] = true;
        }
        for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++) {
            if (!slotPresent[slot])
                continue;
            const s32 link = sFinalCloudPaintingLinkBySlot[slot];
            if (link < 0 || link >= LD::sPaintingLinkCount || used[link]) {
                setFailure(FailureCode::DataError,
                    "Cloud painting link assignment is not a permutation.");
                return false;
            }
            order[orderCount++] = static_cast<s16>(link);
            used[link] = true;
        }
        for (s32 link = 0; link < LD::sPaintingLinkCount; link++) {
            if (!used[link])
                order[orderCount++] = static_cast<s16>(link);
        }
        if (orderCount != LD::sPaintingLinkCount
            || !PaintingRando::setCloudLinkOrder(order, orderCount)) {
            setFailure(FailureCode::DataError,
                "Could not finalize the Cloud painting link order.");
            return false;
        }

        sCloudPaintingAssignmentsReady = true;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            for (s32 unlock = 0;
                 unlock < sMoonCloudPaintingSlotCounts[item]; unlock++) {
                const s32 slot = sMoonCloudPaintingSlots[item][unlock];
                if (slot >= 0 && slot < kCloudPaintingSlotCount)
                    sMoonPaintingUnlocks[item][unlock]
                        = sFinalCloudPaintingLinkBySlot[slot];
            }
        }
        return true;
    }

    static void fillRemainingEvenly(bool* unfilled, bool* unplaced, Randomizer::Rng& rng) {
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        sDiagPhase = DiagPhase_EvenFill;
        sDiagTier = -1;
#endif
        s32 items[LD::sMoonCount];
        s32 itemCount = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            if (unplaced[i])
                items[itemCount++] = i;
        }
        shuffle(items, itemCount, rng);

        for (s32 index = 0; index < itemCount; index++) {
            const s32 item = items[index];
            s32 compatible[LD::sMoonCount];
            s32 compatibleCount = 0;
            for (s32 location = 0; location < LD::sMoonCount; location++) {
                if (unfilled[location]
                    && canPlaceMoonItemAtLocation(item, location))
                    compatible[compatibleCount++] = location;
            }
            const s32 location = pickEvenlyDistributedCompatibleLocation(
                item, compatible, compatibleCount, rng);
            if (location < 0)
                continue;
            sPlacementItemForLocation[location] = item;
            unfilled[location] = false;
            unplaced[item] = false;
            sProgressPlacedCount++;
            if ((sProgressPlacedCount & 31) == 0)
                reportPlacementProgress();
        }
        reportPlacementProgress();

        s32 leftoverItems = 0;
        s32 leftoverLocations = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            leftoverItems += unplaced[i] ? 1 : 0;
            leftoverLocations += unfilled[i] ? 1 : 0;
        }
        if (leftoverItems != 0 || leftoverLocations != 0)
            addWarning(
                "Moon pool/location mismatch during even fill: %d item(s), %d location(s).",
                leftoverItems, leftoverLocations);
    }

    static void buildPlacementListAndCreditMap() {
        sPlacementCount = 0;
        sCreditMapCount = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            sPlacementIndexForItem[i] = -1;
            sPlacementIndexForLocation[i] = -1;
        }

        for (s32 location = 0; location < LD::sMoonCount; location++) {
            const s32 item = sPlacementItemForLocation[location];
            if (!isValidMoon(location) || !isValidMoon(item))
                continue;

            sPlacementCritical[sPlacementCount] = sCriticalPathValid
                ? sCriticalPathLocation[location]
                : (itemHasProgressUnlock(item) || isMultiMoon(item));
            // Keep the first entry for a repeated identity, matching what the
            // linear scans these indices replace would have returned.
            if (sPlacementIndexForItem[item] < 0)
                sPlacementIndexForItem[item] = static_cast<s16>(sPlacementCount);
            if (sPlacementIndexForLocation[location] < 0)
                sPlacementIndexForLocation[location] = static_cast<s16>(sPlacementCount);
            sPlacements[sPlacementCount++] = { sMoonEntries[location], sMoonEntries[item] };
            sCreditMap[sCreditMapCount++] = {
                sMoonEntries[location]->uid,
                sMoonEntries[location]->stage,
                sMoonEntries[item],
            };
        }
    }

    static void resetPlacementState() {
        sKingdomHintStatusValid = false;
        sHintStatusFile = nullptr;
        sHintStatusShineSignature = -1;
        sHintStatusVisitedKingdoms = 0;
        sHintStatusCurrentWorldId = -1;
        sHintStatusRoleLayoutHash = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            sPlacementItemForLocation[i] = -1;
            sLocationSphere[i] = -1;
            sCriticalPathLocation[i] = false;
            sPlacementIndexForItem[i] = -1;
            sPlacementIndexForLocation[i] = -1;
        }
        sCriticalPathValid = false;
        std::memset(sAutoGrantItems, 0xFF, sizeof(sAutoGrantItems));
        sAutoGrantCount = 0;
        sPlacementCount = 0;
        sCreditMapCount = 0;
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        resetKingdomCreditLedger();
#endif
        sPathStepCount = 0;
        sWarningCount = 0;
        sFinalObjectiveReached = false;
        sFinalObjectiveNote[0] = '\0';
    }

    // Simulate a forward playthrough of the final placement: starting from the
    // given state, repeatedly collect every reachable location that has a placed
    // item until no new collection is possible. This brings the state up to date
    // with all tokens, moon counts, and scenario advances that a real player would
    // accumulate.
    static void simulateFinalPlacementReachability(LogicState& sim) {
        // A safety-net item is already in the player's inventory when the run
        // starts, so it must participate in the same state replay as configured
        // starting equipment before physical locations are walked.
        for (s32 i = 0; i < sAutoGrantCount; i++)
            collectItem(sim, sAutoGrantItems[i], nullptr, 0);

        bool consider[LD::sMoonCount];
        for (s32 loc = 0; loc < LD::sMoonCount; loc++)
            consider[loc] = sPlacementItemForLocation[loc] >= 0 || isStoryMoon(loc);

        bool changed = true;
        s32 guard = 0;
        const s32 maxIterations = LD::sMoonCount + 8;
        while (changed && guard++ < maxIterations) {
            changed = false;
            s32 reachable[LD::sMoonCount];
            const s32 count = reachableLocations(sim, consider, reachable);
            for (s32 i = 0; i < count; i++) {
                const s32 loc = reachable[i];
                if (sPlacementItemForLocation[loc] >= 0 || isStoryMoon(loc)) {
                    collectLocation(sim, loc, nullptr, 0);
                    consider[loc] = false;
                    changed = true;
                }
            }
            refreshReachableStateEvents(sim, nullptr, 0);

            // A route advance represents leaving the current kingdom. Exhaust
            // everything reachable in its current scenario first; otherwise the
            // On Kingdom Complete option can move it to postgame before a newly
            // acquired capture/ability opens another pre-peace location.
            if (!changed)
                changed = advanceKingdomOrder(sim);
        }
    }

    static bool hasAutoGrantItem(s32 item) {
        for (s32 i = 0; i < sAutoGrantCount; i++)
            if (sAutoGrantItems[i] == item)
                return true;
        return false;
    }

    static bool addAutoGrantItem(s32 item) {
        if (!isValidMoon(item) || hasAutoGrantItem(item))
            return true;
        if (sAutoGrantCount >= kMaxAutoGrants)
            return false;
        sAutoGrantItems[sAutoGrantCount++] = static_cast<s16>(item);
        return true;
    }

    static bool itemNeedsAutoGrant(s32 item, const LogicState& opening) {
        if (!itemHasProgressUnlock(item))
            return false;

        // Keys and paintings are independent rewards even when all of a
        // carrier's capture/ability tokens happen to be in the opener.
        if (moonUnlocksCloudPainting(item) || itemIsMoonRockKey(item)
            || itemIsSubareaKey(item))
            return true;

        for (s32 i = 0; i < sMoonUnlockTokenCounts[item]; i++) {
            const s32 token = sMoonUnlockTokens[item][i];
            if (token >= 0 && token < LD::sTokenCount && !opening.tokens[token])
                return true;
        }
        return false;
    }

    static void applyAutoGrantsToState(LogicState& state) {
        for (s32 i = 0; i < sAutoGrantCount; i++)
            collectItem(state, sAutoGrantItems[i], nullptr, 0);
    }

    // The final fill may leave a required carrier without a compatible physical
    // slot. Keep the ordinary bijection strict for everything else, but reserve
    // those missing carriers as start-of-run rewards. Kingdom-unlock carriers are
    // selected only until the replayed credited count reaches that kingdom's
    // requirement; capture/ability/key/painting carriers are always retained.
    static void buildAutoGrantList(const bool* unplaced) {
        if (!unplaced)
            return;

        LogicState opening;
        initState(opening);
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (unplaced[item] && itemNeedsAutoGrant(item, opening)
                && !addAutoGrantItem(item)) {
                addWarning("Auto-grant list exceeded its capacity.");
                break;
            }
        }

        LogicState replay;
        initState(replay);
        simulateFinalPlacementReachability(replay);

        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier)
                continue;
            for (s32 tierWorld = 0; tierWorld < tier->count; tierWorld++) {
                const s32 kingdom = tier->worlds[tierWorld];
                if (!isValidKingdom(kingdom))
                    continue;

                s32 shortfall = requirementForKingdom(kingdom) - replay.moonCounts[kingdom];
                if (shortfall <= 0)
                    continue;

                for (s32 item = 0; item < LD::sMoonCount && shortfall > 0; item++) {
                    if (!unplaced[item] || hasAutoGrantItem(item)
                        || assignedKingdomOf(item) != kingdom)
                        continue;
                    if (!addAutoGrantItem(item)) {
                        addWarning("Auto-grant list exceeded its capacity.");
                        break;
                    }
                    collectItem(replay, item, nullptr, 0);
                    shortfall -= moonValue(item);
                }

                // A newly granted carrier can unlock a physical location that
                // contributes to a later kingdom. Replay the same fixed point
                // before evaluating the next route boundary.
                simulateFinalPlacementReachability(replay);
                shortfall = requirementForKingdom(kingdom) - replay.moonCounts[kingdom];
                if (shortfall > 0)
                    addWarning(
                        "%s still lacks %d required moon value after auto-grants.",
                        WorldMeta::getDisplayNameForWorldId(kingdom), shortfall);
            }
        }

        if (sAutoGrantCount > 0) {
            addWarning(
                "%d required item(s) had no available location and will be granted at the start.",
                sAutoGrantCount);
            Randomizer::logPush(
                "%d items had no available locations and were granted at the start.",
                sAutoGrantCount);
        }
    }

    static bool validatePoolBijection() {
        bool seenItems[LD::sMoonCount] = {};
        s32 eligibleLocations = 0;
        s32 placedItems = 0;
        s32 emptyLocations = 0;
        for (s32 location = 0; location < LD::sMoonCount; location++) {
            const bool eligible = sMoonEntries[location] && isMoonPoolEligible(*sMoonEntries[location]);
            const s32 item = sPlacementItemForLocation[location];
            if (!eligible) {
                if (item >= 0) {
                    setFailure(FailureCode::DataError,
                        "Ineligible location %d received an item.", location);
                    return false;
                }
                continue;
            }

            eligibleLocations++;
            if (item == -1) {
                emptyLocations++;
                continue;
            }
            if (!isValidMoon(item) || !isMoonPoolEligible(*sMoonEntries[item])) {
                setFailure(FailureCode::DataError,
                    "Eligible location %d has no valid item.", location);
                return false;
            }
            if (seenItems[item]) {
                setFailure(FailureCode::DataError,
                    "Moon item %d was placed more than once.", item);
                return false;
            }
            if (!canPlaceMoonItemAtLocation(item, location)) {
                setFailure(FailureCode::CapacityFailure,
                    "Incompatible moon type at location %d for item %d.", location, item);
                return false;
            }
            seenItems[item] = true;
            placedItems++;
        }

        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!sMoonEntries[item] || !isMoonPoolEligible(*sMoonEntries[item]))
                continue;
            if (!seenItems[item]) {
                if (hasAutoGrantItem(item))
                    continue;
                setFailure(FailureCode::DataError,
                    "Moon item %d is missing from the placement.", item);
                return false;
            }
        }
        if (emptyLocations != sAutoGrantCount
            || eligibleLocations != placedItems + sAutoGrantCount) {
            setFailure(FailureCode::CapacityFailure,
                "Pool bijection mismatch: %d locations, %d items.",
                eligibleLocations, placedItems);
            return false;
        }
        return true;
    }

    // Collect everything reachable, optionally advancing the route as it goes.
    //
    // Pass allowRouteAdvance = false when the caller is *proving* one tier's
    // requirement. Otherwise the fixed point opens later kingdoms mid-proof and
    // the requirement is then satisfied by moons that only became reachable
    // after that advance - which is exactly how a route that the game can never
    // walk passes validation.
    static bool collectReachableToFixedPoint(
        LogicState& state, bool* remaining, bool allowRouteAdvance = true) {
        bool any = false;
        bool changed = true;
        s32 guard = 0;
        while (changed && guard++ < LD::sMoonCount + 16) {
            changed = false;
            s32 reachable[LD::sMoonCount];
            const s32 count = reachableLocations(state, remaining, reachable);
            if (count > 0) {
                for (s32 i = 0; i < count; i++) {
                    const s32 location = reachable[i];
                    if (remaining[location] && sPlacementItemForLocation[location] >= 0) {
                        collectLocation(state, location, nullptr, 0);
                        remaining[location] = false;
                        changed = true;
                        any = true;
                    }
                }
            }
            refreshReachableStateEvents(state, nullptr, 0);

            // Route advancement may irreversibly change a kingdom scenario when
            // On Kingdom Complete is enabled. Only perform that simulated
            // departure after the current scenario has no placements left to
            // collect, including locations opened by the previous collection pass.
            //
            // Deliberately NOT promoting to peace here when allowRouteAdvance is
            // false. A caller proving one tier walks a deliberately incomplete
            // state, so "nothing changed" means "nothing is collectable YET",
            // not "nothing ever will be" - and advanceCompletedKingdomToPeace
            // has no accessibility guard, so with items shuffled a kingdom's
            // counter can fill from locations on the other side of the world and
            // promote a kingdom the player has never set foot in. Bowser's moon
            // uid=332 exists ONLY at its scenario 1, so an early promotion
            // destroys it permanently. The promotion belongs at the tier
            // boundary, which is where construction does it and where
            // validateBranchOrder now does it too.
            if (!changed && allowRouteAdvance)
                changed = advanceKingdomOrder(state);
        }
        return any;
    }

    static bool stageAccessIsRooted(
        const LogicState& state,
        s32 stage,
        bool* visiting) {
        if (!isValidStage(stage) || visiting[stage])
            return false;

        visiting[stage] = true;
        const StageTrace& trace = state.stageTraces[stage];
        bool rooted = trace.kind == StageTrace_Odyssey;

        if (trace.kind == StageTrace_Entrance || trace.kind == StageTrace_Painting)
            rooted = stageAccessIsRooted(state, trace.sourceStage, visiting);

        // A small number of authored routes cross stage files inside one logical
        // kingdom (for example an interior that emerges into its overworld). Those
        // stages are recorded as inferred rather than entrance/painting access, so
        // inherit the root from the reachable side of that authored route.
        if (trace.kind == StageTrace_Inferred) {
            for (s32 edgeIndex = 0; edgeIndex < LD::sRouteEdgeCount && !rooted; edgeIndex++) {
                const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                if (edge.fromNode < 0 || edge.fromNode >= LD::sNodeCount
                    || edge.toNode < 0 || edge.toNode >= LD::sNodeCount)
                    continue;
                const s32 fromStage = LD::sNodes[edge.fromNode].stage;
                const s32 toStage = LD::sNodes[edge.toNode].stage;
                if (toStage == stage && fromStage != stage
                    && state.accessibleStages[fromStage])
                    rooted = stageAccessIsRooted(state, fromStage, visiting);
                if (!rooted && edge.bidirectional
                    && fromStage == stage && toStage != stage
                    && state.accessibleStages[toStage])
                    rooted = stageAccessIsRooted(state, toStage, visiting);
            }
        }

        visiting[stage] = false;
        return rooted;
    }

    static void computeRootedStageMap(
        const LogicState& state,
        bool* rootedStages) {
        std::memset(rootedStages, 0, sizeof(bool) * LD::sStageCount);

        bool changed = true;
        while (changed) {
            changed = false;
            for (s32 stage = 0; stage < LD::sStageCount; stage++) {
                if (rootedStages[stage] || !state.accessibleStages[stage])
                    continue;

                const StageTrace& trace = state.stageTraces[stage];
                bool rooted = trace.kind == StageTrace_Odyssey;
                if (!rooted
                    && (trace.kind == StageTrace_Entrance
                        || trace.kind == StageTrace_Painting)
                    && isValidStage(trace.sourceStage))
                    rooted = rootedStages[trace.sourceStage];

                if (!rooted && trace.kind == StageTrace_Inferred) {
                    for (s32 edgeIndex = 0;
                         edgeIndex < LD::sRouteEdgeCount && !rooted;
                         edgeIndex++) {
                        const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
                        if (edge.fromNode < 0 || edge.fromNode >= LD::sNodeCount
                            || edge.toNode < 0 || edge.toNode >= LD::sNodeCount)
                            continue;
                        const s32 fromStage = LD::sNodes[edge.fromNode].stage;
                        const s32 toStage = LD::sNodes[edge.toNode].stage;
                        if (toStage == stage && fromStage != stage
                            && isValidStage(fromStage)
                            && rootedStages[fromStage])
                            rooted = true;
                        if (!rooted && edge.bidirectional
                            && fromStage == stage && toStage != stage
                            && isValidStage(toStage)
                            && rootedStages[toStage])
                            rooted = true;
                    }
                }

                if (rooted) {
                    rootedStages[stage] = true;
                    changed = true;
                }
            }
        }
    }

    // Why the last validateBranchOrder call rejected. The attempt record only
    // ever carried "Forward validation failed for branch-order mask N", and the
    // detail behind it went to logPush - which reaches a debug-string sink no
    // on-disk log captures, so a rejected seed was undiagnosable from the
    // spoiler log alone. Recorded here and folded into the failure message.
    static char sBranchValidationNote[192] = {};

    static void setBranchValidationNote(const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(sBranchValidationNote, sizeof(sBranchValidationNote), fmt, args);
        va_end(args);
    }

    static bool allAccessibleKingdomsHaveRootedAccess(const LogicState& state) {
        for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
            if (!state.accessibleKingdoms[kingdom])
                continue;
            const s32 stage = LD::sMainStageByKingdom[kingdom];
            bool visiting[LD::sStageCount] = {};
            if (!stageAccessIsRooted(state, stage, visiting)) {
                setBranchValidationNote(
                    "%s access not rooted in an overworld entrance (stage %s)",
                    WorldMeta::getDisplayNameForWorldId(kingdom),
                    isValidStage(stage) ? LD::sStages[stage].stage : "???");
                Randomizer::logPush("Unrooted kingdom access: %s", sBranchValidationNote);
                return false;
            }
        }
        return true;
    }

    // Failure diagnostics for validateBranchOrder: name every kingdom in the tier
    // whose requirement is unmet so a rejected attempt is self-explanatory in logs.
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
    // Names the FIRST gate in isLocationReachable that rejects this location.
    // Deliberately mirrors that function's order: the first failure is the one
    // worth reporting, and a list of every unmet condition would bury it.
    //
    // Keep the two in step. A gate added there and not here reports as
    // "reachable" from a caller that just proved otherwise, which is a worse
    // diagnostic than none - hence the fallthrough text at the bottom.
    static const char* explainLocationUnreachable(
        const LogicState& state, const bool* reachableNodes, s32 location,
        char* out, size_t outSize) {
        if (!isValidMoon(location))
            return "not a valid moon location";

        const LD::MoonAccess& moon = LD::sMoons[location];
        const s16 scenario = isValidKingdom(moon.kingdom)
            ? state.kingdomScenarios[moon.kingdom]
            : (s16)-1;

        if (!hasStageAccessForLocation(state, reachableNodes, location)) {
            bool visiting[LD::sStageCount] = {};
            const bool rooted = isValidStage(moon.stageIndex)
                && stageAccessIsRooted(state, moon.stageIndex, visiting);
            nn::util::SNPrintf(out, outSize, "no stage access (accessible=%d, rooted=%d)",
                isValidStage(moon.stageIndex)
                    ? (int)state.accessibleStages[moon.stageIndex]
                    : 0,
                (int)rooted);
            return out;
        }
        if (moon.isPrincessPeach && !hasToken(state, LD::sMushroomReachedToken))
            return "Princess Peach chain needs Mushroom Kingdom reached";
        if (moon.storyOrder >= 0 && isValidKingdom(moon.kingdom)
            && state.storyCounts[moon.kingdom] < moon.storyOrder) {
            nn::util::SNPrintf(out, outSize, "story order %d, kingdom has %d",
                (s32)moon.storyOrder, (s32)state.storyCounts[moon.kingdom]);
            return out;
        }

        const bool promoted
            = ScenarioPatcher::getRequiredScenarioForRandomizedMultiPrizeSubarea(moon.stage) > 0;
        if (!promoted && moon.scenarioMask != 0 && isValidKingdom(moon.kingdom)
            && !scenarioMaskContains(moon.scenarioMask, scenario)) {
            nn::util::SNPrintf(out, outSize, "scenario mask 0x%X excludes scenario %d",
                moon.scenarioMask, (s32)scenario);
            return out;
        }
        if (!promoted && moon.scenarioFloor >= 0 && isValidKingdom(moon.kingdom)
            && scenario < moon.scenarioFloor) {
            nn::util::SNPrintf(out, outSize, "scenario floor %d, kingdom at %d",
                (s32)moon.scenarioFloor, (s32)scenario);
            return out;
        }

        const bool stageHasRoutes
            = isValidStage(moon.stageIndex) && LD::sStages[moon.stageIndex].hasRoutes;
        if (stageHasRoutes
            && (moon.fromNode < 0 || moon.fromNode >= LD::sNodeCount
                || !reachableNodes[moon.fromNode])) {
            nn::util::SNPrintf(out, outSize, "host node %s not reached",
                moon.fromNode >= 0 && moon.fromNode < LD::sNodeCount
                    ? LD::sNodes[moon.fromNode].nodeId
                    : "(none)");
            return out;
        }
        if (!anyRequirementOptionScenarioAllowed(moon.localRequirement,
                moon.localOptionScenarioOffset, moon.localOptionScenarioCount, scenario)) {
            nn::util::SNPrintf(out, outSize,
                "no local requirement option allowed in scenario %d", (s32)scenario);
            return out;
        }
        if (sNoLogicFill)
            return "reachable under No Logic (caller disagrees)";
        if (!moonRequirementSatisfiedForScenario(state, moon.stateRequirement, 0, 0,
                scenario, moon, true, reachableNodes))
            return "state requirement unmet (progression/tokens)";
        if (!moonRequirementSatisfiedForScenario(state, moon.localRequirement,
                moon.localOptionScenarioOffset, moon.localOptionScenarioCount, scenario,
                moon, false, reachableNodes))
            return "local requirement unmet (movement/capture)";

        return "no gate rejected it - explainLocationUnreachable is out of step "
               "with isLocationReachable";
    }
#endif

    // Which moons crediting `kingdom` the forward replay still could not
    // collect. Construction placed all of them at locations it believed were
    // reachable, so every line here is a place the two reachability models
    // disagree - which is the only thing worth knowing about a validation
    // rejection, and exactly what the bare "moons 8/12" counter withheld.
    static void logUncollectedCredit(
        const LogicState& state, const bool* remaining, s32 kingdom, u32 mask) {
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
        // What construction actually banked for this kingdom, so the shortfall
        // can be diffed rather than guessed at.
        const s32 recorded = sKingdomCreditCount[kingdom];
        Randomizer::logPush(
            "branch-validate mask=%u: %s credit banked during construction: %d%s",
            mask, WorldMeta::getDisplayNameForWorldId(kingdom), recorded,
            recorded > kMaxCreditRecord ? " (list truncated)" : "");
        bool reachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(const_cast<LogicState&>(state), reachableNodes);
        for (s32 i = 0; i < recorded && i < kMaxCreditRecord; i++) {
            const CreditRecord& record = sKingdomCredit[kingdom][i];
            const s32 location = record.location;
            if (!isValidMoon(location))
                continue;
            const s32 stage = LD::sMoons[location].stageIndex;
            const char* stageName = isValidStage(stage) ? LD::sStages[stage].stage : "?";
            if (!remaining[location]) {
                Randomizer::logPush(
                    "    [%s t%d] %s val=%d ran=%d (sphere %d): replay COLLECTED",
                    diagPhaseName(record.phase), (s32)record.tier, stageName,
                    (s32)record.value, (s32)record.runningCount,
                    sLocationSphere[location]);
                continue;
            }
            char why[128];
            why[0] = '\0';
            const char* reason = explainLocationUnreachable(
                state, reachableNodes, location, why, sizeof(why));
            Randomizer::logPush(
                "    [%s t%d] %s val=%d ran=%d (sphere %d): replay REJECTED - %s",
                diagPhaseName(record.phase), (s32)record.tier, stageName,
                (s32)record.value, (s32)record.runningCount,
                sLocationSphere[location], reason ? reason : "?");
        }
#endif
        s32 shown = 0;
        for (s32 location = 0; location < LD::sMoonCount && shown < 6; location++) {
            if (!remaining[location])
                continue; // already collected by the replay
            const s32 item = sPlacementItemForLocation[location];
            if (!isValidMoon(item) || assignedKingdomOf(item) != kingdom)
                continue;
            const s32 stage = LD::sMoons[location].stageIndex;
            Randomizer::logPush(
                "branch-validate mask=%u: %s credit stranded at %s (sphere %d)",
                mask, WorldMeta::getDisplayNameForWorldId(kingdom),
                isValidStage(stage) ? LD::sStages[stage].stage : "?",
                sLocationSphere[location]);
            shown++;
        }
        if (shown == 0) {
            Randomizer::logPush(
                "branch-validate mask=%u: %s has no uncollected credit left -"
                " the shortfall is the requirement itself, not reachability",
                mask, WorldMeta::getDisplayNameForWorldId(kingdom));
        }
    }

    static void logTierIncomplete(const LogicState& state, s32 tierIndex, u32 mask) {
        const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
        if (!tier)
            return;
        for (s32 i = 0; i < tier->count; i++) {
            const s32 kingdom = tier->worlds[i];
            if (!isValidKingdom(kingdom) || isKingdomRequirementMet(state, kingdom))
                continue;
            Randomizer::logPush(
                "branch-validate mask=%u: %s unmet (moons %d/%d, accessible=%d)",
                mask, WorldMeta::getDisplayNameForWorldId(kingdom),
                state.moonCounts[kingdom], requirementForKingdom(kingdom),
                (int)state.accessibleKingdoms[kingdom]);
        }
    }

    static bool validateBranchOrder(u32 branchOrderMask) {
        sBranchValidationNote[0] = '\0';
        LogicState state;
        initState(state);
        applyAutoGrantsToState(state);
        refreshReachableStateEvents(state, nullptr, 0);

        bool remaining[LD::sMoonCount];
        for (s32 location = 0; location < LD::sMoonCount; location++)
            remaining[location] = sPlacementItemForLocation[location] >= 0;

        s32 branchTier = 0;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier || tier->count <= 0)
                continue;
            if (tierIndex > 0 && !routePrefixComplete(state, tierIndex - 1)) {
                setBranchValidationNote(
                    "route tier %d incomplete when tier %d opens",
                    tierIndex - 1, tierIndex);
                logTierIncomplete(state, tierIndex - 1, branchOrderMask);
                return false;
            }

            s32 order[2] = { tier->worlds[0], tier->count > 1 ? tier->worlds[1] : -1 };
            if (tier->count == 2 && ((branchOrderMask >> branchTier) & 1u)) {
                const s32 tmp = order[0];
                order[0] = order[1];
                order[1] = tmp;
            }
            if (tier->count == 2)
                branchTier++;

            for (s32 i = 0; i < tier->count; i++) {
                const s32 kingdom = order[i];
                if (!isValidKingdom(kingdom)) {
                    setBranchValidationNote(
                        "route tier %d slot %d invalid kingdom %d",
                        tierIndex, i, kingdom);
                    return false;
                }
                unlockKingdom(state, kingdom);
                // This walk already unlocks each tier in route order, so the
                // fixed point must not advance the route on its own: the
                // requirement below has to be proven against exactly the
                // unlocks the game would have granted at this point.
                collectReachableToFixedPoint(state, remaining, false);
                if (!allAccessibleKingdomsHaveRootedAccess(state))
                    return false;
                if (!isKingdomRequirementMet(state, kingdom)) {
                    setBranchValidationNote(
                        "%s short of requirement (%d/%d moons collectable)",
                        WorldMeta::getDisplayNameForWorldId(kingdom),
                        state.moonCounts[kingdom], requirementForKingdom(kingdom));
                    Randomizer::logPush(
                        "branch-validate mask=%u: %s unmet after unlock (moons %d/%d)",
                        branchOrderMask, WorldMeta::getDisplayNameForWorldId(kingdom),
                        state.moonCounts[kingdom], requirementForKingdom(kingdom));
                    logUncollectedCredit(state, remaining, kingdom, branchOrderMask);
                    return false;
                }

                // The tier boundary: this kingdom has proved its requirement and
                // the route now leaves it, which under On Kingdom Complete is
                // exactly what promotes it to post-peace. Construction does this
                // in the same place (see the two advanceCompletedKingdomToPeace
                // calls in runBufferManagedSpherePlacement), and a validator that
                // skipped it kept every kingdom at its opening scenario forever -
                // which then reported perfectly reachable later-scenario
                // locations as unreachable and rejected sound placements.
                //
                // Per kingdom and only at its own boundary, never a global sweep:
                // promotion is irreversible and can gate off a location that
                // exists only at the earlier scenario.
                if (advanceCompletedKingdomToPeace(state, kingdom))
                    collectReachableToFixedPoint(state, remaining, false);
            }
        }

        collectReachableToFixedPoint(state, remaining);
        if (!allAccessibleKingdomsHaveRootedAccess(state))
            return false;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            if (!isRouteTierComplete(state, tierIndex)) {
                setBranchValidationNote(
                    "route tier %d never completes", tierIndex);
                logTierIncomplete(state, tierIndex, branchOrderMask);
                return false;
            }
        }

        unlockKingdom(state, WorldMeta::WorldId_Mushroom);
        collectReachableToFixedPoint(state, remaining);
        if (!allAccessibleKingdomsHaveRootedAccess(state))
            return false;

        LogicState initialState;
        initState(initialState);
        applyAutoGrantsToState(initialState);
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!itemHasProgressUnlock(item))
                continue;
            bool alreadyStarted = sMoonUnlockTokenCounts[item] > 0
                && !moonUnlocksCloudPainting(item);
            for (s32 i = 0; i < sMoonUnlockTokenCounts[item]; i++) {
                if (!hasToken(initialState, sMoonUnlockTokens[item][i])) {
                    alreadyStarted = false;
                    break;
                }
            }
            if (!alreadyStarted && !state.collectedItems[item]) {
                s32 placedAt = -1;
                for (s32 location = 0; location < LD::sMoonCount; location++) {
                    if (sPlacementItemForLocation[location] == item) {
                        placedAt = location;
                        break;
                    }
                }
                setBranchValidationNote(
                    "unlock '%s' never collectable (placed at %s)",
                    sMoonEntries[item]->name,
                    placedAt >= 0 ? sMoonEntries[placedAt]->name : "nowhere");
                Randomizer::logPush(
                    "branch-validate mask=%u: unlock item uid=%d '%s' uncollected (placed at uid=%d %s)",
                    branchOrderMask, sMoonEntries[item]->uid, sMoonEntries[item]->name,
                    placedAt >= 0 ? sMoonEntries[placedAt]->uid : -1,
                    placedAt >= 0 ? sMoonEntries[placedAt]->stage : "nowhere");
                return false;
            }
        }

        // No world-peace scenario check here, by design: peace follows from the
        // guarantees above (kingdoms reachable, every capture/ability collectable)
        // plus in-game story ordering. A replay-based peace loop rejects seeds
        // whenever a story sub-area sits behind an unreachable shuffled entrance,
        // even though the seed itself is completable.
        return true;
    }

    // Canary for the one thing a player can verify in thirty seconds: with only
    // the starting kingdom unlocked and no route advancement whatsoever, how many
    // of that kingdom's own requirement moons can actually be collected?
    //
    // The globe cannot leave route index 0 until its counter is full
    // (KingdomProgress::tryCompleteFrontier), so a seed that scores short here is
    // unplayable no matter what the rest of validation proved. Reported rather
    // than merely asserted, because when this and validateFinalPlacement disagree
    // the disagreement itself is the bug worth seeing.
    static s32 probeStartingKingdomMoons(s32& outRequirement) {
        const s32 start = KingdomRando::getStartingWorldId();
        outRequirement = isValidKingdom(start) ? requirementForKingdom(start) : 0;
        if (!isValidKingdom(start))
            return 0;

        LogicState state;
        initState(state);
        applyAutoGrantsToState(state);
        unlockKingdom(state, start);
        refreshReachableStateEvents(state, nullptr, 0);

        bool remaining[LD::sMoonCount];
        for (s32 location = 0; location < LD::sMoonCount; location++)
            remaining[location] = sPlacementItemForLocation[location] >= 0;

        collectReachableToFixedPoint(state, remaining, false);
        return state.moonCounts[start];
    }

    static void reportStartingKingdomProbe() {
        s32 requirement = 0;
        const s32 reachable = probeStartingKingdomMoons(requirement);
        const s32 start = KingdomRando::getStartingWorldId();
        Randomizer::logPush(
            "start-probe %s reachable=%d requirement=%d",
            WorldMeta::getDisplayNameForWorldId(start), reachable, requirement);
        if (reachable < requirement) {
            addWarning(
                "Only %d of %s's %d required moons are reachable before leaving it; "
                "the route cannot advance past the starting kingdom.",
                reachable, WorldMeta::getDisplayNameForWorldId(start), requirement);
        }
    }

    static bool validateFinalPlacement() {
        if (!validatePoolBijection())
            return false;

        s32 branchCount = 0;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (tier && tier->count == 2)
                branchCount++;
        }
        if (branchCount > 8) {
            setFailure(FailureCode::DataError,
                "Validator supports at most eight branch tiers; found %d.", branchCount);
            return false;
        }

        const u32 orderCount = 1u << branchCount;
        for (u32 order = 0; order < orderCount; order++) {
            if (validateBranchOrder(order))
                continue;
            setFailure(FailureCode::ValidationFailure,
                "Forward validation failed (branch order %u): %s.", order,
                sBranchValidationNote[0] ? sBranchValidationNote : "no reason recorded");
            return false;
        }
        return true;
    }

    // Confirms the chosen start still works against the FINAL wiring. Vanilla
    // returns true trivially (no start was randomized, or the entrance table is
    // the vanilla one the choice was already made against).
    // Seed Difficulty C4: how long the completion path should be, expressed as
    // a band on the fill sphere count.
    //
    // This is a SOFT preference and must stay one. An attempt whose sphere
    // count lands outside the band is re-rolled, but the last few attempts stop
    // enforcing it, so a seed is always produced - a difficulty level is a
    // request for a shape, never a reason to fail generation.
    //
    // The numbers are a starting calibration; re-derive them from a sweep of
    // logic/scratch/verify_beatability.py if the soft-reject rate gets high.
    static bool sphereCountInDifficultyBand(s32 sphereCount) {
        // DISABLED pending calibration, and deliberately left in place so the
        // numbers are recorded rather than rediscovered.
        //
        // The first cut used min {0,0,0,14,17} / max {9,11,0,0,0}. Measured fill
        // sphere counts are 70-91, so the Relaxed and Easy caps were ~8x too
        // low - unsatisfiable, rejecting 24 of the 32 attempts for nothing -
        // while the Hard and Brutal floors were satisfied by every seed and did
        // nothing at all. Worthless in both directions.
        //
        // Calibrating this needs a real distribution, not the three samples
        // that produced the range above; until someone has one, an uncalibrated
        // rejection criterion is strictly worse than none. Seed Difficulty
        // still shapes the seed through sphere depth (C1), unlock/gate
        // separation (C2), the movement tier (C3) and entrance distance (C5).
        (void)sphereCount;
        return true;
    }

    // Attempts stop enforcing the band once this many are left, so the soft
    // preference can never consume the whole budget.
    static constexpr s32 kSphereBandRelaxedAttempts = 8;
    static bool sEnforceSphereBand = false;
    // Frontier depth the last completed fill reached. This is the CONSTRUCTION
    // sphere counter, not the spoiler playthrough numbering - they measure
    // different things and only this one reflects how the fill was shaped.
    static s32 sLastFillSphereCount = 0;

    static bool revalidateStartNode() {
        if (!Randomizer::isEntranceRandoEnabled())
            return true;
        if (!StartPositionRando::isActive())
            return true;
        const s32 node = StartPositionRando::getStartNode();
        if (node < 0)
            return true; // the Odyssey start has no node and cannot be stranded
        const s32 kingdom = StartPositionRando::getStartKingdom();
        if (!isValidKingdom(kingdom))
            return true;
        const s32 mainStage = LD::sMainStageByKingdom[kingdom];
        if (!isValidStage(mainStage))
            return true;
        return canStartAtNode(node, mainStage);
    }

    // Kingdom Rando rebuilds the route on every attempt, so a failure naming
    // a kingdom is unreadable without the order that attempt actually used -
    // "Luncheon Kingdom buffer exhausted" means one thing at tier 0 and a
    // completely different one at tier 8.
    static void logRouteOrder(u32 attempt) {
        char line[160];
        s32 used = 0;
        line[0] = 0;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier || tier->count <= 0)
                continue;
            for (s32 i = 0; i < tier->count && used < (s32)sizeof(line) - 24; i++) {
                const char* name = WorldMeta::getDisplayNameForWorldId(tier->worlds[i]);
                used += nn::util::SNPrintf(line + used, sizeof(line) - used, "%s%.4s",
                    used == 0 ? "" : (i > 0 ? "|" : ">"), name ? name : "?");
            }
        }
        Randomizer::logPush("route attempt=%u start=%s order=%s", attempt,
            WorldMeta::getDisplayNameForWorldId(KingdomRando::getStartingWorldId()),
            line);
    }

    static bool runBufferManagedSpherePlacement(u32 seed, u32 attempt) {
        logRouteOrder(attempt);
        if (attempt == 31) {
            TraceBuf::enabled = true;
            TraceBuf::reset();
            TraceBuf::appendf("=== Buffer Sphere Trace (Seed %u) ===\n\n", seed);
            TraceBuf::appendf("Starting buffer-managed tier route loop...\n");
        } else {
            TraceBuf::enabled = false;
        }
        resetPlacementState();
        sSearchNodes = 0;
        sSearchBundlesTried = 0;
        sFailureCode = FailureCode::None;

        // An opening with NOTHING reachable is dead on arrival, and every
        // recovery lever the fill has needs something to stand on first: there
        // is no location to place into, no unlock bundle can be simulated, and
        // an entrance wiring needs a source door the player can already stand
        // at. Detect it here rather than paying a full fill to rediscover it.
        //
        // Kingdom Rando is what makes this reachable at all. It picks the
        // STARTING kingdom, and a few kingdoms open at a scenario in which
        // almost nothing of theirs exists yet - Luncheon and Metro each have
        // exactly one moon in their whole kingdom at their opening scenario,
        // and Cloud has none. Whether that single moon is takeable then depends
        // on the opener: under Starting Equipment = Scarce there are no
        // captures, chooseLogicStartingEquipment declines to widen the opening
        // by design, and the frontier is empty before a single placement.
        //
        // Rejecting the attempt re-rolls the kingdom order on attempt+1, which
        // is the existing recovery path - this only makes it cost microseconds
        // instead of a doomed second, and say why.
        {
            LogicState opening;
            initState(opening);
            if (countOpeningReachableLocations(opening) <= 0) {
                const s32 start = KingdomRando::getStartingWorldId();
                setFailure(
                    FailureCode::UnreachableObjective,
                    "%s opens with no reachable location (scenario %d, %s starting "
                    "equipment); re-rolling the route.",
                    WorldMeta::getDisplayNameForWorldId(start),
                    isValidKingdom(start) ? (s32)opening.kingdomScenarios[start] : -1,
                    Randomizer::getStartingEquipmentName(
                        Randomizer::getStartingEquipment()));
                Randomizer::logPush(
                    "opening frontier empty: start=%s attempt=%u",
                    WorldMeta::getDisplayNameForWorldId(start), attempt);
                return false;
            }
        }

        bool unfilled[LD::sMoonCount] = {};
        bool unplaced[LD::sMoonCount] = {};

        s32 eligibleCount = 0;
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            const ShineDB::ShineEntry* moon = sMoonEntries[i];
            if (!moon || !isMoonPoolEligible(*moon))
                continue;
            unfilled[i] = true;
            unplaced[i] = true;
            eligibleCount++;
        }

        if (eligibleCount <= 0) {
            setFailure(FailureCode::DataError, "No eligible moon locations were available.");
            addWarning("No eligible moon locations were available.");
            return false;
        }

        sProgressPlacedCount = 0;
        sProgressTotalCount = eligibleCount;
        std::memset(sUnlockSearchFailHash, 0, sizeof(sUnlockSearchFailHash));

        // The strict difficult-movement mask applies to construction only; it is
        // released below before validation, which must prove beatability with
        // every method the selected difficulty allows. On Casual or No Logic
        // there is nothing harder to require, so the axis no-ops entirely.
        sDifficultFillLevel = (sNoLogicFill
                                  || Randomizer::getLogicDifficulty()
                                      == Randomizer::LogicDifficulty_Casual)
            ? -1
            : seedDifficultyLevel();
        sCasualFallbackActive = false;
        sPacingCarrierSphere = -1;
        sPacingCarriersThisSphere = 0;

        LogicState state;
        initState(state);
        refreshReachableStateEvents(state, nullptr, 0);

        Randomizer::Rng rng = Randomizer::makeRng(seed ^ (attempt * 0x7F4A7C15u), Randomizer::RngStream::Logic);

        bool success = true;
        s32 currentSphere = 0;
        bool frontierInitialized = false;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
#if SMOR_FEATURE_LOGIC_DIAGNOSTICS
            sDiagTier = (s16)tierIndex;
#endif
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier || tier->count <= 0)
                continue;

            if (tier->count == 2) {
                if (tierIndex > 0 && !routePrefixComplete(state, tierIndex - 1)) {
                    setFailure(FailureCode::UnreachableObjective,
                        "Route tier %d was not complete before its branch.", tierIndex - 1);
                    addWarning("Could not prove branch access after route tier %d.", tierIndex - 1);
                    success = false;
                    break;
                }

                const LogicState baseState = state;
                LogicState branchStates[2] = { baseState, baseState };
                bool branchFrontiersInitialized[2] = {
                    frontierInitialized, frontierInitialized
                };
                const s32 baseSphere = currentSphere;
                for (s32 branchIndex = 0; branchIndex < 2; branchIndex++) {
                    const s32 kingdom = tier->worlds[branchIndex];
                    if (!isValidKingdom(kingdom)) {
                        setFailure(FailureCode::DataError,
                            "Invalid kingdom %d in branch tier %d.",
                            kingdom, tierIndex);
                        success = false;
                        break;
                    }

                    LogicState& branchState = branchStates[branchIndex];
                    unlockKingdom(branchState, kingdom);
                    refreshReachableStateEvents(branchState, nullptr, 0);
                    // Placements made while proving the sibling remain available if
                    // this branch can reach them through an earlier shuffled entrance.
                    collectPreplacedReachable(branchState);

                    s32 branchSphere = baseSphere;
                    bool& branchFrontierInitialized = branchFrontiersInitialized[branchIndex];
                    if (!runBufferForKingdom(
                            branchState, kingdom, unfilled, unplaced,
                            branchSphere, branchFrontierInitialized, rng)) {
                        addWarning(
                            "Buffer engine could not independently satisfy %s.",
                            WorldMeta::getDisplayNameForWorldId(kingdom));
                        success = false;
                        break;
                    }
                    // runBufferForKingdom stops at the departure requirement. Apply
                    // the optional postgame promotion at this real route boundary,
                    // after every placement used to satisfy the kingdom was taken.
                    if (advanceCompletedKingdomToPeace(branchState, kingdom))
                        refreshReachableStateEvents(branchState, nullptr, 0);
                    if (branchSphere > currentSphere)
                        currentSphere = branchSphere;
                }

                if (!success)
                    break;
                state = baseState;
                mergeStateProgress(state, branchStates[0]);
                mergeStateProgress(state, branchStates[1]);
                frontierInitialized = branchFrontiersInitialized[0]
                    || branchFrontiersInitialized[1];
                continue;
            }

            const s32 kingdom = tier->worlds[0];
            if (!isValidKingdom(kingdom)) {
                setFailure(FailureCode::DataError,
                    "Invalid kingdom %d in route tier %d.", kingdom, tierIndex);
                success = false;
                break;
            }
            if (tierIndex > 0 && !routePrefixComplete(state, tierIndex - 1)) {
                setFailure(FailureCode::UnreachableObjective,
                    "Route tier %d was not complete before %s.",
                    tierIndex - 1,
                    WorldMeta::getDisplayNameForWorldId(kingdom));
                success = false;
                break;
            }

            unlockKingdom(state, kingdom);
            refreshReachableStateEvents(state, nullptr, 0);
            collectPreplacedReachable(state);
            if (!runBufferForKingdom(
                    state, kingdom, unfilled, unplaced,
                    currentSphere, frontierInitialized, rng)) {
                addWarning(
                    "Buffer engine could not satisfy %s.",
                    WorldMeta::getDisplayNameForWorldId(kingdom));
                success = false;
                break;
            }
            if (advanceCompletedKingdomToPeace(state, kingdom))
                refreshReachableStateEvents(state, nullptr, 0);
        }

        // The completed main route opens Mushroom Kingdom. Its six authored rematch
        // grand moons become valid completion capacity for the unique multi unlocks.
        if (success && routePrefixComplete(state, KingdomRando::getTierCount() - 1)) {
            unlockKingdom(state, WorldMeta::WorldId_Mushroom);
            refreshReachableStateEvents(state, nullptr, 0);
            collectPreplacedReachable(state);
        }

        // Every remaining progress unlock carrier is placed and collected forward.
        // This tail keeps checking for useful sphere expansions, but distributes
        // locations evenly rather than using the critical-path depth weights.
        if (success && !placeRemainingProgressEvenly(state, unfilled, unplaced, currentSphere, frontierInitialized, rng))
            success = false;

        // The fill is done choosing doors. Everything it never got to is paired
        // now, which is where the door graph stops being partial: from here on
        // the reachability model sees the finished table, exactly as the
        // runtime and every post-generation consumer will.
        if (Randomizer::isEntranceRandoEnabled())
            EntranceRando::finalizeIncrementalBuild();

        // Repair unreachable sub-areas HERE, before anything reads the wiring.
        // repairUnreachableSubAreas() rewrites the entrance table, so it has to run
        // ahead of revalidateStartNode(), the final replay and validateFinalPlacement()
        // - otherwise those all judge a table that no longer exists, and a repair that
        // makes the objective reachable never gets noticed.
        //
        // Unreachable sub-areas are a WARNING, never a rejection.
        //
        // This was briefly a hard check, on the theory that an incremental
        // builder could drive the residue to zero. Measurement says otherwise:
        // a vanilla, completely unshuffled topology already leaves 6 of 124
        // randomizable links unreachable, and a shuffled one 9-19. So the
        // residue is the NORM, and rejecting on it rejected every attempt -
        // generation failed outright rather than producing a seed. The repair
        // pass inside still runs and still helps; its result just does not get
        // a veto. This matches the host solver, which never enforced it, and
        // the original rationale: an isolated island nobody can reach does not
        // prevent completing the game.
        allEntranceDestinationsReachable(state);

        // The start was chosen against the vanilla stand-in (see canStartAtNode);
        // now that the real wiring exists, prove the player can still walk back
        // out. Wiring is not monotone against vanilla - a door that led home
        // before may lead elsewhere now - so this genuinely has to be re-run.
        if (success && !revalidateStartNode()) {
            // Do not keep a start the player cannot walk out of. Rejecting the
            // whole attempt over it is too expensive, so take the same fallback
            // a rebuild whose every probe failed takes: the ordinary Odyssey
            // drop-off in the same kingdom, which is always walkable.
            //
            // This has to happen HERE, before the final replay and
            // validateFinalPlacement: both re-seed from
            // applyRandomizedStartPosition, so they judge the demoted start and
            // reject the attempt if the fill's proof actually depended on the
            // node that just went away.
            StartPositionRando::demoteToVanillaDropoff();
            addWarning(
                "The randomized starting position could not reach its kingdom overworld "
                "under the wiring this attempt produced; fell back to the vanilla drop-off.");
        }

        // Snapshot the critical path before evenly distributing ordinary leftovers.
        for (s32 location = 0; location < LD::sMoonCount; location++)
            sCriticalPathLocation[location] = sPlacementItemForLocation[location] >= 0;
        sCriticalPathValid = true;

        fillRemainingEvenly(unfilled, unplaced, rng);
        buildAutoGrantList(unplaced);

        // Construction is done: validation and every later consumer (spoiler
        // replay, tracker, hints) evaluate with every allowed method again, so a
        // seed that needed the casual fallback still validates as beatable.
        sDifficultFillLevel = -1;
        sCasualFallbackActive = false;

        const s32 finalKingdom = routeKingdomAt(routeKingdomCount() - 1);
        const s32 finalRequirement = requirementForKingdom(finalKingdom);

        // Replay the complete placement so cleanup moons and physical story effects
        // are represented before objective and world-peace validation.
        LogicState finalState = state;
        simulateFinalPlacementReachability(finalState);

        const s32 finalMoonCount = isValidKingdom(finalKingdom) ? finalState.moonCounts[finalKingdom] : 0;
        bool finalReachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(finalState, finalReachableNodes);
        const bool chapelReached = LD::sFinalObjectiveNode >= 0
            && LD::sFinalObjectiveNode < LD::sNodeCount
            && finalReachableNodes[LD::sFinalObjectiveNode];
        sFinalObjectiveReached = isValidKingdom(finalKingdom)
            && finalState.accessibleKingdoms[finalKingdom]
            && finalMoonCount >= finalRequirement
            && chapelReached;
        if (sFinalObjectiveReached) {
            if (finalRequirement > 0) {
                nn::util::SNPrintf(sFinalObjectiveNote, sizeof(sFinalObjectiveNote),
                    "%s is reachable, %d local moons are proven, and Wedding Hall Checkpoint (obj1348) is reachable.",
                    WorldMeta::getDisplayNameForWorldId(finalKingdom), finalRequirement);
            } else {
                nn::util::SNPrintf(sFinalObjectiveNote, sizeof(sFinalObjectiveNote),
                    "%s and Wedding Hall Checkpoint (obj1348) are reachable after the suggested path.",
                    WorldMeta::getDisplayNameForWorldId(finalKingdom));
            }
        } else {
            if (isValidKingdom(finalKingdom)
                && finalState.accessibleKingdoms[finalKingdom]
                && finalMoonCount >= finalRequirement
                && !chapelReached) {
                nn::util::SNPrintf(sFinalObjectiveNote, sizeof(sFinalObjectiveNote),
                    "Moon Kingdom access was proven, but Wedding Hall Checkpoint (obj1348) was not reachable.");
            } else if (isValidKingdom(finalKingdom) && finalState.accessibleKingdoms[finalKingdom]) {
                nn::util::SNPrintf(sFinalObjectiveNote, sizeof(sFinalObjectiveNote),
                    "%s is reachable, but only %d/%d local moons were proven for its progression requirement.",
                    WorldMeta::getDisplayNameForWorldId(finalKingdom),
                    finalMoonCount,
                    finalRequirement);
            } else {
                nn::util::SNPrintf(sFinalObjectiveNote, sizeof(sFinalObjectiveNote),
                    "%s access was not proven after the suggested path.",
                    WorldMeta::getDisplayNameForWorldId(finalKingdom));
            }
            if (sFailureCode == FailureCode::None)
                setFailure(FailureCode::UnreachableObjective, "%s", sFinalObjectiveNote);
            success = false;
        }

        // No dedicated world-peace simulation here, by design. Construction collects
        // locations in sphere order, so the construction state can hold story beats
        // "burned" out of story order (collected without story credit) — a pure
        // modeling artifact a real player can never hit, which made a post-fill
        // peace sim reject beatable seeds. Peace attainability already follows from
        // what validateFinalPlacement proves on a fresh replay: every kingdom
        // reachable, every capture/ability collectable, and the chapel objective
        // reachable — with those, the location-driven story chains (and therefore
        // world peace) complete naturally in story order in-game.

        if (success)
            SeedGenerator::reportProgress(80, "Validating placement");
        buildPlacementListAndCreditMap();
        if (success && !finalizeCloudPaintingAssignments(finalState))
            success = false;
        if (success && !validateFinalPlacement())
            success = false;
        if (success)
            reportStartingKingdomProbe();

        sLastFillSphereCount = currentSphere;

        Randomizer::logPush(
            "buffer-sphere attempt=%u frontier=%d search_nodes=%d bundles=%d success=%d "
            "placements=%d wired=%d",
            attempt, currentSphere, sSearchNodes, sSearchBundlesTried,
            (int)success, sPlacementCount, sWireStepCount);
        return success;
    }

    // A location is worth collecting on the optimized path only when it still serves
    // a purpose: its item counts toward an unmet route-kingdom requirement, its item
    // carries a capture/ability/cloud-painting unlock, or the location itself drives
    // story/scenario progression. Anything else (e.g. Dark Side / Mushroom filler
    // items with their locks disabled) is skipped — extra moons only cost time.
    static bool isLocationUsefulForOptimizedPath(s32 location, s32 item, const LogicState& state) {
        if (!isValidMoon(location) || !isValidMoon(item))
            return false;

        if (itemHasProgressUnlock(item))
            return true;

        // Story locations advance their kingdom's scenario, world peace and the
        // moon rock; chase them when world peace is required.
        const LD::MoonAccess& loc = LD::sMoons[location];
        if (isStoryMoon(location) && isValidKingdom(loc.kingdom)
            && Randomizer::isWorldPeaceRequirementEnabled()
            && LD::sStoryTotalsByKingdom[loc.kingdom] > 0
            && state.storyCounts[loc.kingdom] < LD::sStoryTotalsByKingdom[loc.kingdom])
            return true;
        if (loc.scenarioMoveTo >= 0 && isValidKingdom(loc.kingdom)
            && state.kingdomScenarios[loc.kingdom] < loc.scenarioMoveTo)
            return true;

        // Items matter only for kingdoms on the Odyssey route with an unmet requirement.
        const s32 itemKingdom = assignedKingdomOf(item);
        if (isValidKingdom(itemKingdom) && KingdomRando::findTierIndex(itemKingdom) >= 0
            && state.moonCounts[itemKingdom] < requirementForKingdom(itemKingdom))
            return true;

        // Once Mushroom Kingdom is physically reached, one of its moons enables the
        // Princess Peach chain.
        if (LD::sMushroomReachedToken >= 0 && state.tokens[LD::sMushroomReachedToken]
            && itemKingdom == WorldMeta::WorldId_Mushroom
            && state.moonCounts[WorldMeta::WorldId_Mushroom] == 0
            && !LD::sMoons[item].isPrincessPeach)
            return true;

        return false;
    }

    static void recordOptimizedPlacement(
        LogicState& state,
        s32 location,
        s32 item,
        s32 sphere,
        s32 hostKingdom) {

        PathStep* step = nullptr;
        if (sOptimizedPathStepCount < LD::sMoonCount) {
            step = &sOptimizedPathSteps[sOptimizedPathStepCount++];
            step->location = sMoonEntries[location];
            step->item = sMoonEntries[item];
            step->locationStageIdx = LD::sMoons[location].stageIndex;
            step->locationKingdomIdx = LD::sMoons[location].kingdom;
            step->hostKingdomIdx = hostKingdom;
            step->reason = "optimized path";
            step->sphere = sphere;
            step->unlockedTokenCount = 0;
            step->travelStepCount = 0;
            step->scenarioAdvanceKingdom = -1;
            step->scenarioAdvanceTo = -1;
            step->scenarioAdvanceIsMoonRock = false;
            step->isStoryStep = isStoryMoon(location);
            step->grantedWorldPeace = false;
            for (s32 i = 0; i < kMaxUnlockTokensPerMoon; i++)
                step->unlockedTokens[i] = nullptr;
            buildTravelForLocation(state, location, *step);
        }

        const s32 locationKingdom = LD::sMoons[location].kingdom;
        const s16 scenarioBefore = isValidKingdom(locationKingdom) ? state.kingdomScenarios[locationKingdom] : -1;

        s16 unlockedTokens[kMaxUnlockTokensPerMoon];
        s32 unlockedCount = collectLocation(state, location, unlockedTokens, kMaxUnlockTokensPerMoon);

        if (step) {
            step->unlockedTokenCount = unlockedCount;
            for (s32 i = 0; i < unlockedCount; i++) {
                if (unlockedTokens[i] >= 0 && unlockedTokens[i] < LD::sTokenCount) {
                    step->unlockedTokens[i] = LD::sTokens[unlockedTokens[i]].key;
                    if (isValidKingdom(locationKingdom) && unlockedTokens[i] == LD::sWorldPeaceTokenByKingdom[locationKingdom])
                        step->grantedWorldPeace = true;
                }
            }
            if (isValidKingdom(locationKingdom) && state.kingdomScenarios[locationKingdom] > scenarioBefore) {
                step->scenarioAdvanceKingdom = (s16)locationKingdom;
                step->scenarioAdvanceTo = state.kingdomScenarios[locationKingdom];
            }
        }
        // Kingdom unlocks happen at explicit Odyssey deposit steps, not implicitly
        // when moon counts are met (see generateOptimizedPlaythrough).
    }

    static KingdomAccessKind accessKindFromTrace(StageTraceKind kind) {
        switch (kind) {
        case StageTrace_Odyssey:
            return KingdomAccessKind::Odyssey;
        case StageTrace_Entrance:
            return KingdomAccessKind::Entrance;
        case StageTrace_Painting:
            return KingdomAccessKind::Painting;
        default:
            return KingdomAccessKind::Inferred;
        }
    }

    static KingdomAccessKind accessKindForKingdom(
        const LogicState& state, s32 kingdom, s32 startingKingdom) {
        if (kingdom == startingKingdom)
            return KingdomAccessKind::Start;
        const s32 mainStage = isValidKingdom(kingdom) ? LD::sMainStageByKingdom[kingdom] : -1;
        if (!isValidStage(mainStage))
            return KingdomAccessKind::Inferred;
        return accessKindFromTrace(state.stageTraces[mainStage].kind);
    }

    static s16 viaStageForStage(const LogicState& state, s32 stage, KingdomAccessKind access) {
        if ((access == KingdomAccessKind::Painting || access == KingdomAccessKind::Entrance)
            && isValidStage(stage))
            return state.stageTraces[stage].sourceStage;
        return -1;
    }

    // The overworld loading-zone link the player used to reach this stage (for
    // entrance access). Its vanilla sub-area names the specific entrance in the
    // spoiler. -1 when not reached through an entrance.
    static s16 viaEntranceForStage(const LogicState& state, s32 stage, KingdomAccessKind access) {
        if (access == KingdomAccessKind::Entrance && isValidStage(stage))
            return state.stageTraces[stage].sourceEntrance;
        return -1;
    }

    static s16 entranceChainFirstLinkForStage(
        const LogicState& state, s32 stage, KingdomAccessKind access) {
        if (access == KingdomAccessKind::Entrance && isValidStage(stage))
            return state.stageTraces[stage].entranceChainFirstLink;
        return -1;
    }

    static u8 entranceChainFirstKindForStage(
        const LogicState& state, s32 stage, KingdomAccessKind access) {
        if (access == KingdomAccessKind::Entrance && isValidStage(stage))
            return state.stageTraces[stage].entranceChainFirstKind;
        return EntranceRando::ConnKind_None;
    }

    static bool stageWasKnownBeforeSphereLayer(
        const bool* prevKingdom,
        const bool* prevStage,
        s32 stage,
        s32 startingKingdom) {
        if (!isValidStage(stage))
            return true;

        const s32 kingdom = LD::sStages[stage].kingdom;
        if (isValidKingdom(kingdom) && stage == LD::sMainStageByKingdom[kingdom])
            return kingdom == startingKingdom || prevKingdom[kingdom];

        return prevStage[stage];
    }

    static bool accessSourceWasKnownBeforeSphereLayer(
        const LogicState& state,
        s32 stage,
        KingdomAccessKind access,
        const bool* prevKingdom,
        const bool* prevStage,
        s32 startingKingdom) {
        if (access != KingdomAccessKind::Entrance && access != KingdomAccessKind::Painting)
            return true;
        if (!isValidStage(stage))
            return true;

        const s32 sourceStage = state.stageTraces[stage].sourceStage;
        if (!isValidStage(sourceStage) || sourceStage == stage)
            return true;

        return stageWasKnownBeforeSphereLayer(prevKingdom, prevStage, sourceStage, startingKingdom);
    }

    // Stage provenance is presentation metadata, not part of logical reachability.
    // A shuffled entrance cycle can make every stage in the cycle point at another
    // stage that has not yet been displayed, even though the reachability graph has
    // already proven the cycle accessible from an earlier area. If a sphere has
    // logically reachable moons but the display-layer filter cannot admit any of
    // them, reveal their stages directly so the itinerary cannot deadlock on that
    // metadata cycle.
    static void revealReachableLocationStages(
        const LogicState& state,
        SphereInfo& info,
        bool* prevKingdom,
        bool* prevStage,
        const s32* reachable,
        s32 reachableCount,
        s32 startingKingdom) {

        for (s32 i = 0; i < reachableCount; i++) {
            const s32 location = reachable[i];
            if (!isValidMoon(location))
                continue;

            const s32 stage = LD::sMoons[location].stageIndex;
            if (!isValidStage(stage))
                continue;

            const s32 kingdom = LD::sStages[stage].kingdom;
            const bool isMainStage = isValidKingdom(kingdom)
                && stage == LD::sMainStageByKingdom[kingdom];
            if (isMainStage) {
                if (prevKingdom[kingdom])
                    continue;
                prevKingdom[kingdom] = true;
                if (info.areaCount >= kMaxSphereAreas)
                    continue;

                SphereArea& area = info.areas[info.areaCount++];
                area.kind = SphereAreaKind::Kingdom;
                area.kingdom = (s16)kingdom;
                area.scenario = state.kingdomScenarios[kingdom];
                area.stageIdx = (s16)stage;
                area.access = accessKindForKingdom(state, kingdom, startingKingdom);
                area.viaStageIdx = viaStageForStage(state, stage, area.access);
                area.viaEntranceIdx = viaEntranceForStage(state, stage, area.access);
                area.entranceChainFirstLink = entranceChainFirstLinkForStage(
                    state, stage, area.access);
                area.entranceChainFirstKind = entranceChainFirstKindForStage(
                    state, stage, area.access);
                continue;
            }

            if (prevStage[stage])
                continue;
            prevStage[stage] = true;
            if (info.areaCount >= kMaxSphereAreas)
                continue;

            SphereArea& area = info.areas[info.areaCount++];
            area.kind = SphereAreaKind::SubArea;
            area.kingdom = (s16)kingdom;
            area.scenario = isValidKingdom(kingdom)
                ? state.kingdomScenarios[kingdom]
                : (s16)-1;
            area.stageIdx = (s16)stage;
            area.access = accessKindFromTrace(state.stageTraces[stage].kind);
            area.viaStageIdx = viaStageForStage(state, stage, area.access);
            area.viaEntranceIdx = viaEntranceForStage(state, stage, area.access);
            area.entranceChainFirstLink = entranceChainFirstLinkForStage(
                state, stage, area.access);
            area.entranceChainFirstKind = entranceChainFirstKindForStage(
                state, stage, area.access);
        }
    }

    // Record the areas that opened this sphere relative to the previous one: newly
    // accessible kingdoms, newly accessible sub-areas, and kingdoms whose scenario
    // advanced. prev* arrays carry the running "already seen" state across spheres.
    static void snapshotSphereAreas(
        const LogicState& state, SphereInfo& info,
        bool* prevKingdom, bool* prevStage, s16* prevScenario, s32 startingKingdom) {
        info.areaCount = 0;

        // New kingdoms (overworld main stages).
        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            if (!state.accessibleKingdoms[k] || prevKingdom[k])
                continue;
            const KingdomAccessKind access = accessKindForKingdom(state, k, startingKingdom);
            if (!accessSourceWasKnownBeforeSphereLayer(
                    state, LD::sMainStageByKingdom[k], access,
                    prevKingdom, prevStage, startingKingdom))
                continue;
            prevKingdom[k] = true;
            if (info.areaCount < kMaxSphereAreas) {
                SphereArea& a = info.areas[info.areaCount++];
                a.kind = SphereAreaKind::Kingdom;
                a.kingdom = (s16)k;
                a.scenario = state.kingdomScenarios[k];
                a.stageIdx = (s16)LD::sMainStageByKingdom[k];
                a.access = access;
                a.viaStageIdx = viaStageForStage(state, LD::sMainStageByKingdom[k], a.access);
                a.viaEntranceIdx = viaEntranceForStage(state, LD::sMainStageByKingdom[k], a.access);
                a.entranceChainFirstLink = entranceChainFirstLinkForStage(
                    state, LD::sMainStageByKingdom[k], a.access);
                a.entranceChainFirstKind = entranceChainFirstKindForStage(
                    state, LD::sMainStageByKingdom[k], a.access);
            }
        }

        // New sub-areas (accessible non-main stages).
        for (s32 s = 0; s < LD::sStageCount; s++) {
            if (!state.accessibleStages[s] || prevStage[s])
                continue;
            const s32 kingdom = LD::sStages[s].kingdom;
            if (isValidKingdom(kingdom) && s == LD::sMainStageByKingdom[kingdom])
                continue; // the kingdom's overworld is reported as a Kingdom above
            const KingdomAccessKind access = accessKindFromTrace(state.stageTraces[s].kind);
            if (!accessSourceWasKnownBeforeSphereLayer(
                    state, s, access, prevKingdom, prevStage, startingKingdom))
                continue;
            prevStage[s] = true;
            if (info.areaCount < kMaxSphereAreas) {
                SphereArea& a = info.areas[info.areaCount++];
                a.kind = SphereAreaKind::SubArea;
                a.kingdom = (s16)kingdom;
                a.scenario = isValidKingdom(kingdom) ? state.kingdomScenarios[kingdom] : (s16)-1;
                a.stageIdx = (s16)s;
                a.access = access;
                a.viaStageIdx = viaStageForStage(state, s, a.access);
                a.viaEntranceIdx = viaEntranceForStage(state, s, a.access);
                a.entranceChainFirstLink = entranceChainFirstLinkForStage(state, s, a.access);
                a.entranceChainFirstKind = entranceChainFirstKindForStage(state, s, a.access);
            }
        }

        // Scenario advances in already-known kingdoms.
        for (s32 k = 0; k < LD::sKingdomCount; k++) {
            if (!state.accessibleKingdoms[k] || state.kingdomScenarios[k] <= prevScenario[k])
                continue;
            if (info.areaCount < kMaxSphereAreas) {
                SphereArea& a = info.areas[info.areaCount++];
                a.kind = SphereAreaKind::ScenarioBump;
                a.kingdom = (s16)k;
                a.scenario = state.kingdomScenarios[k];
                a.stageIdx = -1;
                a.access = KingdomAccessKind::Inferred;
                a.viaStageIdx = -1;
                a.viaEntranceIdx = -1;
                a.entranceChainFirstLink = -1;
                a.entranceChainFirstKind = EntranceRando::ConnKind_None;
            }
            prevScenario[k] = state.kingdomScenarios[k];
        }
    }

    // Open every route kingdom whose gating is satisfied: a tier's kingdoms become
    // valid Odyssey destinations once the previous tier's requirements are met, and
    // finishing the whole route opens Mushroom. Both kingdoms of a branch tier open
    // together (the player may take either first). Returns true if anything changed.
    static bool unlockRouteKingdomsReady(LogicState& state) {
        bool changed = false;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier || tier->count <= 0)
                continue;
            if (tierIndex > 0 && !routePrefixComplete(state, tierIndex - 1))
                continue;
            for (s32 i = 0; i < tier->count; i++) {
                const s32 kingdom = tier->worlds[i];
                changed |= unlockKingdom(state, kingdom);
                // A branch tier shows one side at a time. At an unresolved
                // branch the globe lists entries[frontier] only; the sibling
                // appears once the chosen side is finished (the player's pick is
                // swapped into that slot by KingdomProgress::commitBranchChoice).
                // Opening both at once made the itinerary hand out a kingdom the
                // map would not be showing yet. If this side turns out to be the
                // wrong pick, openStalledBranchSibling below opens the other.
                if (tier->count > 1 && !isKingdomRequirementMet(state, kingdom))
                    break;
            }
        }
        if (KingdomRando::getTierCount() > 0
            && routePrefixComplete(state, KingdomRando::getTierCount() - 1))
            changed |= unlockKingdom(state, WorldMeta::WorldId_Mushroom);
        return changed;
    }

    // The itinerary picks worlds[0] at every branch, but either side is a legal
    // choice and some seeds are only completable taking the second one first.
    // Call this when the replay has otherwise run dry: it re-opens the sibling of
    // the earliest branch that is stuck, which is exactly the decision a player
    // makes at the unlock prompt (getFrontierUnlockCandidates offers both).
    static bool openStalledBranchSibling(LogicState& state) {
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier || tier->count != 2)
                continue;
            if (tierIndex > 0 && !routePrefixComplete(state, tierIndex - 1))
                continue;

            for (s32 i = 0; i < 2; i++) {
                const s32 chosen = tier->worlds[i];
                const s32 sibling = tier->worlds[1 - i];
                if (!isValidKingdom(chosen) || !isValidKingdom(sibling))
                    continue;
                if (state.accessibleKingdoms[chosen]
                    && !isKingdomRequirementMet(state, chosen)
                    && !state.accessibleKingdoms[sibling]) {
                    if (unlockKingdom(state, sibling))
                        return true;
                }
            }
        }
        return false;
    }

    static bool optimizedPlaythroughComplete(LogicState& state) {
        const s32 finalKingdom = routeKingdomAt(routeKingdomCount() - 1);
        if (!isValidKingdom(finalKingdom)
            || !state.accessibleKingdoms[finalKingdom]
            || state.moonCounts[finalKingdom] < requirementForKingdom(finalKingdom))
            return false;

        bool reachableNodes[LD::sNodeCount];
        expandReachabilityFixedPoint(state, reachableNodes);
        if (LD::sFinalObjectiveNode < 0
            || LD::sFinalObjectiveNode >= LD::sNodeCount
            || !reachableNodes[LD::sFinalObjectiveNode])
            return false;

        LogicState initialState;
        initState(initialState);
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!itemHasProgressUnlock(item))
                continue;
            bool alreadyStarted = sMoonUnlockTokenCounts[item] > 0
                && !moonUnlocksCloudPainting(item);
            for (s32 i = 0; i < sMoonUnlockTokenCounts[item]; i++) {
                if (!hasToken(initialState, sMoonUnlockTokens[item][i])) {
                    alreadyStarted = false;
                    break;
                }
            }
            if (!alreadyStarted && !state.collectedItems[item])
                return false;
        }

        // No world-peace scenario check here, by design (matches validateBranchOrder):
        // the itinerary is complete when it reaches the objective and every unlock.
        // The suggested path still chases story moons when world peace is required
        // (see isLocationUsefulForOptimizedPath) — that's routing guidance, not a gate.
        return true;
    }

    // Build the suggested playthrough as accessibility spheres. Each sphere collects
    // everything reachable with only the previous spheres' unlocks; the moons that
    // matter for progress (gate requirements, captures, abilities, story/scenario
    // drivers) are recorded as path steps, while pure filler is silently collected so
    // reachability stays correct. This mirrors the forward replay proven by
    // validateBranchOrder, so it always terminates once every reachable placement is
    // taken.
    static void generateOptimizedPlaythrough() {
        sOptimizedPathStepCount = 0;
        sSphereCount = 0;

        LogicState state = {};
        initState(state);
        applyAutoGrantsToState(state);
        unlockRouteKingdomsReady(state);
        refreshReachableStateEvents(state, nullptr, 0);

        const s32 startingKingdom = KingdomRando::getStartingWorldId();

        bool prevKingdom[LD::sKingdomCount];
        bool prevStage[LD::sStageCount];
        s16 prevScenario[LD::sKingdomCount];
        for (s32 i = 0; i < LD::sKingdomCount; i++) {
            prevKingdom[i] = false;
            prevScenario[i] = state.kingdomScenarios[i];
        }
        for (s32 i = 0; i < LD::sStageCount; i++)
            prevStage[i] = false;

        while (sSphereCount < kMaxSpheres) {
            // Placed, not-yet-collected locations reachable with the current state.
            bool consider[LD::sMoonCount];
            for (s32 i = 0; i < LD::sMoonCount; i++)
                consider[i] = !state.collectedLocations[i] && sPlacementItemForLocation[i] >= 0;

            s32 logicalReachable[LD::sMoonCount];
            const s32 logicalReachableCount = reachableLocations(
                state, consider, logicalReachable);
            s32 reachable[LD::sMoonCount];
            std::memcpy(
                reachable,
                logicalReachable,
                sizeof(s32) * logicalReachableCount);
            s32 reachableCount = logicalReachableCount;

            SphereInfo& info = sSpheres[sSphereCount];
            info.index = sSphereCount;
            info.firstStepIndex = sOptimizedPathStepCount;
            info.routeUnlockCount = 0;
            snapshotSphereAreas(state, info, prevKingdom, prevStage, prevScenario, startingKingdom);

            // Stage provenance is presentation metadata, so it decides what gets
            // *announced*, never what gets taken. Filtering the collection set by
            // it made this replay strictly slower than real reachability: moons
            // sat uncollected for whole spheres purely because their stage had
            // not been announced yet, and the route-unlock sweep at the foot of
            // the loop then fired against that stale moon count. Announce every
            // still-unannounced stage that has something reachable in it, then
            // collect the full set.
            if (logicalReachableCount > 0) {
                revealReachableLocationStages(
                    state,
                    info,
                    prevKingdom,
                    prevStage,
                    logicalReachable,
                    logicalReachableCount,
                    startingKingdom);
            }
            if (reachableCount <= 0 && info.areaCount <= 0) {
                // Nothing left to collect at the current scenarios. Before calling
                // the itinerary finished, take the same route-advancement step the
                // proven replays take -- collectReachableToFixedPoint ends every
                // pass with `if (!changed) advanceKingdomOrder(state)`, and
                // simulateFinalPlacementReachability does the same.
                //
                // unlockRouteKingdomsReady (used above) only mirrors advance-
                // KingdomOrder's tier-unlock sweep. It omits the peace sweep, which
                // under WorldPeaceRequirementMode_OnKingdomComplete promotes a
                // finished kingdom to its post-game scenario, grants its world-peace
                // token and completes its story count. Without that, every kingdom
                // stayed frozen near its starting scenario for this replay alone --
                // starving it of moons, nodes and usable loading zones, so it ran dry
                // early and reported the seed unbeatable even though the placement
                // validator had already proven it.
                //
                // Gated on exhaustion for the same reason the fixed point is: the
                // scenario jump is irreversible and can put pre-peace content out of
                // reach, so it must not run while anything is still collectable.
                // In the other peace modes the peace sweep is a no-op and the tier
                // sweep is already covered above, so this returns false and the loop
                // breaks exactly as before.
                if (!advanceKingdomOrder(state) && !openStalledBranchSibling(state))
                    break;
                refreshReachableStateEvents(state, nullptr, 0);
                continue;
            }

            // Story first (drives scenarios), then captures/abilities, then moons by
            // value, so usefulness is judged before later same-kingdom filler.
            sortProgressCandidates(reachable, reachableCount);

            for (s32 i = 0; i < reachableCount; i++) {
                const s32 location = reachable[i];
                const s32 item = sPlacementItemForLocation[location];
                if (!isValidMoon(item))
                    continue;
                if (isLocationUsefulForOptimizedPath(location, item, state)) {
                    // Records a path step and collects the location into `state`.
                    recordOptimizedPlacement(
                        state, location, item, sSphereCount, LD::sMoons[location].kingdom);
                } else {
                    // Filler: collect so moon counts/scenarios advance, but do not list.
                    collectLocation(state, location, nullptr, 0);
                }
            }
            info.stepCount = sOptimizedPathStepCount - info.firstStepIndex;

            // Depositing this sphere's moons at the Odyssey opens the next route
            // kingdom(s): diff accessibility across the route-unlock pass so only true
            // progression unlocks (not painting/entrance arrivals) get the marker.
            bool wasAccessible[LD::sKingdomCount];
            for (s32 i = 0; i < LD::sKingdomCount; i++)
                wasAccessible[i] = state.accessibleKingdoms[i];
            unlockRouteKingdomsReady(state);
            for (s32 k = 0; k < LD::sKingdomCount; k++) {
                if (state.accessibleKingdoms[k] && !wasAccessible[k]
                    && info.routeUnlockCount < kMaxSphereKingdoms)
                    info.routeUnlocked[info.routeUnlockCount++] = (s16)k;
            }

            // Only commit a sphere that shows something: progress moons, new areas, or
            // a route unlock. Filler-only waves (e.g. leftover moons reachable after
            // the objective is already met) are collected to keep state correct but not
            // emitted, so the log ends at the last meaningful sphere.
            const bool worthShowing = info.stepCount > 0
                || info.areaCount > 0
                || info.routeUnlockCount > 0;
            if (worthShowing)
                sSphereCount++;

            // Reachable moon rocks may advance scenarios for the next sphere.
            refreshReachableStateEvents(state, nullptr, 0);
        }

        if (sSphereCount >= kMaxSpheres)
            Randomizer::logPush(
                "itinerary: sphere cap (%d) reached; suggested path may be truncated",
                kMaxSpheres);

        // The placement validator and the human-readable itinerary are separate
        // replays. Never report the latter as proven unless it independently reaches
        // the same objective and collects every required progress unlock carrier.
        if (sLogicSucceeded) {
            sFinalObjectiveReached = optimizedPlaythroughComplete(state);
            if (!sFinalObjectiveReached) {
                nn::util::SNPrintf(
                    sFinalObjectiveNote,
                    sizeof(sFinalObjectiveNote),
                    "Placement logic succeeded, but the suggested-path replay did not reach every required unlock and the final objective.");
                addWarning("%s", sFinalObjectiveNote);
            } else if (!sFinalObjectiveNote[0]) {
                const s32 finalKingdom = routeKingdomAt(routeKingdomCount() - 1);
                nn::util::SNPrintf(
                    sFinalObjectiveNote,
                    sizeof(sFinalObjectiveNote),
                    "%s is reachable, %d local moons are proven, and Wedding Hall Checkpoint (obj1348) is reachable.",
                    WorldMeta::getDisplayNameForWorldId(finalKingdom),
                    requirementForKingdom(finalKingdom));
            }
        }
    }

    // --- Hardcoded triple-moon crash repro ------------------------------------
    // Triple Moons Anywhere can drop a grand moon into a location whose vanilla
    // actor was never built to host one. Two of those are reported to crash: the
    // Cap Kingdom timer challenge, and the 2D wall painting inside Sand's Bullet
    // Bill tower. Waiting for the shuffle to land a triple on both at once is a
    // lottery, so this one seed forces it: after a normal, fully validated
    // generation, the finished layout is post-processed to swap a triple item
    // into each pinned location (and the single it displaced into the triple's
    // old spot, so the pool stays a permutation).
    //
    // Debug aid only. The swap runs AFTER logic validation, so the resulting seed
    // is not guaranteed beatable -- it exists to produce a crash dump. Every pin
    // is reported as a generation warning so the spoiler log says what happened.
    static constexpr u32 kCrashReproSeed = 999999999u;

    struct CrashReproPin {
        s32 uid;
        const char* stage;
    };

    // Alternates for the same two spots, if the crash needs the other slot:
    //   { 981, "CapWorldHomeStage" }        -- Cap Kingdom Timer Challenge 2
    //   { 783, "SandWorldKillerTowerZone" } -- Taking Notes: In the Wall Painting
    static constexpr CrashReproPin sCrashReproPins[] = {
        { 861, "CapWorldHomeStage" }, // Cap Kingdom Timer Challenge 1
        { 505, "SandWorldKillerTowerZone" }, // Secret of the Mural (tower 2D section)
    };
    static constexpr s32 sCrashReproPinCount = static_cast<s32>(sizeof(sCrashReproPins) / sizeof(sCrashReproPins[0]));

    static const char* debugPinMoonName(s32 moon) {
        return isValidMoon(moon) ? sMoonEntries[moon]->name : "(nothing)";
    }

    static void applyCrashReproPins(u32 seed) {
        if (seed != kCrashReproSeed)
            return;

        s32 pinLocations[sCrashReproPinCount];
        bool isPinned[LD::sMoonCount] = {};
        for (s32 i = 0; i < sCrashReproPinCount; i++) {
            pinLocations[i] = findMoonItemByIdentity(sCrashReproPins[i].uid, sCrashReproPins[i].stage);
            if (pinLocations[i] >= 0)
                isPinned[pinLocations[i]] = true;
        }

        addWarning(
            "Seed %u is the hardcoded triple-moon crash repro: its layout is forced after "
            "validation and is not guaranteed beatable.",
            seed);
        if (!Randomizer::isTripleMoonsAnywhereEnabled())
            addWarning(
                "Crash repro: Triple Moons Anywhere is OFF, so the forced triples are the only "
                "ones sitting outside a vanilla grand slot.");

        bool swapped = false;
        for (s32 i = 0; i < sCrashReproPinCount; i++) {
            const s32 location = pinLocations[i];
            if (location < 0) {
                addWarning("Crash repro: unknown pin location uid=%d stage=%s.",
                    sCrashReproPins[i].uid, sCrashReproPins[i].stage);
                continue;
            }

            const s32 current = sPlacementItemForLocation[location];
            if (isMultiMoon(current)) {
                addWarning("Crash repro: %s already holds the triple \"%s\".",
                    debugPinMoonName(location), debugPinMoonName(current));
                continue;
            }

            // Donate from any non-pinned location holding a triple, preferring one
            // whose item is not a story moon so the forced layout disturbs the
            // story chain as little as possible. Scenario progression is driven by
            // the LOCATION (see isMultiSlot), so moving the item itself is safe.
            s32 donor = -1;
            for (s32 other = 0; other < LD::sMoonCount; other++) {
                if (other == location || isPinned[other])
                    continue;
                const s32 item = sPlacementItemForLocation[other];
                if (!isMultiMoon(item))
                    continue;
                if (donor < 0)
                    donor = other;
                if (!isStoryMoon(item)) {
                    donor = other;
                    break;
                }
            }
            if (donor < 0) {
                addWarning("Crash repro: no spare triple item left for %s.",
                    debugPinMoonName(location));
                continue;
            }

            const s32 donorItem = sPlacementItemForLocation[donor];
            sPlacementItemForLocation[location] = donorItem;
            sPlacementItemForLocation[donor] = current;
            isPinned[donor] = true;
            swapped = true;
            addWarning("Crash repro: forced triple \"%s\" into %s; \"%s\" moved to %s.",
                debugPinMoonName(donorItem), debugPinMoonName(location),
                debugPinMoonName(current), debugPinMoonName(donor));
        }

        if (swapped)
            buildPlacementListAndCreditMap();
    }

    // --- Hardcoded triple-moon presentation test -------------------------------
    // The reward decides the model and the location decides the get animation, so
    // the three interesting combinations are a triple on an ordinary slot, a
    // triple on a (non-grand) story slot, and an ordinary moon on a vanilla grand
    // slot. Waiting for one shuffle to produce all three in the same kingdom is a
    // lottery, so seed 0 forces them into three Sand Kingdom locations that are
    // quick to reach:
    //
    //   Atop the Highest Tower      story slot, reachable from scenario 0 -> triple
    //   On the Leaning Pillar       ordinary slot, next to the town           -> triple
    //   Showdown on the Inverted Pyramid  vanilla grand slot (Hariet)         -> single
    //
    // Same contract as the crash repro above: this runs AFTER logic validation, so
    // the seed is not guaranteed beatable, every swap is reported as a generation
    // warning, and the pool stays a permutation because the displaced item takes
    // the donor's place.
    static constexpr u32 kPresentationTestSeed = 0u;

    struct PresentationTestPin {
        s32 uid;
        const char* stage;
        bool wantsTriple;
    };
    static constexpr PresentationTestPin sPresentationTestPins[] = {
        { 497, "SandWorldHomeStage", true }, // Atop the Highest Tower (story slot)
        { 516, "SandWorldHomeStage", true }, // On the Leaning Pillar (ordinary slot)
        { 495, "SandWorldHomeStage", false }, // Showdown on the Inverted Pyramid (grand slot)
    };
    static constexpr s32 sPresentationTestPinCount = static_cast<s32>(sizeof(sPresentationTestPins) / sizeof(sPresentationTestPins[0]));

    static void applyPresentationTestPins(u32 seed) {
        if (seed != kPresentationTestSeed)
            return;

        s32 pinLocations[sPresentationTestPinCount];
        bool isPinned[LD::sMoonCount] = {};
        for (s32 i = 0; i < sPresentationTestPinCount; i++) {
            pinLocations[i] = findMoonItemByIdentity(
                sPresentationTestPins[i].uid, sPresentationTestPins[i].stage);
            if (pinLocations[i] >= 0)
                isPinned[pinLocations[i]] = true;
        }

        addWarning(
            "Seed %u is the hardcoded triple-moon presentation test: its Sand Kingdom layout is "
            "forced after validation and is not guaranteed beatable.",
            seed);
        if (!Randomizer::isRandomizeTripleMoonsEnabled()
            && !Randomizer::isTripleMoonsAnywhereEnabled()) {
            addWarning(
                "Presentation test: both triple-moon options are OFF, so these forced swaps are "
                "the only mismatched reward/slot pairs in the seed.");
        }

        bool swapped = false;
        for (s32 i = 0; i < sPresentationTestPinCount; i++) {
            const s32 location = pinLocations[i];
            const bool wantsTriple = sPresentationTestPins[i].wantsTriple;
            if (location < 0) {
                addWarning("Presentation test: unknown pin location uid=%d stage=%s.",
                    sPresentationTestPins[i].uid, sPresentationTestPins[i].stage);
                continue;
            }

            const s32 current = sPlacementItemForLocation[location];
            if (!isValidMoon(current)) {
                addWarning("Presentation test: %s holds no item to swap.",
                    debugPinMoonName(location));
                continue;
            }
            if (isMultiMoon(current) == wantsTriple) {
                addWarning("Presentation test: %s already holds the %s \"%s\".",
                    debugPinMoonName(location), wantsTriple ? "triple" : "single",
                    debugPinMoonName(current));
                continue;
            }

            // Donate from any non-pinned location already holding an item of the
            // needed kind, preferring a non-story one so the story chain is
            // disturbed as little as possible. Scenario progression follows the
            // LOCATION (see isMultiSlot), so moving items is safe.
            s32 donor = -1;
            for (s32 other = 0; other < LD::sMoonCount; other++) {
                if (other == location || isPinned[other])
                    continue;
                const s32 item = sPlacementItemForLocation[other];
                if (!isValidMoon(item) || isMultiMoon(item) != wantsTriple)
                    continue;
                if (donor < 0)
                    donor = other;
                if (!isStoryMoon(item)) {
                    donor = other;
                    break;
                }
            }
            if (donor < 0) {
                addWarning("Presentation test: no spare %s item left for %s.",
                    wantsTriple ? "triple" : "single", debugPinMoonName(location));
                continue;
            }

            const s32 donorItem = sPlacementItemForLocation[donor];
            sPlacementItemForLocation[location] = donorItem;
            sPlacementItemForLocation[donor] = current;
            isPinned[donor] = true;
            swapped = true;
            addWarning("Presentation test: forced %s \"%s\" into %s; \"%s\" moved to %s.",
                wantsTriple ? "triple" : "single", debugPinMoonName(donorItem),
                debugPinMoonName(location), debugPinMoonName(current),
                debugPinMoonName(donor));
        }

        if (swapped)
            buildPlacementListAndCreditMap();
    }

    bool buildRemapTable(u32 seed) {
        invalidatePhysicalRouteState();
        const LogicSettingsSnapshot settings = captureLogicSettings();
        const bool captureRandoEnabled = settings.captureRando;
        const bool abilityRandoEnabled = settings.abilityRando;
        const bool entranceRandoEnabled = settings.entranceMode != Randomizer::EntranceRandoMode_Off;
        const bool paintingRandoEnabled = settings.paintingRando;
        const bool kingdomRandoEnabled = settings.kingdomRando;
        const bool logicRandoEnabled = settings.logicRando;

        if (sInitialized && sLogicSucceeded && seed == sLastSeed
            && sHasLastSettings && settingsEqual(settings, sLastSettings)) {
            if (kingdomRandoEnabled)
                KingdomRando::rebuild(seed, sLastEntranceAttempt);
            else
                KingdomRando::clear();
            // Redistribution is a pre-processing step: it must run before the role
            // assigners and requirement calculation so they all see the reshuffled
            // pool, matching the fresh-generation ordering below.
            precomputeMoonEntries();
            computeRewardMultiOverride(seed);
            if (!computeMoonKingdomAssignment(seed))
                return false;
            RequirementRando::rebuild(seed, sLastEntranceAttempt);
            PaintingRando::buildRemapTable(seed, sLastEntranceAttempt);
            if (captureRandoEnabled)
                CaptureRando::buildRemapTable(seed, sLastEntranceAttempt);
            else
                CaptureRando::clear();
            if (!EnemySwapRando::buildRemapTable(seed, sLastEntranceAttempt)) {
                setFailure(
                    FailureCode::DataError,
                    "Enemy/capture placement constraints failed (code %d).",
                    static_cast<s32>(EnemySwapRando::getFailureCode()));
                return false;
            }
            if (abilityRandoEnabled)
                AbilityRando::buildRemapTable(seed, sLastEntranceAttempt);
            else
                AbilityRando::clear();
            buildMoonRockKeys(seed, sLastEntranceAttempt);
            buildToadetteMedals(seed, sLastEntranceAttempt);
            // Last, and allowed to share carriers with either of the above.
            if (!buildSubareaKeys(seed, sLastEntranceAttempt))
                return false;
            precomputeMoonUnlocks();
            // PaintingRando rebuilds its normal seed permutation above.  Preserve
            // the solver-finalized Cloud order when this already-complete seed is
            // queried again (for example by a UI lookup), rather than silently
            // reverting its free links and Cloud-moon rewards.
            if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
                && sCloudPaintingAssignmentsReady) {
                s16 cloudOrder[LD::sPaintingLinkCount] = {};
                bool usedLinks[LD::sPaintingLinkCount] = {};
                s32 cloudOrderCount = 0;
                for (s32 link = 0; link < LD::sPaintingLinkCount; link++) {
                    if (!sFinalFreePaintingLinks[link])
                        continue;
                    cloudOrder[cloudOrderCount++] = static_cast<s16>(link);
                    usedLinks[link] = true;
                }
                for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++) {
                    const s32 link = sFinalCloudPaintingLinkBySlot[slot];
                    if (link < 0 || link >= LD::sPaintingLinkCount)
                        continue;
                    if (usedLinks[link]) {
                        setFailure(FailureCode::DataError,
                            "Saved Cloud painting assignment is not a permutation.");
                        return false;
                    }
                    cloudOrder[cloudOrderCount++] = static_cast<s16>(link);
                    usedLinks[link] = true;
                }
                for (s32 link = 0; link < LD::sPaintingLinkCount; link++) {
                    if (!usedLinks[link])
                        cloudOrder[cloudOrderCount++] = static_cast<s16>(link);
                }
                if (cloudOrderCount != LD::sPaintingLinkCount
                    || !PaintingRando::setCloudLinkOrder(
                        cloudOrder, cloudOrderCount)) {
                    setFailure(FailureCode::DataError,
                        "Could not restore the finalized Cloud painting order.");
                    return false;
                }
            }
            // Deliberately NOT rebuilding the entrance table here. It is the
            // one the fill chose on the winning attempt, is still in memory,
            // and is no longer derivable from seed + attempt alone - rebuilding
            // would replace it with a different wiring the placement was never
            // proven against.
            if (!entranceRandoEnabled)
                EntranceRando::reset();
            StartPositionRando::rebuild(seed, sLastEntranceAttempt);
            return true;
        }

        // No Logic bypasses capture/ability and trick requirements only. Topology,
        // scenarios, story order, entrances and paintings remain authoritative.
        sNoLogicFill = !logicRandoEnabled;

        sUsedLogicPlacement = true;
        sLogicSucceeded = false;
        sInitialized = false;
        sFailureCode = FailureCode::None;
        std::strncpy(sFailureMessage, "No generation failure.", sizeof(sFailureMessage) - 1);
        sFailureMessage[sizeof(sFailureMessage) - 1] = '\0';
        sGenerationAttemptCount = 0;
        sGenerationDurationMillis = 0;
        std::memset(sGenerationAttempts, 0, sizeof(sGenerationAttempts));

        // Moon allocation is seed/settings deterministic and must be finalized
        // before every attempt's capture, ability, painting and solver work.
        precomputeMoonEntries();
        computeRewardMultiOverride(seed);
        if (!computeMoonKingdomAssignment(seed)) {
            addWarning("Moon allocation rejected: %s", sFailureMessage);
            sUsedLogicPlacement = true;
            sLogicSucceeded = false;
            sInitialized = false;
            sLastSeed = 0xFFFFFFFF;
            return false;
        }
        const u64 generationStartTick = nn::os::GetSystemTick().value;

        for (u32 attempt = 0; attempt < kMaxAttempts; attempt++) {
            const u64 attemptStartTick = nn::os::GetSystemTick().value;
            {
                char progressStatus[64];
                nn::util::SNPrintf(progressStatus, sizeof(progressStatus),
                    "Placing moons (attempt %u of %d)", attempt + 1, kMaxAttempts);
                SeedGenerator::reportProgress(46, progressStatus);
            }
            sSearchNodes = 0;
            sSearchBundlesTried = 0;
            sFailureCode = FailureCode::None;
            std::strncpy(sFailureMessage, "No failure recorded for this attempt.",
                sizeof(sFailureMessage) - 1);
            sFailureMessage[sizeof(sFailureMessage) - 1] = '\0';
            // Every dependency is salted with the same attempt and rebuilt in a stable
            // order so a saved attempt reconstructs exactly.
            resetCloudPaintingAssignments(seed, attempt);
            AbilityRando::clearLogicStartingOverride();
            CaptureRando::clearLogicStartingOverride();
            if (kingdomRandoEnabled)
                KingdomRando::rebuild(seed, attempt);
            else
                KingdomRando::clear();
            RequirementRando::rebuild(seed, attempt);
            PaintingRando::buildRemapTable(seed, attempt);
            if (captureRandoEnabled)
                CaptureRando::buildRemapTable(seed, attempt);
            else
                CaptureRando::clear();
            if (!EnemySwapRando::buildRemapTable(seed, attempt)) {
                setFailure(
                    FailureCode::DataError,
                    "Enemy/capture placement constraints are unsatisfied "
                    "(attempt %u, code %d).",
                    attempt,
                    static_cast<s32>(EnemySwapRando::getFailureCode()));
                recordGenerationAttempt(
                    attempt, elapsedMillisSince(attemptStartTick), 0, false);
                sLastEntranceAttempt = attempt;
                continue;
            }
            if (abilityRandoEnabled)
                AbilityRando::buildRemapTable(seed, attempt);
            else
                AbilityRando::clear();
            if (entranceRandoEnabled) {
                // Only the pools are built here. The wiring itself is decided
                // inside runBufferManagedSpherePlacement, one door at a time,
                // and closed out by finalizeIncrementalBuild.
                EntranceRando::beginIncrementalBuild(seed, attempt);
            } else {
                EntranceRando::reset();
            }
            sWireStepCount = 0;
            sEnforceSphereBand = attempt + kSphereBandRelaxedAttempts < (u32)kMaxAttempts;
            buildMoonRockKeys(seed, attempt);
            buildToadetteMedals(seed, attempt);
            // Last, and allowed to share carriers with either of the above.
            if (!buildSubareaKeys(seed, attempt)) {
                recordGenerationAttempt(
                    attempt, elapsedMillisSince(attemptStartTick), 0, false);
                sLastEntranceAttempt = attempt;
                continue;
            }
            precomputeMoonUnlocks();
            if (chooseLogicStartingEquipment(seed, attempt)) {
                if (captureRandoEnabled)
                    CaptureRando::buildRemapTable(seed, attempt);
                if (abilityRandoEnabled)
                    AbilityRando::buildRemapTable(seed, attempt);
                precomputeMoonUnlocks();
            }
            chooseInitialFreePaintingLinks();
            // Last dependency: the walk-back proof reads the finished entrance
            // shuffle AND the starting equipment chosen just above.
            StartPositionRando::rebuild(seed, attempt);

            const u32 dependencyMillis = elapsedMillisSince(attemptStartTick);
            const u64 placementStartTick = nn::os::GetSystemTick().value;
            bool success = runBufferManagedSpherePlacement(seed, attempt);
            if (success && sEnforceSphereBand && !sphereCountInDifficultyBand(sLastFillSphereCount)) {
                // Beatable, but not the shape this Seed Difficulty asked for.
                // Re-roll while there is budget to spare.
                Randomizer::logPush(
                    "seed shape rejected: %d spheres outside the Seed Difficulty band (attempt %u)",
                    sLastFillSphereCount, attempt);
                success = false;
            }
            const u32 placementMillis = elapsedMillisSince(placementStartTick);
            recordGenerationAttempt(
                attempt, dependencyMillis, placementMillis, success);
            if (success) {
                sLogicSucceeded = true;
                sLastEntranceAttempt = attempt;
                if (attempt > 0)
                    Randomizer::logPush("logic placement succeeded on attempt %u", attempt);
                break;
            }
            sLastEntranceAttempt = attempt;
        }
        sGenerationDurationMillis = elapsedMillisSince(generationStartTick);

        if (!sLogicSucceeded) {
            if (sFailureCode == FailureCode::None)
                setFailure(FailureCode::UnreachableObjective,
                    "Could not prove a valid seed after %d dependency attempts.", kMaxAttempts);
            addWarning("Logic placement rejected after %d attempts: %s", kMaxAttempts, sFailureMessage);
            // The last attempt ran with TraceBuf armed (see
            // runBufferManagedSpherePlacement). Write it out: this is the only
            // per-decision record of why a seed could not be built, and until
            // now dumpToFile had no callers at all, so the buffer was filled
            // on every failed generation and then simply thrown away.
            TraceBuf::dumpToFile(seed);
            resetPlacementState();
            sUsedLogicPlacement = true;
            sLogicSucceeded = false;
            sInitialized = false;
            sLastSeed = 0xFFFFFFFF;
            return false;
        }

        sLastSeed = seed;
        sLastCaptureRandoEnabled = captureRandoEnabled;
        sLastAbilityRandoEnabled = abilityRandoEnabled;
        sLastEntranceRandoEnabled = entranceRandoEnabled;
        sLastPaintingRandoEnabled = paintingRandoEnabled;
        sLastKingdomRandoEnabled = kingdomRandoEnabled;
        sLastLogicRandoEnabled = logicRandoEnabled;
        sLastMoonDifficulty = settings.moonDifficulty;
        sLastLogicDifficulty = settings.logicDifficulty;
        sLastSettings = settings;
        sHasLastSettings = true;
        sInitialized = true;
        sFailureCode = FailureCode::None;

        // Forced layout for the hardcoded crash-repro seed, applied to the proven
        // placement so the playthrough below is rebuilt against what will actually
        // be saved. No-op for every other seed.
        applyCrashReproPins(seed);
        applyPresentationTestPins(seed);

        generateOptimizedPlaythrough();
        // Include the final itinerary replay in the top-level logic timing; its
        // work is deterministic, but can be material on a long successful path.
        sGenerationDurationMillis = elapsedMillisSince(generationStartTick);

        Randomizer::logPush("moon logic table: placements=%d path=%d optimized_path=%d success=%d warnings=%d",
            sPlacementCount, sPathStepCount, sOptimizedPathStepCount, (int)sLogicSucceeded, sWarningCount);
        return true;
    }

    void clear() {
        invalidatePhysicalRouteState();
        resetPlacementState();
        AbilityRando::clearLogicStartingOverride();
        CaptureRando::clearLogicStartingOverride();
        std::memset(sGenerationAttempts, 0, sizeof(sGenerationAttempts));
        sGenerationAttemptCount = 0;
        sGenerationDurationMillis = 0;
        // There is no placement any more, so there are no doors the fill chose.
        sWireStepCount = 0;
        std::memset(sMoonEntries, 0, sizeof(sMoonEntries));
        sMoonEntryIndexCacheReady = false;
        for (s32 i = 0; i < LD::sMoonCount; i++)
            sMoonAssignedKingdom[i] = -1;
        std::memset(sKingdomAllocationSummaries, 0,
            sizeof(sKingdomAllocationSummaries));
        sKingdomAllocationSummaryReady = false;
        sMoonRoleLayoutHash = 0;
        std::memset(sMoonUnlockTokens, 0, sizeof(sMoonUnlockTokens));
        std::memset(sMoonUnlockTokenCounts, 0, sizeof(sMoonUnlockTokenCounts));
        for (s32 i = 0; i < LD::sMoonCount; i++) {
            sMoonCloudPaintingSlotCounts[i] = 0;
            for (s32 unlock = 0;
                 unlock < kMaxCloudPaintingUnlocksPerMoon; unlock++) {
                sMoonCloudPaintingSlots[i][unlock] = -1;
                sMoonPaintingUnlocks[i][unlock] = -1;
            }
        }
        std::memset(sFreePaintingLinks, 0, sizeof(sFreePaintingLinks));
        std::memset(sFinalFreePaintingLinks, 0, sizeof(sFinalFreePaintingLinks));
        for (s32 i = 0; i < kCloudPaintingSlotCount; i++)
            sFinalCloudPaintingLinkBySlot[i] = -1;
        sCloudPaintingAssignmentsReady = false;
        sCloudPaintingAssignmentTieSeed = 0;
        clearMoonRockKeys();
        clearToadetteMedals();
        clearSubareaKeys();
        sToadetteMedalWinTriggered = false;
        sInitialized = false;
        sRewardMultiOverrideActive = false;
        sKingdomAssignmentActive = false;
        sUsedLogicPlacement = false;
        sLogicSucceeded = false;
        sNoLogicFill = false;
        sDifficultFillLevel = -1;
        sCasualFallbackActive = false;
        sLastSeed = 0xFFFFFFFF;
        sLastEntranceAttempt = 0;
        sLastCaptureRandoEnabled = false;
        sLastAbilityRandoEnabled = false;
        sLastEntranceRandoEnabled = false;
        sLastPaintingRandoEnabled = false;
        sLastKingdomRandoEnabled = false;
        sLastLogicRandoEnabled = false;
        sLastMoonDifficulty = Randomizer::MoonDifficulty_Medium;
        sLastLogicDifficulty = Randomizer::LogicDifficulty_Casual;
        sHasLastSettings = false;
        sFailureCode = FailureCode::None;
        std::strncpy(sFailureMessage, "No generation failure.", sizeof(sFailureMessage) - 1);
        sFailureMessage[sizeof(sFailureMessage) - 1] = '\0';
    }

    void rebuildPlaythrough() {
        generateOptimizedPlaythrough();
    }

    // See header. Computes the moon->kingdom redistribution alone so that role
    // assignments rebuilt outside buildRemapTable (the save-restore path) see the
    // reshuffled pool. This also prepares full Moon Allocation during deterministic
    // save restoration, before the role maps are rebuilt.
    bool prepareKingdomRedistribution(u32 seed) {
        precomputeMoonEntries();
        computeRewardMultiOverride(seed);
        return computeMoonKingdomAssignment(seed);
    }

    // Rebuild the placement state from a saved seed's serialized item->location map
    // (so a loaded save reproduces the same shuffle without re-running the solver).
    // Mirrors the config snapshot that generatePlacements takes on a fresh run.
    bool restorePlacements(const s32* itemForLocation, s32 itemCount, const SerializedPlacement* placements, s32 placementCount, u32 seed, u32 entranceAttempt, bool logicSucceeded, const s16* cloudPaintingLinkOrder, s32 cloudPaintingLinkOrderCount, const s32* autoGrantItems, s32 autoGrantCount) {
        invalidatePhysicalRouteState();
        precomputeMoonEntries();
        computeRewardMultiOverride(seed);
        if (!computeMoonKingdomAssignment(seed))
            return false;

        // Attempt timings describe a live generation run, not deterministic save
        // restoration. Do not show stale diagnostics from the previous seed.
        std::memset(sGenerationAttempts, 0, sizeof(sGenerationAttempts));
        sGenerationAttemptCount = 0;
        sGenerationDurationMillis = 0;

        // Same reason, and it matters more: the wiring ORDER is not serialized
        // (the save carries the finished entrance table, not the sequence of
        // decisions that produced it), so a restore has no wire steps of its
        // own. Leaving the array alone would print the PREVIOUS seed's doors
        // under this seed's spoiler in any process that already generated one.
        // Clearing it makes appendEntranceWiringOrder omit the section instead,
        // which is what a fresh process already does.
        sWireStepCount = 0;

        sCloudPaintingAssignmentsReady = false;
        std::memset(sFreePaintingLinks, 0, sizeof(sFreePaintingLinks));
        std::memset(sFinalFreePaintingLinks, 0, sizeof(sFinalFreePaintingLinks));
        for (s32 i = 0; i < kCloudPaintingSlotCount; i++)
            sFinalCloudPaintingLinkBySlot[i] = -1;
        s16 legacyCloudPaintingLinkOrder[LD::sPaintingLinkCount] = {};
        if (!cloudPaintingLinkOrder
            || cloudPaintingLinkOrderCount != LD::sPaintingLinkCount) {
            if (PaintingRando::getCloudLinkOrder(
                    legacyCloudPaintingLinkOrder, LD::sPaintingLinkCount)
                == LD::sPaintingLinkCount) {
                cloudPaintingLinkOrder = legacyCloudPaintingLinkOrder;
                cloudPaintingLinkOrderCount = LD::sPaintingLinkCount;
            }
        }
        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
            && cloudPaintingLinkOrder
            && cloudPaintingLinkOrderCount == LD::sPaintingLinkCount
            && PaintingRando::setCloudLinkOrder(
                cloudPaintingLinkOrder, cloudPaintingLinkOrderCount)) {
            bool slotPresent[kCloudPaintingSlotCount] = {};
            for (s32 i = 0; i < LD::sMoonCount; i++) {
                const ShineDB::ShineEntry* moon = sMoonEntries[i];
                if (!moon)
                    continue;
                const s32 firstSlot = PaintingRando::getCloudMoonSlot(*moon);
                const s32 unlockCount
                    = PaintingRando::getCloudMoonPaintingUnlockCount(*moon);
                for (s32 unlock = 0;
                     unlock < unlockCount
                     && unlock < kMaxCloudPaintingUnlocksPerMoon;
                     unlock++) {
                    const s32 slot = firstSlot + unlock;
                    if (slot >= 0 && slot < kCloudPaintingSlotCount)
                        slotPresent[slot] = true;
                }
            }
            s32 cloudSlotCount = 0;
            for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++)
                cloudSlotCount += slotPresent[slot] ? 1 : 0;
            const s32 freeLinkCount = LD::sPaintingLinkCount > cloudSlotCount
                ? LD::sPaintingLinkCount - cloudSlotCount
                : 0;
            for (s32 i = 0; i < freeLinkCount; i++) {
                const s32 link = cloudPaintingLinkOrder[i];
                if (link >= 0 && link < LD::sPaintingLinkCount) {
                    sFreePaintingLinks[link] = true;
                    sFinalFreePaintingLinks[link] = true;
                }
            }
            for (s32 slot = 0; slot < kCloudPaintingSlotCount; slot++) {
                const s32 orderIndex = freeLinkCount + slot;
                if (!slotPresent[slot] || orderIndex >= LD::sPaintingLinkCount)
                    continue;
                sFinalCloudPaintingLinkBySlot[slot] = cloudPaintingLinkOrder[orderIndex];
            }
            sCloudPaintingAssignmentsReady = true;
        }
        buildMoonRockKeys(seed, entranceAttempt);
        buildToadetteMedals(seed, entranceAttempt);
        // Last, and allowed to share carriers with either of the above. Carrier
        // assignment is derived from (seed, attempt, mode, host) rather than
        // serialized, so a restore re-derives it under the current rules; a
        // layout that no longer validates must not come back as a live seed.
        if (!buildSubareaKeys(seed, entranceAttempt))
            return false;
        sToadetteMedalWinTriggered = false;
        precomputeMoonUnlocks();
        resetPlacementState();

        if (autoGrantCount < 0 || autoGrantCount > kMaxAutoGrants)
            return false;
        for (s32 i = 0; i < autoGrantCount; i++) {
            const s32 item = autoGrantItems ? autoGrantItems[i] : -1;
            if (!isValidMoon(item) || !sMoonEntries[item]
                || !isMoonPoolEligible(*sMoonEntries[item]))
                return false;
            for (s32 previous = 0; previous < i; previous++) {
                if (sAutoGrantItems[previous] == item)
                    return false;
            }
            sAutoGrantItems[i] = static_cast<s16>(item);
        }
        sAutoGrantCount = autoGrantCount;

        const s32 copyCount = itemCount < LD::sMoonCount ? itemCount : LD::sMoonCount;
        for (s32 i = 0; i < copyCount; i++)
            sPlacementItemForLocation[i] = itemForLocation ? itemForLocation[i] : -1;

        buildPlacementListAndCreditMap();

        if (placements && placementCount >= 0 && placementCount != sPlacementCount)
            addWarning("Restored placement list count mismatch: saved=%d rebuilt=%d", placementCount, sPlacementCount);

        sUsedLogicPlacement = true;
        sLogicSucceeded = logicSucceeded;
        sNoLogicFill = !Randomizer::isLogicRandoEnabled();
        sLastSeed = seed;
        sLastEntranceAttempt = entranceAttempt;
        sLastCaptureRandoEnabled = Randomizer::isCaptureRandoEnabled();
        sLastAbilityRandoEnabled = Randomizer::isLockedAbilitiesEnabled();
        sLastEntranceRandoEnabled = Randomizer::isEntranceRandoEnabled();
        sLastPaintingRandoEnabled = Randomizer::isPaintingRandoEnabled();
        sLastKingdomRandoEnabled = Randomizer::isKingdomRandoEnabled();
        sLastLogicRandoEnabled = Randomizer::isLogicRandoEnabled();
        sLastMoonDifficulty = Randomizer::getMoonDifficulty();
        sLastLogicDifficulty = Randomizer::getLogicDifficulty();
        sLastSettings = captureLogicSettings();
        sHasLastSettings = true;
        sInitialized = true;
        sFailureCode = logicSucceeded ? FailureCode::None : FailureCode::ValidationFailure;
        if (!logicSucceeded) {
            std::strncpy(sFailureMessage, "Legacy save was not logic-proven.", sizeof(sFailureMessage) - 1);
            sFailureMessage[sizeof(sFailureMessage) - 1] = '\0';
        }

        generateOptimizedPlaythrough();
        return true;
    }

    void setCarriedStartingEquipment(u64 captures, u64 abilities) {
        sCarriedStartingCaptures = captures;
        sCarriedStartingAbilities = abilities;
    }

    bool isInitialized() { return sInitialized; }

    bool usedLogicPlacement() { return sUsedLogicPlacement; }

    bool logicSucceeded() { return sLogicSucceeded; }

    FailureCode getFailureCode() { return sFailureCode; }

    const char* getFailureMessage() { return sFailureMessage; }

    const char* getFailureCodeName(FailureCode code) {
        switch (code) {
        case FailureCode::None:
            return "None";
        case FailureCode::DataError:
            return "Data error";
        case FailureCode::CapacityFailure:
            return "Capacity failure";
        case FailureCode::StrandedUnlocks:
            return "Stranded unlocks";
        case FailureCode::UnreachableObjective:
            return "Unreachable objective";
        case FailureCode::WorldPeaceFailure:
            return "World peace failure";
        case FailureCode::SearchBudgetExhausted:
            return "Search budget exhausted";
        case FailureCode::ValidationFailure:
            return "Validation failure";
        default:
            return "Unknown failure";
        }
    }

    s32 getSearchNodeCount() { return sSearchNodes; }

    s32 getSearchBundleCount() { return sSearchBundlesTried; }

    s32 getGenerationAttemptCount() { return sGenerationAttemptCount; }

    const GenerationAttemptInfo* getGenerationAttempt(s32 index) {
        if (index < 0 || index >= sGenerationAttemptCount)
            return nullptr;
        return &sGenerationAttempts[index];
    }

    u32 getGenerationDurationMillis() { return sGenerationDurationMillis; }

    s32 getCloudPaintingLinkOrder(s16* outOrder, s32 maxCount) {
        return PaintingRando::getCloudLinkOrder(outOrder, maxCount);
    }

    const ShineDB::ShineEntry* getCreditMoon(const char* objectId, const char* stage) {
        if (!sInitialized || !objectId || !stage)
            return nullptr;

        if (objectId[0] != 'o' || objectId[1] != 'b' || objectId[2] != 'j')
            return nullptr;

        s32 objNum = 0;
        const char* p = objectId + 3;
        if (*p == '\0')
            return nullptr;
        while (*p >= '0' && *p <= '9')
            objNum = objNum * 10 + (*p++ - '0');
        if (*p != '\0')
            return nullptr;

        const s32 uid = ShineDB::findUid(stage, objNum);
        if (uid < 0)
            return nullptr;

        for (s32 i = 0; i < sCreditMapCount; i++)
            if (sCreditMap[i].srcUid == uid && strEq(sCreditMap[i].srcStage, stage))
                return sCreditMap[i].dest;

        return nullptr;
    }

    static bool isFigureWalkingLocation(const ShineDB::ShineEntry& entry) {
        if (!entry.stage)
            return false;

        if (std::strcmp(entry.stage, "SandWorldHomeStage") == 0)
            return entry.uid == 533 || entry.uid == 712;
        if (std::strcmp(entry.stage, "IceWalkerExStage") == 0)
            return entry.uid == 877 || entry.uid == 878;
        if (std::strcmp(entry.stage, "MoonWorldHomeStage") == 0)
            return entry.uid == 578 || entry.uid == 714;
        return false;
    }

    using SpecialMoonLocationPredicate = bool (*)(const ShineDB::ShineEntry&);

    static s32 getSpecialMoonCollectedCount(const GameDataFile* file,
        const char* objectNameOrOptionalId, SpecialMoonLocationPredicate isLocation) {
        if (!sInitialized || !file || !objectNameOrOptionalId || !objectNameOrOptionalId[0])
            return -1;

        bool isSpecialMoonQuery = false;
        s32 collectedCount = 0;
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry& source = ShineDB::sShineTable[i];
            if (!isLocation(source))
                continue;

            const GameDataFile::HintInfo* sourceInfo = file->customFindShine(source.uid, source.stage);
            if (!sourceInfo)
                continue;

            // GameDataFile's native query checks objectName as a substring and
            // optionalId as an exact match. Mirror those comparisons so this
            // hook only changes these special-room queries, regardless of
            // whether the game uses the object name or its optional ID.
            const char* objectName = sourceInfo->objectName.cstr();
            const char* optionalId = sourceInfo->optionalId.cstr();
            const bool matchesQuery = (objectName && std::strstr(objectName, objectNameOrOptionalId))
                || (optionalId && std::strcmp(optionalId, objectNameOrOptionalId) == 0);
            if (!matchesQuery)
                continue;

            isSpecialMoonQuery = true;
            char objectId[32];
            nn::util::SNPrintf(objectId, sizeof(objectId), "obj%d", source.objId);
            const ShineDB::ShineEntry* destination = getCreditMoon(objectId, source.stage);
            const ShineDB::ShineEntry* record = destination ? destination : &source;
            if (file->customIsGotShine(record->uid, record->stage))
                collectedCount++;
        }

        return isSpecialMoonQuery ? collectedCount : -1;
    }

    s32 getFigureWalkingCollectedCount(const GameDataFile* file, const char* objectNameOrOptionalId) {
        return getSpecialMoonCollectedCount(file, objectNameOrOptionalId, isFigureWalkingLocation);
    }

    s32 getPictureMatchCollectedState(const GameDataFile* file, const char* objectId, const char* stage) {
        if (!sInitialized || !file || !objectId || !stage || !stage[0])
            return -1;
        if (objectId[0] != 'o' || objectId[1] != 'b' || objectId[2] != 'j')
            return -1;

        s32 objNum = 0;
        const char* p = objectId + 3;
        if (*p == '\0')
            return -1;
        while (*p >= '0' && *p <= '9')
            objNum = objNum * 10 + (*p++ - '0');
        if (*p != '\0')
            return -1;

        const s32 uid = ShineDB::findUid(stage, objNum);
        const bool isPictureMatch = (std::strcmp(stage, "FukuwaraiKuriboStage") == 0
                                        && (uid == 982 || uid == 983))
            || (std::strcmp(stage, "FukuwaraiMarioStage") == 0
                && (uid == 985 || uid == 986));
        if (!isPictureMatch)
            return -1;

        const ShineDB::ShineEntry* source = ShineDB::findEntry(uid, stage);
        if (!source)
            return -1;

        // GameDataFunction::isGotShine builds a ShineInfo from the current
        // stage and this placement ID. Under moon randomization, the physical
        // Picture Match moon's collected bit is stored on its destination.
        const ShineDB::ShineEntry* destination = getCreditMoon(objectId, stage);
        const ShineDB::ShineEntry* record = destination ? destination : source;
        return file->customIsGotShine(record->uid, record->stage) ? 1 : 0;
    }

    const Placement* getPlacementForItem(s32 uid, const char* stage) {
        if (!sInitialized || uid < 0 || !stage)
            return nullptr;

        const s32 item = findMoonItemByIdentity(uid, stage);
        if (item < 0)
            return nullptr;
        const s32 index = sPlacementIndexForItem[item];
        if (index < 0 || index >= sPlacementCount)
            return nullptr;
        const Placement& placement = sPlacements[index];
        return placement.location && placement.item ? &placement : nullptr;
    }

    // The item shuffled onto a physical location; null when the location is not
    // remapped (then the location's own flag is its collected state).
    const ShineDB::ShineEntry* getItemForLocation(s32 uid, const char* stage) {
        if (!sInitialized || uid < 0 || !stage)
            return nullptr;

        const s32 location = findMoonItemByIdentity(uid, stage);
        if (location < 0)
            return nullptr;
        const s32 index = sPlacementIndexForLocation[location];
        if (index < 0 || index >= sPlacementCount)
            return nullptr;
        const Placement& placement = sPlacements[index];
        return placement.location && placement.item ? placement.item : nullptr;
    }

    bool kingdomHasMoonRockKey(s32 kingdom) {
        return sInitialized
            && Randomizer::isMoonRockKeyEnabled()
            && kingdomHasRealMoonRock(kingdom)
            && sMoonRockKeyItemByKingdom[kingdom] >= 0;
    }

    const ShineDB::ShineEntry* getMoonRockKeyForKingdom(s32 kingdom) {
        if (!kingdomHasMoonRockKey(kingdom))
            return nullptr;
        const s32 item = sMoonRockKeyItemByKingdom[kingdom];
        return isValidMoon(item) ? sMoonEntries[item] : nullptr;
    }

    s32 getMoonRockKeyKingdomForItem(s32 uid, const char* stage) {
        if (!sInitialized || uid < 0 || !stage || !Randomizer::isMoonRockKeyEnabled())
            return -1;

        for (s32 item = 0; item < LD::sMoonCount; item++) {
            const ShineDB::ShineEntry* entry = sMoonEntries[item];
            if (!entry || entry->uid != uid || !strEq(entry->stage, stage))
                continue;
            return moonRockKeyKingdomForItem(item);
        }
        return -1;
    }

    bool isMoonRockKeyItem(s32 uid, const char* stage) {
        return getMoonRockKeyKingdomForItem(uid, stage) >= 0;
    }

    // Whether the built table covers every slot the current mode asks for.
    // buildSubareaKeys refuses to leave a partial one behind, so this is really
    // "was a layout built at all", and it is the gate every presentation query
    // below goes through: a half-filled list would hand the native
    // ShineListLayout an index it cannot resolve, which it dereferences without
    // a null check.
    bool isSubareaKeyLayoutComplete() {
        return sInitialized && Randomizer::isSubareaUnlockEnabled()
            && sSubareaKeyLayoutComplete;
    }

    bool isSubareaKeySlotAssigned(s32 slot) {
        if (!isSubareaKeyLayoutComplete())
            return false;
        if (slot < 0 || slot >= SubareaKeyDB::keySlotCount() || slot >= kMaxSubareaKeySlots)
            return false;
        return isValidMoon(sSubareaKeyItemBySlot[slot]);
    }

    // Does a key covering this pair exist in this seed? Distinct from
    // hasSubareaKey, which asks whether the player has COLLECTED it and falls
    // open when it cannot tell. UI that draws an icon must ask this one first,
    // or a pair with no carrier renders as a permanently-owned key.
    bool hasSubareaKeyAssignment(s32 kingdom, s32 subareaType) {
        if (!isSubareaKeyLayoutComplete())
            return false;
        return isSubareaKeySlotAssigned(SubareaKeyDB::findKeySlot(
            kingdom, static_cast<SubareaKeyDB::SubareaType>(subareaType)));
    }

    // Rows the key list can render: the mode's full slot count, because the
    // build guarantees every one of them has a carrier - or zero, because
    // nothing usable was built.
    s32 getSubareaKeySlotCount() {
        if (!isSubareaKeyLayoutComplete())
            return 0;
        const s32 slotCount = SubareaKeyDB::keySlotCount();
        return slotCount < kMaxSubareaKeySlots ? slotCount : kMaxSubareaKeySlots;
    }

    const ShineDB::ShineEntry* getSubareaKeyForSlot(s32 slot) {
        if (!isSubareaKeySlotAssigned(slot))
            return nullptr;
        return sMoonEntries[sSubareaKeyItemBySlot[slot]];
    }

    s32 getSubareaKeySlotForItem(s32 uid, const char* stage) {
        if (!isSubareaKeyLayoutComplete() || uid < 0 || !stage)
            return -1;
        const s32 item = findMoonItemByIdentity(uid, stage);
        return item >= 0 ? subareaKeySlotForItem(item) : -1;
    }

    bool isSubareaKeyItem(s32 uid, const char* stage) {
        return getSubareaKeySlotForItem(uid, stage) >= 0;
    }

    bool hasSubareaKey(GameDataFile* file, s32 kingdom, s32 subareaType) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return true;
        // Without a built placement (or a save to read) nothing can be proven
        // locked, so fall open rather than sealing every door in the kingdom.
        // Same for a layout that failed validation: a malformed seed must not
        // trap the player behind doors no moon in it can ever open.
        if (!sInitialized || !file || !sSubareaKeyLayoutComplete)
            return true;

        const s32 slot = SubareaKeyDB::findKeySlot(
            kingdom, static_cast<SubareaKeyDB::SubareaType>(subareaType));
        if (slot < 0 || slot >= kMaxSubareaKeySlots)
            return true;

        const s32 item = sSubareaKeyItemBySlot[slot];
        if (!isValidMoon(item))
            return true;

        // HintInfo::isGet lags a just-collected shuffled item until its list is
        // opened, so query the Shine record itself (same reason as
        // getToadetteMedalCollectedCount).
        const ShineDB::ShineEntry* entry = sMoonEntries[item];
        const LD::MoonAccess& moon = LD::sMoons[item];
        return entry ? MoonHooks::isMoonAcquiredDirect(file, entry)
                     : file->customIsGotShine(moon.uid, moon.stage);
    }

    bool canEnterSubareaLink(GameDataFile* file, s32 linkIndex) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return true;
        if (!SubareaKeyDB::isGatedLink(linkIndex))
            return true;
        return hasSubareaKey(file, SubareaKeyDB::kingdomForLink(linkIndex),
            static_cast<s32>(SubareaKeyDB::typeForLink(linkIndex)));
    }

    bool isToadetteMedalItem(s32 uid, const char* stage) {
        if (!sInitialized || !Randomizer::isToadetteMedalModeEnabled())
            return false;
        const s32 item = findMoonItemByIdentity(uid, stage);
        return item >= 0 && sIsToadetteMedalItem[item];
    }

    // Number of medal ENTRIES in the pool - i.e. the number of rows the Toadette
    // Medals moon list has, and the valid range of getToadetteMedalAt. A triple
    // medal is one entry here even though it is worth three medals; use
    // getToadetteMedalTotalValue for anything that counts medals.
    s32 getToadetteMedalTotalCount() {
        if (!sInitialized)
            return 0;
        s32 count = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++)
            if (sIsToadetteMedalItem[item])
                count++;
        return count;
    }

    // Total MEDALS the seed generated, counting a triple medal as the three it
    // pays out. This is what the required-medal setting is measured against, so
    // it is the number to show opposite getToadetteMedalCollectedCount.
    s32 getToadetteMedalTotalValue() {
        if (!sInitialized)
            return 0;
        s32 medals = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++)
            if (sIsToadetteMedalItem[item])
                medals += toadetteMedalValueOf(item);
        return medals;
    }

    s32 getToadetteMedalValueAt(s32 index) {
        if (!sInitialized || index < 0)
            return 0;
        s32 seen = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!sIsToadetteMedalItem[item])
                continue;
            if (seen == index)
                return toadetteMedalValueOf(item);
            seen++;
        }
        return 0;
    }

    // Stable ascending-item-index enumeration of the medal pool, for UI display
    // (e.g. the Toadette Medals moon-list page).
    const ShineDB::ShineEntry* getToadetteMedalAt(s32 index) {
        if (!sInitialized || index < 0)
            return nullptr;
        s32 seen = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!sIsToadetteMedalItem[item])
                continue;
            if (seen == index)
                return sMoonEntries[item];
            seen++;
        }
        return nullptr;
    }

    bool hasTriggeredToadetteMedalWin() { return sToadetteMedalWinTriggered; }
    void setToadetteMedalWinTriggered(bool triggered) { sToadetteMedalWinTriggered = triggered; }

    // Medals collected so far, counting a collected triple medal as the three
    // medals it pays out (matching getToadetteMedalTotalValue and the required
    // count). Collecting one used to credit exactly one medal no matter how many
    // moons it gave.
    s32 getToadetteMedalCollectedCount(GameDataFile* file) {
        if (!sInitialized || !file)
            return 0;
        s32 medals = 0;
        for (s32 item = 0; item < LD::sMoonCount; item++) {
            if (!sIsToadetteMedalItem[item])
                continue;
            const LD::MoonAccess& moon = LD::sMoons[item];
            // HintInfo::isGet is refreshed when its physical kingdom list is
            // opened, so it can lag a just-collected shuffled item. Query the
            // actual Shine record to keep the Medal page accurate immediately.
            const ShineDB::ShineEntry* entry = sMoonEntries[item];
            const bool isCollected = entry
                ? MoonHooks::isMoonAcquiredDirect(file, entry)
                : file->customIsGotShine(moon.uid, moon.stage);
            if (isCollected)
                medals += toadetteMedalValueOf(item);
        }
        return medals;
    }

    bool isMultiMoonLocation(s32 uid, const char* stage) {
        if (uid < 0 || !stage)
            return false;
        // Resolve the multi flag straight from ShineDB so it is correct independent of
        // whether the placement tables have been built (moon rando may be disabled).
        // Intentionally the VANILLA flag: scenario progression is physical and must
        // not follow the "Randomize Triple Moons" reward override.
        const ShineDB::ShineEntry* entry = ShineDB::findEntry(uid, stage);
        return entry && (entry->flags & ShineDB::ShineFlag_Multi);
    }

    // Reward-multi status (does collecting this item pay out as a triple/grand)
    // honoring the "Randomize Triple Moons" override. Falls back to the vanilla
    // ShineFlag_Multi when the override is inactive. Used by the runtime reward,
    // count and visual hooks so a reshuffled triple pays out and looks correct.
    bool isRewardMultiMoon(s32 uid, const char* stage) {
        if (uid < 0 || !stage)
            return false;
        if (sRewardMultiOverrideActive) {
            const s32 item = findMoonItemByIdentity(uid, stage);
            if (item >= 0)
                return sRewardMultiOverride[item];
        }
        const ShineDB::ShineEntry* entry = ShineDB::findEntry(uid, stage);
        return entry && (entry->flags & ShineDB::ShineFlag_Multi);
    }

    bool isRewardMultiEntry(const ShineDB::ShineEntry* entry) {
        if (!entry)
            return false;
        if (sRewardMultiOverrideActive) {
            // sMoonEntries hold pointers into the same static ShineDB table.
            for (s32 item = 0; item < LD::sMoonCount; item++)
                if (sMoonEntries[item] == entry)
                    return sRewardMultiOverride[item];
            return isRewardMultiMoon(entry->uid, entry->stage);
        }
        return (entry->flags & ShineDB::ShineFlag_Multi) != 0;
    }

    // Kingdom a moon is assigned to after redistribution (default = home kingdom).
    // Downstream systems -- moon lists, per-kingdom counts, requirements, and the
    // role assigners (captures/abilities/rock keys/toadette) -- read this instead
    // of the static home kingdom so they operate on the reshuffled pool.
    s32 getAssignedKingdom(s32 uid, const char* stage) {
        const s32 item = findMoonItemByIdentity(uid, stage);
        if (item < 0)
            return -1;
        return sKingdomAssignmentActive
            ? sMoonAssignedKingdom[item]
            : LD::sMoons[item].kingdom;
    }

    s32 getAssignedKingdomForEntry(const ShineDB::ShineEntry* entry) {
        if (!entry)
            return -1;
        // Fast path: with no redistribution the answer is always the home kingdom,
        // so per-moon presentation callers don't pay a moon-table scan every seed.
        if (!sKingdomAssignmentActive)
            return entry->worldId;
        const s32 item = findMoonItemByEntryPointer(entry);
        if (item >= 0)
            return sMoonAssignedKingdom[item];
        // Moons outside the logic table keep their home kingdom, so callers that
        // sweep the whole ShineDB stay correct.
        const s32 assigned = getAssignedKingdom(entry->uid, entry->stage);
        return assigned >= 0 ? assigned : entry->worldId;
    }

    bool isKingdomRedistributionActive() { return sKingdomAssignmentActive; }

    u32 getMoonRoleLayoutHash() { return sMoonRoleLayoutHash; }

    bool getKingdomAllocationSummary(s32 worldId,
        KingdomAllocationSummary* outSummary) {
        if (!outSummary || worldId < 0 || worldId >= LD::sKingdomCount
            || !sKingdomAllocationSummaryReady)
            return false;
        *outSummary = sKingdomAllocationSummaries[worldId];
        return true;
    }

    bool isRemapDest(const char* objectId, const char* stage) {
        if (!sInitialized || !objectId || !stage)
            return false;
        if (objectId[0] != 'o' || objectId[1] != 'b' || objectId[2] != 'j')
            return false;

        s32 objNum = 0;
        const char* p = objectId + 3;
        while (*p >= '0' && *p <= '9')
            objNum = objNum * 10 + (*p++ - '0');
        if (*p != '\0')
            return false;

        const s32 uid = ShineDB::findUid(stage, objNum);
        if (uid < 0)
            return false;

        for (s32 i = 0; i < sCreditMapCount; i++)
            if (sCreditMap[i].dest->uid == uid && strEq(sCreditMap[i].dest->stage, stage))
                return true;

        return false;
    }

    s32 getPlacementCount() { return sPlacementCount; }

    s32 getAutoGrantCount() { return sAutoGrantCount; }

    s32 getAutoGrantItemIndex(s32 index) {
        if (index < 0 || index >= sAutoGrantCount)
            return -1;
        return sAutoGrantItems[index];
    }

    const ShineDB::ShineEntry* getMoonEntry(s32 index) {
        if (!sInitialized || !isValidMoon(index))
            return nullptr;
        return sMoonEntries[index];
    }

    const Placement* getPlacement(s32 index) {
        if (index < 0 || index >= sPlacementCount)
            return nullptr;
        return &sPlacements[index];
    }

    bool isCriticalPathPlacement(s32 index) {
        if (index < 0 || index >= sPlacementCount)
            return false;
        return sPlacementCritical[index];
    }

    s32 getPlacementItemForLocation(s32 index) {
        if (index < 0 || index >= LD::sMoonCount)
            return -1;
        return sPlacementItemForLocation[index];
    }

    u32 getLastEntranceAttempt() { return sLastEntranceAttempt; }

    s32 getEntranceWireStepCount() { return sWireStepCount; }

    bool getEntranceWireStep(s32 index, EntranceWireStep* out) {
        if (!out || index < 0 || index >= sWireStepCount)
            return false;
        const WireStep& step = sWireSteps[index];
        out->sourceLink = (s16)EntranceRando::doorLink(step.sourceDoor);
        out->targetLink = (s16)EntranceRando::doorLink(step.targetDoor);
        out->sourceSlot = (u8)EntranceRando::doorSlot(step.sourceDoor);
        out->targetSlot = (u8)EntranceRando::doorSlot(step.targetDoor);
        out->sphere = step.sphere;
        out->doorClass = step.doorClass;
        out->intent = step.intent;
        return true;
    }

    s32 getPathStepCount() { return sPathStepCount; }

    const PathStep* getPathStep(s32 index) {
        if (index < 0 || index >= sPathStepCount)
            return nullptr;
        return &sPathSteps[index];
    }

    s32 getOptimizedPathStepCount() { return sOptimizedPathStepCount; }

    const PathStep* getOptimizedPathStep(s32 index) {
        if (index < 0 || index >= sOptimizedPathStepCount)
            return nullptr;
        return &sOptimizedPathSteps[index];
    }

    s32 getSphereCount() { return sSphereCount; }

    const SphereInfo* getSphere(s32 index) {
        if (index < 0 || index >= sSphereCount)
            return nullptr;
        return &sSpheres[index];
    }

    bool finalObjectiveReached() { return sFinalObjectiveReached; }

    const char* getFinalObjectiveNote() { return sFinalObjectiveNote; }

    s32 getWarningCount() { return sWarningCount; }

    const char* getWarning(s32 index) {
        if (index < 0 || index >= sWarningCount)
            return nullptr;
        return sWarnings[index];
    }

    s32 getPaintingLinkCount() { return LD::sPaintingLinkCount; }

    PaintingLinkInfo getPaintingLinkInfo(s32 index) {
        PaintingLinkInfo info = { "???", "???", "???", "???", false };
        if (index < 0 || index >= LD::sPaintingLinkCount)
            return info;

        const LD::PaintingLink& link = LD::sPaintingLinks[index];
        const bool paintingRandoActive = Randomizer::isPaintingRandoEnabled() && PaintingRando::isInitialized();
        const s32 sourceNodeIndex = link.sourceNode;
        const s32 destinationNodeIndex = paintingRandoActive
            ? PaintingRando::getMappedDestinationNode(index)
            : link.destinationNode;
        info.bidirectional = link.bidirectional;

        if (sourceNodeIndex >= 0 && sourceNodeIndex < LD::sNodeCount) {
            const LD::Node& node = LD::sNodes[sourceNodeIndex];
            info.sourceNode = node.nodeId ? node.nodeId : "???";
            if (isValidStage(node.stage)) {
                const s32 kingdom = LD::sStages[node.stage].kingdom;
                info.sourceKingdom = kingdomName(kingdom);
            }
        }

        if (destinationNodeIndex >= 0 && destinationNodeIndex < LD::sNodeCount) {
            const LD::Node& node = LD::sNodes[destinationNodeIndex];
            info.destinationNode = node.nodeId ? node.nodeId : "???";
            if (isValidStage(node.stage)) {
                const s32 kingdom = LD::sStages[node.stage].kingdom;
                info.destinationKingdom = kingdomName(kingdom);
            }
        }

        return info;
    }

} // namespace MoonRando
