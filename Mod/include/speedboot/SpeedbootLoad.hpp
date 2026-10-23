#pragma once

#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Play/Layout/WipeSimple.h"

#include "Keyboard.hpp"
#include "Library/Nerve/NerveSetupUtil.h"

namespace al {
    class LayoutInitInfo;
}

class WorldResourceLoader;
class HakoniwaSequence;

class SpeedbootLoad : public al::LayoutActor {
public:
    SpeedbootLoad(WorldResourceLoader* resourceLoader, const al::LayoutInitInfo& initInfo, HakoniwaSequence* sequence, float autoCloseAfter);

    void exeAppear();
    void exeWait();
    void exeDecrease();
    void exeEnd();

private:
    const char* getStageName() const;
    const char16_t* getKingdomName(const char* stageName) const;
    bool isKnownStage(const char* stageName) const;

    void updateProgressBar();
    void updateUIElements();
    void updateTextElements();

    WorldResourceLoader* worldResourceLoader;
    HakoniwaSequence* mSequence;
    al::WipeSimple* wipe;
    al::WipeSimple* wipeSkip;

    f32 mProgression = 0.0f;
    f32 mRotTime = 0.0f;
    f32 mFallbackTimer = 0.0f;
    bool mUsingFallback = false;
};

namespace {
    NERVE_IMPL(SpeedbootLoad, Appear)
    NERVE_IMPL(SpeedbootLoad, Wait)
    NERVE_IMPL(SpeedbootLoad, Decrease)
    NERVE_IMPL(SpeedbootLoad, End)

    NERVES_MAKE_STRUCT(SpeedbootLoad, Appear, Wait, Decrease, End)
} // namespace