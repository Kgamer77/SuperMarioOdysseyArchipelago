#include "Multiplayer/actors/PuppetCapActor.h"

#include "sead/heap/seadHeapMgr.h"
#include "sead/math/seadQuat.h"
#include "sead/math/seadVectorFwd.h"

#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitFunction.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Project/HitSensor/HitSensor.h"

#include "game/Player/PlayerFunction.h"
#include "game/Util/SensorMsgFunction.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/puppets/LateModelDrawer.h"
#include "Multiplayer/server/Client.hpp"
#include "Scene/StageSceneStateModConfig.hpp"
#include "helpers.hpp"

PuppetCapActor::PuppetCapActor(const char* name)
    : al::LiveActor(name) { }

void PuppetCapActor::init(const al::ActorInitInfo& initInfo) {
    initCostume(initInfo, tryGetPuppetCapName(mInfo), false);
}

void PuppetCapActor::initCostume(const al::ActorInitInfo& initInfo,
    const char* costumeCapName, bool lateBuild) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    LateModelDrawer::ScopedLateBuild lateScope(lateBuild);

    if (lateBuild)
        LateModelDrawer::prepareForLateRegistration(initInfo, costumeCapName);

    const LateModelDrawer::DepthShadowCounts shadowCounts
        = lateBuild ? LateModelDrawer::snapshotDepthShadow(initInfo)
                    : LateModelDrawer::DepthShadowCounts {};

    sead::FixedSafeString<0x20> capModelName;

    PlayerFunction::createCapModelName(&capModelName,
        costumeCapName && costumeCapName[0] ? costumeCapName : "Mario");
    mCapModelName = capModelName;
    PlayerFunction::initCapModelActorDemo(this, initInfo, capModelName.cstr());

    initHitSensor(2);
    al::addHitSensor(this, initInfo, "Push", (u32)al::HitSensorType::MapObjSimple, 60.0f, 8,
        sead::Vector3f::zero);
    al::addHitSensor(this, initInfo, "Attack", (u32)al::HitSensorType::EnemyAttack, 300.0f, 8,
        sead::Vector3f::zero);

    al::hideSilhouetteModelIfShow(this);
    al::initExecutorModelUpdate(this, initInfo);

    mJointKeeper = new HackCapJointControlKeeper();
    mJointKeeper->initCapJointControl(this);

    al::ModelKeeper* modelKeeper = getModelKeeper();
    mHasModel = modelKeeper && modelKeeper->getModelCtrl();

    if (lateBuild) {
        // The cap initializer can create material-specific lists that did not
        // exist during the first preparation pass.  Grow once more, then
        // materialize the new actor's private tables before it can be woken.
        LateModelDrawer::prepareForLateRegistration(initInfo, capModelName.cstr());
        LateModelDrawer::createMissingTables(initInfo, this, capModelName.cstr());
        LateModelDrawer::restoreDepthShadow(initInfo, shadowCounts, capModelName.cstr());
    }

    if (!mHasModel)
        MpDebugLog::write("[PuppetCap %p] costume %s produced no model", this,
            costumeCapName ? costumeCapName : "(null)");

    makeActorDead();
}

void PuppetCapActor::initOnline(PuppetInfo* pupInfo) {
    mInfo = pupInfo;
}

void PuppetCapActor::control() {
    PuppetInfo frameInfo;
    {
        PuppetStateGuard stateGuard;
        if (!mInfo)
            return;
        frameInfo = *mInfo;
    }

    applySnapshot(frameInfo);
}

void PuppetCapActor::applySnapshot(const PuppetInfo& info) {
    if (!info.capAnim.isEmpty()) {
        startAction(info.capAnim.cstr());
    }

    al::setTrans(this, info.capPos);
    al::setQuat(this, info.capQuat);
    // Identity unless the cap is being worn on a capture, where the game
    // shrinks it to fit the enemy - see PuppetInfo::capScale.
    al::setScale(this, info.capScale);

    if (mJointKeeper) {
        mJointKeeper->mJointRot.x = info.capRot.x;
        mJointKeeper->mJointRot.y = info.capRot.y;
        mJointKeeper->mJointRot.z = info.capRot.z;
        mJointKeeper->mSkew = info.capRot.w;
    }
}

void PuppetCapActor::makeActorAlive() {
    al::LiveActor::makeActorAlive();
    al::showModel(this);

    // Late replacement caps use the same shared draw lists as late body and
    // capture models.  Verify after both native request phases have drained.
    mActivationCheckPending = true;
    mActivationCheckDelay = 2;
    mActivationCheckAttempts = 0;
}

void PuppetCapActor::update() {
    al::LiveActor::calcAnim();
    al::LiveActor::movement();
    updateActivationCheck();
}

void PuppetCapActor::updateActivationCheck() {
    if (!mActivationCheckPending)
        return;

    if (al::isDead(this)) {
        mActivationCheckPending = false;
        return;
    }

    if (mActivationCheckDelay > 0) {
        --mActivationCheckDelay;
        return;
    }

    const bool allowDrawRepair = mActivationCheckAttempts == 0;
    ++mActivationCheckAttempts;
    if (LateModelDrawer::verifyActivationAndRequestRepair(
            this, mCapModelName.cstr(), allowDrawRepair)) {
        mActivationCheckPending = false;
        return;
    }

    constexpr s32 cMaxActivationChecks = 3;
    if (mActivationCheckAttempts < cMaxActivationChecks) {
        mActivationCheckDelay = 2;
        return;
    }

    MpDebugLog::write("[Activation] %s: cap still incomplete after %d checks actor=%p",
        mCapModelName.cstr(), mActivationCheckAttempts, this);
    mActivationCheckPending = false;
}

void PuppetCapActor::attackSensor(al::HitSensor* sender, al::HitSensor* receiver) {
    return;

    // if (al::isSensorPlayer(receiver) && al::isSensorName(sender, "Push")) {
    //     rs::sendMsgPushToPlayer(receiver, sender);
    // }
}

bool PuppetCapActor::receiveMsg(const al::SensorMsg* msg, al::HitSensor* sender, al::HitSensor* receiver) {
    return false;

    // if (al::isMsgPlayerDisregard(msg)) {
    //     return true;
    // }
    //
    // if (rs::isMsgPlayerCapTouchJump(msg)) {
    //     return true;
    // }
    //
    // if (rs::isMsgPlayerCapTrample(msg)) {
    //     rs::requestHitReactionToAttacker(msg, receiver, al::getSensorPos(sender));
    //     return true;
    // }
}

void PuppetCapActor::startAction(const char* actName) {
    if (al::tryStartActionIfNotPlaying(this, actName)) {
        const char* curActName = al::getActionName(this);
        if (curActName) {
            if (al::isSklAnimExist(this, curActName)) {
                al::clearSklAnimInterpole(this);
            }
        }
    }
}
