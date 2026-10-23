#include "Randomizer/SkyboxRando.h"
#include "EntranceDB.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/WorldMeta.h"
#include "al/Library/Base/StringUtil.h"

#include <cstring>

// FNV-1a hash of a NUL-terminated string; used to derive a stable per-sub-area
// RNG sub-stream from the stage name without shipping a list of sub-area names.
static u32 hashStageName(const char* s) {
    u32 h = 0x811C9DC5u;
    for (; *s; ++s) {
        h ^= (u32)(u8)*s;
        h *= 0x01000193u;
    }
    return h;
}

static constexpr s32 kStageCount = WorldMeta::sWorldCount;
static s32 sMapping[kStageCount];
static bool sInitialized = false;
static u32 sLastSeed = 0xFFFFFFFF;

// ── Sub-area skybox source pool ────────────────────────────────────────────────
// The pool a sub-area can draw a skybox from: the 17 kingdom main stages plus
// every distinct sub-area in EntranceDB, minus stages that are obviously indoor
// (shops, costume/slot rooms, 2D and secret rooms) and so carry no real sky —
// borrowing those would produce a black/empty sky. The list is heuristic by
// design; add markers here as testing turns up more sky-less sources.
struct SkyboxSource {
    const char* stage;
    s32 scenario; // -1 = default scenario (fine for resource loading)
};

static constexpr s32 kMaxSources = 256;
static SkyboxSource sSources[kMaxSources];
static s32 sSourceCount = 0;
// sSources[0, sKingdomSourceCount) are kingdom skyboxes; the rest are sub-areas.
static s32 sKingdomSourceCount = 0;

static bool isIndoorSubArea(const char* stage) {
    static const char* const kIndoorMarkers[] = { "Shop", "Costume", "Slot", "Secret", "2D" };
    for (const char* marker : kIndoorMarkers)
        if (std::strstr(stage, marker))
            return true;
    return false;
}

static bool isSkyboxRandoExcluded(const char* stage) {
    static const char* const kExcluded[] = { "PeachWorldShopStage", "RollingExStage" };
    for (const char* excluded : kExcluded)
        if (std::strcmp(stage, excluded) == 0)
            return true;
    return false;
}

static bool sourcePoolContains(const char* stage) {
    for (s32 i = 0; i < sSourceCount; i++)
        if (std::strcmp(sSources[i].stage, stage) == 0)
            return true;
    return false;
}

static void buildSourcePool() {
    sSourceCount = 0;

    // Kingdom main stages keep their tested skybox scenario.
    for (s32 i = 0; i < (s32)WorldMeta::sWorldCount; i++) {
        const WorldMeta::Entry* entry = WorldMeta::getBySkyboxIndex(i);
        if (entry && sSourceCount < kMaxSources)
            sSources[sSourceCount++] = { entry->mainStage, entry->skyboxScenario };
    }
    sKingdomSourceCount = sSourceCount;

    // Distinct, non-indoor sub-areas from the entrance database (default scenario).
    for (s32 i = 0; i < EntranceDB::sLinkCount && sSourceCount < kMaxSources; i++) {
        if (!EntranceDB::isRuntimeUsableLink(i))
            continue;
        const char* sub = EntranceDB::sLinks[i].subStage;
        if (!sub || !sub[0] || isIndoorSubArea(sub) || isSkyboxRandoExcluded(sub) || sourcePoolContains(sub))
            continue;
        sSources[sSourceCount++] = { sub, -1 };
    }
}

// Pick a stable source from sSources[begin, end) for this stage (per-stage RNG).
static const SkyboxSource* pickSource(const char* stageName, s32 begin, s32 end) {
    if (end <= begin)
        return nullptr;
    Randomizer::Rng rng = Randomizer::makeRng(sLastSeed ^ hashStageName(stageName), Randomizer::RngStream::Skybox);
    return &sSources[begin + (s32)rng.nextModulo((u32)(end - begin))];
}

// Compatibility matrix: sCompatible[kingdom] is a bitmask of which skyboxes work in that kingdom.
// Bit N = skybox index N is compatible.
// Derived directly from the tested list.
static constexpr u32 sCompatible[kStageCount] = {
    // Cap (0): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Luncheon (10)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Cascade (1): Cascade, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cap (0), Sand (2), Luncheon (10)
    (1 << 1) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Sand (2): Cap, Cascade, Sand, Lake, Wooded, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cloud (5), Luncheon (10)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Lake (3): Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cap (0), Luncheon (10)
    (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Wooded (4): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Moon, Mushroom, Dark, Darker
    // missing: Luncheon (10), Bowser's (12)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Cloud (5): Cap, Sand, Cloud, Lost, Seaside, Luncheon, Ruined, Moon, Dark, Darker
    // missing: Cascade (1), Lake (3), Wooded (4), Metro (7), Snow (8), Bowser's (12), Mushroom (14)
    (1 << 0) | (1 << 2) | (1 << 5) | (1 << 6) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 13) | (1 << 15) | (1 << 16),

    // Lost (6): Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cap (0), Luncheon (10)
    (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Metro (7): Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Luncheon, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cap (0)
    (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Snow (8): Cap, Sand, Lake, Cloud, Lost, Snow, Seaside, Luncheon, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cascade (1), Wooded (4), Metro (7), Ruined (11)
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Seaside (9): Cascade, Sand, Lake, Wooded, Metro, Snow, Seaside, Luncheon, Moon, Mushroom, Dark, Darker
    // missing: Cap (0), Cloud (5), Lost (6), Ruined (11), Bowser's (12)
    (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Luncheon (10): Cascade, Sand, Lake, Snow, Seaside, Luncheon, Bowser's, Mushroom
    // missing: Cap (0), Wooded (4), Cloud (5), Lost (6), Metro (7), Ruined (11), Moon (13), Dark (15), Darker (16)
    (1 << 1) | (1 << 2) | (1 << 3) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 12) | (1 << 14),

    // Ruined (11): Cap, Cascade, Lake, Cloud, Lost, Snow, Seaside, Ruined, Bowser's, Darker
    // missing: Sand (2), Wooded (4), Metro (7), Luncheon (10), Moon (13), Mushroom (14), Dark (15)
    (1 << 0) | (1 << 1) | (1 << 3) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 16),

    // Bowser's (12): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Luncheon, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: none — all work
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Moon (13): Cap, Sand, Lake, Wooded, Cloud, Lost, Snow, Seaside, Luncheon, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Cascade (1), Metro (7)
    (1 << 0) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 10) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Mushroom (14): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Luncheon (10)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Dark (15): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Luncheon (10)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),

    // Darker (16): Cap, Cascade, Sand, Lake, Wooded, Cloud, Lost, Metro, Snow, Seaside, Ruined, Bowser's, Moon, Mushroom, Dark, Darker
    // missing: Luncheon (10)
    (1 << 0) | (1 << 1) | (1 << 2) | (1 << 3) | (1 << 4) | (1 << 5) | (1 << 6) | (1 << 7) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 12) | (1 << 13) | (1 << 14) | (1 << 15) | (1 << 16),
};

namespace SkyboxRando {

    void reinit() {
        u32 seed = Randomizer::getSeed();
        if (sInitialized && seed == sLastSeed)
            return;

        // Sub-area source pool is seed-independent; (re)build it alongside the mapping.
        buildSourcePool();

        // Build a per-kingdom valid skybox list from the compatibility mask,
        // then pick one randomly for each kingdom.
        Randomizer::Rng rng = Randomizer::makeRng(seed, Randomizer::RngStream::Skybox);

        for (s32 kingdom = 0; kingdom < kStageCount; kingdom++) {
            // Collect all compatible skybox indices for this kingdom.
            s32 candidates[kStageCount];
            s32 candidateCount = 0;
            for (s32 sky = 0; sky < kStageCount; sky++) {
                if (sCompatible[kingdom] & (1u << sky))
                    candidates[candidateCount++] = sky;
            }

            // Pick one at random.
            sMapping[kingdom] = candidates[rng.nextModulo((u32)candidateCount)];
        }

        sLastSeed = seed;
        sInitialized = true;
    }

    void clear() {
        sInitialized = false;
        sLastSeed = 0xFFFFFFFF;
        sSourceCount = 0;
    }

    const char* remap(const char* stageName) {
        s32 dummy;
        return remapWithScenario(stageName, &dummy);
    }

    // Kingdom (main-stage) skybox. In Kingdoms/Own Areas it draws a compatible
    // kingdom sky via the compatibility matrix; in Max it draws from the full
    // pool (kingdoms + sub-areas, no compatibility limit). None/Subareas: no-op.
    const char* remapWithScenario(const char* stageName, s32* outScenario) {
        if (!stageName || !stageName[0] || !sInitialized)
            return stageName;
        if (isSkyboxRandoExcluded(stageName))
            return stageName;
        if (!Randomizer::isKingdomSkyboxRandoEnabled())
            return stageName;

        if (Randomizer::getSkyboxRandoMode() == Randomizer::SkyboxRandoMode_Max) {
            const SkyboxSource* source = pickSource(stageName, 0, sSourceCount);
            if (!source || !source->stage)
                return stageName;
            if (outScenario)
                *outScenario = source->scenario;
            return source->stage;
        }

        // Kingdoms / Own Areas: compatibility-matrix pick among kingdom skies.
        for (s32 i = 0; i < kStageCount; i++) {
            const WorldMeta::Entry* source = WorldMeta::getBySkyboxIndex(i);
            if (source && al::isEqualString(stageName, source->mainStage)) {
                const WorldMeta::Entry* target = WorldMeta::getBySkyboxIndex(sMapping[i]);
                if (!target)
                    return stageName;
                if (outScenario)
                    *outScenario = target->skyboxScenario;
                return target->mainStage;
            }
        }

        return stageName;
    }

    // Sub-area skybox. In Subareas/Own Areas it draws from sub-area skies only; in
    // Max it draws from the full pool (kingdoms + sub-areas). None/Kingdoms: no-op.
    const char* remapSubAreaWithScenario(const char* stageName, s32* outScenario) {
        if (!stageName || !stageName[0] || !sInitialized)
            return stageName;
        if (isSkyboxRandoExcluded(stageName))
            return stageName;
        if (!Randomizer::isSubAreaSkyboxRandoEnabled())
            return stageName;

        // Main stages are handled by remapWithScenario; leave them be.
        if (WorldMeta::findByMainStage(stageName))
            return stageName;

        // Max draws from everything; otherwise restrict to the sub-area sub-range.
        const s32 begin = (Randomizer::getSkyboxRandoMode() == Randomizer::SkyboxRandoMode_Max)
            ? 0
            : sKingdomSourceCount;
        const SkyboxSource* source = pickSource(stageName, begin, sSourceCount);
        if (!source || !source->stage)
            return stageName;

        if (outScenario)
            *outScenario = source->scenario;
        return source->stage;
    }

} // namespace SkyboxRando
