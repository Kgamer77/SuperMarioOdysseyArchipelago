#pragma once

#include <basis/seadTypes.h>

namespace MusicDB {
    struct WorldMainEntry;
    struct SubStageEntry;
}

namespace MusicRando {

    void buildRemapTable(u32 seed);
    // Replaces the current fixed remap with a new runtime-random shuffle while
    // keeping the same resource pool and randomizer seed.
    bool reshuffle();
    void clear();
    bool isInitialized();

    const MusicDB::WorldMainEntry* findWorldMainForStage(const char* stageName, s32 scenarioNo);
    const MusicDB::SubStageEntry* findSubStageForStage(const char* stageName);
    const char* remapResourceName(const char* playName, const char* resourceName);
    const char* getTrackDisplayName(const char* resourceName);

    // Select a deterministic replacement for a physical stage and vanilla
    // source cue. Unlike remapResourceName(), different stages can receive
    // different replacements even when they share the same source resource.
    const char* remapResourceNameForStage(const char* stageName, const char* resourceName);

    // Select a track from the shuffled pool using runtime entropy. Used by Max
    // mode instead of the fixed seed remap.
    const char* pickRuntimeRandomResource();

    // Stages with no BGM defined at all in vanilla (many "Ex" challenge rooms)
    // never request a resource to remap. Pick one deterministically by stage
    // name so the silence is filled with a shuffled track instead.
    const char* pickResourceForSilentStage(const char* stageName);

} // namespace MusicRando
