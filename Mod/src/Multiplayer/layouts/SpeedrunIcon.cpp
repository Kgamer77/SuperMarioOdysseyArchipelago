#include "Multiplayer/layouts/SpeedrunIcon.h"

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolderAccessor.h"

#include "Multiplayer/server/Client.hpp"
#include "Scene/StageSceneStateModConfig.hpp"

SpeedrunIcon* SpeedrunIcon::sInstance = nullptr;

SpeedrunIcon::SpeedrunIcon(const char* name, const al::LayoutInitInfo& initInfo)
    : al::LayoutActor(name) {
    al::initLayoutActor(this, initInfo, "SpeedrunIcon", 0);

    al::hidePane(this, "TxtNonstop");
    al::setPaneStringFormat(this, "TxtNonstop", "Non-Stop");

    initNerve(&NrvSpeedrunIcon.End, 0);

    kill();
}

void SpeedrunIcon::appear() {
    al::startAction(this, "Appear", 0);
    al::setNerve(this, &NrvSpeedrunIcon.Appear);
    al::LayoutActor::appear();
}

bool SpeedrunIcon::tryEnd() {
    if (!al::isNerve(this, &NrvSpeedrunIcon.End)) {
        al::setNerve(this, &NrvSpeedrunIcon.End);
        return true;
    }
    return false;
}

bool SpeedrunIcon::tryStart() {
    if (!al::isNerve(this, &NrvSpeedrunIcon.Wait) && !al::isNerve(this, &NrvSpeedrunIcon.Appear)) {
        appear();
        return true;
    }
    return false;
}

void SpeedrunIcon::exeAppear() {
    if (al::isActionEnd(this, 0)) {
        al::setNerve(this, &NrvSpeedrunIcon.Wait);
    }
}

void SpeedrunIcon::exeWait() {
    if (al::isFirstStep(this)) {
        al::startAction(this, "Wait", 0);
    }

    updateSpeedrunText();
    updateShineCount();

    al::hidePane(this, "TxtVersion");

    al::showPane(this, "ShineCount");
    al::showPane(this, "ShineIcon");
    // al::hidePane(this, "ShineCount");
    // al::hidePane(this, "ShineIcon");
}

void SpeedrunIcon::exeEnd() {
    if (al::isFirstStep(this)) {
        al::startAction(this, "End", 0);
    }

    if (al::isActionEnd(this, 0)) {
        kill();
    }
}

void SpeedrunIcon::updateSpeedrunText() {
    int playerCount = Client::getConnectCount() + 1; // include local player
    al::setPaneStringFormat(this, "TxtSpeedrun", "%dP Speedrun", playerCount);
}

void SpeedrunIcon::updateShineCount() {
    if (mGDataHolder)
        al::setPaneStringFormat(this, "ShineCount", "%03d",
            mGDataHolder->getGameDataFile()->getTotalUniqueShineNum());
}
