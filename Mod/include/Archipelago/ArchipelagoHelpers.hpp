#pragma once

#include "game/Player/PlayerActorHakoniwa.h"

#include "MessengerData.h"
#include "types.h"

// __attribute__((used)) static const char* regionalShopItemsCap[] = {
//     "MarioTailCoat",
//     "StickerCap",
//     "SouvenirHat1",
//     "SouvenirHat2",
// };

// __attribute__((used)) static const char* regionalShopItemsCascade[] = {"MarioPrimitiveMan", "StickerWaterfall", "SouvenirFall1", "SouvenirFall2"};

// __attribute__((used)) static const char* regionalShopItemsSand[] = {"MarioPoncho", "MarioGunman", "StickerSand", "SouvenirSand1", "SouvenirSand2"};

// __attribute__((used)) static const char* regionalShopItemsWooded[] = {
//     "MarioExplorer", "MarioScientist", "StickerForest", "SouvenirForest1", "SouvenirForest2",

// };

// __attribute__((used)) static const char* regionalShopItemsLake[] = {
//     "MarioSwimwear",
//     "StickerLake",
//     "SouvenirLake1",
//     "SouvenirLake2",
// };

// __attribute__((used)) static const char* regionalShopItemsLost[] = {
//     "MarioPilot",
//     "StickerClash",
//     "SouvenirCrash1",
//     "SouvenirCrash2",
// };

// __attribute__((used)) static const char* regionalShopItemsMetro[] = {
//     "MarioMaker", "MarioGolf", "StickerCity", "SouvenirCity1", "SouvenirCity2",

// };

// __attribute__((used)) static const char* regionalShopItemsSeaside[] = {
//     "MarioAloha", "MarioSailor", "StickerSea", "SouvenirSea1", "SouvenirSea2",
// };

// __attribute__((used)) static const char* regionalShopItemsSnow[] = {
//     "MarioSnowSuit",
//     "StickerSnow",
//     "SouvenirSnow1",
//     "SouvenirSnow2",

// };

// __attribute__((used)) static const char* regionalShopItemsLuncheon[] = {
//     "MarioCook", "MarioPainter", "StickerLava", "SouvenirLava1", "SouvenirLava2",

// };

// __attribute__((used)) static const char* regionalShopItemsBowser[] = {
//     "MarioArmor", "MarioHappi", "StickerSky", "SouvenirSky1", "SouvenirSky2",
// };
// __attribute__((used)) static const char* regionalShopItemsMoon[] = {
//     "MarioSpaceSuit",
//     "StickerMoon",
//     "SouvenirMoon1",
//     "SouvenirMoon2",
// };
// __attribute__((used)) static const char* regionalShopItemsMushroom[] = {
//     "Mario64", "StickerPeachDokan", "StickerPeachCoin", "StickerPeachBlock", "StickerPeachBlockQuestion", "StickerPeach", "SouvenirPeach1",
//     "SouvenirPeach2"};

// __attribute__((used)) static const char** regionalShopItems[] = {
//     regionalShopItemsCap,    regionalShopItemsCascade, regionalShopItemsSand,    regionalShopItemsWooded, regionalShopItemsLake,
//     regionalShopItemsLost,   regionalShopItemsMetro,   regionalShopItemsSeaside, regionalShopItemsSnow,   regionalShopItemsLuncheon,
//     regionalShopItemsBowser, regionalShopItemsMoon,    regionalShopItemsMushroom};

// __attribute__((used)) static const int regionalShopItemsSizes[] = {4, 4, 5, 5, 4, 4, 5, 5, 4, 5, 5, 4, 8};

// __attribute__((used)) static const char* costumeNamesByCheckId[] = {"Mario",
//                                                                     regionalShopItemsCap[0],
//                                                                     regionalShopItemsCascade[0],
//                                                                     regionalShopItemsSand[0],
//                                                                     regionalShopItemsSand[1],
//                                                                     regionalShopItemsLake[0],
//                                                                     regionalShopItemsWooded[0],
//                                                                     regionalShopItemsWooded[1],
//                                                                     regionalShopItemsLost[0],
//                                                                     regionalShopItemsMetro[0],
//                                                                     regionalShopItemsMetro[1],
//                                                                     regionalShopItemsSnow[0],
//                                                                     regionalShopItemsSeaside[0],
//                                                                     regionalShopItemsSeaside[1],
//                                                                     regionalShopItemsLuncheon[0],
//                                                                     regionalShopItemsLuncheon[1],
//                                                                     regionalShopItemsBowser[0],
//                                                                     regionalShopItemsBowser[1],
//                                                                     regionalShopItemsMoon[0],
//                                                                     regionalShopItemsMushroom[0],
//                                                                     "MarioShopman",
//                                                                     "MarioNew3DS",
//                                                                     "MarioMechanic",
//                                                                     "MarioSuit",
//                                                                     "MarioPirate",
//                                                                     "MarioClown",
//                                                                     "MarioFootball",
//                                                                     "MarioColorClassic",
//                                                                     "MarioColorLuigi",
//                                                                     "MarioColorWario",
//                                                                     "MarioColorWaluigi",
//                                                                     "MarioColorGold",
//                                                                     "MarioDoctor",
//                                                                     "MarioDiddyKong",
//                                                                     "MarioKoopa",
//                                                                     "MarioPeach",
//                                                                     "Mario64Metal",
//                                                                     "MarioKing",
//                                                                     "MarioTuxedo",
//                                                                     "MarioCaptain",
//                                                                     "MarioUnderwear",
//                                                                     "MarioHakama",
//                                                                     "MarioBone",
//                                                                     "MarioInvisible"};

// __attribute__((used)) static const char* stickerNames[] = {
//     regionalShopItemsCap[1],      regionalShopItemsCascade[1],  regionalShopItemsSand[2],     regionalShopItemsLake[1],     regionalShopItemsWooded[2],
//     regionalShopItemsLost[1],     regionalShopItemsMetro[2],    regionalShopItemsSnow[1],     regionalShopItemsSeaside[2],  regionalShopItemsLuncheon[2],
//     regionalShopItemsBowser[2],   regionalShopItemsMoon[1],     regionalShopItemsMushroom[1], regionalShopItemsMushroom[2], regionalShopItemsMushroom[3],
//     regionalShopItemsMushroom[4], regionalShopItemsMushroom[5],
// };

// __attribute__((used)) static const char* souvenirNames[] = {
//     regionalShopItemsCap[2],  regionalShopItemsCap[3],     regionalShopItemsCascade[2],  regionalShopItemsCascade[3],  regionalShopItemsSand[3],
//     regionalShopItemsSand[4], regionalShopItemsLake[2],    regionalShopItemsLake[3],     regionalShopItemsWooded[3],   regionalShopItemsWooded[4],
//     regionalShopItemsLost[2], regionalShopItemsLost[3],    regionalShopItemsMetro[3],    regionalShopItemsMetro[4],    regionalShopItemsSnow[2],
//     regionalShopItemsSnow[3], regionalShopItemsSeaside[3], regionalShopItemsSeaside[4],  regionalShopItemsBowser[3],   regionalShopItemsBowser[4],
//     regionalShopItemsMoon[2], regionalShopItemsMoon[3],    regionalShopItemsMushroom[6], regionalShopItemsMushroom[7],
// };

// __attribute__((used)) static const char* moonItemNames[] = {
//     "MoonCity",       // 101
//     "MoonForest",     // 138
//     "MoonWaterfall",  // 211
//     "MoonCap",        // 230
//     "MoonLava",       // 294
//     "MoonSky",        // 360
//     "MoonClash",      // 398
//     "MoonLake",       // 430
//     "MoonSea",        // 460
//     "MoonSand",       // 565
//     "MoonSnow",       // 868
//     "MoonPeach",      // 933
//     "MoonMoon"        // 1157
// };

// __attribute__((used)) static const char* captureListNames[] = {
//     "Frog",
//     "ElectricWire",  // Spark pylon
//     "KuriboWing",    // Paragoomba
//     "Wanwan",        // Chain Chomp
//     "WanwanBig",     // Big Chain Chomp
//     "BreedaWanwan",  // Broode's Chain Chomp
//     "TRex",
//     "Fukankun",  // Binoculars
//     "Killer",    // Bullet Bill
//     "Megane",    // Moe-eye
//     "Cactus",
//     "Kuribo",           // Goomba
//     "BossKnuckleHand",  // Knucklotec's Fist
//     "BazookaElectric",  // Mini Rocket
//     "Kakku",            // Glydon
//     "JugemFishing",     // Lakitu
//     "Fastener",         // Zipper
//     "Pukupuku",         // Cheep Cheep
//     "GotogotonLake",    // Puzzle Part (Lake Kingdom)
//     "PackunPoison",     // Poison Pirana Plant
//     "Senobi",           // Uproot
//     "FireBros",         // Fire Bro
//     "Tank",             // Sherm
//     "Gamane",           // Coin Coffer
//     "Tree",
//     "RockForest",                // Boulder
//     "FukuwaraiFacePartsKuribo",  // Gooma Picture Match Piece
//     "Imomu",                     // Tropical Wiggler
//     "Guidepost",                 // Pole
//     "Manhole",
//     "Car",                       // Taxi
//     "Radicon",                   // RC Car
//     "Byugo",                     // Ty-foo
//     "Yukimaru",                  // Shiverian Racer
//     "PukupukuSnow",              // Cheep Cheep (Snow Kingdom)
//     "Hosui",                     // Gushen
//     "Bubble",                    // Lava Bubble
//     "HackFork",                  // Volbonan
//     "HammerBros",                // Hammer and Pan Bros
//     "CarryMeat",                 // Meat
//     "PackunFire",                // Fire Pirana Plant
//     "Tsukkun",                   // Pokio
//     "Statue",                    // Jizo
//     "StatueKoopa",               // Bowser Statue
//     "KaronWing",                 // Para Bones
//     "KillerMagnum",              // Bonsai Bill
//     "Bull",                      // Chargin' Chuck
//     "Koopa",                     // Bowser
//     "AnagramAlphabetCharacter",  // Letter
//     "GotogotonCity",             // Puzzle Part (Metro Kingdom)
//     "FukuwaraiFacePartsMario",   // Mario Picture Match Piece
//     "Yoshi",
// };

// __attribute__((used)) static const char* regionalCoinStages[] = {
//     "CapWorldHomeStage",
//     "CapWorldTowerStage",
//     "FrogSearchExStage",
//     "PushBlockExStage",
//     "PoisonWaveExStage",
//     "WaterfallWorldHomeStage",
//     "Lift2DExStage",
//     "SandWorldHomeStage",
//     "SandWorldKillerExStage",
//     "SandWorldMeganeExStage",
//     "SandWorldPressExStage",
//     "SandWorldPyramid001Stage",
//     "SandWorldRotateExStage",
//     "SandWorldUnderground000Stage",
//     "SandWorldSphinxExStage",
//     "ForestWorldHomeStage",
//     "ForestWorldTowerStage",
//     "ForestWorldWaterExStage",
//     "ForestWorldWoodsStage",
//     "ForestWorldCloudBonusExStage",
//     "RailCollisionExStage",
//     "ShootingElevatorExStage",
//     "LakeWorldHomeStage",
//     "TrampolineWallCatchExStage",
//     "ClashWorldHomeStage",
//     "CityWorldHomeStage",
//     "CityWorldFactoryStage",
//     "CityWorldMainTowerStage",
//     "PoleKillerExStage",
//     "PoleGrabCeilExStage",
//     "TrexBikeExStage",
//     "SeaWorldHomeStage",
//     "SeaWorldUtsuboCaveStage",
//     "SnowWorldHomeStage",
//     "SnowWorldTownStage",
//     "SnowWorldLobby000Stage",
//     "LavaWorldHomeStage",
//     "LavaWorldBubbleLaneExStage",
//     "LavaWorldClockExStage",
//     "ForkExStage",
//     "SkyWorldHomeStage",
//     "MoonWorldHomeStage",
//     "MoonWorldCaptureParadeStage",
//     "PeachWorldHomeStage",
//     "PeachWorldCastleStage",
// };

// __attribute__((used)) static const int regionalCoinListLengths[] = {31, 9,  4,  3, 3,  46, 4, 63, 6,  4, 4,  3,  5, 7,  8, 76, 3, 3,   9,  3,  3,  3, 47,
//                                                                     3,  50, 71, 7, 10, 3,  3, 6,  94, 6, 13, 30, 7, 91, 3, 3,  3, 100, 32, 18, 97, 3};

// // region Cap Coins
// __attribute__((used)) static const char* CapWorldHomeRegionalCoinIds[] = {
//     "obj2103", "obj2105", "obj2108", "obj2104", "obj2109", "obj2110", "obj2111", "obj2112", "obj2117", "obj2118", "obj2119",
//     "obj2120", "obj2139", "obj2140", "obj2141", "obj2142", "obj2143", "obj2144", "obj2145", "obj2147", "obj2148", "obj2149",
//     "obj2166", "obj2167", "obj2168", "obj2169", "obj2170", "obj2171", "obj2233", "obj2234", "obj2235",
// };

// __attribute__((used)) static const char* CapWorldTowerRegionalCoinIds[] = {
//     "obj1348", "obj1349", "obj1350", "obj1351", "obj1352", "obj1353", "obj1354", "obj1355", "obj1356",
// };

// __attribute__((used)) static const char* FrogSearchExRegionalCoinIds[] = {
//     "obj64",
//     "obj65",
//     "obj66",
//     "obj67",
// };

// __attribute__((used)) static const char* PushBlockExRegionalCoinIds[] = {
//     "obj131",
//     "obj515",
//     "obj516",
// };

// __attribute__((used)) static const char* PoisonWaveExRegionalCoinIds[] = {
//     "obj422",
//     "obj423",
//     "obj424",
// };
// // endregion

// // region Cascade Coins
// __attribute__((used)) static const char* WaterfallWorldHomeRegionalCoinIds[] = {
//     "obj1046", "obj1047", "obj1048", "obj1057", "obj1211", "obj1212", "obj1106", "obj1107", "obj1109", "obj1535", "obj1536", "obj1537",
//     "obj1641", "obj1926", "obj1927", "obj1796", "obj1797", "obj1798", "obj1855", "obj1856", "obj1857", "obj1897", "obj1898", "obj1899",
//     "obj2041", "obj2042", "obj2043", "obj2156", "obj2157", "obj2158", "obj2159", "obj3265", "obj3266", "obj3267", "obj3268", "obj3269",
//     "obj3270", "obj3271", "obj3272", "obj3273", "obj1049", "obj1050", "obj1394", "obj1555", "obj1556", "obj1557",
// };

// __attribute__((used)) static const char* Lift2DExRegionalCoinIds[] = {
//     "obj6460",
//     "obj6461",
//     "obj6462",
//     "obj7621",
// };
// // endregion

// // region Sand Coins
// __attribute__((used)) static const char* SandWorldHomeRegionalCoinIds[] = {
//     "obj1438",
//     "obj3024",
//     "obj3025",
//     "obj1831",
//     "obj1832",
//     "obj1833",
//     "obj1967",
//     "obj1969",
//     "obj1970",
//     "obj1999",
//     "obj2000",
//     "obj2399",
//     "obj2018",
//     "obj3737",
//     "obj2019",
//     "obj2021",
//     "obj3726",
//     "obj2022",
//     "obj3727",
//     "obj2392",
//     "obj2393",
//     "obj2394",
//     "obj2396",
//     "obj2397",
//     "obj2398",
//     "obj3404",
//     "obj3405",
//     "obj3406",
//     "obj3479",
//     "obj3480",
//     "obj3481",
//     "obj3720",
//     "obj3721",
//     "obj3722",
//     "obj3723",
//     "obj3724",
//     "obj3725",
//     "obj3855",
//     "obj3856",
//     "obj3857",
//     "obj3879",
//     "obj3880",
//     "obj4864",
//     "obj4865",
//     "obj4866",
//     "obj4867",
//     "obj4868",
//     "obj4869",
//     "obj6862",
//     "obj6863",
//     "obj6864",
//     "obj3671",
//     "obj3673",
//     "obj3676",
//     "obj3677",
//     "obj4871",
//     "obj4873",
//     "obj4875",
//     "obj4876",
//     "obj134(SandWorldKillerTowerZone[obj2721])",
//     "obj140(SandWorldKillerTowerZone[obj2721])",
//     "obj135(SandWorldKillerTowerZone[obj2721])",
//     "obj141(SandWorldKillerTowerZone[obj2721])",
// };

// __attribute__((used)) static const char* SandWorldKillerExRegionalCoinIds[] = {
//     "obj38", "obj39", "obj40", "obj41", "obj132", "obj133",
// };

// __attribute__((used)) static const char* SandWorldMeganeExRegionalCoinIds[] = {
//     "obj172",
//     "obj203",
//     "obj204",
//     "obj205",
// };

// __attribute__((used)) static const char* SandWorldPressExRegionalCoinIds[] = {
//     "obj44",
//     "obj46",
//     "obj195",
//     "obj196",
// };

// __attribute__((used)) static const char* SandWorldPyramid001RegionalCoinIds[] = {
//     "obj295",
//     "obj296",
//     "obj297",
// };

// __attribute__((used)) static const char* SandWorldRotateExRegionalCoinIds[] = {
//     "obj201", "obj202", "obj203", "obj225", "obj226",
// };

// __attribute__((used)) static const char* SandWorldUnderground000RegionalCoinIds[] = {
//     "obj165", "obj166", "obj168", "obj530", "obj531", "obj532", "obj533",
// };

// __attribute__((used)) static const char* SandWorldSphinxExRegionalCoinIds[] = {
//     "obj1136", "obj1139", "obj1137", "obj1141", "obj1142", "obj1138", "obj1143", "obj1144",
// };
// // endregion

// // region Wooded Coins
// __attribute__((used)) static const char* ForestWorldHomeRegionalCoinIds[] = {
//     "obj1727", "obj1729", "obj1730", "obj1752", "obj1753", "obj1754", "obj1757", "obj1758", "obj1759", "obj1772", "obj5937", "obj1783", "obj1784",
//     "obj1785", "obj1786", "obj2294", "obj2295", "obj4826", "obj6989", "obj2305", "obj2306", "obj2307", "obj4918", "obj4919", "obj4920", "obj5000",
//     "obj5001", "obj5002", "obj6990", "obj5036", "obj5037", "obj5038", "obj5039", "obj5119", "obj5120", "obj5121", "obj5122", "obj5616", "obj5617",
//     "obj5618", "obj5950", "obj5952", "obj5963", "obj5953", "obj5954", "obj5955", "obj6145", "obj5957", "obj5958", "obj6150", "obj5964", "obj5965",
//     "obj5966", "obj5970", "obj5971", "obj5972", "obj6024", "obj6025", "obj6146", "obj6147", "obj6148", "obj6149", "obj6402", "obj6985", "obj7317",
//     "obj7318", "obj7319", "obj7320", "obj7321", "obj7322", "obj7704", "obj7705", "obj7706", "obj1748", "obj1745", "obj1750",
// };

// __attribute__((used)) static const char* ForestWorldTowerRegionalCoinIds[] = {
//     "obj216",
//     "obj217",
//     "obj218",
// };

// __attribute__((used)) static const char* ForestWorldWaterExRegionalCoinIds[] = {
//     "obj245",
//     "obj246",
//     "obj247",
// };

// __attribute__((used)) static const char* ForestWorldWoodsRegionalCoinIds[] = {
//     "obj93", "obj94", "obj95", "obj223", "obj224", "obj225", "obj321", "obj322", "obj323",
// };

// __attribute__((used)) static const char* ForestWorldCloudBonusExRegionalCoinIds[] = {
//     "obj1546",
//     "obj1547",
//     "obj1548",
// };

// __attribute__((used)) static const char* RailCollisionExRegionalCoinIds[] = {
//     "obj261",
//     "obj262",
//     "obj263",
// };

// __attribute__((used)) static const char* ShootingElevatorExRegionalCoinIds[] = {
//     "obj253",
//     "obj254",
//     "obj255",
// };
// // endregion

// // region Lake Coins
// __attribute__((used)) static const char* LakeWorldHomeRegionalCoinIds[] = {
//     "obj20(LakeWorld2DZone[obj526])",
//     "obj22(LakeWorld2DZone[obj526])",
//     "obj23(LakeWorld2DZone[obj526])",
//     "obj21(LakeWorld2DZone[obj526])",
//     "obj111",
//     "obj113",
//     "obj112",
//     "obj312",
//     "obj313",
//     "obj314",
//     "obj315",
//     "obj351",
//     "obj353",
//     "obj354",
//     "obj509",
//     "obj511",
//     "obj510",
//     "obj529",
//     "obj530",
//     "obj531",
//     "obj336(LakeWorldTownZone[obj324])",
//     "obj337(LakeWorldTownZone[obj324])",
//     "obj338(LakeWorldTownZone[obj324])",
//     "obj431(LakeWorldTownZone[obj324])",
//     "obj432(LakeWorldTownZone[obj324])",
//     "obj433(LakeWorldTownZone[obj324])",
//     "obj549(LakeWorldTownZone[obj324])",
//     "obj448(LakeWorldTownZone[obj324])",
//     "obj449(LakeWorldTownZone[obj324])",
//     "obj450(LakeWorldTownZone[obj324])",
//     "obj746(LakeWorldTownZone[obj324])",
//     "obj551(LakeWorldTownZone[obj324])",
//     "obj552(LakeWorldTownZone[obj324])",
//     "obj554(LakeWorldTownZone[obj324])",
//     "obj668(LakeWorldTownZone[obj324])",
//     "obj670(LakeWorldTownZone[obj324])",
//     "obj671(LakeWorldTownZone[obj324])",
//     "obj669(LakeWorldTownZone[obj324])",
//     "obj743(LakeWorldTownZone[obj324])",
//     "obj744(LakeWorldTownZone[obj324])",
//     "obj745(LakeWorldTownZone[obj324])",
//     "obj759(LakeWorldTownZone[obj324])",
//     "obj760(LakeWorldTownZone[obj324])",
//     "obj761(LakeWorldTownZone[obj324])",
//     "obj613(LakeWorldTownZone[obj324])",
//     "obj615(LakeWorldTownZone[obj324])",
//     "obj758(LakeWorldTownZone[obj324])",
// };

// __attribute__((used)) static const char* TrampolineWallCatchExRegionalCoinIds[] = {
//     "obj1095",
//     "obj1096",
//     "obj1137",
// };
// // endregion

// // region Lost Coins
// __attribute__((used)) static const char* ClashWorldHomeRegionalCoinIds[] = {
//     "obj553",  "obj554",  "obj555",  "obj587",  "obj1030", "obj1035", "obj1122", "obj588",  "obj589",  "obj1036", "obj727",  "obj979", "obj728",
//     "obj729",  "obj1121", "obj851",  "obj852",  "obj853",  "obj854",  "obj868",  "obj964",  "obj870",  "obj872",  "obj874",  "obj875", "obj977",
//     "obj873",  "obj998",  "obj901",  "obj1193", "obj902",  "obj904",  "obj903",  "obj1738", "obj978",  "obj1740", "obj1739", "obj997", "obj999",
//     "obj1039", "obj1043", "obj1096", "obj1097", "obj1098", "obj1689", "obj1690", "obj1691", "obj1031", "obj1033", "obj1034",
// };
// // endregion

// // region Metro Coins
// __attribute__((used)) static const char* CityWorldHomeRegionalCoinIds[] = {
//     "obj4637",  "obj4639",  "obj4640",  "obj7901",  "obj7902",  "obj7904",  "obj8580",  "obj8581",  "obj8582",  "obj9306",  "obj9307",  "obj9308",
//     "obj9372",  "obj9373",  "obj9374",  "obj10505", "obj11282", "obj4506",  "obj4508",  "obj4507",  "obj4633",  "obj10075", "obj10076", "obj4638",
//     "obj13420", "obj13421", "obj5055",  "obj5056",  "obj9413",  "obj5910",  "obj5913",  "obj5911",  "obj7939",  "obj11281", "obj8043",  "obj13097",
//     "obj13098", "obj8044",  "obj8046",  "obj8269",  "obj9541",  "obj9542",  "obj8807",  "obj8808",  "obj8809",  "obj10062", "obj10063", "obj10064",
//     "obj10675", "obj10676", "obj10968", "obj10969", "obj10970", "obj10971", "obj10978", "obj10979", "obj10980", "obj11001", "obj11002", "obj11003",
//     "obj11083", "obj11280", "obj12978", "obj12979", "obj12983", "obj13145", "obj13146", "obj15765", "obj15766", "obj15775", "obj15777",
// };

// __attribute__((used)) static const char* CityWorldFactoryRegionalCoinIds[] = {
//     "obj664(CityWorldFactory01Zone[obj309])",
//     "obj665(CityWorldFactory01Zone[obj309])",
//     "obj666(CityWorldFactory01Zone[obj309])",
//     "obj298",
//     "obj299",
//     "obj301",
//     "obj367",
// };

// __attribute__((used)) static const char* CityWorldMainTowerRegionalCoinIds[] = {
//     "obj546", "obj547", "obj1260", "obj874", "obj1041", "obj882", "obj883", "obj884", "obj966", "obj967",
// };

// __attribute__((used)) static const char* PoleKillerExRegionalCoinIds[] = {
//     "obj1497",
//     "obj1498",
//     "obj1499",
// };

// __attribute__((used)) static const char* PoleGrabCeilExRegionalCoinIds[] = {
//     "obj800",
//     "obj801",
//     "obj802",
// };

// __attribute__((used)) static const char* TrexBikeExRegionalCoinIds[] = {
//     "obj5435", "obj5436", "obj5437", "obj5438", "obj5439", "obj5440",
// };
// // endregion

// // region Seaside Coins
// __attribute__((used)) static const char* SeaWorldHomeRegionalCoinIds[] = {
//     "obj254(SeaWorld2DLargeZone[obj2083])",
//     "obj256(SeaWorld2DLargeZone[obj2083])",
//     "obj371(SeaWorld2DLargeZone[obj2083])",
//     "obj220(SeaWorldDamageBallZone[obj1070])",
//     "obj221(SeaWorldDamageBallZone[obj1070])",
//     "obj223(SeaWorldDamageBallZone[obj1070])",
//     "obj1149",
//     "obj1150",
//     "obj1151",
//     "obj1152",
//     "obj1154",
//     "obj1153",
//     "obj1403",
//     "obj1404",
//     "obj1405",
//     "obj1566",
//     "obj1567",
//     "obj1568",
//     "obj2064",
//     "obj2065",
//     "obj2066",
//     "obj2204",
//     "obj2205",
//     "obj2206",
//     "obj2385",
//     "obj2386",
//     "obj2387",
//     "obj2398",
//     "obj2399",
//     "obj2400",
//     "obj2445",
//     "obj2409",
//     "obj2411",
//     "obj2410",
//     "obj2413",
//     "obj2415",
//     "obj2414",
//     "obj2455",
//     "obj2456",
//     "obj2457",
//     "obj2458",
//     "obj2459",
//     "obj2460",
//     "obj2552",
//     "obj2553",
//     "obj2554",
//     "obj2686",
//     "obj2687",
//     "obj2688",
//     "obj3078",
//     "obj3079",
//     "obj3080",
//     "obj4312",
//     "obj4313",
//     "obj4314",
//     "obj206(SeaWorldLavaZone[obj1399])",
//     "obj207(SeaWorldLavaZone[obj1399])",
//     "obj208(SeaWorldLavaZone[obj1399])",
//     "obj236(SeaWorldLavaZone[obj1399])",
//     "obj237(SeaWorldLavaZone[obj1399])",
//     "obj238(SeaWorldLavaZone[obj1399])",
//     "obj430(SeaWorldLavaZone[obj1399])",
//     "obj431(SeaWorldLavaZone[obj1399])",
//     "obj432(SeaWorldLavaZone[obj1399])",
//     "obj494(SeaWorldLavaZone[obj1399])",
//     "obj495(SeaWorldLavaZone[obj1399])",
//     "obj496(SeaWorldLavaZone[obj1399])",
//     "obj280(SeaWorldLighthouseZone[obj1402])",
//     "obj282(SeaWorldLighthouseZone[obj1402])",
//     "obj281(SeaWorldLighthouseZone[obj1402])",
//     "obj314(SeaWorldLighthouseZone[obj1402])",
//     "obj315(SeaWorldLighthouseZone[obj1402])",
//     "obj316(SeaWorldLighthouseZone[obj1402])",
//     "obj351(SeaWorldLighthouseZone[obj1402])",
//     "obj352(SeaWorldLighthouseZone[obj1402])",
//     "obj353(SeaWorldLighthouseZone[obj1402])",
//     "obj373(SeaWorldLighthouseZone[obj1402])",
//     "obj375(SeaWorldLighthouseZone[obj1402])",
//     "obj374(SeaWorldLighthouseZone[obj1402])",
//     "obj83(SeaWorldLongReefZone[obj1921])",
//     "obj85(SeaWorldLongReefZone[obj1921])",
//     "obj84(SeaWorldLongReefZone[obj1921])",
//     "obj89(SeaWorldSphinxQuizZone[obj2084])",
//     "obj90(SeaWorldSphinxQuizZone[obj2084])",
//     "obj103(SeaWorldSphinxQuizZone[obj2084])",
//     "obj149(SeaWorldUnderGlassZone[obj1898])",
//     "obj150(SeaWorldUnderGlassZone[obj1898])",
//     "obj151(SeaWorldUnderGlassZone[obj1898])",
//     "obj346(SeaWorldUnderGlassZone[obj1898])",
//     "obj347(SeaWorldUnderGlassZone[obj1898])",
//     "obj348(SeaWorldUnderGlassZone[obj1898])",
//     "obj46(SeaWorldWallCaveWestZone[obj1354])",
//     "obj47(SeaWorldWallCaveWestZone[obj1354])",
//     "obj48(SeaWorldWallCaveWestZone[obj1354])",
// };

// __attribute__((used)) static const char* SeaWorldUtsuboCaveRegionalCoinIds[] = {
//     "obj484", "obj485", "obj486", "obj487", "obj488", "obj489",
// };
// // endregion

// // Find way to differentiate Shiveria Coins
// // Likely use other placementId information
// // region Snow Coins
// __attribute__((used)) static const char* SnowWorldHomeRegionalCoinIds[] = {
//     "obj1150", "obj1151", "obj1348", "obj1350", "obj1426", "obj1427", "obj1428", "obj1432", "obj1433", "obj1533", "obj1534", "obj1535", "obj1536",
// };

// __attribute__((used)) static const char* SnowWorldTownRegionalCoinIds[] = {
//     "obj1097",
//     "obj1098",
//     "obj1099",
//     "obj1100",
//     "obj169(SnowWorldBalconyZone[obj534])",
//     "obj170(SnowWorldBalconyZone[obj534])",
//     "obj251(SnowWorldBalconyZone[obj534])",
//     "obj271(SnowWorldBalconyZone[obj534])",
//     "obj272(SnowWorldBalconyZone[obj534])",
//     "obj273(SnowWorldBalconyZone[obj534])",
//     "obj222(SnowWorldByugoZone[obj420])",
//     "obj223(SnowWorldByugoZone[obj420])",
//     "obj224(SnowWorldByugoZone[obj420])",
//     "obj225(SnowWorldByugoZone[obj420])",
//     "obj273(SnowWorldByugoZone[obj420])",
//     "obj274(SnowWorldByugoZone[obj420])",
//     "obj275(SnowWorldByugoZone[obj420])",
//     "obj273(SnowWorldGabuzouZone[obj419])",
//     "obj274(SnowWorldGabuzouZone[obj419])",
//     "obj275(SnowWorldGabuzouZone[obj419])",
//     "obj281(SnowWorldGabuzouZone[obj419])",
//     "obj282(SnowWorldGabuzouZone[obj419])",
//     "obj283(SnowWorldGabuzouZone[obj419])",
//     "obj380(SnowWorldIcicleZone[obj417])",
//     "obj381(SnowWorldIcicleZone[obj417])",
//     "obj448(SnowWorldIcicleZone[obj417])",
//     "obj449(SnowWorldIcicleZone[obj417])",
//     "obj421(SnowWorldIcicleZone[obj417])",
//     "obj422(SnowWorldIcicleZone[obj417])",
//     "obj423(SnowWorldIcicleZone[obj417])",
// };

// __attribute__((used)) static const char* SnowWorldLobbyRegionalCoinIds[] = {"obj923", "obj924", "obj925", "obj926", "obj927", "obj928", "obj929"};
// // endregion

// // region Luncheon Coins
// __attribute__((used)) static const char* LavaWorldHomeRegionalCoinIds[] = {
//     "obj2675",
//     "obj2746",
//     "obj2747",
//     "obj2678",
//     "obj2679",
//     "obj2680",
//     "obj2685",
//     "obj2686",
//     "obj2688",
//     "obj2690",
//     "obj2692",
//     "obj2691",
//     "obj2713",
//     "obj2774",
//     "obj2773",
//     "obj2717",
//     "obj2775",
//     "obj4287",
//     "obj2722",
//     "obj2728",
//     "obj2735",
//     "obj2736",
//     "obj2737",
//     "obj2738",
//     "obj2740",
//     "obj2741",
//     "obj2742",
//     "obj2744",
//     "obj5567",
//     "obj5568",
//     "obj2748",
//     "obj2756",
//     "obj2760",
//     "obj2750",
//     "obj2754",
//     "obj2758",
//     "obj2762",
//     "obj2751",
//     "obj2755",
//     "obj2785",
//     "obj2783",
//     "obj2757",
//     "obj2761",
//     "obj3489",
//     "obj3490",
//     "obj2784",
//     "obj2787",
//     "obj3123",
//     "obj3310",
//     "obj3311",
//     "obj3312",
//     "obj3808",
//     "obj3809",
//     "obj3810",
//     "obj3843",
//     "obj3844",
//     "obj3845",
//     "obj5656",
//     "obj5657",
//     "obj5658",
//     "obj6254",
//     "obj6255",
//     "obj6256",
//     "obj6363",
//     "obj6364",
//     "obj6365",
//     "obj2659",
//     "obj2660",
//     "obj2663",
//     "obj2664",
//     "obj2662",
//     "obj3740",
//     "obj3738",
//     "obj2665",
//     "obj2666",
//     "obj3991",
//     "obj3910",
//     "obj3911",
//     "obj3912",
//     "obj6347",
//     "obj6348",
//     "obj6349",
//     "obj6366",
//     "obj6367",
//     "obj6368",
//     "obj185(LavaWorldCaveZone[obj3426])",
//     "obj208(LavaWorldCaveZone[obj3426])",
//     "obj209(LavaWorldCaveZone[obj3426])",
//     "obj187(LavaWorldCaveZone[obj3426])",
//     "obj206(LavaWorldCaveZone[obj3426])",
//     "obj207(LavaWorldCaveZone[obj3426])",
// };

// __attribute__((used)) static const char* LavaWorldBubbleLaneExRegionalCoinIds[] = {
//     "obj807",
//     "obj808",
//     "obj809",
// };

// __attribute__((used)) static const char* LavaWorldClockExRegionalCoinIds[] = {
//     "obj431",
//     "obj432",
//     "obj433",
// };

// __attribute__((used)) static const char* ForkExRegionalCoinIds[] = {
//     "obj5762",
//     "obj5763",
//     "obj5764",
// };
// // endregion

// // region Bowser Coins
// __attribute__((used)) static const char* SkyWorldHomeRegionalCoinIds[] = {
//     "obj653(SkyWorldCastleZone[obj2160])",
//     "obj4377(SkyWorldCastleZone[obj2160])",
//     "obj6733(SkyWorldCastleZone[obj2160])",
//     "obj1692(SkyWorldCastleZone[obj2160])",
//     "obj1694(SkyWorldCastleZone[obj2160])",
//     "obj1693(SkyWorldCastleZone[obj2160])",
//     "obj1800(SkyWorldCastleZone[obj2160])",
//     "obj1918(SkyWorldCastleZone[obj2160])",
//     "obj2450(SkyWorldCastleZone[obj2160])",
//     "obj1801(SkyWorldCastleZone[obj2160])",
//     "obj1917(SkyWorldCastleZone[obj2160])",
//     "obj2451(SkyWorldCastleZone[obj2160])",
//     "obj1955(SkyWorldCastleZone[obj2160])",
//     "obj1958(SkyWorldCastleZone[obj2160])",
//     "obj1956(SkyWorldCastleZone[obj2160])",
//     "obj2306(SkyWorldCastleZone[obj2160])",
//     "obj2307(SkyWorldCastleZone[obj2160])",
//     "obj2308(SkyWorldCastleZone[obj2160])",
//     "obj2309(SkyWorldCastleZone[obj2160])",
//     "obj2501(SkyWorldCastleZone[obj2160])",
//     "obj2512(SkyWorldCastleZone[obj2160])",
//     "obj2514(SkyWorldCastleZone[obj2160])",
//     "obj2726(SkyWorldCastleZone[obj2160])",
//     "obj2727(SkyWorldCastleZone[obj2160])",
//     "obj2728(SkyWorldCastleZone[obj2160])",
//     "obj3233(SkyWorldCastleZone[obj2160])",
//     "obj3234(SkyWorldCastleZone[obj2160])",
//     "obj3235(SkyWorldCastleZone[obj2160])",
//     "obj5680(SkyWorldCastleZone[obj2160])",
//     "obj5681(SkyWorldCastleZone[obj2160])",
//     "obj8169(SkyWorldCastleZone[obj2160])",
//     "obj6735(SkyWorldCastleZone[obj2160])",
//     "obj6736(SkyWorldCastleZone[obj2160])",
//     "obj6737(SkyWorldCastleZone[obj2160])",
//     "obj7322(SkyWorldCastleZone[obj2160])",
//     "obj7324(SkyWorldCastleZone[obj2160])",
//     "obj7323(SkyWorldCastleZone[obj2160])",
//     "obj650",
//     "obj2132",
//     "obj2133",
//     "obj2714",
//     "obj6734",
//     "obj7583",
//     "obj2715",
//     "obj5705",
//     "obj5682",
//     "obj3634",
//     "obj3635",
//     "obj3636",
//     "obj5706",
//     "obj5708",
//     "obj5707",
//     "obj5735",
//     "obj5736",
//     "obj5737",
//     "obj7580",
//     "obj7581",
//     "obj7582",
//     "obj1722",
//     "obj1723",
//     "obj1724",
//     "obj1925",
//     "obj1808",
//     "obj1810",
//     "obj2073",
//     "obj2828",
//     "obj1970",
//     "obj1971",
//     "obj1972",
//     "obj2272",
//     "obj2273",
//     "obj2274",
//     "obj2829",
//     "obj2830",
//     "obj2841",
//     "obj2831",
//     "obj2832",
//     "obj959(SkyWorldWallZone[obj2161])",
//     "obj960(SkyWorldWallZone[obj2161])",
//     "obj1227(SkyWorldWallZone[obj2161])",
//     "obj1503(SkyWorldWallZone[obj2161])",
//     "obj1504(SkyWorldWallZone[obj2161])",
//     "obj2159(SkyWorldWallZone[obj2161])",
//     "obj1612(SkyWorldWallZone[obj2161])",
//     "obj1832(SkyWorldWallZone[obj2161])",
//     "obj1613(SkyWorldWallZone[obj2161])",
//     "obj1872(SkyWorldWallZone[obj2161])",
//     "obj1873(SkyWorldWallZone[obj2161])",
//     "obj2192(SkyWorldWallZone[obj2161])",
//     "obj1964(SkyWorldWallZone[obj2161])",
//     "obj1965(SkyWorldWallZone[obj2161])",
//     "obj1967(SkyWorldWallZone[obj2161])",
//     "obj2238(SkyWorldWallZone[obj2161])",
//     "obj2097(SkyWorldWallZone[obj2161])",
//     "obj2098(SkyWorldWallZone[obj2161])",
//     "obj2099(SkyWorldWallZone[obj2161])",
//     "obj2218(SkyWorldWallZone[obj2161])",
//     "obj2219(SkyWorldWallZone[obj2161])",
//     "obj2220(SkyWorldWallZone[obj2161])",
//     "obj2237(SkyWorldWallZone[obj2161])",
// };
// // endregion

// // region Moon Coins
// __attribute__((used)) static const char* MoonWorldHomeRegionalCoinIds[] = {
//     "obj71(MoonWorldHome2DZone[obj638])",
//     "obj72(MoonWorldHome2DZone[obj638])",
//     "obj73(MoonWorldHome2DZone[obj638])",
//     "obj94",
//     "obj95",
//     "obj96",
//     "obj97",
//     "obj197",
//     "obj198",
//     "obj199",
//     "obj836",
//     "obj837",
//     "obj838",
//     "obj469",
//     "obj470",
//     "obj471",
//     "obj516",
//     "obj517",
//     "obj518",
//     "obj519",
//     "obj672",
//     "obj673",
//     "obj700",
//     "obj805",
//     "obj806",
//     "obj807",
//     "obj811",
//     "obj812",
//     "obj814",
//     "obj859",
//     "obj860",
//     "obj861",
// };

// __attribute__((used)) static const char* MoonWorldCaptureParadeRegionalCoinIds[] = {
//     "obj6104(MoonWorldCaptureParadeBullZone[obj81])",      "obj6105(MoonWorldCaptureParadeBullZone[obj81])",
//     "obj6106(MoonWorldCaptureParadeBullZone[obj81])",      "obj239(MoonWorldCaptureParadeLavaPillarZone[obj80])",
//     "obj240(MoonWorldCaptureParadeLavaPillarZone[obj80])", "obj412(MoonWorldCaptureParadeLavaPillarZone[obj80])",
//     "obj430(MoonWorldCaptureParadeLavaPillarZone[obj80])", "obj461(MoonWorldCaptureParadeLavaPillarZone[obj80])",
//     "obj462(MoonWorldCaptureParadeLavaPillarZone[obj80])", "obj405(MoonWorldCaptureParadeLiftZone[obj243])",
//     "obj406(MoonWorldCaptureParadeLiftZone[obj243])",      "obj407(MoonWorldCaptureParadeLiftZone[obj243])",
//     "obj21(MoonWorldCaptureParadeMeganeZone[obj317])",     "obj22(MoonWorldCaptureParadeMeganeZone[obj317])",
//     "obj23(MoonWorldCaptureParadeMeganeZone[obj317])",     "obj193(MoonWorldCaptureParadeKillerZone[obj304])",
//     "obj194(MoonWorldCaptureParadeKillerZone[obj304])",    "obj195(MoonWorldCaptureParadeKillerZone[obj304])",
// };
// // endregion

// // region Mushroom Coins
// __attribute__((used)) static const char* PeachWorldHomeRegionalCoinIds[] = {
//     "obj740",  "obj741",  "obj742",  "obj847",  "obj848",  "obj849",  "obj850",  "obj851",  "obj852",  "obj853",  "obj854",  "obj855",  "obj856",  "obj857",
//     "obj871",  "obj872",  "obj873",  "obj858",  "obj859",  "obj860",  "obj861",  "obj862",  "obj863",  "obj864",  "obj865",  "obj866",  "obj867",  "obj877",
//     "obj887",  "obj874",  "obj875",  "obj876",  "obj878",  "obj879",  "obj880",  "obj881",  "obj882",  "obj883",  "obj884",  "obj885",  "obj886",  "obj888",
//     "obj889",  "obj890",  "obj1286", "obj1287", "obj1288", "obj1289", "obj1290", "obj1291", "obj1811", "obj1813", "obj1812", "obj1900", "obj1901", "obj1902",
//     "obj1903", "obj1920", "obj1921", "obj1922", "obj1923", "obj1924", "obj1927", "obj1928", "obj1929", "obj1930", "obj1931", "obj1932", "obj1933", "obj1934",
//     "obj1950", "obj1951", "obj1952", "obj1953", "obj1954", "obj1955", "obj1956", "obj1957", "obj1958", "obj1977", "obj1978", "obj1979", "obj1980", "obj1981",
//     "obj1982", "obj1994", "obj1995", "obj1996", "obj2216", "obj2217", "obj2218", "obj2644", "obj2645", "obj2646", "obj1904", "obj1905", "obj1906",
// };

// __attribute__((used)) static const char* PeachWorldCastleRegionalCoinIds[] = {
//     "obj92",
//     "obj93",
//     "obj94",
// };

// __attribute__((used)) static const char** regionalCoinsByCheckId[45] = {
//     CapWorldHomeRegionalCoinIds,
//     CapWorldTowerRegionalCoinIds,
//     FrogSearchExRegionalCoinIds,
//     PushBlockExRegionalCoinIds,
//     PoisonWaveExRegionalCoinIds,
//     WaterfallWorldHomeRegionalCoinIds,
//     Lift2DExRegionalCoinIds,
//     SandWorldHomeRegionalCoinIds,
//     SandWorldKillerExRegionalCoinIds,
//     SandWorldMeganeExRegionalCoinIds,
//     SandWorldPressExRegionalCoinIds,
//     SandWorldPyramid001RegionalCoinIds,
//     SandWorldRotateExRegionalCoinIds,
//     SandWorldUnderground000RegionalCoinIds,
//     SandWorldSphinxExRegionalCoinIds,
//     ForestWorldHomeRegionalCoinIds,
//     ForestWorldTowerRegionalCoinIds,
//     ForestWorldWaterExRegionalCoinIds,
//     ForestWorldWoodsRegionalCoinIds,
//     ForestWorldCloudBonusExRegionalCoinIds,
//     RailCollisionExRegionalCoinIds,
//     ShootingElevatorExRegionalCoinIds,
//     LakeWorldHomeRegionalCoinIds,
//     TrampolineWallCatchExRegionalCoinIds,
//     ClashWorldHomeRegionalCoinIds,
//     CityWorldHomeRegionalCoinIds,
//     CityWorldFactoryRegionalCoinIds,
//     CityWorldMainTowerRegionalCoinIds,
//     PoleKillerExRegionalCoinIds,
//     PoleGrabCeilExRegionalCoinIds,
//     TrexBikeExRegionalCoinIds,
//     SeaWorldHomeRegionalCoinIds,
//     SeaWorldUtsuboCaveRegionalCoinIds,
//     SnowWorldHomeRegionalCoinIds,
//     SnowWorldTownRegionalCoinIds,
//     SnowWorldLobbyRegionalCoinIds,
//     LavaWorldHomeRegionalCoinIds,
//     LavaWorldBubbleLaneExRegionalCoinIds,
//     LavaWorldClockExRegionalCoinIds,
//     ForkExRegionalCoinIds,
//     SkyWorldHomeRegionalCoinIds,
//     MoonWorldHomeRegionalCoinIds,
//     MoonWorldCaptureParadeRegionalCoinIds,
//     PeachWorldHomeRegionalCoinIds,
//     PeachWorldCastleRegionalCoinIds,
// };

// __attribute__((used)) static const char* stageNameList[] = {
//     "AnimalChaseExStage",
//     "BikeSteelExStage",
//     "BossRaidWorldHomeStage",
//     "BullRunExStage",
//     "ByugoPuzzleExStage",
//     "CapAppearExStage",
//     "CapAppearLavaLiftExStage",
//     "CapRotatePackunExStage",
//     "CapWorldHomeStage",
//     "CapWorldTowerStage",
//     "CityPeopleRoadStage",
//     "CityWorldFactoryStage",
//     "CityWorldHomeStage",
//     "CityWorldMainTowerStage",
//     "CityWorldSandSlotStage",
//     "CityWorldShop01Stage",
//     "ClashWorldHomeStage",
//     "ClashWorldShopStage",
//     "CloudWorldHomeStage",
//     "Cube2DExStage",
//     "DonsukeExStage",
//     "DotHardExStage",
//     "ElectricWireExStage",
//     "FastenerExStage",
//     "ForestWorldBonusStage",
//     "ForestWorldBossStage",
//     "ForestWorldCloudBonusExStage",
//     "ForestWorldHomeStage",
//     "ForestWorldTowerStage",
//     "ForestWorldWaterExStage",
//     "ForestWorldWoodsCostumeStage",
//     "ForestWorldWoodsStage",
//     "ForestWorldWoodsTreasureStage",
//     "ForkExStage",
//     "FrogPoisonExStage",
//     "FrogSearchExStage",
//     "FukuwaraiKuriboStage",
//     "GabuzouClockExStage",
//     "Galaxy2DExStage",
//     "GotogotonExStage",
//     "HomeShipInsideStage",
//     "IceWalkerExStage",
//     "IceWaterBlockExStage",
//     "IceWaterDashExStage",
//     "ImomuPoisonExStage",
//     "JangoExStage",
//     "JizoSwitchExStage",
//     "KaronWingTowerStage",
//     "KillerRailCollisionExStage",
//     "KillerRoadExStage",
//     "LakeWorldHomeStage",
//     "LakeWorldShopStage",
//     "LavaWorldBubbleLaneExStage",
//     "LavaWorldClockExStage",
//     "LavaWorldCostumeStage",
//     "LavaWorldExcavationExStage",
//     "LavaWorldFenceLiftExStage",
//     "LavaWorldHomeStage",
//     "LavaWorldShopStage",
//     "LavaWorldTreasureStage",
//     "LavaWorldUpDownExStage",
//     "Lift2DExStage",
//     "MeganeLiftExStage",
//     "MoonAthleticExStage",
//     "MoonWorldCaptureParadeStage",
//     "MoonWorldHomeStage",
//     "MoonWorldKoopa1Stage",
//     "MoonWorldKoopa2Stage",
//     "MoonWorldShopRoom",
//     "MoonWorldSphinxRoom",
//     "MoonWorldWeddingRoomStage",
//     "Note2D3DRoomExStage",
//     "PackunPoisonExStage",
//     "PeachWorldCastleStage",
//     "PeachWorldHomeStage",
//     "PeachWorldPictureBossForestStage",
//     "PeachWorldPictureBossKnuckleStage",
//     "PeachWorldPictureBossMagmaStage",
//     "PeachWorldPictureBossRaidStage",
//     "PeachWorldPictureGiantWanderBossStage",
//     "PeachWorldPictureMofumofuStage",
//     "PeachWorldShopStage",
//     "PoisonWaveExStage",
//     "PoleKillerExStage",
//     "PushBlockExStage",
//     "RadioControlExStage",
//     "RailCollisionExStage",
//     "ReflectBombExStage",
//     "RocketFlowerExStage",
//     "RollingExStage",
//     "SandWorldCostumeStage",
//     "SandWorldHomeStage",
//     "SandWorldKillerExStage",
//     "SandWorldMeganeExStage",
//     "SandWorldPressExStage",
//     "SandWorldPyramid000Stage",
//     "SandWorldPyramid001Stage",
//     "SandWorldSecretStage",
//     "SandWorldShopStage",
//     "SandWorldSlotStage",
//     "SandWorldSphinxExStage",
//     "SandWorldUnderground000Stage",
//     "SandWorldUnderground001Stage",
//     "SandWorldVibrationStage",
//     "SeaWorldCostumeStage",
//     "SeaWorldHomeStage",
//     "SeaWorldSecretStage",
//     "SeaWorldSneakingManStage",
//     "SeaWorldUtsuboCaveStage",
//     "SeaWorldVibrationStage",
//     "SenobiTowerExStage",
//     "ShootingCityExStage",
//     "ShootingElevatorExStage",
//     "SkyWorldCloudBonusExStage",
//     "SkyWorldCostumeStage",
//     "SkyWorldHomeStage",
//     "SkyWorldShopStage",
//     "SkyWorldTreasureStage",
//     "SnowWorldCloudBonusExStage",
//     "SnowWorldCostumeStage",
//     "SnowWorldHomeStage",
//     "SnowWorldLobby000Stage",
//     "SnowWorldLobbyExStage",
//     "SnowWorldShopStage",
//     "SnowWorldTownStage",
//     "Special1WorldHomeStage",
//     "Special1WorldTowerBombTailStage",
//     "Special1WorldTowerCapThrowerStage",
//     "Special1WorldTowerFireBlowerStage",
//     "Special1WorldTowerStackerStage",
//     "Special2WorldCloudStage",
//     "Special2WorldHomeStage",
//     "Special2WorldKoopaStage",
//     "Special2WorldLavaStage",
//     "SwingSteelExStage",
//     "Theater2DExStage",
//     "TogezoRotateExStage",
//     "TrampolineWallCatchExStage",
//     "TrexBikeExStage",
//     "TrexPoppunExStage",
//     "TsukkunClimbExStage",
//     "TsukkunRotateExStage",
//     "WanwanClashExStage",
//     "WaterfallWorldHomeStage",
//     "WaterTubeExStage",
//     "WaterValleyExStage",
//     "WindBlowExStage",
//     "YoshiCloudExStage",
//     "SandWorldRotateExStage",
//     "FogMountainExStage",
//     "PoleGrabCeilExStage",
//     "CloudExStage",
//     "LavaBonus1Zone",
//     "DotTowerExStage",
//     "BikeSteelNoCapExStage",
//     "PackunPoisonNoCapExStage",
//     "ShootingCityYoshiExStage",
//     "SenobiTowerYoshiExStage",
//     "LavaWorldUpDownYoshiExStage",
//     "KillerRoadNoCapExStage",
//     "RevengeBossMagmaStage",
//     "RevengeMofumofuStage",
//     "RevengeBossRaidStage",
//     "RevengeForestBossStage",
//     "RevengeBossKnuckleStage",
//     "RevengeGiantWanderBossStage",
//     "FukuwaraiMarioStage",
//     "PeachWorldCostumeStage",
// };

// __attribute__((used)) static const char* stageIdList[] = {"jizo01",
//                                                           "dot00",
//                                                           "start",
//                                                           "LavaWorldUpDownYoshiEx_Exit",
//                                                           "CapTrampolineB",
//                                                           "PoisonWaveExEnt",
//                                                           "arijigoku",
//                                                           "SeaWorldEX1a",
//                                                           "BossRaidWorldMoonEx02_Exit",
//                                                           "SeaWorldEX1b",
//                                                           "under001enter",
//                                                           "Tower002",
//                                                           "SeaWorldMoonEX1b",
//                                                           "Bonus",
//                                                           "None",
//                                                           "KeyMoveEx",
//                                                           "ShootingCityYoshiEx_Exit",
//                                                           "CP_Entrance",
//                                                           "PoisonEx_Exit",
//                                                           "donsuke",
//                                                           "cube",
//                                                           "pyramid03",
//                                                           "vvv",
//                                                           "EX_RailCollision_Exit",
//                                                           "main_enter",
//                                                           "boureturn",
//                                                           "moon",
//                                                           "pyramid02",
//                                                           "bar1",
//                                                           "FrogSearchExStageEnt",
//                                                           "FireBlowerRoomGoal",
//                                                           "MoonRace",
//                                                           "gunsyu",
//                                                           "Jyukai002",
//                                                           "LakeWorldMoonEX1b",
//                                                           "BBQExDokan",
//                                                           "BossKnuckleA",
//                                                           "Kinopio",
//                                                           "StackerRoomStart",
//                                                           "Tower001",
//                                                           "bbb",
//                                                           "aaa",
//                                                           "KeyMoveExDokan",
//                                                           "run00",
//                                                           "CostumeEventSeaWorld",
//                                                           "dot01",
//                                                           "bike02",
//                                                           "tukkun000_enter",
//                                                           "RaceEntrance",
//                                                           "SnowCostumeEx",
//                                                           "wall",
//                                                           "tukkun001_exit",
//                                                           "FenceLiftEx",
//                                                           "CostumeOut",
//                                                           "FenceLiftExdokan",
//                                                           "MofumofuA",
//                                                           "BBQEx",
//                                                           "WindBlowExStart",
//                                                           "Lift2D",
//                                                           "Fukuwarai",
//                                                           "LakeWorldMoonEX1a",
//                                                           "EXCloud",
//                                                           "SeaWorldMoonEX1a",
//                                                           "SenobiTowerYoshiEx_Exit",
//                                                           "Jyukai003",
//                                                           "BombTailRoomGoal",
//                                                           "EX_AnimalChase",
//                                                           "ForkEX",
//                                                           "town",
//                                                           "town_lava",
//                                                           "PeachWorldEx2a",
//                                                           "main_exit",
//                                                           "Goton",
//                                                           "CapAppearExEnt",
//                                                           "WanwanExStart",
//                                                           "bou",
//                                                           "Explorer_Bonus",
//                                                           "ddd",
//                                                           "EX_IceWaterDash",
//                                                           "abc",
//                                                           "CapAppearExExit",
//                                                           "BossRaidB",
//                                                           "jizo02",
//                                                           "LavaLiftEx",
//                                                           "EX_SkyBonus",
//                                                           "BikeSteelNoCapEx_Exit",
//                                                           "imomu_02",
//                                                           "meganelift02",
//                                                           "Jyukai001v",
//                                                           "ggg",
//                                                           "taxireturn",
//                                                           "shop_dress",
//                                                           "room2_start",
//                                                           "room3_start",
//                                                           "sora001",
//                                                           "TreasureEventWorldSea",
//                                                           "arijigoku2",
//                                                           "rocket",
//                                                           "CapTrampolineA",
//                                                           "EX_IceWater_Exit",
//                                                           "Jyukai001",
//                                                           "LakeWorldShop",
//                                                           "EX_RailCollision",
//                                                           "bonus",
//                                                           "kaitendokan",
//                                                           "shindo",
//                                                           "PushBlockExStageEnt",
//                                                           "pyramid01",
//                                                           "densendokan",
//                                                           "EX_RailCol2",
//                                                           "StackerRoomGoal",
//                                                           "Ex",
//                                                           "icestart",
//                                                           "PechoBubbleExDokan",
//                                                           "PechoBubbleEx",
//                                                           "onpu",
//                                                           "PeachWorldEx1a",
//                                                           "SeaWorldEX3b",
//                                                           "EX_Tankuro_Exit",
//                                                           "PackunPoisonNoCapEx_Exit",
//                                                           "SeaWorldMoonEX2",
//                                                           "meganelift01",
//                                                           "PushBlockExStageEntDokan",
//                                                           "GiantWanderBossA",
//                                                           "moon_exit",
//                                                           "Goal",
//                                                           "car",
//                                                           "bar2",
//                                                           "RaceTrackExit",
//                                                           "ShootingCityYoshiEx",
//                                                           "bike02return",
//                                                           "shindo_Lv2",
//                                                           "room2_goal",
//                                                           "EX_IceWater",
//                                                           "ShopDoor",
//                                                           "ByugoPuzzle",
//                                                           "EX_Water_Exit",
//                                                           "BombTailRoomStart",
//                                                           "taxi",
//                                                           "densen",
//                                                           "ClashWorldMoonEX2",
//                                                           "GabuzouClockEx",
//                                                           "MartinCubeEx",
//                                                           "FireBlowerRoomStart",
//                                                           "ccc",
//                                                           "PeachCastleGate",
//                                                           "SenobiTowerYoshiEx",
//                                                           "RexPoppunEx",
//                                                           "PukupukuCaveGoal",
//                                                           "tukkun001_enter",
//                                                           "gragrareturn",
//                                                           "EX_Water",
//                                                           "WanwanExGoal",
//                                                           "Lift2DExit",
//                                                           "theater",
//                                                           "Yadokari00",
//                                                           "BossForestA",
//                                                           "imomu_01",
//                                                           "PeachWorldShopA",
//                                                           "rollinggoal",
//                                                           "EX_2DHosui_Exit",
//                                                           "LavaLiftExdokan",
//                                                           "GabuzouClockExdokan",
//                                                           "anki2",
//                                                           "boss001",
//                                                           "FastenerEx",
//                                                           "FigureWalker",
//                                                           "shop",
//                                                           "shop_lava",
//                                                           "CapThrowerRoomStart",
//                                                           "ForkEX2",
//                                                           "arijigoku1",
//                                                           "CapThrowerRoomGoal",
//                                                           "Jyukai003v",
//                                                           "pyramid04",
//                                                           "doukutu2",
//                                                           "PoisonWaveExExit",
//                                                           "SnowUGEnt",
//                                                           "BossMagmaA",
//                                                           "KillerRoad",
//                                                           "PoisonEx",
//                                                           "BossRaidWorldMoonEx02_Enter",
//                                                           "bonus2",
//                                                           "SnowUGExit",
//                                                           "BossRaidA",
//                                                           "Jyukai004",
//                                                           "Out",
//                                                           "EX_IceWaterDash_Exit",
//                                                           "hide",
//                                                           "byoubu",
//                                                           "shop_coin",
//                                                           "fff",
//                                                           "bike",
//                                                           "PukupukuCaveStart",
//                                                           "boss002",
//                                                           "GiantWanderBossB",
//                                                           "MoonGoal",
//                                                           "Patakaron02",
//                                                           "doukutu1",
//                                                           "gunsyudokan",
//                                                           "room3_goal",
//                                                           "EX_2DHosui",
//                                                           "run00return",
//                                                           "WindBlowExGoal",
//                                                           "EX_Tankuro",
//                                                           "RoomEventWorldSea",
//                                                           "LavaWorldUpDownYoshiEx",
//                                                           "rollingstart",
//                                                           "bikereturn",
//                                                           "shop_corect",
//                                                           "EX_RailCol2_Exit",
//                                                           "Under01",
//                                                           "SeaWorldEX3a",
//                                                           "TreasureEventWorldLava",
//                                                           "TreasureTree",
//                                                           "CostumeEventWorldLava",
//                                                           "tukkun000_exit",
//                                                           "gragra",
//                                                           "PictureBossRaid",
//                                                           "PictureGiantWanderBoss",
//                                                           "biru",
//                                                           "birureturn",
//                                                           "EX_Mist",
//                                                           "tenjo",
//                                                           "tenjo2",
//                                                           "SeaWorldEX2",
//                                                           "BossRaidWorldEx01_Eixt",
//                                                           "BossRaidWorldEx01_Eixt2",
//                                                           "BikeSteelNoCapEx",
//                                                           "PackunPoisonNoCapEx",
//                                                           "KillerRoadNoCapEx",
//                                                           "PictureBossMagma",
//                                                           "PictureMofumofu",
//                                                           "PictureBossForest",
//                                                           "PictureBossKnuckle",
//                                                           "Fukuwarai2",
//                                                           "CostumeEventWorldPeach",
//                                                           "SeaWorldEX2Return",
//                                                           "aaaSand"};

// // Used for Cappy Message Building
// __attribute__((used)) static const char* SMOItems[] = {"Connect",  "Power Moon", "Multi-Moon", "Regional Coin", "Cap",      "Clothes",  "Sticker",
//                                                        "Souvenir", "Capture",    "Moon Rock",  "Ability",       "Missing1", "Missing2", "Coins"};
// __attribute__((used)) static const char* kingdoms[] = {"Cap",  "Cascade",  "Sand",   "Wooded", "Lake", "Lost",     "Cloud",     "Metro",      "Seaside",
//                                                        "Snow", "Luncheon", "Ruined", "Bowser", "Moon", "Mushroom", "Dark Side", "Darker Side"};

// __attribute__((used)) static const char* abilities[] = {"Jump",
//                                                         "Double Jump",
//                                                         "Triple Jump",
//                                                         "Side Flip",
//                                                         "Back Flip",
//                                                         "Long Jump",
//                                                         "Wall Jump",
//                                                         "Spin",
//                                                         "Spin Jump",
//                                                         "Ground Pound",
//                                                         "Ground Pound Jump",
//                                                         "Cap Throw",
//                                                         "Up Throw",
//                                                         "Down Throw",
//                                                         "Spin Throw",
//                                                         "Crouch",
//                                                         "Roll",
//                                                         "Roll Boost",
//                                                         "Vault",
//                                                         "Dive",
//                                                         "Climb",
//                                                         "Ledge Grab",
//                                                         "Cappy Return Jump",
//                                                         "Jaxi",
//                                                         "Scooter"};
// __attribute__((used)) static const char16_t regionalIcons1[] = {0x0010, 0x0011, 0x0012, 0x0013, 0x0014, 0x0000, 0x0015, 0x0016, 0x0017,
//                                                                 0x0018, 0x0019, 0x0000, 0x001a, 0x001b, 0x001c, 0x001b, 0x001b};
// __attribute__((used)) static const char16_t regionalIcons2[] = {
//     0x0050, 0x0051, 0x0052, 0x0053, 0x0054, 0x0001, 0x0055, 0x0056, 0x0057, 0x0058, 0x0059, 0x0001, 0x005a, 0x005b, 0x005c, 0x005b, 0x005b,
// };

// __attribute__((used)) static const char16_t miscIcons1[] = {0x0031, 0xf777};
// // Lifeup, Coin

// __attribute__((used)) static const char16_t miscIcons2[] = {0x0037, 0x0040};

// __attribute__((used)) static const char16_t color1[] = {0x0000, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0xffff};
// __attribute__((used)) static const char16_t color2[] = {0x0006, 0x0005, 0x0004, 0x0005, 0x0005, 0x0004, 0x0005, 0x0000, 0x0000};

// __attribute__((used)) static const char16_t colors[] = {0xf777, 0x0001, 0x0002, 0x0003, 0x0004, 0x0005, 0x0006, 0x0007, 0xffff};

// enum ProjectTextColors {
//     Black = 0,
//     Yellow = 1,
//     White = 2,
//     Red = 3,
//     Green = 4,
//     Blue = 5,
//     Gray = 6,
//     Rough = 7,
//     ResetColor = 8,
// };

// __attribute__((used)) static const char* itemSystemMessageFiles[] = {
//     "ItemCap",
//     "ItemCloth",
//     "ItemSticker",
//     "ItemGift",
//     "HackList"
//     "ItemMoon",
// };

struct stageConnection {
    short toStageIdIndex;
    short toStageNameIndex;
};

struct replaceText {
    u8 slotNameIndex;
    u8 itemNameIndex;
};

struct shopReplaceText {
    u8 gameIndex;
    u8 slotNameIndex;
    u8 itemNameIndex;
    u8 itemClassification;
};

enum CappyMessageTypes
{
    CappyConnect = 0,
    CappyMoon = 1,
    CappyMultiMoon = 2,
    CappyRegionalCoin = 3,
    CappyCap = 4,
    CappyClothes = 5,
    CappySticker = 6,
    CappySouvenir = 7,
    CappyCapture = 8,
    CappyMoonRock = 9,
    CappyAbility = 10,
    CappyHealthUpgrade = 11,
    CappyWalletUpgrade = 12,
    CappyCoins = 13,
    CappySent = 14,
};

struct cappyMessage {
    u8 slotNameIndex;
    u8 itemType;
    u8 itemIndex;
    bool isOutgoing;
};

struct scenarioShine {
    u8 worldId;
    short shineUid;
    u8 scenario;
};

__attribute__((used)) static const scenarioShine shineScenarios[] = { { 1, 218, 2 }, { 2, 495, 2 }, { 2, 560, 3 }, { 3, 130, 2 }, { 3, 181, 3 },
    { 4, 424, 2 }, { 7, 130, 2 }, { 7, 181, 3 }, { 8, 437, 2 }, { 9, 1020, 2 },
    { 10, 292, 2 }, { 10, 290, 3 }, { 11, 795, 2 }, { 12, 332, 2 } };

__attribute__((used)) static const short capStory[] = {};
__attribute__((used)) static const short cascadeStory[] = { 205, 218 };
__attribute__((used)) static const short sandStory[] = { 497, 496, 495, 560 };
__attribute__((used)) static const short woodedStory[] = { 129, 130, 159, 181 };
__attribute__((used)) static const short lakeStory[] = { 424 };
__attribute__((used)) static const short cloudStory[] = { -1 }; // Post Bowser
__attribute__((used)) static const short lostStory[] = { -2 }; // Post Klepto
__attribute__((used)) static const short metroStory[] = { 37, 41, 42, 43, 44, 95 };
__attribute__((used)) static const short seasideStory[] = { 438, 439, 440, 441, 437 };
__attribute__((used)) static const short snowStory[] = { 17, 18, 22, 25, 1020 };
__attribute__((used)) static const short luncheonStory[] = { 291, 251, 292, 290 };
__attribute__((used)) static const short ruinedStory[] = { 795 };
__attribute__((used)) static const short bowserStory[] = { 325, 334, 314, 332 };
__attribute__((used)) static const short moonStory[] = { -3 }; // Reach with Odyssey?

__attribute__((used)) static const u8 storyLengths[] = { 0, 2, 4, 4, 1, 1, 1, 6, 5, 5, 4, 1, 4, 1 };

__attribute__((used)) static const short* storyShines[] = {
    capStory,
    cascadeStory,
    sandStory,
    woodedStory,
    lakeStory,
    cloudStory,
    lostStory,
    metroStory,
    seasideStory,
    snowStory,
    luncheonStory,
    ruinedStory,
    bowserStory,
    moonStory,
};

bool isInApCostumeList(const char* costumeName);
int getIndexApCostumeList(const char* costumeName);

int getIndexStickerList(const char* stickerName);
int getIndexSouvenirList(const char* souvenirName);
int getIndexCaptureList(const char* captureName);
int getIndexMoonItemList(const char* moonItemName);

int getIndexStageNameList(const char* stageName);
int getIndexStageIdList(const char* stageId);

int getIndexRegionalCoinStageList(const char* stageName);
int getIndexRegionalCoinId(const char* stageName, const char* placementId);
int getRelativeIndexRegionalCoinId(const char* stageName, const char* placementId);
int getRegionalCoinLocationId(const char* stageName, const char* placementId);
const char* getWorldStageNameByRegionalCoinStageList(const char* stageName);
int getIndexRegionalItemList(int worldId, const char* itemName);

const char* intToCstr(int number);
const char16_t* utf8ToUtf16(const char* src);
void appendUtf8ToUtf16(const char* src, sead::WFixedSafeString<64 * 3>* dest);
const char16_t* getRegionalCoinIcon(int worldId);
void getColor(ProjectTextColors color, sead::WBufferedSafeString* str);
const char16_t* getMiscIcon(int type);
