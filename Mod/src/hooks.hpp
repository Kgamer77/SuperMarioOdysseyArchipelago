#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "hk/util/Math.h"

#include "al/Library/Camera/CameraDirector.h"
#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Execute/ExecuteDirector.h"
#include "al/Library/Execute/ExecuteRequestKeeper.h"
#include "al/Library/Execute/ExecuteTable.h"
#include "al/Library/Execute/ExecuteTableHolderDraw.h"
#include "al/Library/Execute/ExecuteTableHolderUpdate.h"
#include "al/Library/Execute/ExecuteTablesImpl.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitFunction.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Scene/Scene.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/Yaml/ByamlIter.h"
#include "al/Library/Yaml/ByamlUtil.h"
#include "al/Library/Yaml/Writer/ByamlWriter.h"

#include "game/Actors/WorldndBorderKeeper.h"
#include "game/Item/Shine.h"
#include "game/Layout/CoinCounter.h"
#include "game/MapObj/AppearSwitchTimer.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Scene/StageSceneStateOption.h"
#include "game/Scene/StageSceneStatePauseMenu.h"

#include <cstring>
#include <sys/types.h>

#include "Imgui.hpp"
#include "Library/Collision/CollisionPartsTriangle.h"
#include "Library/Nerve/Nerve.h"
#include "Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"
#include "Scene/StageScene.h"
#include "Scene/StageSceneStateModConfig.hpp"
#include "Scene/Twists/TwistsConfig.hpp"
#include "System/GameDataHolder.h"
#include "helpers.hpp"
#include "layouts/ConnectionStatus.h"
#include "server/Client.hpp"
#include "server/gamemode/GameModeManager.hpp"

static HkReplace<bool, al::IUseSceneObjHolder*> comboBtnHook = hk::hook::replace([](al::IUseSceneObjHolder* holder) -> bool {
    // only switch to combo if freezetag or shinethief is active
    if (GameModeManager::instance()->isModeAndActive(GameMode::FREEZETAG) || GameModeManager::instance()->isModeAndActive(GameMode::SHINETHIEF) || GameModeManager::instance()->isModeAndActive(GameMode::ARCHIPELAGO))
        return false;

    // only if the gamemode wants it
    if (GameModeManager::instance()->isActive()) { // only switch to combo if any gamemode is active
        return !al::isPadHoldL(-1) && al::isPadTriggerDown(-1);
    } else {
        return al::isPadTriggerDown(-1);
    }
});

static HkTrampoline<void, GameConfigData*, al::ByamlWriter*> saveWriteHook = hk::hook::trampoline([](GameConfigData* cfgData, al::ByamlWriter* writer) -> void {
    saveWriteHook.orig(cfgData, writer);

    const char* serverIP = Client::getCurrentIP();
    const int serverPort = Client::getCurrentPort();
    const bool serverHidden = Client::isServerHidden();
    const bool capCollision = StageSceneStateModConfig::isCapCollisionEnabled();
    const bool capBounce = StageSceneStateModConfig::isCapBounceEnabled();
    const bool playerCollision = StageSceneStateModConfig::isPuppetCollisionEnabled();
    const bool playerBounce = StageSceneStateModConfig::isPuppetBounceEnabled();
    const bool costumeDoorsUnlocked = StageSceneStateModConfig::isCostumeDoorsUnlocked();
    const bool lowLatency = StageSceneStateModConfig::isLowLatencyEnabled();
    const bool music = !Client::isMusicDisabled();
    const int defaultMode = Client::getDefaultGameMode();

    const char* apClientIP = Client::getApClientIP();
    const char* archipelagoHost = Client::getArchipelagoHost();
    int archipelagoPort = Client::getArchipelagoPort();
    const char* archipelagoSlot = Client::getArchipelagoSlot();
    const char* archipelagoPassword = Client::getArchipelagoPassword();

    writer->pushHash("SMOOData");
    if (serverIP) {
        writer->addString("ServerIP", serverIP);
    } else {
        writer->addString("ServerIP", "127.0.0.1");
    }

    if (serverPort) {
        writer->addInt("ServerPort", serverPort);
    } else {
        writer->addInt("ServerPort", 0);
    }

    writer->addBool("ServerHidden", serverHidden);
    writer->addBool("CapCollision", capCollision);
    writer->addBool("CapBounce", capBounce);
    writer->addBool("PlayerCollision", playerCollision);
    writer->addBool("PlayerBounce", playerBounce);
    writer->addBool("CostumeDoorsUnlocked", costumeDoorsUnlocked);
    writer->addBool("LowLatency", lowLatency);
    writer->addBool("Music", music);
    writer->addInt("DefaultGameMode", defaultMode);
    writer->pop();

    writer->pushHash("ArchipelagoData");
    if (apClientIP) {
        writer->addString("ApClientIP", apClientIP);
    } else {
        writer->addString("ApClientIP", "127.0.0.1");
    }

    if (archipelagoHost) {
        writer->addString("ArchipelagoHost", archipelagoHost);
    } else {
        writer->addString("ArchipelagoHost", "archipelago.gg");
    }

    writer->addInt("ArchipelagoPort", archipelagoPort);

    if (archipelagoSlot) {
        writer->addString("ArchipelagoSlot", archipelagoSlot);
    } else {
        writer->addString("ArchipelagoSlot", "");
    }

    if (archipelagoPassword) {
        writer->addString("ArchipelagoPassword", archipelagoPassword);
    } else {
        writer->addString("ArchipelagoPassword", "");
    }

    writer->pop();
});

static HkTrampoline<void, GameConfigData*, const al::ByamlIter&> saveReadHook = hk::hook::trampoline([](GameConfigData* cfgData, const al::ByamlIter& iter) -> void {
    saveReadHook.orig(cfgData, iter);

    const char* serverIP = "";
    int serverPort = 0;
    bool serverHidden = false;
    bool capCollision = false;
    bool capBounce = false;
    bool playerCollision = true;
    bool playerBounce = true;
    bool costumeDoorsUnlocked = true;
    bool lowLatency = false;
    bool music = true;
    int defaultMode = 0;

    const char* apClientIP = "";
    const char* archipelagoHost = "";
    int archipelagoPort = 0;
    const char* archipelagoSlot = "";
    const char* archipelagoPassword = "";

    al::ByamlIter iterIntern;
    al::tryGetByamlIterByKey(&iterIntern, iter, "SMOOData");

    if (al::tryGetByamlString(&serverIP, iterIntern, "ServerIP")) {
        Client::setLastUsedIP(serverIP);
    }

    if (al::tryGetByamlS32(&serverPort, iterIntern, "ServerPort")) {
        Client::setLastUsedPort(serverPort);
    }

    if (al::tryGetByamlBool(&serverHidden, iterIntern, "ServerHidden")) {
        Client::setServerHidden(serverHidden);
    }

    if (al::tryGetByamlBool(&capCollision, iterIntern, "CapCollision")) {
        StageSceneStateModConfig::setCapCollisionEnabled(capCollision);
    }
    if (al::tryGetByamlBool(&capBounce, iterIntern, "CapBounce")) {
        StageSceneStateModConfig::setCapBounceEnabled(capBounce);
    }
    if (al::tryGetByamlBool(&playerCollision, iterIntern, "PlayerCollision")) {
        StageSceneStateModConfig::setPuppetCollisionEnabled(playerCollision);
    }
    if (al::tryGetByamlBool(&playerBounce, iterIntern, "PlayerBounce")) {
        StageSceneStateModConfig::setPuppetBounceEnabled(playerBounce);
    }
    if (al::tryGetByamlBool(&costumeDoorsUnlocked, iterIntern, "CostumeDoorsUnlocked")) {
        StageSceneStateModConfig::setCostumeDoorsUnlocked(costumeDoorsUnlocked);
    }
    if (al::tryGetByamlBool(&lowLatency, iterIntern, "LowLatency")) {
        StageSceneStateModConfig::setLowLatencyEnabled(lowLatency);
    }
    if (al::tryGetByamlBool(&music, iterIntern, "Music")) {
        if (Client::isMusicDisabled() != !music) {
            Client::toggleMusicDisabled();
        }
    }
    if (al::tryGetByamlS32(&defaultMode, iterIntern, "DefaultGameMode")) {
        Client::setDefaultGameMode(defaultMode);
    }

    al::tryGetByamlIterByKey(&iterIntern, iter, "ArchipelagoData");

    if (al::tryGetByamlString(&apClientIP, iterIntern, "ApClientIP")) {
        Client::setApClientIP(apClientIP);
    }

    if (al::tryGetByamlString(&archipelagoHost, iterIntern, "ArchipelagoHost")) {
        Client::setArchipelagoHost(archipelagoHost);
    }

    if (al::tryGetByamlS32(&archipelagoPort, iterIntern, "ArchipelagoPort")) {
        Client::setArchipelagoPort(archipelagoPort);
    }

    if (al::tryGetByamlString(&archipelagoSlot, iterIntern, "ArchipelagoSlot")) {
        Client::setArchipelagoSlot(archipelagoSlot);
    }

    if (al::tryGetByamlString(&archipelagoPassword, iterIntern, "ArchipelagoPassword")) {
        Client::setArchipelagoPassword(archipelagoPassword);
    }
});

static HkTrampoline<void, Shine*> registerShineToListHook = hk::hook::trampoline([](Shine* shine) -> void {
    registerShineToListHook.orig(shine);
    if (shine->mShineIdx >= 0) {
        Client::tryRegisterShine(shine);
    }
});

static HkTrampoline<void, CoinCollect*> registerCoinCollectToListHook = hk::hook::trampoline([](CoinCollect* coin) -> void {
    registerCoinCollectToListHook.orig(coin);
    Client::tryRegisterCoinCollect(coin);
});

static HkTrampoline<void, CoinCollect2D*> registerCoinCollect2DToListHook = hk::hook::trampoline([](CoinCollect2D* coin) -> void {
    registerCoinCollect2DToListHook.orig(coin);
    Client::tryRegisterCoinCollect2D(coin);
});

static HkReplace<void, StageSceneStatePauseMenu*> overrideHelpFadeNerve = hk::hook::replace([](StageSceneStatePauseMenu* state) -> void {
    // Set label in menu inside
    // LocalizedData/${lang}/MessageData/LayoutMessage.szs/Menu.msbt/Menu_Help
    state->exeModConfig();
    al::setNerve(state, &NrvStageSceneStatePauseMenu.ModConfig);
});

static StageSceneStateModConfig* sceneStateModConfig = nullptr;

static HkTrampoline<void, StageSceneStateOption*, const char*, al::Scene*, const al::LayoutInitInfo&, FooterParts*, GameDataHolder*, bool> initStateHook = hk::hook::trampoline([](StageSceneStateOption* thisPtr, const char* stateName, al::Scene* host, const al::LayoutInitInfo& initInfo, FooterParts* footer,
                                                                                                                                                                                    GameDataHolder* data, bool unkBool) -> void {
    initStateHook.orig(thisPtr, stateName, host, initInfo, footer, data, unkBool);
    sceneStateModConfig = new StageSceneStateModConfig("ModConfig", host, initInfo, footer, data, unkBool);
});

static HkTrampoline<void, StageSceneStatePauseMenu*, const char*, al::Scene*, al::SimpleLayoutAppearWaitEnd*, GameDataHolder*, const al::SceneInitInfo&,
    const al::ActorInitInfo&, const al::LayoutInitInfo&, al::WindowConfirm*, StageSceneLayout*, bool, SceneAudioSystemPauseController*>
    initNerveStateHook = hk::hook::trampoline([](StageSceneStatePauseMenu* state, const char* name, al::Scene* host, al::SimpleLayoutAppearWaitEnd* menuLayout,
                                                  GameDataHolder* gameDataHolder, const al::SceneInitInfo& sceneInitInfo, const al::ActorInitInfo& actorInitInfo,
                                                  const al::LayoutInitInfo& layoutInitInfo, al::WindowConfirm* windowConfirm, StageSceneLayout* stageSceneLayout,
                                                  bool isTitle, SceneAudioSystemPauseController* sceneAudioSystemPauseController) -> void {
        initNerveStateHook.orig(state, name, host, menuLayout, gameDataHolder, sceneInitInfo, actorInitInfo, layoutInitInfo, windowConfirm, stageSceneLayout,
            isTitle, sceneAudioSystemPauseController);

        al::initNerveState(state, sceneStateModConfig, &NrvStageSceneStatePauseMenu.ModConfig, "CustomNerveOverride");
    });

// skips starting both coin counters
static HkTrampoline<void, CoinCounter*> startCoinCounterHook = hk::hook::trampoline([](CoinCounter* counter) -> void {
    if (!GameModeManager::instance()->isModeRequireUI()) {
        startCoinCounterHook.orig(counter);
    }
});

// Simple hook that can be used to override isModeE3 checks to enable/disable certain behaviors
static bool modeE3Hook() {
    return GameModeManager::instance()->isModeRequireUI();
}

// Gravity Hooks

static void initHackCapHook(al::LiveActor* cappy) {
    al::initActorPoseTQGSV(cappy);
}

// Skips ending the play guide layout if a mode is active, since the mode would have already ended
// it
static void playGuideEndHook(al::SimpleLayoutAppearWaitEnd* thisPtr) {
    if (!GameModeManager::instance()->isModeRequireUI()) {
        thisPtr->end();
    }
}

static HkTrampoline<void, StageScene*, al::SceneInitInfo*> stageSceneInitHook = hk::hook::trampoline([](StageScene* curScene, al::SceneInitInfo* initInfo) -> void {
    stageSceneInitHook.orig(curScene, initInfo);
    // if (GameModeManager::instance()->isMode(GameMode::HIDEANDSEEK)) {
    //     al::CameraDirector* director = curScene->getCameraDirector();
    //     if (director) {
    //         if (director->mPoserFactory) {
    //             al::CameraTicket* gravityCamera = director->createCameraFromFactory(
    //                 "CameraPoserCustom", nullptr, 0, 5, sead::Matrix34f::ident);

    //             HideAndSeekMode* mode =
    //             GameModeManager::instance()->getMode<HideAndSeekMode>();

    //             mode->setCameraTicket(gravityCamera);
    //         }
    //     }
    // }

    // if (GameModeManager::instance()->isMode(GameMode::FREEZETAG) ||
    //     GameModeManager::instance()->isMode(GameMode::HIDEANDSEEK)) {
    //     al::CameraDirector* director = curScene->getCameraDirector();
    //     if (director && director->mPoserFactory) {
    //         al::CameraTicket* spectateCamera = director->createCameraFromFactory(
    //             "CameraPoserActorSpectate", nullptr, 0, 5, sead::Matrix34f::ident);

    //         if (GameModeManager::instance()->isMode(GameMode::FREEZETAG)) {
    //             FreezeTagMode* mode = GameModeManager::instance()->getMode<FreezeTagMode>();
    //             mode->setCameraTicket(spectateCamera);
    //         } else if (GameModeManager::instance()->isMode(GameMode::HIDEANDSEEK)) {
    //             HideAndSeekMode* mode =
    //             GameModeManager::instance()->getMode<HideAndSeekMode>();
    //             mode->setCameraTicket(spectateCamera);
    //         }
    //     }
    // }
    // if (GameModeManager::instance()->isMode(GameMode::SHINETHIEF)) {
    //     al::CameraDirector* director = curScene->getCameraDirector();
    //     if (director && director->mPoserFactory) {
    //         al::CameraTicket* spectateCamera = director->createCameraFromFactory(
    //             "CameraPoserActorSpectate", nullptr, 0, 5, sead::Matrix34f::ident);

    //         ShineThiefMode* mode = GameModeManager::instance()->getMode<ShineThiefMode>();
    //         mode->setCameraTicket(spectateCamera);
    //     }
    // }
});

static HkTrampoline<void, WorldEndBorderKeeper*> borderPullBackHook = hk::hook::trampoline([](WorldEndBorderKeeper* keeper) -> void {
    // if (al::isFirstStep(keeper) && GameModeManager::instance()->isActive()) {
    //     if (GameModeManager::instance()->isModeAndActive(GameMode::HIDEANDSEEK)) {
    //         HideAndSeekMode* mode = GameModeManager::instance()->getMode<HideAndSeekMode>();

    //         if (mode->isUseGravity()) {
    //             killMainPlayer(keeper->mActor);
    //         }
    //     }
    // }
    borderPullBackHook.orig(keeper);
});

constexpr static al::ExecuteTable DrawTableCustom[] = {
    createDrawTable("OnlineDrawExecutors", "PuppetActor", "ActorModelDrawDeferred", "PuppetActor", "ActorModelDrawDeferred")
};

constexpr static al::ExecuteTable UpdateTableCustom[] = {
    createUpdateTable("OnlineUpdateExecutors", "PuppetActor", "PuppetActor"),
};

static HkTrampoline<void, al::ExecuteDirector*, const al::ExecuteSystemInitInfo&> drawTableHook = hk::hook::trampoline([](al::ExecuteDirector* director, const al::ExecuteSystemInitInfo& initInfo) -> void {
    drawTableHook.orig(director, initInfo);

    constexpr s32 UpdateTableSize = sizeof(UpdateTableCustom) / sizeof(UpdateTableCustom[0]);
    al::ExecuteTableHolderUpdate** updateTables = new al::ExecuteTableHolderUpdate*[director->mUpdateTableCount + UpdateTableSize]();

    for (s32 i = 0; i < director->mUpdateTableCount; i++) {
        updateTables[i] = director->mUpdateTables[i];
    }
    for (s32 i = 0; i < UpdateTableSize; i++) {
        updateTables[director->mUpdateTableCount + i] = new al::ExecuteTableHolderUpdate();
        const al::ExecuteTable& curTable = UpdateTableCustom[i];
        updateTables[director->mUpdateTableCount + i]->init(curTable.name, initInfo, curTable.executeOrders, curTable.executeOrderCount);
    }
    director->mUpdateTableCount += UpdateTableSize;
    director->mUpdateTables = updateTables;

    constexpr s32 DrawTableSize = sizeof(DrawTableCustom) / sizeof(DrawTableCustom[0]);
    al::ExecuteTableHolderDraw** drawTables = new al::ExecuteTableHolderDraw*[director->mDrawTableCount + DrawTableSize]();
    for (s32 i = 0; i < director->mDrawTableCount; i++) {
        drawTables[i] = director->mDrawTables[i];
    }
    for (s32 i = 0; i < DrawTableSize; i++) {
        drawTables[director->mDrawTableCount + i] = new al::ExecuteTableHolderDraw();
        const al::ExecuteTable& curTable = DrawTableCustom[i];
        drawTables[director->mDrawTableCount + i]->init(curTable.name, initInfo, curTable.executeOrders, curTable.executeOrderCount);
    }
    director->mDrawTableCount += DrawTableSize;
    director->mDrawTables = drawTables;
});

static HkTrampoline<bool, al::IUseStageSwitch*, const char*, const al::FunctorBase&> unlockCostumeDoorsHook = hk::hook::trampoline([](al::IUseStageSwitch* user, const char* eventName, const al::FunctorBase& action) -> bool {
    if (strcmp(eventName, "OpenKeySwitch") == 0 && StageSceneStateModConfig::isCostumeDoorsUnlocked())
        return false;
    return unlockCostumeDoorsHook.orig(user, eventName, action);
});

static bool unlockCostumeDoorMetroHook(const char* str1, const char* str2) {
    if (StageSceneStateModConfig::isCostumeDoorsUnlocked())
        return true;
    return al::isEqualString(str1, str2);
}

static HkTrampoline<bool, al::Triangle&, char*> icePhysicsHook = hk::hook::trampoline([](al::Triangle& triangle, char* floorCode) -> bool {
    if (strcmp(floorCode, "Skate") != 0)
        return icePhysicsHook.orig(triangle, floorCode);

    bool isNaturalIce = icePhysicsHook.orig(triangle, floorCode);

    return isNaturalIce ? true : TwistsConfig::isIcePhysicsEnabled();
});

static HkTrampoline<void, StageSceneStatePauseMenu*> pauseMenuWaitHook = hk::hook::trampoline([](StageSceneStatePauseMenu* menu) -> void {
    if (al::isFirstStep(menu))
        menu->mSelectParts->setSelectMessage(2, u"Mod Menu");

    pauseMenuWaitHook.orig(menu);

    if (!menu->isDrawLayout()) {
        ConnectionStatus::sInstance->tryStart();
    } else {
        ConnectionStatus::sInstance->tryEnd();
    }
});

static HkTrampoline<void, AppearSwitchTimer*, const al::ActorInitInfo&, const al::IUseAudioKeeper*, al::IUseStageSwitch*, al::IUseCamera*, al::LiveActor*>
    disableAppearSwitchCameraHook = hk::hook::trampoline([](AppearSwitchTimer* timer, const al::ActorInitInfo& initInofo, const al::IUseAudioKeeper* audio,
                                                             al::IUseStageSwitch* stageSwitch, al::IUseCamera* camera, al::LiveActor* actor) -> void {
        disableAppearSwitchCameraHook.orig(timer, initInofo, audio, stageSwitch, camera, actor);
        if (!StageSceneStateModConfig::isSpeedrunModeEnabled())
            timer->mDemoCameraFrame = 0;
    });

static HkTrampoline<bool, al::WindowConfirmWait*> windowConfirmWaitHook = hk::hook::trampoline([](al::WindowConfirmWait* win) -> bool {
    al::setNerve(win, (al::Nerve*)(hk::ro::getMainModule()->range().start() + 0x1e05be8));
    return true;
});

namespace speedrun {

    static bool isHooksCreated = false;

    static constexpr u32 listPtrNop[] = {
        0x4DB934,
        0x2D250C,
    };

    static hk::hook::a64::AsmBlock<true, 1>* listNop[hk::util::arraySize(listPtrNop)];

    static void uninstallHooks() {
        for (int i = 0; i < hk::util::arraySize(listPtrNop); i++) {
            listNop[i]->uninstall();
        }
    }

    static void createHooks() {
        if (!isHooksCreated) {
            for (int i = 0; i < hk::util::arraySize(listPtrNop); i++) {
                listNop[i] = new (imgui::sImGuiHeap) hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"nop", true>());
            }
            isHooksCreated = true;
        }
    }

    static void installHooks() {
        for (int i = 0; i < hk::util::arraySize(listPtrNop); i++) {
            listNop[i]->installAtMainOffset(listPtrNop[i]);
        }
    }

} // namespace speedrun