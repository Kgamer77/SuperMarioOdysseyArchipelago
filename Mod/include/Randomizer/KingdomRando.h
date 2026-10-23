#pragma once

#include <basis/seadTypes.h>

namespace KingdomRando {

    struct BranchPair {
        s32 first;
        s32 second;
    };

    struct RouteTier {
        s32 worlds[2];
        s32 count;
    };

    void rebuild(u32 seed, u32 attempt = 0);
    void clear();
    bool isInitialized();

    s32 getTierCount();
    const RouteTier* getTier(s32 index);
    s32 findTierIndex(s32 logicalWorldId);

    s32 getRouteWorldCount();
    s32 getRouteWorldId(s32 index);
    bool isRouteWorld(s32 logicalWorldId);
    s32 getStartingWorldId();
    bool isMoonInRandomOrder();

} // namespace KingdomRando
