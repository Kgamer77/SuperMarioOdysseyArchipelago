#pragma once

#include "game/Player/PlayerActorHakoniwa.h"

#include <cstring>

#include "Multiplayer/puppets/PuppetInfo.h"

const char* tryGetPuppetCapName(PuppetInfo* info);
const char* tryGetPuppetBodyName(PuppetInfo* info);

const char* tryConvertName(const char* className);
bool isPartOf(const char* w1, const char* w2);

__attribute__((used)) static const char* costumeNames[] = {
    "Mario",          "MarioCaptain",      "Mario64",        "Mario64Metal",    "MarioAloha",        "MarioArmor",      "MarioBone",
    "MarioClown",     "MarioColorClassic", "MarioColorGold", "MarioColorLuigi", "MarioColorWaluigi", "MarioColorWario", "MarioCook",
    "MarioDiddyKong", "MarioDoctor",       "MarioExplorer",  "MarioFootball",   "MarioGolf",         "MarioGunman",     "MarioHakama",
    "MarioHappi",     "MarioKing",         "MarioKoopa",     "MarioMaker",      "MarioMechanic",     "MarioNew3DS",     "MarioPainter",
    "MarioPeach",     "MarioPilot",        "MarioPirate",    "MarioPoncho",     "MarioPrimitiveMan", "MarioSailor",     "MarioScientist",
    "MarioShopman",   "MarioSnowSuit",     "MarioSpaceSuit", "MarioSuit",       "MarioSwimwear",     "MarioTailCoat",   "MarioTuxedo",
    "MarioUnderwear"};

struct HackActorName {
    const char* className;
    const char* hackName;
};

// attribute otherwise the build log is spammed with unused warnings
__attribute__((used)) static HackActorName classHackNames[] = {
    {"SenobiGeneratePoint", "Senobi"},
    {"JugemFishing", "Jugem"},
    {"Yoshi", "YoshiModel"},
    {"Statue", "StatueJizo"},
    {"KuriboPossessed", "Kuribo"},
    {"KillerLauncher", "Killer"},
    {"KillerLauncherMagnum", "KillerMagnum"},
    {"FireBrosPossessed", "FireBros"},
    {"HammerBrosPossessed", "HammerBros"},
    {"ElectricWire", "ElectricWireMover"},
    {"TRexSleep", "TRex"},
    {"TRexPatrol", "TRex"},
    {"Koopa", "KoopaHack"},
    {"PukupukuSnow", "Pukupuku"},  // Maps PukupukuSnow to same hack name as Pukupuku for syncing
};

__attribute__((used)) static const char* toadetteMoons[] = {"Scenario_Ending",
                                                            "Scenario_WorldAll",
                                                            "Shine_Gather_1",
                                                            "Shine_Gather_2",
                                                            "Shine_Gather_3",
                                                            "Shine_CollectCoinShop",
                                                            "Shine_Shine2D_1",
                                                            "Shine_Shine2D_2",
                                                            "Shine_TreasureBox_1",
                                                            "Shine_TreasureBox_2",
                                                            "Shine_MusicNote_1",
                                                            "Shine_MusicNote_2",
                                                            "Shine_TimerAthretic_1",
                                                            "Shine_TimerAthretic_2",
                                                            "Shine_CaptainKinopio_1",
                                                            "Shine_CaptainKinopio_2",
                                                            "Shine_TravelingPeach_1",
                                                            "Shine_TravelingPeach_2",
                                                            "Shine_CollectAnimalAll",
                                                            "Shine_KuriboGirl",
                                                            "Shine_Jugem",
                                                            "Shine_Seed_1",
                                                            "Shine_Seed_2",
                                                            "Shine_Rabbit_1",
                                                            "Shine_Rabbit_2",
                                                            "Shine_DigPoint_1",
                                                            "Shine_DigPoint_2",
                                                            "Shine_CapHanger_1",
                                                            "Shine_CapHanger_2",
                                                            "Shine_Bird",
                                                            "Shine_CostumeRoom_1",
                                                            "Shine_CostumeRoom_2",
                                                            "Shine_CostumeRoom_3",
                                                            "Shine_HideAndSeekCapMan",
                                                            "Shine_CollectBgm",
                                                            "Shine_HintPhoto_1",
                                                            "Shine_HintPhoto_2",
                                                            "Shine_CapThrottle",
                                                            "MiniGame_RaceMan_1",
                                                            "MiniGame_RaceMan_2",
                                                            "MiniGame_FigureWalker",
                                                            "MiniGame_SphinxQuiz",
                                                            "Souvenir_Count_1",
                                                            "Souvenir_Count_2",
                                                            "Souvenir_Count_3",
                                                            "Capture_Count_1",
                                                            "Capture_Count_2",
                                                            "Capture_Count_3",
                                                            "Costume_Cap_1",
                                                            "Costume_Cap_2",
                                                            "Costume_Clothes_1",
                                                            "Costume_Clothes_2",
                                                            "Other_MoonStoneAll",
                                                            "Other_WorldWarpHoleAll",
                                                            "Other_CheckPoint_1",
                                                            "Other_CheckPoint_2",
                                                            "Other_Coin_1",
                                                            "Other_Coin_2",
                                                            "Other_Coin_3",
                                                            "Other_Jump",
                                                            "Other_CapThrow"};

// same ids as GameDataFunction::getCurrentWorldIdNoDevelop
__attribute__((used)) static const char* worldNames[17] = {"Cap Kingdom",      "Cascade Kingdom", "Sand Kingdom",     "Wooded Kingdom",  "Lake Kingdom",
                                                           "Cloud Kingdom",    "Lost Kingdom",    "Metro Kingdom",    "Seaside Kingdom", "Snow Kingdom",
                                                           "Luncheon Kingdom", "Ruined Kingdom",  "Bowser's Kingdom", "Moon Kingdom",    "Mushroom Kingdom",
                                                           "Dark Side",        "Darker Side"};

__attribute__((used)) static const char* homeStageNames[17] = {
    "CapWorldHomeStage",   "WaterfallWorldHomeStage", "SandWorldHomeStage",  "ForestWorldHomeStage",   "LakeWorldHomeStage",    "CloudWorldHomeStage",
    "ClashWorldHomeStage", "CityWorldHomeStage",      "SeaWorldHomeStage",   "SnowWorldHomeStage",     "LavaWorldHomeStage",    "BossRaidWorldHomeStage",
    "SkyWorldHomeStage",   "MoonWorldHomeStage",      "PeachWorldHomeStage", "Special1WorldHomeStage", "Special2WorldHomeStage"};

struct Transform {
    sead::Vector3f* position;
    sead::Quatf* rotation;
};

// From Boss Room Unity Example
class VisualUtils {
public:
    static float SmoothMove(Transform moveTransform, Transform targetTransform, float timeDelta, float closingSpeed, float maxAngularSpeed);

    constexpr static const float k_MinSmoothSpeed = 0.1f;
    constexpr static const float k_TargetCatchupTime = 0.2f;
};

class StageScene;

StageScene* getStageScene();