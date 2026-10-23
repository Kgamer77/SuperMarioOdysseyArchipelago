#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"

#include "al/Library/Event/EventFlowFunction.h"
#include "al/Library/Event/EventFlowNode.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutPartsActorKeeper.h"
#include "al/Library/Layout/LayoutSceneInfo.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Placement/PlacementInfo.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/Stage/IUseStageSwitch.h"
#include "al/Library/Thread/FunctorV0M.h"

#include "game/Layout/StageSceneLayout.h"
#include "game/MapObj/AppearSwitchTimer.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/ShineTowerRocket.h"
#include "game/MapObj/WorldWarpHole.h"
#include "game/Npc/VolleyballNpc.h"
#include "game/Scene/StageScene.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameSystem.h"
#include "game/Util/SensorMsgFunction.h"

#include "EntranceDB2.h"
#include "Randomizer/FeatureFlags.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/StartPositionRando.h"
#include "System/GameDataFunction.h"
#include "helpers.hpp"

// ── QOL patches ───────────────────────────────────────────────────────────────

namespace {
    // GrowFlowerPot keeps its stems (plus the trailing berry) in a
    // sead::PtrArrayImpl: size, capacity, then the buffer pointer.
    constexpr s32 cGrowFlowerPotPartsNumOffset = 0x108;
    constexpr s32 cGrowFlowerPotPartsBufferOffset = 0x110;

    // How long the whole plant should take to shoot up, in frames.
    constexpr f32 cGrowFlowerGrowFrames = 180.0f; // 3 seconds at 60 fps

    // Handing the pot its full grow level (below) does not make the plant pop
    // into place: GrowFlowerPot only ever raises its target one level per
    // Grow/GrowWait cycle, and each level plays one Grow action on the next
    // stem. A 24-stem pot therefore crawls through two dozen animations at
    // vanilla speed. Rescale those actions so the sequence fits the budget
    // above regardless of how many stems the pot has.
    void applyFastSeedGrowth(void* pot) {
        if (!pot)
            return;

        char* base = static_cast<char*>(pot);
        const s32 partsNum = *reinterpret_cast<s32*>(base + cGrowFlowerPotPartsNumOffset);
        al::LiveActor** parts = *reinterpret_cast<al::LiveActor***>(base + cGrowFlowerPotPartsBufferOffset);
        if (partsNum <= 0 || !parts)
            return;

        const f32 framesPerLevel = cGrowFlowerGrowFrames / partsNum;
        for (s32 i = 0; i < partsNum; i++) {
            al::LiveActor* part = parts[i];
            if (!part)
                continue;

            // Only the growth actions growSetting just (re)started -- Grow00
            // through Grow02 and the ReactionGrow variants. The stems' idle
            // Wait animations have to keep playing at vanilla speed, and
            // nothing resets a frame rate once the plant is done.
            const char* actionName = al::getActionName(part);
            if (!actionName || !strstr(actionName, "Grow"))
                continue;

            // Only ever speed a part up; a pot with few stems keeps vanilla
            // pacing rather than being slowed down to fill the budget.
            const f32 frameMax = al::getActionFrameMax(part);
            if (frameMax > framesPerLevel)
                al::setActionFrameRate(part, frameMax / framesPerLevel);
        }
    }
} // namespace

static HkTrampoline<void, void*> growFlowerTryMaxHook = hk::hook::trampoline([](void* pot) -> void { growFlowerTryMaxHook.orig(pot); });

static HkTrampoline<void, void*> growFlowerControlHook = hk::hook::trampoline([](void* pot) -> void {
    growFlowerControlHook.orig(pot);
    growFlowerTryMaxHook.orig(pot);
});

// GrowFlowerPot::growSetting starts the Grow action for the level the pot is
// entering (and re-starts the ones already reached), so it is the one place
// that owns the growth animation's speed.
static HkTrampoline<void, void*> growFlowerGrowSettingHook = hk::hook::trampoline([](void* pot) -> void {
    growFlowerGrowSettingHook.orig(pot);
    applyFastSeedGrowth(pot);
});

static HkTrampoline<void, al::IUseSceneObjHolder*> disableActionGuideHook = hk::hook::trampoline([](al::IUseSceneObjHolder* holder) -> void {});

static HkTrampoline<void, AppearSwitchTimer*, const al::ActorInitInfo&, const al::IUseAudioKeeper*, al::IUseStageSwitch*, al::IUseCamera*, al::LiveActor*>
    disableAppearSwitchCameraHook = hk::hook::trampoline([](AppearSwitchTimer* timer, const al::ActorInitInfo& initInfo, const al::IUseAudioKeeper* audio,
                                                             al::IUseStageSwitch* stageSwitch, al::IUseCamera* camera, al::LiveActor* actor) -> void {
        disableAppearSwitchCameraHook.orig(timer, initInfo, audio, stageSwitch, camera, actor);
        // Runtime-toggleable QoL (Skip Cutscene Cameras): zero the demo camera
        // frame so the reveal pan is skipped. When off, leave vanilla timing.
        if (Randomizer::isSkipCutsceneCamerasEnabled())
            timer->mDemoCameraFrame = 0;
    });

// EnemySwapHooks also needs this exact overload when chaos randomization is
// compiled in. Hakkun trampolines cannot be stacked: the second hook would
// copy the first hook's PC-relative branch without relocating it. In that
// configuration EnemySwapHooks owns the one combined hook instead.
#if !SMOR_FEATURE_CHAOS_ANY
static HkTrampoline<bool, bool*, const al::ActorInitInfo&, const char*> tryGetArgBoolHook = hk::hook::trampoline([](bool* out, const al::ActorInitInfo& info, const char* name) -> bool {
    if (strcmp(name, "IsAfterClearOnly") == 0) {
        *out = false;
        return false;
    }
    return tryGetArgBoolHook.orig(out, info, name);
});
#endif

static HkTrampoline<bool, al::IUseStageSwitch*, const char*, const al::FunctorBase&> unlockCostumeDoorsHook = hk::hook::trampoline([](al::IUseStageSwitch* user, const char* eventName, const al::FunctorBase& action) -> bool {
    // Costume doors (all except metro): skip registering the OpenKeySwitch lock
    // listener so the door always stays open.
    if (strcmp(eventName, "OpenKeySwitch") == 0)
        return false;
    return unlockCostumeDoorsHook.orig(user, eventName, action);
});

// Metro's costume door is the construction-site door in New Donk City
// (CityWorldHomeDoor004 at -5300/0/-10950, the one in front of the
// ElectricWireExStage warp for "Off the Beaten Wire"). Its SwitchStart switch
// obj6483 is driven by obj6480, a TalkNpcCityMan with CostumeName MarioMaker
// and CapType 25 -- the Builder Outfit and Helmet.
//
// It is a plain DoorCity, exactly like the ~45 decorative New Donk City
// building doors, so DoorCity::init's InitState check cannot be forced open
// blindly: that opens every door in the kingdom. DoorCity::init is the only
// place that can tell the placements apart, so latch the answer there and let
// the redirect below read it back.
static constexpr const char* cMetroCostumeDoorUnitName = "CityWorldHomeDoor004";
static constexpr const char* cMetroCostumeDoorStage = "CityWorldHomeStage";
static constexpr const char* cMetroCostumeDoorChangeStageId = "densen";
static constexpr const char* cMetroCostumeDoorTargetStage = "ElectricWireExStage";

// ...and only in the scenarios that actually put a loading zone behind it.
// CityWorldHomeStageMap ships the door twice with the same UnitConfigName and
// InitState=Close - obj10374 for the scenarios that place the densen
// ChangeStageArea, obj10373 for the ones that do not - so matching the unit
// name alone also opens the door onto an empty construction site. Gate on the
// entrance table's own scenario mask for densen rather than re-listing the
// scenarios here, so this cannot drift from the entrance data.
static bool isMetroCostumeDoorZonePlaced(const al::ActorInitInfo& initInfo) {
    GameDataHolder* holder = static_cast<GameDataHolder*>(initInfo.actorSceneInfo.gameDataHolder);
    GameDataFile* file = holder ? holder->getGameDataFile() : nullptr;
    if (!file)
        return false;

    const EntranceDB2::Endpoint* endpoint = EntranceDB2::findEndpoint(cMetroCostumeDoorStage, cMetroCostumeDoorChangeStageId, cMetroCostumeDoorTargetStage);
    return endpoint && EntranceDB2::endpointSupportsScenario(*endpoint, file->getScenarioNo());
}

static bool sIsMetroCostumeDoorInit = false;

static HkTrampoline<void, void*, const al::ActorInitInfo&> doorCityInitHook = hk::hook::trampoline([](void* door, const al::ActorInitInfo& initInfo) -> void {
    const char* unitConfigName = nullptr;
    if (initInfo.placementInfo)
        initInfo.placementInfo->getPlacementIter().tryGetStringByKey(&unitConfigName, "UnitConfigName");
    sIsMetroCostumeDoorInit = unitConfigName && strcmp(unitConfigName, cMetroCostumeDoorUnitName) == 0 && isMetroCostumeDoorZonePlaced(initInfo);

    doorCityInitHook.orig(door, initInfo);

    sIsMetroCostumeDoorInit = false;
});

// Replaces the al::isEqualString(InitState, "Open") call in DoorCity::init (+0x40):
// forcing it true makes the metro costume door init in the "Open" state. Every
// other DoorCity placement falls back to the original string comparison.
static bool unlockCostumeDoorMetroHook(const char* str1, const char* str2) {
    if (sIsMetroCostumeDoorInit)
        return true;
    return str1 && str2 && strcmp(str1, str2) == 0;
}

// Costume-moon conversations use these event-flow query nodes to select case 0
// (matching costume) or case 1 (wrong costume). Force the matching branch only
// while Free Costume Moon is enabled; all other event-flow checks stay vanilla.
static HkTrampoline<s32, const al::EventFlowNode*> checkCostumeMoonHook = hk::hook::trampoline([](const al::EventFlowNode* node) -> s32 {
    if (Randomizer::isFreeCostumeMoonEnabled())
        return al::getCaseEventNextId(node, 0);
    return checkCostumeMoonHook.orig(node);
});

static HkTrampoline<s32, const al::EventFlowNode*> checkCostumePairMoonHook = hk::hook::trampoline([](const al::EventFlowNode* node) -> s32 {
    if (Randomizer::isFreeCostumeMoonEnabled())
        return al::getCaseEventNextId(node, 0);
    return checkCostumePairMoonHook.orig(node);
});

static HkTrampoline<bool, al::IUseSceneObjHolder*> tryWearCostumeRandomIfEnableHook = hk::hook::trampoline([](al::IUseSceneObjHolder* holder) -> bool {
    if (Randomizer::isRandomizeOutfitsEnabled()) {
        GameDataFunction::wearCostumeRandom(holder);
        return true;
    } else
        return tryWearCostumeRandomIfEnableHook.orig(holder);
});

static HkTrampoline<bool, al::IUseSceneObjHolder*> tryWearCapRandomIfEnableHook = hk::hook::trampoline([](al::IUseSceneObjHolder* holder) -> bool {
    if (Randomizer::isRandomizeOutfitsEnabled()) {
        GameDataFunction::wearCapRandom(holder);
        return true;
    } else
        return tryWearCapRandomIfEnableHook.orig(holder);
});

namespace {
    constexpr s32 cCollectAnimalTargetCountOffset = 0x108;
    constexpr s32 cCollectAnimalInFenceCapacityOffset = 0x11C;

    void applyOneSheepHerdingThreshold(void* watcher) {
        if (!watcher)
            return;

        char* base = static_cast<char*>(watcher);
        const s32 targetCount = *reinterpret_cast<s32*>(base + cCollectAnimalTargetCountOffset);
        if (targetCount <= 0)
            return;

        s32& requiredCount = *reinterpret_cast<s32*>(base + cCollectAnimalInFenceCapacityOffset);
        requiredCount = Randomizer::isOneSheepHerdingEnabled() && targetCount > 1 ? 1 : targetCount;
    }
} // namespace

static HkTrampoline<u64, void*> collectAnimalWatcherUpdateHintsHook = hk::hook::trampoline([](void* watcher) -> u64 {
    const u64 result = collectAnimalWatcherUpdateHintsHook.orig(watcher);
    applyOneSheepHerdingThreshold(watcher);
    return result;
});

static HkTrampoline<void, void*, const al::ActorInitInfo&> jumpingRopeNpcInitHook = hk::hook::trampoline([](void* npc, const al::ActorInitInfo& initInfo) -> void {
    jumpingRopeNpcInitHook.orig(npc, initInfo);
    if (!npc || !Randomizer::isEasyMinigamesEnabled())
        return;

    char* base = static_cast<char*>(npc);
    *reinterpret_cast<s32*>(base + 0x200) = 10;
    *reinterpret_cast<s32*>(base + 0x204) = 25;
});

static HkTrampoline<void, VolleyballNpc*, const al::ActorInitInfo&> volleyballNpcInitHook = hk::hook::trampoline([](VolleyballNpc* npc, const al::ActorInitInfo& initInfo) -> void {
    volleyballNpcInitHook.orig(npc, initInfo);
    if (!npc || !Randomizer::isEasyMinigamesEnabled())
        return;

    npc->mClearCount = 10;
    npc->mSuperClearCount = 25;
});

class MapLayout;

// "Always Enable Warping" replaces every gate in
// MapLayout::isEnableCheckpointWarp - among them rs::isEnableOpenMap, which is
// what normally refuses a checkpoint warp while Mario is airborne or otherwise
// not in a settled state. A Nuzlocke run cannot have that: warping out of midair
// is a free escape from a fall that is about to become a death, which is exactly
// the moment the mode is built around. Those two modes keep vanilla's rules -
// with a single exception.
//
// Vanilla's last gate is `!rs::isSceneStatusBossBattle`, which seals the map for
// the whole of a boss fight. Handing the Nuzlocke modes the vanilla check gave
// that gate back along with the airborne one, but it is not what the midair rule
// protects, and warping out of a boss is meant to stay available. So it is
// suppressed for the duration of the vanilla check only: the hook below reports
// "not a boss battle" while sInCheckpointWarpGate is set, leaving every other
// gate - the airborne one included - exactly as the game wrote it. Both hooks
// run on the game's single logic thread, so the flag needs no guarding.
static bool sInCheckpointWarpGate = false;

static HkTrampoline<bool, u64> isSceneStatusBossBattleHook = hk::hook::trampoline([](u64 accessor) -> bool {
    if (sInCheckpointWarpGate)
        return false;
    return isSceneStatusBossBattleHook.orig(accessor);
});

static HkTrampoline<bool, const MapLayout*> mapIsEnableCheckpointWarpHook = hk::hook::trampoline([](const MapLayout* mapLayout) -> bool {
    if (!Randomizer::isNuzlockeRunMode())
        return true;

    sInCheckpointWarpGate = true;
    const bool isEnabled = mapIsEnableCheckpointWarpHook.orig(mapLayout);
    sInCheckpointWarpGate = false;
    return isEnabled;
});

// MapLayout::isEnableCheckpointWarp only decides whether the warp UI shows up.
// The A press itself runs through StageSceneStateStageMap::tryCheckpointWarp,
// which asks GameDataFunction::isEnableCheckpointWarp instead - a plain read of
// a saved byte that StateGateKeeperBattleStartDemo::prepareDemo clears when a
// boss fight opens and StateGateKeeperBattleEndDemo::appear puts back. It never
// consults rs::isSceneStatusBossBattle, so the suppression above cannot reach
// it, and the map would offer a warp that the confirm button then refuses.
// tryCheckpointWarp is this function's only caller and the byte is only ever 0
// during a boss demo, so reporting it enabled is exactly "boss fights no longer
// seal the warp" and nothing else. Every remaining gate on the press - the icon
// kind, Jango's stolen cap, an in-progress race - is left alone.
static HkTrampoline<bool, u64> dataIsEnableCheckpointWarpHook = hk::hook::trampoline([](u64 accessor) -> bool { return true; });

static HkTrampoline<bool, const GameDataHolder*, s32, s32> checkIsOpenWorldWarpHoleInScenarioHook = hk::hook::trampoline([](const GameDataHolder* holder, s32 worldId, s32 scenarioNo) -> bool {
    if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
        return true;
    return PaintingRando::isAnyPaintingUnlockedForWorld(holder ? holder->getGameDataFile() : nullptr, worldId);
});

static HkTrampoline<bool, const WorldWarpHole*> checkIsOpenWorldWarpHoleCurrentScenarioHook = hk::hook::trampoline([](const WorldWarpHole* worldWarpHole) -> bool {
    if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
        return true;
    return PaintingRando::isWorldWarpHoleUnlocked(worldWarpHole);
});

static HkTrampoline<s32, const GameDataFile*> calcWorldWarpHoleThroughNumHook = hk::hook::trampoline([](const GameDataFile* file) -> s32 { return PaintingRando::calcUnlockedPaintingCount(file); });

static HkTrampoline<s32, const GameDataFile*> getWorldWarpHoleThroughNumMaxHook = hk::hook::trampoline(
    [](const GameDataFile* file) -> s32 { return Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled() ? PaintingRando::getPaintingCount() : 999; });

static HkTrampoline<bool, const al::LiveActor*, const char*> checkSavedHintPhotoHook = hk::hook::trampoline([](const al::LiveActor* actor, const char* photoName) -> bool {
    if (Randomizer::isUnlockHintArtEnabled())
        return true;
    return checkSavedHintPhotoHook.orig(actor, photoName);
});

static HkTrampoline<void, StageSceneLayout*> stageSceneLayoutUpdateCounterPartsHook = hk::hook::trampoline([](StageSceneLayout* layout) -> void {
    // ShineCounter internally builds a GameDataHolderAccessor from its
    // LayoutSceneInfo->SceneObjHolder.  During scene teardown that pointer
    // becomes null while the layout still exists, crashing in
    // rs::getWorldShinePictureFont.  Skip the update when unavailable.
    if (layout->mShineCountLyt) {
        al::LayoutActor* shineActor = reinterpret_cast<al::LayoutActor*>(layout->mShineCountLyt);
        al::LayoutSceneInfo* sceneInfo = shineActor->getLayoutSceneInfo();
        if (!sceneInfo || !sceneInfo->getSceneObjHolder())
            return;
    }
    stageSceneLayoutUpdateCounterPartsHook.orig(layout);

    StageScene* scene = getStageScene();
    if (scene && StartPositionRando::shouldHideOpeningHud(scene->mStageName.cstr())) {
        if (layout->mShineCountLyt)
            reinterpret_cast<al::LayoutActor*>(layout->mShineCountLyt)->kill();
        if (layout->mCoinCollectLyt)
            reinterpret_cast<al::LayoutActor*>(layout->mCoinCollectLyt)->kill();
        return;
    }

    if (Randomizer::isTrackerOverlayEnabled() && Randomizer::isTrackerRightAligned()) {
        if (layout->mMapMiniLyt) {
            al::LayoutActor* mapMini = reinterpret_cast<al::LayoutActor*>(layout->mMapMiniLyt);
            if (mapMini->isAlive()) {
                mapMini->kill();
            }
            if (mapMini->mLayoutPartsActorKeeper) {
                for (int i = 0; i < mapMini->mLayoutPartsActorKeeper->mNumActors; i++) {
                    al::LayoutActor* child = mapMini->mLayoutPartsActorKeeper->mPartsActors[i];
                    if (child && al::isEqualString(child->getName(), "Compass")) {
                        if (child->isAlive()) {
                            child->kill();
                        }
                    }
                }
            }
        }
        if (layout->mKidsModeLyt && layout->mKidsModeLyt->isAlive()) {
            layout->mKidsModeLyt->kill();
        }
    }
});

void InstallQolPatches() {
    PurpleCoinRewards::installHooks();

    growFlowerTryMaxHook.template installAtSym<"_ZN13GrowFlowerPot15tryMaxGrowLevelEv">(); // Grow Seeds Instantly
    growFlowerControlHook.template installAtSym<"_ZN13GrowFlowerPot7controlEv">(); // Grow Seeds Instantly
    growFlowerGrowSettingHook.template installAtSym<"_ZN13GrowFlowerPot11growSettingEv">(); // Grow Seeds Instantly (3s growth)
    disableActionGuideHook.installAtSym<"_ZN2rs21requestShowHtmlViewerEPKN2al18IUseSceneObjHolderE">(); // Disable Action Guide / HtmlViewer
    disableAppearSwitchCameraHook.installAtSym<"R_ZN17AppearSwitchTimer4init">(); // Disable AppearSwitchTimer camera switch
    mapIsEnableCheckpointWarpHook.installAtSym<"_ZNK9MapLayout22isEnableCheckpointWarpEv">(); // Always Enable Warping (except Nuzlocke)
    isSceneStatusBossBattleHook.installAtMainOffset(0x50e068); // Nuzlocke: keep boss-fight warping
    dataIsEnableCheckpointWarpHook.installAtMainOffset(0x529c58); // Allow the warp confirm during boss fights
    hk::hook::a64::assemble<"nop">().installAtMainOffset(0x4DB934); // LifeUpMaxItem demo skip
    hk::hook::a64::assemble<"nop">().installAtMainOffset(0x2D250C); // Notes Demo Skip

    checkIsOpenWorldWarpHoleInScenarioHook.installAtSym<"_ZNK14GameDataHolder34checkIsOpenWorldWarpHoleInScenarioEii">(); // Unlock Paintings
    checkIsOpenWorldWarpHoleCurrentScenarioHook.installAtSym<"_ZN2rs39checkIsOpenWorldWarpHoleCurrentScenarioEPK13WorldWarpHole">(); // Unlock Paintings
    calcWorldWarpHoleThroughNumHook.installAtSym<"_ZNK12GameDataFile27calcWorldWarpHoleThroughNumEv">(); // Unlock Paintings
    getWorldWarpHoleThroughNumMaxHook.installAtSym<"_ZNK12GameDataFile29getWorldWarpHoleThroughNumMaxEv">(); // Unlock Paintings
    checkSavedHintPhotoHook.installAtMainOffset(0x00534330); // Unlock Hint Art
#if !SMOR_FEATURE_CHAOS_ANY
    tryGetArgBoolHook.installAtSym<"_ZN2al9tryGetArgEPbRKNS_13ActorInitInfoEPKc">(); // Unlock Paintings
#endif
    hk::hook::a64::assemble<"mov w25, #0x1">().installAtMainOffset(0x34d498); // Unlock Paintings
    hk::hook::a64::assemble<"nop">().installAtMainOffset(0x34d49c); // Unlock Paintings
    hk::hook::trampoline([]() -> bool { return true; }).installAtMainOffset(0x34d544); // Unlock Paintings

    unlockCostumeDoorsHook
        .template installAtSym<"_ZN2al19listenStageSwitchOnEPNS_15IUseStageSwitchEPKcRKNS_11FunctorBaseE">(); // Unlock costume doors (all except metro)
    doorCityInitHook.template installAtSym<"_ZN8DoorCity4initERKN2al13ActorInitInfoE">(); // Unlock costume doors (metro): scope the redirect below
    hk::hook::writeBranchLinkAtSym<"R_metroCostumeDoor">(unlockCostumeDoorMetroHook); // Unlock costume doors (metro)
    checkCostumeMoonHook.installAtMainOffset(0x001b429c); // Costume-moon single-costume check
    checkCostumePairMoonHook.installAtMainOffset(0x001b4448); // Costume-moon cap/outfit pair check

    collectAnimalWatcherUpdateHintsHook.installAtMainOffset(0x3850a0); // One-sheep herding moons
    jumpingRopeNpcInitHook.installAtMainOffset(0x0039b5f8); // Easy minigames
    volleyballNpcInitHook.installAtMainOffset(0x003ec830); // Easy minigames

    stageSceneLayoutUpdateCounterPartsHook.installAtSym<"_ZN16StageSceneLayout18updateCounterPartsEv">();
    tryWearCostumeRandomIfEnableHook.installAtSym<"_ZN16GameDataFunction28tryWearCostumeRandomIfEnableEPN2al18IUseSceneObjHolderE">();
    tryWearCapRandomIfEnableHook.installAtSym<"_ZN16GameDataFunction24tryWearCapRandomIfEnableEPN2al18IUseSceneObjHolderE">();
}
