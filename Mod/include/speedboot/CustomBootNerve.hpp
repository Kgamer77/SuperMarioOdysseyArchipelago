#pragma once

#include "al/Library/Nerve/Nerve.h"
#include "al/Library/Nerve/NerveKeeper.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/Sequence/HakoniwaSequence.h"

namespace {
    NERVE_IMPL(HakoniwaSequence, LoadStage);
    NERVE_IMPL(HakoniwaSequence, LoadWorldResourceWithBoot);

    NERVES_MAKE_STRUCT(HakoniwaSequence, LoadStage, LoadWorldResourceWithBoot);
} // namespace
namespace speedboot {
    class CustomBootNerve : public al::Nerve {
    public:
        void execute(al::NerveKeeper* keeper) const {
            if (al::updateNerveState(keeper->mParent)) {
                al::setNerve(keeper->mParent, &NrvHakoniwaSequence.LoadStage);
            }
        }
    };
} // namespace speedboot