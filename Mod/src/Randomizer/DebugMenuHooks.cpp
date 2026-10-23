#include "Randomizer/DebugMenuHooks.h"

#include <basis/seadTypes.h>
#include <sead/heap/seadExpHeap.h>

#include "sead/gfx/seadCamera.h"
#include "sead/gfx/seadPrimitiveRenderer.h"
#include "sead/gfx/seadProjection.h"
#include "sead/heap/seadHeap.h"

#include "nn/fs/fs_mount.h"
#include "nn/hid.h" // IWYU pragma: keep
#include "nn/nifm.h"
#include "nn/socket.h"

#include "Imgui.hpp"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/server/Client.hpp"
#include "Player/PlayerActorHakoniwa.h"
#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AreaNameOverlay.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/EnemySwapDebug.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/ModConfigInfoOverlay.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonPurchaseOverlay.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MusicHooks.h"
#include "Randomizer/NuzlockeOverlay.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PaintingUnlockOverlay.h"
#include "Randomizer/PathHintOverlay.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/SeedGenerator.h"
#include "Randomizer/SkyboxHooks.h"
#include "Randomizer/SpoilerLog.hpp"
#include "Randomizer/TimerMode.h"
#include "Randomizer/TrackerBridge.h"
#include "Randomizer/TrackerOverlay.h"
#include "Scene/StageScene.h"
#include "agl/common/aglDrawContext.h"
#include "al/Library/Camera/CameraUtil.h"
#include "al/Library/Controller/InputFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/System/GameSystemInfo.h"
#include "al/Project/Memory/Util.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/Application.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameSystem.h"
#include "helpers.hpp"
#include "hk/gfx/ImGuiBackendNvn.h"
#include "hk/hook/Trampoline.h"
#include "hk/svc/api.h"
#include "hk/svc/types.h"
#include "imgui.h"
#include "nn/err.h"
#include "nn/hid.h"

namespace DebugMenuHooks {

    static sead::Heap* sImGuiHeap = nullptr;
    static f32 gSpeedGain = 0.0f;
    static bool gNoclip = false;

    static void noclip(PlayerActorHakoniwa* player) {
        if (!Randomizer::isNoClipEnabled()) {
            if (gNoclip) {
                gNoclip = false;
                al::onCollide(player);
                player->endDemoPuppetable();
            }
            return;
        }

        if (al::isPadHoldZL(-1) && al::isPadHoldZR(-1) && al::isPadTriggerUp(-1)) {
            gNoclip = !gNoclip;
            if (!gNoclip) {
                al::onCollide(player);
                player->endDemoPuppetable();
            } else {
                player->startDemoPuppetable();
            }
        }
        if (!gNoclip)
            return;

        constexpr f32 kSpeed = 25.0f;
        constexpr f32 kSpeedMax = 150.0f;
        constexpr f32 kVSpeed = 20.0f;

        sead::Vector3f* playerPos = al::getTransPtr(player);
        sead::Vector3f cameraPos = al::getCameraPos(player, 0);
        sead::Vector2f leftStick = al::getLeftStick(-1);

        al::offCollide(player);
        al::setVelocityZero(player);

        f32 d = sead::Mathf::sqrt(al::powerIn(playerPos->x - cameraPos.x, 2) + al::powerIn(playerPos->z - cameraPos.z, 2));
        f32 vx = ((kSpeed + gSpeedGain) / d) * (playerPos->x - cameraPos.x);
        f32 vz = ((kSpeed + gSpeedGain) / d) * (playerPos->z - cameraPos.z);

        if (!al::isPadHoldZR(-1)) {
            playerPos->x -= leftStick.x * vz;
            playerPos->z += leftStick.x * vx;
            playerPos->x += leftStick.y * vx;
            playerPos->z += leftStick.y * vz;

            if (al::isPadHoldX(-1))
                gSpeedGain -= 0.5f;
            if (al::isPadHoldY(-1))
                gSpeedGain += 0.5f;
            gSpeedGain = sead::Mathf::clamp(gSpeedGain, 0.0f, kSpeedMax);

            if (al::isPadHoldZL(-1) || al::isPadHoldA(-1))
                playerPos->y -= (kVSpeed + gSpeedGain / 6);
            if (al::isPadHoldB(-1))
                playerPos->y += (kVSpeed + gSpeedGain / 6);
        }
    }

    // Was 6 MB. This pool is static storage, so its full size is charged to the
    // module's .bss at load time whether or not anyone ever connects to a
    // server. Combined with the debug menu's inspector scratch it pushed the
    // module past what the process could map alongside the game's own heap:
    // the game's SetHeapSize came back LimitReached and the boot faulted right
    // after. 1 MB is enough for the handful of sockets the puppet protocol
    // opens; if multiplayer throughput suffers, raise this rather than
    // reverting, and see the note in StageSceneStateModConfig.cpp about the
    // scratch buffers that should come out of .bss too.
    static constexpr int socketPoolSize = 1_MB;
    static constexpr int socketAllocPoolSize = 128_KB;
    static char socketPool[socketPoolSize + socketAllocPoolSize] __attribute__((aligned(4_KB)));
    static HkReplace<void> disableSocketInit = [] {};

    static HkTrampoline<void, GameSystem*> gameSystemInit = hk::hook::trampoline([](GameSystem* gameSystem) -> void {
        SeedGenerator::createHeap();
        sImGuiHeap = sead::ExpHeap::create(
            5_MB, "ImGuiHeap", al::getStationedHeap(), 8,
            sead::Heap::cHeapDirection_Forward, false);

        SkyboxHooks::setHeap(sImGuiHeap);

        nn::nifm::Initialize();
        nn::nifm::SubmitNetworkRequest();
        while (nn::nifm::IsNetworkRequestOnHold()) {
        }
        nn::socket::Initialize(socketPool, socketPoolSize, socketAllocPoolSize, 0xE);
        disableSocketInit.installAtSym<"_ZN2nn6socket10InitializeEPvmmi">();
        Client::createInstance(gHeap);

        gameSystemInit.orig(gameSystem);

        SeedGenerator::prepareThread();
        SpoilerLog::ensureDirectory();

        auto* imgui = hk::gfx::ImGuiBackendNvn::instance();
        imgui->setAllocator({ [](size allocSize, size alignment) -> void* {
                                 return sImGuiHeap->tryAlloc(allocSize, alignment);
                             },
            [](void* ptr) -> void {
                sImGuiHeap->free(ptr);
            } });

        nn::hid::InitializeMouse();
        nn::hid::InitializeKeyboard();

        imgui->tryInitialize();
        imgui::setupFont();
    });

    static HkTrampoline<void, GameSystem*> drawMainHookHk = hk::hook::trampoline([](GameSystem* gameSystem) -> void {
        drawMainHookHk.orig(gameSystem);

        auto* drawContext = Application::instance()->mDrawSystemInfo->drawContext;

        imgui::updateImGuiInput();
        ImGui::GetIO().MouseDrawCursor = false;
        ImGui::NewFrame();
        MusicHooks::drawNowPlayingOverlay();

        if (PlayerEventLog::sInstance)
            PlayerEventLog::sInstance->update();

        if (getStageScene()) {
            const s32 startingEquipmentFrames = AbilityOverlay::tryQueueStartingEquipment();
            if (startingEquipmentFrames >= 0)
                PaintingRando::tryQueueFreeLinkAnnouncement(startingEquipmentFrames);
        }
        if (Randomizer::isAbilityLockFeatureEnabled() || AbilityOverlay::hasQueuedEntries())
            AbilityOverlay::draw();
        if (Randomizer::isCaptureLockFeatureEnabled())
            CaptureOverlay::draw();
        MoonPurchaseOverlay::draw();
        PaintingUnlockOverlay::draw();
        TrackerOverlay::draw();
        Bingo::draw();
        TimerMode::draw();
        AreaNameOverlay::draw();
        ModConfigInfoOverlay::draw();
        TrackerBridge::updateProgress();
        PathHintOverlay::draw();
        // Last of the HUD surfaces: the death screen owns the whole frame while
        // it runs, and it draws after the StageScene is gone so nothing else is
        // competing for the middle of the screen anyway.
        NuzlockeOverlay::draw();
        EnemySwapDebug::drawObjectIds();
        EnemySwapDebug::drawInspectorOverlay();
        ImGui::Render();
        hk::gfx::ImGuiBackendNvn::instance()->draw(
            ImGui::GetDrawData(),
            drawContext->getCommandBuffer()->ToData()->pNvnCommandBuffer);
    });

    void updateSequence(HakoniwaSequence* sequence) {
        if (sequence->mGameDataHolderAccessor.mData)
            Bingo::update(sequence->mGameDataHolderAccessor.mData->getGameDataFile());

        StageScene* stageScene = (StageScene*)sequence->mCurrentScene;
        al::PlayerHolder* pHolder = al::getScenePlayerHolder(stageScene);
        PlayerActorHakoniwa* player = (PlayerActorHakoniwa*)al::tryGetPlayerActor(pHolder, 0);
        if (!player)
            return;

        noclip(player);
    }

    void installHooks() {
        gameSystemInit.installAtSym<"_ZN10GameSystem4initEv">();
        drawMainHookHk.installAtSym<"_ZN10GameSystem8drawMainEv">();
    }

} // namespace DebugMenuHooks
