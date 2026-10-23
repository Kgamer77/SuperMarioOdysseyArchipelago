#pragma once

#include <basis/seadTypes.h>

namespace SkyboxRando {

    void reinit();
    void clear();
    const char* remap(const char* stageName);
    const char* remapWithScenario(const char* stageName, s32* outScenario);

    // Sub-area skybox: any stage that is NOT a kingdom main stage gets its own
    // skybox, picked deterministically from the seed + the stage name (so it is
    // stable per sub-area without enumerating the sub-area list). Returns the
    // stage name unchanged for main stages or when uninitialized.
    const char* remapSubAreaWithScenario(const char* stageName, s32* outScenario);

} // namespace SkyboxRando