#pragma once

#include <math/seadVector.h>
#include <prim/seadSafeString.h>

#include "Library/Nerve/NerveExecutor.h"
// #include "game/Scene/StageSceneStateShineList.h"
#include "al/Library/Scene/Scene.h"

#include "game/Layout/ShineListLayout.h"

class SceneAudioSystemPauseController;
class StageSceneStateCollectionList {
public:
    al::Scene* mScene = nullptr; // 0x18
    ShineListLayout* mShineListLayout = nullptr;
    void* mHackListLayout = nullptr;
    void* mSouvenirListLayout = nullptr;
    void* mStageSceneStateStageMap = nullptr;
    void* mStageSceneStateCollectBgm = nullptr;
    void* mInputSeparator = nullptr;
    void* mCollectList = nullptr;
    void* mParCursor = nullptr;
    void* mMapFooter = nullptr;
    void* mParFooter = nullptr;
    void* mParRoll = nullptr;
    void** unkPtrArray = nullptr;
    void* mSceneAudioSystemPauseController = nullptr; // 0x98
    // void* mSimpleLayoutAppearWaitEnd = nullptr;
    // _94 = 0;
    // _90 = 0;
    // bool _96 = false;
    // SceneAudioSystemPauseController *_98 = nullptr;
};
