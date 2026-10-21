#pragma once

#include "Library/LiveActor/LiveActor.h"
#include "Player/IUsePlayerCollision.h"
#include "math/seadVector.h"

namespace rs {
    bool calcOnGroundNormalOrGravityDir(sead::Vector3f*, const al::LiveActor*, const IUsePlayerCollision*);
    bool isExistShineChipWatcher(const al::IUseSceneObjHolder*);
    int getShineChipCount(const al::IUseSceneObjHolder*);
    void setCounterAndDenominator(al::LayoutActor*, int, int);
} // namespace rs
