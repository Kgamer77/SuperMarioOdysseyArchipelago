#pragma once

// Auto-generated from BgmStageInfoList.byml + BgmResourceInfoList.byml
// Static database of all stage→BGM resource mappings used by MusicRando.
//
// Two separate pools are eligible for shuffling:
//
//   POOL 1 – Home-stage WorldMain tracks (one per scenario)
//     These are the big overworld kingdom themes that play while exploring.
//     Shuffling these swaps which kingdom music plays in which kingdom.
//
//   POOL 2 – Sub-stage "Stage" slot tracks
//     These are the music tracks for challenge rooms / sub-stages.
//     Many sub-stages share generic tracks (ExChika, ExPsyche …) so the
//     shuffle is intentionally kept inside each generic-group to avoid
//     dead-silent stages or resource load failures.
//
// Entries flagged MusicFlag_NeverShuffle are structural (dummy, jingles,
// boss) and are always left untouched.

#include <basis/seadTypes.h>
#include <cstring>

namespace MusicDB {

    enum MusicFlags : u8
    {
        MusicFlag_None = 0,
        MusicFlag_NeverShuffle = 1 << 0, // boss / jingle / dummy – skip
        MusicFlag_WorldMain = 1 << 1, // overworld main theme
        MusicFlag_SubStage = 1 << 2, // sub-stage "Stage" slot
    };

    // ─────────────────────────────────────────────────────────────────────────
    // WorldMain pool — one record per (stage, scenarioNo) pair.
    // The resource that plays in the WorldMain BGM slot for that scenario.
    // ─────────────────────────────────────────────────────────────────────────
    struct WorldMainEntry {
        const char* stage;
        s32 scenarioNo;
        const char* resourceName; // original; overwritten at runtime by remap
        u8 flags;
    };

    // clang-format off
    static constexpr WorldMainEntry sWorldMainTable[] = {
        // ── Cap ───────────────────────────────────────────────────────────────
        { "CapWorldHomeStage",          0,  "StmRsBgmHat",                MusicFlag_WorldMain },

        // ── Cascade ───────────────────────────────────────────────────────────
        { "WaterfallWorldHomeStage",    0,  "StmRsBgmFall",               MusicFlag_WorldMain },

        // ── Sand ──────────────────────────────────────────────────────────────
        { "SandWorldHomeStage",         0,  "StmRsBgmDesertMain",         MusicFlag_WorldMain },
        { "SandWorldHomeStage",         2,  "StmRsBgmDesertNight",        MusicFlag_WorldMain },
        { "SandWorldHomeStage",         10, "StmRsBgmDesertNight",        MusicFlag_WorldMain },

        // ── Wooded ────────────────────────────────────────────────────────────
        { "ForestWorldHomeStage",       0,  "StmRsBgmForest",             MusicFlag_WorldMain },
        { "ForestWorldTowerStage",      0,  "StmRsBgmForest",             MusicFlag_WorldMain },

        // ── Lake ──────────────────────────────────────────────────────────────
        { "LakeWorldHomeStage",         0,  "StmRsBgmLakeScenario2",      MusicFlag_WorldMain },
        { "LakeWorldHomeStage",         1,  "StmRsBgmLakeScenario1",      MusicFlag_WorldMain },

        // ── Lost ──────────────────────────────────────────────────────────────
        { "ClashWorldHomeStage",        0,  "StmRsBgmClashSecond",        MusicFlag_WorldMain },

        // ── Metro ─────────────────────────────────────────────────────────────
        { "CityWorldHomeStage",         0,  "StmRsBgmCityScenario04",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         1,  "StmRsBgmCityScenario01",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         2,  "StmRsBgmCityScenario02",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         3,  "StmRsBgmCityScenario03",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         7,  "StmRsBgmCityScenario03",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         11, "StmRsBgmCityScenario03Special", MusicFlag_WorldMain },
        { "CityWorldHomeStage",         12, "StmRsBgmCityScenario02",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         13, "StmRsBgmCityScenario02",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         14, "StmRsBgmCityScenario03",     MusicFlag_WorldMain },
        { "CityWorldHomeStage",         15, "StmRsBgmCityScenario01",     MusicFlag_WorldMain },
        { "CityWorldFactoryStage",      0,  "StmRsBgmExChika",            MusicFlag_WorldMain },

        // ── Seaside ───────────────────────────────────────────────────────────
        { "SeaWorldHomeStage",          0,  "StmRsBgmSea02",              MusicFlag_WorldMain },
        { "SeaWorldHomeStage",          1,  "StmRsBgmSea01",              MusicFlag_WorldMain },
        { "SeaWorldUtsuboCaveStage",    0,  "StmRsBgmLakeTunnel",         MusicFlag_WorldMain },

        // ── Snow ──────────────────────────────────────────────────────────────
        // sc=0 uses StmSnowWorld (an ambient layered stream, not a StmRs* BGM)
        // so we only shuffle sc=1 here; the ambient stream is left alone.
        { "SnowWorldHomeStage",         1,  "StmRsBgmSnowTownJingleBell", MusicFlag_WorldMain },

        // ── Luncheon ──────────────────────────────────────────────────────────
        { "LavaWorldHomeStage",         0,  "StmRsBgmLava",               MusicFlag_WorldMain },

        // ── Bowser's ──────────────────────────────────────────────────────────
        { "SkyWorldHomeStage",          0,  "StmRsBgmSky01",              MusicFlag_WorldMain },

        // ── Moon ──────────────────────────────────────────────────────────────
        { "MoonWorldHomeStage",         0,  "StmRsBgmMoon",               MusicFlag_WorldMain },

        // ── Darker Side ───────────────────────────────────────────────────────
        { "Special2WorldHomeStage",     0,  "StmRsBgmCityScenario03",     MusicFlag_WorldMain },
        { "Special2WorldLavaStage",     0,  "StmRsBgmFall",               MusicFlag_WorldMain },
        { "Theater2DExStage",           0,  "StmRsBgmM1PL",               MusicFlag_WorldMain },
    };
    // clang-format on

    static constexpr s32 sWorldMainTableSize = sizeof(sWorldMainTable) / sizeof(sWorldMainTable[0]);

    // ─────────────────────────────────────────────────────────────────────────
    // SubStage pool — one record per (stage, "Stage" slot) at scenarioNo 0.
    // These are shuffled within groups that share the same original resource
    // so we never accidentally try to load a resource that isn't streamed
    // in the stage's asset bundle.
    // ─────────────────────────────────────────────────────────────────────────
    struct SubStageEntry {
        const char* stage;
        const char* resourceName; // original value from BYML
        u8 flags;
    };

    // clang-format off
    static constexpr SubStageEntry sSubStageTable[] = {
        // ── ExChika group (underground / cave / pipe) ─────────────────────────
        { "CapAppearLavaLiftExStage",   "StmRsBgmExChika",   MusicFlag_SubStage },
        { "Cube2DExStage",              "StmRsBgmExChika",   MusicFlag_SubStage },
        { "ForestWorldWaterExStage",    "StmRsBgmExChika",   MusicFlag_SubStage },
        { "Galaxy2DExStage",            "StmRsBgmExChika",   MusicFlag_SubStage },
        { "Note2D3DRoomExStage",        "StmRsBgmExChika",   MusicFlag_SubStage },
        { "PackunPoisonExStage",        "StmRsBgmExChika",   MusicFlag_SubStage },
        { "PackunPoisonNoCapExStage",   "StmRsBgmExChika",   MusicFlag_SubStage },
        { "RailCollisionExStage",       "StmRsBgmExChika",   MusicFlag_SubStage },
        { "RocketFlowerExStage",        "StmRsBgmExChika",   MusicFlag_SubStage },
        { "ShootingElevatorExStage",    "StmRsBgmExChika",   MusicFlag_SubStage },
        { "TrexPoppunExStage",          "StmRsBgmExChika",   MusicFlag_SubStage },
        { "WanwanClashExStage",         "StmRsBgmExChika",   MusicFlag_SubStage },
        { "WaterTubeExStage",           "StmRsBgmExChika",   MusicFlag_SubStage },
        { "WaterValleyExStage",         "StmRsBgmExChika",   MusicFlag_SubStage },

        // ── ExChika2 group (ruin maze / frog) ────────────────────────────────
        { "ByugoPuzzleExStage",         "StmRsBgmExChika2",  MusicFlag_SubStage },
        { "GotogotonExStage",           "StmRsBgmExChika2",  MusicFlag_SubStage },
        { "SandWorldSphinxExStage",     "StmRsBgmExChika2",  MusicFlag_SubStage },

        // ── ExPsyche group (psychedelic / 2D) ────────────────────────────────
        { "BullRunExStage",             "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "CapAppearExStage",           "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "CloudExStage",               "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "ElectricWireExStage",        "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "FogMountainExStage",         "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "FrogPoisonExStage",          "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "JangoExStage",               "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "KaronWingTowerStage",        "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "KillerRoadExStage",          "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "KillerRoadNoCapExStage",     "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "LavaWorldFenceLiftExStage",  "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "MoonAthleticExStage",        "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "PushBlockExStage",           "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "SandWorldRotateExStage",     "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "SenobiTowerExStage",         "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "SenobiTowerYoshiExStage",    "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "TogezoRotateExStage",        "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "TrampolineWallCatchExStage", "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "TsukkunRotateExStage",       "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "WindBlowExStage",            "StmRsBgmExPsyche",  MusicFlag_SubStage },
        { "YoshiCloudExStage",          "StmRsBgmExPsyche",  MusicFlag_SubStage },

        // ── ExAthletic01 group (athletic 1 – upbeat) ─────────────────────────
        { "AnimalChaseExStage",         "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "BikeSteelExStage",           "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "BikeSteelNoCapExStage",      "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "ForkExStage",                "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "IceWaterDashExStage",        "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "LavaWorldClockExStage",      "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "LavaWorldExcavationExStage", "StmRsBgmExAthletic01", MusicFlag_SubStage },
        { "RollingExStage",             "StmRsBgmExAthletic01", MusicFlag_SubStage },

        // ── ExAthletic02 group (athletic 2 – tense) ──────────────────────────
        { "CapRotatePackunExStage",     "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "CityPeopleRoadStage",        "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "DotHardExStage",             "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "FastenerExStage",            "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "GabuzouClockExStage",        "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "ImomuPoisonExStage",         "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "JizoSwitchExStage",          "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "LavaWorldBubbleLaneExStage", "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "LavaWorldCostumeStage",      "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "LavaWorldUpDownExStage",     "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "LavaWorldUpDownYoshiExStage","StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "Lift2DExStage",              "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "PoleGrabCeilExStage",        "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "PoleKillerExStage",          "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "ReflectBombExStage",         "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "SnowWorldCostumeStage",      "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "SwingSteelExStage",          "StmRsBgmExAthletic02", MusicFlag_SubStage },
        { "TsukkunClimbExStage",        "StmRsBgmExAthletic02", MusicFlag_SubStage },

        // ── ExRuin group (pyramid / ruins) ────────────────────────────────────
        { "DonsukeExStage",             "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "DotTowerExStage",            "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "MeganeLiftExStage",          "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "SandWorldKillerExStage",     "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "SandWorldMeganeExStage",     "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "SandWorldPyramid000Stage",   "StmRsBgmExRuin",    MusicFlag_SubStage },
        { "SandWorldPyramid001Stage",   "StmRsBgmExRuin",    MusicFlag_SubStage },

        // ── ExCold group (ice / snow) ─────────────────────────────────────────
        { "IceWaterBlockExStage",       "StmRsBgmExCold",    MusicFlag_SubStage },
        { "KillerRailCollisionExStage", "StmRsBgmExCold",    MusicFlag_SubStage },
        { "SandWorldPressExStage",      "StmRsBgmExCold",    MusicFlag_SubStage },
        { "SandWorldUnderground000Stage","StmRsBgmExCold",   MusicFlag_SubStage },

        // ── ExHat group (Cap / Ghost house) ──────────────────────────────────
        { "CapWorldTowerStage",         "StmRsBgmExHat",     MusicFlag_SubStage },
        { "FrogSearchExStage",          "StmRsBgmExHat",     MusicFlag_SubStage },
        { "PoisonWaveExStage",          "StmRsBgmExHat",     MusicFlag_SubStage },

        // ── ExBonus group (cloud bonus) ───────────────────────────────────────
        { "ForestWorldCloudBonusExStage","StmRsBgmExBonus",  MusicFlag_SubStage },
        { "SkyWorldCloudBonusExStage",  "StmRsBgmExBonus",   MusicFlag_SubStage },
        { "SnowWorldCloudBonusExStage", "StmRsBgmExBonus",   MusicFlag_SubStage },

        // ── Singletons (unique themes – shuffled 1-for-1 across group) ────────
        // These each have their own group of one, so they are always played
        // at the same stage; listed here so the override table covers them.
        { "LavaWorldTreasureStage",     "StmRsBgmLava",          MusicFlag_SubStage },
        { "MoonWorldSphinxRoom",        "StmRsBgmMoon",          MusicFlag_SubStage },
        { "MoonWorldCaptureParadeStage","StmRsBgmMoonDungeon",   MusicFlag_SubStage },
        { "MoonWorldWeddingRoomStage",  "StmRsBgmMoonChurch",    MusicFlag_SubStage },
        { "MoonWorldWeddingRoom2Stage", "StmRsBgmMoonChurch",    MusicFlag_SubStage },
        { "PeachWorldCastleStage",      "StmRsBgmPeachCastle",   MusicFlag_SubStage },
        { "SeaWorldSecretStage",        "StmRsBgmLakeTunnel",    MusicFlag_SubStage },
        { "SeaWorldSneakingManStage",   "StmRsBgmLakeTunnel",    MusicFlag_SubStage },
        { "SkyWorldCostumeStage",       "StmRsBgmSky01",         MusicFlag_SubStage },
        { "SkyWorldTreasureStage",      "StmRsBgmSky02",         MusicFlag_SubStage },
        { "ShootingCityExStage",        "StmRsBgmCityScenario01Strain", MusicFlag_SubStage },
        { "ShootingCityYoshiExStage",   "StmRsBgmCityScenario01Strain", MusicFlag_SubStage },
        { "SnowWorldLobby000Stage",     "StmRsBgmSnowRaceEntrance", MusicFlag_SubStage },
        { "SnowWorldLobby001Stage",     "StmRsBgmSnowRaceEntrance", MusicFlag_SubStage },
        { "SnowWorldLobbyExStage",      "StmRsBgmSnowRaceEntrance", MusicFlag_SubStage },
    };
    // clang-format on

    static constexpr s32 sSubStageTableSize = sizeof(sSubStageTable) / sizeof(sSubStageTable[0]);

    // Extra resources found in the decomp string table / BgmResourceInfoList.
    // These are not always tied to one stable stage entry in the current table,
    // but they are valid stage music resources and make runtime request remaps
    // cover more areas and mid-stage changes.
    static constexpr const char* sKnownResourceTable[] = {
        "StmRsBgmHat",
        "StmRsBgmFall",
        "StmRsBgmDinosaur",
        "StmRsBgmDesertMain",
        "StmRsBgmDesertNight",
        "StmRsBgmDesertTown",
        "StmRsBgmJaguar",
        "StmRsBgmForest",
        "StmRsBgmLakeScenario1",
        "StmRsBgmLakeScenario2",
        "StmRsBgmLakeTunnel",
        "StmRsBgmClashFirst",
        "StmRsBgmClashSecond",
        "StmRsBgmCityScenario01",
        "StmRsBgmCityScenario01Strain",
        "StmRsBgmCityScenario02",
        "StmRsBgmCityCafe01",
        "StmRsBgmCityScenario03",
        "StmRsBgmCityScenario03Jp",
        "StmRsBgmCityScenario03Special",
        "StmRsBgmCityScenario04",
        "StmRsBgmSea01",
        "StmRsBgmSea01beach",
        "StmRsBgmSea02",
        "StmRsBgmSnowTown",
        "StmRsBgmSnowTownJingleBell",
        "StmRsBgmSnowRaceEntrance",
        "StmRsBgmLava",
        "StmRsBgmLavaTown",
        "StmRsBgmSky01",
        "StmRsBgmSky02",
        "StmRsBgmMoon",
        "StmRsBgmMoonDungeon",
        "StmRsBgmMoonChurch",
        "StmRsBgmPeachCastle",
        "StmRsBgmExAthletic01",
        "StmRsBgmExAthletic02",
        "StmRsBgmExChika",
        "StmRsBgmExChika2",
        "StmRsBgmExHat",
        "StmRsBgmExCold",
        "StmRsBgmExPsyche",
        "StmRsBgmExRuin",
        "StmRsBgmExBonus",

        // Collectible Music List additions
        "StmRsBgmBeforeGK",
        "StmRsBgmCitySession",
        "StmRsBgmDemoWorldMap",
        "StmRsBgmEndRock",
        "StmRsBgmEndRockJp",
        "StmRsBgmEndRockClct",
        "StmRsBgmCityScenario03Clct",
        "StmRsBgmDesertTownClct",
        "StmRsBgmKinopioBrigade",
        "StmRsBgmExFantasy",
        "StmRsBgmM1UG",
        "StmRsBgmRace",
        "StmRsBgmRadioControlCar",
        "StmRsBgmMiniGame01",
        "StmRsBgmSlotRoom",
        "StmRsBgmMoonEscape",
        "StmRsBgmRaidWorld",
        "StmRsBgmShop",
        "StmRsBgmShopClct",

        // Additions
        "StmRsBgmStaffRoll",
        "StmRsBgmTitle",
        "StmRsBgmE3Title",
        "StmRsBgmE3Title2",
        "StmRsBgmDemoOpening",
        "StmRsBgmDesertMainParade",
        "StmRsBgmMoonParade",
        "StmRsBgmRaceHurry",
        "StmRsBgmRaceResult",

        // 8-Bit Pseudo-Resources
        "StmRsBgmFall_8Bit",
        "StmRsBgmDesertMain_8Bit",
        "StmRsBgmDesertNight_8Bit",
        "StmRsBgmForest_8Bit",
        "StmRsBgmLakeScenario1_8Bit",
        "StmRsBgmClashSecond_8Bit",
        "StmRsBgmSea02_8Bit",
        "StmRsBgmLava_8Bit",
        "StmRsBgmSky01_8Bit",
        "StmRsBgmMoon_8Bit",
        "StmRsBgmMoonEscape_8Bit",
        "StmRsBgmExAthletic02_8Bit",
        "StmRsBgmExChika_8Bit",
        "StmRsBgmExRuin_8Bit",
    };

    static constexpr s32 sKnownResourceTableSize = sizeof(sKnownResourceTable) / sizeof(sKnownResourceTable[0]);

    // Boss music resources — kept in the same unified pool as everything else.
    // BossHaikai_A is followed by BossHaikai_B by the music playback hooks.
    static constexpr const char* sBossResourceTable[] = {
        "StmRsBgmBossBreeda",
        "StmRsBgmBossDragon",
        "StmRsBgmBossForest",
        "StmRsBgmBossGolem",
        "StmRsBgmBossHaikai_B",
        "StmRsBgmBossKoopa01",
        "StmRsBgmBossKoopa02",
        "StmRsBgmBossGateKeeper",
        "StmRsBgmBossHaikai_A",
        "StmRsBgmBossKnuckle",
        "StmRsBgmBossMagma",
        "StmRsBgmBossMofumofu",
        "StmRsBgmBossHaikai_B_8Bit",
    };

    static constexpr s32 sBossResourceTableSize = sizeof(sBossResourceTable) / sizeof(sBossResourceTable[0]);

    // ─────────────────────────────────────────────────────────────────────────
    // Optional pool filters — the two checkboxes on the Music Randomization
    // page carve these subsets back out of the unified pool.
    //
    // "2D themes" are the 8-bit pseudo-resources above. Drawing one makes the
    // stage play its In2DArea variant instead (see MusicHooks), so they are
    // recognised by the "_8Bit" name suffix rather than by a table.
    //
    // "Extra themes" are the tracks that are not a kingdom overworld or a
    // sub-area theme: titles, demos, credits, parades, minigames, and the
    // pause-menu music-list ("Clct") variants. They are only in the pool at
    // all because it was widened past the core stage music, so they get their
    // own toggle. Everything else stays core — the kingdom and Ex* themes,
    // the Ruined Kingdom theme (StmRsBgmRaidWorld) and the Crazy Cap shop theme.
    //
    // "Boss themes" are sBossResourceTable above, filtered the same way.
    // ─────────────────────────────────────────────────────────────────────────
    static constexpr const char* sExtraResourceTable[] = {
        // Titles / demos / cutscene cues
        "StmRsBgmTitle",
        "StmRsBgmE3Title",
        "StmRsBgmE3Title2",
        "StmRsBgmDemoOpening",
        "StmRsBgmDemoWorldMap",
        "StmRsBgmBeforeGK",

        // Ending / credits
        "StmRsBgmEndRock",
        "StmRsBgmEndRockJp",
        "StmRsBgmStaffRoll",

        // Festival / parade one-offs
        "StmRsBgmCitySession", // Jump Up, Super Star!
        "StmRsBgmDesertMainParade",
        "StmRsBgmMoonParade",
        "StmRsBgmMoonEscape",
        "StmRsBgmKinopioBrigade",
        "StmRsBgmM1UG",

        // Minigames / races
        "StmRsBgmRace",
        "StmRsBgmRaceHurry",
        "StmRsBgmRaceResult",
        "StmRsBgmRadioControlCar",
        "StmRsBgmMiniGame01",
        "StmRsBgmSlotRoom",

        // Pause-menu music-list arrangements of tracks that already have a
        // stage version in the pool.
        "StmRsBgmEndRockClct",
        "StmRsBgmCityScenario03Clct",
        "StmRsBgmDesertTownClct",
        "StmRsBgmShopClct",
    };

    static constexpr s32 sExtraResourceTableSize = sizeof(sExtraResourceTable) / sizeof(sExtraResourceTable[0]);

    // Stable, player-facing theme groups used by the Music Themes advanced
    // page. resourceName is a prefix rather than a single asset name: the
    // kingdom entries cover their scenario/8-bit variants and the final
    // catch-all keeps newly discovered StmRsBgm resources controllable without
    // changing the serialized mask. Never reorder or reuse an id; the mask is
    // stored in rando.bin.
    struct MusicTheme {
        u16 id;
        const char* resourceName;
    };

    // clang-format off
    static constexpr MusicTheme sThemes[] = {
        { 0, "StmRsBgmHat" },
        { 1, "StmRsBgmFall" },
        { 2, "StmRsBgmDinosaur" },
        { 3, "StmRsBgmDesert" },
        { 4, "StmRsBgmForest" },
        { 5, "StmRsBgmLake" },
        { 6, "StmRsBgmClash" },
        { 7, "StmRsBgmCity" },
        { 8, "StmRsBgmSea" },
        { 9, "StmRsBgmSnow" },
        { 10, "StmRsBgmLava" },
        { 11, "StmRsBgmSky" },
        { 12, "StmRsBgmMoon" },
        { 13, "StmRsBgmPeach" },
        { 14, "StmRsBgmExChika2" },
        { 15, "StmRsBgmExChika" },
        { 16, "StmRsBgmExPsyche" },
        { 17, "StmRsBgmExAthletic01" },
        { 18, "StmRsBgmExAthletic02" },
        { 19, "StmRsBgmExRuin" },
        { 20, "StmRsBgmExCold" },
        { 21, "StmRsBgmExHat" },
        { 22, "StmRsBgmExBonus" },
        { 23, "StmRsBgmTitle" },
        { 24, "StmRsBgmDemo" },
        { 25, "StmRsBgmEnd" },
        { 26, "StmRsBgmRace" },
        { 27, "StmRsBgmMiniGame" },
        { 28, "StmRsBgmBoss" },
        { 29, "StmRsBgmShop" },
        { 30, "StmRsBgm" },
    };
    // clang-format on
    static constexpr s32 sThemeCount = sizeof(sThemes) / sizeof(sThemes[0]);

    // ─────────────────────────────────────────────────────────────────────────
    // Helpers
    // ─────────────────────────────────────────────────────────────────────────

    inline bool isTwoDResource(const char* resourceName) {
        return resourceName && strstr(resourceName, "_8Bit") != nullptr;
    }

    inline bool isExtraResource(const char* resourceName) {
        if (!resourceName)
            return false;
        for (s32 i = 0; i < sExtraResourceTableSize; i++)
            if (strcmp(sExtraResourceTable[i], resourceName) == 0)
                return true;
        return false;
    }

    inline bool isBossResource(const char* resourceName) {
        if (!resourceName)
            return false;
        for (s32 i = 0; i < sBossResourceTableSize; i++)
            if (strcmp(sBossResourceTable[i], resourceName) == 0)
                return true;
        return false;
    }

    inline s32 themeIndexForResource(const char* resourceName) {
        if (!resourceName)
            return -1;
        for (s32 i = 0; i < sThemeCount; i++) {
            const char* prefix = sThemes[i].resourceName;
            const size_t prefixLength = std::strlen(prefix);
            if (std::strncmp(resourceName, prefix, prefixLength) == 0)
                return sThemes[i].id;
        }
        return -1;
    }

    inline const WorldMainEntry* findWorldMain(const char* stage, s32 scenarioNo) {
        for (s32 i = 0; i < sWorldMainTableSize; i++) {
            if (sWorldMainTable[i].scenarioNo == scenarioNo && strcmp(sWorldMainTable[i].stage, stage) == 0)
                return &sWorldMainTable[i];
        }
        return nullptr;
    }

    inline const SubStageEntry* findSubStage(const char* stage) {
        for (s32 i = 0; i < sSubStageTableSize; i++) {
            if (strcmp(sSubStageTable[i].stage, stage) == 0)
                return &sSubStageTable[i];
        }
        return nullptr;
    }

} // namespace MusicDB
