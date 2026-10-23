#include "speedboot/HakoniwaSequenceSpeedboot.hpp"

#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Play/Layout/WipeHolder.h"
#include "al/Library/Thread/AsyncFunctorThread.h"

#include "game/Sequence/HakoniwaSequence.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "game/System/GameDataFunction.h"
#include "game/System/WorldList.h"

namespace speedboot {
    // Constructor
    HakoniwaSequenceSpeedboot::HakoniwaSequenceSpeedboot(HakoniwaSequence* sequence)
        : al::NerveStateBase("Speedboot")
        , mSequence(sequence) {
        initNerve(&NrvHakoniwaSequenceSpeedboot.LoadStage, 0);
    }

    // Initialize loading thread
    void HakoniwaSequenceSpeedboot::exeInitThread() {
        if (al::isFirstStep(this)) {
            mSequence->mInitThread->start();
        }

        if (mSequence->mInitThread->isDone()) {
            al::setNerve(this, &NrvHakoniwaSequenceSpeedboot.LoadStage);
        }
    }

    // Load stage resources
    void HakoniwaSequenceSpeedboot::exeLoadStage() {
        if (al::isFirstStep(this)) {
            mSequence->mInitThread->start();

            // Get stage name from game data - mGameDataHolder is an accessor (not a pointer)
            const char* stageName = GameDataFunction::getNextStageName(mSequence->mGameDataHolderAccessor);
            if (!stageName) {
                stageName = GameDataFunction::getMainStageName(mSequence->mGameDataHolderAccessor, 0);
            }

            // Calculate scenario number
            s32 scenario = GameDataFunction::calcNextScenarioNo(mSequence->mGameDataHolderAccessor);
            if (scenario == -1) {
                scenario = 1;
            }

            // Request world resources - use dot notation since mGameDataHolder is not a pointer
            s32 worldIndex = mSequence->mGameDataHolderAccessor.mData->mWorldList->tryFindWorldIndexByStageName(stageName);
            if (worldIndex > -1) {
                mSequence->mResourceLoader->requestLoadWorldHomeStageResource(worldIndex, scenario);
            }
        }

        if (isDoneLoading()) {
            al::setNerve(this, &NrvHakoniwaSequenceSpeedboot.WipeToKill);
        }
    }

    // Fade to black and complete speedboot
    void HakoniwaSequenceSpeedboot::exeWipeToKill() {
        if (al::isFirstStep(this)) {
            mSequence->mWipeHolder->startClose("FadeBlack", -1);
        }

        if (mSequence->mWipeHolder->isCloseEnd()) {
            kill();
        }
    }

    // Check if loading is complete
    bool HakoniwaSequenceSpeedboot::isDoneLoading() const {
        return mSequence->mResourceLoader->isEndLoadWorldResource() && mSequence->mInitThread->isDone();
    }

} // namespace speedboot