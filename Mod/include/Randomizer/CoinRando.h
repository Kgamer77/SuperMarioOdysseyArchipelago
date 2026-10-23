#pragma once

#include <basis/seadTypes.h>

namespace al {
    class ActorInitInfo;
    class PlacementId;
    class PlacementInfo;
}

namespace CoinRando {

    struct Swap {
        const char* objectName = nullptr;
        const char* displayName = nullptr;
        const char* className = nullptr;
        const char* modelName = nullptr;
    };

    // Resolves a placed standalone coin against the deterministic per-map
    // assignment. currentStage is only a fallback: embedded maps are keyed from
    // PlacementInfo so duplicate obj IDs in different zones never share a pool.
    bool resolveSwap(const al::PlacementInfo& placementInfo,
        const char* currentStage, s16 scenarioNoPlacement, Swap* outSwap);

    // Purple-coin save calls use the canonical source-purple identity, even when
    // that coin actor now lives at a regular-coin placement.
    const al::PlacementId* translatePurplePlacement(
        const al::PlacementId* placementId);

    void resetStageState();
    void installHooks();

    // Used only by ProgressionHooks, which owns the placement factory trampoline
    // when Chaos Randomization is compiled out. CoinRando's own identity readers
    // consume this nested scope in that build.
    void beginIdentityScope(const al::PlacementInfo* placementInfo,
        const char* sourceObjectId, const char* sourceUnitConfigName,
        const Swap& swap);
    void endIdentityScope();

} // namespace CoinRando
