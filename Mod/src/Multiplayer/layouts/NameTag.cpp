#include "Multiplayer/layouts/NameTag.h"

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Screen/ScreenFunction.h"

#include "Multiplayer/actors/PuppetActor.h"

NameTag::NameTag(PuppetActor* pupActor, const al::LayoutInitInfo& initInfo, float startDist, float endDist,
    const char* playerName)
    : al::LayoutActor("PNameTag")
    , mPuppet(pupActor)
    , mStartDist(startDist)
    , mEndDist(endDist) {
    al::initLayoutActor(this, initInfo, "BalloonSpeak", 0);

    mPaneName = "TxtMessage";

    al::setPaneStringFormat(this, mPaneName, playerName);

    initNerve(&NrvNameTag.Wait, 0);

    end();
}

void NameTag::appear() {
    if (!al::isNerve(this, &NrvNameTag.End) && !al::isNerve(this, &NrvNameTag.Hide) && mIsAlive) {
        LayoutActor::appear();
        al::startFreezeActionEnd(this, "End", 0);
        al::setNerve(this, &NrvNameTag.Hide);
        return;
    }

    if (!isNearPlayerActor(mStartDist)) {
        LayoutActor::appear();
        al::startFreezeActionEnd(this, "End", 0);
        al::setNerve(this, &NrvNameTag.Hide);
        return;
    }

    setText(mPuppet->getName());

    al::startAction(this, "Appear", 0);
    LayoutActor::appear();
    al::setActionFrameRate(this, 1.0, 0);
    al::setNerve(this, &NrvNameTag.Appear);
}

void NameTag::control() {
    update();

    al::LiveActor* puppetModel = mPuppet->getCurrentModel();

    if (!al::isNerve(this, &NrvNameTag.End) && !al::isNerve(this, &NrvNameTag.Hide) && (al::isClipped(puppetModel) || al::isDead(puppetModel))) {
        al::setNerve(this, &NrvNameTag.End);
    } else {
        updateTrans();
    }
}

void NameTag::updateTrans() {
    sead::Vector2f newTrans = sead::Vector2f::zero;

    sead::Vector3f targetOffset(0, 130, 0);

    al::LiveActor* puppetModel = mPuppet->getCurrentModel();

    al::calcLayoutPosFromWorldPos(&newTrans, puppetModel, al::getTrans(puppetModel) + targetOffset);

    al::setLocalTrans(this, newTrans);

    mNormalizedDist = 1 - al::normalize(al::calcDistance(puppetModel, al::getPlayerActor(puppetModel, 0)), 200.0f, mEndDist);

    al::setLocalScale(this, mNormalizedDist);
}

void NameTag::update() {
    if (al::isNerve(this, &NrvNameTag.End) || al::isNerve(this, &NrvNameTag.Hide) || !mIsAlive) {
        if (isNearPlayerActor(mStartDist)) {
            appear();
        }
    }

    if (!al::isNerve(this, &NrvNameTag.End) && !al::isNerve(this, &NrvNameTag.Hide) && mIsAlive) {
        if (!isNearPlayerActor(mEndDist)) {
            al::setNerve(this, &NrvNameTag.End);
        }
    }
}

void NameTag::end() {
    if (!al::isNerve(this, &NrvNameTag.End) && !al::isNerve(this, &NrvNameTag.Hide)) {
        al::setNerve(this, &NrvNameTag.End);
    }
}

void NameTag::setText(const char* text) {
    al::setPaneStringFormat(this, mPaneName, text);
}

bool NameTag::isNearPlayerActor(float dist) const {
    // Freeze tag specific checks for frozen
    if (mPuppet->getInfo()->isFreezeTagFreeze)
        return true;

    return al::isNearPlayer(mPuppet->getCurrentModel(), dist);
}

bool NameTag::isVisible() const {
    return isNearPlayerActor(mStartDist);
}

const char* NameTag::getCurrentState() {
    if (al::isNerve(this, &NrvNameTag.Appear)) {
        return "Appear";
    }
    if (al::isNerve(this, &NrvNameTag.Wait)) {
        return "Wait";
    }
    if (al::isNerve(this, &NrvNameTag.End)) {
        return "End";
    }
    if (al::isNerve(this, &NrvNameTag.Hide)) {
        return "Hide";
    }
    return "Unknown";
}

void NameTag::exeAppear(void) {
    if (al::isActionEnd(this, 0))
        al::setNerve(this, &NrvNameTag.Wait);
}
void NameTag::exeWait(void) {
    if (al::isFirstStep(this))
        al::startAction(this, "Wait", 0);
}
void NameTag::exeEnd(void) {
    if (al::isFirstStep(this))
        al::startAction(this, "End", 0);

    if (al::isActionEnd(this, 0))
        al::setNerve(this, &NrvNameTag.Hide);
}

void NameTag::exeHide(void) { }
