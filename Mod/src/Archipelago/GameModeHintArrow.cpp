#include "Archipelago/GameModeHintArrow.h"

#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorInitUtil.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Player/PlayerUtil.h"

#include "game/Player/PlayerHackKeeper.h"

// #include "../src/Scene/Twists/SmallMario/smallMarioHooks.hpp"
#include "math/seadQuat.h"
#include "math/seadVector.h"
// #include "Scene/Twists/char* name) : al::LiveActor(name) {}

void GameModeHintArrow::init(const al::ActorInitInfo& info) {
    al::initActorWithArchiveName(this, info, "GameModeHintArrow", nullptr);
    al::initNerve(this, &NrvGameModeHintArrow.Wait, 0);
    al::invalidateClipping(this);

    makeActorAlive();
}

void GameModeHintArrow::initAfterPlacement(void) {
    al::LiveActor::initAfterPlacement();

    mPlayer = (PlayerActorBase*)al::tryGetPlayerActor(mSceneInfo->playerHolder, 0);
    mTargetTrans = al::getTransPtr(mPlayer);
    mArrowTrans = al::getTransPtr(this);
    setupMaterials();
}

bool GameModeHintArrow::receiveMsg(const al::SensorMsg* message, al::HitSensor* source, al::HitSensor* target) {
    return false;
}

void GameModeHintArrow::attackSensor(al::HitSensor* target, al::HitSensor* source) {
    return;
}

void GameModeHintArrow::control(void) {
    al::LiveActor::control();
}

void GameModeHintArrow::appear() {
    al::LiveActor::appear();
    al::setNerve(this, &NrvGameModeHintArrow.Wait);
}

void GameModeHintArrow::end() {
    kill();
}

void GameModeHintArrow::exeWait() {
    if (al::isFirstStep(this))
        al::startAction(this, "Wait");

    if (!shouldBeVisible() || !mTargetTrans) {
        mSize = al::lerpValue(mSize, 0.f, 0.3f);
        al::setScaleAll(this, mSize);
        return;
    }

    *mArrowTrans = al::getTrans(mPlayer);
    mArrowTrans->y += 200.f;

    float targetSize = 1.0f;
    mSize = al::lerpValue(mSize, mIsActive && mIsVisible ? targetSize : 0.f, 0.2f);
    al::setScaleAll(this, mSize);

    if (!mIsActive)
        return;
    mVisibilityCooldown = al::clamp(mVisibilityCooldown - 1, 0, 255);

    if (mVisibilityCooldown == 0) {
        mDistance = al::calcDistance(this, *mTargetTrans);
        mIsVisible = mDistance > mMinDistance;

        if (mIsVisible != mWasVisible) {
            mWasVisible = mIsVisible;
            mVisibilityCooldown = 150;
        }
    }

    setupMaterials();

    if (mIsVisible) {
        sead::Vector3f direction = *mTargetTrans - *mArrowTrans;
        al::normalize(&direction);

        // Calculate a proper up vector that's perpendicular to the direction
        sead::Vector3f up;

        // If direction is nearly vertical, use a different reference
        if (fabsf(direction.y) > 0.99f) {
            // Use X-axis as reference when pointing up/down
            up = sead::Vector3f::ex;
        } else {
            // Use Y-axis as reference for other directions
            up = sead::Vector3f::ey;
        }

        // Calculate right vector (perpendicular to both direction and up)
        sead::Vector3f right = up.cross(direction);
        al::normalize(&right);

        // Recalculate up to be perpendicular to both direction and right
        up = direction.cross(right);
        al::normalize(&up);

        sead::Quatf newQuat;
        al::makeQuatUpFront(&newQuat, up, direction);

        al::setQuat(this, newQuat);
    }
}