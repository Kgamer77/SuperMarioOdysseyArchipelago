#include "Randomizer/MusicRando.h"

#include "MusicDB.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"

#include "nn/os.h"

#include <cstring>

namespace MusicRando {

    static constexpr s32 kMaxAllResources = MusicDB::sWorldMainTableSize + MusicDB::sSubStageTableSize + MusicDB::sKnownResourceTableSize + MusicDB::sBossResourceTableSize;

    static const char* sAllResources[kMaxAllResources];
    static s32 sAllRemap[kMaxAllResources];
    static s32 sAllResourceCount = 0;

    static bool sInitialized = false;
    static u32 sLastSeed = 0xFFFFFFFF;
    static bool sLastStreamerModeEnabled = false;
    static bool sLast2DThemesDisabled = false;
    static bool sLastExtraThemesDisabled = false;
    static bool sLastBossThemesDisabled = false;
    static u64 sLastThemeMask = ~static_cast<u64>(0);
    static u32 sRuntimeRandomState = 0x9E3779B9u;
    static u32 sAreaRandomSalt = 0;

    static bool isEqual(const char* a, const char* b) {
        return a && b && std::strcmp(a, b) == 0;
    }

    static bool startsWith(const char* value, const char* prefix) {
        if (!value || !prefix)
            return false;
        return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
    }

    // Every StmRsBgm* stream resource is shufflable — no category exclusions.
    // The prefix check keeps non-stream resources (Wsd* jingles, StmDummy*
    // silence) out of the pool.
    static bool endsWith(const char* value, const char* suffix) {
        if (!value || !suffix)
            return false;
        const size_t valueLength = std::strlen(value);
        const size_t suffixLength = std::strlen(suffix);
        return valueLength >= suffixLength
            && std::strcmp(value + valueLength - suffixLength, suffix) == 0;
    }

    // Streamer Mode excludes Japanese-language variants, which use the "Jp"
    // resource-name suffix, from the randomizer's destination pool. The two
    // Music Randomization page filters carve out further categories the same
    // way: an excluded resource is neither a destination nor a randomized
    // source, so its vanilla placements keep playing untouched.
    static bool isResourceAllowed(const char* resourceName) {
        if (!startsWith(resourceName, "StmRsBgm"))
            return false;
        if (Randomizer::isStreamerModeEnabled() && endsWith(resourceName, "Jp"))
            return false;
        if (Randomizer::isMusic2DThemesDisabled() && MusicDB::isTwoDResource(resourceName))
            return false;
        if (Randomizer::isMusicExtraThemesDisabled() && MusicDB::isExtraResource(resourceName))
            return false;
        if (Randomizer::isMusicBossThemesDisabled() && MusicDB::isBossResource(resourceName))
            return false;
        const s32 themeId = MusicDB::themeIndexForResource(resourceName);
        if (themeId >= 0 && !Randomizer::isMusicThemeEnabled(themeId))
            return false;
        return true;
    }

    static bool hasResource(const char* const* resources, s32 count, const char* resourceName) {
        for (s32 i = 0; i < count; i++)
            if (isEqual(resources[i], resourceName))
                return true;
        return false;
    }

    static void addResource(const char** resources, s32& count, s32 maxCount, const char* resourceName) {
        if (!resourceName || count >= maxCount || hasResource(resources, count, resourceName))
            return;
        resources[count++] = resourceName;
    }

    static void tryAddResource(const char* resourceName) {
        if (isResourceAllowed(resourceName))
            addResource(sAllResources, sAllResourceCount, kMaxAllResources, resourceName);
    }

    static void shuffle(s32* remap, s32 count, Randomizer::Rng& rng) {
        for (s32 i = 0; i < count; i++)
            remap[i] = i;
        for (s32 i = count - 1; i > 0; i--) {
            s32 j = (s32)rng.nextModulo((u32)(i + 1));
            s32 tmp = remap[i];
            remap[i] = remap[j];
            remap[j] = tmp;
        }
    }

    static void derange(s32* remap, s32 count) {
        if (count < 2)
            return;

        bool hasFixedPoint = true;
        for (s32 pass = 0; pass < count && hasFixedPoint; pass++) {
            hasFixedPoint = false;
            for (s32 i = 0; i < count; i++) {
                if (remap[i] != i)
                    continue;

                const s32 swapIndex = (i + 1) % count;
                const s32 tmp = remap[i];
                remap[i] = remap[swapIndex];
                remap[swapIndex] = tmp;
                hasFixedPoint = true;
            }
        }
    }

    static void shuffleNoRepeats(s32* remap, s32 count, Randomizer::Rng& rng) {
        shuffle(remap, count, rng);
        derange(remap, count);
    }

    static const char* remapFromPool(
        const char* resourceName, const char* const* resources, const s32* remap, s32 count) {
        for (s32 i = 0; i < count; i++)
            if (isEqual(resources[i], resourceName))
                return resources[remap[i]];
        return resourceName;
    }

    void buildRemapTable(u32 seed) {
        const bool streamerModeEnabled = Randomizer::isStreamerModeEnabled();
        const bool twoDDisabled = Randomizer::isMusic2DThemesDisabled();
        const bool extraDisabled = Randomizer::isMusicExtraThemesDisabled();
        const bool bossDisabled = Randomizer::isMusicBossThemesDisabled();
        const u64 themeMask = Randomizer::getMusicThemeMask();
        if (sInitialized && seed == sLastSeed && streamerModeEnabled == sLastStreamerModeEnabled
            && twoDDisabled == sLast2DThemesDisabled && extraDisabled == sLastExtraThemesDisabled
            && bossDisabled == sLastBossThemesDisabled && themeMask == sLastThemeMask)
            return;

        sAllResourceCount = 0;

        // Every track in the database is shuffled into one unified pool, so any
        // track can play anywhere (world themes, sub-stage themes, one-offs, boss).
        for (s32 i = 0; i < MusicDB::sWorldMainTableSize; i++) {
            const MusicDB::WorldMainEntry& entry = MusicDB::sWorldMainTable[i];
            if ((entry.flags & MusicDB::MusicFlag_NeverShuffle) == 0)
                tryAddResource(entry.resourceName);
        }

        for (s32 i = 0; i < MusicDB::sSubStageTableSize; i++) {
            const MusicDB::SubStageEntry& entry = MusicDB::sSubStageTable[i];
            if ((entry.flags & MusicDB::MusicFlag_NeverShuffle) == 0)
                tryAddResource(entry.resourceName);
        }

        for (s32 i = 0; i < MusicDB::sKnownResourceTableSize; i++)
            tryAddResource(MusicDB::sKnownResourceTable[i]);

        for (s32 i = 0; i < MusicDB::sBossResourceTableSize; i++)
            tryAddResource(MusicDB::sBossResourceTable[i]);

        Randomizer::Rng rng = Randomizer::makeRng(seed, Randomizer::RngStream::Music);
        shuffleNoRepeats(sAllRemap, sAllResourceCount, rng);

        // Extended mode derives each area's assignment from this salt and the
        // stage/source key. Reset it from the seed whenever the table is rebuilt
        // so the mapping is stable across sessions for a given seed.
        sAreaRandomSalt = seed ^ 0xA4EAA4E1u;
        sLastSeed = seed;
        sLastStreamerModeEnabled = streamerModeEnabled;
        sLast2DThemesDisabled = twoDDisabled;
        sLastExtraThemesDisabled = extraDisabled;
        sLastBossThemesDisabled = bossDisabled;
        sLastThemeMask = themeMask;
        sInitialized = true;
        Randomizer::logPush("Music built: %d tracks streamer=%d no2D=%d noExtra=%d noBoss=%d themes=%08x",
            sAllResourceCount, streamerModeEnabled, twoDDisabled, extraDisabled, bossDisabled,
            static_cast<u32>(themeMask));
    }

    bool reshuffle() {
        if (!sInitialized || sAllResourceCount < 2)
            return false;

        const u64 tick = nn::os::GetSystemTick().value;
        sRuntimeRandomState = sRuntimeRandomState * 1664525u + 1013904223u;
        sRuntimeRandomState ^= static_cast<u32>(tick)
            + static_cast<u32>(tick >> 32) * 2654435761u;

        Randomizer::Rng rng = Randomizer::makeRng(sRuntimeRandomState, Randomizer::RngStream::Music);
        shuffleNoRepeats(sAllRemap, sAllResourceCount, rng);
        sAreaRandomSalt = sRuntimeRandomState ^ 0xA4EAA4E1u;
        Randomizer::logPush("Music reshuffled: %d tracks", sAllResourceCount);
        return true;
    }

    void clear() {
        sInitialized = false;
        sLastSeed = 0xFFFFFFFF;
        sLastStreamerModeEnabled = false;
        sLast2DThemesDisabled = false;
        sLastExtraThemesDisabled = false;
        sLastBossThemesDisabled = false;
        sLastThemeMask = ~static_cast<u64>(0);
        sAllResourceCount = 0;
        sAreaRandomSalt = 0;
    }

    bool isInitialized() {
        return sInitialized;
    }

    const MusicDB::WorldMainEntry* findWorldMainForStage(const char* stageName, s32 scenarioNo) {
        if (!stageName)
            return nullptr;

        if (const MusicDB::WorldMainEntry* exact = MusicDB::findWorldMain(stageName, scenarioNo))
            return exact;
        if (scenarioNo > 0) {
            if (const MusicDB::WorldMainEntry* previous = MusicDB::findWorldMain(stageName, scenarioNo - 1))
                return previous;
        }
        if (scenarioNo != 0)
            return MusicDB::findWorldMain(stageName, 0);
        return nullptr;
    }

    const MusicDB::SubStageEntry* findSubStageForStage(const char* stageName) {
        return stageName ? MusicDB::findSubStage(stageName) : nullptr;
    }

    const char* remapResourceName(const char* playName, const char* resourceName) {
        if (!sInitialized || !playName || !resourceName)
            return resourceName;
        return remapFromPool(resourceName, sAllResources, sAllRemap, sAllResourceCount);
    }

    static u32 hashKeyPart(u32 hash, const char* value) {
        if (!value)
            return hash;

        for (const char* p = value; *p; p++) {
            hash ^= (u8)*p;
            hash *= 16777619u;
        }
        // Keep adjacent strings distinct (for example, "ab" + "c" versus
        // "a" + "bc") without allocating a combined key.
        hash ^= 0xFFu;
        hash *= 16777619u;
        return hash;
    }

    const char* remapResourceNameForStage(const char* stageName, const char* resourceName) {
        if (!sInitialized || !stageName || !stageName[0]
            || !resourceName || !resourceName[0] || sAllResourceCount <= 0)
            return resourceName;
        // Match Standard's source-pool boundary: excluded resources (notably
        // Streamer Mode's Japanese variants and non-stream/jingle resources)
        // remain vanilla rather than being promoted into Extended's pool.
        if (!hasResource(sAllResources, sAllResourceCount, resourceName))
            return resourceName;

        u32 hash = hashKeyPart(2166136261u, stageName);
        hash = hashKeyPart(hash, resourceName);
        Randomizer::Rng rng = Randomizer::makeRng(hash ^ sAreaRandomSalt, Randomizer::RngStream::Music);
        const s32 start = (s32)rng.nextModulo((u32)sAllResourceCount);

        // Use the current seeded/runtime shuffle as the destination ordering,
        // but choose an independent starting point for every stage/source key.
        // This permits two areas with the same vanilla cue to diverge while
        // still avoiding a self-map whenever another resource is available.
        for (s32 offset = 0; offset < sAllResourceCount; offset++) {
            const s32 poolIndex = (start + offset) % sAllResourceCount;
            const char* candidate = sAllResources[sAllRemap[poolIndex]];
            if (!isEqual(candidate, resourceName))
                return candidate;
        }

        return resourceName;
    }

    const char* pickRuntimeRandomResource() {
        if (!sInitialized || sAllResourceCount <= 0)
            return nullptr;

        const u64 tick = nn::os::GetSystemTick().value;
        sRuntimeRandomState = sRuntimeRandomState * 1664525u + 1013904223u;
        sRuntimeRandomState ^= static_cast<u32>(tick)
            + static_cast<u32>(tick >> 32) * 2654435761u;
        return sAllResources[sRuntimeRandomState % static_cast<u32>(sAllResourceCount)];
    }

    static u32 hashStageName(const char* stageName) {
        u32 hash = 2166136261u;
        for (const char* p = stageName; *p; p++) {
            hash ^= (u8)*p;
            hash *= 16777619u;
        }
        return hash;
    }

    const char* pickResourceForSilentStage(const char* stageName) {
        if (!sInitialized || !stageName || !stageName[0] || sAllResourceCount <= 0)
            return nullptr;
        return sAllResources[hashStageName(stageName) % (u32)sAllResourceCount];
    }

} // namespace MusicRando
