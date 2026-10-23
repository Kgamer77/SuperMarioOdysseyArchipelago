#include "Multiplayer/puppets/PuppetHolder.hpp"

#include "hk/diag/diag.h"

#include "sead/container/seadPtrArray.h"

#include "al/Library/LiveActor/ActorFlagFunction.h"

#include <math.h>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/actors/PuppetActor.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"

PuppetHolder::PuppetHolder(int size) {
    if (!mPuppetArr.tryAllocBuffer(size, gHeap)) {
        hk::diag::logLine("[PuppetHolder] ERROR: Buffer Alloc Failed on Puppet Holder!");
    } else {
        hk::diag::logLine("[PuppetHolder] Successfully allocated buffer for %d puppets", size);
    }
}

/**
 * @brief resizes puppet ptr array by creating a new ptr array and storing previous ptrs in it,
 * before freeing the previous array
 *
 * @param size the size of the new ptr array
 * @return returns true if resizing was successful
 */
bool PuppetHolder::resizeHolder(int size) {
    if (mPuppetArr.capacity() == size) {
        hk::diag::logLine("[PuppetHolder] No resize needed, already at capacity %d", size);
        return true; // no need to resize if we're already at the same capacity
    }

    if (!mPuppetArr.isBufferReady()) {
        bool result = mPuppetArr.tryAllocBuffer(size, gHeap);
        hk::diag::logLine("[PuppetHolder] Initial buffer allocation %s for size %d",
            result ? "succeeded" : "FAILED", size);
        return result;
    }

    sead::PtrArray<PuppetActor> newPuppets = sead::PtrArray<PuppetActor>();

    if (newPuppets.tryAllocBuffer(size, gHeap)) {
        int curPupCount = mPuppetArr.size();
        int copyCount = (curPupCount > size) ? size : curPupCount;

        hk::diag::logLine("[PuppetHolder] Resizing from %d to %d, copying %d puppets", mPuppetArr.capacity(),
            size, copyCount);

        for (int i = 0; i < copyCount; i++) {
            newPuppets.pushBack(mPuppetArr[i]);
        }

        mPuppetArr.freeBuffer();
        mPuppetArr = newPuppets;

        hk::diag::logLine("[PuppetHolder] Resize successful");
        return true;
    } else {
        hk::diag::logLine("[PuppetHolder] ERROR: Failed to allocate new buffer for resize");
        return false;
    }
}

bool PuppetHolder::tryRegisterPuppet(PuppetActor* puppet) {
    if (!mPuppetArr.isFull()) {
        mPuppetArr.pushBack(puppet);
        hk::diag::logLine("[PuppetHolder] Registered puppet %d/%d", mPuppetArr.size(), mPuppetArr.capacity());
        return true;
    } else {
        hk::diag::logLine("[PuppetHolder] ERROR: Cannot register puppet, holder is full (%d/%d)",
            mPuppetArr.size(), mPuppetArr.capacity());
        return false;
    }
}

void PuppetHolder::update() {
    PuppetStateGuard stateGuard;

    for (size_t i = 0; i < mPuppetArr.size(); i++) {
        PuppetActor* curPuppet = mPuppetArr[i];

        if (!curPuppet) {
            // hk::diag::logLine("[PuppetHolder] WARNING: Null puppet at index %zu", i);
            continue;
        }

        PuppetInfo* curInfo = curPuppet->getInfo();

        if (!curInfo) {
            // hk::diag::logLine("[PuppetHolder] WARNING: Null info for puppet at index %zu", i);
            continue;
        }

        bool wasInStage = curInfo->isInSameStage;
        curInfo->isInSameStage = checkInfoIsInStage(curInfo);

        // Stage visibility changes are rare and are the decisive evidence for
        // a "connected but cannot see each other" report. Persist the values
        // used by the gate instead of relying on a debug-only puppet flag.
        if (wasInStage != curInfo->isInSameStage) {
            MpDebugLog::write(
                "[PuppetHolder] %s visible=%d connected=%d localStage=%s remoteStage=%s localScenario=%d remoteScenario=%d localTeam=%d remoteTeam=%d",
                curInfo->puppetName.cstr(), curInfo->isInSameStage ? 1 : 0,
                curInfo->isConnected ? 1 : 0, mStageName.cstr(), curInfo->stageName.cstr(),
                mScenarioNo, curInfo->scenarioNo, Client::getTeam(),
                Client::getTeamOf(curInfo->playerID));
        }

        if (curInfo->isInSameStage && al::isDead(curPuppet)) {
            curPuppet->makeActorAlive();

            if (curPuppet->mIsDebug) {
                // hk::diag::logLine("[PuppetHolder] Puppet '%s' made alive (entered stage)",
                // curInfo->puppetName);
            }

            // curPuppet->emitJoinEffect();  //Poof Particles
        } else if (!curInfo->isInSameStage && !al::isDead(curPuppet)) {
            curPuppet->makeActorDead();

            if (curPuppet->mIsDebug) {
                // hk::diag::logLine("[PuppetHolder] Puppet '%s' made dead (left stage)",
                // curInfo->puppetName);
            }

            // curPuppet->emitJoinEffect(); //Poof Particles
        }
    }
}

bool PuppetHolder::checkInfoIsInStage(PuppetInfo* info) {
    if (!info) {
        return false;
    }

    // Belt-and-braces for the cross-team relay filter. The server already
    // refuses to send an opposing team's player packets while "Sync Other
    // Teams" is off, so in a healthy room this never fires - but a puppet
    // registered before a team change (or before the host flipped the setting)
    // would otherwise stay standing wherever its last position packet left it.
    if (!Client::isSyncOtherTeamsEnabled()
        && Client::getTeamOf(info->playerID) != Client::getTeam()) {
        return false;
    }

    // A PlayerConnect/GameInf pair can arrive before the first usable
    // PlayerInf.  Waking at that point used to feed PuppetInfo's uninitialized
    // rotation into every body/capture model.  Keep the puppet asleep at the
    // visibility gate until updatePlayerInfo has accepted a coherent pose.
    if (info->isConnected && info->hasPlayerPose) {
        // For scenarios 0-14 (main game scenarios), only check stage name
        if (info->scenarioNo < 15) {
            return al::isEqualString(mStageName.cstr(), info->stageName);
        } else {
            // For scenario 15+ (likely bonus/special stages), check both stage name and scenario
            return al::isEqualString(mStageName.cstr(), info->stageName) && info->scenarioNo == mScenarioNo;
        }
    }

    return false;
}

void PuppetHolder::setStageInfo(const char* stageName, u8 scenarioNo) {
    if (stageName) {
        mStageName = stageName;
        mScenarioNo = scenarioNo;
        // hk::diag::logLine("[PuppetHolder] Stage info updated: %s, Scenario %d", stageName, scenarioNo);
    } else {
        // hk::diag::logLine("[PuppetHolder] WARNING: Attempted to set null stage name");
    }
}
