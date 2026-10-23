#include "Randomizer/KingdomRando.h"

#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/WorldMeta.h"

namespace KingdomRando {
    namespace {

        static constexpr s32 kTierCount = 12;
        static constexpr s32 kMaxRouteWorlds = 14;
        static constexpr s32 kTierSizes[kTierCount] = {
            1,
            1,
            1,
            2,
            1,
            1,
            1,
            2,
            1,
            1,
            1,
            1,
        };

        static constexpr RouteTier kVanillaTiers[kTierCount] = {
            { { WorldMeta::WorldId_Cap, -1 }, 1 },
            { { WorldMeta::WorldId_Cascade, -1 }, 1 },
            { { WorldMeta::WorldId_Sand, -1 }, 1 },
            { { WorldMeta::WorldId_Lake, WorldMeta::WorldId_Wooded }, 2 },
            { { WorldMeta::WorldId_Cloud, -1 }, 1 },
            { { WorldMeta::WorldId_Lost, -1 }, 1 },
            { { WorldMeta::WorldId_Metro, -1 }, 1 },
            { { WorldMeta::WorldId_Snow, WorldMeta::WorldId_Seaside }, 2 },
            { { WorldMeta::WorldId_Luncheon, -1 }, 1 },
            { { WorldMeta::WorldId_Ruined, -1 }, 1 },
            { { WorldMeta::WorldId_Bowsers, -1 }, 1 },
            { { WorldMeta::WorldId_Moon, -1 }, 1 },
        };

        static constexpr s32 kRandomizableWorldsWithoutMoon[] = {
            WorldMeta::WorldId_Cap,
            WorldMeta::WorldId_Cascade,
            WorldMeta::WorldId_Sand,
            WorldMeta::WorldId_Wooded,
            WorldMeta::WorldId_Lake,
            WorldMeta::WorldId_Cloud,
            WorldMeta::WorldId_Lost,
            WorldMeta::WorldId_Metro,
            WorldMeta::WorldId_Seaside,
            WorldMeta::WorldId_Snow,
            WorldMeta::WorldId_Luncheon,
            WorldMeta::WorldId_Ruined,
            WorldMeta::WorldId_Bowsers,
        };

        static RouteTier sTiers[kTierCount] = {
            { { WorldMeta::WorldId_Cap, -1 }, 1 },
            { { WorldMeta::WorldId_Cascade, -1 }, 1 },
            { { WorldMeta::WorldId_Sand, -1 }, 1 },
            { { WorldMeta::WorldId_Lake, WorldMeta::WorldId_Wooded }, 2 },
            { { WorldMeta::WorldId_Cloud, -1 }, 1 },
            { { WorldMeta::WorldId_Lost, -1 }, 1 },
            { { WorldMeta::WorldId_Metro, -1 }, 1 },
            { { WorldMeta::WorldId_Snow, WorldMeta::WorldId_Seaside }, 2 },
            { { WorldMeta::WorldId_Luncheon, -1 }, 1 },
            { { WorldMeta::WorldId_Ruined, -1 }, 1 },
            { { WorldMeta::WorldId_Bowsers, -1 }, 1 },
            { { WorldMeta::WorldId_Moon, -1 }, 1 },
        };
        static bool sInitialized = false;
        static bool sMoonInRandomOrder = false;

        void copyVanillaRoute() {
            for (s32 i = 0; i < kTierCount; i++)
                sTiers[i] = kVanillaTiers[i];
            sMoonInRandomOrder = false;
        }

        void shuffle(s32* worlds, s32 count, Randomizer::Rng& rng) {
            for (s32 i = count - 1; i > 0; i--) {
                const s32 swapIndex = (s32)rng.nextModulo((u32)(i + 1));
                const s32 tmp = worlds[i];
                worlds[i] = worlds[swapIndex];
                worlds[swapIndex] = tmp;
            }
        }

        static u32 seedForAttempt(u32 seed, u32 attempt) {
            return seed ^ (attempt * 0xA341316Cu);
        }

        void buildRandomRoute(u32 seed, u32 attempt) {
            s32 pool[kMaxRouteWorlds] = {};
            s32 poolCount = 0;

            for (s32 i = 0; i < (s32)(sizeof(kRandomizableWorldsWithoutMoon) / sizeof(kRandomizableWorldsWithoutMoon[0])); i++)
                pool[poolCount++] = kRandomizableWorldsWithoutMoon[i];

            sMoonInRandomOrder = Randomizer::isMoonKingdomRequirementsEnabled();
            if (sMoonInRandomOrder)
                pool[poolCount++] = WorldMeta::WorldId_Moon;

            Randomizer::Rng rng = Randomizer::makeRng(seedForAttempt(seed, attempt), Randomizer::RngStream::Kingdom);
            shuffle(pool, poolCount, rng);

            s32 poolIndex = 0;
            for (s32 tier = 0; tier < kTierCount; tier++) {
                sTiers[tier].count = kTierSizes[tier];
                sTiers[tier].worlds[0] = -1;
                sTiers[tier].worlds[1] = -1;

                if (!sMoonInRandomOrder && tier == kTierCount - 1) {
                    sTiers[tier].worlds[0] = WorldMeta::WorldId_Moon;
                    sTiers[tier].count = 1;
                    continue;
                }

                for (s32 i = 0; i < sTiers[tier].count; i++) {
                    if (poolIndex >= poolCount) {
                        copyVanillaRoute();
                        return;
                    }
                    sTiers[tier].worlds[i] = pool[poolIndex++];
                }
            }
        }

    } // namespace

    void rebuild(u32 seed, u32 attempt) {
        if (!Randomizer::isKingdomRandoEnabled()) {
            clear();
            return;
        }

        buildRandomRoute(seed, attempt);
        sInitialized = true;
    }

    void clear() {
        copyVanillaRoute();
        sInitialized = false;
    }

    bool isInitialized() { return sInitialized; }

    s32 getTierCount() { return kTierCount; }

    const RouteTier* getTier(s32 index) {
        if (index < 0 || index >= kTierCount)
            return nullptr;
        return &sTiers[index];
    }

    s32 findTierIndex(s32 logicalWorldId) {
        for (s32 tier = 0; tier < kTierCount; tier++) {
            for (s32 i = 0; i < sTiers[tier].count; i++) {
                if (sTiers[tier].worlds[i] == logicalWorldId)
                    return tier;
            }
        }
        return -1;
    }

    s32 getRouteWorldCount() {
        s32 count = 0;
        for (s32 tier = 0; tier < kTierCount; tier++)
            count += sTiers[tier].count;
        return count;
    }

    s32 getRouteWorldId(s32 index) {
        if (index < 0)
            return -1;

        s32 routeIndex = 0;
        for (s32 tier = 0; tier < kTierCount; tier++) {
            for (s32 i = 0; i < sTiers[tier].count; i++) {
                if (routeIndex == index)
                    return sTiers[tier].worlds[i];
                routeIndex++;
            }
        }
        return -1;
    }

    bool isRouteWorld(s32 logicalWorldId) {
        return findTierIndex(logicalWorldId) >= 0;
    }

    s32 getStartingWorldId() {
        return sTiers[0].worlds[0];
    }

    bool isMoonInRandomOrder() {
        return sMoonInRandomOrder;
    }

} // namespace KingdomRando
