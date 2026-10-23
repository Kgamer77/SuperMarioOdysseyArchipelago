#pragma once

#include <basis/seadTypes.h>
#include <cstring>

namespace WorldMeta {

    enum WorldId : s32
    {
        WorldId_Cap = 0,
        WorldId_Cascade = 1,
        WorldId_Sand = 2,
        WorldId_Wooded = 3,
        WorldId_Lake = 4,
        WorldId_Cloud = 5,
        WorldId_Lost = 6,
        WorldId_Metro = 7,
        WorldId_Seaside = 8,
        WorldId_Snow = 9,
        WorldId_Luncheon = 10,
        WorldId_Ruined = 11,
        WorldId_Bowsers = 12,
        WorldId_Moon = 13,
        WorldId_Mushroom = 14,
        WorldId_Dark = 15,
        WorldId_Darker = 16,
    };

    struct Entry {
        s32 worldId;
        s32 scenarioWorldId;
        const char* developName;
        const char* displayName;
        const char16_t* displayName16;
        const char* mainStage;
        s32 skyboxScenario;
        const char* skyboxLabel;
        const char* moonArchive;
        const char* grandMoonArchive;
        s32 mclFrame;
        s32 vanillaMoonRequirement;
        const char* paintingPaneName;
        // Stock kingdom image shown on a save-file card, from the texture set in
        // ObjectData/TextureSaveData.bfres. Vanilla derives this at runtime
        // through WorldList::getWorldDevelopName + suffix probing
        // (GameDataFunction::makeTextureSaveDataFileName picks the scenario
        // variant: "…WorldMoonRock", "…WorldED", "…World00"); the rando's file
        // select has no scenario in its save metadata, so it uses the plain
        // base name, which exists for every kingdom. Note it is NOT the
        // mainStage prefix - Ruined Kingdom's BossRaidWorld stages carry
        // "AttackWorld" images.
        const char* saveThumbnail;
    };

    static constexpr s32 sWorldCount = 17;

    static constexpr Entry sWorlds[sWorldCount] = {
        { 0, 0, "Cap", "Cap Kingdom", u"Cap Kingdom", "CapWorldHomeStage", 4, "Cap (4)", "ShineCap", "ShineGrandCap", 0, 0, nullptr, "CapWorld" },
        { 1, 1, "Cascade", "Cascade Kingdom", u"Cascade Kingdom", "WaterfallWorldHomeStage", 4, "Cascade (4)", "ShineWaterfall", "ShineGrandWaterfall", 15, 5, "Cascade", "WaterfallWorld" },
        { 2, 2, "Sand", "Sand Kingdom", u"Sand Kingdom", "SandWorldHomeStage", 5, "Sand (5)", "ShineSand", "ShineGrandSand", 5, 16, "Sand", "SandWorld" },
        { 3, 4, "Wooded", "Wooded Kingdom", u"Wooded Kingdom", "ForestWorldHomeStage", 5, "Wooded (5)", "ShineForest", "ShineGrandForest", 2, 16, "Wooded", "ForestWorld" },
        { 4, 3, "Lake", "Lake Kingdom", u"Lake Kingdom", "LakeWorldHomeStage", 4, "Lake (4)", "ShineLake", "ShineGrandLake", 7, 8, "Lake", "LakeWorld" },
        { 5, 5, "Cloud", "Cloud Kingdom", u"Cloud Kingdom", "CloudWorldHomeStage", 4, "Cloud (4)", "ShineCloud", "ShineGrandCloud", 27, 0, nullptr, "CloudWorld" },
        { 6, 6, "Lost", "Lost Kingdom", u"Lost Kingdom", "ClashWorldHomeStage", 4, "Lost (4)", "ShineClash", "ShineGrandClash", 14, 10, nullptr, "ClashWorld" },
        { 7, 7, "Metro", "Metro Kingdom", u"Metro Kingdom", "CityWorldHomeStage", 8, "Metro (8)", "ShineCity", "ShineGrandCity", 1, 20, "Metro", "CityWorld" },
        { 8, 9, "Seaside", "Seaside Kingdom", u"Seaside Kingdom", "SeaWorldHomeStage", 4, "Seaside (4)", "ShineSea", "ShineGrandSea", 8, 10, "Seaside", "SeaWorld" },
        { 9, 8, "Snow", "Snow Kingdom", u"Snow Kingdom", "SnowWorldHomeStage", 4, "Snow (4)", "ShineSnow", "ShineGrandSnow", 4, 10, nullptr, "SnowWorld" },
        { 10, 10, "Luncheon", "Luncheon Kingdom", u"Luncheon Kingdom", "LavaWorldHomeStage", 4, "Luncheon (4)", "ShineLava", "ShineGrandLava", 6, 18, "Luncheon", "LavaWorld" },
        { 11, 11, "Ruined", "Ruined Kingdom", u"Ruined Kingdom", "BossRaidWorldHomeStage", 4, "Ruined (4)", "ShineBoss", "ShineGrandBossRaid", 17, 3, nullptr, "AttackWorld" },
        { 12, 12, "Bowsers", "Bowser's Kingdom", u"Bowser's Kingdom", "SkyWorldHomeStage", 4, "Bowser's (4)", "ShineSky", "ShineGrand", 3, 8, "Bowser", "SkyWorld" },
        { 13, 13, "Moon", "Moon Kingdom", u"Moon Kingdom", "MoonWorldHomeStage", 3, "Moon (3)", "ShineMoon", nullptr, 27, 0, nullptr, "MoonWorld" },
        { 14, 14, "Mushroom", "Mushroom Kingdom", u"Mushroom Kingdom", "PeachWorldHomeStage", 4, "Mushroom (4)", "PowerStar", "ShineGrandPeach", 0, 0, "Mushroom", "PeachWorld" },
        { 15, 15, "Dark", "Dark Side", u"Dark Side", "Special1WorldHomeStage", 4, "Dark (4)", "ShineSpecial1", "ShineGrandSpecial1", 9, 250, nullptr, "Special1World" },
        // Darker Side ships no moon archive of its own, so it borrows Dark Side's
        // rather than falling through to the generic ShineMoon/ShineGrand.
        { 16, 16, "Darker", "Darker Side", u"Darker Side", "Special2WorldHomeStage", 4, "Darker (4)", "ShineSpecial1", "ShineGrandSpecial1", 9, 500, nullptr, "Special2World" },
    };

    inline const Entry* getByWorldId(s32 worldId) {
        if (worldId < 0 || worldId >= sWorldCount)
            return nullptr;
        return &sWorlds[worldId];
    }

    inline const Entry* getByScenarioWorldId(s32 scenarioWorldId) {
        for (s32 i = 0; i < sWorldCount; i++) {
            if (sWorlds[i].scenarioWorldId == scenarioWorldId)
                return &sWorlds[i];
        }
        return nullptr;
    }

    inline const Entry* getBySkyboxIndex(s32 skyboxIndex) {
        return getByScenarioWorldId(skyboxIndex);
    }

    inline const Entry* findByMainStage(const char* stageName) {
        if (!stageName)
            return nullptr;
        for (s32 i = 0; i < sWorldCount; i++) {
            if (std::strcmp(sWorlds[i].mainStage, stageName) == 0)
                return &sWorlds[i];
        }
        return nullptr;
    }

    inline const char* getDisplayNameForWorldId(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->displayName : "???";
    }

    inline const char16_t* getDisplayName16ForWorldId(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->displayName16 : u"Error";
    }

    inline const char* getMoonArchiveForWorldId(s32 worldId, bool isGrand = false) {
        const Entry* entry = getByWorldId(worldId);
        if (!entry)
            return nullptr;
        if (isGrand && entry->grandMoonArchive)
            return entry->grandMoonArchive;
        return entry->moonArchive;
    }

    inline s32 getMclFrameForWorldId(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->mclFrame : -1;
    }

    inline s32 getVanillaMoonRequirement(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->vanillaMoonRequirement : 0;
    }

    inline const char* getSaveThumbnailForWorldId(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->saveThumbnail : nullptr;
    }

    inline const char* getPaintingPaneNameForWorldId(s32 worldId) {
        const Entry* entry = getByWorldId(worldId);
        return entry ? entry->paintingPaneName : nullptr;
    }

    inline const char* getPaintingPaneNameForDisplayName(const char* displayName) {
        if (!displayName)
            return nullptr;
        for (s32 i = 0; i < sWorldCount; i++) {
            if (sWorlds[i].displayName && std::strcmp(sWorlds[i].displayName, displayName) == 0)
                return sWorlds[i].paintingPaneName;
        }
        return nullptr;
    }

} // namespace WorldMeta
