#include "game/System/WorldList.h"

#include "Archipelago/Archipelago.h"
#include "helpers.hpp"

void Archipelago::isSubArea(GameDataHolderAccessor accessor, bool* isInSubArea, sead::FixedSafeString<32> stageId) {
    *isInSubArea = !GameDataFunction::isMainStage(accessor);

    // Handle Sub Area -> Sub Area connections
    if (*isInSubArea) {
        const char* subAreaExclusions[] = { "SnowWorldTown", "Underground000", "Special2WorldLavaStage", "Revenge" };
        for (int i = 0; i < sizeof(subAreaExclusions) / sizeof(subAreaExclusions[0]); i++) {
            if (isPartOf(GameDataFunction::getCurrentStageName(accessor), subAreaExclusions[i])) {
                // Stage Ids that lead into the sub area from an overworld
                // might need to add darker side transitions and dark side transitions
                // possibly re add Under01
                const char* excludedStageIds[] = { "SnowUG", "CP_Entrance", "MoonGoal", "MofumofuA", "BossMagmaA",
                    "BossKnuckleA", "BossForestA", "GiantWanderBossA", "BossRaidA" };
                *isInSubArea = false;
                for (int j = 0; j < sizeof(excludedStageIds) / sizeof(excludedStageIds[0]); j++) {
                    *isInSubArea = isPartOf(stageId.cstr(), excludedStageIds[j]);
                    if (*isInSubArea) {
                        break;
                    }
                }
                break;
            }
        }
    }
}

void Archipelago::getCustomStageId(GameDataHolderAccessor accessor, const ChangeStageInfo* info, sead::FixedSafeString<64>* stageId) {
    if (isPartOf(info->getStageName(), "LavaBonus") || isPartOf(GameDataFunction::getCurrentStageName(accessor), "LavaBonus")) {
        *stageId = "town_lava";
    }

    if (isPartOf(info->getStageName(), "LavaWorldShopStage") || isPartOf(GameDataFunction::getCurrentStageName(accessor), "LavaWorldShopStage")) {
        *stageId = "shop_lava";
    }

    if (isPartOf(stageId->cstr(), "aaa") && isPartOf(GameDataFunction::getCurrentStageName(accessor), "SandWorldHome")) {
        *stageId = "aaaSand";
    }

    if ((!isPartOf(stageId->cstr(), "A") && !isPartOf(stageId->cstr(), "B")) && (isPartOf(info->getStageName(), "Revenge") || isPartOf(GameDataFunction::getCurrentStageName(accessor), "Picture"))) {
        *stageId = "PictureBoss";
        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Knuckle"))
            stageId->append("Knuckle");

        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Mofumofu"))
            *stageId = "PictureMofumofu";

        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Forest"))
            stageId->append("Forest");

        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Wander"))
            *stageId = "PictureGiantWanderBoss";

        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Raid"))
            stageId->append("Raid");

        if (isPartOf(GameDataFunction::getCurrentStageName(accessor), "Magma"))
            stageId->append("Magma");
    }
}

void Archipelago::correctCustomStageId(sead::FixedSafeString<64>* toStageId) {
    if (al::isEqualString(toStageId->cstr(), "town_lava")) {
        *toStageId = "town";
    }

    if (al::isEqualString(toStageId->cstr(), "shop_lava")) {
        *toStageId = "shop";
    }

    if (al::isEqualString(toStageId->cstr(), "aaaSand")) {
        *toStageId = "aaa";
    }
}

ChangeStageInfo* Archipelago::handleER(const ChangeStageInfo* info) {
    if (!sIsEntranceRandomizationEnabled) {
        setRelativeWorldCoinCollect(info->getStageName());
        return nullptr;
    }

    GameDataHolderAccessor accessor(sCurScene);

    sead::FixedSafeString<64> stageId = sead::FixedSafeString<64>();
    stageId = info->getChangeStageId();

    sLastExitStageId = stageId.cstr();
    sLastExitStageName = GameDataFunction::getCurrentStageName(accessor);

    // Gets custom stageIds added by Archipelago based on kingdom to prevent duplicates
    getCustomStageId(accessor, info, &stageId);

    // if (mGoal == GameDataFunction::getWorldIndexMoon() && al::isEqualString(stageId.cstr(),
    // "aaa") &&
    //     !accessor.mData->getGameDataFile()->getGameProgressData()->mIsUnlockWorld[GameDataFunction::getWorldIndexMoon()])

    //     ChangeStageInfo toInfo(accessor.mData, "aaa", "MoonWorldHomeStage", false, -1,
    //     static_cast<ChangeStageInfo::SubScenarioType>(0)); toInfo.mPlacementString =
    //     info->getPlacementString(); return &toInfo;
    // }

    int stageIndex = getIndexStageNameList(info->getStageName());
    int stageIdIndex = getIndexStageIdList(stageId.cstr());
    bool isInSubArea = false;
    isSubArea(accessor, &isInSubArea, stageId);

    sead::FixedSafeString<64> toStageId = sead::FixedSafeString<64>();
    sead::FixedSafeString<64> toStageName = sead::FixedSafeString<64>();

    sead::FixedSafeString<128> foo = sead::FixedSafeString<128>();
    foo = "";
    foo.append(info->getStageName());
    foo.append(", ");
    foo.append(info->getChangeStageId());
    foo.append(", ");
    foo.append(info->getPlacementString());

    if (stageIdIndex < 0) {
        foo = "";
        foo.append("Error, Invalid Stage ID ");
        foo.append(stageId.cstr());
        sLastERStageId = stageId.cstr();

        setRelativeWorldCoinCollect(info->getStageName());
        return nullptr;
    }

    // Handle Sub area entrances in sub areas (i.e Shiveria)
    // Might fail on snow sub areas ammend with list of strictly effects maps
    stageConnection currentStageConnection;
    if (isInSubArea) {
        // Access Sub World conenctions
        currentStageConnection = sSubAreaStageConnections[stageIdIndex];
    } else {
        // Access Over World connections
        currentStageConnection = sOverworldStageConnections[stageIdIndex];
    }

    toStageId = stageIdList[currentStageConnection.toStageIdIndex];
    toStageName = stageNameList[currentStageConnection.toStageNameIndex];
    // Corrects custom stageIds added by Archipelago back to the base game stageId
    // Prevents need to change stageIds in game files.
    correctCustomStageId(&toStageId);

    // Add GetSubAreaScenario function for scenario dependent sub areas
    // like top hat tower and wooded boss arena, and deep woods
    int toScenario = 1;
    bool isHomeStage = isPartOf(toStageName.cstr(), "WorldHomeStage");
    if (isHomeStage) {
        int worldID = accessor.mData->mWorldList->tryFindWorldIndexByStageName(toStageName.cstr());
        toScenario = isMoonRockScenario(worldID);
    } else
        toScenario = getSubAreaScenario(toStageName.cstr());

    setRelativeWorldCoinCollect(toStageName.cstr());

    ChangeStageInfo toInfo(accessor.mData, toStageId.cstr(), toStageName.cstr(), false, toScenario, static_cast<ChangeStageInfo::SubScenarioType>(0));
    toInfo.mPlacementString = info->getPlacementString();
    foo = "";
    foo.append(toStageId);
    foo.append(", ");
    foo.append(toStageName);
    foo.append(", ");
    foo.append(toInfo.getPlacementString());

    sLastERStageId = toInfo.mChangeStageId.cstr();
    sLastERStageName = toInfo.mChangeStageName.cstr();

    return &toInfo;
    // return nullptr
}