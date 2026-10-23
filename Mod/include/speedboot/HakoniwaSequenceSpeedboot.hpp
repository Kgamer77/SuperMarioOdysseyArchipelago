#pragma once

#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveStateBase.h"

#include "game/Sequence/HakoniwaSequence.h"

namespace speedboot {
    class HakoniwaSequenceSpeedboot : public al::NerveStateBase {
    public:
        explicit HakoniwaSequenceSpeedboot(HakoniwaSequence* sequence);

        // Nerve execution functions
        void exeInitThread();
        void exeLoadStage();
        void exeWipeToKill();

        /**
         * Check if both world resources and initialization thread are complete
         */
        bool isDoneLoading() const;

    private:
        class HakoniwaSequence* mSequence;
    };
    namespace {
        NERVE_IMPL(HakoniwaSequenceSpeedboot, InitThread);
        NERVE_IMPL(HakoniwaSequenceSpeedboot, LoadStage);
        NERVE_IMPL(HakoniwaSequenceSpeedboot, WipeToKill);

        NERVES_MAKE_STRUCT(HakoniwaSequenceSpeedboot, InitThread, LoadStage, WipeToKill)
    } // namespace
} // namespace speedboot