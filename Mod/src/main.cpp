// ── Includes ──────────────────────────────────────────────────────────────────

#include "Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Effect/EffectSystem.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Resource/ResourceFunction.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/Yaml/ByamlIter.h"
#include "al/Library/Yaml/ByamlUtil.h"
#include "al/Library/Yaml/Writer/ByamlWriter.h"
#include "al/Project/Scene/SceneInitInfo.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Util/DemoUtil.h"
#include "prim/seadSafeString.h"

#include "hk/gfx/DebugRenderer.h"
#include "hk/gfx/ImGuiBackendNvn.h"
#include "hk/hook/InstrUtil.h"
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"

#include "al/Library/Math/MathUtil.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"

#include "Player/PlayerActorHakoniwa.h"
#include "Scene/StageScene.h"
#include "Scene/StageSceneStateFileSelect.hpp"
#include "Scene/StageSceneStatePauseMenu.h"

#include "CaptureHat.h"

#include "Randomizer/AreaNameOverlay.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureHooks.h"
#include "Randomizer/ChestOrderRando.h"
#include "Randomizer/CoinRando.h"
#include "Randomizer/CoinUncap.h"
#if SMOR_FEATURE_CHAOS_ANY
#include "Randomizer/EnemySwapDebug.h"
#include "Randomizer/EnemySwapHooks.h"
#endif
#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/CreditsRoll.h"
#include "Randomizer/DebugMenuHooks.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/InputGateHooks.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MovementAbilityLock.h"
#include "Randomizer/MusicHooks.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PaintingUnlockOverlay.h"
#include "Randomizer/PauseMenuHooks.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/RandoSaveFile.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SkyboxHooks.h"
#include "Randomizer/SkyboxRando.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/StoryProgressHooks.h"
#include "Randomizer/TimerMode.h"
#include "Randomizer/TravelingNpcRando.h"
#include "Randomizer/WorldMapStatusHooks.h"
#include "Randomizer/WorldMeta.h"
#include "SD/SDHooks.h"
#include "al/Library/Controller/PadRumbleDirector.h"
#include "al/Library/Controller/PadRumbleFunction.h"

#include "speedboot/BootHooks.hpp"
#include "speedboot/HakoniwaSequenceSpeedboot.hpp"

#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/layouts/SpeedrunIcon.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"

#include <cstring>

// ── Forward declarations ──────────────────────────────────────────────────────
static int pInfSendTimer = 0;
static int gameInfSendTimer = 0;
static s32 sLastRandoMetaSlot = -1;
static s32 sLastRandoMetaMoons = -1;
static s32 sLastRandoMetaWorld = -1;
static u32 sLastRandoMetaVisited = 0xFFFFFFFFu;
static u32 sLastRandoMetaForcedPeace = 0xFFFFFFFFu;
// Metadata is only used for save-file previews and restoring randomizer runtime
// state.  A moon collection updates it during a paused get demo, where an
// OpenFile + read + Flush write is particularly visible on emulators.  Keep a
// per-slot dirty bit instead of dropping that update: it is flushed on the first
// non-demo frame, and survives transient player/scene teardown until that slot
// is active again.
static bool sRandoMetaWritePending[11] = {};
// A moon collection changes metadata during a paused get demo.  Keep the
// durable sidecar update, but do not synchronously flood the emulator's debug
// output on that critical frame. Enable only when diagnosing sidecar state.
static constexpr bool cLogRuntimeMetaChanges = false;
// al::SequenceInitInfo* initInfo;

void InstallQolPatches();
void InstallAbilityLockHooks();
// Declared in Multiplayer/main.hpp alongside the mpOn* sync-bridge entry points.

class GameConfigData;

void updatePlayerInfo(GameDataHolderAccessor holder, PlayerActorBase* playerBase, bool isYukimaru) {
    if (pInfSendTimer >= 1) {
        Client::sendPlayerInfPacket(playerBase, isYukimaru);

        if (!isYukimaru) {
            Client::sendHackCapInfPacket(((PlayerActorHakoniwa*)playerBase)->mHackCap);
        }

        // CaptureInf does not read the player object. Send it for Yukimaru as
        // well so a transition away from a captured ordinary player cannot
        // leave remote puppets (or the late-join cache) stuck in capture mode.
        Client::sendCaptureInfPacket(isYukimaru ? nullptr : (PlayerActorHakoniwa*)playerBase);

        pInfSendTimer = 0;
    }

    if (gameInfSendTimer >= 60) {
        if (isYukimaru) {
            Client::sendGameInfPacket(holder);
        } else {
            Client::sendGameInfPacket((PlayerActorHakoniwa*)playerBase, holder);
        }

        gameInfSendTimer = 0;
    }

    pInfSendTimer++;
    gameInfSendTimer++;
}

// ── Hooks ─────────────────────────────────────────────────────────────────────

static void notifyBoardMarioControlAvailable(HakoniwaSequence* sequence) {
    if (!sequence || !sequence->mCurrentScene)
        return;

    StageScene* stageScene = (StageScene*)sequence->mCurrentScene;
    al::PlayerHolder* holder = al::getScenePlayerHolder(stageScene);
    PlayerActorHakoniwa* player = holder
        ? (PlayerActorHakoniwa*)al::tryGetPlayerActor(holder, 0)
        : nullptr;
    if (!player || rs::isActiveDemo(player)
        || rs::isActiveDemoPlayerPuppetable(player))
        return;

    Bingo::notifyMarioControlAvailable();
    // Timer Mode's opening lock arms on the same signal: the run has handed
    // Mario over, so the start chord can be offered.
    TimerMode::notifyMarioControlAvailable();
}

static HkTrampoline<void, HakoniwaSequence*> hakoniwaSequenceHook = hk::hook::trampoline([](HakoniwaSequence* sequence) -> void {
    // Check before StageScene updates. If a demo ends during that update, its
    // first normal-input frame is the next tick, after the board has armed.
    notifyBoardMarioControlAvailable(sequence);
    // Board and Timer chords must be handled before gameplay consumes their
    // buttons. The two run modes are mutually exclusive, so only one of these
    // can ever claim the ZL+ZR press.
    Bingo::handleInput();
    TimerMode::handleInput();
    hakoniwaSequenceHook.orig(sequence);

    DebugMenuHooks::updateSequence(sequence);
    CreditsRoll::updateSequence(sequence);

    if (!sequence || !sequence->mCurrentScene)
        return;

    SpeedrunIcon::sInstance->setHolder(sequence->mGameDataHolderAccessor);
    StageScene* stageScene = (StageScene*)sequence->mCurrentScene;
    al::PlayerHolder* pHolder = al::getScenePlayerHolder(stageScene);
    if (!pHolder)
        return;

    PlayerActorHakoniwa* player = (PlayerActorHakoniwa*)al::tryGetPlayerActor(pHolder, 0);
    if (!player)
        return;

    GameDataHolderAccessor accessor(player);
    if (!accessor.mData || !accessor.mData->getGameDataFile())
        return;

    GameDataHolderWriter writer(player);
    const s32 currentWorldId = GameDataFunction::getCurrentWorldId(accessor);

    if (!GameDataFunction::isEnableCap(accessor))
        GameDataFunction::enableCap(writer);
    if (!GameDataFunction::isActivateHome(accessor)) {
        GameDataFunction::activateHome(writer);
        GameDataFunction::setRequireSave(writer);
    }
    if (currentWorldId == WorldMeta::WorldId_Cascade && !GameDataFunction::isTalkedCapNearHomeInWaterfall(player)) {
        GameDataFunction::talkCapNearHomeInWaterfall(player);
        GameDataFunction::setRequireSave(writer);
    }

    GameDataFile* file = accessor.mData->getGameDataFile();

    // A kingdom's first-arrival cutscene relocates Mario to the Odyssey after the
    // randomized start spawn has already landed. This re-runs the entrance warp
    // once the demo hands control back, and no-ops on every other frame of the
    // run (including when the spawn survived and no correction is needed).
    StartPositionRando::updatePendingReposition(
        player, accessor.mData, file ? file->getStageNameCurrent() : nullptr);

    // Lets the door a Subarea Key just refused go live again once the player has
    // had a moment to step off it, and speaks Cappy's line for that refusal once
    // the bounce has landed.
    EntranceHooks::tickSubareaDenialCooldown();
    MoonHooks::updateSubareaKeyDeniedMessage(stageScene);

    // Movement Abilities: speaks Cappy's refusal lines for a locked vehicle and
    // drives the retreat for an arrival that landed the player in deep water
    // without Swim.
    MovementAbilityLock::update(stageScene);

    // After the native update, so a demo or pause that started this frame has
    // already been applied and the clock does not tick through its first frame.
    TimerMode::update(stageScene, player, file);

    const s32 activeRandoSlot = SaveRedirect::getActiveSlot();
    if (activeRandoSlot >= 1 && activeRandoSlot <= 10) {
        const s32 totalMoons = file->getTotalShineNum();
        const u32 visitedKingdoms = Randomizer::getVisitedKingdoms();
        const u32 forcedPeaceKingdoms = Randomizer::getForcedPeaceKingdoms();
        const bool progressionStateChanged = activeRandoSlot != sLastRandoMetaSlot
            || totalMoons != sLastRandoMetaMoons
            || currentWorldId != sLastRandoMetaWorld
            || visitedKingdoms != sLastRandoMetaVisited
            || forcedPeaceKingdoms != sLastRandoMetaForcedPeace;
        if (progressionStateChanged) {
            if (cLogRuntimeMetaChanges) {
                Randomizer::logPush(
                    "META runtime slot=%d curN=%d moons=%d visited=%08x peace=%08x",
                    activeRandoSlot, currentWorldId, totalMoons,
                    visitedKingdoms, forcedPeaceKingdoms);
                KingdomProgress::debugLogState(file, "runtime-meta-change");
            }
            sRandoMetaWritePending[activeRandoSlot] = true;
            sLastRandoMetaSlot = activeRandoSlot;
            sLastRandoMetaMoons = totalMoons;
            sLastRandoMetaWorld = currentWorldId;
            sLastRandoMetaVisited = visitedKingdoms;
            sLastRandoMetaForcedPeace = forcedPeaceKingdoms;
        }

        // Keep immediate persistence for ordinary gameplay and world changes,
        // but move a pickup's sidecar read/Flush out of the get-demo frame.  Do
        // not clear the dirty bit while a player/scene is unavailable; the
        // static per-slot state lets a later safe frame finish the exact same
        // update rather than silently losing it.
        const bool isDemoActive = rs::isActiveDemo(player)
            || rs::isActiveDemoPlayerPuppetable(player);
        if (sRandoMetaWritePending[activeRandoSlot] && !isDemoActive) {
            if (!RandoSaveFile::updateMeta(
                    activeRandoSlot, accessor, cLogRuntimeMetaChanges)) {
                Randomizer::logPush("rando meta update failed slot=%d", activeRandoSlot);
            }
            // Match the old one-attempt behavior if the sidecar itself is
            // unavailable, while retaining the dirty bit across every path
            // before a write can safely be attempted.
            sRandoMetaWritePending[activeRandoSlot] = false;
        }
    } else {
        sLastRandoMetaSlot = -1;
        sLastRandoMetaMoons = -1;
        sLastRandoMetaWorld = -1;
        sLastRandoMetaVisited = 0xFFFFFFFFu;
        sLastRandoMetaForcedPeace = 0xFFFFFFFFu;
    }

    bool isYukimaru = !player->getPlayerInfo();

    Client::setStageInfo(sequence);

    Client::update();

    updatePlayerInfo(GameDataHolderWriter(stageScene), player, isYukimaru);

    //  if (SpeedrunIcon::sInstance) {
    //      SpeedrunIcon::sInstance->tryStart();
    //  } else {
    //      SpeedrunIcon::sInstance->tryEnd();
    //  }

    if (Client::shouldStopRumble() && player && !isYukimaru) {
        auto* rumbleDirector = alPadRumbleFunction::getPadRumbleDirector(player);
        if (rumbleDirector)
            rumbleDirector->stopAllRumble();
        Client::clearStopRumble();
    }

    if (stageScene->stageSceneLayout && stageScene->mIsAlive) {
        stageScene->stageSceneLayout->updateCounterParts();
        // After updateCounterParts, which is what refreshes the counters
        // themselves - this only repositions the purple one relative to however
        // wide the gold one ended up.
        CoinUncap::updateCounterLayout(stageScene);
    }
});

// The native destroy nerve is the first point at which the dying StageScene,
// its scene heap, and its resource requests have all been released.  Taking
// ownership here prevents HakoniwaSequence::exeMiss from creating a new scene
// while Soft Nuzlocke is rebuilding randomizer tables.
static HkTrampoline<void, HakoniwaSequence*> hakoniwaDestroyHook = hk::hook::trampoline([](HakoniwaSequence* sequence) -> void {
    // Before the original, because its first step is what hands the delete-scene
    // state the flag that decides whether the dead stage's resource heap is
    // released with it.
    Nuzlocke::prepareSceneTeardown(sequence);
    hakoniwaDestroyHook.orig(sequence);
    // Both notification layouts are LayoutActors allocated from the StageScene's
    // heap, held in file statics that outlive it. Once the scene is gone those
    // pointers are dangling, and the next scene's initLayout would delete them -
    // handing the allocator memory that by then belongs to the new scene. Drop
    // them here, while we know the scene has just been torn down.
    if (!sequence || !sequence->mCurrentScene) {
        PaintingUnlockOverlay::invalidateLayout();
        AbilityOverlay::invalidateLayout();
        HintSystem::invalidateAbilityIconTextures();
    }
    Nuzlocke::takeControlAfterSceneDestroy(sequence);
});

// The Cap Kingdom wake-up demo (archive "CapWorldOpening"), which the rando
// never plays.  OpeningStageStartDemo::init builds its partner actor itself -
// operator new(0x210), then al::initLinksActor on "LinkCapManHero" - and the
// hook below suppresses that partner's init outright, so what is left behind is
// a constructed but uninitialized OpeningStageStartCapManHero: no archive, no
// model, no nerve, and never registered with the scene.
//
// These two writes are what keeps the demo off it.  0x110 is the "opening is
// already finished" bool init sets on the isOnSaveObjInfo path, and 0x160 is
// the partner pointer itself (`*((void**)this + 44)`) - nulled so nothing
// downstream reaches into the half-built actor.  init's own use of it reads a
// flag at partner+280 that a suppressed init never wrote, which is the read
// this is really guarding.
class OpeningStageStartDemo;
static HkTrampoline<void, OpeningStageStartDemo*, const al::ActorInitInfo&>
    openingDemoInitHook = hk::hook::trampoline([](OpeningStageStartDemo* demo, const al::ActorInitInfo& info) -> void {
        openingDemoInitHook.orig(demo, info);
        *reinterpret_cast<u8*>(reinterpret_cast<uintptr_t>(demo) + 0x110) = 0;
        *reinterpret_cast<uintptr_t*>(reinterpret_cast<uintptr_t>(demo) + 0x160) = 0;
    });

class OpeningStageStartCapManHero;
static HkTrampoline<void, OpeningStageStartCapManHero*, const al::ActorInitInfo&>
    capManHeroInitHook = hk::hook::trampoline([](OpeningStageStartCapManHero*, const al::ActorInitInfo&) -> void {});

bool gHasRerolledOutfitOnBoot = false;

static HkTrampoline<void, StageScene*, const al::SceneInitInfo&>
    stageSceneInitHook = hk::hook::trampoline([](StageScene* scene, const al::SceneInitInfo& info) -> void {
        CoinRando::resetStageState();
#if SMOR_FEATURE_CHAOS_ANY
        EnemySwapDebug::clearLiveActors();
        EnemySwapHooks::resetStageState();
#endif
        Randomizer::logPush("stageSceneInit ENTER stage=%s scenario=%d",
            info.initStageName ? info.initStageName : "(null)", info.scenarioNo);
        EntranceHooks::resetBypass();
        Randomizer::reinit();
        SkyboxRando::reinit();

        // Catch-all sub-tracking reconcile: whatever code path got us here (including a
        // warp that bypasses the changeNextStage hook), the stage now loading is where the
        // player actually is. Re-point the entrance sub-tracking at it so a warp out of a
        // sub-area can't leave the cache stale and misclassify later door transitions.
        if (EntranceRando::isInitialized() && info.initStageName) {
            const char* beforeSub = EntranceRando::peekTopSubStage();
            EntranceRando::syncSubTrackingToStage(info.initStageName);
            const char* afterSub = EntranceRando::peekTopSubStage();
            if (beforeSub != afterSub)
                Randomizer::logPush("stageSceneInit sub reconcile: %s -> %s (loaded %s)",
                    beforeSub ? beforeSub : "NULL",
                    afterSub ? afterSub : "NULL",
                    info.initStageName);
        }
        MoonHooks::clearShineColorTable();
        HintSystem::resetStageContext();

        al::SceneInitInfo patchedInfo = info;
        const al::SceneInitInfo* initInfo = &info;
        const char* forcedStageName = nullptr;
        s32 forcedScenario = -1;
        GameDataHolder* holder = static_cast<GameDataHolder*>(info.gameDataHolder);
        // Randomizer starts skip the vanilla Cappy meeting, but Mario's model checks
        // this flag while the stage is being initialized. Repair it before actor init;
        // doing so later in exePlayStage leaves Mario capless until the first throw
        // forces the worn-cap model to refresh.
        if (holder) {
            GameDataHolderAccessor accessor(holder);
            GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
            if (file) {
                // Boss-rematch Multi Moons already return through their authored
                // painting-room route. Remove a stale duplicate ReturnToHome
                // request before StageScene chooses its initial demo nerve.
                MoonHooks::sanitizeBossRematchReturnToHome(
                    file, info.initStageName);
                // Pay for the collection list's hint-list indices here, where a
                // loading screen already hides it, rather than on the first
                // pause-menu open. Seed generation cannot do it: that runs on a
                // worker thread which must not touch the GameDataFile.
                HintSystem::warmShineListCaches(file);
                KingdomProgress::debugLogState(file, "stage-scene-init");
                GameDataHolderWriter writer(holder);
                if (!file->isMeetCap()) {
                    file->meetCap();
                    GameDataFunction::setRequireSave(writer);
                    Randomizer::logPush("Initialized Cappy ownership before stage actor init");
                }
                if (!GameDataFunction::isEnableCap(accessor))
                    GameDataFunction::enableCap(writer);
            }
        }

        if (ProgressionHooks::prepareStartStage(
                GameDataHolderWriter(holder), &forcedStageName, &forcedScenario)
            && forcedStageName && forcedStageName[0] != '\0') {
            const bool changedStage = !info.initStageName || std::strcmp(info.initStageName, forcedStageName) != 0;
            const bool changedScenario = forcedScenario > 0 && info.scenarioNo != forcedScenario;
            patchedInfo.initStageName = forcedStageName;
            if (forcedScenario > 0)
                patchedInfo.scenarioNo = forcedScenario;
            initInfo = &patchedInfo;

            if (changedStage || changedScenario) {
                Randomizer::logPush("Start scene redirect: %s/%d -> %s/%d",
                    info.initStageName ? info.initStageName : "???", info.scenarioNo,
                    forcedStageName, patchedInfo.scenarioNo);
            }
        }

        // Deliberately outside the redirect block above and independent of its
        // result: a Randomized Starting Position inside a sub-area must never
        // reach actor init with mCurrentWorldId still -1, or Shine's
        // initAfterPlacement crashes in WorldList::getMainStageName(-1). The
        // repair no-ops unless the world id is actually invalid.
        if (holder)
            ProgressionHooks::repairStartPositionWorldId(GameDataHolderWriter(holder));

        if (holder) {
            GameDataHolderAccessor accessor(holder);
            GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
            if (file) {
                if (!gHasRerolledOutfitOnBoot && Randomizer::isRandomizeOutfitsEnabled()) {
                    gHasRerolledOutfitOnBoot = true;

                    const char* validCloths[64];
                    int validClothCount = 0;
                    for (int i = 0; i < 64; i++) {
                        const char* name = file->getClothList()[i].cstr();
                        if (name && name[0] != '\0') {
                            validCloths[validClothCount++] = name;
                        }
                    }
                    if (validClothCount > 0) {
                        file->wearCostume(validCloths[al::getRandom(validClothCount)]);
                    }

                    const char* validCaps[64];
                    int validCapCount = 0;
                    for (int i = 0; i < 64; i++) {
                        const char* name = file->getCapList()[i].cstr();
                        if (name && name[0] != '\0') {
                            validCaps[validCapCount++] = name;
                        }
                    }
                    if (validCapCount > 0) {
                        file->wearCap(validCaps[al::getRandom(validCapCount)]);
                    }
                }

                // Moon shuffle credits the randomized item identity, which can
                // advance another kingdom's vanilla MainScenarioNo. Rebuild the
                // physical single-story progression before quests and actors read it.
                StoryProgressHooks::healAllMainScenarios(file);
            }
        }

        AreaNameOverlay::show(initInfo->initStageName);
        Randomizer::recordKingdomVisit(initInfo->initStageName);

        ScenarioPatcher::beforeStageSceneInit(initInfo->initStageName, static_cast<GameDataHolder*>(initInfo->gameDataHolder));
        stageSceneInitHook.orig(scene, *initInfo);
        ScenarioPatcher::afterStageSceneInit(scene);
        ScenarioPatcher::clearPendingRestart();
        // A Subarea Key refusal bounces the player back into this same stage.
        // Only now is the outgoing wipe over, so this is where the armed Cappy
        // line is allowed to start its settle countdown.
        MoonHooks::noteSubareaKeyDeniedArrival(initInfo->initStageName);
    });

// Layout actors need the StageScene's initialized LayoutInitInfo. Hooking the
// common actor-init setup gives PaintingNotif the same executor lifetime as the
// game's HUD layouts, rather than drawing it through ImGui.
static HkTrampoline<void, al::ActorInitInfo*, al::Scene*, al::PlacementInfo*, al::LayoutInitInfo*,
    al::ActorFactory*, al::SceneMsgCtrl*, al::GameDataHolderBase*>
    paintingNotifActorInitInfoHook = hk::hook::trampoline(
        [](al::ActorInitInfo* actorInitInfo, al::Scene* scene, al::PlacementInfo* placementInfo,
            al::LayoutInitInfo* layoutInitInfo, al::ActorFactory* actorFactory,
            al::SceneMsgCtrl* sceneMsgCtrl, al::GameDataHolderBase* gameDataHolderBase) -> void {
            paintingNotifActorInitInfoHook.orig(actorInitInfo, scene, placementInfo, layoutInitInfo,
                actorFactory, sceneMsgCtrl, gameDataHolderBase);

            // Multiplayer used to install its own trampoline here, which made
            // both hooks' orig() a wild branch (one trampoline per function --
            // see the note in Multiplayer/main.hpp). It rides along instead.
            mpOnActorInitInfo(actorInitInfo, scene);

            if (scene && layoutInitInfo && al::isEqualString(scene->mName.cstr(), "StageScene")) {
                PaintingUnlockOverlay::initLayout(*layoutInitInfo);
                AbilityOverlay::initLayout(*layoutInitInfo);
                // Same scene-heap lifetime as those two, and the same reason for
                // being here: building a TextureInfo allocates, so it cannot be
                // done later from the collection list's nerve execute.
                HintSystem::prepareAbilityIconTextures();
                // CaptureOverlay::initLayout(*layoutInitInfo);
            }
        });

HkTrampoline<void, HakoniwaSequence*> hakoniwaInitSystemHook = hk::hook::trampoline([](HakoniwaSequence* seq) -> void {
    al::EffectSystem* effectSystem = seq->mEffectSystem;
    if (!seq->mIsInitialized && !effectSystem->_68) {
        al::findOrCreateResource(sead::SafeString("EffectData/EffectPtclPatch"), nullptr);
    }
    hakoniwaInitSystemHook.orig(seq);
});

// Bit layout of the compact "Flg" field in RandoData. The vanilla Common save
// buffer is only ~1 KB; the old one-key-per-setting block overflowed it and the
// byaml got truncated mid-hash (keys after "PaintingRando" never persisted and
// lookups over the broken entry table failed randomly). Keep this block small.
enum RandoSaveFlag : u32
{
    RandoSaveFlag_MoonRando = 1u << 0,
    RandoSaveFlag_EntranceRando = 1u << 1,
    RandoSaveFlag_PaintingRando = 1u << 2,
    RandoSaveFlag_KingdomRando = 1u << 3,
    RandoSaveFlag_LogicRando = 1u << 4,
    RandoSaveFlag_SkyboxRando = 1u << 5,
    RandoSaveFlag_LockedCaptures = 1u << 6,
    RandoSaveFlag_SphinxRando = 1u << 7,
    // Retired: the spoiler log is always written now. Bit reserved so an
    // older build still parses the rest of the config flags.
    RandoSaveFlag_SpoilerLogRetired = 1u << 8,
    RandoSaveFlag_LockedAbilities = 1u << 9,
    RandoSaveFlag_MusicRando = 1u << 10,
    RandoSaveFlag_MusicOverlay = 1u << 11,
    RandoSaveFlag_MoonKingdomReq = 1u << 12,
    RandoSaveFlag_WorldPeaceReq = 1u << 13,
    RandoSaveFlag_CloudPaintings = 1u << 14,
    // RandoSaveFlag_Multiplayer = 1u << 15,
    RandoSaveFlag_VisitedKingdomsMap = 1u << 16,
    RandoSaveFlag_MoonRockKey = 1u << 17,
    RandoSaveFlag_StreamerMode = 1u << 18,
    RandoSaveFlag_ShopPriceRando = 1u << 19,
    RandoSaveFlag_ProportionalKingdomPricing = 1u << 20,
};

static HkTrampoline<void, GameConfigData*, al::ByamlWriter*> saveWriteHook = hk::hook::trampoline([](GameConfigData* cfgData, al::ByamlWriter* writer) -> void {
    saveWriteHook.orig(cfgData, writer);

    u32 flags = 0;
    if (Randomizer::isMoonRandoEnabled())
        flags |= RandoSaveFlag_MoonRando;
    if (Randomizer::isEntranceRandoEnabled())
        flags |= RandoSaveFlag_EntranceRando;
    if (Randomizer::isPaintingRandoEnabled())
        flags |= RandoSaveFlag_PaintingRando;
    if (Randomizer::isKingdomRandoEnabled())
        flags |= RandoSaveFlag_KingdomRando;
    if (Randomizer::isLogicRandoEnabled())
        flags |= RandoSaveFlag_LogicRando;
    if (Randomizer::isSkyboxRandoEnabled())
        flags |= RandoSaveFlag_SkyboxRando;
    if (Randomizer::isCaptureRandoEnabled())
        flags |= RandoSaveFlag_LockedCaptures;
    if (Randomizer::isSphinxRandoEnabled())
        flags |= RandoSaveFlag_SphinxRando;
    if (Randomizer::isLockedAbilitiesEnabled())
        flags |= RandoSaveFlag_LockedAbilities;
    if (Randomizer::isMusicRandoEnabled())
        flags |= RandoSaveFlag_MusicRando;
    if (Randomizer::isMusicOverlayEnabled())
        flags |= RandoSaveFlag_MusicOverlay;
    if (Randomizer::isStreamerModeEnabled())
        flags |= RandoSaveFlag_StreamerMode;
    if (Randomizer::isMoonKingdomRequirementsEnabled())
        flags |= RandoSaveFlag_MoonKingdomReq;
    if (Randomizer::isWorldPeaceRequirementEnabled())
        flags |= RandoSaveFlag_WorldPeaceReq;
    if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
        flags |= RandoSaveFlag_CloudPaintings;
    if (Randomizer::isVisitedKingdomsMapEnabled())
        flags |= RandoSaveFlag_VisitedKingdomsMap;
    if (Randomizer::isMoonRockKeyEnabled())
        flags |= RandoSaveFlag_MoonRockKey;
    if (Randomizer::isShopPriceRandoEnabled())
        flags |= RandoSaveFlag_ShopPriceRando;
    if (Randomizer::isProportionalKingdomPricingEnabled())
        flags |= RandoSaveFlag_ProportionalKingdomPricing;
    // if (Client::isMultiplayerEnabled())
    //     flags |= RandoSaveFlag_Multiplayer;

    writer->pushHash("RandoData");
    writer->addInt("Seed", (s32)Randomizer::getSeed());
    writer->addInt("Flg", (s32)flags);
    writer->addInt("Ent", (s32)Randomizer::getEntranceRandoMode());
    writer->addInt("Shp", (s32)Randomizer::getShopLocationMode());
    writer->addInt("SEq", (s32)Randomizer::getStartingEquipment());
    writer->addInt("CAD", (s32)Randomizer::getCaptureAbilityDifficulty());
    writer->addInt("MRq", (s32)Randomizer::getMoonRequirementLevel());
    writer->addInt("MTo", (s32)Randomizer::getMoonRequirement());
    writer->addInt("Cap", (s32)Randomizer::getCapKingdomMoonUse());
    writer->addInt("Cld", (s32)Randomizer::getCloudKingdomMoonUse());
    writer->addInt("Mon", (s32)Randomizer::getMoonKingdomMoonUse());
    writer->addInt("MDf", (s32)Randomizer::getMoonDifficulty());
    writer->addInt("LDf", (s32)Randomizer::getLogicDifficulty());
    writer->addInt("Sty", (s32)Randomizer::getSeedAxesPacked());
    writer->addInt("Hpc", (s32)Randomizer::packHintPriceProfile(Randomizer::getHintCoinPriceProfile()));
    writer->addInt("Hpt", (s32)Randomizer::packHintPriceProfile(Randomizer::getHintTokenPriceProfile()));
    // Legacy builds only understand Hnt. Keep its tier projection so their
    // compact configuration reader still has a sensible fallback; Hpc/Hpt are
    // authoritative for this and all later builds.
    writer->addInt("Hnt", (s32)Randomizer::getHintPriceTier());
    writer->addInt("Hmd", (s32)Randomizer::getHintMode());
    writer->addInt("Sky", (s32)Randomizer::getSkyboxRandoMode());
    writer->addInt("Wpk", (s32)Randomizer::getWorldPeaceRequirementMode());
    writer->addInt("Vkm", (s32)Randomizer::getVisitedKingdomsMapMode());
    writer->addInt("Sau", (s32)Randomizer::getSubareaUnlockMode());
    writer->addInt("Sah", (s32)Randomizer::getSubareaUnlockHost());
    writer->addInt("Stp", (s32)Randomizer::getStartPositionMode());
    writer->addInt("Mus", (s32)Randomizer::getMusicRandoMode());
    writer->addInt("Coi", Randomizer::isCoinRandoEnabled() ? 1 : 0);
    writer->addInt("PSA", Randomizer::getPurpleShopAbilityCount());
    writer->addInt("PSC", Randomizer::getPurpleShopCaptureCount());
    writer->addInt("PSM", Randomizer::arePurpleShopMoonsEnabled() ? 1 : 0);
    writer->addInt("PSP", (s32)Randomizer::getPurpleShopPrice());
    writer->addInt("BWc", (s32)Randomizer::getBowserWinCondition());
    writer->addInt("TMr", (s32)Randomizer::getToadetteMedalRatioIdx());
    writer->addInt("MvA", (s32)Randomizer::packMovementAbilitySettings());
    // writer->addString("SIP", Client::getCurrentIP());
    // writer->addInt("SPt", Client::getCurrentPort());
    writer->pop();

    Randomizer::logPush("cfg save seed=%u flags=0x%x ent=%d seq=%d cad=%d kr=%d painting=%d",
        Randomizer::getSeed(), flags, (s32)Randomizer::getEntranceRandoMode(),
        (s32)Randomizer::getStartingEquipment(),
        (s32)Randomizer::getCaptureAbilityDifficulty(), Randomizer::isKingdomRandoEnabled(),
        Randomizer::isPaintingRandoEnabled());
});

static HkTrampoline<void, GameConfigData*, const al::ByamlIter&> saveReadHook = hk::hook::trampoline([](GameConfigData* cfgData, const al::ByamlIter& iter) -> void {
    saveReadHook.orig(cfgData, iter);

    al::ByamlIter iterIntern;
    if (!al::tryGetByamlIterByKey(&iterIntern, iter, "RandoData"))
        return;

    // Compact format only (see saveWriteHook). Saves written by older builds
    // used long keys and were truncated by the Common buffer - unreliable to
    // read, so they fall back to defaults once and get rewritten compactly.
    s32 seed = 0;
    if (al::tryGetByamlS32(&seed, iterIntern, "Seed"))
        Randomizer::setSeed((u32)seed);

    s32 flags = 0;
    const bool flagsFound = al::tryGetByamlS32(&flags, iterIntern, "Flg");
    if (flagsFound) {
        Randomizer::setMoonRandoEnabled(flags & RandoSaveFlag_MoonRando);
        Randomizer::setPaintingRandoEnabled(flags & RandoSaveFlag_PaintingRando);
        Randomizer::setKingdomRandoEnabled(flags & RandoSaveFlag_KingdomRando);
        Randomizer::setLogicRandoEnabled(flags & RandoSaveFlag_LogicRando);
        // Skybox mode is read from the "Sky" int below (bit is a legacy fallback).
        Randomizer::setCaptureRandoEnabled(flags & RandoSaveFlag_LockedCaptures);
        Randomizer::setSphinxRandoEnabled(flags & RandoSaveFlag_SphinxRando);
        Randomizer::setLockedAbilitiesEnabled(flags & RandoSaveFlag_LockedAbilities);
        Randomizer::setMusicRandoEnabled(flags & RandoSaveFlag_MusicRando);
        Randomizer::setMusicOverlayEnabled(flags & RandoSaveFlag_MusicOverlay);
        Randomizer::setStreamerModeEnabled(flags & RandoSaveFlag_StreamerMode);
        Randomizer::setMoonKingdomRequirementsEnabled(flags & RandoSaveFlag_MoonKingdomReq);
        // World Peace Requirement mode is read from the "Wpk" int below (bit is
        // a legacy fallback).
        Randomizer::setCloudKingdomMoonsUnlockPaintingsEnabled(flags & RandoSaveFlag_CloudPaintings);
        // Visited Kingdoms map mode is read from the "Vkm" int below (bit is a
        // legacy fallback that can only express Vanilla/Visited).
        Randomizer::setVisitedKingdomsMapEnabled(flags & RandoSaveFlag_VisitedKingdomsMap);
        Randomizer::setMoonRockKeyEnabled(flags & RandoSaveFlag_MoonRockKey);
        // Both default off, so a config written before they existed reads back
        // as the behaviour it actually had.
        Randomizer::setShopPriceRandoEnabled(flags & RandoSaveFlag_ShopPriceRando);
        Randomizer::setProportionalKingdomPricingEnabled(
            flags & RandoSaveFlag_ProportionalKingdomPricing);
        // Client::setMultiplayerEnabled(flags & RandoSaveFlag_Multiplayer);
    }

    s32 v = 0;
    // Absent in configs written before the movement ability page existed; 0 is
    // the all-granted default, which is exactly how those seeds played.
    Randomizer::applyMovementAbilitySettings(
        al::tryGetByamlS32(&v, iterIntern, "MvA") ? (u32)v : 0u);

    v = 0;
    if (al::tryGetByamlS32(&v, iterIntern, "Ent"))
        Randomizer::setEntranceRandoMode((Randomizer::EntranceRandoMode)v);
    else if (flagsFound)
        Randomizer::setEntranceRandoMode((flags & RandoSaveFlag_EntranceRando)
                ? Randomizer::EntranceRandoMode_Standard
                : Randomizer::EntranceRandoMode_Off);
    // A config written before this setting existed has no key and keeps the
    // Randomized default, which is how those configs already behaved.
    if (al::tryGetByamlS32(&v, iterIntern, "Shp"))
        Randomizer::setShopLocationMode((Randomizer::ShopLocationMode)v);
    if (al::tryGetByamlS32(&v, iterIntern, "SEq"))
        Randomizer::setStartingEquipment((Randomizer::StartingEquipment)v);
    else
        Randomizer::setStartingEquipment(Randomizer::StartingEquipment_Normal);
    s32 legacyItemPool = Randomizer::CaptureAbilityDifficulty_Normal;
    al::tryGetByamlS32(&legacyItemPool, iterIntern, "CAD");

    // GameConfigData is persisted UI configuration, so a config lacking these
    // later-added keys is historical data, not a fresh setup. Preserve its old
    // Cap coin reward; genuinely fresh defaults come from Randomizer's static
    // initialization and are written with an explicit Cap key.
    Randomizer::setCapKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
    Randomizer::setCloudKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
    Randomizer::setMoonKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
    s32 capUse = 0;
    const bool hasSpecialMoonModes = al::tryGetByamlS32(&capUse, iterIntern, "Cap");
    if (hasSpecialMoonModes) {
        Randomizer::setCapKingdomMoonUse((Randomizer::SpecialMoonUse)capUse);
        if (al::tryGetByamlS32(&v, iterIntern, "Cld"))
            Randomizer::setCloudKingdomMoonUse((Randomizer::SpecialMoonUse)v);
        if (al::tryGetByamlS32(&v, iterIntern, "Mon"))
            Randomizer::setMoonKingdomMoonUse((Randomizer::SpecialMoonUse)v);
    } else if (legacyItemPool == Randomizer::CaptureAbilityDifficulty_Plentiful) {
        Randomizer::setCapKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            Randomizer::setCloudKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
        if (!Randomizer::isMoonKingdomRequirementsEnabled())
            Randomizer::setMoonKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
    }
    Randomizer::setCaptureAbilityDifficulty(
        (Randomizer::CaptureAbilityDifficulty)legacyItemPool);
    if (al::tryGetByamlS32(&v, iterIntern, "Sky"))
        Randomizer::setSkyboxRandoMode((Randomizer::SkyboxRandoMode)v);
    else if (flagsFound)
        Randomizer::setSkyboxRandoMode((flags & RandoSaveFlag_SkyboxRando)
                ? Randomizer::SkyboxRandoMode_Kingdoms
                : Randomizer::SkyboxRandoMode_None);
    if (al::tryGetByamlS32(&v, iterIntern, "Wpk"))
        Randomizer::setWorldPeaceRequirementMode((Randomizer::WorldPeaceRequirementMode)v);
    else if (flagsFound)
        Randomizer::setWorldPeaceRequirementMode((flags & RandoSaveFlag_WorldPeaceReq)
                ? Randomizer::WorldPeaceRequirementMode_Required
                : Randomizer::WorldPeaceRequirementMode_StoryProgression);
    if (al::tryGetByamlS32(&v, iterIntern, "Vkm"))
        Randomizer::setVisitedKingdomsMapMode((Randomizer::VisitedKingdomsMapMode)v);
    // Order matters: the mode re-settles the host, so read it first and let the
    // stored host land on top. A save written before this feature has neither
    // key and keeps the Unlocked default.
    if (al::tryGetByamlS32(&v, iterIntern, "Sau"))
        Randomizer::setSubareaUnlockMode((Randomizer::SubareaUnlockMode)v);
    if (al::tryGetByamlS32(&v, iterIntern, "Sah"))
        Randomizer::setSubareaUnlockHost((Randomizer::SubareaUnlockHost)v);
    if (al::tryGetByamlS32(&v, iterIntern, "Stp"))
        Randomizer::setStartPositionMode((Randomizer::StartPositionMode)v);
    if (al::tryGetByamlS32(&v, iterIntern, "MTo"))
        Randomizer::setMoonRequirement((u32)v);
    else if (al::tryGetByamlS32(&v, iterIntern, "MRq"))
        Randomizer::setMoonRequirementLevel((Randomizer::MoonRequirement)v);
    if (al::tryGetByamlS32(&v, iterIntern, "MDf"))
        Randomizer::setMoonDifficulty((Randomizer::MoonDifficulty)v);
    if (al::tryGetByamlS32(&v, iterIntern, "LDf"))
        Randomizer::setLogicDifficulty((Randomizer::LogicDifficulty)v);
    if (al::tryGetByamlS32(&v, iterIntern, "Sty"))
        Randomizer::setSeedAxesPacked((u16)v);
    else
        Randomizer::applyLegacySeedStyle(Randomizer::SeedStyle_Standard);
    // After the axes, for the same reason as in RandoSaveFile: a retired
    // chain-length Loading Zones mode means a Seed Openness level.
    Randomizer::applyLegacyEntranceRandoMode(Randomizer::getEntranceRandoMode());
    if (al::tryGetByamlS32(&v, iterIntern, "Mus"))
        Randomizer::setMusicRandoMode((Randomizer::MusicRandoMode)v);
    if (al::tryGetByamlS32(&v, iterIntern, "Coi"))
        Randomizer::setCoinRandoEnabled(v != 0);
    else
        Randomizer::setCoinRandoEnabled(false);
    if (al::tryGetByamlS32(&v, iterIntern, "PSA"))
        Randomizer::setPurpleShopAbilityCount(v);
    if (al::tryGetByamlS32(&v, iterIntern, "PSC"))
        Randomizer::setPurpleShopCaptureCount(v);
    if (al::tryGetByamlS32(&v, iterIntern, "PSM"))
        Randomizer::setPurpleShopMoonsEnabled(v != 0);
    if (al::tryGetByamlS32(&v, iterIntern, "PSP"))
        Randomizer::setPurpleShopPrice((Randomizer::PurpleShopPrice)v);
    s32 packedHintProfile = 0;
    if (al::tryGetByamlS32(&packedHintProfile, iterIntern, "Hpc")) {
        Randomizer::setHintCoinPriceProfile(
            Randomizer::unpackHintCoinPriceProfile(static_cast<u32>(packedHintProfile)));
    } else if (al::tryGetByamlS32(&v, iterIntern, "Hnt")) {
        if (v < 0)
            v = 0;
        else if (v > (s32)Randomizer::HintPriceTier::Expensive)
            v = (s32)Randomizer::HintPriceTier::Expensive;
        Randomizer::setHintPriceTier((Randomizer::HintPriceTier)v);
    } else {
        Randomizer::setHintPriceTier(Randomizer::HintPriceTier::Normal);
    }
    if (al::tryGetByamlS32(&packedHintProfile, iterIntern, "Hpt")) {
        Randomizer::setHintTokenPriceProfile(
            Randomizer::unpackHintTokenPriceProfile(static_cast<u32>(packedHintProfile)));
    } else {
        Randomizer::HintPriceProfile tokenDefaults = { {
                                                           static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                           static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                           static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                           static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                           static_cast<u8>(Randomizer::HintTokenPrice::Disabled),
                                                       },
            static_cast<u8>(Randomizer::HintTokenProgressiveIncrement::Disabled), false };
        Randomizer::setHintTokenPriceProfile(tokenDefaults);
    }
    if (al::tryGetByamlS32(&v, iterIntern, "Hmd"))
        Randomizer::setHintMode(static_cast<Randomizer::HintMode>(v));
    if (al::tryGetByamlS32(&v, iterIntern, "BWc"))
        Randomizer::setBowserWinCondition((Randomizer::BowserWinCondition)v);
    if (al::tryGetByamlS32(&v, iterIntern, "TMr"))
        Randomizer::setToadetteMedalRatioIdx(v);
    // const char* serverIP = nullptr;
    // if (al::tryGetByamlString(&serverIP, iterIntern, "SIP") && serverIP && serverIP[0] != '\0')
    //     Client::setServerIP(serverIP);
    // else if (al::tryGetByamlString(&serverIP, iterIntern, "ServerIP") && serverIP && serverIP[0] != '\0')
    //     Client::setServerIP(serverIP);
    // if (al::tryGetByamlS32(&v, iterIntern, "SPt") && v > 0)
    //     Client::setServerPort(v);
    // else if (al::tryGetByamlS32(&v, iterIntern, "ServerPort") && v > 0)
    //     Client::setServerPort(v);
    // bool mpEnabled = false;
    // if (!flagsFound && al::tryGetByamlBool(&mpEnabled, iterIntern, "MultiplayerEnabled"))
    //     Client::setMultiplayerEnabled(mpEnabled);
    // GameConfigData is global UI configuration, not an active seed save.  A
    // full reinit() here rebuilds entrance and moon placements during every
    // boot, only for the selected file's sidecar to replace them moments
    // later.  New seeds run reinit() in SeedGenerator and existing seeds use
    // RandoSaveFile::loadAndApply()'s deterministic restore path instead.
    Randomizer::logPush("cfg load settings seed=%u flgFound=%d flags=0x%x ent=%d seq=%d cad=%d painting=%d kr=%d",
        Randomizer::getSeed(), flagsFound, flags,
        (s32)Randomizer::getEntranceRandoMode(),
        (s32)Randomizer::getStartingEquipment(),
        (s32)Randomizer::getCaptureAbilityDifficulty(),
        Randomizer::isPaintingRandoEnabled(), Randomizer::isKingdomRandoEnabled());
});

// Force boot onto the returning-player TitleMenuScene instead of the first-boot
// opening flow. GameDataFunction::isPlayDemoOpening() reads a per-file save flag;
// HakoniwaSequence::init uses it to pick the DemoOpening nerve (which runs
// FirstSequenceScene's "Start / Start in Assist Mode" mode-select) over the title
// path. With SD-redirected empty saves that flag is always set, so the game never
// reaches the title menu our "Start" -> file-select hooks live on. Returning false
// keeps boot on the title path (and skips the intro cutscene, unused by the rando).
static HkReplace<bool, void*> isPlayDemoOpeningHook = hk::hook::replace([](void*) -> bool {
    return false;
});

static HkTrampoline<void, HakoniwaStateBootLoadData*> bootStartLoadHook = hk::hook::trampoline([](HakoniwaStateBootLoadData* state) -> void {
    if ((gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested())
        || Nuzlocke::isReturningToTitle()) {
        Randomizer::logPush("HakoniwaStateBootLoadData::startLoad -> orig (file-select direct load)");
        gHasRerolledOutfitOnBoot = false;
        bootStartLoadHook.orig(state);
        return;
    }

    Randomizer::logPush("HakoniwaStateBootLoadData::startLoad skipped before title file-select start");
});

// Diagnostic: trace the boot-load-data world-resource wait that drives the title
// "loading" screen. If stageSceneInit never fires, the boot is stalled here — this
// shows whether the load percent climbs (resource load OK, kill-gate stuck) or
// stays low (resource load itself never completes). Gated on the file-select start
// so it only logs the post-generation / load path.
static HkTrampoline<void, void*> bootLoadDataExeMenuHook = hk::hook::trampoline([](void* state) -> void {
    static s32 sFrame = 0;
    const bool isFileSelectStart = gStageSceneStateFileSelect && gStageSceneStateFileSelect->isStartRequested();

    // On a plain "Return to Main Menu" boot (no file-select load), the holder's
    // current GameData pointer (holder+0x20, what getCurrentWorldId derefs) is
    // left DANGLING: the in-game data-management return released the loaded file
    // without selecting a new one, so it is non-null but points at freed memory
    // (a plain null-check misses it). Vanilla exeMenu's first step calls
    // getCurrentWorldId(holder) and crashes. Repoint the current GameData at
    // file 0 (which still exists in holder->fileArray) whenever it doesn't
    // already match — setPlayingFileId(0) does exactly holder+0x20 =
    // fileArray[0]. The title only needs *a* valid world id to background-load
    // resources while it shows the menu. Done before orig and self-limiting
    // (once repaired, current == file0 so it won't fire again).
    if (!isFileSelectStart) {
        const uintptr_t s = reinterpret_cast<uintptr_t>(state);

        // Clear BootLoadData's direct-load flag (byte+0x101). On a return-to-title
        // boot it stays set (the "continue the save" intent), and exeMenu only
        // starts the title BGM when it is clear (`!byte+257 && isStep(195)`), so
        // the menu came up silent. Clearing it restores the title music (and also
        // belt-and-suspenders prevents the startLoadDirect path).
        *reinterpret_cast<u8*>(s + 0x101) = 0;

        GameDataHolder* holder = *reinterpret_cast<GameDataHolder**>(s + 0x50); // this[10]
        if (holder) {
            const uintptr_t h = reinterpret_cast<uintptr_t>(holder);
            void* currentGameData = *reinterpret_cast<void**>(h + 0x20); // holder+0x20
            void** fileArray = *reinterpret_cast<void***>(h + 0x18); // holder+0x18
            void* file0 = fileArray ? fileArray[0] : nullptr;
            if (file0 && currentGameData != file0) {
                holder->setPlayingFileId(0);
                Randomizer::logPush("exeMenu: repaired current GameData cur=%p file0=%p (return to title)",
                    currentGameData, file0);
            }
        }
    }

    bootLoadDataExeMenuHook.orig(state);

    if (isFileSelectStart && gStageSceneStateFileSelect) {
        const uintptr_t s = reinterpret_cast<uintptr_t>(state);
        auto* loader = *reinterpret_cast<WorldResourceLoader**>(s + 0x40); // this[8]
        GameDataHolder* holder = gStageSceneStateFileSelect->getGameDataHolder();

        // The loader was cancelled and left empty (cancelled=1, loadWorld=-1) by
        // the title's reset, with no re-request. Once exeActivateSlot has finished
        // setting the start stage (isDone), re-request the world-home resources
        // ourselves while the loader is still empty — retry until it takes, so
        // calcLoadPercent climbs and boot can advance. Gating on isDone avoids
        // requesting against the pre-initialised holder (worldId=-1).
        if (loader && holder && gStageSceneStateFileSelect->isDone()
            && (loader->mLoadWorldId < 0 || loader->mIsCancelled) && (sFrame % 6 == 0)) {
            GameDataHolderAccessor accessor(holder);
            if (!accessor.mData || !accessor.mData->getGameDataFile()) {
                if (sFrame % 60 == 0)
                    Randomizer::logPush("exeMenu resource retry waiting for playing file");
                return;
            }
            s32 worldId = GameDataFunction::getCurrentWorldId(accessor);
            s32 scenario = GameDataFunction::calcNextScenarioNo(accessor);
            const char* stageName = nullptr;
            const StageSceneStateFileSelect::StartMode startMode = gStageSceneStateFileSelect->getStartMode();

            if (worldId < 0) {
                s32 forcedWorldId = -1;
                s32 forcedScenario = -1;
                const char* forcedStageName = nullptr;
                if (ProgressionHooks::getForcedStartStage(
                        accessor, &forcedStageName, &forcedScenario, &forcedWorldId)
                    && forcedWorldId >= 0) {
                    worldId = forcedWorldId;
                    scenario = forcedScenario;
                    stageName = forcedStageName;
                    Randomizer::logPush("exeMenu fallback forced stage=%s world=%d scen=%d",
                        stageName ? stageName : "", worldId, scenario);
                } else if (startMode == StageSceneStateFileSelect::StartMode::NewGame) {
                    worldId = WorldMeta::WorldId_Cap;
                    scenario = 1;
                    stageName = "CapWorldHomeStage";
                    Randomizer::logPush("exeMenu fallback default stage=%s world=%d scen=%d",
                        stageName, worldId, scenario);
                } else {
                    Randomizer::logPush("exeMenu no load target mode=%d curWorld=%d scen=%d",
                        (int)startMode, worldId, scenario);
                }
            }

            if (worldId >= 0) {
                if (scenario < 0)
                    scenario = 1;
                loader->mIsCancelled = false; // clear the stuck cancelled state
                const bool ok = loader->requestLoadWorldHomeStageResource(worldId, scenario);
                Randomizer::logPush("exeMenu re-request mode=%d stage=%s world=%d scen=%d ok=%d -> loadWorld=%d",
                    (int)startMode, stageName ? stageName : "", worldId, scenario,
                    (s32)ok, loader->mLoadWorldId);
            }
        }

        if (sFrame % 30 == 0 && loader) {
            Randomizer::logPush("exeMenu f=%d mode=%d loadWorld=%d scen=%d isEnd=%d calcPct=%d cancelled=%d done=%d",
                sFrame, (int)gStageSceneStateFileSelect->getStartMode(),
                loader->mLoadWorldId, loader->mScenarioNo,
                (s32)loader->isEndLoadWorldResource(), (s32)loader->calcLoadPercent(),
                (s32)loader->mIsCancelled, (s32)gStageSceneStateFileSelect->isDone());
        }
        sFrame++;
    } else {
        sFrame = 0;
    }
});

// ── Hook installation ─────────────────────────────────────────────────────────

extern "C" void hkMain() {
    DebugMenuHooks::installHooks();
    CreditsRoll::installHooks();
    Nuzlocke::installHooks();
    hakoniwaSequenceHook.installAtSym<"_ZN16HakoniwaSequence12exePlayStageEv">();
    hakoniwaDestroyHook.installAtSym<"_ZN16HakoniwaSequence10exeDestroyEv">();
    hakoniwaInitSystemHook.installAtSym<"_ZN16HakoniwaSequence10initSystemEv">();
    stageSceneInitHook.installAtSym<"_ZN10StageScene4initERKN2al13SceneInitInfoE">();
    paintingNotifActorInitInfoHook.installAtSym<"R_ZN2al17initActorInitInfo">();
    ScenarioPatcher::installHooks();
    StoryProgressHooks::installHooks();
    EntranceHooks::installHooks();
    PaintingRando::installHooks();
    CoinUncap::installHooks();
    TravelingNpcRando::installHooks();
    ProgressionHooks::installHooks();
    WorldMapStatusHooks::installHooks();
    MusicHooks::installHooks();

    // Moon rando
    MoonHooks::createMoonCutsceneHooks();
    MoonHooks::installHooks();
    HintSystem::installHooks();
    CaptureHooks::installHooks();
    ChestOrderRando::installHooks();
#if SMOR_FEATURE_CHAOS_ANY
    EnemySwapHooks::installHooks();
#endif
    CoinRando::installHooks();

    hk::gfx::DebugRenderer::instance()->installHooks();
    hk::gfx::ImGuiBackendNvn::instance()->installHooks(false);

    openingDemoInitHook.installAtSym<"_ZN21OpeningStageStartDemo4initERKN2al13ActorInitInfoE">();
    capManHeroInitHook.installAtSym<"_ZN27OpeningStageStartCapManHero4initERKN2al13ActorInitInfoE">();

    SkyboxHooks::installHooks();
    hk::ro::getMainModule()->writeRo(0x30d7a4, 0x14000006);

    InputGate::installHooks();

    PauseMenuHooks::installHooks();
    bootStartLoadHook.installAtSym<"_ZN25HakoniwaStateBootLoadData9startLoadEv">();
    bootLoadDataExeMenuHook.installAtMainOffset(0x511498);
    isPlayDemoOpeningHook.installAtSym<"_ZN16GameDataFunction17isPlayDemoOpeningE22GameDataHolderAccessor">();

    saveWriteHook.installAtSym<"_ZN14GameConfigData5writeEPN2al11ByamlWriterE">();
    saveReadHook.installAtSym<"_ZN14GameConfigData4readERKN2al9ByamlIterE">();

    InstallAbilityLockHooks();
    MovementAbilityLock::installHooks();
    InstallQolPatches();
    CaptureHat::installHooks();
    // Must stay after every Randomizer installHooks() above: the Multiplayer
    // hooks deliberately avoid the functions those already own, and the mpOn*
    // bridge (Multiplayer/main.hpp) is what keeps its sync features alive on
    // them. Never re-add a trampoline there for a function hooked above.
    installMultiplayerHooks();
    SDRedirect::installHooks();
    SaveRedirect::installHooks();

    hk::hook::writeBranchLinkAtSym<"R_hakoniwaSetNerveSetup">(speedboot::hakoniwaSetNerveSetup);
    hk::hook::a64::assemble<"mov w2, #0x1f">().installAtSym<"R_hakoniwaSetNerveCount">();
    speedboot::prepareSpeedBootHook.installAtSym<"_ZN10BootLayoutC1ERKN2al14LayoutInitInfoE">();
    // speedboot::installInputHooks();

    hk::ro::getMainModule()->writeRo(0x5145c8, 0x7107D29F);
    hk::hook::a64::assemble<"ret">().installAtMainOffset(0x514710);
}
