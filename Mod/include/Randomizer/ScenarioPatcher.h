#pragma once

#include <basis/seadTypes.h>

class ChangeStageInfo;
class GameDataFile;
class GameDataHolder;
class GameDataHolderAccessor;

namespace al {
    class Scene;
}

namespace ScenarioPatcher {

    s32 getRequiredScenarioForRandomizedMultiPrizeSubarea(const char* stageName);
    s32 normalizeScenarioForStage(GameDataHolderAccessor accessor, const char* stageName, s32 requestedScenario, bool isKfr);
    void normalizeChangeStageInfo(GameDataFile* file, ChangeStageInfo* info, s32 raceType);
    void beforeStageSceneInit(const char* stageName, GameDataHolder* holder);
    void afterStageSceneInit(al::Scene* scene);
    void clearPendingRestart();
    // Writes the seed-selected initial scenario for every kingdom into a fresh
    // GameDataFile before the first stage is prepared.
    void applyInitialKingdomStates(GameDataFile* file);
    void installHooks();
    void healAllScenarios(GameDataFile* file);

} // namespace ScenarioPatcher
