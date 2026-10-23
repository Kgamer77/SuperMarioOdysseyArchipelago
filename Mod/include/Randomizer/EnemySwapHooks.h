#pragma once

#include "Randomizer/FeatureFlags.h"

class StageScene;

namespace EnemySwapHooks {
#if SMOR_FEATURE_CHAOS_ANY
    void installHooks();
    void resetStageState();
    // Called from the existing StageScene::control hook once per frame. This
    // is the stage-wide tick for deferred replacement-actor lifecycle work.
    void update(StageScene* scene);
#else
    inline void installHooks() { }
    inline void resetStageState() { }
    inline void update(StageScene*) { }
#endif
} // namespace EnemySwapHooks
