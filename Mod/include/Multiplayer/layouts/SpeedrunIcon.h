#pragma once

#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Nerve/NerveSetupUtil.h"

#include "game/Scene/StageScene.h"
#include "game/System/GameDataHolder.h"
// TODO: kill layout if going through loading zone or paused

class SpeedrunIcon : public al::LayoutActor {
public:
    SpeedrunIcon(const char* name, const al::LayoutInitInfo& initInfo);

    void appear() override;

    bool tryStart();
    bool tryEnd();

    void exeAppear();
    void exeWait();
    void exeEnd();
    void updateSpeedrunText();
    void updateShineCount();

    void setHolder(GameDataHolder* holder) { mGDataHolder = holder; }

    static SpeedrunIcon* sInstance;

private:
    GameDataHolder* mGDataHolder = nullptr;
};

namespace {
    NERVE_IMPL(SpeedrunIcon, Appear)
    NERVE_IMPL(SpeedrunIcon, Wait)
    NERVE_IMPL(SpeedrunIcon, End)

    NERVES_MAKE_STRUCT(SpeedrunIcon, Appear, Wait, End)
} // namespace