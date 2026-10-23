#pragma once

#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutUtil.h"
#include "al/Library/Nerve/NerveSetupUtil.h"

class PuppetActor;

class NameTag : public al::LayoutActor {
public:
    NameTag(PuppetActor*, const al::LayoutInitInfo&, float startDist, float endDist, const char* playerName);

    void appear(void) override;
    void control(void) override;
    void updateTrans(void);
    void update(void);
    void end(void);
    void setText(const char*);

    bool isNearPlayerActor(float) const;
    bool isVisible() const;

    const char* getCurrentState();

    void exeAppear(void);
    void exeWait(void);
    void exeEnd(void);
    void exeHide(void);

    PuppetActor* mPuppet = nullptr; // 0x130
    const char* mPaneName = nullptr; // 0x138
    float mStartDist = 0.0f; // 0x140
    float mEndDist = 0.0f; // 0x144
    float mNormalizedDist = 0.0f; // 0x148
};

namespace {
    NERVE_IMPL(NameTag, Appear)
    NERVE_IMPL(NameTag, Wait)
    NERVE_IMPL(NameTag, End)
    NERVE_IMPL(NameTag, Hide)

    NERVES_MAKE_STRUCT(NameTag, Appear, Wait, End, Hide)
} // namespace