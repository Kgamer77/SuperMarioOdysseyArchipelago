#pragma once

#include <cstring>

namespace EntranceFallbacks {

    using ScenarioMask = unsigned int;

    enum Mode : unsigned char
    {
        Mode_Vanilla,
        Mode_Coordinates,
        Mode_Pipe,
        Mode_Moon_Pipe,
        Mode_Odyssey,
    };

    struct Vec3 {
        float x;
        float y;
        float z;
    };

    struct HomeFallback {
        const char* stage;
        const char* changeStageId;
        ScenarioMask scenarioMask;
        Mode mode;
        bool hasCoordinates;
        Vec3 coordinates;
        bool hasCameraYawOffset;
        float cameraYawOffsetDeg;
        // Camera-to-subject distance for the pipe entrance camera (al::setEntranceCameraParam).
        // Vanilla entrance cameras in-game range roughly 1300-2700 depending on how much open
        // space surrounds the spot; 1800 is a reasonable default but a tight indoor spawn (e.g.
        // a PlayerRestartPos inside a small room) can need a much smaller value to avoid the
        // camera clipping into nearby walls at every yaw angle.
        float cameraDistance;
        bool hasPipeOffset;
        Vec3 pipeOffset;
    };

    struct OdysseyFallback {
        const char* stage;
        ScenarioMask scenarioMask;
        Vec3 coordinates;
    };

    // Scenario_Any matches every scenario (used when there is no scenario
    // restriction at all). Combine specific scenarios with |, e.g.
    // Scenario_3 | Scenario_7. For an entrance that is vanilla-available in
    // only a few scenarios, it reads better to list what IS available and
    // subtract it from Scenario_All, e.g. Scenario_All & ~(Scenario_5).
    static constexpr ScenarioMask Scenario_Any = 0;
    static constexpr ScenarioMask Scenario_1 = 1u << 0;
    static constexpr ScenarioMask Scenario_2 = 1u << 1;
    static constexpr ScenarioMask Scenario_3 = 1u << 2;
    static constexpr ScenarioMask Scenario_4 = 1u << 3;
    static constexpr ScenarioMask Scenario_5 = 1u << 4;
    static constexpr ScenarioMask Scenario_6 = 1u << 5;
    static constexpr ScenarioMask Scenario_7 = 1u << 6;
    static constexpr ScenarioMask Scenario_8 = 1u << 7;
    static constexpr ScenarioMask Scenario_9 = 1u << 8;
    static constexpr ScenarioMask Scenario_10 = 1u << 9;
    static constexpr ScenarioMask Scenario_11 = 1u << 10;
    static constexpr ScenarioMask Scenario_12 = 1u << 11;
    static constexpr ScenarioMask Scenario_13 = 1u << 12;
    static constexpr ScenarioMask Scenario_14 = 1u << 13;
    static constexpr ScenarioMask Scenario_15 = 1u << 14;
    static constexpr ScenarioMask Scenario_All = Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7 | Scenario_8
        | Scenario_9 | Scenario_10 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15;

    static constexpr Vec3 NoCoordinates = { 0.0f, 0.0f, 0.0f };
    static constexpr float DefaultCameraDistance = 1800.0f;

    static constexpr HomeFallback fallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, false, NoCoordinates, false, 0.0f,
            DefaultCameraDistance, false, NoCoordinates };
    }

    static constexpr HomeFallback fallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode,
        float cameraYawOffsetDeg, float cameraDistance = DefaultCameraDistance) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, false, NoCoordinates, true, cameraYawOffsetDeg,
            cameraDistance, false, NoCoordinates };
    }

    static constexpr HomeFallback coordinateFallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode,
        float x, float y, float z) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, true, Vec3 { x, y, z }, false, 0.0f,
            DefaultCameraDistance, false, NoCoordinates };
    }

    static constexpr HomeFallback coordinateFallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode,
        float x, float y, float z,
        float cameraYawOffsetDeg, float cameraDistance = DefaultCameraDistance) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, true, Vec3 { x, y, z }, true, cameraYawOffsetDeg,
            cameraDistance, false, NoCoordinates };
    }

    // Pipe/moon-pipe fallbacks are generated as a physical actor cloned from a
    // nearby vanilla pipe or door in tools/patch_fake_exit_pipes.py; that clone
    // inherits the source object's Translate exactly. When the entrance sits in
    // bad geometry (e.g. under a rock), offsetX/Y/Z nudges the generated pipe's
    // position away from the vanilla door's coordinates. The offset is applied
    // offline by patch_fake_exit_pipes.py, not at runtime.
    static constexpr HomeFallback pipeOffsetFallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode,
        float offsetX, float offsetY, float offsetZ) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, false, NoCoordinates, false, 0.0f,
            DefaultCameraDistance, true, Vec3 { offsetX, offsetY, offsetZ } };
    }

    static constexpr HomeFallback pipeOffsetFallback(const char* stage, const char* changeStageId,
        ScenarioMask scenarioMask, Mode mode,
        float offsetX, float offsetY, float offsetZ,
        float cameraYawOffsetDeg, float cameraDistance = DefaultCameraDistance) {
        return HomeFallback { stage, changeStageId, scenarioMask, mode, false, NoCoordinates, true, cameraYawOffsetDeg,
            cameraDistance, true, Vec3 { offsetX, offsetY, offsetZ } };
    }

    static constexpr OdysseyFallback odyssey(const char* stage, float x, float y, float z) {
        return OdysseyFallback { stage, Scenario_Any, Vec3 { x, y, z } };
    }

    static constexpr OdysseyFallback odysseyForScenario(const char* stage, ScenarioMask scenarioMask,
        float x, float y, float z) {
        return OdysseyFallback { stage, scenarioMask, Vec3 { x, y, z } };
    }

    // Overworld-only arrival fallback table. Scenario_Any matches every scenario;
    // combine specific scenarios with |, e.g. Scenario_3 | Scenario_7.
    // Add specific scenario rows after generic rows; lookup walks this table backward.
    // Pipe rows must also be generated into StageData by tools/patch_fake_exit_pipes.py.
    //
    // Modes:
    // - Mode_Vanilla keeps the authored target even if this scenario says unavailable.
    // - Mode_Coordinates force-spawns at EntranceDB2 coords, or custom coords if supplied
    //   with coordinateFallback(...).
    // - Mode_Pipe uses a generated green pipe at this entrance's location.
    // - Mode_Moon_Pipe uses a generated moon pipe at this entrance's location.
    // - Mode_Odyssey falls back to HomeEntrance using the per-kingdom Odyssey coords below.
    // For pipe modes, optional cameraYawOffsetDeg arg rotates the entrance camera around
    // the pipe in degrees, using the pipe as the pivot. Example: fallback(..., Mode_Moon_Pipe, 90.0f).
    // A further optional cameraDistance arg (default 1800) sets how far back that camera
    // sits; vanilla entrance cameras range roughly 1300-2700 depending on available space,
    // and a tight indoor spawn (e.g. a PlayerRestartPos inside a small room) may need a much
    // smaller value than the default to stop the camera clipping into nearby walls at every
    // yaw angle. Example: fallback(..., Mode_Moon_Pipe, 90.0f, 900.0f).
    // Use pipeOffsetFallback(..., offsetX, offsetY, offsetZ) instead of fallback(...) for a
    // pipe/moon-pipe entry whose generated actor needs to be nudged off the vanilla door's
    // coordinates (e.g. it spawns clipped into a rock); the same optional cameraYawOffsetDeg
    // and cameraDistance args work the same as above.
    static constexpr HomeFallback sHomeFallbacks[] = {
        // Cap Kingdom
        fallback("CapWorldHomeStage", "rollingstart", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 270.0f),
        // No vanilla CapWorldHomeStage-side endpoint exists for rollinggoal at all
        // (home_out=missing in EntranceDB2); treat it as missing in every scenario.
        fallback("CapWorldHomeStage", "rollinggoal", Scenario_All, Mode_Moon_Pipe, 270.0f),

        // Cascade Kingdom
        pipeOffsetFallback("WaterfallWorldHomeStage", "WanwanExStart", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Pipe, -200.0f, 0.0f, -300.0f, 180.0f),
        pipeOffsetFallback("WaterfallWorldHomeStage", "WanwanExGoal", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Pipe, -200.0f, 0.0f, -300.0f, 180.0f),
        fallback("WaterfallWorldHomeStage", "WindBlowExStart", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),
        fallback("WaterfallWorldHomeStage", "WindBlowExGoal", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),
        fallback("WaterfallWorldHomeStage", "CapAppearExEnt", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("WaterfallWorldHomeStage", "CapAppearExExit", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        pipeOffsetFallback("WaterfallWorldHomeStage", "Lift2D", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_7), Mode_Pipe, -250.0f, 0.0f, 250.0f, 180.0f),
        pipeOffsetFallback("WaterfallWorldHomeStage", "Lift2DExit", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Pipe, -250.0f, 0.0f, 250.0f, 180.0f),
        fallback("WaterfallWorldHomeStage", "RexPoppunEx", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_7), Mode_Pipe, 180.0f),

        // Sand Kingdom
        fallback("SandWorldHomeStage", "meganelift01", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),
        fallback("SandWorldHomeStage", "rocket", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),
        fallback("SandWorldHomeStage", "EX_2DHosui", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),

        fallback("SandWorldHomeStage", "meganelift02", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),
        fallback("SandWorldHomeStage", "EX_2DHosui_Exit", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),
        fallback("SandWorldHomeStage", "Under01", Scenario_All, Mode_Pipe),

        // Wooded Kingdom
        fallback("ForestWorldHomeStage", "EX_AnimalChase", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe),
        fallback("ForestWorldHomeStage", "EX_Mist", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5), Mode_Pipe),
        fallback("ForestWorldHomeStage", "PoisonEx", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe, 180.0f),
        fallback("ForestWorldHomeStage", "PoisonEx_Exit", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe, 180.0f),
        fallback("ForestWorldHomeStage", "EXCloud", Scenario_All & ~(Scenario_3 | Scenario_4 | Scenario_5), Mode_Coordinates),
        fallback("ForestWorldHomeStage", "KillerRoad", Scenario_All & ~(Scenario_5), Mode_Moon_Pipe, 225.0f),

        // Lake Kingdom
        fallback("LakeWorldHomeStage", "LakeWorldMoonEX1a", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),
        fallback("LakeWorldHomeStage", "LakeWorldMoonEX1b", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),

        // Goton's PlayerRestartPos sits in a tight indoor spot - the default 1800
        // camera distance clipped into walls at every yaw, so this uses a much
        // shorter pull-back. Tune further if 900 still clips.
        pipeOffsetFallback("LakeWorldHomeStage", "Goton", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Pipe, 0.0f, 0.0f, -400.0f, 180.0f, 900.0f),

        // Cloud Kingdom
        fallback("CloudWorldHomeStage", "cube", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),

        // Lost Kingdom
        fallback("ClashWorldHomeStage", "ClashWorldMoonEX2", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("ClashWorldHomeStage", "imomu_01", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("ClashWorldHomeStage", "imomu_02", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),

        // Metro Kingdom
        //
        // New Donk City's building doors are DoorCity actors that ship
        // InitState=Close in exactly the scenarios that do not place the
        // loading zone behind them, and QoL only force-opens the one costume
        // door (see unlockCostumeDoorMetroHook). Every entrance below whose
        // vanilla object is a ChangeStageArea therefore sits behind a shut door
        // in its own fallback scenarios, and coord-spawning onto that object
        // dropped the player inside the closed doorway. They are pipes instead:
        // each of those ChangeStageAreas links a PlayerRestartPos out on the
        // street (the spot vanilla walks Mario back out to), and
        // patch_fake_exit_pipes.py prefers that linked position over the area's
        // own Translate, so the generated pipe lands outside the door.
        // The 180 deg camera yaw puts the entrance camera on the street side
        // looking back at the door, matching the other Metro door pipes -
        // UNVERIFIED per-door in-game, nudge individually if one frames badly.
        fallback("CityWorldHomeStage", "densen", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "densendokan", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "bike02", Scenario_1 | Scenario_3 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_14 | Scenario_15, Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "bike02return", Scenario_1 | Scenario_3 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_14 | Scenario_15, Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "kaitendokan", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe),
        fallback("CityWorldHomeStage", "gragra", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("CityWorldHomeStage", "gragrareturn", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("CityWorldHomeStage", "donsuke", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe, 90.0f),
        fallback("CityWorldHomeStage", "theater", Scenario_All & ~(Scenario_5 | Scenario_8), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "gunsyu", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Odyssey),
        fallback("CityWorldHomeStage", "gunsyudokan", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_13), Mode_Odyssey),
        fallback("CityWorldHomeStage", "bou", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "boureturn", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "onpu", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "tenjo", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_13), Mode_Odyssey),
        fallback("CityWorldHomeStage", "bike", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("CityWorldHomeStage", "bikereturn", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("CityWorldHomeStage", "car", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "Bonus", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_10 | Scenario_12 | Scenario_13), Mode_Pipe, 90.0f),
        fallback("CityWorldHomeStage", "taxi", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_13), Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "taxireturn", Scenario_All, Mode_Pipe, 180.0f),
        fallback("CityWorldHomeStage", "under001enter", Scenario_All & ~(Scenario_2 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_13), Mode_Pipe),

        // Seaside Kingdom
        fallback("SeaWorldHomeStage", "SeaWorldMoonEX2", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("SeaWorldHomeStage", "SeaWorldMoonEX1a", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),
        fallback("SeaWorldHomeStage", "SeaWorldMoonEX1b", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),

        // Snow Kingdom
        fallback("SnowWorldHomeStage", "EX_RailCol2", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("SnowWorldHomeStage", "EX_RailCol2_Exit", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("SnowWorldHomeStage", "FigureWalker", Scenario_All & ~(Scenario_4), Mode_Pipe, 180.0f),
        fallback("SnowWorldHomeStage", "EX_SkyBonus", Scenario_All & ~(Scenario_4), Mode_Coordinates),
        fallback("SnowWorldHomeStage", "MoonRace", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe, 180.0f),

        // Luncheon Kingdom
        fallback("LavaWorldHomeStage", "FenceLiftEx", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("LavaWorldHomeStage", "FenceLiftExdokan", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("LavaWorldHomeStage", "GabuzouClockEx", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("LavaWorldHomeStage", "TreasureEventWorldLava", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_7 | Scenario_8 | Scenario_9), Mode_Odyssey),
        fallback("LavaWorldHomeStage", "GabuzouClockExdokan", Scenario_All & ~(Scenario_8), Mode_Moon_Pipe),
        fallback("LavaWorldHomeStage", "LavaLiftEx", Scenario_All & ~(Scenario_8), Mode_Odyssey),
        fallback("LavaWorldHomeStage", "LavaLiftExdokan", Scenario_All & ~(Scenario_8), Mode_Odyssey),

        // Ruined Kingdom
        fallback("BossRaidWorldHomeStage", "BossRaidWorldMoonEx02_Enter", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("BossRaidWorldHomeStage", "BossRaidWorldMoonEx02_Exit", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("BossRaidWorldHomeStage", "BossRaidWorldEx01_Eixt", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Pipe),

        // Bowser's Kingdom
        fallback("SkyWorldHomeStage", "jizo01", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Odyssey),
        fallback("SkyWorldHomeStage", "jizo02", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Odyssey),
        fallback("SkyWorldHomeStage", "tukkun001_enter", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("SkyWorldHomeStage", "tukkun001_exit", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),
        fallback("SkyWorldHomeStage", "Patakaron02", Scenario_All & ~(Scenario_4), Mode_Moon_Pipe),

        // Spinning Tower (TsukkunRotateExStage). The base position is the area's
        // linked PlayerRestartPos, obj6464 at -8200/10900/-7350, which sits on
        // the TOWER side of the SkyWorldHomeGate gateway: the gate leaves stand
        // on the z=-7100 plane spanning x=-8600..-7800, and the tower's
        // ChangeStageArea box reaches out to z=-7667 behind it. So the useful
        // axis here is Z, and it has to go positive - the old -200/0/+200 nudge
        // landed at z=-7150, 50 units off the gate plane and inside the frame,
        // while the unoffset restart pos is just deeper into the tower alcove.
        //
        // +550 in Z puts it at -8200/10900/-6800: still centred on the opening
        // (the leaves' midpoint is x=-8200), 300 clear of the gate on the open
        // castle-grounds side, and on the same y=10900 plateau at the same Z as
        // the vanilla exit pipe obj4067 (750 away in X), so the floor is known
        // good. Nothing solid sits within 500 units of it.
        pipeOffsetFallback("SkyWorldHomeStage", "tukkun000_enter", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Pipe, 0.0f, 0.0f, 550.0f, 180.0f),
        pipeOffsetFallback("SkyWorldHomeStage", "tukkun000_exit", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Pipe, -200.0f, 0.0f, 200.0f, 180.0f),
        fallback("SkyWorldHomeStage", "shop_dress", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Pipe, 180.0f),

        // Moon Kingdom
        fallback("MoonWorldHomeStage", "dot00", Scenario_All & ~(Scenario_3), Mode_Moon_Pipe),
        fallback("MoonWorldHomeStage", "dot01", Scenario_All & ~(Scenario_3), Mode_Moon_Pipe),
        fallback("MoonWorldHomeStage", "moon", Scenario_All & ~(Scenario_3), Mode_Moon_Pipe),
    };

    // Candidate home loading zones not currently overridden. This list is generated
    // from EntranceDB2 endpoints so the ChangeStageId spelling matches runtime data.
    // Copy one into sHomeFallbacks above and choose the desired mode. Pipe modes
    // also need matching StageData generated by tools/patch_fake_exit_pipes.py.
    //
    // Other Cap Kingdom home loading zones not overridden yet:
    // fallback("CapWorldHomeStage", "Ex", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4 | Scenario_7), Mode_Odyssey), // ChangeStageArea, mask=0x0000004F, dest=CapWorldTowerStage
    // fallback("CapWorldHomeStage", "FrogSearchExStageEnt", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000E, dest=FrogSearchExStage
    // fallback("CapWorldHomeStage", "Goal", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=CapWorldTowerStage
    // fallback("CapWorldHomeStage", "PoisonWaveExEnt", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000E, dest=PoisonWaveExStage
    // fallback("CapWorldHomeStage", "PushBlockExStageEnt", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000E, dest=PushBlockExStage
    // fallback("CapWorldHomeStage", "PushBlockExStageEntDokan", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6), Mode_Odyssey), // DokanStageChange, mask=0x0000003E, dest=ChangeStageList
    //
    // Other Cascade Kingdom home loading zones not overridden yet:
    // fallback("WaterfallWorldHomeStage", "CapAppearExExit", Scenario_All & ~(Scenario_4), Mode_Odyssey), // DokanStageChange, mask=0x00000008, dest=ChangeStageList
    // fallback("WaterfallWorldHomeStage", "Lift2D", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_7), Mode_Odyssey), // ChangeStageArea, mask=0x0000004E, dest=Lift2DExStage
    // fallback("WaterfallWorldHomeStage", "Lift2DExit", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Odyssey), // DokanStageChange, mask=0x0000007E, dest=ChangeStageList
    // fallback("WaterfallWorldHomeStage", "WanwanExGoal", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Odyssey), // DokanStageChange, mask=0x0000007E, dest=ChangeStageList
    // fallback("WaterfallWorldHomeStage", "WanwanExStart", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Odyssey), // DokanStageChange, mask=0x0000007E, dest=WanwanClashExStage
    // fallback("WaterfallWorldHomeStage", "WindBlowExGoal", Scenario_All & ~(Scenario_4), Mode_Odyssey), // DokanStageChange, mask=0x00000008, dest=ChangeStageList
    //
    // Other Sand Kingdom home loading zones not overridden yet:
    // fallback("SandWorldHomeStage", "aaa", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=SandWorldSphinxExStage
    // fallback("SandWorldHomeStage", "abc", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=SandWorldCostumeStage
    // fallback("SandWorldHomeStage", "anki2", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // DokanStageChange, mask=0x0000039F, dest=ChangeStageList
    // fallback("SandWorldHomeStage", "arijigoku", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=SandWorldPressExStage
    // fallback("SandWorldHomeStage", "arijigoku1", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // DokanStageChange, mask=0x0000039F, dest=SandWorldPressExStage
    // fallback("SandWorldHomeStage", "arijigoku2", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // DokanStageChange, mask=0x0000039F, dest=SandWorldPressExStage
    // fallback("SandWorldHomeStage", "bar1", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=SandWorldShopStage
    // fallback("SandWorldHomeStage", "bar2", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=SandWorldShopStage
    // fallback("SandWorldHomeStage", "biru", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // BazookaElectric, mask=0x0000039F, dest=SandWorldRotateExStage
    // fallback("SandWorldHomeStage", "doukutu1", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // ChangeStageArea, mask=0x0000039F, dest=SandWorldKillerExStage
    // fallback("SandWorldHomeStage", "doukutu2", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=SandWorldKillerExStage
    // fallback("SandWorldHomeStage", "EX_2DHosui_Exit", Scenario_All & ~(Scenario_5), Mode_Odyssey), // DokanStageChange, mask=0x00000010, dest=WaterTubeExStage
    // fallback("SandWorldHomeStage", "hide", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // ChangeStageArea, mask=0x0000039F, dest=SandWorldSecretStage
    // fallback("SandWorldHomeStage", "icestart", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7 | Scenario_10), Mode_Odyssey), // ChangeStageArea, mask=0x0000027E, dest=SandWorldUnderground000Stage
    // fallback("SandWorldHomeStage", "meganelift02", Scenario_All & ~(Scenario_5), Mode_Odyssey), // DokanStageChange, mask=0x00000010, dest=MeganeLiftExStage
    // fallback("SandWorldHomeStage", "Out", Scenario_All & ~(Scenario_3 | Scenario_4 | Scenario_5), Mode_Odyssey), // DokanStageChange, mask=0x0000001C, dest=SandWorldUnderground001Stage
    // fallback("SandWorldHomeStage", "pyramid01", Scenario_All & ~(Scenario_1 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_8 | Scenario_9), Mode_Odyssey), // ChangeStageArea, mask=0x0000001C/0x00000181, dest=SandWorldPyramid000Stage
    // fallback("SandWorldHomeStage", "pyramid04", Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // ChangeStageArea, mask=0x00000181/0x0000027E, dest=SandWorldPyramid001Stage
    // fallback("SandWorldHomeStage", "shindo", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=SandWorldVibrationStage
    // fallback("SandWorldHomeStage", "start", Scenario_All & ~(Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Odyssey), // ChangeStageArea, mask=0x0000007C, dest=SandWorldUnderground001Stage
    // fallback("SandWorldHomeStage", "town", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=SandWorldSlotStage
    // fallback("SandWorldHomeStage", "Under01", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_10), Mode_Odyssey), // DokanStageChange, mask=0x0000021E, dest=SandWorldUnderground000Stage
    // fallback("SandWorldHomeStage", "wall", Scenario_6 | Scenario_7 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // ChangeStageArea, mask=0x0000039F, dest=SandWorldMeganeExStage
    //
    // Other Wooded Kingdom home loading zones not overridden yet:
    // fallback("ForestWorldHomeStage", "bonus", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_6 | Scenario_7), Mode_Odyssey), // DokanStageChange, mask=0x0000007E, dest=ForestWorldBonusStage
    // fallback("ForestWorldHomeStage", "bonus2", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=ForestWorldBonusStage
    // fallback("ForestWorldHomeStage", "boss001", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=ForestWorldBossStage
    // fallback("ForestWorldHomeStage", "boss002", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=ForestWorldBossStage
    // fallback("ForestWorldHomeStage", "EX_Mist", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5), Mode_Odyssey), // BazookaElectric, mask=0x0000001E, dest=FogMountainExStage
    // fallback("ForestWorldHomeStage", "EX_RailCollision", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_8), Mode_Odyssey), // ChangeStageArea, mask=0x0000009F, dest=RailCollisionExStage
    // fallback("ForestWorldHomeStage", "EX_RailCollision_Exit", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=RailCollisionExStage
    // fallback("ForestWorldHomeStage", "EX_Tankuro", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4 | Scenario_5 | Scenario_8), Mode_Odyssey), // ChangeStageArea, mask=0x0000009F, dest=ShootingElevatorExStage
    // fallback("ForestWorldHomeStage", "EX_Tankuro_Exit", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=ShootingElevatorExStage
    // fallback("ForestWorldHomeStage", "EX_Water", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=ForestWorldWaterExStage
    // fallback("ForestWorldHomeStage", "EX_Water_Exit", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=ForestWorldWaterExStage
    // fallback("ForestWorldHomeStage", "Jyukai001", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=ForestWorldWoodsStage
    // fallback("ForestWorldHomeStage", "Jyukai003", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=ForestWorldWoodsStage
    // fallback("ForestWorldHomeStage", "Tower001", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=ForestWorldTowerStage
    // fallback("ForestWorldHomeStage", "Tower002", Scenario_9 | Scenario_10 | Scenario_11 | Scenario_12 | Scenario_13 | Scenario_14 | Scenario_15, Mode_Odyssey), // ChangeStageArea, mask=0x0000007E/0x00000081, dest=ForestWorldTowerStage
    //
    // Other Lake Kingdom home loading zones not overridden yet:
    // fallback("LakeWorldHomeStage", "CapTrampolineA", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=TrampolineWallCatchExStage
    // fallback("LakeWorldHomeStage", "CapTrampolineB", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=TrampolineWallCatchExStage
    // fallback("LakeWorldHomeStage", "FastenerEx", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=FastenerExStage
    // fallback("LakeWorldHomeStage", "LakeWorldShop", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=LakeWorldShopStage
    //
    // Other Cloud Kingdom home loading zones not overridden yet:
    // fallback("CloudWorldHomeStage", "cube", Scenario_All & ~(Scenario_4), Mode_Odyssey), // DokanStageChange, mask=0x00000008, dest=Cube2DExStage
    // fallback("CloudWorldHomeStage", "Fukuwarai", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // DokanStageChange, mask=0x0000000E, dest=FukuwaraiKuriboStage
    //
    // Other Lost Kingdom home loading zones not overridden yet:
    // fallback("ClashWorldHomeStage", "Kinopio", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=ClashWorldShopStage
    //
    // Other Metro Kingdom home loading zones not overridden yet:
    // fallback("CityWorldHomeStage", "main_enter", Scenario_6 | Scenario_9, Mode_Odyssey), // ChangeStageArea, mask=0x00007EDF, dest=CityWorldMainTowerStage
    // fallback("CityWorldHomeStage", "main_exit", Scenario_6 | Scenario_9, Mode_Odyssey), // ChangeStageArea, mask=0x00007EDF, dest=CityWorldMainTowerStage
    // fallback("CityWorldHomeStage", "shop_coin", Scenario_3 | Scenario_6 | Scenario_7 | Scenario_9 | Scenario_11 | Scenario_14, Mode_Odyssey), // ChangeStageArea, mask=0x00005A9B, dest=CityWorldShop01Stage
    // fallback("CityWorldHomeStage", "shop_corect", Scenario_3 | Scenario_6 | Scenario_7 | Scenario_9 | Scenario_11 | Scenario_14, Mode_Odyssey), // ChangeStageArea, mask=0x00005A9B, dest=CityWorldShop01Stage
    //
    // Other Seaside Kingdom home loading zones not overridden yet:
    // fallback("SeaWorldHomeStage", "CostumeEventSeaWorld", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=SeaWorldCostumeStage
    // fallback("SeaWorldHomeStage", "PukupukuCaveGoal", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=SeaWorldUtsuboCaveStage
    // fallback("SeaWorldHomeStage", "PukupukuCaveStart", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=SeaWorldUtsuboCaveStage
    // fallback("SeaWorldHomeStage", "RoomEventWorldSea", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=SeaWorldSneakingManStage
    // fallback("SeaWorldHomeStage", "SeaWorldEX1a", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=WaterValleyExStage
    // fallback("SeaWorldHomeStage", "SeaWorldEX1b", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=WaterValleyExStage
    // fallback("SeaWorldHomeStage", "SeaWorldEX2", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // BazookaElectric, mask=0x0000000F, dest=CloudExStage
    // fallback("SeaWorldHomeStage", "SeaWorldEX3a", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=SenobiTowerExStage
    // fallback("SeaWorldHomeStage", "SeaWorldEX3b", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // DokanStageChange, mask=0x0000000F, dest=SenobiTowerExStage
    // fallback("SeaWorldHomeStage", "shindo_Lv2", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=SeaWorldVibrationStage
    // fallback("SeaWorldHomeStage", "TreasureEventWorldSea", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=SeaWorldSecretStage
    //
    // Other Snow Kingdom home loading zones not overridden yet:
    // fallback("SnowWorldHomeStage", "ByugoPuzzle", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=ByugoPuzzleExStage
    // fallback("SnowWorldHomeStage", "EX_IceWater", Scenario_Any, Mode_Odyssey), // DoorAreaChangeCap, mask=0x00007FFF, dest=IceWaterBlockExStage
    // fallback("SnowWorldHomeStage", "EX_IceWater_Exit", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=IceWaterBlockExStage
    // fallback("SnowWorldHomeStage", "EX_IceWaterDash", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000F, dest=IceWaterDashExStage
    // fallback("SnowWorldHomeStage", "EX_SkyBonus", Scenario_All & ~(Scenario_2 | Scenario_3 | Scenario_4), Mode_Odyssey), // ChangeStageArea, mask=0x0000000E, dest=SnowWorldCloudBonusExStage
    // fallback("SnowWorldHomeStage", "SnowUGEnt", Scenario_Any, Mode_Odyssey), // ChangeStageArea, mask=0x00007FFF, dest=SnowWorldTownStage
    // fallback("SnowWorldHomeStage", "SnowUGExit", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=SnowWorldTownStage
    //
    // Other Luncheon Kingdom home loading zones not overridden yet:
    // fallback("LavaWorldHomeStage", "BBQEx", Scenario_Any, Mode_Odyssey), // DoorAreaChangeCap, mask=0x00007FFF, dest=LavaWorldClockExStage
    // fallback("LavaWorldHomeStage", "BBQExDokan", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // DokanStageChange, mask=0x000002C7, dest=LavaWorldClockExStage
    // fallback("LavaWorldHomeStage", "CostumeEventWorldLava", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=LavaWorldCostumeStage
    // fallback("LavaWorldHomeStage", "CostumeOut", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // DokanStageChange, mask=0x000002C7, dest=ChangeStageList
    // fallback("LavaWorldHomeStage", "ForkEX", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // ChangeStageArea, mask=0x000002C7, dest=ForkExStage
    // fallback("LavaWorldHomeStage", "KeyMoveEx", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // ChangeStageArea, mask=0x000002C7, dest=LavaWorldUpDownExStage
    // fallback("LavaWorldHomeStage", "KeyMoveExDokan", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // DokanStageChange, mask=0x000002C7, dest=LavaWorldUpDownExStage
    // fallback("LavaWorldHomeStage", "MartinCubeEx", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // ChangeStageArea, mask=0x000002C7, dest=LavaWorldExcavationExStage
    // fallback("LavaWorldHomeStage", "PechoBubbleEx", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // ChangeStageArea, mask=0x000002C7, dest=LavaWorldBubbleLaneExStage
    // fallback("LavaWorldHomeStage", "PechoBubbleExDokan", Scenario_All & ~(Scenario_1 | Scenario_2 | Scenario_3 | Scenario_7 | Scenario_8 | Scenario_10), Mode_Odyssey), // DokanStageChange, mask=0x000002C7, dest=LavaWorldBubbleLaneExStage
    // fallback("LavaWorldHomeStage", "shop", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=LavaWorldShopStage
    // fallback("LavaWorldHomeStage", "town", Scenario_Any, Mode_Odyssey), // DoorWarpStageChange, mask=0x00007FFF, dest=LavaBonus1Zone
    //
    // Other Darker Side home loading zones not overridden yet:
    // fallback("Special2WorldHomeStage", "CP_Entrance", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=Special2WorldLavaStage
    // fallback("Special2WorldHomeStage", "MoonGoal", Scenario_Any, Mode_Odyssey), // DokanStageChange, mask=0x00007FFF, dest=Special2WorldLavaStage

    // Explicit Odyssey fallback coordinates, organized by kingdom. These are used when
    // an unavailable overworld entrance falls back to the Odyssey instead of trusting
    // the game's HomeTrans result, which can resolve on the globe or inside the model.
    //
    // Each kingdom's home stage places a ShineTowerRocket actor (the postgame moon-rock
    // launcher) with a linked PlayerRestartPos child - a real PlayerStartObj the game
    // itself uses as a nearby safe spawn. These coordinates are that linked
    // PlayerRestartPos's Translate, not the ShineTowerRocket's own (the previous values
    // here were the ShineTowerRocket's Translate, which can be a few thousand units off
    // the real spawn point and land Mario inside collision).
    static constexpr OdysseyFallback sOdysseyFallbacks[] = {
        // Cap Kingdom
        odyssey("CapWorldHomeStage", 200.0f, -900.0f, 29050.0f),

        // Cascade Kingdom - ShineTowerRocket's linked PlayerRestartPos here (obj7) is
        // ~13000 units away in X from the tower itself, unlike every other kingdom
        // (~1500-1700 units); the moon-rock area is far from where the ship actually
        // parks. Left at the old ShineTowerRocket-derived value pending in-game
        // verification of where the real ship-adjacent spawn is.
        odyssey("WaterfallWorldHomeStage", 13859.293f, 600.0f, 3889.088f),

        // Sand Kingdom
        odyssey("SandWorldHomeStage", 32900.0f, -1150.0f, -12700.0f),

        // Wooded Kingdom
        odyssey("ForestWorldHomeStage", -12056.17f, 20.247f, 6257.895f),

        // Lake Kingdom
        odyssey("LakeWorldHomeStage", 700.0f, 800.0f, 6350.0f),

        // Cloud Kingdom
        odyssey("CloudWorldHomeStage", 0.0f, -319.953f, 10250.0f),

        // Lost Kingdom
        odyssey("ClashWorldHomeStage", 5500.0f, 520.488f, 12250.0f),

        // Metro Kingdom
        odyssey("CityWorldHomeStage", -2750.0f, 2600.0f, 22850.0f),

        // Seaside Kingdom
        odyssey("SeaWorldHomeStage", 0.0f, 752.057f, 0.0f),

        // Snow Kingdom
        odyssey("SnowWorldHomeStage", -3200.0f, 5001.293f, 9000.0f),

        // Luncheon Kingdom
        odyssey("LavaWorldHomeStage", 21550.0f, 200.0f, 9500.0f),

        // Ruined Kingdom
        odyssey("BossRaidWorldHomeStage", 15250.0f, -31697.229f, 48350.0f),

        // Bowser Kingdom
        odyssey("SkyWorldHomeStage", -50000.0f, 0.0f, 52150.0f),

        // Moon Kingdom
        odyssey("MoonWorldHomeStage", 11496.49f, 2810.881f, 37647.23f),

        // Mushroom Kingdom
        odyssey("PeachWorldHomeStage", 4100.0f, 52.796f, 5300.0f),

        // Dark Side
        odyssey("Special1WorldHomeStage", -18950.0f, 15034.662f, 25000.0f),

        // Darker Side - Special2WorldHomeStageMap's ShineTowerRocket (obj4)
        // does carry a linked PlayerRestartPos (obj5), like every other
        // kingdom here, so this row follows the same rule as the ones above.
        // It sits at the same spot as the stage's own
        // PlayerActorHakoniwa/CP_Start placement, which is the authored
        // ground spawn beside the parked Odyssey - not inside it. An earlier
        // revision assumed the link was missing and read that coincidence as
        // "this coordinate IS the ship", then invented a point ~930 units
        // along the ledge toward the Fukankun; that invented point is what
        // actually spawned Mario inside the ship's hull.
        odyssey("Special2WorldHomeStage", -7150.0f, -1028.001f, 1500.0f),
    };

    // ─────────────────────────────────────────────────────────────
    // Shared lookup
    //
    // EntranceHooks owns the runtime placement itself; these helpers exist so
    // the logic layers (MoonRando's in-game reachability/hint tracker and the
    // host-side randomizer_logic.py, which parses this same table) can answer
    // the one question they care about without duplicating the table: when an
    // entrance is absent in the target kingdom's current scenario, does the
    // fallback still put Mario at that entrance, or hand him to the Odyssey?
    // ─────────────────────────────────────────────────────────────

    static inline bool scenarioMaskContainsScenario(ScenarioMask mask, int scenario) {
        return scenario >= 1 && scenario <= 15 && (mask & (1u << (scenario - 1))) != 0;
    }

    // Walks the table backward so a later, more specific scenario row wins over
    // an earlier generic one - matching findHomeFallbackEntry in EntranceHooks
    // and the "add specific scenario rows after generic rows" note above.
    static inline const HomeFallback* findHomeFallback(const char* stage, const char* changeStageId,
        int scenario) {
        if (!stage || !stage[0] || !changeStageId || !changeStageId[0])
            return nullptr;
        constexpr int count = static_cast<int>(sizeof(sHomeFallbacks) / sizeof(sHomeFallbacks[0]));
        for (int i = count - 1; i >= 0; --i) {
            const HomeFallback& entry = sHomeFallbacks[i];
            if (std::strcmp(entry.stage, stage) == 0
                && std::strcmp(entry.changeStageId, changeStageId) == 0
                && (entry.scenarioMask == 0 || scenarioMaskContainsScenario(entry.scenarioMask, scenario)))
                return &entry;
        }
        return nullptr;
    }

    // Whether a mode leaves Mario standing at the loading zone itself.
    // Mode_Vanilla keeps the authored spawn, Mode_Coordinates force-spawns on
    // it, and both pipe modes generate a pipe at that exact spot - all three
    // put the player at the entrance, so logic can credit its node. Only
    // Mode_Odyssey does not, and a missing table row counts as Odyssey too
    // because that is how EntranceHooks resolves it.
    static inline bool modePlacesPlayerAtEntrance(Mode mode) {
        return mode == Mode_Vanilla || mode == Mode_Coordinates || mode == Mode_Pipe
            || mode == Mode_Moon_Pipe;
    }

    static inline bool arrivalPlacesPlayerAtEntrance(const char* stage, const char* changeStageId,
        int scenario) {
        const HomeFallback* fallback = findHomeFallback(stage, changeStageId, scenario);
        return fallback && modePlacesPlayerAtEntrance(fallback->mode);
    }

} // namespace EntranceFallbacks
