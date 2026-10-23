#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FeatureFlags.h"

#if SMOR_FEATURE_CHAOS_ANY
#include "Randomizer/EnemySwapDB.h"

// Enemy/Capture Swap randomizer. It resolves one versioned assignment per
// seed/attempt, constructively satisfies directional route capture gates, and
// then uses that assignment for both actor creation and route evaluation.
// logic/enemy_swap.py is the host-side parity implementation.
namespace EnemySwapRando {

    // A resolved prop with this value is intentionally not constructed. Keep
    // it distinct from -1, which means unavailable/uninitialized throughout
    // the existing assignment and lookup APIs.
    inline constexpr s16 kDeletedPlacementEntity = -2;

    enum class FailureCode : u8
    {
        None = 0,
        UnsatisfiedConstraints = 1,
        WitnessVerification = 2,
        MemoryBudgetExceeded = 3,
        SpawnRestricted = 4,
    };

    // FullActor replaces UnitConfigName/class/model. ModelOnly changes just
    // ModelName, preserving the source actor's behavior and placement data.
    // ManualOverride is a deliberate inspector override: it uses the same
    // full-actor identity swap, but is not subject to seed-assignment memory
    // admission or generated-NPC safety filters.
    enum class SwapMode : u8
    {
        FullActor,
        ModelOnly,
        ManualOverride,
        Suppress,
    };

    // Deterministic per-seed assignment; cached on the last inputs. False
    // rejects the seed attempt because no authored gate witness was possible.
    bool buildRemapTable(u32 seed, u32 attempt = 0);
    void clear();
    bool isInitialized();
    FailureCode getFailureCode();
    bool isMemoryDataAvailable();
    const Randomizer::EnemySwapDB::EntityMemoryCost* getMemoryCost(
        s16 entityIndex);
    bool checkMemoryBudgetForReplacement(
        s32 placementIndex, s16 entityIndex);
    void getMemoryFailure(
        const char** stage, s16* scenario,
        u32* extraResourceBytes, u32* maxExtraResourceBytes,
        u32* extraSceneBytes, u32* maxExtraSceneBytes);

    // Audited compatibility exceptions from config/enemy_spawn_restrictions.csv.
    // The table is empty unless the generator ran with --enforce-suspected or a
    // restriction was promoted to status=confirmed, so these are no-ops by
    // default. checkSpawnRestrictionForReplacement returns true when placing
    // entityIndex at placementIndex is allowed.
    bool isSpawnRestrictionDataAvailable();
    bool checkSpawnRestrictionForReplacement(
        s32 placementIndex, s16 entityIndex);
    void getSpawnRestrictionFailure(
        const char** stage, s16* scenario, s16* entity,
        s32* count, s32* maxInstances);

    // Re-resolves the assignment with a different attempt, so the same seed
    // yields a different set of swaps. For hunting crashes: reroll, reload the
    // stage, see whether it still dies.
    //
    // Session-only and never persisted, because it is not seed-stable in the
    // way the rest of this file is: a different assignment means different
    // captures exist, so the route logic a seed was verified against no longer
    // describes the file. Reloading the save restores the seed's own roll.
    // Deterministic all the same -- roll N of a seed is always the same roll --
    // so a crashing roll can be returned to by counting.
    bool rerollAssignment();
    u32 getRerollCount();

    // Deterministic assignment diagnostics. These intentionally exclude
    // session-only inspector overrides so spoiler data stays seed-stable.
    s32 getResolvedPlacementCount();
    s16 getResolvedEntityIndex(s32 placementIndex);
    // True when the constructive logic solver forced this placement to be a
    // physical capture-gate witness. Runtime resource admission must reserve
    // these placements before spending capacity on optional swaps.
    bool isCaptureGateWitnessPlacement(s32 placementIndex);
    s32 getRouteWitnessCount();
    bool getRouteWitness(
        s32 witnessIndex, const char** edgeId, s16* scenario,
        const char** gateId);

    // Replacement entity for a placed instance, or nullptr to spawn vanilla.
    // A nullptr with mode == Suppress intentionally skips actor construction.
    // stage is the placement's stage file name (zone name for zone objects),
    // objId the placement "Id" string (e.g. "obj123"). unitConfigName, when
    // given, must match the instance the DB recorded (stale-DB safety).
    const Randomizer::EnemySwapDB::Entity* findReplacement(
        const char* stage, const char* objId, const char* unitConfigName = nullptr,
        SwapMode* mode = nullptr, s32* placementIndex = nullptr);

    // Sorted generated placement lookup used by the actor hook and inspector.
    const Randomizer::EnemySwapDB::Placement* findPlacement(
        const char* stage, const char* objId, s32* placementIndex = nullptr);

    // Most generated identities equal the live UnitConfigName. A small set of
    // model-selected props (Sand ice variants) use ModelName as their database
    // identity and intentionally accept the generic live placement name.
    bool sourceEntityMatchesUnitConfig(
        s16 entityIndex, const char* unitConfigName);

    // Whether a runtime placement-file key names a physical stage represented
    // in the generated table, accepting the common trailing "Map" spelling.
    bool isKnownPlacementStage(const char* stage);

    // Runtime placement lookup. The exact stage/object key is preferred, but
    // actor creation can receive a zone name that differs from the stage key
    // used by the extracted placement table. In that case the unit-config
    // name is used as a stale-data-safe fallback when it identifies one
    // placement or a deterministic duplicate group.
    const Randomizer::EnemySwapDB::Placement* findPlacementForRuntime(
        const char* stage, const char* objId, const char* unitConfigName,
        s32* placementIndex = nullptr);

    // Edge-aware capture-gate access. A route evaluator first checks whether
    // the edge/source token has authored gates, then asks for replacement
    // LogicData token indices whose physical object predicates are true.
    bool hasRouteCaptureGates(
        const char* edgeId, const char* sourceCaptureToken);
    s32 getSatisfiedRouteCaptureTokens(
        const char* edgeId, const char* sourceCaptureToken, s16 scenario,
        s16* outTokens, s32 maxTokens);

    // Logic-side capture-token remap for requirements evaluated on a stage
    // (LogicData stage index). Returns -1 to mean "this capture path is not a
    // guaranteed logic route", which makes only that alternative unavailable:
    //   - a token-less full-pool swap result (the original capture was removed), or
    //   - any capture that Enemy/Capture chaos can randomize away. Captures whose
    //     actor is never swapped out (Mini Rocket, Spark Pylon, scripted/attached
    //     captures, bosses; EnemySwapDB::sReliableCaptureTokens) stay usable.
    // Identity when the randomizer is off, or for movement/ability/event tokens
    // and reliable captures with no active swap group.
    s32 remapLogicToken(s16 stageIndex, s32 tokenIndex);

    // Whether an Enemy Swap capture token (EnemySwapDB name / LogicData key) is
    // guaranteed present under chaos in the given stage, so logic may still
    // require it there. A capture is reliable if it can never be swapped out
    // at all, or if no capture gate was authored for that exact (stage,
    // token) pair -- vanilla until gated, scoped per physical stage. Pass a
    // null stage to check across every stage instead (matches a gate
    // anywhere), for callers not tied to one specific place.
    bool isReliableCaptureToken(const char* stage, const char* tokenKey);

} // namespace EnemySwapRando
#else
// Keep callers simple in builds without Chaos Randomization. The feature's
// implementation and generated swap database are excluded by CMake.
namespace EnemySwapRando {
    enum class FailureCode : u8
    {
        None = 0,
        UnsatisfiedConstraints = 1,
        WitnessVerification = 2,
        MemoryBudgetExceeded = 3,
        SpawnRestricted = 4,
    };
    inline bool buildRemapTable(u32, u32 = 0) { return true; }
    inline void clear() { }
    inline bool isInitialized() { return false; }
    inline FailureCode getFailureCode() { return FailureCode::None; }
    inline bool isMemoryDataAvailable() { return false; }
    inline const void* getMemoryCost(s16) {
        return nullptr;
    }
    inline bool checkMemoryBudgetForReplacement(s32, s16) { return true; }
    inline void getMemoryFailure(
        const char**, s16*, u32*, u32*, u32*, u32*) { }
    inline bool isSpawnRestrictionDataAvailable() { return false; }
    inline bool checkSpawnRestrictionForReplacement(s32, s16) { return true; }
    inline void getSpawnRestrictionFailure(
        const char**, s16*, s16*, s32*, s32*) { }
    inline s32 getResolvedPlacementCount() { return 0; }
    inline s16 getResolvedEntityIndex(s32) { return -1; }
    inline bool isCaptureGateWitnessPlacement(s32) { return false; }
    inline bool isKnownPlacementStage(const char*) { return false; }
    inline bool sourceEntityMatchesUnitConfig(s16, const char*) { return false; }
    inline const void* findPlacementForRuntime(
        const char*, const char*, const char*, s32* = nullptr) { return nullptr; }
    inline s32 getRouteWitnessCount() { return 0; }
    inline bool getRouteWitness(s32, const char**, s16*, const char**) {
        return false;
    }
    inline bool hasRouteCaptureGates(const char*, const char*) { return false; }
    inline s32 getSatisfiedRouteCaptureTokens(
        const char*, const char*, s16, s16*, s32) { return 0; }
    inline s32 remapLogicToken(s16, s32 tokenIndex) { return tokenIndex; }
    inline bool isReliableCaptureToken(const char*, const char*) { return false; }
} // namespace EnemySwapRando
#endif
