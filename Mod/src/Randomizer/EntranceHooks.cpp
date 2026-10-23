#include "Randomizer/EntranceHooks.h"

#include "Randomizer/MovementAbilityLock.h"

#include <cmath>
#include <cstdio>
#include <cstring>

#include "Randomizer/ArrivalLog.h"
#include "Randomizer/EntranceFallbacks.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/TimerMode.h"
#include "Randomizer/WorldMeta.h"

#include "Library/LiveActor/ActorInitInfo.h"
#include "Library/LiveActor/LiveActor.h"
#include "Library/Nerve/NerveUtil.h"
#include "Library/Placement/PlacementInfo.h"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Player/PlayerUtil.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/CheckpointFlag.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/WorldList.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

namespace EntranceHooks {

    static bool sBypassEntranceRando = false;
    static constexpr const char* kOdysseyEntranceName = "HomeEntrance";
    static constexpr const char* kRoutedStartPrefix = "Randomized_";
    static constexpr s32 kRoutedStartPrefixLength = 11;
    static constexpr ptrdiff_t kCalcLinkChildNumActorInitOffset = 0x009a1634;
    static constexpr ptrdiff_t kGetLinksInfoByIndexActorInitOffset = 0x009a19c0;
    static constexpr ptrdiff_t kGetNextPlayerStartIdOffset = 0x0052f834;
    static constexpr ptrdiff_t kTryFindLinkDestStageInfoOffset = 0x00530708;
    static constexpr ptrdiff_t kPlayerStartInfoCtorOffset = 0x002e2c48;
    static constexpr ptrdiff_t kRegisterPlayerStartInfoOffset = 0x002e2f4c;
    static constexpr ptrdiff_t kTryFindInitInfoByStartIdOffset = 0x002e35e0;
    static constexpr ptrdiff_t kStageSceneStateWarpTryStartWarpOffset = 0x004ff278;
    static constexpr ptrdiff_t kInitEntranceCameraOffset = 0x0083e634;
    static constexpr ptrdiff_t kSetEntranceCameraParamOffset = 0x0083e7c8;
    static constexpr ptrdiff_t kSetEntranceCameraLookAtOffset = 0x0083e7d0;
    static constexpr ptrdiff_t kIsPlayerStartObjOffset = 0x00528384;
    static constexpr ptrdiff_t kIsPlayerStartPlacementOffset = 0x00528410;
    static constexpr ptrdiff_t kIsPlayerStartLinkedObjOffset = 0x005284e4;
    static constexpr ptrdiff_t kReturnPrevStageOffset = 0x0051d6bc;
    static constexpr ptrdiff_t kChangeNextStageWithDemoWorldWarpOffset = 0x0051d8e0;
    static constexpr ptrdiff_t kChangeNextStageWithWorldWarpHoleOffset = 0x0051dc9c;
    static constexpr ptrdiff_t kTryGetRestartPointIdStringOffset = 0x0051e738;
    static constexpr ptrdiff_t kMissAndRestartStageOffset = 0x0051e828;
    static constexpr ptrdiff_t kSetMissRestartInfoOffset = 0x0051e8e0;
    static constexpr ptrdiff_t kEndBossBattleOffset = 0x0047650;
    static constexpr ptrdiff_t kTryGetPlayerActorOffset = 0x009a3fe4;
    static constexpr const char* kBossDeathCheckpointStartId = "Randomized_BossDeathCheckpoint";
    static char sRoutedArrivalStartId[0x80] = {};

    template <typename Fn>
    static Fn mainOffsetFn(ptrdiff_t offset) {
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
    }

    // These functions are present in the game executable but are not exported
    // in the mod link map. Keep calls local to the known main-module address.
    // The vanilla helper is nothing but
    // `ChangeStageInfo::init(file->mMissRestartInfo, placementInfo, file->mGameDataHolder)`
    // followed by setting the use-flag, and it null-checks neither. Vanilla can
    // afford that because StageScene::updatePlay is its only caller - deep into
    // gameplay, where mMissRestartInfo has long been live. We call it from
    // DokanStageChange::init while StageScene::init is still running, and there
    // it has been observed null (crash inside ChangeStageInfo::init on the
    // scene-init thread). With no ChangeStageInfo to fill there is nothing for
    // the call to do, so drop it - both call sites already bail on a null
    // mMissRestartInfo immediately afterwards.
    static void setMissRestartInfo(GameDataFile* file, const al::PlacementInfo& placementInfo) {
        if (!file || !file->mMissRestartInfo) {
            Randomizer::logPush("ER miss-restart skipped: file=%p restartInfo=%p",
                file, file ? file->mMissRestartInfo : nullptr);
            return;
        }
        using SetMissRestartInfoFn = void (*)(GameDataFile*, const al::PlacementInfo&);
        mainOffsetFn<SetMissRestartInfoFn>(kSetMissRestartInfoOffset)(file, placementInfo);
    }

    static al::LiveActor* tryGetPlayerActorAtMainOffset(const al::LiveActor* actor, s32 index) {
        using TryGetPlayerActorFn = al::LiveActor* (*)(const al::LiveActor*, s32);
        return mainOffsetFn<TryGetPlayerActorFn>(kTryGetPlayerActorOffset)(actor, index);
    }

    // Keep the expected endpoint while a routed arrival is in flight: if the game
    // resolves a real PlayerStart under the arrival id, it is trusted as-is (it is a
    // genuine, level-authored spot for that exact id); these coordinates are only
    // used to construct a synthetic PlayerStartInfo when no real one exists at all.
    struct PendingArrival {
        bool active;
        bool forceSynthetic;
        char id[0x80];
        float x;
        float y;
        float z;
    };
    static PendingArrival sPendingArrival = {};

    // True only while an arrival that THIS hook rerouted (an ER door / pipe or a
    // Painting transition) is in flight. The fallback below must never fire for
    // vanilla warps the hook left untouched - a scenario/story reposition (Triple
    // Moon, intro demos) resolves its start id to null on purpose and vanilla
    // places the player correctly; substituting array[0] there drops the player at
    // an arbitrary registered start (a loading-zone exit). Cleared per transition.
    static bool sRoutedArrival = false;
    static bool sFallbackToOdysseyOnMissingStart = false;
    static GameDataFile* sRoutedFakeExitPipeFile = nullptr;
    static char sRoutedFakeExitPipeStage[0x80] = {};
    static char sRoutedFakeExitPipeId[0x80] = {};
    static s32 sRoutedFakeExitPipeScenario = -1;

    // A routed sub-area pipe normally registers its linked PlayerRestartPos as
    // the PlayerStart. Remember the routed pipe until that registration so a
    // death checkpoint can reload through the same linked start instead of
    // restarting from the pipe trigger volume.
    static GameDataFile* sPendingSubAreaPipeCheckpointFile = nullptr;
    static char sPendingSubAreaPipeCheckpointStage[0x80] = {};
    static char sPendingSubAreaPipeCheckpointId[0x80] = {};
    static s32 sPendingSubAreaPipeCheckpointScenario = -1;

    // A generated fake exit pipe is only treated as a PlayerStart while its
    // routed arrival is loading. Keep a separate death-checkpoint record so a
    // later death can arm that same one-shot arrival again, without making
    // ordinary warps or checkpoints inherit it.
    struct FakeExitPipeDeathCheckpoint {
        GameDataFile* file;
        GameDataHolder* holder;
        s64 saveDataId;
        char stage[0x80];
        char id[0x80];
        s32 scenario;
    };
    static FakeExitPipeDeathCheckpoint sFakeExitPipeDeathCheckpoint = {};

    struct SubAreaPipeDeathCheckpoint {
        GameDataFile* file;
        GameDataHolder* holder;
        s64 saveDataId;
        char stage[0x80];
        char id[0x80];
        s32 scenario;
    };
    static SubAreaPipeDeathCheckpoint sSubAreaPipeDeathCheckpoint = {};

    // A cleared boss does not use MissRestartInfo on a later death. The game
    // reloads the current stage and asks for its restart-point id instead, so
    // retain a synthetic PlayerStart for that separate restart path.
    struct BossDeathCheckpoint {
        GameDataFile* file;
        char stage[0x80];
        float x;
        float y;
        float z;
    };
    static BossDeathCheckpoint sBossDeathCheckpoint = {};

    using HomeFallbackEntry = EntranceFallbacks::HomeFallback;
    using HomeFallbackMode = EntranceFallbacks::Mode;
    static constexpr HomeFallbackMode HomeFallback_Vanilla = EntranceFallbacks::Mode_Vanilla;
    static constexpr HomeFallbackMode HomeFallback_Coordinates = EntranceFallbacks::Mode_Coordinates;
    static constexpr HomeFallbackMode HomeFallback_Pipe = EntranceFallbacks::Mode_Pipe;
    static constexpr HomeFallbackMode HomeFallback_MoonPipe = EntranceFallbacks::Mode_Moon_Pipe;
    static constexpr HomeFallbackMode HomeFallback_Odyssey = EntranceFallbacks::Mode_Odyssey;

    static constexpr float kDegToRad = 0.017453292519943295f;

    // BazookaElectric (the launch rocket) has no arrival spawn on its exit side: vanilla
    // only ever launches OUT of it, never lands the player there. We can't replay the
    // rocket-landing demo (that needs a real arrival PlayerStart we don't have), so the
    // best we can do is drop the player in beside the rocket. Spawning ON the rocket
    // coord would re-capture/relaunch, so lift the spawn up by this much and let the
    // player fall in next to it. Vertical is the only direction-agnostic offset - the
    // endpoint table carries position but no facing.
    static constexpr float kRocketSpawnOffsetY = 500.0f;

    // Subarea Keys: a locked door bounces the player straight back out of the
    // door they touched, without ever loading the sub-area. Most gated links are
    // contact-triggered ChangeStageArea volumes, so landing back on the trigger
    // would re-fire the transition immediately (see isSafeSpawnUnit). Lift the
    // arrival clear of the volume AND swallow that one link's transitions for a
    // moment, which covers volumes too tall for the lift to escape.
    static constexpr float kSubareaDenialSpawnOffsetY = 200.0f;
    static constexpr s32 kSubareaDenialSuppressFrames = 90;
    // The Sphinx's vault door lives in SeaWorldSphinxQuizZone, an embedded zone
    // of the Seaside overworld rather than a stage of its own. EntranceDB2
    // records its ChangeStageArea at the ZONE-LOCAL (0, -700, -1200); the zone
    // itself is placed at (600, -6750, -31800), so the door's real world
    // position is (600, -7450, -33000). Bouncing a denied attempt onto the raw
    // endpoint therefore aims ~33000 units off, near the map origin and well
    // under the seabed - which is the fall-through, not the +200 Y nudge.
    //
    // Rather than re-deriving the offset here, spawn at the position the zone's
    // own author placed Mario at (PlayerActorHakoniwa obj2, local (0, 250, 650)
    // -> world (600, -6500, -31150)): in front of the Sphinx, on the approach
    // side of the vault, and ~1900 units clear of the ChangeStageArea so the
    // trigger cannot immediately re-fire. The kingdom's real checkpoints are all
    // up on the trench rim, 11000+ units away and 5900 above the door.
    static constexpr EntranceFallbacks::Vec3 kSeasideSphinxDenialSpawn
        = { 600.0f, -6500.0f, -31150.0f };
    static s32 sSubareaDenialLink = -1;
    static s32 sSubareaDenialFrames = 0;

    static void clearPendingArrival() {
        sPendingArrival.active = false;
        sPendingArrival.forceSynthetic = false;
        sRoutedArrival = false;
        sFallbackToOdysseyOnMissingStart = false;
        sRoutedArrivalStartId[0] = '\0';
        sRoutedFakeExitPipeFile = nullptr;
        sRoutedFakeExitPipeStage[0] = '\0';
        sRoutedFakeExitPipeId[0] = '\0';
        sRoutedFakeExitPipeScenario = -1;
        sPendingSubAreaPipeCheckpointFile = nullptr;
        sPendingSubAreaPipeCheckpointStage[0] = '\0';
        sPendingSubAreaPipeCheckpointId[0] = '\0';
        sPendingSubAreaPipeCheckpointScenario = -1;
    }

    // Shiveria Town is the only Snow Kingdom sub-area whose randomizable
    // entrances (SnowCostumeEx, ShopDoor) can drop the player there directly
    // from an unrelated kingdom, bypassing SnowWorldHomeStage entirely - so it
    // is the only sub-stage that needs an explicit kingdom mapping here.
    static s32 findKingdomIdForAnyStage(const char* stageName) {
        if (stageName && std::strcmp(stageName, "SnowWorldTownStage") == 0)
            return WorldMeta::WorldId_Snow;
        const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
        return entry ? entry->worldId : -1;
    }

    static bool hasRoutedStartPrefix(const char* startId) {
        return startId && std::strncmp(startId, kRoutedStartPrefix, kRoutedStartPrefixLength) == 0;
    }

    static const char* stripRoutedStartPrefix(const char* startId) {
        return hasRoutedStartPrefix(startId) ? startId + kRoutedStartPrefixLength : startId;
    }

    static bool isSameStartId(const char* lhs, const char* rhs) {
        return lhs && rhs && std::strcmp(stripRoutedStartPrefix(lhs), stripRoutedStartPrefix(rhs)) == 0;
    }

    static void rememberRoutedStartId(const char* startId) {
        if (!startId || !startId[0])
            return;
        std::strncpy(sRoutedArrivalStartId, startId, sizeof(sRoutedArrivalStartId) - 1);
        sRoutedArrivalStartId[sizeof(sRoutedArrivalStartId) - 1] = '\0';
    }

    // Keep routed ids out of vanilla ChangeStageList replacement. The loaded
    // stage's player-start lookup strips this prefix before resolving authored
    // starts, mirroring the older randomizer's Randomized_* guard without
    // requiring offline StageData rewrites for every seed.
    static void protectRoutedStartId(ChangeStageInfo* info) {
        if (!info)
            return;

        const char* id = info->mChangeStageId.cstr();
        if (!id || !id[0])
            return;
        if (hasRoutedStartPrefix(id)) {
            rememberRoutedStartId(id);
            return;
        }

        char protectedId[0x80];
        std::snprintf(protectedId, sizeof(protectedId), "%s%s", kRoutedStartPrefix, id);
        info->mChangeStageId.copy(protectedId);
        info->mPlacementString.copy(protectedId);
        rememberRoutedStartId(protectedId);
    }

    // A door's stored coordinate is the trigger itself. Pipes and door-warps are
    // press-to-enter, so standing the player on them is safe. Walk-through zones
    // (ChangeStageArea) and launchers (BazookaElectric) fire on contact, so spawning
    // on them would immediately re-fire the transition (relaunch / loop) - far worse
    // than the default-spawn misplacement. Only coord-spawn the safe types; the rest
    // keep the existing default fallback until a vetted offset spawn is added.
    static bool isSafeSpawnUnit(const char* unit) {
        if (!unit)
            return false;
        return std::strcmp(unit, "DokanStageChange") == 0
            || std::strcmp(unit, "DokanStageChange2D") == 0
            || std::strcmp(unit, "DoorWarpStageChange") == 0
            || std::strcmp(unit, "DoorAreaChangeCap") == 0;
    }

    static bool isSubAreaPipeUnit(const char* unit) {
        return unit
            && (std::strcmp(unit, "DokanStageChange") == 0
                || std::strcmp(unit, "DokanStageChange2D") == 0);
    }

    static bool isOffsetSpawnUnit(const char* unit) {
        if (!unit)
            return false;
        return std::strcmp(unit, "BazookaElectric") == 0;
    }

    static bool scenarioMaskContains(u32 mask, s32 scenario) {
        return scenario >= 1 && scenario <= 15 && (mask & (1u << (scenario - 1)));
    }

    static const HomeFallbackEntry* findHomeFallbackEntry(const char* stage, const char* changeStageId, s32 scenario) {
        if (!stage || !stage[0] || !changeStageId || !changeStageId[0])
            return nullptr;
        for (s32 i = static_cast<s32>(sizeof(EntranceFallbacks::sHomeFallbacks) / sizeof(EntranceFallbacks::sHomeFallbacks[0])) - 1; i >= 0; --i) {
            const HomeFallbackEntry& entry = EntranceFallbacks::sHomeFallbacks[i];
            if (std::strcmp(entry.stage, stage) == 0
                && std::strcmp(entry.changeStageId, changeStageId) == 0
                && (entry.scenarioMask == 0 || scenarioMaskContains(entry.scenarioMask, scenario)))
                return &entry;
        }
        return nullptr;
    }

    static const EntranceFallbacks::OdysseyFallback* findOdysseyFallbackEntry(const char* stage, s32 scenario) {
        if (!stage || !stage[0])
            return nullptr;
        for (s32 i = static_cast<s32>(sizeof(EntranceFallbacks::sOdysseyFallbacks) / sizeof(EntranceFallbacks::sOdysseyFallbacks[0])) - 1; i >= 0; --i) {
            const EntranceFallbacks::OdysseyFallback& entry = EntranceFallbacks::sOdysseyFallbacks[i];
            if (std::strcmp(entry.stage, stage) == 0
                && (entry.scenarioMask == 0 || scenarioMaskContains(entry.scenarioMask, scenario)))
                return &entry;
        }
        return nullptr;
    }

    static const char* homeFallbackModeName(HomeFallbackMode mode) {
        switch (mode) {
        case HomeFallback_Vanilla:
            return "Vanilla";
        case HomeFallback_Coordinates:
            return "Coordinates";
        case HomeFallback_Pipe:
            return "Pipe";
        case HomeFallback_MoonPipe:
            return "MoonPipe";
        case HomeFallback_Odyssey:
            return "Odyssey";
        }
        return "Unknown";
    }

    static bool isPipeFallbackMode(HomeFallbackMode mode) {
        return mode == HomeFallback_Pipe || mode == HomeFallback_MoonPipe;
    }

    static bool hasFakeExitPipe(const char* stage, const char* changeStageId) {
        if (!stage || !stage[0] || !changeStageId || !changeStageId[0])
            return false;
        for (const HomeFallbackEntry& entry : EntranceFallbacks::sHomeFallbacks) {
            if (isPipeFallbackMode(entry.mode)
                && std::strcmp(entry.stage, stage) == 0
                && std::strcmp(entry.changeStageId, changeStageId) == 0)
                return true;
        }
        return false;
    }

    // Id-only variant for identifying a placed actor as one of our generated fake
    // pipes. A zone-hosted entry (patch_fake_exit_pipes.py's AREA_PIPE_SPECS
    // physical_stage override, e.g. LakeWorldTownZone for LakeWorldHomeStage)
    // correctly stamps PlacementFileName with the physical file the object lives
    // in for the game's own object management, which differs from the logical
    // stage EntranceFallbacks.h authors it under - so a stage-qualified lookup
    // never matches for those. changeStageId is unique across the whole fallback
    // table, so matching on it alone is safe.
    static bool hasFakeExitPipeById(const char* changeStageId) {
        if (!changeStageId || !changeStageId[0])
            return false;
        for (const HomeFallbackEntry& entry : EntranceFallbacks::sHomeFallbacks) {
            if (isPipeFallbackMode(entry.mode) && std::strcmp(entry.changeStageId, changeStageId) == 0)
                return true;
        }
        return false;
    }

    // Finds the fallback entry for a home-side id that vanilla considers
    // unavailable this scenario, for any fallback mode - not just pipe modes.
    // Used for vanilla transitions taken before Entrance Randomizer builds its
    // connection tables (ER off, or not yet initialized), where there is no
    // applyHomeConn to fall back on.
    static const HomeFallbackEntry* findUnavailableHomeFallbackEntry(const char* stage, const char* changeStageId,
        s32 scenario) {
        const HomeFallbackEntry* fallback = findHomeFallbackEntry(stage, changeStageId, scenario);
        if (!fallback)
            return nullptr;
        if (isPipeFallbackMode(fallback->mode) && !hasFakeExitPipe(stage, changeStageId))
            return nullptr;

        const EntranceDB2::Endpoint* endpoint = EntranceDB2::findEndpointForScenario(stage, changeStageId, scenario);
        if (!endpoint)
            return fallback;
        if (endpoint->scenarioMask == 0 || scenarioMaskContains(endpoint->scenarioMask, scenario))
            return nullptr;

        return fallback;
    }

    static void findFakePipeCameraParams(const char* stage, const char* changeStageId, s32 scenario,
        float* outYawOffsetDeg, float* outDistance) {
        const HomeFallbackEntry* fallback = findHomeFallbackEntry(stage, changeStageId, scenario);
        *outYawOffsetDeg = fallback && fallback->hasCameraYawOffset ? fallback->cameraYawOffsetDeg : 0.0f;
        *outDistance = fallback ? fallback->cameraDistance : EntranceFallbacks::DefaultCameraDistance;
    }

    // Arms a synthetic-spawn candidate for both sub-door and home-door arrivals alike.
    // Mark the arrival as ER-routed even when we end up not arming coordinates: the
    // generic default-spawn fallback (not just coord-spawn) is gated on sRoutedArrival
    // so non-safe-unit doors and doors with no endpoint data still get rescued from a
    // bounce-back rather than being left completely untouched.
    static void armPendingArrival(const char* entrance, const EntranceDB2::Endpoint* endpoint,
        bool forceSynthetic = false, bool allowUnsafeUnit = false) {
        sPendingArrival.active = false;
        sPendingArrival.forceSynthetic = false;
        sRoutedArrival = true;
        if (!entrance || !entrance[0] || !endpoint)
            return;
        if (!allowUnsafeUnit && !isSafeSpawnUnit(endpoint->unitConfigName) && !isOffsetSpawnUnit(endpoint->unitConfigName))
            return;
        std::strncpy(sPendingArrival.id, entrance, sizeof(sPendingArrival.id) - 1);
        sPendingArrival.id[sizeof(sPendingArrival.id) - 1] = '\0';
        sPendingArrival.x = endpoint->x;
        sPendingArrival.y = endpoint->y;
        sPendingArrival.z = endpoint->z;
        if (isOffsetSpawnUnit(endpoint->unitConfigName))
            sPendingArrival.y += kRocketSpawnOffsetY;
        sPendingArrival.active = true;
        sPendingArrival.forceSynthetic = forceSynthetic;
    }

    static bool armFixedCoordinateArrival(const char* entrance, const EntranceFallbacks::Vec3& coordinates) {
        sPendingArrival.active = false;
        sPendingArrival.forceSynthetic = false;
        sRoutedArrival = true;
        if (!entrance || !entrance[0])
            return false;

        std::strncpy(sPendingArrival.id, entrance, sizeof(sPendingArrival.id) - 1);
        sPendingArrival.id[sizeof(sPendingArrival.id) - 1] = '\0';
        sPendingArrival.x = coordinates.x;
        sPendingArrival.y = coordinates.y;
        sPendingArrival.z = coordinates.z;
        sPendingArrival.active = true;
        sPendingArrival.forceSynthetic = true;
        return true;
    }

    static bool armOdysseyFallbackArrival(const char* stage, s32 scenario, const char* entrance) {
        const EntranceFallbacks::OdysseyFallback* fallback = findOdysseyFallbackEntry(stage, scenario);
        return fallback && armFixedCoordinateArrival(entrance, fallback->coordinates);
    }

    static void rememberFakeExitPipeDeathCheckpoint(GameDataFile* file, const char* stage,
        const char* id, s32 scenario) {
        if (!file || !stage || !stage[0] || !id || !id[0])
            return;

        sFakeExitPipeDeathCheckpoint.file = file;
        sFakeExitPipeDeathCheckpoint.holder = file->getGameDataHolder();
        sFakeExitPipeDeathCheckpoint.saveDataId = file->getSaveDataIdForPrepo();
        std::strncpy(sFakeExitPipeDeathCheckpoint.stage, stage,
            sizeof(sFakeExitPipeDeathCheckpoint.stage) - 1);
        sFakeExitPipeDeathCheckpoint.stage[sizeof(sFakeExitPipeDeathCheckpoint.stage) - 1] = '\0';
        std::strncpy(sFakeExitPipeDeathCheckpoint.id, id,
            sizeof(sFakeExitPipeDeathCheckpoint.id) - 1);
        sFakeExitPipeDeathCheckpoint.id[sizeof(sFakeExitPipeDeathCheckpoint.id) - 1] = '\0';
        sFakeExitPipeDeathCheckpoint.scenario = scenario;
        Randomizer::logPush("ER fake pipe death checkpoint armed: %s/%s scenario=%d",
            sFakeExitPipeDeathCheckpoint.stage,
            sFakeExitPipeDeathCheckpoint.id,
            sFakeExitPipeDeathCheckpoint.scenario);
    }

    static void armFakeExitPipeArrival(GameDataFile* file, const char* stage, const char* id, s32 scenario) {
        sPendingArrival.active = false;
        sPendingArrival.forceSynthetic = false;
        sRoutedArrival = true;
        sFallbackToOdysseyOnMissingStart = true;
        sRoutedFakeExitPipeFile = file;
        if (!stage || !id)
            return;
        std::strncpy(sRoutedFakeExitPipeStage, stage, sizeof(sRoutedFakeExitPipeStage) - 1);
        sRoutedFakeExitPipeStage[sizeof(sRoutedFakeExitPipeStage) - 1] = '\0';
        std::strncpy(sRoutedFakeExitPipeId, id, sizeof(sRoutedFakeExitPipeId) - 1);
        sRoutedFakeExitPipeId[sizeof(sRoutedFakeExitPipeId) - 1] = '\0';
        sRoutedFakeExitPipeScenario = scenario;
        // A fake Dokan may register a linked PlayerRestartPos rather than the
        // fake actor itself. Arm here instead of relying on that registration
        // callback to identify the fake placement.
        rememberFakeExitPipeDeathCheckpoint(file, stage, id, scenario);
    }

    static bool isFakeExitPipeDeathCheckpointFile(GameDataFile* file) {
        return file
            && sFakeExitPipeDeathCheckpoint.file == file
            && sFakeExitPipeDeathCheckpoint.holder == file->getGameDataHolder()
            && sFakeExitPipeDeathCheckpoint.saveDataId == file->getSaveDataIdForPrepo()
            && sFakeExitPipeDeathCheckpoint.stage[0]
            && sFakeExitPipeDeathCheckpoint.id[0];
    }

    static bool matchesFakeExitPipeDeathCheckpoint(GameDataFile* file, const ChangeStageInfo* info) {
        return isFakeExitPipeDeathCheckpointFile(file)
            && info
            && std::strcmp(info->mChangeStageName.cstr(), sFakeExitPipeDeathCheckpoint.stage) == 0
            && isSameStartId(info->mChangeStageId.cstr(), sFakeExitPipeDeathCheckpoint.id);
    }

    static void logFakeExitPipeDeathCheckpointState(const char* event, GameDataFile* file) {
        const ChangeStageInfo* restartInfo = file ? file->mMissRestartInfo : nullptr;
        Randomizer::logPush("ER fake pipe cp %s: valid=%d use=%d miss=%s/%s stored=%s/%s",
            event,
            isFakeExitPipeDeathCheckpointFile(file) ? 1 : 0,
            file && file->mIsUseMissRestartInfo ? 1 : 0,
            restartInfo ? restartInfo->mChangeStageName.cstr() : "NULL",
            restartInfo ? restartInfo->mChangeStageId.cstr() : "NULL",
            sFakeExitPipeDeathCheckpoint.stage[0] ? sFakeExitPipeDeathCheckpoint.stage : "NULL",
            sFakeExitPipeDeathCheckpoint.id[0] ? sFakeExitPipeDeathCheckpoint.id : "NULL");
    }

    static void clearFakeExitPipeDeathCheckpoint(GameDataFile* file) {
        if (!sFakeExitPipeDeathCheckpoint.file)
            return;

        logFakeExitPipeDeathCheckpointState("cleared", file);

        // A later MissRestartArea may already have superseded the generated
        // pipe. Never clear that authored replacement; only disable the
        // engine restart entry while it still points at our pipe checkpoint.
        if (file && matchesFakeExitPipeDeathCheckpoint(file, file->mMissRestartInfo))
            file->mIsUseMissRestartInfo = false;

        sFakeExitPipeDeathCheckpoint = {};
    }

    static void setFakeExitPipeDeathCheckpoint(const al::PlacementInfo* placementInfo,
        const char* changeStageId) {
        GameDataFile* file = sRoutedFakeExitPipeFile;
        if (!file || !placementInfo || !sRoutedFakeExitPipeStage[0])
            return;

        const char* rawId = (changeStageId && changeStageId[0])
            ? changeStageId
            : sRoutedFakeExitPipeId;
        if (!rawId || !rawId[0])
            return;

        setMissRestartInfo(file, *placementInfo);
        ChangeStageInfo* restartInfo = file->mMissRestartInfo;
        if (!restartInfo)
            return;

        // setMissRestartInfo normally copies the pipe's authored destination.
        // This actor is an exit-only visual stand-in, so its authored target is
        // the loading zone it borrowed from rather than the spot Mario arrived
        // at. Rewrite it as a reload of this kingdom at the pipe itself.
        char protectedStartId[0x80];
        std::snprintf(protectedStartId, sizeof(protectedStartId), "%s%s", kRoutedStartPrefix, rawId);
        restartInfo->mChangeStageName.copy(sRoutedFakeExitPipeStage);
        restartInfo->mChangeStageId.copy(protectedStartId);
        restartInfo->mPlacementString.copy(protectedStartId);
        restartInfo->mIsReturn = false;
        if (sRoutedFakeExitPipeScenario > 0)
            restartInfo->mScenarioNo = sRoutedFakeExitPipeScenario;

        rememberFakeExitPipeDeathCheckpoint(file, sRoutedFakeExitPipeStage,
            rawId, restartInfo->mScenarioNo);
    }

    static void armPendingSubAreaPipeDeathCheckpoint(GameDataFile* file, const char* stage,
        const char* id, s32 scenario) {
        sPendingSubAreaPipeCheckpointFile = file;
        sPendingSubAreaPipeCheckpointStage[0] = '\0';
        sPendingSubAreaPipeCheckpointId[0] = '\0';
        sPendingSubAreaPipeCheckpointScenario = scenario;
        if (!file || !stage || !stage[0] || !id || !id[0])
            return;

        std::strncpy(sPendingSubAreaPipeCheckpointStage, stage,
            sizeof(sPendingSubAreaPipeCheckpointStage) - 1);
        sPendingSubAreaPipeCheckpointStage[sizeof(sPendingSubAreaPipeCheckpointStage) - 1] = '\0';
        std::strncpy(sPendingSubAreaPipeCheckpointId, id,
            sizeof(sPendingSubAreaPipeCheckpointId) - 1);
        sPendingSubAreaPipeCheckpointId[sizeof(sPendingSubAreaPipeCheckpointId) - 1] = '\0';
    }

    static bool isSubAreaPipeDeathCheckpointFile(GameDataFile* file) {
        return file
            && sSubAreaPipeDeathCheckpoint.file == file
            && sSubAreaPipeDeathCheckpoint.holder == file->getGameDataHolder()
            && sSubAreaPipeDeathCheckpoint.saveDataId == file->getSaveDataIdForPrepo()
            && sSubAreaPipeDeathCheckpoint.stage[0]
            && sSubAreaPipeDeathCheckpoint.id[0];
    }

    static bool matchesSubAreaPipeDeathCheckpoint(GameDataFile* file, const ChangeStageInfo* info) {
        return isSubAreaPipeDeathCheckpointFile(file)
            && info
            && std::strcmp(info->mChangeStageName.cstr(), sSubAreaPipeDeathCheckpoint.stage) == 0
            && isSameStartId(info->mChangeStageId.cstr(), sSubAreaPipeDeathCheckpoint.id);
    }

    static void clearSubAreaPipeDeathCheckpoint(GameDataFile* file) {
        if (!sSubAreaPipeDeathCheckpoint.file)
            return;

        // An authored MissRestartArea may have replaced the pipe checkpoint.
        // Only turn off the engine entry while it still names our routed pipe.
        if (file && matchesSubAreaPipeDeathCheckpoint(file, file->mMissRestartInfo))
            file->mIsUseMissRestartInfo = false;

        sSubAreaPipeDeathCheckpoint = {};
    }

    // The checkpoint only means anything in the stage it was armed in.
    // StageScene::init feeds tryGetRestartPointIdString straight into
    //     rs::tryFindCheckpointFlagPlayerRestartInfo(holder, id)
    //     al::tryGetPlacementId(&placementId, thatResult, ...)
    // so handing it kBossDeathCheckpointStartId in any other stage points that
    // lookup at an id no CheckpointFlag anywhere carries. Nothing clears the
    // checkpoint on every route out of a boss stage -- warping away leaves it
    // armed -- and the file alone cannot tell the stages apart, which is why the
    // recorded stage has to be part of the test. Without it the arrival after a
    // boss resolves its restart point from a stage that is no longer loaded.
    static bool isBossDeathCheckpointFile(const GameDataFile* file) {
        if (!file || sBossDeathCheckpoint.file != file || !sBossDeathCheckpoint.stage[0])
            return false;
        const char* stage = file->getStageNameCurrent();
        return stage && std::strcmp(stage, sBossDeathCheckpoint.stage) == 0;
    }

    static void clearBossDeathCheckpoint(GameDataFile* file) {
        if (!sBossDeathCheckpoint.file)
            return;

        Randomizer::logPush("ER boss death checkpoint cleared: %s (%d,%d,%d)",
            sBossDeathCheckpoint.stage,
            (s32)sBossDeathCheckpoint.x,
            (s32)sBossDeathCheckpoint.y,
            (s32)sBossDeathCheckpoint.z);
        sBossDeathCheckpoint = {};
    }

    static void setBossDeathCheckpoint(const al::LiveActor* boss) {
        if (!boss)
            return;

        GameDataHolderAccessor accessor(boss);
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        const char* stage = file ? file->getStageNameCurrent() : nullptr;
        if (!file || !stage || !stage[0])
            return;

        // The player is still in the victory scene when endBossBattle fires;
        // that position is the safe, level-authored place to resume afterward.
        const al::LiveActor* player = tryGetPlayerActorAtMainOffset(boss, 0);
        const sead::Vector3f& trans = al::getTrans(player ? player : boss);
        sBossDeathCheckpoint = {};
        sBossDeathCheckpoint.file = file;
        std::strncpy(sBossDeathCheckpoint.stage, stage, sizeof(sBossDeathCheckpoint.stage) - 1);
        sBossDeathCheckpoint.stage[sizeof(sBossDeathCheckpoint.stage) - 1] = '\0';
        sBossDeathCheckpoint.x = trans.x;
        sBossDeathCheckpoint.y = trans.y;
        sBossDeathCheckpoint.z = trans.z;
        Randomizer::logPush("ER boss death checkpoint armed: %s (%d,%d,%d)",
            sBossDeathCheckpoint.stage,
            (s32)sBossDeathCheckpoint.x,
            (s32)sBossDeathCheckpoint.y,
            (s32)sBossDeathCheckpoint.z);
    }

    static void clearRoutedPipeDeathCheckpoints() {
        // The stored owner is the only reliable GameDataFile available to
        // in-stage warp hooks, which have no GameDataFile parameter.
        clearFakeExitPipeDeathCheckpoint(sFakeExitPipeDeathCheckpoint.file);
        clearSubAreaPipeDeathCheckpoint(sSubAreaPipeDeathCheckpoint.file);
        clearBossDeathCheckpoint(sBossDeathCheckpoint.file);
    }

    static void setSubAreaPipeDeathCheckpoint(const al::PlacementInfo* placementInfo) {
        GameDataFile* file = sPendingSubAreaPipeCheckpointFile;
        if (!file || !placementInfo || !sPendingSubAreaPipeCheckpointStage[0]
            || !sPendingSubAreaPipeCheckpointId[0])
            return;

        setMissRestartInfo(file, *placementInfo);
        ChangeStageInfo* restartInfo = file->mMissRestartInfo;
        if (!restartInfo)
            return;

        // The PlayerRestartPos linked to a Dokan is the authored "come out of
        // the pipe" start. setMissRestartInfo initially inherits the pipe's
        // forward destination, so replace it with a reload of this sub-area at
        // the routed entrance. The normal routed-start registration then finds
        // that linked PlayerRestartPos again on death.
        char protectedStartId[0x80];
        std::snprintf(protectedStartId, sizeof(protectedStartId), "%s%s",
            kRoutedStartPrefix, sPendingSubAreaPipeCheckpointId);
        restartInfo->mChangeStageName.copy(sPendingSubAreaPipeCheckpointStage);
        restartInfo->mChangeStageId.copy(protectedStartId);
        restartInfo->mPlacementString.copy(protectedStartId);
        restartInfo->mIsReturn = false;
        if (sPendingSubAreaPipeCheckpointScenario > 0)
            restartInfo->mScenarioNo = sPendingSubAreaPipeCheckpointScenario;

        sSubAreaPipeDeathCheckpoint.file = file;
        sSubAreaPipeDeathCheckpoint.holder = file->getGameDataHolder();
        sSubAreaPipeDeathCheckpoint.saveDataId = file->getSaveDataIdForPrepo();
        std::strncpy(sSubAreaPipeDeathCheckpoint.stage, sPendingSubAreaPipeCheckpointStage,
            sizeof(sSubAreaPipeDeathCheckpoint.stage) - 1);
        sSubAreaPipeDeathCheckpoint.stage[sizeof(sSubAreaPipeDeathCheckpoint.stage) - 1] = '\0';
        std::strncpy(sSubAreaPipeDeathCheckpoint.id, sPendingSubAreaPipeCheckpointId,
            sizeof(sSubAreaPipeDeathCheckpoint.id) - 1);
        sSubAreaPipeDeathCheckpoint.id[sizeof(sSubAreaPipeDeathCheckpoint.id) - 1] = '\0';
        sSubAreaPipeDeathCheckpoint.scenario = restartInfo->mScenarioNo;
        Randomizer::logPush("ER sub pipe death checkpoint: %s/%s scenario=%d",
            sSubAreaPipeDeathCheckpoint.stage,
            sSubAreaPipeDeathCheckpoint.id,
            sSubAreaPipeDeathCheckpoint.scenario);

        sPendingSubAreaPipeCheckpointFile = nullptr;
        sPendingSubAreaPipeCheckpointStage[0] = '\0';
        sPendingSubAreaPipeCheckpointId[0] = '\0';
        sPendingSubAreaPipeCheckpointScenario = -1;
    }

    static bool restoreRoutedPipeDeathCheckpoint(GameDataFile* file, const char* stage,
        const char* id, s32 scenario) {
        if (!file || !file->mMissRestartInfo || !stage || !stage[0] || !id || !id[0])
            return false;

        char protectedStartId[0x80];
        std::snprintf(protectedStartId, sizeof(protectedStartId), "%s%s", kRoutedStartPrefix, id);
        ChangeStageInfo* restartInfo = file->mMissRestartInfo;
        restartInfo->mChangeStageName.copy(stage);
        restartInfo->mChangeStageId.copy(protectedStartId);
        restartInfo->mPlacementString.copy(protectedStartId);
        restartInfo->mIsReturn = false;
        if (scenario > 0)
            restartInfo->mScenarioNo = scenario;
        file->mIsUseMissRestartInfo = true;
        return true;
    }

    // StageScene refreshes MissRestartInfo from the area's default restart
    // volume while the player is alive. Reapply a routed-pipe checkpoint at
    // the moment death consumes it, after that refresh has happened.
    static bool restoreFakeExitPipeDeathCheckpoint(GameDataFile* file) {
        return isFakeExitPipeDeathCheckpointFile(file)
            && restoreRoutedPipeDeathCheckpoint(file,
                sFakeExitPipeDeathCheckpoint.stage,
                sFakeExitPipeDeathCheckpoint.id,
                sFakeExitPipeDeathCheckpoint.scenario);
    }

    static bool restoreSubAreaPipeDeathCheckpoint(GameDataFile* file) {
        return isSubAreaPipeDeathCheckpointFile(file)
            && restoreRoutedPipeDeathCheckpoint(file,
                sSubAreaPipeDeathCheckpoint.stage,
                sSubAreaPipeDeathCheckpoint.id,
                sSubAreaPipeDeathCheckpoint.scenario);
    }

    // GameDataHolder copies MissRestartInfo before it reaches this hook, so
    // pointer identity alone does not identify a death restart.
    static bool isMissRestartTransition(GameDataFile* file, const ChangeStageInfo* info) {
        if (!file || !info || !file->mIsUseMissRestartInfo || !file->mMissRestartInfo)
            return false;
        if (info == file->mMissRestartInfo)
            return true;

        const ChangeStageInfo* restartInfo = file->mMissRestartInfo;
        return std::strcmp(info->mChangeStageName.cstr(), restartInfo->mChangeStageName.cstr()) == 0
            && isSameStartId(info->mChangeStageId.cstr(), restartInfo->mChangeStageId.cstr())
            && info->mScenarioNo == restartInfo->mScenarioNo
            && info->mIsReturn == restartInfo->mIsReturn;
    }

    static s32 findUseScenarioNoFromWorldList(const GameDataHolder* holder, const char* stageName) {
        if (!holder || !holder->mWorldList || !stageName || !stageName[0])
            return -1;

        for (auto& world : holder->mWorldList->mWorldList) {
            for (auto& stage : world.stageNames) {
                if (std::strcmp(stage.stageName.cstr(), stageName) == 0)
                    return stage.useScenario;
            }
        }

        return -1;
    }

    void bypassNextTransition() {
        sBypassEntranceRando = true;
    }

    void resetBypass() {
        sBypassEntranceRando = false;
    }

    void tickSubareaDenialCooldown() {
        if (sSubareaDenialFrames > 0 && --sSubareaDenialFrames == 0)
            sSubareaDenialLink = -1;
    }

    static HkTrampoline<void, GameDataFile*, const ChangeStageInfo*, s32>
        changeNextStageHook = hk::hook::trampoline([](GameDataFile* file, const ChangeStageInfo* info, s32 raceType) -> void {
            const bool isDeathRestart = isMissRestartTransition(file, info);
            const bool isFakeExitPipeDeathRestart = isDeathRestart
                && matchesFakeExitPipeDeathCheckpoint(file, info);
            const bool isSubAreaPipeDeathRestart = isDeathRestart
                && matchesSubAreaPipeDeathCheckpoint(file, info);
            // Any transition not routed below leaves no door coordinate, so the spawn
            // hook falls back to the default; only a routed door sets this again.
            clearPendingArrival();
            if (!isFakeExitPipeDeathRestart)
                clearFakeExitPipeDeathCheckpoint(file);
            if (!isSubAreaPipeDeathRestart)
                clearSubAreaPipeDeathCheckpoint(file);
            // Boss clears use the restart-point path rather than this method.
            // Every actual stage transition is therefore a newer arrival and
            // must supersede a previously saved boss-arena position.
            clearBossDeathCheckpoint(file);
            if (!file || !info || info->isEmpty()) {
                sBypassEntranceRando = false;
                changeNextStageHook.orig(file, info, raceType);
                return;
            }

            GameDataHolder* holder = file->getGameDataHolder();
            const char* curStage = file->getStageNameCurrent();
            const char* destStage = info->mChangeStageName.cstr();
            const char* exitId = info->mChangeStageId.cstr();

            // Logged for every GameDataFile::changeNextStage so a warp out of a sub-area
            // (e.g. warp to the Odyssey) is visible: which path it takes, and the cached
            // current-sub before the transition. If a warp produces NO "ER ENTRY" line,
            // it bypasses this hook entirely (e.g. routes via GameDataHolder::changeNextStage).
            Randomizer::logPush("ER ENTRY: cur=%s dest=%s exit=%s race=%d ret=%d cachedSub=%s",
                curStage ? curStage : "NULL",
                destStage ? destStage : "NULL",
                exitId ? exitId : "NULL",
                raceType,
                info->mIsReturn ? 1 : 0,
                EntranceRando::peekTopSubStage() ? EntranceRando::peekTopSubStage() : "NULL");

            if (raceType == GameDataFile::RaceType_Flag) {
                sBypassEntranceRando = false;
                changeNextStageHook.orig(file, info, raceType);
                ProgressionHooks::resetTravelFacadeForStageChange();
                // Leaving for a race stage: reconcile the cache with where we actually go.
                EntranceRando::syncSubTrackingToStage(destStage);
                Randomizer::logPush("ER FLAG race: cachedSub now %s",
                    EntranceRando::peekTopSubStage() ? EntranceRando::peekTopSubStage() : "NULL");
                return;
            }

            ChangeStageInfo patched(holder, exitId, destStage,
                info->mIsReturn, info->mScenarioNo, info->mSubScenarioType);
            patched.mPlacementString.copy(info->mPlacementString.cstr());
            patched.mWipeType.copy(info->mWipeType.cstr());
            patched.mHintPriority = info->mHintPriority;

            s32 currentWorldId = file->getCurrentWorldId();
            s32 savedScenario = currentWorldId >= 0 && currentWorldId < 0x10 ? file->getScenarioNumArr()[currentWorldId] : -1;

            auto finish = [&]() {
                // A MissRestartArea owns an authored retry target and scenario.
                // Re-normalizing it after a boss clear can select the kingdom's
                // later scenario, where the boss-retry PlayerStart no longer
                // exists and the game falls back to the Odyssey.
                if (!isDeathRestart)
                    ScenarioPatcher::normalizeChangeStageInfo(file, &patched, raceType);

                s32 destWorldId = findKingdomIdForAnyStage(patched.mChangeStageName.cstr());
                if (destWorldId >= 0) {
                    file->mCurrentWorldId = destWorldId;
                    file->mCurrentWorldIdForWrite = destWorldId;
                    file->mCurrentWorldIdForDisp = destWorldId;
                }

                changeNextStageHook.orig(file, &patched, raceType);

                if (currentWorldId >= 0 && savedScenario >= 0 && currentWorldId != destWorldId)
                    file->getScenarioNumArr()[currentWorldId] = savedScenario;

                // World Peace Requirement: On Kingdom Complete marking moved to
                // ProgressionHooks::tryCompleteAcceptedGlobe (fires exactly once per
                // genuine moon-deposit/kingdom-complete event). This stage-transition
                // hook fires on every intermediate stage hop during travel, which
                // wrongly marked the destination kingdom too.

                ProgressionHooks::resetTravelFacadeForStageChange();
                // Single chokepoint: every routed transition ends here, so reconcile the
                // cached current-sub with the ACTUAL destination. Door routes already set it
                // (this confirms the same value); warps / unknown-exit bypasses / returns get
                // it corrected here, which is what prevents a stale cache from misclassifying
                // (and never recovering from) later door transitions.
                const char* before = EntranceRando::peekTopSubStage();
                EntranceRando::syncSubTrackingToStage(patched.mChangeStageName.cstr());
                const char* after = EntranceRando::peekTopSubStage();
                if (before != after)
                    Randomizer::logPush("ER finish: dest=%s cachedSub %s -> %s",
                        patched.mChangeStageName.cstr(),
                        before ? before : "NULL",
                        after ? after : "NULL");
            };

            auto getScenarioForStage = [&](const char* stageName) -> s32 {
                GameDataHolder* holder = file->getGameDataHolder();
                if (!holder)
                    return -1;
                GameDataHolderAccessor accessor(holder);
                // Ask for the authoritative scenario (accounts for per-kingdom start
                // scenarios such as Mushroom/Moon always opening on 2) rather than reading
                // the raw save slot directly: that slot is only populated by the game's own
                // travel/load hooks, so it is still 0 for a kingdom the player has never
                // visited through the normal game flow - exactly the case entrance rando
                // hits when it routes a first-time arrival into a fresh kingdom.
                return ScenarioPatcher::normalizeScenarioForStage(accessor, stageName, 0, false);
            };

            if (isDeathRestart) {
                logFakeExitPipeDeathCheckpointState("change-stage", file);
                if (isFakeExitPipeDeathRestart) {
                    armFakeExitPipeArrival(file, sFakeExitPipeDeathCheckpoint.stage,
                        sFakeExitPipeDeathCheckpoint.id, sFakeExitPipeDeathCheckpoint.scenario);
                    protectRoutedStartId(&patched);
                    Randomizer::logPush("ER fake pipe death restart: %s/%s scenario=%d",
                        patched.mChangeStageName.cstr(), patched.mChangeStageId.cstr(), patched.mScenarioNo);
                } else if (isSubAreaPipeDeathRestart) {
                    // Reloading through the routed pipe makes DokanStageChange
                    // register its linked PlayerRestartPos, rather than using
                    // the pipe trigger itself as the player's restart point.
                    sRoutedArrival = true;
                    protectRoutedStartId(&patched);
                    Randomizer::logPush("ER sub pipe death restart: %s/%s scenario=%d",
                        patched.mChangeStageName.cstr(), patched.mChangeStageId.cstr(), patched.mScenarioNo);
                } else {
                    // Boss arenas use authored MissRestartArea targets. Keep
                    // their destination and scenario untouched, but protect
                    // their raw start id from ChangeStageList remapping (which
                    // can otherwise turn it into HomeEntrance/Odyssey).
                    sRoutedArrival = true;
                    protectRoutedStartId(&patched);
                    Randomizer::logPush("ER authored death restart protected: %s/%s scenario=%d",
                        patched.mChangeStageName.cstr(), patched.mChangeStageId.cstr(), patched.mScenarioNo);
                }
                sBypassEntranceRando = false;
                finish();
                return;
            }

            // Yukimaru races: skip entrance redirect but DO normalize scenario.
            // Also redirect the tutorial stage directly to the race circuit - the
            // tutorial requires a non-obvious race-pause button press to skip, which
            // is confusing in a randomizer context. SnowWorldRaceTutorialStage exits
            // to SnowWorldRace000Stage anyway, so skip the middleman.
            if (raceType == GameDataFile::RaceType_Yukimaru
                || raceType == GameDataFile::RaceType_YukimaruTutorial) {
                if (std::strcmp(patched.mChangeStageName.cstr(), "SnowWorldRaceTutorialStage") == 0) {
                    patched.mChangeStageName.copy("SnowWorldRace000Stage");
                    Randomizer::logPush("ER Yukimaru bypass: skipping tutorial -> SnowWorldRace000Stage");
                } else {
                    Randomizer::logPush("ER Yukimaru bypass: cur=%s dest=%s raceType=%d",
                        curStage ? curStage : "NULL",
                        patched.mChangeStageName.cstr(),
                        raceType);
                }
                sBypassEntranceRando = false;
                finish();
                return;
            }

            if (sBypassEntranceRando) {
                sBypassEntranceRando = false;
                finish();
                return;
            }

            // Painting transitions always route through PaintingRando (identity map
            // when the feature is off): the QoL-forced-open paintings have no vanilla
            // destination data pre-endgame, so the table must supply it.
            if (PaintingRando::isInitialized() && PaintingRando::tryApplyPaintingChange(file, curStage, &patched)) {
                EntranceRando::clearSubStack();
                // A painting is an explicit cross-stage route, not a checkpoint
                // reload. Do not let a checkpoint-warp bit left by an earlier map
                // warp make the destination scene look up the old checkpoint.
                file->mIsWarpCheckpoint = false;
                if (!patched.isEmpty()) {
                    protectRoutedStartId(&patched);
                    sRoutedArrival = true;
                }
                finish();
                return;
            }

            if (!EntranceRando::isInitialized()) {
                const s32 scenario = getScenarioForStage(patched.mChangeStageName.cstr());
                const HomeFallbackEntry* fallback = findUnavailableHomeFallbackEntry(patched.mChangeStageName.cstr(), patched.mChangeStageId.cstr(), scenario);
                if (fallback && fallback->mode != HomeFallback_Vanilla) {
                    // Endpoint lookups need the raw (unprefixed) id - EntranceDB2 keys
                    // its table on the real ChangeStageId. sPendingArrival matching does
                    // not: tryFindInitInfoByStartIdHook compares against the id the game
                    // actually queries with, which is the Randomized_-prefixed one once
                    // protectRoutedStartId runs below. Arming sPendingArrival with the
                    // unprefixed id here would silently never match and fall through to
                    // the stage's arbitrary default spawn.
                    char rawArrival[0x80];
                    std::strncpy(rawArrival, patched.mChangeStageId.cstr(), sizeof(rawArrival) - 1);
                    rawArrival[sizeof(rawArrival) - 1] = '\0';
                    Randomizer::logPush("ER vanilla fallback %s: %s/%s scenario=%d",
                        homeFallbackModeName(fallback->mode), patched.mChangeStageName.cstr(), rawArrival, scenario);
                    file->mIsWarpCheckpoint = false;

                    if (fallback->mode == HomeFallback_Odyssey) {
                        patched.mChangeStageId.copy(kOdysseyEntranceName);
                        patched.mPlacementString.copy(kOdysseyEntranceName);
                    }
                    protectRoutedStartId(&patched);

                    if (fallback->mode == HomeFallback_Coordinates) {
                        const EntranceDB2::Endpoint* endpoint = EntranceDB2::findEndpointForScenario(patched.mChangeStageName.cstr(), rawArrival, scenario);
                        if (!fallback->hasCoordinates
                            || !armFixedCoordinateArrival(patched.mChangeStageId.cstr(), fallback->coordinates))
                            armPendingArrival(patched.mChangeStageId.cstr(), endpoint, true, true);
                    } else if (isPipeFallbackMode(fallback->mode)) {
                        armFakeExitPipeArrival(file, patched.mChangeStageName.cstr(), rawArrival, scenario);
                    } else {
                        if (!armOdysseyFallbackArrival(patched.mChangeStageName.cstr(), scenario, patched.mChangeStageId.cstr()))
                            armPendingArrival(patched.mChangeStageId.cstr(), nullptr);
                    }
                    finish();
                    return;
                }
                Randomizer::logPush("ER disabled: vanilla transition kept dest=%s exit=%s",
                    patched.mChangeStageName.cstr(), patched.mChangeStageId.cstr());
                finish();
                return;
            }

            // When inside a sub-stage (e.g. a shop), the game's getStageNameCurrent()
            // can return the home stage name instead of the sub-stage name. If the sub
            // stack has a sub-stage that differs from curStage, override curStage so
            // Branch A fires and classifies the exit correctly, rather than Branch B
            // treating it as a new entrance and remapping it through the entrance table.
            {
                const char* topSub = EntranceRando::peekTopSubStage();
                const char* trackedSub = EntranceDB::findTrackedSubStageName(topSub);
                if (trackedSub && (!curStage || std::strcmp(curStage, trackedSub) != 0))
                    curStage = trackedSub;
            }

            Randomizer::logPush("ER hook: cur=%s dest=%s exit=%s",
                curStage ? curStage : "NULL",
                destStage ? destStage : "NULL",
                exitId ? exitId : "NULL");
            Randomizer::logPush("ER hook: isReturn=%d exitId='%s' destStage='%s'",
                info->mIsReturn ? 1 : 0,
                exitId ? exitId : "NULL",
                destStage ? destStage : "NULL");

            if (patched.mIsReturn
                || !curStage || !curStage[0]
                || !destStage || !destStage[0]
                || !exitId || !exitId[0]) {
                // Non-door teleport (a warp - e.g. warping to the Odyssey - or a return) that
                // the door routing below never classifies. finish() reconciles the cached
                // current-sub with the destination, so a warp out of a sub-area cannot leave
                // a stale cache that misclassifies later door transitions.
                Randomizer::logPush("ER warp/return early-out: dest=%s", destStage ? destStage : "NULL");
                finish();
                return;
            }

            auto applySubConn = [&](const EntranceRando::Conn& conn) {
                const EntranceDB::EntranceLink& dest = EntranceDB::sLinks[conn.link];
                const bool farSide = conn.kind == EntranceRando::ConnKind_SubFar;
                const char* entrance = farSide ? dest.subEntranceOut : dest.subEntranceIn;
                file->mIsWarpCheckpoint = false;
                patched.mChangeStageName.copy(dest.subStage);
                patched.mChangeStageId.copy(entrance);
                patched.mPlacementString.copy(entrance);
                if (holder) {
                    const s32 useScenario = findUseScenarioNoFromWorldList(holder, dest.subStage);
                    if (useScenario > 0 && patched.mScenarioNo != useScenario) {
                        Randomizer::logPush("ER sub use-scenario: %s %d -> %d",
                            dest.subStage, patched.mScenarioNo, useScenario);
                        patched.mScenarioNo = useScenario;
                    }
                }
                const s32 multiPrizeScenario = ScenarioPatcher::getRequiredScenarioForRandomizedMultiPrizeSubarea(dest.subStage);
                if (multiPrizeScenario > 0 && patched.mScenarioNo != multiPrizeScenario) {
                    Randomizer::logPush("ER multi-prize scenario: %s %d -> %d",
                        dest.subStage, patched.mScenarioNo, multiPrizeScenario);
                    patched.mScenarioNo = multiPrizeScenario;
                }
                // A sub-area's StageMap is an array of scenario roots; the arrival
                // PlayerStart for this entrance only exists in the scenario(s) flagged by
                // its EntranceDB2 endpoint mask (bit b = scenario b+1). Sub-areas are
                // exempt from ScenarioPatcher, so without this the sub inherits the source
                // kingdom's scenario - and if that scenario lacks the entrance's root the
                // player loads the wrong layer (e.g. the Yoshi Magma Swamp serving the
                // plain Luncheon moons, the Yoshi spawn missing -> default fallback). Snap
                // to a scenario that actually carries this entrance, but only when the
                // inherited one doesn't already (so multi-scenario subs keep the root that
                // matches current progress).
                const EntranceDB2::Endpoint* ep = EntranceDB2::findEndpointForScenario(dest.subStage, entrance, patched.mScenarioNo);
                if (ep) {
                    const u32 mask = ep->scenarioMask;
                    const s32 cur = patched.mScenarioNo;
                    const bool curValid = cur >= 1 && cur <= 15 && (mask & (1u << (cur - 1)));
                    if (mask != 0 && !curValid) {
                        for (s32 s = 1; s <= 15; ++s) {
                            if (mask & (1u << (s - 1))) {
                                patched.mScenarioNo = s;
                                Randomizer::logPush("ER sub scenario: %s/%s mask=%08x %d -> %d",
                                    dest.subStage, entrance, mask, cur, s);
                                break;
                            }
                        }
                    }
                }
                protectRoutedStartId(&patched);
                if (ep && isSubAreaPipeUnit(ep->unitConfigName))
                    armPendingSubAreaPipeDeathCheckpoint(file, dest.subStage, entrance, patched.mScenarioNo);
                // Near arrivals already resolve to the sub-area's vanilla default spawn
                // (placed at the entry), which is correct - don't override those. Only the
                // far/exit side has no usable spawn, so coord-spawn it from the door.
                if (farSide)
                    armPendingArrival(patched.mChangeStageId.cstr(), ep);
                else {
                    // Near arrivals normally use the sub-area's default spawn.
                    // Still mark the transition as routed so a missing
                    // scenario-specific PlayerStart is rescued instead of
                    // aborting back to the source entrance.
                    sPendingArrival.active = false;
                    sRoutedArrival = true;
                }
                EntranceRando::noteEnteredSub(dest.subStage);
            };

            auto applyHomeConn = [&](s32 destLink, bool outSide, bool forceDarkerOdyssey,
                                     bool* actualOutSide = nullptr) -> bool {
                const EntranceDB::EntranceLink& dest = EntranceDB::sLinks[destLink];
                if (actualOutSide)
                    *actualOutSide = outSide;

                // The Darker Side loading-zone link itself remains vanilla. A
                // randomized home-side source uses the same logical HomeIn
                // destination, but must arrive at the Darker Side Odyssey
                // rather than at its internal CP_Entrance loading zone.
                // Sub-stage exits pass fallbackHomeLink >= 0 and therefore use
                // the authored CP_Entrance endpoint below.
                if (forceDarkerOdyssey && !outSide && EntranceRando::isDarkerSideLink(destLink)) {
                    const s32 scenario = getScenarioForStage(dest.homeStage);
                    file->mIsWarpCheckpoint = false;
                    patched.mChangeStageName.copy(dest.homeStage);
                    patched.mChangeStageId.copy(kOdysseyEntranceName);
                    patched.mPlacementString.copy(kOdysseyEntranceName);
                    protectRoutedStartId(&patched);
                    if (!armOdysseyFallbackArrival(dest.homeStage, scenario, kOdysseyEntranceName))
                        armPendingArrival(kOdysseyEntranceName, nullptr);
                    if (scenario > 0)
                        patched.mScenarioNo = scenario;
                    Randomizer::logPush("ER Darker Side home warp -> Odyssey: %s scenario=%d",
                        dest.homeStage, scenario);
                    EntranceRando::clearSubStack();
                    return false;
                }

                const char* entrance = outSide ? dest.homeEntranceOut : dest.homeEntranceIn;
                s16 endpointIndex = outSide ? dest.homeOutEndpoint : dest.homeInEndpoint;
                if (!entrance || !entrance[0]) {
                    entrance = outSide ? dest.homeEntranceIn : dest.homeEntranceOut;
                    endpointIndex = outSide ? dest.homeInEndpoint : dest.homeOutEndpoint;
                    if (actualOutSide)
                        *actualOutSide = !outSide;
                }
                // Luncheon's Magma Swamp home-in id ("KeyMoveEx") is a walk-in
                // ChangeStageArea whose only registered spawn is a PlayerRestartPos-linked
                // demo object. Confirmed broken as an arrival target (collision loss) even
                // when coordinate-placed correctly onto a normal spawn. Its paired home-out
                // id ("KeyMoveExDokan") is the real, vanilla-tested return pipe (used
                // successfully by many other doors already), so route arrivals there
                // instead of to the Odyssey fallback. Scoped to this exact door only --
                // not a general "prefer home-out" rule, since that needs wider testing
                // before applying to other doors shaped the same way.
                if (!outSide && std::strcmp(dest.homeStage, "LavaWorldHomeStage") == 0
                    && entrance && std::strcmp(entrance, "KeyMoveEx") == 0
                    && dest.homeEntranceOut && dest.homeEntranceOut[0]) {
                    Randomizer::logPush("ER home override: LavaWorldHomeStage/KeyMoveEx -> %s (exit pipe)",
                        dest.homeEntranceOut);
                    entrance = dest.homeEntranceOut;
                    endpointIndex = dest.homeOutEndpoint;
                    if (actualOutSide)
                        *actualOutSide = true;
                }
                s32 scenario = getScenarioForStage(dest.homeStage);
                const EntranceDB2::Endpoint* configuredEndpoint = endpointIndex >= 0 && endpointIndex < EntranceDB2::sEndpointCount
                    ? &EntranceDB2::sEndpoints[endpointIndex]
                    : nullptr;
                const char* targetStage = configuredEndpoint ? configuredEndpoint->targetStage : nullptr;
                const EntranceDB2::Endpoint* endpoint = EntranceDB2::findEndpointForScenario(dest.homeStage, entrance, scenario, targetStage);
                if (!endpoint && targetStage)
                    endpoint = EntranceDB2::findEndpointForScenario(dest.homeStage, entrance, scenario);
                if (configuredEndpoint && endpoint && endpoint != configuredEndpoint) {
                    Randomizer::logPush("ER home scenario endpoint: %s/%s target=%s mask=%08x -> %08x scenario=%d",
                        dest.homeStage, entrance, targetStage ? targetStage : "",
                        configuredEndpoint->scenarioMask, endpoint->scenarioMask, scenario);
                }
                const char* arrival = entrance && entrance[0] ? entrance : kOdysseyEntranceName;
                bool useFakeExitPipe = false;
                bool useCoordinateFallback = false;
                bool useOdysseyFallback = false;
                const HomeFallbackEntry* fallback = nullptr;
                // No endpoint at all (e.g. rollinggoal, taxireturn - vanilla never
                // authors a home-side spawn for these ids) is always "unavailable",
                // same as findUnavailableHomeFallbackEntry treats it for the ER-off
                // vanilla-transition path. Only gate on scenario availability when
                // there IS endpoint data to check it against.
                //
                // ...and only when there is a real scenario to check it with. A home
                // side that itself sits inside a shuffled sub-area (Deep Woods, whose
                // own doors lead to the Costume Room and the Treasure tree) gets 0
                // back from normalizeScenarioForStage by design - sub-areas do not
                // carry a kingdom scenario. Every non-zero mask fails a 0 test, so
                // testing one here declared every such door unavailable and routed it
                // to the Odyssey fallback; with no Odyssey spawn in the stage the
                // player then landed on its default spawn, which in Deep Woods is the
                // story ceiling drop rather than the door they came out of.
                const bool canJudgeScenario = scenario >= 1;
                if (!endpoint
                    || (canJudgeScenario && endpoint->scenarioMask != 0
                        && !scenarioMaskContains(endpoint->scenarioMask, scenario))) {
                    fallback = findHomeFallbackEntry(dest.homeStage, arrival, scenario);
                    const HomeFallbackMode mode = fallback ? fallback->mode : HomeFallback_Odyssey;
                    Randomizer::logPush("ER home unavailable fallback %s: %s/%s mask=%08x scenario=%d",
                        homeFallbackModeName(mode), dest.homeStage, arrival,
                        endpoint ? endpoint->scenarioMask : 0u, scenario);

                    if (mode == HomeFallback_Vanilla) {
                        // Keep the authored target even though this scenario lacks it.
                        // This is only for explicit per-entrance overrides.
                    } else if (mode == HomeFallback_Coordinates) {
                        useCoordinateFallback = true;
                    } else if (isPipeFallbackMode(mode) && hasFakeExitPipe(dest.homeStage, arrival)) {
                        useFakeExitPipe = true;
                    } else {
                        arrival = kOdysseyEntranceName;
                        endpoint = nullptr;
                        useOdysseyFallback = true;
                    }
                }
                file->mIsWarpCheckpoint = false;
                patched.mChangeStageName.copy(dest.homeStage);
                patched.mChangeStageId.copy(arrival);
                patched.mPlacementString.copy(arrival);
                protectRoutedStartId(&patched);
                if (useFakeExitPipe) {
                    armFakeExitPipeArrival(file, dest.homeStage, arrival, scenario);
                } else if (useOdysseyFallback) {
                    if (!armOdysseyFallbackArrival(dest.homeStage, scenario, patched.mChangeStageId.cstr()))
                        armPendingArrival(patched.mChangeStageId.cstr(), endpoint);
                } else if (useCoordinateFallback) {
                    if (!fallback || !fallback->hasCoordinates
                        || !armFixedCoordinateArrival(patched.mChangeStageId.cstr(), fallback->coordinates))
                        armPendingArrival(patched.mChangeStageId.cstr(), endpoint, true, true);
                } else {
                    armPendingArrival(patched.mChangeStageId.cstr(), endpoint);
                }
                // normalizeScenarioForStage returns the requested value unchanged for
                // sub-areas. Here the request is 0, meaning "let the authored transition
                // choose"; writing that sentinel into ChangeStageInfo strips scenario
                // objects from nested home stages such as the boss-rematch Painting
                // Rooms. Preserve the transition's original scenario unless the
                // normalizer produced a concrete positive one.
                if (scenario > 0)
                    patched.mScenarioNo = scenario;
                EntranceRando::clearSubStack();
                return !useOdysseyFallback;
            };

            // Applies a door's outward connection; fallbackHomeLink/fallbackOutSide
            // describe the vanilla surface for unconnected doors (-1 = leave
            // unmodified).
            auto applyConn = [&](const EntranceRando::Conn& conn, s32 fallbackHomeLink, bool fallbackOutSide) {
                if ((conn.kind == EntranceRando::ConnKind_SubNear || conn.kind == EntranceRando::ConnKind_SubFar)
                    && conn.link >= 0 && conn.link < EntranceDB::sLinkCount) {
                    applySubConn(conn);
                    ArrivalLog::recordArrival(conn);
                } else if ((conn.kind == EntranceRando::ConnKind_HomeIn || conn.kind == EntranceRando::ConnKind_HomeOut)
                    && conn.link >= 0 && conn.link < EntranceDB::sLinkCount) {
                    const bool forceDarkerOdyssey = fallbackHomeLink < 0
                        && conn.kind == EntranceRando::ConnKind_HomeIn
                        && EntranceRando::isDarkerSideLink(conn.link);
                    bool actualOutSide = conn.kind == EntranceRando::ConnKind_HomeOut;
                    if (applyHomeConn(conn.link,
                            conn.kind == EntranceRando::ConnKind_HomeOut,
                            forceDarkerOdyssey, &actualOutSide)) {
                        const EntranceRando::Conn actualConn = {
                            conn.link,
                            actualOutSide ? EntranceRando::ConnKind_HomeOut
                                          : EntranceRando::ConnKind_HomeIn,
                        };
                        ArrivalLog::recordArrival(actualConn);
                    }
                } else if (fallbackHomeLink >= 0)
                    applyHomeConn(fallbackHomeLink, fallbackOutSide, false);
            };

            // Branch A: leaving a sub-stage through one of its doors
            if (EntranceDB::isTrackedSubStage(curStage)) {
                bool doorFar = false;
                const s32 doorLink = EntranceDB::findIndexBySubDoor(curStage, exitId, doorFar);

                if (doorLink >= 0) {
                    const EntranceRando::Conn conn = EntranceRando::getSubDoorConn(doorLink, doorFar);
                    applyConn(conn, doorLink, doorFar);
                    Randomizer::logPush("FINAL A (door %d/%s): %s -> %s (%s)",
                        doorLink, doorFar ? "far" : "near",
                        curStage,
                        patched.mChangeStageName.cstr(),
                        patched.mChangeStageId.cstr());
                    finish();
                    return;
                }

                // A stage can be both a shuffled sub-stage and the home side of a
                // nested loading zone (Shiveria Town and Deep Woods). If this exit
                // is not one of the stage's sub-side doors, let Branch B try its
                // home-side doors. Truly unknown transitions (shop purchases,
                // outfit changes, etc.) still fall through Branch B unchanged.
                Randomizer::logPush("ER branchA: no sub-door match; trying home side");
            }

            // Branch B: entering an overworld pipe
            bool enteredFromOutSide = false;
            const s32 pipeIdx = EntranceDB::findIndexByHomeEntrance(curStage, exitId, enteredFromOutSide);
            if (pipeIdx < 0) {
                Randomizer::logPush("ER branchB: no pipe match");
                finish();
                return;
            }

            // Subarea Keys. Gated here on the HOME side only, matching
            // MoonRando::canUseHomeEntranceSide, so the runtime and the
            // placement logic agree about which doors are shut. Leaving a
            // sub-area (Branch A) is never gated: entrance rando can drop the
            // player inside one whose door they hold no key for.
            if (sSubareaDenialLink == pipeIdx && sSubareaDenialFrames > 0) {
                // Still standing in the volume we just bounced out of. Cancel
                // the transition outright - deliberately without finish(), so
                // no stage change is requested and nothing happens.
                Randomizer::logPush("ER branchB: subarea denial cooldown (link %d)", pipeIdx);
                return;
            }
            if (!MoonRando::canEnterSubareaLink(file, pipeIdx)) {
                const EntranceDB::EntranceLink& locked = EntranceDB::sLinks[pipeIdx];
                // Come back out of the door just used: the opposite home side
                // when the link has a distinct one, otherwise the same door.
                const char* exit = enteredFromOutSide ? locked.homeEntranceIn : locked.homeEntranceOut;
                if (!exit || !exit[0] || std::strcmp(exit, exitId) == 0)
                    exit = exitId;

                const s32 scenario = getScenarioForStage(curStage);
                const EntranceDB2::Endpoint* endpoint
                    = EntranceDB2::findEndpointForScenario(curStage, exit, scenario);
                if (!endpoint)
                    endpoint = EntranceDB2::findEndpoint(curStage, exit);

                patched.mChangeStageName.copy(curStage);
                patched.mChangeStageId.copy(exit);
                patched.mPlacementString.copy(exit);
                protectRoutedStartId(&patched);
                if (EntranceDB::isSeasideSphinxTreasureLink(locked)) {
                    // Everything else about the denial is unchanged - same
                    // stage, same protected start id, same cooldown and message
                    // below. Only the coordinates differ, because this door has
                    // no standable endpoint of its own.
                    armFixedCoordinateArrival(
                        patched.mChangeStageId.cstr(), kSeasideSphinxDenialSpawn);
                } else if (endpoint) {
                    const EntranceFallbacks::Vec3 spawn = { endpoint->x,
                        endpoint->y + kSubareaDenialSpawnOffsetY, endpoint->z };
                    armFixedCoordinateArrival(patched.mChangeStageId.cstr(), spawn);
                } else {
                    armPendingArrival(patched.mChangeStageId.cstr(), endpoint, true, true);
                }

                sSubareaDenialLink = pipeIdx;
                sSubareaDenialFrames = kSubareaDenialSuppressFrames;
                MoonHooks::armSubareaKeyDeniedMessage(
                    SubareaKeyDB::kingdomForLink(pipeIdx), SubareaKeyDB::typeForLink(pipeIdx),
                    curStage);

                Randomizer::logPush("ER DENY (link %d): %s/%s locked -> back out at %s",
                    pipeIdx, curStage, exitId, exit);
                EntranceRando::clearSubStack();
                finish();
                return;
            }

            applyConn(EntranceRando::getHomeDoorConn(pipeIdx, enteredFromOutSide), -1, enteredFromOutSide);
            // Swim lock: remember how to come back out of this door, so an
            // arrival that lands the player in deep water can push them back the
            // way they came. Recorded AFTER applyConn, which is what rewrites
            // patched with the shuffled destination - reading it earlier would
            // record the vanilla one and the guard on arrival would reject it.
            // Same choice of exit the Subarea Key denial above makes: the
            // opposite home side when the link has a distinct one, else the door
            // just used.
            {
                const EntranceDB::EntranceLink& used = EntranceDB::sLinks[pipeIdx];
                const char* back = enteredFromOutSide ? used.homeEntranceIn : used.homeEntranceOut;
                if (!back || !back[0])
                    back = exitId;
                MovementAbilityLock::noteRetreatTarget(
                    curStage, back, patched.mChangeStageName.cstr());
            }
            Randomizer::logPush("FINAL B (pipe %d): %s -> %s (%s)",
                pipeIdx,
                curStage,
                patched.mChangeStageName.cstr(),
                patched.mChangeStageId.cstr());
            finish();
        });

    // GameDataFunction::changeNextStageWithCloset fires when the player exits the shop UI
    // or closet (with or without an outfit change). It uses exitId="ClosetStart" which does
    // not match any sub-stage entrance, so without bypassing, Branch A would classify it as
    // a far-exit and route the player through the entrance table to the wrong location.
    // Bypassing here lets the vanilla reload of the current sub-stage proceed unmodified.
    static HkTrampoline<void, GameDataHolderWriter>
        changeNextStageWithClosetHook = hk::hook::trampoline([](GameDataHolderWriter writer) -> void {
            if (EntranceRando::isInitialized())
                bypassNextTransition();
            changeNextStageWithClosetHook.orig(writer);
        });

    using CalcLinkChildNumActorInitFn = s32 (*)(const al::ActorInitInfo&, const char*);
    using GetLinksInfoByIndexActorInitFn = void (*)(al::PlacementInfo*, const al::ActorInitInfo&, const char*, s32);
    using GetNextPlayerStartIdFn = const char* (*)(const GameDataHolder*);
    using TryFindLinkDestStageInfoFn = bool (*)(const GameDataHolder*, const char**, const char**, const char*, const char*);

    static s32 calcLinkChildNumActorInit(const al::ActorInitInfo& initInfo, const char* linkName) {
        static CalcLinkChildNumActorInitFn fn = reinterpret_cast<CalcLinkChildNumActorInitFn>(
            mainOffsetFn<CalcLinkChildNumActorInitFn>(kCalcLinkChildNumActorInitOffset));
        return fn(initInfo, linkName);
    }

    static void getLinksInfoByIndexActorInit(al::PlacementInfo* out, const al::ActorInitInfo& initInfo,
        const char* linkName, s32 index) {
        static GetLinksInfoByIndexActorInitFn fn = reinterpret_cast<GetLinksInfoByIndexActorInitFn>(
            mainOffsetFn<GetLinksInfoByIndexActorInitFn>(kGetLinksInfoByIndexActorInitOffset));
        fn(out, initInfo, linkName, index);
    }

    static const char* getNextPlayerStartId(const GameDataHolder* holder) {
        static GetNextPlayerStartIdFn fn = reinterpret_cast<GetNextPlayerStartIdFn>(
            mainOffsetFn<GetNextPlayerStartIdFn>(kGetNextPlayerStartIdOffset));
        return holder ? fn(holder) : nullptr;
    }

    static bool tryFindLinkDestStageInfo(const GameDataHolder* holder, const char** destStageName,
        const char** destLabel, const char* srcStageName, const char* srcLabel) {
        static TryFindLinkDestStageInfoFn fn = reinterpret_cast<TryFindLinkDestStageInfoFn>(
            mainOffsetFn<TryFindLinkDestStageInfoFn>(kTryFindLinkDestStageInfoOffset));
        return holder && fn(holder, destStageName, destLabel, srcStageName, srcLabel);
    }

    static bool tryGetPlacementStringByKey(const char** out, const al::PlacementInfo& placementInfo,
        const char* key) {
        *out = nullptr;
        return placementInfo.getPlacementIter().tryGetStringByKey(out, key) && *out && (*out)[0];
    }

    static bool tryGetPlacementChangeStageId(const char** out, const al::PlacementInfo& placementInfo) {
        return tryGetPlacementStringByKey(out, placementInfo, "ChangeStageId");
    }

    static bool isRandoFakeExitPipePlacement(const al::PlacementInfo& placementInfo,
        const char** placementStage, const char** changeStageId) {
        const char* layer = nullptr;
        const char* unitConfig = nullptr;
        const char* model = nullptr;
        const char* stage = nullptr;
        const char* id = nullptr;
        if (!tryGetPlacementStringByKey(&layer, placementInfo, "LayerConfigName")
            || std::strcmp(layer, "RandoObjects") != 0)
            return false;
        if (!tryGetPlacementStringByKey(&unitConfig, placementInfo, "UnitConfigName")
            || std::strcmp(unitConfig, "DokanStageChange") != 0)
            return false;
        if (!tryGetPlacementStringByKey(&model, placementInfo, "ModelName")
            || (std::strcmp(model, "Dokan") != 0 && std::strcmp(model, "DokanMoon") != 0))
            return false;
        if (!tryGetPlacementStringByKey(&stage, placementInfo, "PlacementFileName"))
            return false;
        if (!tryGetPlacementChangeStageId(&id, placementInfo))
            return false;
        if (!hasFakeExitPipeById(id))
            return false;
        if (placementStage)
            *placementStage = stage;
        if (changeStageId)
            *changeStageId = id;
        return true;
    }

    static bool shouldKeepRandoFakeExitPipePlacement(const al::PlacementInfo& placementInfo) {
        const char* placementStage = nullptr;
        const char* changeStageId = nullptr;
        if (!isRandoFakeExitPipePlacement(placementInfo, &placementStage, &changeStageId))
            return false;

        // Id match only, not stage - a zone-hosted pipe's placementStage is the
        // physical file (e.g. LakeWorldTownZone), while sRoutedFakeExitPipeStage
        // is the logical stage armFakeExitPipeArrival recorded (e.g.
        // LakeWorldHomeStage). changeStageId is unique across the fallback table,
        // so it alone is a safe identity check.
        const bool keep = sRoutedArrival
            && sRoutedFakeExitPipeStage[0]
            && sRoutedFakeExitPipeId[0]
            && isSameStartId(sRoutedFakeExitPipeId, changeStageId);
        if (keep)
            Randomizer::logPush("ER fake pipe active: %s/%s", placementStage, changeStageId);
        return keep;
    }

    bool shouldHideRandoFakeExitPipeActor(const al::ActorInitInfo& info) {
        if (!info.placementInfo)
            return false;
        if (!isRandoFakeExitPipePlacement(*info.placementInfo, nullptr, nullptr))
            return false;
        return !shouldKeepRandoFakeExitPipePlacement(*info.placementInfo);
    }

    static bool isRoutedPlayerStartPlacement(GameDataHolderAccessor accessor, const al::PlacementInfo& placementInfo) {
        GameDataHolder* holder = accessor;
        if (!sRoutedArrival || !holder)
            return false;

        const char* nextStartId = getNextPlayerStartId(holder);
        if (!hasRoutedStartPrefix(nextStartId))
            return false;

        const char* changeStageId = nullptr;
        if (!tryGetPlacementChangeStageId(&changeStageId, placementInfo))
            return false;

        // Fake exit pipes are authored with the vanilla ChangeStageId. Match that
        // raw value before consulting ChangeStageList so an unavailable-scenario
        // pipe is still selected as the arrival object instead of being converted
        // back to HomeEntrance/Odyssey.
        if (isSameStartId(nextStartId, changeStageId))
            return true;

        const char* placementFileName = nullptr;
        if (!tryGetPlacementStringByKey(&placementFileName, placementInfo, "PlacementFileName")
            || !placementFileName || !placementFileName[0])
            return false;

        const char* convertedStageName = nullptr;
        const char* convertedChangeStageId = changeStageId;
        if (tryFindLinkDestStageInfo(holder, &convertedStageName, &convertedChangeStageId,
                placementFileName, changeStageId))
            return isSameStartId(nextStartId, convertedChangeStageId);

        return false;
    }

    static bool shouldRegisterAsRoutedStart(const al::PlacementInfo* placementInfo, const char* changeStageId) {
        if (!sRoutedArrival || !sRoutedArrivalStartId[0])
            return false;

        const char* id = changeStageId;
        if ((!id || !id[0]) && placementInfo)
            tryGetPlacementChangeStageId(&id, *placementInfo);

        return isSameStartId(sRoutedArrivalStartId, id);
    }

    static s32 playerStartInfoCount(void* holder);
    static void* playerStartInfoAt(void* holder, s32 index);
    static void* initEntranceCameraForHolder(void* holder, const al::PlacementInfo* placementInfo);
    static void configureRoutedFakePipeEntranceCamera(void* info, const char* id);

    static HkTrampoline<bool, const al::LiveActor*, const al::ActorInitInfo&>
        isPlayerStartObjHook = hk::hook::trampoline([](const al::LiveActor* actor, const al::ActorInitInfo& initInfo) -> bool {
            if (actor && initInfo.placementInfo
                && isRoutedPlayerStartPlacement(GameDataHolderAccessor(actor), *initInfo.placementInfo))
                return true;
            return isPlayerStartObjHook.orig(actor, initInfo);
        });

    static HkTrampoline<bool, const al::LiveActor*, const al::PlacementInfo&>
        isPlayerStartPlacementHook = hk::hook::trampoline([](const al::LiveActor* actor,
                                                              const al::PlacementInfo& placementInfo) -> bool {
            if (actor && isRoutedPlayerStartPlacement(GameDataHolderAccessor(actor), placementInfo))
                return true;
            return isPlayerStartPlacementHook.orig(actor, placementInfo);
        });

    static HkTrampoline<bool, const al::LiveActor*, const al::ActorInitInfo&, const char*>
        isPlayerStartLinkedObjHook = hk::hook::trampoline([](const al::LiveActor* actor, const al::ActorInitInfo& initInfo,
                                                              const char* linkName) -> bool {
            if (actor && sRoutedArrival) {
                GameDataHolderAccessor accessor(actor);
                if (initInfo.placementInfo && isRoutedPlayerStartPlacement(accessor, *initInfo.placementInfo))
                    return true;

                const s32 linkChildNum = calcLinkChildNumActorInit(initInfo, linkName);
                for (s32 i = 0; i < linkChildNum; ++i) {
                    al::PlacementInfo linkInfo;
                    getLinksInfoByIndexActorInit(&linkInfo, initInfo, linkName, i);
                    if (isRoutedPlayerStartPlacement(accessor, linkInfo))
                        return true;
                }
            }
            return isPlayerStartLinkedObjHook.orig(actor, initInfo, linkName);
        });

    static HkTrampoline<void, void*, const al::PlacementInfo*, const char*, void*, void*, const void*, const void*>
        registerPlayerStartInfoHook = hk::hook::trampoline([](void* holder, const al::PlacementInfo* placementInfo,
                                                               const char* changeStageId, void* cameraTicket, void* startObj,
                                                               const void* startPos, const void* startRot) -> void {
            const bool isFakeExitPipe = placementInfo && isRandoFakeExitPipePlacement(*placementInfo, nullptr, nullptr);
            if (isFakeExitPipe && !shouldKeepRandoFakeExitPipePlacement(*placementInfo))
                return;

            const bool isRoutedStart = shouldRegisterAsRoutedStart(placementInfo, changeStageId);
            if (isRoutedStart) {
                const s32 countBefore = playerStartInfoCount(holder);
                void* routedCameraTicket = cameraTicket;
                if (isFakeExitPipe) {
                    // Generated fallback pipes need a real entrance camera ticket even
                    // when the copied source object did not carry IsValidEntranceCamera.
                    void* fakePipeCameraTicket = initEntranceCameraForHolder(holder, placementInfo);
                    if (fakePipeCameraTicket)
                        routedCameraTicket = fakePipeCameraTicket;
                }
                Randomizer::logPush("ER player-start register: %s -> %s",
                    changeStageId ? changeStageId : "NULL", sRoutedArrivalStartId);
                registerPlayerStartInfoHook.orig(holder, placementInfo, sRoutedArrivalStartId, routedCameraTicket, startObj, startPos, startRot);
                if (isFakeExitPipe) {
                    const s32 countAfter = playerStartInfoCount(holder);
                    void* registeredInfo = countAfter > countBefore ? playerStartInfoAt(holder, countAfter - 1) : nullptr;
                    if (registeredInfo)
                        configureRoutedFakePipeEntranceCamera(registeredInfo, sRoutedArrivalStartId);
                    else
                        Randomizer::logPush("ER fake pipe camera skipped: id=%s already registered",
                            sRoutedArrivalStartId);
                    setFakeExitPipeDeathCheckpoint(placementInfo, changeStageId);
                } else if (sPendingSubAreaPipeCheckpointFile
                    && isSameStartId(sRoutedArrivalStartId, sPendingSubAreaPipeCheckpointId)) {
                    setSubAreaPipeDeathCheckpoint(placementInfo);
                } else if (sRoutedFakeExitPipeFile) {
                    Randomizer::logPush("ER fake pipe linked start: id=%s routed=%s",
                        changeStageId ? changeStageId : "NULL", sRoutedArrivalStartId);
                }
                return;
            }

            registerPlayerStartInfoHook.orig(holder, placementInfo, changeStageId, cameraTicket, startObj, startPos, startRot);
        });

    // CheckpointFlag::receiveMsg writes the checkpoint and restart-point IDs
    // before switching to exeGet. Cleaning up in that state preserves the
    // intended "normal checkpoint supersedes temporary respawn" behavior
    // without intercepting either save-data setter in the collision callback.
    static HkTrampoline<void, CheckpointFlag*>
        checkpointGetHook = hk::hook::trampoline([](CheckpointFlag* checkpoint) -> void {
            const bool isFirstStep = checkpoint && al::isFirstStep(checkpoint);
            // Multiplayer used to own a second trampoline here and sent its
            // packet before the original ran (one trampoline per function --
            // see the note in Multiplayer/main.hpp); keep that ordering.
            if (isFirstStep)
                mpOnCheckpointGet(checkpoint);
            checkpointGetHook.orig(checkpoint);
            if (!isFirstStep)
                return;

            clearFakeExitPipeDeathCheckpoint(sFakeExitPipeDeathCheckpoint.file);
            clearSubAreaPipeDeathCheckpoint(sSubAreaPipeDeathCheckpoint.file);
            clearBossDeathCheckpoint(sBossDeathCheckpoint.file);
        });

    // Boss deaths reload the current stage without ChangeStageInfo. StageScene
    // obtains the start id through this getter, so substitute the synthetic id
    // only while the matching boss checkpoint remains active.
    //
    // StageScene::init consumes this id on two different paths, and only one of
    // them tolerates a synthetic value. On a checkpoint warp it runs
    //     info = rs::tryFindCheckpointFlagPlayerRestartInfo(holder, id);
    //     al::tryGetPlacementId(&placementId, info);
    // with no null check between them, and tryGetPlacementId takes its
    // PlacementInfo by reference -- so an id that no CheckpointFlag carries
    // returns null and faults straight away in al::PlacementId::init. Ours can
    // never be found there: it only exists for
    // PlayerStartInfoHolder::tryFindInitInfoByStartId, which
    // tryFindInitInfoByStartIdHook below resolves itself. Warping to the Odyssey
    // after a boss is that checkpoint-warp path, and the warp keeps the player in
    // the same stage, so the stage check alone cannot rule it out. Defer to
    // vanilla whenever the game is doing the checkpoint lookup.
    static HkTrampoline<const char*, const GameDataFile*>
        tryGetRestartPointIdStringHook = hk::hook::trampoline([](const GameDataFile* file) -> const char* {
            if (file && file->isWarpCheckpoint())
                return tryGetRestartPointIdStringHook.orig(file);
            if (isBossDeathCheckpointFile(file)) {
                Randomizer::logPush("ER boss death checkpoint supplied: %s", sBossDeathCheckpoint.stage);
                return kBossDeathCheckpointStartId;
            }
            return tryGetRestartPointIdStringHook.orig(file);
        });

    static HkTrampoline<void, const al::LiveActor*, s32>
        endBossBattleHook = hk::hook::trampoline([](const al::LiveActor* boss, s32 battleType) -> void {
            endBossBattleHook.orig(boss, battleType);
            setBossDeathCheckpoint(boss);
        });

    // World-map/Odyssey travel and world-warp holes bypass
    // GameDataFile::changeNextStage, so clear the temporary pipe checkpoint on
    // their dedicated transition paths as well. Returning to a previous stage
    // is also a new arrival and must not retain an old pipe restart.
    static HkTrampoline<void, GameDataFile*>
        returnPrevStageHook = hk::hook::trampoline([](GameDataFile* file) -> void {
            clearFakeExitPipeDeathCheckpoint(file);
            clearSubAreaPipeDeathCheckpoint(file);
            clearBossDeathCheckpoint(file);
            returnPrevStageHook.orig(file);
        });

    static HkTrampoline<void, GameDataFile*, const char*>
        changeNextStageWithDemoWorldWarpHook = hk::hook::trampoline([](GameDataFile* file, const char* stageName) -> void {
            clearFakeExitPipeDeathCheckpoint(file);
            clearSubAreaPipeDeathCheckpoint(file);
            clearBossDeathCheckpoint(file);
            changeNextStageWithDemoWorldWarpHook.orig(file, stageName);
        });

    static HkTrampoline<void, GameDataFile*, const char*>
        changeNextStageWithWorldWarpHoleHook = hk::hook::trampoline([](GameDataFile* file, const char* name) -> void {
            clearFakeExitPipeDeathCheckpoint(file);
            clearSubAreaPipeDeathCheckpoint(file);
            clearBossDeathCheckpoint(file);
            changeNextStageWithWorldWarpHoleHook.orig(file, name);
        });

    // "Return to Odyssey" is an in-stage WarpArea, so it never reaches any
    // GameDataFile stage-transition function. This method is polled each frame;
    // clear only after the original reports that a warp really began.
    static HkTrampoline<bool, void*>
        stageSceneStateWarpTryStartWarpHook = hk::hook::trampoline([](void* stateWarp) -> bool {
            const bool didStartWarp = stageSceneStateWarpTryStartWarpHook.orig(stateWarp);
            if (didStartWarp) {
                clearRoutedPipeDeathCheckpoints();
                Randomizer::logPush("ER temporary death checkpoints cleared by in-stage warp");
            }
            return didStartWarp;
        });

    static HkTrampoline<void, GameDataFile*>
        missAndRestartStageHook = hk::hook::trampoline([](GameDataFile* file) -> void {
            // This is the normal StageScene miss path.  Minigames and manual
            // reloads do not call it, which keeps Nuzlocke penalties limited to
            // genuine Mario deaths.
            // Arm the post-destruction coordinator, but preserve the original
            // miss bookkeeping so StageScene still reaches HakoniwaSequence's
            // destroy state.  The coordinator takes over only after that scene
            // and its frame heap are gone.
            Client::onPlayerDeathDetected();
            Nuzlocke::armMarioDeath(file);
            // Timer Mode's death penalty answers to the same signal for the same
            // reason: this is a genuine Mario death, not a minigame or a manual
            // reload. A timeout reaches here as an ordinary death too, and is
            // recognized and waived on the clock state it already set.
            TimerMode::notifyPlayerDeath();
            // Player Event Log signal only - the 10-coin vanilla death
            // penalty is real GameDataFunction::subCoin, already relayed
            // to peers by subCoinHook (syncHooks.cpp) like any other coin
            // change, so nothing coin-related needs to happen here.
            if (Client::isSocketActive()) {
                Client::sendPlayerDeathPacket();
                PlayerEventLog::addSelfEvent(PlayerEventLog::DEATH, "");
            }
            logFakeExitPipeDeathCheckpointState("death", file);
            // A warp can disable MissRestartInfo without overwriting its
            // buffer. Respect that disabled flag: restoring the old routed
            // pipe here would resurrect it after the player moved elsewhere.
            const bool isUseMissRestartInfo = file && file->mIsUseMissRestartInfo;
            if (isUseMissRestartInfo && isBossDeathCheckpointFile(file)) {
                // A newly-authored MissRestartArea is a newer restart target
                // than the boss checkpoint and must win.
                clearBossDeathCheckpoint(file);
            }
            if (isUseMissRestartInfo && restoreFakeExitPipeDeathCheckpoint(file)) {
                Randomizer::logPush("ER fake pipe death checkpoint restored");
            } else if (isUseMissRestartInfo && restoreSubAreaPipeDeathCheckpoint(file)) {
                Randomizer::logPush("ER sub pipe death checkpoint restored");
            } else if (isFakeExitPipeDeathCheckpointFile(file)
                || isSubAreaPipeDeathCheckpointFile(file)) {
                Randomizer::logPush("ER routed pipe death checkpoint discarded: miss restart disabled");
                clearRoutedPipeDeathCheckpoints();
            }
            missAndRestartStageHook.orig(file);
        });

    // PlayerStartInfoHolder layout (from PlayerStartInfoHolder.md): the registered
    // PlayerStartInfo* array base lives at +0x18 and its count at +0x10.
    static s32 playerStartInfoCount(void* holder) {
        if (!holder)
            return 0;
        u8* h = reinterpret_cast<u8*>(holder);
        return *reinterpret_cast<const s32*>(h + 0x10);
    }

    static void* playerStartInfoAt(void* holder, s32 index) {
        if (!holder || index < 0)
            return nullptr;
        u8* h = reinterpret_cast<u8*>(holder);
        const s32 count = playerStartInfoCount(holder);
        if (index >= count)
            return nullptr;
        void** array = *reinterpret_cast<void***>(h + 0x18);
        return array ? array[index] : nullptr;
    }

    static void* firstPlayerStartInfo(void* holder) {
        return playerStartInfoAt(holder, 0);
    }

    static const void* placementInfoFromPlayerStartInfo(void* info) {
        return info ? *reinterpret_cast<const void* const*>(info) : nullptr;
    }

    static const void* cameraTicketFromPlayerStartInfo(void* info) {
        return info ? *reinterpret_cast<const void* const*>(reinterpret_cast<const u8*>(info) + 0x8) : nullptr;
    }

    static const float* positionFromPlayerStartInfo(void* info) {
        return info ? reinterpret_cast<const float*>(reinterpret_cast<const u8*>(info) + 0xb0) : nullptr;
    }

    static const void* rotationFromPlayerStartInfo(void* info) {
        return info ? reinterpret_cast<const u8*>(info) + 0xbc : nullptr;
    }

    static const float* rotationQuatFromPlayerStartInfo(void* info) {
        return reinterpret_cast<const float*>(rotationFromPlayerStartInfo(info));
    }

    using InitEntranceCameraFn = void* (*)(const void*, const al::PlacementInfo*, const char*);
    using SetEntranceCameraParamFn = void (*)(void*, float, const float*, const float*);
    using SetEntranceCameraLookAtFn = void (*)(void*, const float*);

    static void* initEntranceCameraForHolder(void* holder, const al::PlacementInfo* placementInfo) {
        if (!holder || !placementInfo)
            return nullptr;
        auto initEntranceCamera = mainOffsetFn<InitEntranceCameraFn>(kInitEntranceCameraOffset);
        return initEntranceCamera(reinterpret_cast<const u8*>(holder) + 0x8, placementInfo, nullptr);
    }

    static void normalizeCameraDirection(float* direction) {
        const float len = std::sqrt(direction[0] * direction[0]
            + direction[1] * direction[1]
            + direction[2] * direction[2]);
        if (len <= 0.001f) {
            direction[0] = 0.0f;
            direction[1] = 0.25f;
            direction[2] = -1.0f;
            return;
        }

        direction[0] /= len;
        direction[1] /= len;
        direction[2] /= len;
    }

    static void calcFakePipeCameraDirection(float* direction, const float* quat) {
        if (!direction)
            return;

        if (!quat) {
            direction[0] = 0.0f;
            direction[1] = 0.25f;
            direction[2] = -1.0f;
            normalizeCameraDirection(direction);
            return;
        }

        const float x = quat[0];
        const float y = quat[1];
        const float z = quat[2];
        const float w = quat[3];
        const float forwardX = 2.0f * (x * z + w * y);
        const float forwardZ = 1.0f - 2.0f * (x * x + y * y);
        direction[0] = -forwardX;
        direction[1] = 0.25f;
        direction[2] = -forwardZ;
        normalizeCameraDirection(direction);
    }

    static void rotateCameraDirectionYaw(float* direction, float yawOffsetDeg) {
        if (!direction || yawOffsetDeg == 0.0f)
            return;

        const float yaw = yawOffsetDeg * kDegToRad;
        const float sinYaw = std::sin(yaw);
        const float cosYaw = std::cos(yaw);
        const float x = direction[0];
        const float z = direction[2];
        direction[0] = x * cosYaw + z * sinYaw;
        direction[2] = -x * sinYaw + z * cosYaw;
        normalizeCameraDirection(direction);
    }

    static void configureRoutedFakePipeEntranceCamera(void* info, const char* id) {
        void* cameraTicket = const_cast<void*>(cameraTicketFromPlayerStartInfo(info));
        const float* position = positionFromPlayerStartInfo(info);
        const float* quat = rotationQuatFromPlayerStartInfo(info);
        if (!cameraTicket || !position) {
            Randomizer::logPush("ER fake pipe camera missing: id=%s ticket=%d pos=%d",
                id ? id : "NULL", cameraTicket ? 1 : 0, position ? 1 : 0);
            return;
        }

        float direction[3];
        calcFakePipeCameraDirection(direction, quat);
        float yawOffsetDeg = 0.0f;
        float distance = EntranceFallbacks::DefaultCameraDistance;
        findFakePipeCameraParams(sRoutedFakeExitPipeStage, sRoutedFakeExitPipeId, sRoutedFakeExitPipeScenario,
            &yawOffsetDeg, &distance);
        rotateCameraDirectionYaw(direction, yawOffsetDeg);
        const float lookAtOffset[3] = { 0.0f, 160.0f, 0.0f };
        const float lookAt[3] = { position[0], position[1] + 160.0f, position[2] };
        auto setEntranceCameraParam = mainOffsetFn<SetEntranceCameraParamFn>(kSetEntranceCameraParamOffset);
        auto setEntranceCameraLookAt = mainOffsetFn<SetEntranceCameraLookAtFn>(kSetEntranceCameraLookAtOffset);
        setEntranceCameraParam(cameraTicket, distance, direction, lookAtOffset);
        setEntranceCameraLookAt(cameraTicket, lookAt);
        Randomizer::logPush("ER fake pipe camera: id=%s pos=(%d,%d,%d) dir=(%d,%d,%d) yaw=%d dist=%d",
            id ? id : "NULL",
            (s32)position[0], (s32)position[1], (s32)position[2],
            (s32)(direction[0] * 100.0f), (s32)(direction[1] * 100.0f), (s32)(direction[2] * 100.0f),
            (s32)yawOffsetDeg, (s32)distance);
    }

    // Constructs an independent PlayerStartInfo at a door's real coordinates instead of
    // patching the stage's unrelated default-spawn object. Calls the engine's own
    // PlayerStartInfo::PlayerStartInfo(PlacementInfo const*, CameraTicket*, char const*,
    // PlayerStartObj*, Vector3f const*, Quat const*) constructor directly at its known
    // offset, the same "call at module offset" pattern AbilityLock.cpp already uses.
    // StageScene later copies the start's PlacementInfo into scene init state, so the
    // synthetic entry must borrow a real backing PlacementInfo even though its id and
    // coordinates are overridden here. sead::BaseQuat's memory layout is x,y,z,w
    // (seadMathBase.h), so identity is {0,0,0,1}.
    using PlayerStartInfoCtorFn = void (*)(void*, const void*, const void*, const char*,
        const void*, const void*, const void*);

    static void* buildSyntheticPlayerStartInfo(const void* backingPlacementInfo, const void* cameraTicket,
        const char* id, float x, float y, float z,
        const void* startRot = nullptr) {
        if (!backingPlacementInfo)
            return nullptr;

        alignas(8) static u8 sBuffer[0xD0];
        const float pos[3] = { x, y, z };
        const float rot[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        auto ctor = reinterpret_cast<PlayerStartInfoCtorFn>(
            mainOffsetFn<PlayerStartInfoCtorFn>(kPlayerStartInfoCtorOffset));
        ctor(sBuffer, backingPlacementInfo, cameraTicket, id, nullptr, pos, startRot ? startRot : rot);
        return sBuffer;
    }

    static bool pendingArrivalMatches(const char* startId) {
        return sPendingArrival.active
            && startId
            && std::strcmp(startId, sPendingArrival.id) == 0;
    }

    static void* tryBuildForcedPendingSynthetic(void* backingInfo, const char* startId) {
        if (!sPendingArrival.forceSynthetic || !pendingArrivalMatches(startId))
            return nullptr;

        void* synthetic = buildSyntheticPlayerStartInfo(placementInfoFromPlayerStartInfo(backingInfo),
            cameraTicketFromPlayerStartInfo(backingInfo),
            startId, sPendingArrival.x, sPendingArrival.y, sPendingArrival.z,
            rotationFromPlayerStartInfo(backingInfo));
        if (synthetic) {
            Randomizer::logPush("ER forced coord-spawn: id=%s -> (%d,%d,%d)",
                startId ? startId : "NULL",
                (s32)sPendingArrival.x, (s32)sPendingArrival.y, (s32)sPendingArrival.z);
            clearPendingArrival();
            return synthetic;
        }

        return nullptr;
    }

    // PlayerStartInfoHolder::tryFindInitInfoByStartId resolves an arrival's start id to
    // a registered spawn, returning null when no PlayerStartObj carries that id. A real
    // resolved PlayerStart is trusted as-is -- it is a genuine, level-authored spot for
    // that exact id, so there is nothing to validate or correct. When nothing is found
    // at all, construct a synthetic PlayerStartInfo from the door's own recorded
    // coordinates rather than falling back to the stage's arbitrary default spawn.
    static HkTrampoline<void*, void*, const char*>
        tryFindInitInfoByStartIdHook = hk::hook::trampoline([](void* holder, const char* startId) -> void* {
            void* info = tryFindInitInfoByStartIdHook.orig(holder, startId);
            if (sBossDeathCheckpoint.file && startId
                && std::strcmp(startId, kBossDeathCheckpointStartId) == 0) {
                void* backingInfo = info ? info : firstPlayerStartInfo(holder);
                void* synthetic = buildSyntheticPlayerStartInfo(placementInfoFromPlayerStartInfo(backingInfo),
                    cameraTicketFromPlayerStartInfo(backingInfo), startId,
                    sBossDeathCheckpoint.x, sBossDeathCheckpoint.y, sBossDeathCheckpoint.z,
                    rotationFromPlayerStartInfo(backingInfo));
                if (synthetic) {
                    Randomizer::logPush("ER boss death respawn: %s -> (%d,%d,%d)",
                        sBossDeathCheckpoint.stage,
                        (s32)sBossDeathCheckpoint.x,
                        (s32)sBossDeathCheckpoint.y,
                        (s32)sBossDeathCheckpoint.z);
                    return synthetic;
                }

                Randomizer::logPush("ER boss death respawn backing missing: %s", sBossDeathCheckpoint.stage);
            }
            const char* unprotectedStartId = stripRoutedStartPrefix(startId);
            if (!info && unprotectedStartId != startId) {
                info = tryFindInitInfoByStartIdHook.orig(holder, unprotectedStartId);
                if (info) {
                    if (void* forcedSynthetic = tryBuildForcedPendingSynthetic(info, startId))
                        return forcedSynthetic;
                    if (sRoutedArrival) {
                        Randomizer::logPush("ER player-start resolved: id=%s -> %s",
                            startId ? startId : "NULL",
                            unprotectedStartId ? unprotectedStartId : "NULL");
                        clearPendingArrival();
                    }
                    return info;
                }
            }
            if (info) {
                if (void* forcedSynthetic = tryBuildForcedPendingSynthetic(info, startId))
                    return forcedSynthetic;
                // Routed-arrival state belongs to one stage load. Consume it even
                // when the requested start exists; otherwise the next unrelated
                // null lookup (notably a checkpoint warp) is replaced with a
                // synthetic spawn or array[0].
                if (sRoutedArrival) {
                    Randomizer::logPush("ER player-start resolved: id=%s",
                        startId ? startId : "NULL");
                    clearPendingArrival();
                }
                return info;
            }
            // Only rescue arrivals THIS hook rerouted. A null result on a vanilla warp
            // (scenario/story reposition such as a Triple Moon) is expected and handled
            // by the game; intervening there drops the player at an arbitrary start.
            if (!sRoutedArrival)
                return info;
            if (!EntranceRando::isInitialized() && !PaintingRando::isInitialized()
                && !sFallbackToOdysseyOnMissingStart && !sPendingArrival.active)
                return info;

            if (sFallbackToOdysseyOnMissingStart) {
                void* odysseyInfo = tryFindInitInfoByStartIdHook.orig(holder, kOdysseyEntranceName);
                if (odysseyInfo) {
                    const EntranceFallbacks::OdysseyFallback* odysseyFallback = findOdysseyFallbackEntry(sRoutedFakeExitPipeStage, sRoutedFakeExitPipeScenario);
                    if (odysseyFallback) {
                        void* synthetic = buildSyntheticPlayerStartInfo(placementInfoFromPlayerStartInfo(odysseyInfo),
                            cameraTicketFromPlayerStartInfo(odysseyInfo),
                            startId,
                            odysseyFallback->coordinates.x,
                            odysseyFallback->coordinates.y,
                            odysseyFallback->coordinates.z,
                            rotationFromPlayerStartInfo(odysseyInfo));
                        if (synthetic) {
                            Randomizer::logPush("ER fake pipe missing: id=%s -> Odyssey coords (%d,%d,%d)",
                                startId ? startId : "NULL",
                                (s32)odysseyFallback->coordinates.x,
                                (s32)odysseyFallback->coordinates.y,
                                (s32)odysseyFallback->coordinates.z);
                            clearPendingArrival();
                            return synthetic;
                        }
                    }

                    Randomizer::logPush("ER fake pipe missing: id=%s -> Odyssey",
                        startId ? startId : "NULL");
                    clearPendingArrival();
                    return odysseyInfo;
                }
            }

            // We routed this arrival to a door with no registered spawn. Construct an
            // independent PlayerStartInfo at the door's real coordinates (from
            // EntranceDB2) instead of falling back to the stage's arbitrary default
            // spawn.
            if (pendingArrivalMatches(startId)) {
                void* backingInfo = firstPlayerStartInfo(holder);
                void* synthetic = buildSyntheticPlayerStartInfo(placementInfoFromPlayerStartInfo(backingInfo),
                    nullptr, startId, sPendingArrival.x, sPendingArrival.y, sPendingArrival.z);
                if (synthetic) {
                    Randomizer::logPush("ER coord-spawn: id=%s -> (%d,%d,%d)",
                        startId, (s32)sPendingArrival.x, (s32)sPendingArrival.y, (s32)sPendingArrival.z);
                    clearPendingArrival();
                    return synthetic;
                }

                Randomizer::logPush("ER coord-spawn backing missing: id=%s -> default spawn",
                    startId ? startId : "NULL");
                clearPendingArrival();
                return backingInfo;
            }

            void* fallback = firstPlayerStartInfo(holder);
            Randomizer::logPush("ER player-start fallback: id=%s not found -> default spawn",
                startId ? startId : "NULL");
            clearPendingArrival();
            return fallback;
        });

    // A start id no PlayerStartObj carries, so tryFindInitInfoByStartId resolves
    // it to null and the coord-spawn path below builds a synthetic spawn. Used
    // for randomized start positions at nodes that are not loading zones
    // (checkpoints, custom places) and therefore have no authored spawn to aim at.
    static constexpr const char* kStartPositionStartId = "RandoStartPos";

    const char* getStartPositionStartId(const char* entranceId) {
        return entranceId && entranceId[0] ? entranceId : kStartPositionStartId;
    }

    void armStartPositionArrival(const char* entranceId, float x, float y, float z,
        bool hasCoordinates) {
        clearPendingArrival();

        const bool hasEntranceId = entranceId && entranceId[0];
        if (!hasEntranceId && !hasCoordinates)
            return;

        // Mark the arrival as ours either way: the generic default-spawn rescue
        // is gated on sRoutedArrival, so an opening spawn whose id fails to
        // resolve still lands somewhere sane instead of bouncing.
        sRoutedArrival = true;

        const char* startId = getStartPositionStartId(entranceId);
        if (hasCoordinates) {
            std::strncpy(sPendingArrival.id, startId, sizeof(sPendingArrival.id) - 1);
            sPendingArrival.id[sizeof(sPendingArrival.id) - 1] = '\0';
            sPendingArrival.x = x;
            sPendingArrival.y = y;
            sPendingArrival.z = z;
            sPendingArrival.active = true;
            // With a real loading-zone id, a resolved authored PlayerStart is the
            // better spawn and the coordinates stay a fallback. Without one there
            // is nothing to resolve, so force the synthetic spawn.
            sPendingArrival.forceSynthetic = !hasEntranceId;
        }

        Randomizer::logPush("start position arrival armed: id=%s coords=%d (%d,%d,%d)",
            startId, hasCoordinates ? 1 : 0, (s32)x, (s32)y, (s32)z);
    }

    void installHooks() {
        changeNextStageHook.installAtSym<"_ZN12GameDataFile15changeNextStageEPK15ChangeStageInfoi">();
        changeNextStageWithClosetHook.installAtMainOffset(0x00527bf4);
        isPlayerStartObjHook.installAtMainOffset(kIsPlayerStartObjOffset);
        isPlayerStartPlacementHook.installAtMainOffset(kIsPlayerStartPlacementOffset);
        isPlayerStartLinkedObjHook.installAtMainOffset(kIsPlayerStartLinkedObjOffset);
        registerPlayerStartInfoHook.installAtMainOffset(kRegisterPlayerStartInfoOffset);
        tryFindInitInfoByStartIdHook.installAtMainOffset(kTryFindInitInfoByStartIdOffset);
        checkpointGetHook.installAtSym<"_ZN14CheckpointFlag6exeGetEv">();
        tryGetRestartPointIdStringHook.installAtMainOffset(kTryGetRestartPointIdStringOffset);
        returnPrevStageHook.installAtMainOffset(kReturnPrevStageOffset);
        changeNextStageWithDemoWorldWarpHook.installAtMainOffset(kChangeNextStageWithDemoWorldWarpOffset);
        changeNextStageWithWorldWarpHoleHook.installAtMainOffset(kChangeNextStageWithWorldWarpHoleOffset);
        stageSceneStateWarpTryStartWarpHook.installAtMainOffset(kStageSceneStateWarpTryStartWarpOffset);
        missAndRestartStageHook.installAtMainOffset(kMissAndRestartStageOffset);
        endBossBattleHook.installAtMainOffset(kEndBossBattleOffset);
    }

} // namespace EntranceHooks
