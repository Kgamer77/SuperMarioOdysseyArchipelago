#include "Randomizer/EnemySwapRando.h"

#include "Randomizer/EnemySwapDebug.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/Randomizer.h"

#include <cstring>

namespace EnemySwapRando {

    namespace DB = Randomizer::EnemySwapDB;
    namespace LD = Randomizer::LogicData;

    static constexpr u32 kAttemptSalt = 0x165667B1u;
    static constexpr u32 kDomainSalt = 0xA11CE5D2u;
    static constexpr u32 kAnySalt = 0x27220A95u;
    static constexpr u32 kPropSalt = 0xB5297A4Du;
    static constexpr u32 kPropDeleteSalt = 0xD31E7E5Bu;
    static constexpr u32 kPropDeleteChancePercent = 25u;
    static constexpr u32 kSkipSalt = 0x7F4A9C2Du;
    static constexpr u32 kTokenMix = 0x9E3779B1u;
    static constexpr s32 kMaxPlacementScenario = 15;
    static constexpr s32 kMaxOptionScratch = 32;
    static constexpr s32 kMaxCaptureScratch = 16;

    struct AssignmentChange {
        u16 placement;
        s16 entity;
        const char* forcedCapture;
    };

    struct RouteConstraintInstance {
        u16 edge;
        s16 scenario;
        u16 candidateCount;
        u32 overlapScore;
    };

    struct RouteWitness {
        u16 edge;
        s16 scenario;
        s16 gate;
    };

    static bool sInitialized = false;
    static bool sAssignmentValid = false;
    static FailureCode sFailureCode = FailureCode::None;
    static const char* sMemoryFailureStage = nullptr;
    static s16 sMemoryFailureScenario = -1;
    static u32 sMemoryFailureExtraResource = 0;
    static u32 sMemoryFailureMaxResource = 0;
    static u32 sMemoryFailureExtraScene = 0;
    static u32 sMemoryFailureMaxScene = 0;
    static const char* sRestrictionFailureStage = nullptr;
    static s16 sRestrictionFailureScenario = -1;
    static s16 sRestrictionFailureEntity = -1;
    static s32 sRestrictionFailureCount = 0;
    static s32 sRestrictionFailureMax = 0;
    static u32 sLastSeed = 0xFFFFFFFFu;
    static u32 sLastAttempt = 0xFFFFFFFFu;
    // Reroll state. The base is captured once so that rolls count from the
    // seed's own assignment rather than compounding off the previous roll.
    static u32 sRerollBaseSeed = 0;
    static u32 sRerollBaseAttempt = 0;
    static bool sRerollBaseValid = false;
    static u32 sRerollCount = 0;
    static bool sLastEnemyEnabled = false;
    // Modes, not booleans: SameCategory, WithCombat and WithDeletion all read
    // as "enabled" but resolve to different assignments, so a boolean cache
    // key would serve a stale assignment when the player moves between them.
    static Randomizer::ChaosSwapMode sLastNpcMode = Randomizer::ChaosSwapMode::Off;
    static Randomizer::ChaosSwapMode sLastPropMode = Randomizer::ChaosSwapMode::Off;
    static bool sLastTwoDEnabled = false;
    static u8 sLastPropChance = 0;
    static u8 sLastLogicDifficulty = 0xFFu;
    static u32 sInspectorResolutionLogCount = 0;

    static s16 sResolvedEntity[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1];
    static const char* sForcedCapture[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1];
    static AssignmentChange sChangeLog[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1];
    static s32 sChangeCount = 0;
    // Final-fill Bowser uniqueness cache.  Broad prop participation adds
    // thousands of placements; rescanning the full placement table for every
    // one would turn assignment construction quadratic.
    static const char* sBowserResolvedStages[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1];
    static s32 sBowserResolvedStageCount = 0;
    static RouteConstraintInstance sRouteConstraints[LD::sRouteEdgeCount * kMaxPlacementScenario > 0
            ? LD::sRouteEdgeCount* kMaxPlacementScenario
            : 1];
    static s32 sRouteConstraintCount = 0;
    static u16 sMemberFrequency[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1];
    static RouteWitness sWitnessLog[DB::sGateGroupCount * kMaxPlacementScenario > 0
            ? DB::sGateGroupCount* kMaxPlacementScenario
            : 1];
    static s32 sWitnessCount = 0;

    static u32 fnv1a(const char* value) {
        u32 hash = 2166136261u;
        if (!value)
            return hash;
        while (*value) {
            hash ^= (u8)*value++;
            hash *= 16777619u;
        }
        return hash;
    }

    static u32 mixHash(u32 value) {
        value += 0x9E3779B9u;
        value = (value ^ (value >> 16)) * 0x85EBCA6Bu;
        value = (value ^ (value >> 13)) * 0xC2B2AE35u;
        return value ^ (value >> 16);
    }

    static u32 swapBase(u32 seed, u32 attempt) {
        return seed ^ (attempt * kAttemptSalt) ^ kDomainSalt;
    }

    static bool strEq(const char* left, const char* right) {
        return left && right && std::strcmp(left, right) == 0;
    }

    static bool hasCategory(const DB::Entity& entity, const char* category);

    static const DB::EntityMemoryCost* memoryCost(s16 entityIndex) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount
            || !DB::sMemoryDataAvailable)
            return nullptr;
        return &DB::sEntityMemoryCosts[entityIndex];
    }

    static u32 memoryResourceBytes(const u16* counts) {
        bool seen[DB::sMemoryResourceCount > 0
                ? DB::sMemoryResourceCount
                : 1]
            = {};
        u32 total = 0;
        for (s32 entityIndex = 0; entityIndex < DB::sEntityCount;
             entityIndex++) {
            if (!counts[entityIndex])
                continue;
            const DB::EntityMemoryCost& cost = DB::sEntityMemoryCosts[entityIndex];
            for (u16 offset = 0; offset < cost.resourceKeyCount; offset++) {
                const u16 refIndex = cost.resourceKeyOffset + offset;
                if (refIndex >= DB::sMemoryResourceRefCount)
                    continue;
                const u16 resourceIndex = DB::sMemoryResourceRefs[refIndex].resource;
                if (resourceIndex >= DB::sMemoryResourceCount || seen[resourceIndex])
                    continue;
                seen[resourceIndex] = true;
                total += DB::sMemoryResources[resourceIndex].bytes;
            }
        }
        return total;
    }

    static u32 memorySceneBytes(const u16* counts) {
        u32 total = 0;
        for (s32 entityIndex = 0; entityIndex < DB::sEntityCount;
             entityIndex++) {
            const u32 count = counts[entityIndex];
            if (!count)
                continue;
            const DB::EntityMemoryCost& cost = DB::sEntityMemoryCosts[entityIndex];
            if (hasCategory(DB::sEntities[entityIndex], "generator")) {
                total += cost.generatorPeakBytes * count;
            } else {
                total += cost.firstInstanceBytes;
                total += (count - 1) * cost.additionalInstanceBytes;
            }
        }
        return total;
    }

    // Placement scenario masks are 0-based (bit 0 is ScenarioNo 1) while every
    // audit artifact reports the game's 1-based ScenarioNo. Mixing the two
    // silently selects a neighbouring scenario's placements, so the conversion
    // lives in one place. Mirrors scenario_mask_bit() in logic/enemy_swap.py.
    static u32 scenarioMaskBit(s16 scenario) {
        return scenario >= 1 ? (1u << (scenario - 1)) : 0u;
    }

    // Whether a placement sits inside a restriction's stage+scenario.
    // The audit names the physical stage, but placements inside an embedded
    // zone carry the zone as `stage` and the physical stage only as
    // `logicStage` -- CityWorldFactory01Zone/obj632 is in
    // CityWorldFactoryStage. Matching both is what the audit itself does when
    // it picks its source placements, so a restriction derived from that
    // context has to use the same rule or it covers nothing.
    // Mirrors SpawnRestriction.applies_to in logic/enemy_swap.py.
    static bool restrictionAppliesTo(
        const DB::EnemySpawnRestriction& rule, const DB::Placement& placement) {
        if (!strEq(placement.stage, rule.stage)
            && !strEq(placement.logicStage, rule.stage))
            return false;
        return (placement.scenarioMask & scenarioMaskBit(rule.scenario)) != 0;
    }

    // How many placements a restriction governs already resolve to its entity,
    // honouring one pending override so a candidate can be tested before it is
    // committed.
    static s32 countResolvedInScenario(
        const DB::EnemySpawnRestriction& rule,
        s32 overridePlacement, s16 overrideEntity) {
        s32 count = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (!restrictionAppliesTo(rule, DB::sPlacements[i]))
                continue;
            const s16 resolved = i == overridePlacement
                ? overrideEntity
                : sResolvedEntity[i];
            if (resolved == (s16)rule.entity)
                count++;
        }
        return count;
    }

    static bool checkSpawnRestrictions(
        s32 overridePlacement = -1, s16 overrideEntity = -1) {
        if (DB::sEnemySpawnRestrictionCount <= 0)
            return true;
        for (s32 i = 0; i < DB::sEnemySpawnRestrictionCount; i++) {
            const DB::EnemySpawnRestriction& rule = DB::sEnemySpawnRestrictions[i];
            const s32 count = countResolvedInScenario(
                rule, overridePlacement, overrideEntity);
            if (count > rule.maxInstances) {
                sRestrictionFailureStage = rule.stage;
                sRestrictionFailureScenario = rule.scenario;
                sRestrictionFailureEntity = (s16)rule.entity;
                sRestrictionFailureCount = count;
                sRestrictionFailureMax = rule.maxInstances;
                return false;
            }
        }
        return true;
    }

    static bool checkMemoryBudgets(
        s32 overridePlacement = -1, s16 overrideEntity = -1) {
        if (!DB::sMemoryDataAvailable || DB::sStageMemoryBudgetCount <= 0)
            return true;
        u16 vanillaCounts[DB::sEntityCount > 0 ? DB::sEntityCount : 1] = {};
        u16 resolvedCounts[DB::sEntityCount > 0 ? DB::sEntityCount : 1] = {};
        for (s32 budgetIndex = 0;
             budgetIndex < DB::sStageMemoryBudgetCount; budgetIndex++) {
            const DB::StageMemoryBudget& budget = DB::sStageMemoryBudgets[budgetIndex];
            for (s32 entityIndex = 0; entityIndex < DB::sEntityCount;
                 entityIndex++) {
                vanillaCounts[entityIndex] = 0;
                resolvedCounts[entityIndex] = 0;
            }
            for (s32 placementIndex = 0;
                 placementIndex < DB::sPlacementCount; placementIndex++) {
                const DB::Placement& placement = DB::sPlacements[placementIndex];
                if (!strEq(placement.stage, budget.stage)
                    || !(placement.scenarioMask & (1u << budget.scenario)))
                    continue;
                if (placement.entity >= 0
                    && placement.entity < DB::sEntityCount)
                    vanillaCounts[placement.entity]++;
                const s16 resolved = placementIndex == overridePlacement
                    ? overrideEntity
                    : sResolvedEntity[placementIndex];
                if (resolved >= 0 && resolved < DB::sEntityCount)
                    resolvedCounts[resolved]++;
            }
            const u32 vanillaResource = memoryResourceBytes(vanillaCounts);
            const u32 resolvedResource = memoryResourceBytes(resolvedCounts);
            const u32 vanillaScene = memorySceneBytes(vanillaCounts);
            const u32 resolvedScene = memorySceneBytes(resolvedCounts);
            const s32 extraResource = (s32)resolvedResource
                - (s32)vanillaResource;
            const s32 extraScene = (s32)resolvedScene - (s32)vanillaScene;
            if ((s64)extraResource + budget.reserveResourceBytes
                    > budget.maxExtraResourceBytes
                || (s64)extraScene + budget.reserveSceneBytes
                    > budget.maxExtraSceneBytes) {
                sMemoryFailureStage = budget.stage;
                sMemoryFailureScenario = budget.scenario;
                sMemoryFailureExtraResource = extraResource > 0
                    ? (u32)extraResource
                    : 0;
                sMemoryFailureMaxResource = budget.maxExtraResourceBytes;
                sMemoryFailureExtraScene = extraScene > 0
                    ? (u32)extraScene
                    : 0;
                sMemoryFailureMaxScene = budget.maxExtraSceneBytes;
                return false;
            }
        }
        return true;
    }

    static bool isBowserEntity(s16 entityIndex) {
        return entityIndex >= 0 && entityIndex < DB::sEntityCount
            && strEq(DB::sEntities[entityIndex].name, "Koopa");
    }

    // This tracks only Bowser assignments introduced by Chaos. A native
    // Koopa placement remains untouched, while a second randomly assigned
    // Koopa in its logical stage is rejected before it reaches the runtime
    // spawn cap in EnemySwapHooks.
    static bool isBowserReplacement(s32 placementIndex, s16 entityIndex) {
        return placementIndex >= 0 && placementIndex < DB::sPlacementCount
            && entityIndex != DB::sPlacements[placementIndex].entity
            && isBowserEntity(entityIndex);
    }

    static const char* placementStageKey(const DB::Placement& placement) {
        return placement.logicStage ? placement.logicStage : placement.stage;
    }

    static bool stageHasBowserReplacement(
        const DB::Placement& placement, s32 ignoredPlacement = -1) {
        const char* stage = placementStageKey(placement);
        if (!stage)
            return false;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (i == ignoredPlacement
                || !strEq(DB::sPlacements[i].logicStage
                        ? DB::sPlacements[i].logicStage
                        : DB::sPlacements[i].stage,
                    stage))
                continue;
            if (isBowserReplacement(i, sResolvedEntity[i]))
                return true;
        }
        return false;
    }

    static bool finalFillStageHasBowser(const DB::Placement& placement) {
        const char* stage = placementStageKey(placement);
        for (s32 i = 0; i < sBowserResolvedStageCount; i++)
            if (strEq(sBowserResolvedStages[i], stage))
                return true;
        return false;
    }

    static void noteFinalFillBowserStage(const DB::Placement& placement) {
        if (finalFillStageHasBowser(placement)
            || sBowserResolvedStageCount >= DB::sPlacementCount)
            return;
        sBowserResolvedStages[sBowserResolvedStageCount++] = placementStageKey(placement);
    }

    static bool canResolvePlacementTo(
        s32 placementIndex, s16 entityIndex) {
        if (!isBowserReplacement(placementIndex, entityIndex))
            return true;
        return !stageHasBowserReplacement(
            DB::sPlacements[placementIndex], placementIndex);
    }

    static bool hasCategory(const DB::Entity& entity, const char* category) {
        return strEq(entity.category, category);
    }

    // stage=nullptr checks across every stage (matches a gate anywhere);
    // otherwise only a gate authored for this exact (stage, tokenKey) pair
    // counts. See the header comment for the vanilla-until-gated model.
    bool isReliableCaptureToken(const char* stage, const char* tokenKey) {
        if (!tokenKey)
            return false;
        for (s32 i = 0; i < DB::sReliableCaptureCount; i++)
            if (strEq(DB::sReliableCaptureTokens[i], tokenKey))
                return true;
        for (s32 i = 0; i < DB::sGateCount; i++) {
            if (!strEq(DB::sGates[i].sourceCaptureToken, tokenKey))
                continue;
            if (!stage || strEq(DB::sGates[i].logicStage, stage))
                return false;
        }
        return true;
    }

    static bool isStageLockedCapture(const char* logicStage, const char* tokenKey) {
        return strEq(logicStage, "KaronWingTowerStage")
            && strEq(tokenKey, "KaronWing");
    }

    static s32 findLogicTokenIndex(const char* token) {
        if (!token)
            return -1;
        for (s32 i = 0; i < LD::sTokenCount; i++)
            if (strEq(LD::sTokens[i].key, token))
                return i;
        return -1;
    }

    static s32 comparePlacementKey(
        const DB::Placement& placement, const char* stage, const char* objId) {
        const s32 stageOrder = std::strcmp(placement.stage, stage);
        if (stageOrder != 0)
            return stageOrder;
        return std::strcmp(placement.objId, objId);
    }

    // StageData filenames conventionally append "Map", while the generated
    // placement table stores the stage key without that suffix. Runtime
    // PlacementFileName values are not uniform across embedded zones, so the
    // exact binary lookup below is followed by this bounded alias match.
    static bool runtimeStageMatches(
        const char* tableStage, const char* runtimeStage) {
        if (!tableStage || !runtimeStage)
            return false;
        if (std::strcmp(tableStage, runtimeStage) == 0)
            return true;
        const size_t tableLength = std::strlen(tableStage);
        const size_t runtimeLength = std::strlen(runtimeStage);
        if (tableLength + 3 == runtimeLength
            && std::strncmp(runtimeStage, tableStage, tableLength) == 0
            && std::strcmp(runtimeStage + tableLength, "Map") == 0)
            return true;
        if (runtimeLength + 3 == tableLength
            && std::strncmp(tableStage, runtimeStage, runtimeLength) == 0
            && std::strcmp(tableStage + runtimeLength, "Map") == 0)
            return true;
        return false;
    }

    const DB::Placement* findPlacement(
        const char* stage, const char* objId, s32* placementIndex) {
        if (!stage || !objId)
            return nullptr;
        s32 low = 0;
        s32 high = DB::sPlacementCount - 1;
        while (low <= high) {
            const s32 mid = low + (high - low) / 2;
            const s32 order = comparePlacementKey(
                DB::sPlacements[mid], stage, objId);
            if (order == 0) {
                if (placementIndex)
                    *placementIndex = mid;
                return &DB::sPlacements[mid];
            }
            if (order < 0)
                low = mid + 1;
            else
                high = mid - 1;
        }
        return nullptr;
    }

    static bool placementStageExists(const char* stage) {
        if (!stage)
            return false;
        s32 low = 0;
        s32 high = DB::sPlacementCount - 1;
        while (low <= high) {
            const s32 mid = low + (high - low) / 2;
            const s32 order = std::strcmp(DB::sPlacements[mid].stage, stage);
            if (order == 0)
                return true;
            if (order < 0)
                low = mid + 1;
            else
                high = mid - 1;
        }
        return false;
    }

    bool isKnownPlacementStage(const char* stage) {
        if (!stage)
            return false;
        if (placementStageExists(stage))
            return true;
        const size_t stageLength = std::strlen(stage);
        if (stageLength <= 3
            || std::strcmp(stage + stageLength - 3, "Map") != 0)
            return false;
        char normalizedStage[128];
        const size_t normalizedLength = stageLength - 3;
        if (normalizedLength >= sizeof(normalizedStage))
            return false;
        std::memcpy(normalizedStage, stage, normalizedLength);
        normalizedStage[normalizedLength] = '\0';
        return placementStageExists(normalizedStage);
    }

    bool sourceEntityMatchesUnitConfig(
        s16 entityIndex, const char* unitConfigName) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount
            || !unitConfigName)
            return false;
        const char* entityName = DB::sEntities[entityIndex].name;
        if (strEq(entityName, unitConfigName))
            return true;

        if (strEq(unitConfigName, "SandWorldHomeIcePillar")) {
            return strEq(entityName, "SandWorldHomeIcePillar000")
                || strEq(entityName, "SandWorldHomeIcePillar001")
                || strEq(entityName, "SandWorldHomeIcePillar002")
                || strEq(entityName, "SandWorldHomeIcePillar003")
                || strEq(entityName, "SandWorldHomeIcePillar004");
        }
        if (strEq(unitConfigName, "CrystalBreakable")) {
            return strEq(entityName, "CrystalBreakableIce")
                || strEq(entityName, "CrystalSmallBreakableIce");
        }
        return false;
    }

    const DB::Placement* findPlacementForRuntime(
        const char* stage, const char* objId, const char* unitConfigName,
        s32* placementIndex) {
        if (!objId)
            return nullptr;

        // The normal path is both fastest and the most precise one. Do not
        // accept it when the extracted source actor disagrees with the live
        // placement: object IDs are only unique within a stage file.
        if (stage) {
            s32 exactIndex = -1;
            const DB::Placement* exact = findPlacement(
                stage, objId, &exactIndex);
            if (exact) {
                if (!unitConfigName
                    || sourceEntityMatchesUnitConfig(
                        exact->entity, unitConfigName)) {
                    if (placementIndex)
                        *placementIndex = exactIndex;
                    return exact;
                }
                // An authoritative physical key with a stale actor identity
                // must fail closed, not search for a similarly numbered
                // object in some unrelated stage.
                return nullptr;
            }

            // Runtime PlacementFileName normally includes the archive's
            // trailing "Map", while the generated table intentionally stores
            // the stage name without it. Normalize that common spelling
            // before falling back to a full identity scan.
            const size_t stageLength = std::strlen(stage);
            if (stageLength > 3
                && std::strcmp(stage + stageLength - 3, "Map") == 0) {
                char normalizedStage[128];
                const size_t normalizedLength = stageLength - 3;
                if (normalizedLength < sizeof(normalizedStage)) {
                    std::memcpy(normalizedStage, stage, normalizedLength);
                    normalizedStage[normalizedLength] = '\0';
                    exact = findPlacement(
                        normalizedStage, objId, &exactIndex);
                    if (exact) {
                        if (!unitConfigName
                            || sourceEntityMatchesUnitConfig(
                                exact->entity, unitConfigName)) {
                            if (placementIndex)
                                *placementIndex = exactIndex;
                            return exact;
                        }
                        return nullptr;
                    }

                    // The runtime gave us a real generated physical stage,
                    // and this object simply is not in the swap inventory.
                    // Returning here keeps ordinary stage objects O(log N).
                    if (placementStageExists(normalizedStage))
                        return nullptr;
                }
            }
        }

        if (!unitConfigName)
            return nullptr;

        // A PlacementInfo built for an embedded zone is not guaranteed to
        // expose the same stage key used by the extracted CSV. Recover only
        // when the object ID + UnitConfigName identify one generated row, so
        // stale or ambiguous data still fails closed.
        const DB::Placement* stageCandidate = nullptr;
        s32 stageCandidateIndex = -1;
        s32 stageCandidateCount = 0;
        const DB::Placement* logicalCandidate = nullptr;
        s32 logicalCandidateIndex = -1;
        s32 logicalCandidateCount = 0;
        const DB::Placement* candidate = nullptr;
        s32 candidateIndex = -1;
        s32 candidateCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            const DB::Placement& placement = DB::sPlacements[i];
            if (!strEq(placement.objId, objId)
                || !sourceEntityMatchesUnitConfig(
                    placement.entity, unitConfigName))
                continue;

            // Prefer an explicitly matching physical stage. This also
            // accepts the harmless Stage/StageMap spelling difference.
            if (stage && runtimeStageMatches(placement.stage, stage)) {
                stageCandidate = &placement;
                stageCandidateIndex = i;
                stageCandidateCount++;
                continue;
            }

            // Prefer the logical stage when the runtime supplied a main-stage
            // name for a placement authored inside an embedded zone.
            if (stage && runtimeStageMatches(placement.logicStage, stage)) {
                logicalCandidate = &placement;
                logicalCandidateIndex = i;
                logicalCandidateCount++;
                continue;
            }

            if (!candidate) {
                candidate = &placement;
                candidateIndex = i;
            }
            candidateCount++;
        }
        if (stageCandidateCount >= 1) {
            if (placementIndex)
                *placementIndex = stageCandidateIndex;
            return stageCandidate;
        }
        if (logicalCandidateCount >= 1) {
            // Duplicate object IDs are present in a few embedded zones. The
            // concrete row is only a generated-assignment fallback; the
            // inspector resolver runs again in findReplacement(), where it
            // can reject an ambiguous pair instead of choosing a first row.
            if (placementIndex)
                *placementIndex = logicalCandidateIndex;
            return logicalCandidate;
        }
        if (candidateCount != 1)
            return nullptr;
        if (placementIndex)
            *placementIndex = candidateIndex;
        return candidate;
    }

    static bool shouldSwapProp(const DB::Placement& placement) {
        const u8 chance = Randomizer::getPropSwapChancePercent();
        if (chance >= 100)
            return true;
        if (chance == 0)
            return false;
        const u32 roll = mixHash(
            swapBase(sLastSeed, sLastAttempt)
            ^ fnv1a(placement.stage)
            ^ (fnv1a(placement.objId) * kTokenMix)
            ^ kPropSalt);
        return roll % 100u < chance;
    }

    static bool shouldDeleteProp(const DB::Placement& placement) {
        if (Randomizer::getPropSwapMode()
            != Randomizer::ChaosSwapMode::WithDeletion)
            return false;
        const u32 roll = mixHash(
            swapBase(sLastSeed, sLastAttempt)
            ^ fnv1a(placement.stage)
            ^ (fnv1a(placement.objId) * kTokenMix)
            ^ kPropDeleteSalt);
        return roll % 100u < kPropDeleteChancePercent;
    }

    static bool categoryEnabled(const DB::Placement& placement) {
        if (placement.entity < 0 || placement.entity >= DB::sEntityCount)
            return false;
        const DB::Entity& entity = DB::sEntities[placement.entity];
        if (hasCategory(entity, "capture") || hasCategory(entity, "enemy")
            || hasCategory(entity, "generator"))
            return Randomizer::isEnemySwapRandoEnabled();
        if (hasCategory(entity, "enemy2d"))
            return Randomizer::isTwoDSwapRandoEnabled();
        if (hasCategory(entity, "npc"))
            return Randomizer::isNpcSwapRandoEnabled();
        if (hasCategory(entity, "prop"))
            return Randomizer::isPropSwapRandoEnabled()
                && shouldSwapProp(placement);
        return false;
    }

    static const DB::SwapRule* placementRule(const DB::Placement& placement) {
        if (placement.rule < 0 || placement.rule >= DB::sRuleCount)
            return nullptr;
        return &DB::sRules[placement.rule];
    }

    static bool placementCanRandomize(const DB::Placement& placement) {
        const DB::SwapRule* rule = placementRule(placement);
        if (placement.entity < 0
            || placement.entity >= DB::sEntityCount
            || !DB::sEntities[placement.entity].swapOut)
            return false;
        return rule && rule->policy == DB::Policy_Randomize
            && categoryEnabled(placement);
    }

    static bool placementCanServeCaptureGate(
        const DB::Placement& placement) {
        if (placement.entity < 0 || placement.entity >= DB::sEntityCount)
            return false;
        const DB::Entity& entity = DB::sEntities[placement.entity];
        return !hasCategory(entity, "npc")
            || Randomizer::isNpcSwapRandoEnabled();
    }

    static s32 countCaptureProducers(const char* token) {
        s32 count = 0;
        for (s32 i = 0; i < DB::sEntityCount; i++)
            if (DB::sEntities[i].fullActorTarget
                && strEq(DB::sEntities[i].captureToken, token))
                count++;
        return count;
    }

    static s16 pickCaptureProducer(
        const DB::Placement& placement, const DB::CaptureGate& gate) {
        const s32 count = countCaptureProducers(gate.replacementCaptureToken);
        if (count <= 0)
            return -1;
        const u32 pick = mixHash(
                             swapBase(sLastSeed, sLastAttempt)
                             ^ fnv1a(gate.gateId)
                             ^ fnv1a(placement.stage)
                             ^ (fnv1a(placement.objId) * kTokenMix))
            % (u32)count;
        u32 remaining = pick;
        for (s32 i = 0; i < DB::sEntityCount; i++) {
            if (!DB::sEntities[i].fullActorTarget
                || !strEq(DB::sEntities[i].captureToken,
                    gate.replacementCaptureToken))
                continue;
            if (remaining == 0)
                return (s16)i;
            remaining--;
        }
        return -1;
    }

    static bool memberCanForce(u16 placementIndex, const DB::CaptureGate& gate) {
        if (placementIndex >= DB::sPlacementCount)
            return false;
        if (!placementCanServeCaptureGate(
                DB::sPlacements[placementIndex]))
            return false;
        if (sForcedCapture[placementIndex]
            && !strEq(sForcedCapture[placementIndex],
                gate.replacementCaptureToken))
            return false;
        const DB::Placement& placement = DB::sPlacements[placementIndex];
        const s16 current = sResolvedEntity[placementIndex];
        if (current >= 0 && current < DB::sEntityCount
            && strEq(DB::sEntities[current].captureToken,
                gate.replacementCaptureToken))
            return true;
        return placementCanRandomize(placement)
            && countCaptureProducers(gate.replacementCaptureToken) > 0;
    }

    static bool forceMember(u16 placementIndex, const DB::CaptureGate& gate) {
        if (placementIndex >= DB::sPlacementCount)
            return false;
        if (!placementCanServeCaptureGate(
                DB::sPlacements[placementIndex]))
            return false;
        if (sForcedCapture[placementIndex]
            && !strEq(sForcedCapture[placementIndex],
                gate.replacementCaptureToken))
            return false;

        const s16 current = sResolvedEntity[placementIndex];
        const char* currentToken = current >= 0 && current < DB::sEntityCount
            ? DB::sEntities[current].captureToken
            : nullptr;
        s16 replacement = current;
        if (!strEq(currentToken, gate.replacementCaptureToken)) {
            replacement = pickCaptureProducer(
                DB::sPlacements[placementIndex], gate);
            if (replacement < 0)
                return false;
        }
        if (!canResolvePlacementTo(placementIndex, replacement))
            return false;
        if (!sForcedCapture[placementIndex]) {
            if (sChangeCount >= DB::sPlacementCount)
                return false;
            sChangeLog[sChangeCount++] = {
                placementIndex, current, sForcedCapture[placementIndex]
            };
            sForcedCapture[placementIndex] = gate.replacementCaptureToken;
            sResolvedEntity[placementIndex] = replacement;
        }
        return true;
    }

    static void rollbackChanges(s32 mark) {
        while (sChangeCount > mark) {
            const AssignmentChange& change = sChangeLog[--sChangeCount];
            sResolvedEntity[change.placement] = change.entity;
            sForcedCapture[change.placement] = change.forcedCapture;
        }
    }

    static s32 gateRequiredCount(const DB::CaptureGate& gate) {
        if (gate.mode == DB::GateMode_All)
            return gate.memberCount;
        if (gate.mode == DB::GateMode_Any || gate.mode == DB::GateMode_AtMost)
            return 1;
        return gate.minimumCount;
    }

    static bool gateDifficultyAllowed(const DB::CaptureGate& gate) {
        return (s32)gate.difficulty
            <= (s32)Randomizer::getLogicDifficulty();
    }

    static bool routeOptionScenarioAllowed(
        const LD::RouteEdge& edge, s32 optionIndex, s16 scenario) {
        if (edge.optionScenarioCount <= 0 || scenario < 0
            || scenario > 5)
            return true;
        s32 index = edge.optionScenarioOffset;
        if (edge.optionScenarioCount > 1)
            index += optionIndex;
        if (index < 0 || index >= LD::sOptionScenarioMaskCount)
            return true;
        const u32 mask = LD::sOptionScenarioMasks[index];
        return mask == 0 || (mask & (1u << scenario)) != 0;
    }

    static bool routeOptionDifficultyAllowed(
        const LD::RouteEdge& edge, s32 optionIndex) {
        if (edge.difficultyCount <= 0)
            return true;
        s32 index = edge.difficultyOffset;
        if (edge.difficultyCount > 1)
            index += optionIndex;
        if (index < 0 || index >= LD::sRouteOptionDifficultyCount)
            return true;
        return (s32)LD::sRouteOptionDifficulties[index]
            <= (s32)Randomizer::getLogicDifficulty();
    }

    static const char* routeLogicStage(const LD::RouteEdge& edge) {
        if (edge.fromNode < 0 || edge.fromNode >= LD::sNodeCount)
            return nullptr;
        const s16 stage = LD::sNodes[edge.fromNode].stage;
        return stage >= 0 && stage < LD::sStageCount
            ? LD::sStages[stage].stage
            : nullptr;
    }

    static bool routeTokenNeedsGate(
        const LD::RouteEdge& edge, s32 tokenIndex) {
        if (tokenIndex < 0 || tokenIndex >= LD::sTokenCount
            || LD::sTokens[tokenIndex].type != LD::TokenType_Capture)
            return false;
        const char* token = LD::sTokens[tokenIndex].key;
        const char* stage = routeLogicStage(edge);
        if (isReliableCaptureToken(stage, token))
            return false;
        return !isStageLockedCapture(stage, token);
    }

    static s32 findGateGroup(
        const char* edgeId, const char* sourceToken) {
        for (s32 i = 0; i < DB::sGateGroupCount; i++)
            if (strEq(DB::sGateGroups[i].edgeId, edgeId)
                && strEq(
                    DB::sGateGroups[i].sourceCaptureToken,
                    sourceToken))
                return i;
        return -1;
    }

    static bool memberActiveInScenario(
        u16 placementIndex, s16 scenario) {
        if (placementIndex >= DB::sPlacementCount)
            return false;
        const s32 scenarioIndex = scenario > 0
            ? scenario - 1
            : scenario;
        return scenarioIndex < 0 || scenarioIndex >= 32
            || (DB::sPlacements[placementIndex].scenarioMask
                   & (1u << scenarioIndex))
            != 0;
    }

    static s32 eligibleMemberCount(
        const DB::CaptureGate& gate, s16 scenario) {
        s32 count = 0;
        for (s32 i = 0; i < gate.memberCount; i++) {
            const u16 member = DB::sGateMembers[gate.memberOffset + i];
            if (memberActiveInScenario(member, scenario)
                && memberCanForce(member, gate))
                count++;
        }
        return count;
    }

    static u16 eligibleMemberAt(
        const DB::CaptureGate& gate, s16 scenario,
        s32 logicalIndex, s32 eligibleCount) {
        if (eligibleCount <= 0)
            return 0xFFFFu;
        const s32 rotation = (s32)(mixHash(
                                       swapBase(sLastSeed, sLastAttempt)
                                       ^ fnv1a(gate.gateId)
                                       ^ ((u32)scenario * kTokenMix))
            % (u32)eligibleCount);
        const s32 wanted = (rotation + logicalIndex)
            % eligibleCount;
        s32 seen = 0;
        for (s32 i = 0; i < gate.memberCount; i++) {
            const u16 member = DB::sGateMembers[gate.memberOffset + i];
            if (!memberActiveInScenario(member, scenario)
                || !memberCanForce(member, gate))
                continue;
            if (seen++ == wanted)
                return member;
        }
        return 0xFFFFu;
    }

    // Members whose currently resolved entity already satisfies this gate's
    // replacement token (e.g. forced by an earlier placement-scenario pass
    // of this same requirement). Locks up to `required` of them via
    // forceMember() -- reserving forced_tokens so the later unconstrained-
    // placement pass can't overwrite them -- and returns how many were
    // locked. Rolls back nothing itself; the caller marks/rolls back.
    // Deliberately checks sForcedCapture (an earlier explicit decision)
    // rather than each member's raw resolved entity, so a member that
    // merely still sits at its unmodified vanilla value (true for every
    // member before any gate in this group has been decided) is never
    // mistaken for an earlier deliberate choice -- that would bias an
    // identity (source == replacement) gate to always win instead of being
    // chosen through the same fair rotation as any other alternative.
    static s32 forceMatchingMembers(
        const DB::CaptureGate& gate, s16 scenario, s32 required) {
        s32 forced = 0;
        for (s32 i = 0; i < gate.memberCount && forced < required; i++) {
            const u16 member = DB::sGateMembers[gate.memberOffset + i];
            if (member >= DB::sPlacementCount)
                continue;
            if (!memberActiveInScenario(member, scenario))
                continue;
            if (!sForcedCapture[member]
                || !strEq(sForcedCapture[member],
                    gate.replacementCaptureToken))
                continue;
            if (forceMember(member, gate))
                forced++;
        }
        return forced;
    }

    static bool solveRouteConstraints(s32 constraintIndex);
    static bool solveAlternativeSources(
        s32 constraintIndex, const LD::RouteEdge& edge,
        s16 scenario, const u16* sourceTokens,
        s32 sourceCount, s32 sourceIndex);

    static bool chooseGateMembers(
        const DB::CaptureGate& gate, s16 scenario,
        s32 eligibleCount, s32 startIndex, s32 chosenCount,
        s32 requiredCount, s32 constraintIndex,
        const LD::RouteEdge& edge, const u16* sourceTokens,
        s32 sourceCount, s32 sourceIndex) {
        if (chosenCount >= requiredCount)
            return solveAlternativeSources(
                constraintIndex, edge, scenario,
                sourceTokens, sourceCount, sourceIndex + 1);
        const s32 remaining = requiredCount - chosenCount;
        for (s32 i = startIndex;
             i <= eligibleCount - remaining; i++) {
            const u16 member = eligibleMemberAt(
                gate, scenario, i, eligibleCount);
            if (member == 0xFFFFu)
                continue;
            const s32 mark = sChangeCount;
            if (forceMember(member, gate)
                && chooseGateMembers(
                    gate, scenario, eligibleCount,
                    i + 1, chosenCount + 1, requiredCount,
                    constraintIndex, edge, sourceTokens,
                    sourceCount, sourceIndex))
                return true;
            rollbackChanges(mark);
        }
        return false;
    }

    static bool tryGate(
        s32 gateIndex, s32 constraintIndex,
        const LD::RouteEdge& edge, s16 scenario,
        const u16* sourceTokens, s32 sourceCount,
        s32 sourceIndex) {
        if (gateIndex < 0 || gateIndex >= DB::sGateCount)
            return false;
        const DB::CaptureGate& gate = DB::sGates[gateIndex];
        if (gate.memberCount == 0 || !gateDifficultyAllowed(gate))
            return false;
        const s32 witnessMark = sWitnessCount;
        if (sWitnessCount >= (s32)(sizeof(sWitnessLog) / sizeof(sWitnessLog[0])))
            return false;
        sWitnessLog[sWitnessCount++] = {
            sRouteConstraints[constraintIndex].edge, scenario,
            (s16)gateIndex
        };

        bool success = false;
        if (gate.mode == DB::GateMode_All) {
            const s32 mark = sChangeCount;
            success = true;
            for (s32 i = 0; i < gate.memberCount; i++) {
                const u16 member = DB::sGateMembers[gate.memberOffset + i];
                if (!memberActiveInScenario(member, scenario)
                    || !forceMember(member, gate)) {
                    success = false;
                    break;
                }
            }
            if (success)
                success = solveAlternativeSources(
                    constraintIndex, edge, scenario,
                    sourceTokens, sourceCount, sourceIndex + 1);
            if (!success)
                rollbackChanges(mark);
        } else {
            const s32 eligible = eligibleMemberCount(gate, scenario);
            const s32 required = gateRequiredCount(gate);
            if (required > 0 && eligible >= required)
                success = chooseGateMembers(
                    gate, scenario, eligible, 0, 0, required,
                    constraintIndex, edge, sourceTokens,
                    sourceCount, sourceIndex);
        }
        if (!success)
            sWitnessCount = witnessMark;
        return success;
    }

    // Matches-only counterpart to tryGate(): succeeds only if this gate is
    // already satisfied by members explicitly forced earlier (an earlier
    // placement-scenario pass of this same requirement, or an earlier
    // OR-alternative gate sharing a member) -- never forces a fresh member.
    // Used by solveAlternativeSources() to check every OR-alternative for an
    // already-made decision before falling back to a fresh, fairly-rotated
    // choice among them; without this, a scenario-invariant requirement
    // solved once per placement scenario could have each pass pick a
    // different OR-alternative and needlessly consume more of the shared
    // placement pool than the single alternative required.
    static bool tryGateReuse(
        s32 gateIndex, s32 constraintIndex,
        const LD::RouteEdge& edge, s16 scenario,
        const u16* sourceTokens, s32 sourceCount,
        s32 sourceIndex) {
        if (gateIndex < 0 || gateIndex >= DB::sGateCount)
            return false;
        const DB::CaptureGate& gate = DB::sGates[gateIndex];
        if (gate.memberCount == 0 || !gateDifficultyAllowed(gate))
            return false;
        const s32 required = gateRequiredCount(gate);
        if (required <= 0)
            return false;
        const s32 mark = sChangeCount;
        if (forceMatchingMembers(gate, scenario, required) < required) {
            rollbackChanges(mark);
            return false;
        }
        const s32 witnessMark = sWitnessCount;
        if (sWitnessCount >= (s32)(sizeof(sWitnessLog) / sizeof(sWitnessLog[0]))) {
            rollbackChanges(mark);
            return false;
        }
        sWitnessLog[sWitnessCount++] = {
            sRouteConstraints[constraintIndex].edge, scenario,
            (s16)gateIndex
        };
        const bool success = solveAlternativeSources(
            constraintIndex, edge, scenario,
            sourceTokens, sourceCount, sourceIndex + 1);
        if (!success) {
            rollbackChanges(mark);
            sWitnessCount = witnessMark;
        }
        return success;
    }

    static s32 gateAtGroupPosition(
        const DB::GateGroup& group, s32 wanted) {
        s32 seen = 0;
        for (s32 offset = 0; offset < group.gateCount; offset++) {
            const s32 candidateIndex = group.gateOffset + offset;
            if (!gateDifficultyAllowed(DB::sGates[candidateIndex]))
                continue;
            if (seen++ == wanted)
                return candidateIndex;
        }
        return -1;
    }

    // This edge/source-token requirement is solved once per placement
    // scenario; if an earlier scenario pass (or an earlier OR-alternative
    // sharing a member) already forced enough members to satisfy one of
    // these alternative gates, reusing that exact alternative is always
    // solvable. See solveAlternativeSources() for why this isn't always
    // tried first.
    static bool reuseAlternativeSources(
        s32 constraintIndex, const LD::RouteEdge& edge, s16 scenario,
        const u16* sourceTokens, s32 sourceCount, s32 sourceIndex,
        const DB::GateGroup& group, s32 allowedGateCount, s32 rotation) {
        for (s32 i = 0; i < allowedGateCount; i++) {
            const s32 gateIndex = gateAtGroupPosition(
                group, (rotation + i) % allowedGateCount);
            if (gateIndex < 0)
                continue;
            if (tryGateReuse(
                    gateIndex, constraintIndex, edge, scenario,
                    sourceTokens, sourceCount, sourceIndex))
                return true;
        }
        return false;
    }

    // A fair seed-random choice among every OR-alternative (including
    // identity gates) via the same rotation used by the caller.
    static bool freshAlternativeSources(
        s32 constraintIndex, const LD::RouteEdge& edge, s16 scenario,
        const u16* sourceTokens, s32 sourceCount, s32 sourceIndex,
        const DB::GateGroup& group, s32 allowedGateCount, s32 rotation) {
        for (s32 i = 0; i < allowedGateCount; i++) {
            const s32 gateIndex = gateAtGroupPosition(
                group, (rotation + i) % allowedGateCount);
            if (gateIndex < 0)
                continue;
            const s32 mark = sChangeCount;
            const s32 witnessMark = sWitnessCount;
            if (tryGate(
                    gateIndex, constraintIndex, edge, scenario,
                    sourceTokens, sourceCount, sourceIndex))
                return true;
            rollbackChanges(mark);
            sWitnessCount = witnessMark;
        }
        return false;
    }

    static bool solveAlternativeSources(
        s32 constraintIndex, const LD::RouteEdge& edge,
        s16 scenario, const u16* sourceTokens,
        s32 sourceCount, s32 sourceIndex) {
        if (sourceIndex >= sourceCount)
            return solveRouteConstraints(constraintIndex + 1);
        const s32 tokenIndex = sourceTokens[sourceIndex];
        const char* sourceToken = LD::sTokens[tokenIndex].key;
        const s32 groupIndex = findGateGroup(
            edge.edgeId, sourceToken);
        if (groupIndex < 0)
            return false;
        const DB::GateGroup& group = DB::sGateGroups[groupIndex];
        s32 allowedGateCount = 0;
        for (s32 offset = 0; offset < group.gateCount; offset++)
            if (gateDifficultyAllowed(
                    DB::sGates[group.gateOffset + offset]))
                allowedGateCount++;
        if (allowedGateCount <= 0)
            return false;
        const s32 rotation = (s32)(mixHash(
                                       swapBase(sLastSeed, sLastAttempt)
                                       ^ fnv1a(edge.edgeId)
                                       ^ fnv1a(sourceToken)
                                       ^ ((u32)scenario * kTokenMix))
            % (u32)allowedGateCount);

        // Always-reuse collapses many distinct requirements onto whatever
        // was decided first, so half the time (seed-random) we deliberately
        // try a fresh independent choice before falling back to reuse,
        // instead of the other way around. Whichever runs second is a pure
        // fallback -- if the first pass can't find a valid choice, the
        // second still gets a full attempt, so this reordering can never
        // turn a solvable seed unsolvable.
        const bool skipReuse = mixHash(
                                   swapBase(sLastSeed, sLastAttempt)
                                   ^ fnv1a(edge.edgeId)
                                   ^ fnv1a(sourceToken)
                                   ^ ((u32)scenario * kTokenMix)
                                   ^ kSkipSalt)
                % 100u
            < 50u;

        if (skipReuse) {
            if (freshAlternativeSources(
                    constraintIndex, edge, scenario, sourceTokens,
                    sourceCount, sourceIndex, group, allowedGateCount,
                    rotation))
                return true;
            return reuseAlternativeSources(
                constraintIndex, edge, scenario, sourceTokens,
                sourceCount, sourceIndex, group, allowedGateCount,
                rotation);
        }
        if (reuseAlternativeSources(
                constraintIndex, edge, scenario, sourceTokens,
                sourceCount, sourceIndex, group, allowedGateCount,
                rotation))
            return true;
        return freshAlternativeSources(
            constraintIndex, edge, scenario, sourceTokens,
            sourceCount, sourceIndex, group, allowedGateCount,
            rotation);
    }

    // True when every token in this alternative (not just the ones needing
    // a gate) is a capture -- e.g. "Frog" alone, not "Frog & WallJump".
    static bool isCaptureOnlyOption(const LD::ReqAlternative& alternative) {
        if (alternative.tokenCount <= 0)
            return false;
        for (s32 i = 0; i < alternative.tokenCount; i++) {
            const s32 token = LD::sReqTokens[alternative.tokenOffset + i];
            if (token < 0 || token >= LD::sTokenCount
                || LD::sTokens[token].type != LD::TokenType_Capture)
                return false;
        }
        return true;
    }

    // Builds and tries the sourceTokens list for one requirement
    // alternative. Rolls back its own changes/witnesses on failure.
    static bool solveRequirementOption(
        s32 constraintIndex, const LD::RouteEdge& edge, s16 scenario,
        const LD::ReqAlternative& alternative) {
        u16 sourceTokens[kMaxCaptureScratch] = {};
        s32 sourceCount = 0;
        for (s32 i = 0; i < alternative.tokenCount; i++) {
            const s32 token = LD::sReqTokens[alternative.tokenOffset + i];
            if (!routeTokenNeedsGate(edge, token))
                continue;
            if (sourceCount >= kMaxCaptureScratch)
                return false;
            sourceTokens[sourceCount++] = (u16)token;
        }
        for (s32 i = 1; i < sourceCount; i++) {
            const u16 value = sourceTokens[i];
            s32 insert = i;
            while (insert > 0
                && std::strcmp(
                       LD::sTokens[value].key,
                       LD::sTokens[sourceTokens[insert - 1]].key)
                    < 0) {
                sourceTokens[insert] = sourceTokens[insert - 1];
                insert--;
            }
            sourceTokens[insert] = value;
        }
        const s32 mark = sChangeCount;
        const s32 witnessMark = sWitnessCount;
        if (solveAlternativeSources(
                constraintIndex, edge, scenario,
                sourceTokens, sourceCount, 0))
            return true;
        rollbackChanges(mark);
        sWitnessCount = witnessMark;
        return false;
    }

    static bool solveRouteConstraints(s32 constraintIndex) {
        if (constraintIndex >= sRouteConstraintCount)
            return true;
        const RouteConstraintInstance& constraint = sRouteConstraints[constraintIndex];
        const LD::RouteEdge& edge = LD::sRouteEdges[constraint.edge];
        if (edge.requirement < 0
            || edge.requirement >= LD::sRequirementCount)
            return solveRouteConstraints(constraintIndex + 1);
        const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
        if (requirement.mode != LD::RequirementMode_Custom
            || requirement.altCount == 0)
            return solveRouteConstraints(constraintIndex + 1);

        // A capture-only alternative (nothing but capture token(s)) existed
        // as a solution before randomization. At least one such alternative
        // must always stay satisfiable, independent of its own tagged
        // difficulty or the selected logic difficulty -- Enemy/Capture
        // Chaos must never take away a capture-only solution that existed
        // in the original logic. This is a hard requirement: if a
        // capture-only alternative exists but none can be witnessed, the
        // constraint fails rather than falling back to an ability route.
        u16 captureOnlyOptions[kMaxOptionScratch] = {};
        s32 captureOnlyCount = 0;
        for (s32 option = 0;
             option < requirement.altCount
             && captureOnlyCount < kMaxOptionScratch;
             option++) {
            if (!routeOptionScenarioAllowed(
                    edge, option, constraint.scenario))
                continue;
            const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
            if (isCaptureOnlyOption(alternative))
                captureOnlyOptions[captureOnlyCount++] = (u16)option;
        }
        if (captureOnlyCount > 0) {
            const s32 rotation = (s32)(mixHash(
                                           swapBase(sLastSeed, sLastAttempt)
                                           ^ fnv1a(edge.edgeId)
                                           ^ fnv1a("capture_only")
                                           ^ ((u32)constraint.scenario * kTokenMix))
                % (u32)captureOnlyCount);
            for (s32 offset = 0; offset < captureOnlyCount; offset++) {
                const s32 option = captureOnlyOptions[(rotation + offset) % captureOnlyCount];
                const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
                if (solveRequirementOption(
                        constraintIndex, edge, constraint.scenario,
                        alternative))
                    return true;
            }
            return false;
        }

        u16 eligibleOptions[kMaxOptionScratch] = {};
        s32 optionCount = 0;
        for (s32 option = 0;
             option < requirement.altCount
             && optionCount < kMaxOptionScratch;
             option++) {
            if (routeOptionScenarioAllowed(
                    edge, option, constraint.scenario)
                && routeOptionDifficultyAllowed(edge, option))
                eligibleOptions[optionCount++] = (u16)option;
        }
        if (optionCount <= 0)
            return solveRouteConstraints(constraintIndex + 1);
        const s32 rotation = (s32)(mixHash(
                                       swapBase(sLastSeed, sLastAttempt)
                                       ^ fnv1a(edge.edgeId)
                                       ^ ((u32)constraint.scenario * kTokenMix))
            % (u32)optionCount);
        for (s32 offset = 0; offset < optionCount; offset++) {
            const s32 option = eligibleOptions[(rotation + offset) % optionCount];
            const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
            if (solveRequirementOption(
                    constraintIndex, edge, constraint.scenario,
                    alternative))
                return true;
        }
        return false;
    }

    static s16 pickFromPool(
        const DB::Placement& placement, const u16* pool, s32 count) {
        if (!pool || count <= 0)
            return -1;
        const u32 pick = mixHash(
            swapBase(sLastSeed, sLastAttempt)
            ^ fnv1a(placement.stage)
            ^ (fnv1a(placement.objId) * kTokenMix)
            ^ kAnySalt);
        return (s16)pool[pick % (u32)count];
    }

    static bool withinPropSizeFactor(u32 left, u32 right) {
        return left > 0 && right > 0
            && (u64)left <= (u64)right * 2
            && (u64)right <= (u64)left * 2;
    }

    // Prop replacements inherit the source placement transform, so comparing
    // the actors' unscaled dimensions is sufficient: the same placement scale
    // multiplies both sides. Unknown-size sources deliberately get no prop
    // candidate, while combat candidates remain available only in the
    // explicitly wider Props + Combat mode.
    static bool propSpatialCompatible(s16 sourceIndex, u16 targetIndex) {
        if (sourceIndex < 0 || sourceIndex >= DB::sEntityCount
            || targetIndex >= DB::sEntityCount)
            return false;
        if (sourceIndex == (s16)targetIndex)
            return true;
        const DB::Entity& source = DB::sEntities[sourceIndex];
        const DB::Entity& target = DB::sEntities[targetIndex];
        if (!hasCategory(source, "prop") || !hasCategory(target, "prop"))
            return true;
        return source.propFootprint > 0 && source.propHeight > 0
            && target.propFootprint > 0 && target.propHeight > 0
            && source.propSolid == target.propSolid
            && withinPropSizeFactor(
                source.propFootprint, target.propFootprint)
            && withinPropSizeFactor(source.propHeight, target.propHeight);
    }

    // Filters explicitly excluded capture tokens and, when a stage already
    // has a Chaos-assigned Bowser, Koopa itself out of the pool before
    // picking. This keeps MATCH_AT_MOST caps and Bowser's per-stage cap from
    // being violated by the unconstrained random-fill pass. With neither
    // exclusion this reduces to pickFromPool() exactly (same hash formula,
    // same domain), so unaffected placements are unchanged.
    // True when placing this entity here would exceed an audited restriction.
    // Counts the live resolved assignment, discounting this placement's own
    // current entity because the candidate is about to take its slot.
    // Mirrors spawn_restricted() in logic/enemy_swap.py.
    static bool spawnRestricted(
        s32 placementIndex, const DB::Placement& placement, s16 entityIndex,
        u8 sourceMask = DB::SourceMask_Both) {
        if (DB::sEnemySpawnRestrictionCount <= 0 || entityIndex < 0)
            return false;
        for (s32 i = 0; i < DB::sEnemySpawnRestrictionCount; i++) {
            const DB::EnemySpawnRestriction& rule = DB::sEnemySpawnRestrictions[i];
            if (rule.entity != (u16)entityIndex
                || !(rule.sourceMask & sourceMask)
                || !restrictionAppliesTo(rule, placement))
                continue;
            // The override discounts this placement's own entity: the
            // candidate is about to take its slot.
            if (countResolvedInScenario(rule, placementIndex, -1)
                >= rule.maxInstances)
                return true;
        }
        return false;
    }

    static s16 pickFromPoolExcluding(
        const DB::Placement& placement, const u16* pool, s32 count,
        const char* const* excludedTokens, s32 excludedCount,
        bool excludeBowser = false, bool excludeNpc = false,
        s32 placementIndex = -1) {
        const bool applyRestrictions = placementIndex >= 0 && DB::sEnemySpawnRestrictionCount > 0;
        const bool applyPropSpatial = placement.entity >= 0 && placement.entity < DB::sEntityCount
            && hasCategory(DB::sEntities[placement.entity], "prop");
        if (excludedCount <= 0 && !excludeBowser && !excludeNpc
            && !applyRestrictions && !applyPropSpatial)
            return pickFromPool(placement, pool, count);
        if (!pool || count <= 0)
            return -1;
        u16 filtered[DB::sEntityCount > 0 ? DB::sEntityCount : 1];
        s32 filteredCount = 0;
        for (s32 i = 0; i < count && filteredCount < DB::sEntityCount; i++) {
            const u16 entityIndex = pool[i];
            if (entityIndex >= DB::sEntityCount)
                continue;
            if (excludeNpc && strEq(DB::sEntities[entityIndex].category, "npc"))
                continue;
            if (excludeBowser && isBowserEntity((s16)entityIndex))
                continue;
            if (applyPropSpatial
                && hasCategory(DB::sEntities[entityIndex], "prop")
                && !propSpatialCompatible(
                    placement.entity, entityIndex))
                continue;
            if (applyRestrictions && spawnRestricted(placementIndex, placement, (s16)entityIndex))
                continue;
            const char* token = DB::sEntities[entityIndex].captureToken;
            bool excluded = false;
            for (s32 e = 0; e < excludedCount; e++)
                if (strEq(token, excludedTokens[e])) {
                    excluded = true;
                    break;
                }
            if (!excluded)
                filtered[filteredCount++] = entityIndex;
        }
        return pickFromPool(placement, filtered, filteredCount);
    }

    // Targets a source of this category may become, per its swap mode.
    // Mirrors mode_domain() in logic/enemy_swap.py -- the two must agree or a
    // seed generated on the host resolves differently on console.
    static s32 buildModeDomain(
        const DB::Entity& entity, u16* out, s32 capacity) {
        const u16* own = nullptr;
        s32 ownCount = 0;
        Randomizer::ChaosSwapMode mode = Randomizer::ChaosSwapMode::Off;
        if (hasCategory(entity, "enemy2d")) {
            // No mode: 2D is same-category or nothing. Returning the pool or
            // an empty list directly keeps that fact in one place instead of
            // encoding it as "a mode that happens never to widen".
            if (!Randomizer::isTwoDSwapRandoEnabled())
                return 0;
            s32 count = 0;
            for (s32 i = 0; i < DB::sTwoDPoolCount && count < capacity; i++)
                out[count++] = DB::sTwoDPool[i];
            return count;
        }
        if (hasCategory(entity, "npc")) {
            own = DB::sNpcPool;
            ownCount = DB::sNpcPoolCount;
            mode = Randomizer::getNpcSwapMode();
        } else if (hasCategory(entity, "prop")) {
            own = DB::sPropPool;
            ownCount = DB::sPropPoolCount;
            mode = Randomizer::getPropSwapMode();
        } else {
            // Combat sources draw from the combat pool alone. That pool is
            // built from capture/enemy/generator entities only, so it cannot
            // hand a combat placement an NPC or a prop.
            s32 count = 0;
            for (s32 i = 0; i < DB::sAnyPoolCount && count < capacity; i++)
                out[count++] = DB::sAnyPool[i];
            return count;
        }
        if (mode == Randomizer::ChaosSwapMode::Off)
            return 0;
        s32 count = 0;
        for (s32 i = 0; i < ownCount && count < capacity; i++)
            out[count++] = own[i];
        if (mode == Randomizer::ChaosSwapMode::WithCombat
            || mode == Randomizer::ChaosSwapMode::WithDeletion)
            for (s32 i = 0; i < DB::sAnyPoolCount && count < capacity; i++)
                out[count++] = DB::sAnyPool[i];
        return count;
    }

    static s16 pickRandomReplacement(
        const DB::Placement& placement,
        const char* const* excludedTokens = nullptr,
        s32 excludedCount = 0, s32 placementIndex = -1,
        bool excludeBowser = false) {
        const DB::SwapRule* rule = placementRule(placement);
        if (!rule)
            return -1;
        if (rule->poolCount > 0)
            return pickFromPoolExcluding(
                placement, DB::sRulePool + rule->poolOffset,
                rule->poolCount, excludedTokens, excludedCount,
                excludeBowser, true, placementIndex);
        if (placement.entity < 0 || placement.entity >= DB::sEntityCount)
            return -1;
        u16 domain[DB::sEntityCount > 0 ? DB::sEntityCount : 1];
        const s32 domainCount = buildModeDomain(
            DB::sEntities[placement.entity], domain,
            static_cast<s32>(sizeof(domain) / sizeof(domain[0])));
        // excludeNpc stays false here: the mode already decided whether this
        // source's own category is in the domain, and for an NPC source under
        // the NPC-only mode the NPC entries are the entire domain.
        return pickFromPoolExcluding(
            placement, domain, domainCount,
            excludedTokens, excludedCount, excludeBowser, false,
            placementIndex);
    }

    static bool gateMatches(
        const DB::CaptureGate& gate, s16 scenario,
        bool includeSessionOverrides = true) {
        if (gate.memberCount == 0 || !gateDifficultyAllowed(gate))
            return false;
        s32 active = 0;
        s32 matching = 0;
        const s32 scenarioIndex = scenario > 0 ? scenario - 1 : scenario;
        for (s32 i = 0; i < gate.memberCount; i++) {
            const u16 placementIndex = DB::sGateMembers[gate.memberOffset + i];
            if (placementIndex >= DB::sPlacementCount)
                return false;
            const DB::Placement& placement = DB::sPlacements[placementIndex];
            if (!placementCanServeCaptureGate(placement))
                return false;
            const bool isActive = scenarioIndex < 0 || scenarioIndex >= 32
                || (placement.scenarioMask & (1u << scenarioIndex)) != 0;
            if (!isActive) {
                if (gate.mode == DB::GateMode_All)
                    return false;
                continue;
            }
            active++;
            const s16 overrideEntity = includeSessionOverrides
                ? EnemySwapDebug::getOverrideEntity(placementIndex)
                : -1;
            const s16 entity = overrideEntity >= 0
                ? overrideEntity
                : sResolvedEntity[placementIndex];
            if (entity >= 0 && entity < DB::sEntityCount
                && strEq(DB::sEntities[entity].captureToken,
                    gate.replacementCaptureToken))
                matching++;
        }
        if (gate.mode == DB::GateMode_All)
            return active == gate.memberCount && matching == active;
        if (gate.mode == DB::GateMode_Any)
            return matching >= 1;
        if (gate.mode == DB::GateMode_AtMost)
            return matching >= 1 && matching <= gate.minimumCount;
        return matching >= gate.minimumCount;
    }

    static bool edgeHasCaptureConstraint(
        const LD::RouteEdge& edge) {
        if (edge.requirement < 0
            || edge.requirement >= LD::sRequirementCount)
            return false;
        const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
        if (requirement.mode != LD::RequirementMode_Custom)
            return false;
        for (s32 option = 0; option < requirement.altCount; option++) {
            const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
            for (s32 i = 0; i < alternative.tokenCount; i++)
                if (routeTokenNeedsGate(
                        edge,
                        LD::sReqTokens[alternative.tokenOffset + i]))
                    return true;
        }
        return false;
    }

    static bool constraintOptionAllowed(
        const LD::RouteEdge& edge, s32 option, s16 scenario) {
        if (!routeOptionScenarioAllowed(edge, option, scenario))
            return false;
        if (routeOptionDifficultyAllowed(edge, option))
            return true;
        // A capture-only alternative stays eligible for a
        // RouteConstraintInstance even above the selected logic difficulty,
        // matching solveRouteConstraints()'s capture-only-is-mandatory rule
        // -- otherwise a capture-only alternative tagged above casual could
        // be filtered out here before that rule ever runs.
        if (edge.requirement < 0
            || edge.requirement >= LD::sRequirementCount)
            return false;
        const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
        if (option < 0 || option >= requirement.altCount)
            return false;
        return isCaptureOnlyOption(
            LD::sReqAlternatives[requirement.altOffset + option]);
    }

    static void buildRouteConstraintList() {
        sRouteConstraintCount = 0;
        sWitnessCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++)
            sMemberFrequency[i] = 0;

        for (s32 edgeIndex = 0;
             edgeIndex < LD::sRouteEdgeCount; edgeIndex++) {
            const LD::RouteEdge& edge = LD::sRouteEdges[edgeIndex];
            if (!edgeHasCaptureConstraint(edge)
                || edge.requirement >= LD::sRequirementCount)
                continue;
            const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
            for (s16 scenario = 1;
                 scenario <= kMaxPlacementScenario; scenario++) {
                bool applicable = false;
                for (s32 option = 0;
                     option < requirement.altCount; option++)
                    if (constraintOptionAllowed(
                            edge, option, scenario)) {
                        applicable = true;
                        break;
                    }
                if (!applicable)
                    continue;
                if (sRouteConstraintCount >= (s32)(sizeof(sRouteConstraints)
                        / sizeof(sRouteConstraints[0])))
                    return;
                sRouteConstraints[sRouteConstraintCount++] = {
                    (u16)edgeIndex, scenario, 0, 0
                };
            }
        }

        // Count cross-constraint membership so the most entangled physical
        // routes win ties after the fewest-candidate ordering.
        for (s32 constraintIndex = 0;
             constraintIndex < sRouteConstraintCount;
             constraintIndex++) {
            const LD::RouteEdge& edge = LD::sRouteEdges[sRouteConstraints[constraintIndex].edge];
            const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
            for (s32 option = 0;
                 option < requirement.altCount; option++) {
                const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
                for (s32 i = 0; i < alternative.tokenCount; i++) {
                    const s32 token = LD::sReqTokens[alternative.tokenOffset + i];
                    if (!routeTokenNeedsGate(edge, token))
                        continue;
                    const s32 groupIndex = findGateGroup(
                        edge.edgeId, LD::sTokens[token].key);
                    if (groupIndex < 0)
                        continue;
                    const DB::GateGroup& group = DB::sGateGroups[groupIndex];
                    for (s32 gateOffset = 0;
                         gateOffset < group.gateCount; gateOffset++) {
                        const DB::CaptureGate& gate = DB::sGates[group.gateOffset + gateOffset];
                        if (!gateDifficultyAllowed(gate))
                            continue;
                        for (s32 memberOffset = 0;
                             memberOffset < gate.memberCount;
                             memberOffset++) {
                            const u16 member = DB::sGateMembers[gate.memberOffset + memberOffset];
                            if (member < DB::sPlacementCount
                                && sMemberFrequency[member] < 0xFFFFu)
                                sMemberFrequency[member]++;
                        }
                    }
                }
            }
        }

        for (s32 constraintIndex = 0;
             constraintIndex < sRouteConstraintCount;
             constraintIndex++) {
            RouteConstraintInstance& constraint = sRouteConstraints[constraintIndex];
            const LD::RouteEdge& edge = LD::sRouteEdges[constraint.edge];
            const LD::Requirement& requirement = LD::sRequirements[edge.requirement];
            u32 candidates = 0;
            u32 overlap = 0;
            for (s32 option = 0;
                 option < requirement.altCount; option++) {
                if (!constraintOptionAllowed(
                        edge, option, constraint.scenario))
                    continue;
                const LD::ReqAlternative& alternative = LD::sReqAlternatives[requirement.altOffset + option];
                u32 optionCandidates = 1;
                for (s32 i = 0; i < alternative.tokenCount; i++) {
                    const s32 token = LD::sReqTokens[alternative.tokenOffset + i];
                    if (!routeTokenNeedsGate(edge, token))
                        continue;
                    const s32 groupIndex = findGateGroup(
                        edge.edgeId, LD::sTokens[token].key);
                    s32 gateCount = 0;
                    if (groupIndex >= 0) {
                        const DB::GateGroup& group = DB::sGateGroups[groupIndex];
                        for (s32 gateOffset = 0;
                             gateOffset < group.gateCount; gateOffset++)
                            if (gateDifficultyAllowed(DB::sGates[group.gateOffset + gateOffset]))
                                gateCount++;
                    }
                    optionCandidates *= gateCount > 0
                        ? (u32)gateCount
                        : 1u;
                    if (optionCandidates > 0xFFFFu)
                        optionCandidates = 0xFFFFu;
                    if (groupIndex < 0)
                        continue;
                    const DB::GateGroup& group = DB::sGateGroups[groupIndex];
                    for (s32 gateOffset = 0;
                         gateOffset < group.gateCount; gateOffset++) {
                        const DB::CaptureGate& gate = DB::sGates[group.gateOffset + gateOffset];
                        if (!gateDifficultyAllowed(gate))
                            continue;
                        for (s32 memberOffset = 0;
                             memberOffset < gate.memberCount;
                             memberOffset++) {
                            const u16 member = DB::sGateMembers[gate.memberOffset + memberOffset];
                            if (member < DB::sPlacementCount)
                                overlap += sMemberFrequency[member];
                        }
                    }
                }
                candidates += optionCandidates;
                if (candidates > 0xFFFFu)
                    candidates = 0xFFFFu;
            }
            constraint.candidateCount = (u16)candidates;
            constraint.overlapScore = overlap;
        }

        for (s32 i = 1; i < sRouteConstraintCount; i++) {
            const RouteConstraintInstance value = sRouteConstraints[i];
            s32 insert = i;
            while (insert > 0) {
                const RouteConstraintInstance& previous = sRouteConstraints[insert - 1];
                const LD::RouteEdge& valueEdge = LD::sRouteEdges[value.edge];
                const LD::RouteEdge& previousEdge = LD::sRouteEdges[previous.edge];
                const bool valueFirst = value.candidateCount < previous.candidateCount
                    || (value.candidateCount
                            == previous.candidateCount
                        && value.overlapScore
                            > previous.overlapScore)
                    || (value.candidateCount
                            == previous.candidateCount
                        && value.overlapScore
                            == previous.overlapScore
                        && (std::strcmp(
                                valueEdge.edgeId,
                                previousEdge.edgeId)
                                < 0
                            || (strEq(
                                    valueEdge.edgeId,
                                    previousEdge.edgeId)
                                && value.scenario
                                    < previous.scenario)));
                if (!valueFirst)
                    break;
                sRouteConstraints[insert] = previous;
                insert--;
            }
            sRouteConstraints[insert] = value;
        }
    }

    bool buildRemapTable(u32 seed, u32 attempt) {
        const bool enemyEnabled = Randomizer::isEnemySwapRandoEnabled();
        const Randomizer::ChaosSwapMode npcMode = Randomizer::getNpcSwapMode();
        const Randomizer::ChaosSwapMode propMode = Randomizer::getPropSwapMode();
        const bool twoDEnabled = Randomizer::isTwoDSwapRandoEnabled();
        const u8 propChance = Randomizer::getPropSwapChancePercent();
        const u8 logicDifficulty = (u8)Randomizer::getLogicDifficulty();
        if (sInitialized && sLastSeed == seed && sLastAttempt == attempt
            && sLastEnemyEnabled == enemyEnabled
            && sLastNpcMode == npcMode
            && sLastPropMode == propMode
            && sLastTwoDEnabled == twoDEnabled
            && sLastPropChance == propChance
            && sLastLogicDifficulty == logicDifficulty)
            return sAssignmentValid;

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastEnemyEnabled = enemyEnabled;
        sLastNpcMode = npcMode;
        sLastPropMode = propMode;
        sLastTwoDEnabled = twoDEnabled;
        sLastPropChance = propChance;
        sLastLogicDifficulty = logicDifficulty;
        sFailureCode = FailureCode::None;
        sMemoryFailureStage = nullptr;
        sMemoryFailureScenario = -1;
        sMemoryFailureExtraResource = 0;
        sMemoryFailureMaxResource = 0;
        sMemoryFailureExtraScene = 0;
        sMemoryFailureMaxScene = 0;
        sChangeCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            sResolvedEntity[i] = DB::sPlacements[i].entity;
            sForcedCapture[i] = nullptr;
        }
        buildRouteConstraintList();
        sAssignmentValid = !enemyEnabled
            || solveRouteConstraints(0);
        if (!sAssignmentValid) {
            sFailureCode = FailureCode::UnsatisfiedConstraints;
            Randomizer::logPush(
                "enemy swap constraints unsatisfied: seed=%u attempt=%u",
                seed, attempt);
            sInitialized = true;
            return false;
        }

        // MATCH_AT_MOST gates cap how many of their own members may resolve
        // to the replacement token. Precompute which such gates already met
        // their cap via forced (witness) members, so the random fill below
        // can exclude their replacement token for the gate's remaining,
        // still-unforced members -- pure chance can't push the count past
        // the declared cap.
        bool cappedGate[DB::sGateCount > 0 ? DB::sGateCount : 1] = {};
        for (s32 g = 0; g < DB::sGateCount; g++) {
            const DB::CaptureGate& gate = DB::sGates[g];
            if (gate.mode != DB::GateMode_AtMost)
                continue;
            s32 forcedCount = 0;
            for (s32 m = 0; m < gate.memberCount; m++) {
                const u16 member = DB::sGateMembers[gate.memberOffset + m];
                if (sForcedCapture[member]
                    && strEq(sForcedCapture[member],
                        gate.replacementCaptureToken))
                    forcedCount++;
            }
            if (forcedCount >= gate.minimumCount)
                cappedGate[g] = true;
        }

        static constexpr s32 kMaxCappedExclusions = 8;
        sBowserResolvedStageCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++)
            if (isBowserReplacement(i, sResolvedEntity[i]))
                noteFinalFillBowserStage(DB::sPlacements[i]);
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            const DB::Placement& placement = DB::sPlacements[i];
            if (placement.entity < 0 || sForcedCapture[i]
                || !placementCanRandomize(placement))
                continue;
            if (hasCategory(DB::sEntities[placement.entity], "prop")
                && shouldDeleteProp(placement)) {
                sResolvedEntity[i] = kDeletedPlacementEntity;
                continue;
            }
            const char* excludedTokens[kMaxCappedExclusions] = {};
            s32 excludedCount = 0;
            for (s32 g = 0;
                 g < DB::sGateCount && excludedCount < kMaxCappedExclusions;
                 g++) {
                if (!cappedGate[g])
                    continue;
                const DB::CaptureGate& gate = DB::sGates[g];
                bool isMember = false;
                for (s32 m = 0; m < gate.memberCount; m++)
                    if (DB::sGateMembers[gate.memberOffset + m] == i) {
                        isMember = true;
                        break;
                    }
                if (!isMember)
                    continue;
                bool alreadyListed = false;
                for (s32 e = 0; e < excludedCount; e++)
                    if (strEq(excludedTokens[e],
                            gate.replacementCaptureToken)) {
                        alreadyListed = true;
                        break;
                    }
                if (!alreadyListed)
                    excludedTokens[excludedCount++] = gate.replacementCaptureToken;
            }
            const s16 replacement = pickRandomReplacement(
                placement, excludedTokens, excludedCount, i,
                finalFillStageHasBowser(placement));
            if (replacement >= 0) {
                sResolvedEntity[i] = replacement;
                if (isBowserReplacement(i, replacement))
                    noteFinalFillBowserStage(placement);
            }
        }

        for (s32 i = 0; i < sWitnessCount; i++) {
            const s16 gate = sWitnessLog[i].gate;
            if (gate >= 0 && gate < DB::sGateCount
                && !gateMatches(
                    DB::sGates[gate],
                    sWitnessLog[i].scenario,
                    false)) {
                Randomizer::logPush(
                    "enemy swap witness verification failed: %s",
                    DB::sGates[gate].gateId);
                sFailureCode = FailureCode::WitnessVerification;
                sAssignmentValid = false;
                break;
            }
        }

        if (sAssignmentValid && !checkSpawnRestrictions()) {
            Randomizer::logPush(
                "enemy swap spawn restriction violated: stage=%s scenario=%d "
                "entity=%s count=%d max=%d",
                sRestrictionFailureStage ? sRestrictionFailureStage : "?",
                sRestrictionFailureScenario,
                sRestrictionFailureEntity >= 0
                        && sRestrictionFailureEntity < DB::sEntityCount
                    ? DB::sEntities[sRestrictionFailureEntity].name
                    : "?",
                sRestrictionFailureCount, sRestrictionFailureMax);
            sFailureCode = FailureCode::SpawnRestricted;
            sAssignmentValid = false;
        }

        if (sAssignmentValid && !checkMemoryBudgets()) {
            Randomizer::logPush(
                "enemy swap memory budget exceeded: stage=%s scenario=%d "
                "resource=%u/%u scene=%u/%u",
                sMemoryFailureStage ? sMemoryFailureStage : "?",
                sMemoryFailureScenario,
                sMemoryFailureExtraResource, sMemoryFailureMaxResource,
                sMemoryFailureExtraScene, sMemoryFailureMaxScene);
            sFailureCode = FailureCode::MemoryBudgetExceeded;
            sAssignmentValid = false;
        }

        Randomizer::logPush(
            "enemy swap assignment v%u: seed=%u attempt=%u placements=%d gates=%d witnesses=%d valid=%s",
            DB::kAssignmentDataVersion, seed, attempt,
            DB::sPlacementCount, DB::sGateCount, sWitnessCount,
            sAssignmentValid ? "yes" : "no");
        for (s32 i = 0; i < sWitnessCount; i++) {
            const RouteWitness& witness = sWitnessLog[i];
            Randomizer::logPush(
                "enemy swap witness: %s scenario=%d gate=%s",
                LD::sRouteEdges[witness.edge].edgeId,
                witness.scenario,
                DB::sGates[witness.gate].gateId);
        }
        sInitialized = true;
        return sAssignmentValid;
    }

    void clear() {
        sInitialized = false;
        sAssignmentValid = false;
        sFailureCode = FailureCode::None;
        sMemoryFailureStage = nullptr;
        sMemoryFailureScenario = -1;
        sLastSeed = 0xFFFFFFFFu;
        sLastAttempt = 0xFFFFFFFFu;
        // Reloading a file drops the rolls with it, so the seed's own
        // assignment is what comes back rather than roll N of the last one.
        sRerollBaseValid = false;
        sRerollCount = 0;
    }

    u32 getRerollCount() {
        return sRerollCount;
    }

    bool rerollAssignment() {
        if (!sInitialized)
            return false;
        if (!sRerollBaseValid) {
            sRerollBaseSeed = sLastSeed;
            sRerollBaseAttempt = sLastAttempt;
            sRerollBaseValid = true;
        }
        // Attempt is the reroll axis: the seed is untouched, so the file is
        // still recognisably itself, but every placement re-resolves.
        //
        // An attempt can legitimately fail to satisfy the capture gates, which
        // is what the seed generator's own retry loop exists for. Walk forward
        // rather than giving up, and restore the last good roll if the whole
        // window fails, so a failed reroll leaves the game playable instead of
        // dropping every swap.
        constexpr u32 kMaxRerollAttempts = 16;
        const u32 startCount = sRerollCount;
        for (u32 i = 0; i < kMaxRerollAttempts; i++) {
            sRerollCount++;
            if (buildRemapTable(
                    sRerollBaseSeed, sRerollBaseAttempt + sRerollCount))
                return true;
        }
        sRerollCount = startCount;
        buildRemapTable(sRerollBaseSeed, sRerollBaseAttempt + sRerollCount);
        return false;
    }

    bool isInitialized() {
        return sInitialized;
    }

    FailureCode getFailureCode() {
        return sFailureCode;
    }

    bool isMemoryDataAvailable() {
        return DB::sMemoryDataAvailable;
    }

    const DB::EntityMemoryCost* getMemoryCost(s16 entityIndex) {
        return memoryCost(entityIndex);
    }

    bool checkMemoryBudgetForReplacement(
        s32 placementIndex, s16 entityIndex) {
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount
            || entityIndex < 0 || entityIndex >= DB::sEntityCount)
            return false;
        return checkMemoryBudgets(placementIndex, entityIndex);
    }

    bool isSpawnRestrictionDataAvailable() {
        return DB::sEnemySpawnRestrictionCount > 0;
    }

    bool checkSpawnRestrictionForReplacement(
        s32 placementIndex, s16 entityIndex) {
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount
            || entityIndex < 0 || entityIndex >= DB::sEntityCount)
            return false;
        return !spawnRestricted(
            placementIndex, DB::sPlacements[placementIndex], entityIndex);
    }

    void getSpawnRestrictionFailure(
        const char** stage, s16* scenario, s16* entity,
        s32* count, s32* maxInstances) {
        if (stage)
            *stage = sRestrictionFailureStage;
        if (scenario)
            *scenario = sRestrictionFailureScenario;
        if (entity)
            *entity = sRestrictionFailureEntity;
        if (count)
            *count = sRestrictionFailureCount;
        if (maxInstances)
            *maxInstances = sRestrictionFailureMax;
    }

    void getMemoryFailure(
        const char** stage, s16* scenario,
        u32* extraResourceBytes, u32* maxExtraResourceBytes,
        u32* extraSceneBytes, u32* maxExtraSceneBytes) {
        if (stage)
            *stage = sMemoryFailureStage;
        if (scenario)
            *scenario = sMemoryFailureScenario;
        if (extraResourceBytes)
            *extraResourceBytes = sMemoryFailureExtraResource;
        if (maxExtraResourceBytes)
            *maxExtraResourceBytes = sMemoryFailureMaxResource;
        if (extraSceneBytes)
            *extraSceneBytes = sMemoryFailureExtraScene;
        if (maxExtraSceneBytes)
            *maxExtraSceneBytes = sMemoryFailureMaxScene;
    }

    s32 getResolvedPlacementCount() {
        return sInitialized && sAssignmentValid ? DB::sPlacementCount : 0;
    }

    s16 getResolvedEntityIndex(s32 placementIndex) {
        if (!sInitialized || !sAssignmentValid
            || placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return -1;
        return sResolvedEntity[placementIndex];
    }

    bool isCaptureGateWitnessPlacement(s32 placementIndex) {
        return sInitialized && sAssignmentValid
            && placementIndex >= 0 && placementIndex < DB::sPlacementCount
            && sForcedCapture[placementIndex] != nullptr;
    }

    s32 getRouteWitnessCount() {
        return sInitialized && sAssignmentValid ? sWitnessCount : 0;
    }

    bool getRouteWitness(
        s32 witnessIndex, const char** edgeId, s16* scenario,
        const char** gateId) {
        if (!sInitialized || !sAssignmentValid
            || witnessIndex < 0 || witnessIndex >= sWitnessCount)
            return false;
        const RouteWitness& witness = sWitnessLog[witnessIndex];
        if (witness.edge >= LD::sRouteEdgeCount
            || witness.gate < 0 || witness.gate >= DB::sGateCount)
            return false;
        if (edgeId)
            *edgeId = LD::sRouteEdges[witness.edge].edgeId;
        if (scenario)
            *scenario = witness.scenario;
        if (gateId)
            *gateId = DB::sGates[witness.gate].gateId;
        return true;
    }

    static bool isForbiddenReplacement(const DB::Entity& entity) {
        return strEq(entity.name, "BreedaWanwan");
    }

    const DB::Entity* findReplacement(
        const char* stage, const char* objId, const char* unitConfigName,
        SwapMode* mode, s32* resolvedPlacementIndex) {
        if (resolvedPlacementIndex)
            *resolvedPlacementIndex = -1;
        if (mode)
            *mode = SwapMode::FullActor;
        // The in-game force disable stops here rather than in the assignment:
        // the resolved table, its capture-gate witnesses, and the spoiler log
        // all describe the seed and must stay exactly as generated. Returning
        // "no replacement" makes every placement build its vanilla actor
        // without any of that moving.
        if (Randomizer::isChaosForceDisabled())
            return nullptr;
        if (!stage || !objId)
            return nullptr;
        s32 placementIndex = -1;
        const DB::Placement* placement = findPlacementForRuntime(
            stage, objId, unitConfigName, &placementIndex);

        // Resolve inspector state from the live identity as well as from the
        // generated row selected above. The factory may report a copied zone
        // PlacementInfo whose stage key is not the one used by the extracted
        // table; in that case a per-row lookup can select the wrong duplicate
        // or miss the bulk setting entirely. Manual overrides are session
        // state, so they must remain usable even when the generated solver or
        // memory model is unavailable.
        if (unitConfigName && EnemySwapDebug::hasAnyOverride()) {
            const char* logicStage = EnemySwapDebug::resolveLogicStage(stage);
            s32 runtimeOverridePlacement = -1;
            if (EnemySwapDebug::findRuntimeOverride(
                    stage, logicStage, objId, unitConfigName,
                    nullptr, &runtimeOverridePlacement)
                && runtimeOverridePlacement >= 0
                && runtimeOverridePlacement < DB::sPlacementCount) {
                placement = &DB::sPlacements[runtimeOverridePlacement];
                placementIndex = runtimeOverridePlacement;
            } else if (runtimeOverridePlacement
                == EnemySwapDebug::kAmbiguousRuntimeOverride) {
                // There is no safe way to tell two authored rows with the
                // same live identity apart. Do not let a generated row or a
                // first-match fallback make one committed pair win.
                return nullptr;
            }
        }
        if (!placement || placement->entity < 0
            || placement->entity >= DB::sEntityCount
            || placementIndex < 0
            || placementIndex >= DB::sPlacementCount)
            return nullptr;
        if (resolvedPlacementIndex)
            *resolvedPlacementIndex = placementIndex;
        const DB::Entity& source = DB::sEntities[placement->entity];
        // The runtime identity resolver above already handles stage aliases
        // and duplicate object IDs. Once a concrete placement row is known,
        // use only that row's effective value. There is deliberately no
        // first-match scan over the rest of an object-ID group: if the game
        // does not expose enough identity to distinguish two rows, the
        // resolver must fail closed instead of making one committed pair
        // hide another.
        const s16 overrideEntity = EnemySwapDebug::getOverrideEntity(
            placementIndex);
        // Inspector overrides are session-local and deliberately do not
        // depend on a valid generated assignment. This is important when the
        // seed solver rejects a constrained or memory-heavy assignment: the
        // debug menu must still be able to force a replacement for diagnosis.
        // Build the generated table only when no explicit override is present.
        if (overrideEntity < 0) {
            if (!sInitialized)
                buildRemapTable(
                    Randomizer::getSeed(),
                    sLastAttempt == 0xFFFFFFFFu ? 0 : sLastAttempt);
            if (!sAssignmentValid)
                return nullptr;
        }
        const s16 replacement = overrideEntity >= 0
            ? overrideEntity
            : sResolvedEntity[placementIndex];
        if (replacement == kDeletedPlacementEntity) {
            if (mode)
                *mode = SwapMode::Suppress;
            return nullptr;
        }
        if (replacement < 0 || replacement >= DB::sEntityCount
            || replacement == placement->entity)
            return nullptr;
        const DB::Entity& result = DB::sEntities[replacement];
        if (isForbiddenReplacement(result))
            return nullptr;
        if (overrideEntity >= 0 && sInspectorResolutionLogCount < 64) {
            Randomizer::logPush(
                "enemy inspector override resolved: stage=%s object=%s "
                "placement=%d source=%s target=%s",
                stage, objId, placementIndex, source.name, result.name);
            sInspectorResolutionLogCount++;
        }
        if (mode)
            *mode = overrideEntity >= 0
                ? SwapMode::ManualOverride
                : SwapMode::FullActor;
        return &result;
    }

    bool hasRouteCaptureGates(
        const char* edgeId, const char* sourceCaptureToken) {
        if (!Randomizer::isEnemySwapRandoEnabled()
            || !edgeId || !sourceCaptureToken)
            return false;
        if (isReliableCaptureToken(nullptr, sourceCaptureToken))
            return false;
        for (s32 i = 0; i < DB::sGateGroupCount; i++)
            if (strEq(DB::sGateGroups[i].edgeId, edgeId)
                && strEq(DB::sGateGroups[i].sourceCaptureToken,
                    sourceCaptureToken)) {
                const DB::GateGroup& group = DB::sGateGroups[i];
                for (s32 offset = 0; offset < group.gateCount; offset++) {
                    const DB::CaptureGate& gate = DB::sGates[group.gateOffset + offset];
                    if (!gateDifficultyAllowed(gate))
                        continue;
                    if (isStageLockedCapture(
                            gate.logicStage, sourceCaptureToken))
                        return false;
                    return true;
                }
                return false;
            }
        return false;
    }

    s32 getSatisfiedRouteCaptureTokens(
        const char* edgeId, const char* sourceCaptureToken, s16 scenario,
        s16* outTokens, s32 maxTokens) {
        if (!outTokens || maxTokens <= 0
            || !hasRouteCaptureGates(edgeId, sourceCaptureToken))
            return 0;
        if (!sInitialized && !buildRemapTable(Randomizer::getSeed(), 0))
            return 0;
        for (s32 groupIndex = 0; groupIndex < DB::sGateGroupCount; groupIndex++) {
            const DB::GateGroup& group = DB::sGateGroups[groupIndex];
            if (!strEq(group.edgeId, edgeId)
                || !strEq(group.sourceCaptureToken, sourceCaptureToken))
                continue;
            s32 count = 0;
            for (s32 offset = 0; offset < group.gateCount; offset++) {
                const DB::CaptureGate& gate = DB::sGates[group.gateOffset + offset];
                if (!gateDifficultyAllowed(gate)
                    || !gateMatches(gate, scenario))
                    continue;
                const s32 token = findLogicTokenIndex(gate.replacementCaptureToken);
                if (token < 0)
                    continue;
                bool duplicate = false;
                for (s32 i = 0; i < count; i++)
                    if (outTokens[i] == token)
                        duplicate = true;
                if (!duplicate && count < maxTokens)
                    outTokens[count++] = (s16)token;
            }
            return count;
        }
        return 0;
    }

    s32 remapLogicToken(s16 stageIndex, s32 tokenIndex) {
        if (!Randomizer::isEnemySwapRandoEnabled()
            || tokenIndex < 0 || tokenIndex >= LD::sTokenCount
            || LD::sTokens[tokenIndex].type != LD::TokenType_Capture)
            return tokenIndex;
        const char* token = LD::sTokens[tokenIndex].key;
        const char* stage = (stageIndex >= 0 && stageIndex < LD::sStageCount)
            ? LD::sStages[stageIndex].stage
            : nullptr;
        if (isReliableCaptureToken(stage, token))
            return tokenIndex;
        if (stage && isStageLockedCapture(stage, token))
            return tokenIndex;
        return -1;
    }

} // namespace EnemySwapRando
