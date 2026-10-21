#pragma once

#include "types.h"

__attribute__((used)) static const char* regionalShopItemsCap[] = {
    "MarioTailCoat",
    "StickerCap",
    "SouvenirHat1",
    "SouvenirHat2",
};

__attribute__((used)) static const char* regionalShopItemsCascade[] = { "MarioPrimitiveMan", "StickerWaterfall", "SouvenirFall1", "SouvenirFall2" };

__attribute__((used)) static const char* regionalShopItemsSand[] = { "MarioPoncho", "MarioGunman", "StickerSand", "SouvenirSand1", "SouvenirSand2" };

__attribute__((used)) static const char* regionalShopItemsWooded[] = {
    "MarioExplorer",
    "MarioScientist",
    "StickerForest",
    "SouvenirForest1",
    "SouvenirForest2",

};

__attribute__((used)) static const char* regionalShopItemsLake[] = {
    "MarioSwimwear",
    "StickerLake",
    "SouvenirLake1",
    "SouvenirLake2",
};

__attribute__((used)) static const char* regionalShopItemsLost[] = {
    "MarioPilot",
    "StickerClash",
    "SouvenirCrash1",
    "SouvenirCrash2",
};

__attribute__((used)) static const char* regionalShopItemsMetro[] = {
    "MarioMaker",
    "MarioGolf",
    "StickerCity",
    "SouvenirCity1",
    "SouvenirCity2",

};

__attribute__((used)) static const char* regionalShopItemsSeaside[] = {
    "MarioAloha",
    "MarioSailor",
    "StickerSea",
    "SouvenirSea1",
    "SouvenirSea2",
};

__attribute__((used)) static const char* regionalShopItemsSnow[] = {
    "MarioSnowSuit",
    "StickerSnow",
    "SouvenirSnow1",
    "SouvenirSnow2",

};

__attribute__((used)) static const char* regionalShopItemsLuncheon[] = {
    "MarioCook",
    "MarioPainter",
    "StickerLava",
    "SouvenirLava1",
    "SouvenirLava2",

};

__attribute__((used)) static const char* regionalShopItemsBowser[] = {
    "MarioArmor",
    "MarioHappi",
    "StickerSky",
    "SouvenirSky1",
    "SouvenirSky2",
};
__attribute__((used)) static const char* regionalShopItemsMoon[] = {
    "MarioSpaceSuit",
    "StickerMoon",
    "SouvenirMoon1",
    "SouvenirMoon2",
};
__attribute__((used)) static const char* regionalShopItemsMushroom[] = {
    "Mario64", "StickerPeachDokan", "StickerPeachCoin", "StickerPeachBlock", "StickerPeachBlockQuestion", "StickerPeach", "SouvenirPeach1", "SouvenirPeach2"
};

__attribute__((used)) static const char** regionalShopItems[] = {
    regionalShopItemsCap, regionalShopItemsCascade, regionalShopItemsSand, regionalShopItemsWooded, regionalShopItemsLake,
    regionalShopItemsLost, regionalShopItemsMetro, regionalShopItemsSeaside, regionalShopItemsSnow, regionalShopItemsLuncheon,
    regionalShopItemsBowser, regionalShopItemsMoon, regionalShopItemsMushroom
};

__attribute__((used)) static const int regionalShopItemsSizes[] = { 4, 4, 5, 5, 4, 4, 5, 5, 4, 5, 5, 4, 8 };

__attribute__((used)) static const char* costumeNamesByCheckId[] = { "Mario",
    regionalShopItemsCap[0],
    regionalShopItemsCascade[0],
    regionalShopItemsSand[0],
    regionalShopItemsSand[1],
    regionalShopItemsLake[0],
    regionalShopItemsWooded[0],
    regionalShopItemsWooded[1],
    regionalShopItemsLost[0],
    regionalShopItemsMetro[0],
    regionalShopItemsMetro[1],
    regionalShopItemsSnow[0],
    regionalShopItemsSeaside[0],
    regionalShopItemsSeaside[1],
    regionalShopItemsLuncheon[0],
    regionalShopItemsLuncheon[1],
    regionalShopItemsBowser[0],
    regionalShopItemsBowser[1],
    regionalShopItemsMoon[0],
    regionalShopItemsMushroom[0],
    "MarioShopman",
    "MarioNew3DS",
    "MarioMechanic",
    "MarioSuit",
    "MarioPirate",
    "MarioClown",
    "MarioFootball",
    "MarioColorClassic",
    "MarioColorLuigi",
    "MarioColorWario",
    "MarioColorWaluigi",
    "MarioColorGold",
    "MarioDoctor",
    "MarioDiddyKong",
    "MarioKoopa",
    "MarioPeach",
    "Mario64Metal",
    "MarioKing",
    "MarioTuxedo",
    "MarioCaptain",
    "MarioUnderwear",
    "MarioHakama",
    "MarioBone",
    "MarioInvisible" };

__attribute__((used)) static const char* stickerNames[] = {
    regionalShopItemsCap[1],
    regionalShopItemsCascade[1],
    regionalShopItemsSand[2],
    regionalShopItemsLake[1],
    regionalShopItemsWooded[2],
    regionalShopItemsLost[1],
    regionalShopItemsMetro[2],
    regionalShopItemsSnow[1],
    regionalShopItemsSeaside[2],
    regionalShopItemsLuncheon[2],
    regionalShopItemsBowser[2],
    regionalShopItemsMoon[1],
    regionalShopItemsMushroom[1],
    regionalShopItemsMushroom[2],
    regionalShopItemsMushroom[3],
    regionalShopItemsMushroom[4],
    regionalShopItemsMushroom[5],
};

__attribute__((used)) static const char* souvenirNames[] = {
    regionalShopItemsCap[2],
    regionalShopItemsCap[3],
    regionalShopItemsCascade[2],
    regionalShopItemsCascade[3],
    regionalShopItemsSand[3],
    regionalShopItemsSand[4],
    regionalShopItemsLake[2],
    regionalShopItemsLake[3],
    regionalShopItemsWooded[3],
    regionalShopItemsWooded[4],
    regionalShopItemsLost[2],
    regionalShopItemsLost[3],
    regionalShopItemsMetro[3],
    regionalShopItemsMetro[4],
    regionalShopItemsSnow[2],
    regionalShopItemsSnow[3],
    regionalShopItemsSeaside[3],
    regionalShopItemsSeaside[4],
    regionalShopItemsBowser[3],
    regionalShopItemsBowser[4],
    regionalShopItemsMoon[2],
    regionalShopItemsMoon[3],
    regionalShopItemsMushroom[6],
    regionalShopItemsMushroom[7],
};

__attribute__((used)) static const char* moonItemNames[] = {
    "MoonCity", // 101
    "MoonForest", // 138
    "MoonWaterfall", // 211
    "MoonCap", // 230
    "MoonLava", // 294
    "MoonSky", // 360
    "MoonClash", // 398
    "MoonLake", // 430
    "MoonSea", // 460
    "MoonSand", // 565
    "MoonSnow", // 868
    "MoonPeach", // 933
    "MoonMoon" // 1157
};