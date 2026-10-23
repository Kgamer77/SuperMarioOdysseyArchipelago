#pragma once

#include <basis/seadTypes.h>

class GameDataFile;
class GameDataHolder;

namespace al {
    class LiveActor;
    class PlacementInfo;
}

namespace PurpleCoinRewards {

    void installHooks();

    // Purchase ledger.  Nothing in the save distinguishes a reward that was
    // bought with regional coins from one that was earned off a moon, and a
    // Soft Nuzlocke reroll needs that distinction: it resets the regional-coin
    // economy outright, so what those coins bought is surrendered rather than
    // entering the retain lottery with the run's earned unlocks.  Recorded as
    // three compact records because the hack dictionary is a shared 160-slot
    // array, not per-purchase entries.
    u64 getPurchasedAbilityMask(const GameDataFile* file);
    u64 getPurchasedCaptureMask(const GameDataFile* file);
    // Returns how many purchased moons were written to out (ShineDB indices).
    s32 getPurchasedMoonIndices(const GameDataFile* file, s32* out, s32 maxOut);
    void clearPurchaseLedger(GameDataFile* file);
    // Called as part of seed setup. Per-shop entries are then materialized from
    // this deterministic seed state when that seller's shop UI is opened.
    void rebuildShopInventories();
    // Placement actors move while talking, so conversation uniqueness cannot
    // use their live transform. Register their authored identity during stage
    // construction and clear it before the next stage builds.
    void clearNpcActorPlacements();
    void registerNpcActorPlacement(
        const al::LiveActor* actor, const al::PlacementInfo* placementInfo);
    void recordNpcConversation(const al::LiveActor* actor);
    s32 findOriginalWorldIdForStage(const char* stageName);
    // Resolves the dynamic title and description keys assigned to generated
    // Purple Shop rows. Routed through the shared system-message hook.
    const char16_t* tryGetShopMessageOverride(const char* fileName, const char* key);

} // namespace PurpleCoinRewards
