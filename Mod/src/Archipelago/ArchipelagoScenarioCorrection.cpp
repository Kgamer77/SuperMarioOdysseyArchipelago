#include "al/Library/Base/StringUtil.h"

#include "game/System/GameDataHolderAccessor.h"
#include "game/System/WorldList.h"

#include "Archipelago/Archipelago.h"

using namespace Archipelago;

void Archipelago::setScenario(int worldID, int scenario) {
    GameDataHolderAccessor accessor(sCurScene);

    if (scenario <= accessor.mData->mWorldList->getMoonRockScenarioNo(worldID))
        sWorldScenarios[worldID] = scenario;
}

bool Archipelago::setScenario(const char* worldName, int scenario) {
    GameDataHolderAccessor accessor(sCurScene);

    int worldID = accessor.mData->mWorldList->tryFindWorldIndexByStageName(worldName);
    if (scenario == -1) {
        // Client::addMessage("ChangeStageInfo failed to init");
    }

    // Exclude revisitable scenarios like festival
    if (!(al::isEqualString(worldName, "CityWorldHomeStage") && scenario == 3)) {
        if (scenario != getScenario(worldID) && !GameDataFunction::isUnlockedWorld(accessor, worldID)) {
            if (getScenario(worldID) < scenario) {
                // Client::addMessage("Scenario Updated");
                setScenario(worldID, scenario);
            }
            return true;
        }
    }
    return false;
}

int Archipelago::getScenario(const char* worldName) {
    GameDataHolderAccessor accessor(sCurScene);

    int worldID = accessor.mData->mWorldList->tryFindWorldIndexByStageName(worldName);

    /*if (worldScenarios[worldID] < GameDataFunction::getWorldScenarioNo(accessor, worldID))
    {
        setScenario(worldID, GameDataFunction::getWorldScenarioNo(accessor, worldID));
    }*/
    return sWorldScenarios[worldID];
}

int Archipelago::getScenario(int worldID) {
    return sWorldScenarios[worldID];
}

void Archipelago::sendCorrectScenario(const ChangeStageInfo* stageInfo) {
    GameDataHolderWriter writer(sCurScene);
    // try changing isReturn (param_4)
    /*if (stageInfo->isReturn)
    {
        Client::addMessage("isReturn: True");

    } else {
        Client::addMessage("isReturn: False");
    }
    sead::FixedSafeString<40> str;
    str = "";
    str.append("subScenario type: ");
    str.append(static_cast<char>(48 + static_cast<unsigned int>(stageInfo->subType)));
    Client::addMessage(str.cstr());*/
    ChangeStageInfo info(writer.mData, stageInfo->mChangeStageId.cstr(), stageInfo->mChangeStageName.cstr(), false,
        getScenario(stageInfo->mChangeStageName.cstr()), static_cast<ChangeStageInfo::SubScenarioType>(0));
    GameDataFunction::tryChangeNextStage(writer, &info);
}

void Archipelago::calculateShineScenarios() {
    for (int i = 0; i < 14; i++) {
        if (hasShine(shineScenarios[i].shineUid)) {
            setScenario(shineScenarios[i].worldId, shineScenarios[i].scenario);
        }
    }
}

int Archipelago::isMoonRockScenario(int worldId) {
    GameDataHolderAccessor accessor(sCurScene);
    int curScenario = getScenario(worldId);
    int preMoonRockScenario = accessor.mData->mWorldList->getAfterEndingScenarioNo(worldId);
    if (curScenario >= preMoonRockScenario)
        return curScenario;
    bool isMoonRock = true;
    if (worldId < GameDataFunction::getWorldIndexPeach()) {
        for (int i = 0; i < storyLengths[worldId]; i++) {
            short shineId = storyShines[worldId][i];
            if (shineId > 0 && !hasShine(shineId)) {
                isMoonRock = false;
                break;
            }
            if (shineId == -1) {
                if (curScenario < 2 && !GameDataFunction::isUnlockedWorld(accessor, worldId)) {
                    curScenario = 2;
                    setScenario(worldId, curScenario);
                } else {
                    curScenario = 1;
                    setScenario(worldId, curScenario);
                }
                isMoonRock = getDefeatedBowserCloud();
            }
            if (shineId == -2) {
            }
        }
    }
    if (isMoonRock) {
        setScenario(worldId, preMoonRockScenario);
        return preMoonRockScenario;
    }
    return curScenario;
}

int Archipelago::getSubAreaScenario(const char* toStageName) {
    int toScenario = -1;
    if (al::isEqualString(toStageName, "ForestWorldBossStage"))
        toScenario = getScenario(GameDataFunction::getWorldIndexForest()) > 2 ? 2 : 1;

    if (al::isEqualString(toStageName, "ForestWorldWoodsStage"))
        toScenario = getScenario(GameDataFunction::getWorldIndexForest()) > 1 ? 2 : 1;

    if (al::isEqualString(toStageName, "CapWorldTowerStage"))
        toScenario = 2;

    return toScenario;
}