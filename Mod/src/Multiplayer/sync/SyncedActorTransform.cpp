#include "Multiplayer/sync/SyncedActorTransform.h"

#include "al/Library/LiveActor/ActorPoseKeeper.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"

#include "Multiplayer/sync/SyncPayload.h"

void SyncedActorTransform::calc(al::Scene* scene) {
    SyncedActor::calc(scene);

    if (!mLiveActor || !mHasPendingRemote)
        return;

    mHasPendingRemote = false;

    // al::updatePoseQuat / setTrans rather than writing the trans field alone:
    // the capture work established that setting the field is not the same as
    // updating the pose the model matrix is built from, and cost several
    // rounds of a model rendering at the origin with a correct-looking pose
    // (PuppetActor::syncPose draws the same distinction).
    if (mLiveActor->mPoseKeeper)
        mLiveActor->mPoseKeeper->updatePoseQuat(mRemoteRotation);

    al::setTrans(mLiveActor, mRemotePosition);
}

void SyncedActorTransform::writeCalc(SyncPayload& payload) {
    payload.write(mPosition);
    payload.write(mRotation);
}

void SyncedActorTransform::readCalc(SyncPayload& payload) {
    payload.read(mRemotePosition);
    payload.read(mRemoteRotation);

    if (payload.isFailed())
        return;

    // Applied in calc(), not here - see the member comment.
    mHasPendingRemote = true;
}
