#pragma once

#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/LiveActor/ActorSceneInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/NerveSetupUtil.h"

#include "game/Player/PlayerActorBase.h"
#include "game/Player/PlayerActorHakoniwa.h"

#include <stdint.h>

class GameModeHintArrow : public al::LiveActor {
public:
    GameModeHintArrow(const char* name);
    void init(const al::ActorInitInfo&) override;
    void initAfterPlacement(void) override;
    bool receiveMsg(const al::SensorMsg* message, al::HitSensor* source, al::HitSensor* target) override;
    void attackSensor(al::HitSensor* source, al::HitSensor* target) override;
    void control(void) override;
    void appear() override;
    void end();

    void setTarget(sead::Vector3f* targetPosition) { mTargetTrans = targetPosition; }
    void setActive(bool isActive) { mIsActive = isActive; }

    void exeWait();

protected:
    virtual bool shouldBeVisible() = 0;
    virtual void setupMaterials() = 0;

    PlayerActorBase* mPlayer;
    sead::Vector3f* mTargetTrans;
    sead::Vector3f* mArrowTrans;
    sead::Vector3f mActorUp = sead::Vector3f::ey;

    const float mMinDistance = 520.f;
    float mDistance = -1.f;
    float mSize = 0.f;

    bool mIsActive = true;
    bool mIsVisible = false;
    bool mWasVisible = false;
    uint8_t mVisibilityCooldown = 0;
};

namespace {
    NERVE_IMPL(GameModeHintArrow, Wait)

    NERVES_MAKE_STRUCT(GameModeHintArrow, Wait)
} // namespace