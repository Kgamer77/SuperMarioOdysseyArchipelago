#pragma once

#include <basis/seadTypes.h>

class GameDataFile;

// Fixes story-progress state that randomized moon credit and out-of-order
// kingdom visits would otherwise leave inconsistent.
namespace StoryProgressHooks {
    // Returns the internal main-quest number that should currently be active for
    // a logical kingdom. Under moon shuffle this is derived from completed
    // physical story locations; the vanilla MainScenarioNo can be contaminated
    // by finding that kingdom's story-moon item at a different location.
    s32 resolveMainScenarioNo(GameDataFile* file, s32 logicalWorldId);

    // Rebuild the saved MainScenarioNo mirrors from physical story progress.
    // Call before stage actor/quest initialization and after moon collection.
    void healAllMainScenarios(GameDataFile* file);

    void installHooks();
}
