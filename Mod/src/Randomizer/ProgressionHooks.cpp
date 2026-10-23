#include "Randomizer/ProgressionHooks.h"

#include "Multiplayer/main.hpp"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/CoinRando.h"
#include "Randomizer/CreditsRoll.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MoonRequirement.h"
#include "Randomizer/MovementAbilityLock.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SphinxRando.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/WorldMeta.h"

#include "Randomizer/MoonRequirement.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SphinxRando.h"
#include "Randomizer/WorldMeta.h"
#include <cstring>

#include <cstring>

#include "Library/Base/StringUtil.h"
#include "Library/Layout/LayoutActor.h"
#include "Library/LiveActor/ActorInitInfo.h"
#include "Library/LiveActor/ActorPoseUtil.h"
#include "Library/LiveActor/ActorSensorUtil.h"
#include "Library/LiveActor/LiveActor.h"
#include "Library/Message/MessageHolder.h"
#include "Library/Nerve/NerveUtil.h"
#include "al/Library/Placement/PlacementFunction.h"
#include "al/Library/Placement/PlacementInfo.h"
#include "al/Project/HitSensor/HitSensor.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/ChurchDoor.h"
#include "game/MapObj/ShineTowerRocket.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/GameProgressData.h"
#include "game/System/SaveObjInfo.h"
#include "game/System/WorldWarpTalkData.h"
#include "game/Util/SensorMsgFunction.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "sead/prim/seadSafeString.h"

class KoopaStateDeadAndDemoBattleEnd;

namespace ProgressionHooks {

    enum RocketNerveIndex : s32
    {
        RocketNerve_Wait = 0,
        RocketNerve_DemoAppearFromEntrance = 1,
        RocketNerve_Reaction = 5,
        RocketNerve_NoStartEarth = 6,
        RocketNerve_DemoPrepareNoShine = 9,
        RocketNerve_GoToWorldMapWithCamera = 10,
        RocketNerve_DemoWalkPlayerToPointNoShine = 14,
        RocketNerve_NoStart = 17,
    };

    static constexpr uintptr_t kRocketNerveStride = sizeof(void*);
    static constexpr const char* kProgressionStartedFlag = "SMORandoProgressionStarted";
    static constexpr const char* kAuditProfileMarker = "SMOR_AUDIT_PROFILE:1";
    static constexpr const char* kCloudHomeStageName = "CloudWorldHomeStage";
    static constexpr const char* kHomeEntranceName = "HomeEntrance";
    static const al::Nerve* sRocketNerveBase = nullptr;
    static char sNoDemoOdysseyArrivalStage[64] = {};
    static bool sAllowCurrentWorldAsUnlockedForHomeInit = false;
    static s32 sCurrentWorldIdForTravelState = -1;
    static s32 sOdysseyFixedFacadeDepth = 0;
    static s32 sOdysseyTravelFacadeDepth = 0;
    static bool sWorldMapTravelFacadeActive = false;
    static GameProgressData* sMaskedHomeStatusProgress = nullptr;
    static GameProgressData::HomeStatus sMaskedHomeStatusOriginal = GameProgressData::HomeStatus::None;
    static bool sHasMaskedHomeStatus = false;
    static GameDataFile* sMaskedTravelFlagFile = nullptr;
    static s32 sMaskedDemoWorldWarpState = 0;
    static bool sMaskedDemoReturnToHome = false;
    static bool sMaskedDemoAwardSpecial = false;
    static bool sMaskedStartKoopaCapture = false;
    static bool sMaskedKoopaLv3 = false;
    static bool sHasMaskedTravelFlags = false;
    static WorldWarpTalkData* sMaskedWorldWarpTalkData = nullptr;
    static s32 sMaskedWorldWarpTalkSize = 0;
    static bool sMaskedWorldWarpTalkPrev[sNumWorlds] = {};
    static bool sMaskedWorldWarpTalkNext[sNumWorlds] = {};
    static bool sMaskedWorldWarpTalkFindKoopa = false;
    static bool sMaskedWorldWarpTalkBossRaid = false;
    static bool sHasMaskedWorldWarpTalk = false;
    static GameProgressData* sPendingLockedProgress = nullptr;
    static s32 sPendingLockedWorlds[2] = { -1, -1 };
    static s32 sPendingLockedWorldCount = 0;
    static s32 sProgressionUnlockDemoDepth = 0;
    static s32 sProgressionUnlockDemoWorlds[2] = { -1, -1 };
    static s32 sProgressionUnlockDemoWorldCount = 0;
    static s32 sCompletedWorldNoDepositDemoDepth = 0;
    static GameDataFile* sCompletedWorldNoDepositDemoFile = nullptr;
    static s32 sWorldMapUnlockMessageWorldId = -1;
    static s32 sSavedFirstBranchChoiceWorld = -1;
    static s32 sSavedSecondBranchChoiceWorld = -1;
    // Game world id the vanilla world map asked to unlock (GameProgressData::unlockNextWorld).
    // Historical stash-and-commit mechanism: unlockNextWorldProgressHook no longer writes to
    // this (it just calls normalizeStoryProgress), so this stays -1 and the consumer inside
    // syncProgressionWorlds is unreachable dead code; unlocks are committed via
    // KingdomProgress::commitBranchChoice / renderAuthoritativeProgression instead.
    static s32 sPendingUnlockNextWorldGameId = -1;
    static GameDataFile* sGlobeCompletionFile = nullptr;
    static s32 sGlobeCompletionWorldId = -1;
    static GameDataFile* sGlobeStartEvaluationFile = nullptr;

    static void syncProgressionWorlds(GameDataFile* file);
    static void clearPendingLockedProgressionWorlds();
    static s32 getBaseUnlockWorldCount(GameDataFile* file);

    static bool isOdysseyFixedFacadeActive() {
        return true;
    }

    static bool isOdysseyTravelFacadeActive() {
        return sOdysseyTravelFacadeDepth > 0 || sWorldMapTravelFacadeActive;
    }

    class ScopedOdysseyFixedFacade {
    public:
        ScopedOdysseyFixedFacade() { sOdysseyFixedFacadeDepth++; }

        ~ScopedOdysseyFixedFacade() {
            if (sOdysseyFixedFacadeDepth > 0)
                sOdysseyFixedFacadeDepth--;
        }
    };

    class ScopedOdysseyTravelFacade {
    public:
        ScopedOdysseyTravelFacade() { sOdysseyTravelFacadeDepth++; }

        ~ScopedOdysseyTravelFacade() {
            if (sOdysseyTravelFacadeDepth > 0)
                sOdysseyTravelFacadeDepth--;
        }
    };

    class ScopedFixedHomeStatus {
    public:
        ScopedFixedHomeStatus(GameProgressData* progress)
            : mProgress(progress) {
            if (!mProgress || !isOdysseyFixedFacadeActive())
                return;
            mOriginal = mProgress->mHomeStatus;
            mDidMask = true;
            mProgress->mHomeStatus = GameProgressData::HomeStatus::LaunchedHome;
        }

        ScopedFixedHomeStatus(GameDataFile* file)
            : ScopedFixedHomeStatus(file ? file->getGameProgressData() : nullptr) { }

        ~ScopedFixedHomeStatus() {
            if (mDidMask)
                mProgress->mHomeStatus = mOriginal;
        }

    private:
        GameProgressData* mProgress = nullptr;
        GameProgressData::HomeStatus mOriginal = GameProgressData::HomeStatus::None;
        bool mDidMask = false;
    };

    static bool isBlockedOdysseyHomeStatus(GameProgressData::HomeStatus status) {
        return status == GameProgressData::HomeStatus::FoundKoopa || status == GameProgressData::HomeStatus::CrashedHome || status == GameProgressData::HomeStatus::RepairedHome || status == GameProgressData::HomeStatus::BossAttackedHome || status == GameProgressData::HomeStatus::RepairedHomeByCrashedBoss;
    }

    static void normalizeStoryProgress(GameProgressData* progress) {
        if (!progress)
            return;

        progress->mHomeStatus = GameProgressData::HomeStatus::LaunchedHome;
        progress->mWaterfallWorldProgress = GameProgressData::WaterfallWorldProgress::TalkedCapNearHome;
    }

    static void clearStoryTravelFlags(GameDataFile* file) {
        if (!file)
            return;

        uintptr_t base = reinterpret_cast<uintptr_t>(file);
        const bool isWorldWarpHoleDemo = *reinterpret_cast<bool*>(base + 0x9fe);
        if (!isWorldWarpHoleDemo) {
            *reinterpret_cast<s32*>(base + 0x9f8) = 0;
            *reinterpret_cast<bool*>(base + 0x9fc) = false;
            *reinterpret_cast<bool*>(base + 0x9fd) = false;
        }
        *reinterpret_cast<bool*>(base + 0xa41) = false;
        *reinterpret_cast<bool*>(base + 0xb64) = false;
    }

    static void normalizeStoryProgress(GameDataFile* file) {
        if (!file)
            return;

        normalizeStoryProgress(file->getGameProgressData());
        clearStoryTravelFlags(file);
    }

    static void restorePersistentFixedHomeStatusMask() {
        if (!sHasMaskedHomeStatus)
            return;

        if (sMaskedHomeStatusProgress)
            normalizeStoryProgress(sMaskedHomeStatusProgress);

        sMaskedHomeStatusProgress = nullptr;
        sMaskedHomeStatusOriginal = GameProgressData::HomeStatus::None;
        sHasMaskedHomeStatus = false;
    }

    static void beginPersistentFixedHomeStatusMask(GameProgressData* progress) {
        if (!progress)
            return;

        normalizeStoryProgress(progress);

        if (sHasMaskedHomeStatus && sMaskedHomeStatusProgress != progress)
            restorePersistentFixedHomeStatusMask();

        if (!sHasMaskedHomeStatus) {
            sMaskedHomeStatusProgress = progress;
            sMaskedHomeStatusOriginal = progress->mHomeStatus;
            sHasMaskedHomeStatus = true;
        }

        if (isBlockedOdysseyHomeStatus(progress->mHomeStatus))
            progress->mHomeStatus = GameProgressData::HomeStatus::LaunchedHome;
    }

    static void beginPersistentFixedHomeStatusMask(GameDataFile* file) {
        beginPersistentFixedHomeStatusMask(file ? file->getGameProgressData() : nullptr);
    }

    static void restorePersistentTravelFlagMask() {
        if (!sHasMaskedTravelFlags)
            return;

        clearStoryTravelFlags(sMaskedTravelFlagFile);

        sMaskedTravelFlagFile = nullptr;
        sMaskedDemoWorldWarpState = 0;
        sMaskedDemoReturnToHome = false;
        sMaskedDemoAwardSpecial = false;
        sMaskedStartKoopaCapture = false;
        sMaskedKoopaLv3 = false;
        sHasMaskedTravelFlags = false;
    }

    static void beginPersistentTravelFlagMask(GameDataFile* file) {
        if (!file)
            return;

        if (sHasMaskedTravelFlags && sMaskedTravelFlagFile != file)
            restorePersistentTravelFlagMask();

        uintptr_t base = reinterpret_cast<uintptr_t>(file);
        if (!sHasMaskedTravelFlags) {
            sMaskedTravelFlagFile = file;
            sMaskedDemoWorldWarpState = *reinterpret_cast<s32*>(base + 0x9f8);
            sMaskedDemoReturnToHome = *reinterpret_cast<bool*>(base + 0x9fc);
            sMaskedDemoAwardSpecial = *reinterpret_cast<bool*>(base + 0x9fd);
            sMaskedStartKoopaCapture = *reinterpret_cast<bool*>(base + 0xa41);
            sMaskedKoopaLv3 = *reinterpret_cast<bool*>(base + 0xb64);
            sHasMaskedTravelFlags = true;
        }

        const bool isWorldWarpHoleDemo = *reinterpret_cast<bool*>(base + 0x9fe);
        if (!isWorldWarpHoleDemo) {
            *reinterpret_cast<s32*>(base + 0x9f8) = 0;
            *reinterpret_cast<bool*>(base + 0x9fc) = false;
            *reinterpret_cast<bool*>(base + 0x9fd) = false;
        }
        *reinterpret_cast<bool*>(base + 0xa41) = false;
        *reinterpret_cast<bool*>(base + 0xb64) = false;
    }

    static void restorePersistentWorldWarpTalkMask() {
        if (!sHasMaskedWorldWarpTalk)
            return;

        if (sMaskedWorldWarpTalkData) {
            const s32 count = sMaskedWorldWarpTalkSize < sNumWorlds ? sMaskedWorldWarpTalkSize : sNumWorlds;
            for (s32 i = 0; i < count; i++) {
                if (sMaskedWorldWarpTalkData->mPrev)
                    sMaskedWorldWarpTalkData->mPrev[i] = sMaskedWorldWarpTalkPrev[i];
                if (sMaskedWorldWarpTalkData->mNext)
                    sMaskedWorldWarpTalkData->mNext[i] = sMaskedWorldWarpTalkNext[i];
            }
            sMaskedWorldWarpTalkData->mIsTalkFindKoopa = sMaskedWorldWarpTalkFindKoopa;
            sMaskedWorldWarpTalkData->mIsTalkBossRaid = sMaskedWorldWarpTalkBossRaid;
        }

        sMaskedWorldWarpTalkData = nullptr;
        sMaskedWorldWarpTalkSize = 0;
        sMaskedWorldWarpTalkFindKoopa = false;
        sMaskedWorldWarpTalkBossRaid = false;
        sHasMaskedWorldWarpTalk = false;
    }

    static void beginPersistentWorldWarpTalkMask(WorldWarpTalkData* talkData) {
        if (!talkData)
            return;

        if (sHasMaskedWorldWarpTalk && sMaskedWorldWarpTalkData != talkData)
            restorePersistentWorldWarpTalkMask();

        if (!sHasMaskedWorldWarpTalk) {
            sMaskedWorldWarpTalkData = talkData;
            sMaskedWorldWarpTalkSize = talkData->mSize;
            const s32 count = talkData->mSize < sNumWorlds ? talkData->mSize : sNumWorlds;
            for (s32 i = 0; i < count; i++) {
                sMaskedWorldWarpTalkPrev[i] = talkData->mPrev && talkData->mPrev[i];
                sMaskedWorldWarpTalkNext[i] = talkData->mNext && talkData->mNext[i];
            }
            sMaskedWorldWarpTalkFindKoopa = talkData->mIsTalkFindKoopa;
            sMaskedWorldWarpTalkBossRaid = talkData->mIsTalkBossRaid;
            sHasMaskedWorldWarpTalk = true;
        }

        talkData->mIsTalkFindKoopa = true;
        talkData->mIsTalkBossRaid = true;

        for (s32 i = 0; i < talkData->mSize; i++) {
            if (talkData->mPrev)
                talkData->mPrev[i] = true;
            if (talkData->mNext)
                talkData->mNext[i] = true;
        }
    }

    static s32 sMoonShineCounterPayOverrideDepth = 0;
    static s32 sChurchDoorInitDepth = 0;
    static ChurchDoor* sInitializingChurchDoor = nullptr;

    static bool isValidTravelWorld(s32 worldId) {
        return worldId >= 0 && worldId < WorldMeta::sWorldCount;
    }

    static bool isValidGameWorldId(s32 worldId) {
        return worldId >= 0 && worldId < WorldMeta::sWorldCount;
    }

    static const WorldMeta::Entry* findWorldByGameWorldId(s32 gameWorldId) {
        return WorldMeta::getByWorldId(gameWorldId);
    }

    static s32 toLogicalWorldId(s32 gameWorldId) {
        // Current-world, world-map, shine, and pay-shine APIs all use the
        // WorldList ID. scenarioWorldId is stage/scenario metadata, not a
        // second runtime progression ID.
        return gameWorldId;
    }

    static s32 toGameWorldId(s32 logicalWorldId) {
        return logicalWorldId;
    }

    static s32 toRequirementWorldId(s32 worldId) {
        // findUnlockShineNum is queried with the logical world-list index.
        // Prefer that index so branch pairs do not swap their requirements.
        return WorldMeta::getByWorldId(worldId) ? worldId : toLogicalWorldId(worldId);
    }

    static bool debugOrderChanged(const s32* values, s32 count, s32* lastValues, s32& lastCount) {
        if (count != lastCount)
            return true;
        for (s32 i = 0; i < count; i++) {
            if (values[i] != lastValues[i])
                return true;
        }
        return false;
    }

    static void debugCopyOrder(const s32* values, s32 count, s32* lastValues, s32& lastCount) {
        lastCount = count;
        for (s32 i = 0; i < sNumWorlds; i++)
            lastValues[i] = i < count ? values[i] : -1;
    }

    static void debugRouteOrder(const char* reason) {
        static s32 sLastRoute[sNumWorlds] = {};
        static s32 sLastRouteCount = -1;
        static s32 sLastFirstChoice = -2;
        static s32 sLastSecondChoice = -2;

        s32 route[sNumWorlds] = {};
        const s32 routeCount = KingdomRando::getRouteWorldCount();
        const s32 count = routeCount < sNumWorlds ? routeCount : sNumWorlds;
        for (s32 i = 0; i < count; i++)
            route[i] = KingdomRando::getRouteWorldId(i);

        const bool changed = debugOrderChanged(route, count, sLastRoute, sLastRouteCount) || sLastFirstChoice != sSavedFirstBranchChoiceWorld || sLastSecondChoice != sSavedSecondBranchChoiceWorld;
        if (!changed)
            return;

        Randomizer::logPush("route %s count=%d f=%d s=%d moon=%d",
            reason ? reason : "sync", count, sSavedFirstBranchChoiceWorld, sSavedSecondBranchChoiceWorld,
            KingdomRando::isMoonInRandomOrder());
        for (s32 i = 0; i < count; i += 5) {
            Randomizer::logPush("route ids %d: %d %d %d %d %d", i,
                route[i],
                i + 1 < count ? route[i + 1] : -1,
                i + 2 < count ? route[i + 2] : -1,
                i + 3 < count ? route[i + 3] : -1,
                i + 4 < count ? route[i + 4] : -1);
        }

        debugCopyOrder(route, count, sLastRoute, sLastRouteCount);
        sLastFirstChoice = sSavedFirstBranchChoiceWorld;
        sLastSecondChoice = sSavedSecondBranchChoiceWorld;
    }

    static void debugWorldMapOrder(GameDataFile* file, s32 count, const s32* unlockedWorldId) {
        static GameDataFile* sLastFile = nullptr;
        static s32 sLastMap[sNumWorlds] = {};
        static s32 sLastMapCount = -1;

        s32 map[sNumWorlds] = {};
        const s32 safeCount = count < sNumWorlds ? count : sNumWorlds;
        for (s32 i = 0; i < safeCount; i++)
            map[i] = unlockedWorldId ? unlockedWorldId[i] : -1;

        if (file == sLastFile && !debugOrderChanged(map, safeCount, sLastMap, sLastMapCount))
            return;

        Randomizer::logPush("map save file=%llx count=%d cur=%d prev=%d",
            (unsigned long long)(uintptr_t)file, safeCount,
            file ? toLogicalWorldId(file->getCurrentWorldId()) : -1,
            file ? toLogicalWorldId(file->getPrevWorldId()) : -1);
        const uintptr_t base = reinterpret_cast<uintptr_t>(file);
        const s32* mapIndex = file ? *reinterpret_cast<s32* const*>(base + 0xb40) : nullptr;
        const s32* warpIndex = file ? *reinterpret_cast<s32* const*>(base + 0xb50) : nullptr;
        const GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
        Randomizer::logPush(
            "MAP meta pending=%d [%d,%d] msg=%d demoDepth=%d unlockNum=%d",
            sPendingLockedWorldCount, sPendingLockedWorlds[0],
            sPendingLockedWorlds[1], sWorldMapUnlockMessageWorldId,
            sProgressionUnlockDemoDepth,
            progress ? progress->mUnlockWorldNum : -1);
        for (s32 i = 0; i < safeCount; i += 5) {
            Randomizer::logPush("MAP unlocked %d: %d %d %d %d %d", i,
                map[i],
                i + 1 < safeCount ? map[i + 1] : -1,
                i + 2 < safeCount ? map[i + 2] : -1,
                i + 3 < safeCount ? map[i + 3] : -1,
                i + 4 < safeCount ? map[i + 4] : -1);
            Randomizer::logPush("MAP index    %d: %d %d %d %d %d", i,
                mapIndex ? mapIndex[i] : -1,
                mapIndex && i + 1 < safeCount ? mapIndex[i + 1] : -1,
                mapIndex && i + 2 < safeCount ? mapIndex[i + 2] : -1,
                mapIndex && i + 3 < safeCount ? mapIndex[i + 3] : -1,
                mapIndex && i + 4 < safeCount ? mapIndex[i + 4] : -1);
            Randomizer::logPush("MAP warp     %d: %d %d %d %d %d", i,
                warpIndex ? warpIndex[i] : -1,
                warpIndex && i + 1 < safeCount ? warpIndex[i + 1] : -1,
                warpIndex && i + 2 < safeCount ? warpIndex[i + 2] : -1,
                warpIndex && i + 3 < safeCount ? warpIndex[i + 3] : -1,
                warpIndex && i + 4 < safeCount ? warpIndex[i + 4] : -1);
        }

        sLastFile = file;
        debugCopyOrder(map, safeCount, sLastMap, sLastMapCount);
    }

    struct WorldMapDevelopName {
        const char* name;
        s32 logicalWorldId;
    };

    static constexpr WorldMapDevelopName sWorldMapDevelopNames[] = {
        { "Cap", WorldMeta::WorldId_Cap },
        { "Waterfall", WorldMeta::WorldId_Cascade },
        { "Cascade", WorldMeta::WorldId_Cascade },
        { "Sand", WorldMeta::WorldId_Sand },
        { "Forest", WorldMeta::WorldId_Wooded },
        { "Wooded", WorldMeta::WorldId_Wooded },
        { "Lake", WorldMeta::WorldId_Lake },
        { "Cloud", WorldMeta::WorldId_Cloud },
        { "Clash", WorldMeta::WorldId_Lost },
        { "Lost", WorldMeta::WorldId_Lost },
        { "City", WorldMeta::WorldId_Metro },
        { "Metro", WorldMeta::WorldId_Metro },
        { "Sea", WorldMeta::WorldId_Seaside },
        { "Seaside", WorldMeta::WorldId_Seaside },
        { "Snow", WorldMeta::WorldId_Snow },
        { "Lava", WorldMeta::WorldId_Luncheon },
        { "Luncheon", WorldMeta::WorldId_Luncheon },
        { "BossRaid", WorldMeta::WorldId_Ruined },
        { "Ruined", WorldMeta::WorldId_Ruined },
        { "Sky", WorldMeta::WorldId_Bowsers },
        { "Bowsers", WorldMeta::WorldId_Bowsers },
        { "Moon", WorldMeta::WorldId_Moon },
        { "Peach", WorldMeta::WorldId_Mushroom },
        { "Mushroom", WorldMeta::WorldId_Mushroom },
        { "Special1", WorldMeta::WorldId_Dark },
        { "Dark", WorldMeta::WorldId_Dark },
        { "Special2", WorldMeta::WorldId_Darker },
        { "Darker", WorldMeta::WorldId_Darker },
    };

    static bool startsWith(const char* text, const char* prefix) {
        return text && prefix && std::strncmp(text, prefix, std::strlen(prefix)) == 0;
    }

    static s32 findLogicalWorldIdByWorldMapDevelopName(const char* name) {
        if (!name || !name[0])
            return -1;

        for (const WorldMapDevelopName& entry : sWorldMapDevelopNames) {
            if (std::strcmp(entry.name, name) == 0)
                return entry.logicalWorldId;
        }
        return -1;
    }

    static s32 tryGetWorldMapUnlockMessageWorldId(const char* fileName, const char* key, bool* outIsDecision) {
        if (outIsDecision)
            *outIsDecision = false;
        if (!fileName || std::strcmp(fileName, "WorldMap") != 0 || !key)
            return -1;

        static constexpr const char* kUnlockPrefix = "UnlockWorld_";
        static constexpr const char* kDecisionPrefix = "DecisionWorld_";
        const char* developName = nullptr;
        if (startsWith(key, kUnlockPrefix)) {
            developName = key + std::strlen(kUnlockPrefix);
        } else if (startsWith(key, kDecisionPrefix)) {
            developName = key + std::strlen(kDecisionPrefix);
            if (outIsDecision)
                *outIsDecision = true;
        } else {
            return -1;
        }

        return findLogicalWorldIdByWorldMapDevelopName(developName);
    }

    static bool isNullMessageString(const char16_t* message) {
        return message && message[0] == u'N' && message[1] == u'U' && message[2] == u'L' && message[3] == u'L' && message[4] == u'\0';
    }

    // The Cappy "cap message" a painting (WorldWarpHole) pops uses labels in the
    // "CapMessage" archive named WorldWarp...<n>. Painting Rando remaps paintings
    // and the QoL patches force them open pre-endgame, producing world/scenario
    // combos vanilla never authored a label for. CapMessageLayout::exeDelay then
    // renders the literal "NULL" on screen. MoonHooks' isExistLabelInSystemMessage
    // hook forces those labels to "exist" so rendering reaches getSystemMessageString
    // (overridden below) where we supply a neutral Cappy line instead.
    static bool isCapMessageWorldWarpKey(const char* fileName, const char* key) {
        return fileName && key && std::strcmp(fileName, "CapMessage") == 0
            && startsWith(key, "WorldWarp");
    }

    static const char16_t* makeWorldWarpCapFallbackMessage(const char* key) {
        if (key && std::strstr(key, "Out"))
            return u"These paintings link distant kingdoms together.";
        if (key && std::strstr(key, "Look"))
            return u"This painting hums with the energy of another kingdom...";
        return u"This painting leads somewhere far away...";
    }

    static const char16_t* makeWorldMapUnlockMessage(s32 logicalWorldId, bool isDecision) {
        if (!isValidTravelWorld(logicalWorldId))
            return nullptr;

        static sead::WFixedSafeString<0x100> sMessage;
        const char16_t* worldName = WorldMeta::getDisplayName16ForWorldId(logicalWorldId);
        sMessage = u"";

        if (isDecision) {
            sMessage.appendWithFormat(u"Set a course for %s!", worldName);
            return sMessage.cstr();
        }

        switch (logicalWorldId) {
        case WorldMeta::WorldId_Cap:
            sMessage.appendWithFormat(u"My home Kingdom! Let's go to %s!", worldName);
            break;
        case WorldMeta::WorldId_Cascade:
            sMessage.appendWithFormat(u"There's a beautiful waterfall in %s!", worldName);
            break;
        case WorldMeta::WorldId_Sand:
            sMessage.appendWithFormat(u"Remember to put some suncream before getting to %s!", worldName);
            break;
        case WorldMeta::WorldId_Wooded:
            sMessage.appendWithFormat(u"I hear %s has the perfect camping spot!", worldName);
            break;
        case WorldMeta::WorldId_Lake:
            sMessage.appendWithFormat(u"Isn't %s so relaxing?", worldName);
            break;
        case WorldMeta::WorldId_Cloud:
            sMessage.appendWithFormat(u"Let's take in the view from %s!", worldName);
            break;
        case WorldMeta::WorldId_Lost:
            sMessage.appendWithFormat(u"Hold tight, getting to %s might be a bumpy ride!", worldName);
            break;
        case WorldMeta::WorldId_Metro:
            sMessage.appendWithFormat(u"The city lights are calling from %s!", worldName);
            break;
        case WorldMeta::WorldId_Seaside:
            sMessage.appendWithFormat(u"%s is paradise, but we don't have time to relax!", worldName);
            break;
        case WorldMeta::WorldId_Snow:
            sMessage.appendWithFormat(u"It's going to be so cold in %s!", worldName);
            break;
        case WorldMeta::WorldId_Luncheon:
            sMessage.appendWithFormat(u"Something tasty is cooking in %s!", worldName);
            break;
        case WorldMeta::WorldId_Ruined:
            sMessage.appendWithFormat(u"A strange roar is coming from %s!", worldName);
            break;
        case WorldMeta::WorldId_Bowsers:
            sMessage.appendWithFormat(u"Let's storm the gates of %s!", worldName);
            break;
        case WorldMeta::WorldId_Moon:
            sMessage.appendWithFormat(u"Our journey rises toward %s!", worldName);
            break;
        case WorldMeta::WorldId_Mushroom:
            sMessage.appendWithFormat(u"A familiar road leads to %s!", worldName);
            break;
        case WorldMeta::WorldId_Dark:
            sMessage.appendWithFormat(u"A tougher challenge opens on %s!", worldName);
            break;
        case WorldMeta::WorldId_Darker:
            sMessage.appendWithFormat(u"The final challenge waits on %s!", worldName);
            break;
        default:
            sMessage.appendWithFormat(u"Let's move onto %s!", worldName);
            break;
        }
        return sMessage.cstr();
    }

    static s32 findFallbackWorldMapDisplayWorldId() {
        if (isValidTravelWorld(sWorldMapUnlockMessageWorldId))
            return sWorldMapUnlockMessageWorldId;
        for (s32 i = 0; i < sPendingLockedWorldCount; i++) {
            if (isValidTravelWorld(sPendingLockedWorlds[i]))
                return sPendingLockedWorlds[i];
        }
        return -1;
    }

    static s32 normalizeWorldMapDisplayWorldId(s32 gameWorldId) {
        if (isValidGameWorldId(gameWorldId))
            return gameWorldId;

        const s32 fallback = findFallbackWorldMapDisplayWorldId();
        return isValidGameWorldId(fallback) ? fallback : gameWorldId;
    }

    static bool isWorldMapPendingUnlockTarget(s32 gameWorldId) {
        const s32 logicalWorldId = gameWorldId;
        if (!isValidTravelWorld(logicalWorldId))
            return false;
        if (sWorldMapUnlockMessageWorldId == logicalWorldId)
            return true;
        for (s32 i = 0; i < sPendingLockedWorldCount; i++) {
            if (sPendingLockedWorlds[i] == logicalWorldId)
                return true;
        }
        return false;
    }

    using BranchPair = KingdomRando::BranchPair;

    static bool isProgressionRouteWorld(s32 logicalWorldId) {
        return KingdomRando::isRouteWorld(logicalWorldId);
    }

    static s32 getStartingProgressionWorldId() {
        const s32 startingWorldId = KingdomRando::getStartingWorldId();
        return isValidTravelWorld(startingWorldId) ? startingWorldId : WorldMeta::WorldId_Cap;
    }

    static bool isBranchTierIndex(s32 tierIndex) {
        const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
        return tier && tier->count == 2;
    }

    static bool isFirstBranchTierIndex(s32 tierIndex) {
        s32 branchIndex = 0;
        for (s32 i = 0; i < KingdomRando::getTierCount(); i++) {
            if (!isBranchTierIndex(i))
                continue;
            if (i == tierIndex)
                return branchIndex == 0;
            branchIndex++;
        }
        return false;
    }

    static BranchPair getBranchPairForTier(s32 tierIndex) {
        const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
        if (!tier || tier->count != 2)
            return { -1, -1 };
        return { tier->worlds[0], tier->worlds[1] };
    }

    static BranchPair getFirstBranch() {
        for (s32 i = 0; i < KingdomRando::getTierCount(); i++) {
            if (isBranchTierIndex(i))
                return getBranchPairForTier(i);
        }
        return { -1, -1 };
    }

    static BranchPair getSecondBranch() {
        bool foundFirst = false;
        for (s32 i = 0; i < KingdomRando::getTierCount(); i++) {
            if (!isBranchTierIndex(i))
                continue;
            if (foundFirst)
                return getBranchPairForTier(i);
            foundFirst = true;
        }
        return { -1, -1 };
    }

    static s32 getBranchSibling(s32 logicalWorldId, BranchPair branch) {
        if (logicalWorldId == branch.first)
            return branch.second;
        if (logicalWorldId == branch.second)
            return branch.first;
        return -1;
    }

    static bool isBossOnlyProgressionWorld(s32 logicalWorldId) {
        return logicalWorldId == WorldMeta::WorldId_Cloud
            && !Randomizer::isCloudKingdomMoonUseSelected(
                Randomizer::SpecialMoonUse_KingdomUnlock);
    }

    static bool isBossRequiredProgressionWorld(s32 logicalWorldId) {
        return logicalWorldId == WorldMeta::WorldId_Bowsers;
    }

    static bool isTerminalWorld(s32 logicalWorldId) {
        // These three sit past every other kingdom on the route, so they end
        // it. They are the same three the Kingdom Moons page refuses to put
        // Progress on.
        if (logicalWorldId == WorldMeta::WorldId_Mushroom
            || logicalWorldId == WorldMeta::WorldId_Dark
            || logicalWorldId == WorldMeta::WorldId_Darker)
            return true;

        if (logicalWorldId == WorldMeta::WorldId_Moon)
            return !Randomizer::isKingdomRandoEnabled() || !KingdomRando::isMoonInRandomOrder();

        return false;
    }

    static s32 getMoonRequirementForWorld(s32 logicalWorldId) {
        if (isBossOnlyProgressionWorld(logicalWorldId) || isTerminalWorld(logicalWorldId))
            return 0;
        return RequirementRando::getRequirement(logicalWorldId);
    }

    static s32 resolvePayShineIndex(const GameDataFile* file, s32 logicalWorldId) {
        (void)file;
        // Deposited moons live in mPayShineNum, which addPayShine indexes by the *current
        // world's* game/scenario WorldId (mPayShineNum[GameDataFile+0x9F0], the byaml "WorldId").
        // That slot is a fixed per-kingdom property (toGameWorldId), independent of which branch
        // sibling the player chose first. The old branch-order indexing (3/4, 7/8) read the wrong
        // sibling's slot once a branch was picked, which mis-computed branch completion — the core
        // progression bug this redesign fixes.
        return toGameWorldId(logicalWorldId);
    }

    static s32 getCollectedMoonsForProgression(const GameDataFile* file, s32 logicalWorldId) {
        if (!file || logicalWorldId < 0 || logicalWorldId >= WorldMeta::sWorldCount)
            return 0;

        // Moon Kingdom has its own native count logic (shine - payShine) rather than
        // just returning deposited pay-shines.
        if (logicalWorldId == WorldMeta::WorldId_Moon) {
            const s32 collected = file->getShineNum(logicalWorldId) - file->getPayShineNum(logicalWorldId);
            return collected > 0 ? collected : 0;
        }

        // Shine/pay-shine save arrays are indexed dynamically by the player's globe choices.
        // The first chosen branch takes the lower index, the second takes the higher.
        const s32 payShineIndex = resolvePayShineIndex(file, logicalWorldId);
        const s32 paid = file->getPayShineNum(payShineIndex);
        return paid > 0 ? paid : 0;
    }

    static bool setWorldScenarioNoAtLeast(GameDataFile* file, s32 logicalWorldId, s32 minScenarioNo) {
        const s32 gameWorldId = toGameWorldId(logicalWorldId);
        if (!file || !isValidGameWorldId(gameWorldId))
            return false;

        auto scenarioNo = file->getScenarioNumArr();
        if (!scenarioNo.begin() || scenarioNo[gameWorldId] >= minScenarioNo)
            return false;

        scenarioNo[gameWorldId] = minScenarioNo;
        return true;
    }

    static bool setWorldMainScenarioNoAtLeast(GameDataFile* file, s32 logicalWorldId, s32 minScenarioNo) {
        const s32 gameWorldId = toGameWorldId(logicalWorldId);
        if (!file || !isValidGameWorldId(gameWorldId))
            return false;

        auto mainScenarioNo = file->getMainScenarioNumArr();
        if (!mainScenarioNo.begin() || mainScenarioNo[gameWorldId] >= minScenarioNo)
            return false;

        mainScenarioNo[gameWorldId] = minScenarioNo;
        return true;
    }

    static void normalizeCloudBossScenario(GameDataFile* file) {
        const s32 cloudGameWorldId = toGameWorldId(WorldMeta::WorldId_Cloud);
        if (!file || !isValidGameWorldId(cloudGameWorldId))
            return;

        // Vanilla Cloud boss clear lands on scenario 2. Store it as scenario 3 so
        // the randomizer has a stable "Bowser beaten" completion marker. This
        // decision must be derived exclusively from this save's scenario data:
        // GameProgressData instances are reused when switching save slots.
        if (file->getScenarioNo(cloudGameWorldId) < 2)
            return;

        setWorldScenarioNoAtLeast(file, WorldMeta::WorldId_Cloud, 3);
        setWorldMainScenarioNoAtLeast(file, WorldMeta::WorldId_Cloud, 3);
    }

    static bool isBossProgressComplete(const GameDataFile* file, s32 logicalWorldId) {
        if (!file)
            return false;
        const s32 gameWorldId = toGameWorldId(logicalWorldId);
        if (!isValidGameWorldId(gameWorldId))
            return false;
        if (logicalWorldId == WorldMeta::WorldId_Bowsers) {
            const s32 scenario = file->getScenarioNo(gameWorldId);
            return scenario == 3 || scenario == 4;
        }
        return file->getScenarioNo(gameWorldId) > 1;
    }

    static bool isProgressionWorldCurrent(const GameDataFile* file, s32 logicalWorldId) {
        return file && toLogicalWorldId(file->getCurrentWorldId()) == logicalWorldId;
    }

    static bool isProgressionWorldVisited(const GameDataFile* file, s32 logicalWorldId) {
        if (isProgressionWorldCurrent(file, logicalWorldId))
            return true;

        const GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
        const s32 gameWorldId = toGameWorldId(logicalWorldId);
        return progress && progress->mIsFirstTimeWorld && isValidGameWorldId(gameWorldId) && !progress->mIsFirstTimeWorld[gameWorldId];
    }

    static bool isProgressionWorldComplete(const GameDataFile* file, s32 logicalWorldId) {
        if (!file || !isValidTravelWorld(logicalWorldId))
            return false;
        const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(const_cast<GameDataFile*>(file), toGameWorldId(logicalWorldId));
        return entry && entry->completeStatus;
    }

    static bool hasAnyProgressionActivity(const GameDataFile* file) {
        if (!file)
            return false;

        for (s32 logicalWorldId = 0; logicalWorldId < WorldMeta::sWorldCount; logicalWorldId++) {
            const s32 gameWorldId = toGameWorldId(logicalWorldId);
            if (!isValidGameWorldId(gameWorldId))
                continue;
            // pay-shine and scenario are both indexed by the game/scenario WorldId.
            if (file->getPayShineNum(gameWorldId) > 0)
                return true;
            if (logicalWorldId == getStartingProgressionWorldId() || isTerminalWorld(logicalWorldId))
                continue;
            if (file->getScenarioNo(gameWorldId) > 1)
                return true;
        }
        return false;
    }

    static bool shouldForceStartWorldOnly(const GameDataFile* file) {
        return file && !file->customIsExistInHackDictionary(kProgressionStartedFlag) && !hasAnyProgressionActivity(file);
    }

    static bool isAuditProfile(const GameDataFile* file) {
        return file && file->customIsExistInHackDictionary(kAuditProfileMarker);
    }

    static bool isProgressionWorldUnlocked(const GameProgressData* progress, s32 logicalWorldId) {
        return progress && progress->mIsUnlockWorld && isValidTravelWorld(logicalWorldId)
            && progress->mIsUnlockWorld[logicalWorldId];
    }

    static bool isBranchPairComplete(const GameDataFile* file, BranchPair branch) {
        return isProgressionWorldComplete(file, branch.first) && isProgressionWorldComplete(file, branch.second);
    }

    static bool isWorldInMask(u32 mask, s32 logicalWorldId) {
        return logicalWorldId >= 0 && logicalWorldId < 32 && (mask & (1u << logicalWorldId)) != 0;
    }

    // The branch sibling the player chose is whichever side is progression-unlocked. With the
    // set-based unlock model mIsUnlockWorld is an exact, stable mirror of the persisted unlock
    // set (re-rendered every sync, never inferred from), so membership is authoritative here.
    // Returns the chosen side when exactly one is unlocked; when both are unlocked the choice is
    // moot for logic, so fall back to the lower game/scenario slot purely for cosmetic map order.
    static s32 deriveBranchChoice(const GameProgressData* progress, BranchPair branch) {
        if (!progress || branch.first < 0 || branch.second < 0)
            return -1;
        const bool firstUnlocked = isProgressionWorldUnlocked(progress, branch.first);
        const bool secondUnlocked = isProgressionWorldUnlocked(progress, branch.second);
        if (firstUnlocked && !secondUnlocked)
            return branch.first;
        if (secondUnlocked && !firstUnlocked)
            return branch.second;
        if (firstUnlocked && secondUnlocked)
            return toGameWorldId(branch.first) <= toGameWorldId(branch.second) ? branch.first : branch.second;
        return -1;
    }

    // Same derivation as deriveBranchChoice but straight off the unlock mask. Used inside
    // syncProgressionWorlds, where the mIsUnlockWorld mirror is transiently cleared while the
    // world-map arrays are rebuilt, so membership must be read from the mask instead.
    static s32 branchChoiceFromMask(u32 mask, BranchPair branch) {
        if (branch.first < 0 || branch.second < 0)
            return -1;
        const bool firstUnlocked = isWorldInMask(mask, branch.first);
        const bool secondUnlocked = isWorldInMask(mask, branch.second);
        if (firstUnlocked && !secondUnlocked)
            return branch.first;
        if (secondUnlocked && !firstUnlocked)
            return branch.second;
        if (firstUnlocked && secondUnlocked)
            return toGameWorldId(branch.first) <= toGameWorldId(branch.second) ? branch.first : branch.second;
        return -1;
    }

    // Render the cosmetic vanilla branch-status enums from the unlock mask. These are vestigial
    // (we overwrite the world-map arrays directly) but kept consistent for any vanilla code that
    // still reads them. Mapping mirrors the vanilla enum semantics (Forest/Lake, Sea/Snow).
    static void renderBranchEnums(GameProgressData* progress, u32 mask) {
        if (!progress)
            return;

        const BranchPair fb = getFirstBranch();
        const s32 fc = branchChoiceFromMask(mask, fb);
        if (fc < 0)
            progress->mUnlockWorldStatusFirstBranch = GameProgressData::FirstBranch::None;
        else if (Randomizer::isKingdomRandoEnabled())
            progress->mUnlockWorldStatusFirstBranch = (fc == fb.first) ? GameProgressData::FirstBranch::Forest : GameProgressData::FirstBranch::Lake;
        else
            progress->mUnlockWorldStatusFirstBranch = (fc == WorldMeta::WorldId_Wooded) ? GameProgressData::FirstBranch::Forest : GameProgressData::FirstBranch::Lake;

        const BranchPair sb = getSecondBranch();
        const s32 sc = branchChoiceFromMask(mask, sb);
        if (sc < 0)
            progress->mUnlockWorldStatusSecondBranch = GameProgressData::SecondBranch::None;
        else if (Randomizer::isKingdomRandoEnabled())
            progress->mUnlockWorldStatusSecondBranch = (sc == sb.first) ? GameProgressData::SecondBranch::Sea : GameProgressData::SecondBranch::Snow;
        else
            progress->mUnlockWorldStatusSecondBranch = (sc == WorldMeta::WorldId_Seaside) ? GameProgressData::SecondBranch::Sea : GameProgressData::SecondBranch::Snow;
    }

    static void requestSave(GameDataFile* file) {
        if (!file || !file->getGameDataHolder())
            return;
        GameDataFunction::setRequireSave(GameDataHolderWriter(file->getGameDataHolder()));
    }

    void persistSeed(GameDataHolderWriter writer, u32 seed) {
        if (!writer.mData)
            return;

        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (!file)
            return;

        u32 savedSeed = 0;
        const bool hasSavedSeed = file->customGetRandoSeed(&savedSeed);
        if (hasSavedSeed && savedSeed == seed)
            return;

        file->customSetRandoSeed(seed);
        Randomizer::logPush("seed persist file=%llx old=%u new=%u",
            (unsigned long long)(uintptr_t)file, hasSavedSeed ? savedSeed : 0, seed);
        requestSave(file);
    }

    static u32 startWorldBit() {
        return 1u << getStartingProgressionWorldId();
    }

    // Legacy helpers retained only for the unreachable pre-list renderer below.
    static u32 loadUnlockSet(GameDataFile* file) {
        (void)file;
        return 0;
    }

    static void storeUnlockSet(GameDataFile* file, u32 mask) {
        (void)file;
        (void)mask;
    }

    // Render the unlock set into the vanilla per-world unlock bits + unlocked count. These are
    // OUTPUTS only: every sync overwrites them from the set, so vanilla globe/warp code writing
    // mIsUnlockWorld can never corrupt our state (it is re-derived, not read back as authority).
    static void renderMaskToUnlockWorld(GameDataFile* file, u32 mask) {
        if (!file)
            return;
        GameProgressData* progress = file->getGameProgressData();
        if (!progress || !progress->mIsUnlockWorld)
            return;

        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            progress->mIsUnlockWorld[i] = false;

        s32 count = 0;
        for (s32 logicalWorldId = 0; logicalWorldId < WorldMeta::sWorldCount; logicalWorldId++) {
            if (!isWorldInMask(mask, logicalWorldId) || !isProgressionRouteWorld(logicalWorldId))
                continue;
            const s32 gameWorldId = toGameWorldId(logicalWorldId);
            if (!isValidGameWorldId(gameWorldId))
                continue;
            progress->mIsUnlockWorld[gameWorldId] = true;
            count++;
        }
        progress->mUnlockWorldNum = count;
    }

    static void calcAccessibleWorlds(bool* accessible, const GameDataFile* file) {
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            accessible[i] = false;

        const GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
        accessible[getStartingProgressionWorldId()] = true;
        if (!progress || !progress->mIsUnlockWorld || shouldForceStartWorldOnly(file))
            return;

        for (s32 logicalWorldId = 0; logicalWorldId < WorldMeta::sWorldCount; logicalWorldId++) {
            if (!isProgressionRouteWorld(logicalWorldId))
                continue;
            const s32 gameWorldId = toGameWorldId(logicalWorldId);
            if (isValidGameWorldId(gameWorldId) && progress->mIsUnlockWorld[gameWorldId])
                accessible[logicalWorldId] = true;
        }
    }

    static void getOrderedBranchWorlds(const GameProgressData* progress, BranchPair branch, bool isFirstBranch,
        s32& first, s32& second) {
        (void)isFirstBranch;
        first = branch.first;
        second = branch.second;

        if (!progress)
            return;

        // Put the chosen sibling first (cosmetic map order). The choice is derived from the
        // stable unlock mirror, not a separately-stored enum.
        if (deriveBranchChoice(progress, branch) == branch.second) {
            first = branch.second;
            second = branch.first;
        }
    }

    // Walk the KingdomRando route in tier order, ordering each branch pair by the chosen sibling.
    // Used only by display/warp helpers (e.g. getNextStageName) — it reads the rendered unlock
    // mirror and is not the unlock authority.
    static s32 buildOrderedRoute(const GameProgressData* progress, s32* outRoute) {
        s32 count = 0;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier)
                continue;

            if (tier->count == 2) {
                s32 first = -1;
                s32 second = -1;
                getOrderedBranchWorlds(progress, getBranchPairForTier(tierIndex), isFirstBranchTierIndex(tierIndex), first, second);
                if (isValidTravelWorld(first) && count < sNumWorlds)
                    outRoute[count++] = first;
                if (isValidTravelWorld(second) && count < sNumWorlds)
                    outRoute[count++] = second;
            } else {
                for (s32 i = 0; i < tier->count && count < sNumWorlds; i++)
                    outRoute[count++] = tier->worlds[i];
            }
        }
        return count;
    }

    static s32 getLastAccessibleProgressionWorld(const GameDataFile* file) {
        if (!file)
            return -1;

        bool accessible[WorldMeta::sWorldCount];
        calcAccessibleWorlds(accessible, file);
        const GameProgressData* progress = file->getGameProgressData();
        s32 last = -1;

        auto considerWorld = [&](s32 logicalWorldId) {
            if (isValidTravelWorld(logicalWorldId) && accessible[logicalWorldId])
                last = logicalWorldId;
        };

        auto considerBranchWorlds = [&](BranchPair branch, bool isFirstBranch) {
            s32 first = -1;
            s32 second = -1;
            getOrderedBranchWorlds(progress, branch, isFirstBranch, first, second);
            considerWorld(first);
            considerWorld(second);
        };

        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier)
                continue;
            if (tier->count == 2)
                considerBranchWorlds(getBranchPairForTier(tierIndex), isFirstBranchTierIndex(tierIndex));
            else if (tier->count == 1)
                considerWorld(tier->worlds[0]);
        }
        return last;
    }

    static bool pushProgressionUnlockCandidate(s32* candidates, s32 maxCandidates, s32& count,
        const GameProgressData* progress, s32 logicalWorldId) {
        if (!candidates || count >= maxCandidates || !isValidTravelWorld(logicalWorldId) || isProgressionWorldUnlocked(progress, logicalWorldId))
            return false;

        for (s32 i = 0; i < count; i++) {
            if (candidates[i] == logicalWorldId)
                return false;
        }

        candidates[count++] = logicalWorldId;
        return true;
    }

    static s32 calcNextProgressionUnlockCandidates(const GameDataFile* file, s32* candidates, s32 maxCandidates) {
        if (!file || !candidates || maxCandidates <= 0)
            return 0;

        for (s32 i = 0; i < maxCandidates; i++)
            candidates[i] = -1;

        s32 nativeCandidates[2] = { -1, -1 };
        const s32 count = KingdomProgress::getFrontierUnlockCandidates(
            const_cast<GameDataFile*>(file), nativeCandidates, maxCandidates < 2 ? maxCandidates : 2);
        for (s32 i = 0; i < count; i++)
            candidates[i] = toLogicalWorldId(nativeCandidates[i]);
        return count;
    }

    static s32 inferNextProgressionUnlockCandidatesFromProgress(const GameProgressData* progress, s32* candidates,
        s32 maxCandidates) {
        if (!progress || !progress->mIsUnlockWorld || !candidates || maxCandidates <= 0)
            return 0;

        for (s32 i = 0; i < maxCandidates; i++)
            candidates[i] = -1;

        if (!isProgressionWorldUnlocked(progress, getStartingProgressionWorldId()))
            return 0;

        s32 count = 0;
        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier)
                continue;

            if (tier->count == 1) {
                if (!isProgressionWorldUnlocked(progress, tier->worlds[0]))
                    return pushProgressionUnlockCandidate(candidates, maxCandidates, count, progress, tier->worlds[0])
                        ? count
                        : 0;
                continue;
            }

            if (tier->count != 2)
                continue;

            const bool firstUnlocked = isProgressionWorldUnlocked(progress, tier->worlds[0]);
            const bool secondUnlocked = isProgressionWorldUnlocked(progress, tier->worlds[1]);
            if (!firstUnlocked && !secondUnlocked) {
                pushProgressionUnlockCandidate(candidates, maxCandidates, count, progress, tier->worlds[0]);
                pushProgressionUnlockCandidate(candidates, maxCandidates, count, progress, tier->worlds[1]);
                return count;
            }
            if (firstUnlocked != secondUnlocked)
                return pushProgressionUnlockCandidate(candidates, maxCandidates, count, progress,
                           firstUnlocked ? tier->worlds[1] : tier->worlds[0])
                    ? count
                    : 0;
        }

        return 0;
    }

    static bool shouldPlayNoShineProgressionDemo(const GameDataFile* file) {
        return file && file->getGameProgressData() == sPendingLockedProgress
            && sPendingLockedWorldCount > 0;
    }

    static void clearProgressionUnlockDemoWorlds() {
        sProgressionUnlockDemoWorldCount = 0;
        for (s32 i = 0; i < 2; i++)
            sProgressionUnlockDemoWorlds[i] = -1;
    }

    static void beginProgressionUnlockDemo(GameDataFile* file) {
        Randomizer::logPush("UNLOCK DEMO begin file=%llx depth=%d pending=%d [%d,%d]",
            (unsigned long long)(uintptr_t)file, sProgressionUnlockDemoDepth,
            sPendingLockedWorldCount, sPendingLockedWorlds[0],
            sPendingLockedWorlds[1]);
        if (sProgressionUnlockDemoDepth++ != 0)
            return;

        clearProgressionUnlockDemoWorlds();
        if (file && file->getGameProgressData() == sPendingLockedProgress
            && sPendingLockedWorldCount > 0) {
            sProgressionUnlockDemoWorldCount = sPendingLockedWorldCount;
            for (s32 i = 0; i < sPendingLockedWorldCount; i++)
                sProgressionUnlockDemoWorlds[i] = sPendingLockedWorlds[i];
        }

        GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
        if (progress && sProgressionUnlockDemoWorldCount > 0) {
            sPendingLockedProgress = nullptr;
            sPendingLockedWorldCount = 0;
            sWorldMapUnlockMessageWorldId = -1;
            for (s32 i = 0; i < 2; i++)
                sPendingLockedWorlds[i] = -1;
            sPendingLockedProgress = progress;
            sPendingLockedWorldCount = sProgressionUnlockDemoWorldCount;
            for (s32 i = 0; i < sProgressionUnlockDemoWorldCount; i++)
                sPendingLockedWorlds[i] = sProgressionUnlockDemoWorlds[i];
            sWorldMapUnlockMessageWorldId = sPendingLockedWorlds[0];
        }
    }

    static void endProgressionUnlockDemo() {
        if (sProgressionUnlockDemoDepth <= 0)
            return;
        if (--sProgressionUnlockDemoDepth == 0) {
            Randomizer::logPush("UNLOCK DEMO end worlds=%d [%d,%d]",
                sProgressionUnlockDemoWorldCount,
                sProgressionUnlockDemoWorlds[0],
                sProgressionUnlockDemoWorlds[1]);
            clearProgressionUnlockDemoWorlds();
            clearPendingLockedProgressionWorlds();
        }
    }

    static bool isProgressionUnlockDemoWorld(s32 gameWorldId) {
        if (sProgressionUnlockDemoDepth <= 0)
            return false;

        const s32 logicalWorldId = gameWorldId;
        for (s32 i = 0; i < sProgressionUnlockDemoWorldCount; i++) {
            if (sProgressionUnlockDemoWorlds[i] == logicalWorldId)
                return true;
        }
        return false;
    }

    class ScopedProgressionUnlockDemo {
    public:
        ScopedProgressionUnlockDemo(GameDataFile* file) { beginProgressionUnlockDemo(file); }
        ~ScopedProgressionUnlockDemo() { endProgressionUnlockDemo(); }
    };

    static void clearPendingLockedProgressionWorlds() {
        if (sPendingLockedWorldCount > 0 || sPendingLockedProgress) {
            Randomizer::logPush("PENDING clear progress=%llx count=%d [%d,%d] msg=%d",
                (unsigned long long)(uintptr_t)sPendingLockedProgress,
                sPendingLockedWorldCount, sPendingLockedWorlds[0],
                sPendingLockedWorlds[1], sWorldMapUnlockMessageWorldId);
        }
        sPendingLockedProgress = nullptr;
        sPendingLockedWorldCount = 0;
        sWorldMapUnlockMessageWorldId = -1;
        for (s32 i = 0; i < 2; i++)
            sPendingLockedWorlds[i] = -1;
    }

    static bool isPendingLockedProgressionWorld(s32 logicalWorldId) {
        if (!isValidTravelWorld(logicalWorldId))
            return false;

        for (s32 i = 0; i < sPendingLockedWorldCount; i++) {
            if (sPendingLockedWorlds[i] == logicalWorldId)
                return true;
        }
        return false;
    }

    static void stageAuthoritativeUnlockPresentation(GameDataFile* file) {
        clearPendingLockedProgressionWorlds();
        if (!file)
            return;

        s32 nativeCandidates[2] = { -1, -1 };
        const s32 count = KingdomProgress::getFrontierUnlockCandidates(
            file, nativeCandidates, 2);
        if (count <= 0)
            return;

        sPendingLockedProgress = file->getGameProgressData();
        sPendingLockedWorldCount = count;
        for (s32 i = 0; i < count; i++)
            sPendingLockedWorlds[i] = toLogicalWorldId(nativeCandidates[i]);
        sWorldMapUnlockMessageWorldId = sPendingLockedWorlds[0];
        Randomizer::logPush(
            "PENDING stage curN=%d frontier=%d count=%d native=[%d,%d] logical=[%d,%d]",
            file->getCurrentWorldId(), KingdomProgress::getFirstIncompleteIndex(file),
            count, nativeCandidates[0], nativeCandidates[1],
            sPendingLockedWorlds[0], sPendingLockedWorlds[1]);
    }

    static bool tryCompleteAcceptedGlobe(GameDataFile* file) {
        if (!file || file != sGlobeCompletionFile
            || file->getCurrentWorldId() != sGlobeCompletionWorldId) {
            Randomizer::logPush(
                "GLOBE complete reject file=%llx armed=%llx curN=%d armedN=%d",
                (unsigned long long)(uintptr_t)file,
                (unsigned long long)(uintptr_t)sGlobeCompletionFile,
                file ? file->getCurrentWorldId() : -1,
                sGlobeCompletionWorldId);
            return false;
        }

        normalizeCloudBossScenario(file);
        if (!KingdomProgress::tryCompleteFrontier(file, sGlobeCompletionWorldId)) {
            Randomizer::logPush("GLOBE complete pending requirements curN=%d",
                sGlobeCompletionWorldId);
            return false;
        }

        // World Peace Requirement: On Kingdom Complete - this fires exactly once,
        // tied to the actual moon-deposit action that commits the frontier kingdom
        // as complete (unlike a raw stage-transition hook, which fires on every
        // intermediate stage hop during travel and can't be scoped to a single
        // kingdom). Mark the kingdom being deposited FROM (not the destination)
        // forced-peace, so its scenario floors to post-game going forward.
        if (Randomizer::getWorldPeaceRequirementMode() == Randomizer::WorldPeaceRequirementMode_OnKingdomComplete) {
            Randomizer::setKingdomForcedPeace(sGlobeCompletionWorldId, true);
            Bingo::recordWorldPeace(sGlobeCompletionWorldId);
            Randomizer::logPush("WORLDPEACE: marked world=%d forced-peace via deposit-complete, mask now=%08x",
                sGlobeCompletionWorldId, Randomizer::getForcedPeaceKingdoms());
        }

        Randomizer::logPush("progress complete world=%d frontier=%d",
            sGlobeCompletionWorldId, KingdomProgress::getFirstIncompleteIndex(file));
        sGlobeCompletionFile = nullptr;
        sGlobeCompletionWorldId = -1;
        stageAuthoritativeUnlockPresentation(file);
        syncProgressionWorlds(file);
        KingdomProgress::debugLogState(file, "globe-complete");
        return true;
    }

    // Commit a progression unlock into the authoritative unlock set.
    //
    // Current behavior: a requested world (a branch sibling picked by the world map's
    // unlock animation) is committed via KingdomProgress::commitBranchChoice, which persists
    // the player's choice so independent branch siblings can never clobber each other. With
    // no requested world (moon deposits, travel-facade setup, start) this is a no-op — the
    // dead code below (mask-based frontier validation/rendering) predates
    // KingdomProgress::commitBranchChoice/renderAuthoritativeProgression and no longer runs.
    static bool advanceProgressionUnlocks(GameDataFile* file, s32 requestedLogicalWorldId = -1) {
        if (!file)
            return false;

        normalizeCloudBossScenario(file);
        // Multiplayer deliberately does NOT hook this, or any other individual
        // progression mutation. Client::syncKingdomProgress diffs
        // KingdomProgress::captureSyncState from update() instead, so the
        // branch choice made here, the frontier advance in
        // tryCompleteAcceptedGlobe and the forced-peace flip beside it are all
        // covered without any of them having to remember to publish - and so
        // that a future mutation site cannot silently go unsynced.
        if (isValidTravelWorld(requestedLogicalWorldId))
            return KingdomProgress::commitBranchChoice(file, toGameWorldId(requestedLogicalWorldId));
        return false;

        GameProgressData* progress = file->getGameProgressData();
        if (!progress || !progress->mIsUnlockWorld)
            return false;

        const u32 loadedMask = loadUnlockSet(file);
        u32 mask = loadedMask | startWorldBit();

        // Render the current set so the frontier check below sees an accurate unlock state
        // (calcNextProgressionUnlockCandidates reads the rendered mIsUnlockWorld mirror).
        renderMaskToUnlockWorld(file, mask);

        if (isValidTravelWorld(requestedLogicalWorldId) && !isWorldInMask(mask, requestedLogicalWorldId)) {
            // Honor the requested unlock when it is the next frontier. Accept it if it passes the
            // strict current-world-complete gate OR if it is the world the map has already revealed
            // as next (the tier frontier the unlock animation showed) — the two can disagree by a
            // frame at the globe, and the cosmetic reveal must never out-run the committed set.
            s32 candidates[2] = { -1, -1 };
            const s32 strictCount = calcNextProgressionUnlockCandidates(file, candidates, 2);
            bool accept = false;
            for (s32 i = 0; i < strictCount; i++)
                accept = accept || candidates[i] == requestedLogicalWorldId;
            if (!accept) {
                s32 revealed[2] = { -1, -1 };
                const s32 revealedCount = inferNextProgressionUnlockCandidatesFromProgress(progress, revealed, 2);
                for (s32 i = 0; i < revealedCount; i++)
                    accept = accept || revealed[i] == requestedLogicalWorldId;
            }
            if (accept) {
                mask |= 1u << requestedLogicalWorldId;
                file->customAddHackDictionary(kProgressionStartedFlag);
            }
        }

        // Pin to the start world only when no progression has legitimately begun (brand-new save
        // with no requested unlock). A committed first unlock has already set the started flag, so
        // this is correctly skipped and the first Cap -> Cascade unlock is never discarded.
        if (shouldForceStartWorldOnly(file))
            mask = startWorldBit();

        const bool changed = mask != loadedMask;
        if (changed)
            storeUnlockSet(file, mask);
        renderMaskToUnlockWorld(file, mask);
        return changed;
    }

    static bool isProgressionWorldAccessible(const GameDataFile* file, s32 logicalWorldId) {
        if (!file || !isValidTravelWorld(logicalWorldId))
            return false;
        if (isAuditProfile(file))
            return true;
        if (file->getGameProgressData() == sPendingLockedProgress
            && isPendingLockedProgressionWorld(logicalWorldId)) {
            return false;
        }
        return KingdomProgress::isVisible(
            const_cast<GameDataFile*>(file), toGameWorldId(logicalWorldId));
    }

    static bool isAlreadyGoAccessibleWorld(const GameProgressData* progress, s32 gameWorldId) {
        (void)progress;
        return isValidGameWorldId(gameWorldId);
    }

    static void renderAuthoritativeProgression(GameDataFile* file) {
        const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
        if (!file || !list)
            return;

        GameProgressData* progress = file->getGameProgressData();

        // The audit baseline deliberately contains all 17 kingdoms, while the
        // normal custom progression record only has the 14 route entries. Keep
        // the native map/unlock mirrors in that synthetic postgame shape on
        // every progression sync so a stage reload or reboot cannot collapse
        // the three endgame kingdoms back out of the saved profile.
        if (isAuditProfile(file)) {
            uintptr_t base = reinterpret_cast<uintptr_t>(file);
            s32* unlockedWorldId = *reinterpret_cast<s32**>(base + 0x5f0);
            s32* worldMapIndex = *reinterpret_cast<s32**>(base + 0xb40);
            s32* worldWarpIndex = *reinterpret_cast<s32**>(base + 0xb50);
            for (s32 i = 0; i < sNumWorlds; i++) {
                if (unlockedWorldId)
                    unlockedWorldId[i] = i < WorldMeta::sWorldCount ? i : -1;
                if (worldMapIndex)
                    worldMapIndex[i] = i < WorldMeta::sWorldCount ? i : -1;
                if (worldWarpIndex)
                    worldWarpIndex[i] = i < WorldMeta::sWorldCount ? i : -1;
                if (progress && i < WorldMeta::sWorldCount) {
                    if (progress->mWorldIdForWorldMap)
                        progress->mWorldIdForWorldMap[i] = i;
                    if (progress->mWorldIdForWorldWarpHole)
                        progress->mWorldIdForWorldWarpHole[i] = i;
                    if (progress->mWorldIdForShineList)
                        progress->mWorldIdForShineList[i] = i;
                    if (progress->mIsUnlockWorld)
                        progress->mIsUnlockWorld[i] = true;
                    if (progress->mIsFirstTimeWorld)
                        progress->mIsFirstTimeWorld[i] = false;
                }
            }
            *reinterpret_cast<s32*>(base + 0xb48) = WorldMeta::sWorldCount;
            *reinterpret_cast<s32*>(base + 0xb58) = WorldMeta::sWorldCount;
            *reinterpret_cast<s32*>(base + 0xb5c) = WorldMeta::sWorldCount;
            if (progress) {
                progress->mUnlockWorldNum = WorldMeta::sWorldCount;
                progress->mUnlockWorldStatusFirstBranch = GameProgressData::FirstBranch::Forest;
                progress->mUnlockWorldStatusSecondBranch = GameProgressData::SecondBranch::Sea;
                progress->mWaterfallWorldProgress = GameProgressData::WaterfallWorldProgress::TalkedCapNearHome;
            }
            return;
        }

        const s32 trueVisibleCount = KingdomProgress::getVisibleCount(file);
        // The frontier is a locked animation candidate until this presentation
        // transaction is consumed. Keep it out of all persistent vanilla mirrors
        // so it cannot appear once before being unlocked a second time.
        const s32 visibleCount = getBaseUnlockWorldCount(file);
        uintptr_t base = reinterpret_cast<uintptr_t>(file);
        s32* unlockedWorldId = *reinterpret_cast<s32**>(base + 0x5f0);
        s32* worldMapIndex = *reinterpret_cast<s32**>(base + 0xb40);
        s32* worldWarpIndex = *reinterpret_cast<s32**>(base + 0xb50);

        for (s32 i = 0; i < sNumWorlds; i++) {
            if (unlockedWorldId)
                unlockedWorldId[i] = -1;
            if (worldMapIndex)
                worldMapIndex[i] = -1;
            if (worldWarpIndex)
                worldWarpIndex[i] = -1;
            if (progress) {
                if (progress->mWorldIdForWorldMap)
                    progress->mWorldIdForWorldMap[i] = -1;
                if (progress->mWorldIdForWorldWarpHole)
                    progress->mWorldIdForWorldWarpHole[i] = -1;
                if (progress->mIsUnlockWorld)
                    progress->mIsUnlockWorld[i] = false;
            }
        }

        const auto writeMapEntry = [&](s32 index, s32 logicalWorldId, bool markUnlocked) {
            if (unlockedWorldId)
                unlockedWorldId[index] = logicalWorldId;
            if (worldMapIndex)
                worldMapIndex[index] = logicalWorldId;
            if (worldWarpIndex)
                worldWarpIndex[index] = logicalWorldId;
            if (progress) {
                if (progress->mWorldIdForWorldMap)
                    progress->mWorldIdForWorldMap[index] = logicalWorldId;
                if (progress->mWorldIdForWorldWarpHole)
                    progress->mWorldIdForWorldWarpHole[index] = logicalWorldId;
                if (markUnlocked && progress->mIsUnlockWorld && isValidTravelWorld(logicalWorldId))
                    progress->mIsUnlockWorld[logicalWorldId] = true;
            }
        };

        s32 count = 0;
        for (s32 i = 0; i < visibleCount; i++)
            writeMapEntry(count++, list->entries[i].kingdomId, true);

        const bool isUnlockPresentation = progress && progress == sPendingLockedProgress
            && sPendingLockedWorldCount > 0;
        if (Randomizer::isVisitedKingdomsMapEnabled() && !isUnlockPresentation) {
            // QoL: append route kingdoms the player has physically visited early
            // (e.g. via Entrance/Kingdom Rando), plus the three optional endgame
            // kingdoms, after the current progression frontier. Keep them in the
            // order first visited so they stay travel-able from the map. These
            // deliberately do NOT set mIsUnlockWorld (read
            // by isProgressionWorldUnlocked/branch-choice derivation below) —
            // that per-kingdom flag is the one thing that actually gates
            // progression, so leaving it false keeps this a pure display
            // convenience.
            // The various *Num/*Index count fields below ARE extended to include
            // them, since those are what the World Map screen itself reads to
            // know how many icons to build/show — completion can still only ever
            // be committed for the true frontier (KingdomProgress::tryCompleteFrontier
            // matches on kingdomId directly, not on any of these counts).
            s32 extra[WorldMeta::sWorldCount];
            s32 extraCount = 0;
            const u32 visited = Randomizer::getVisitedKingdoms();
            // Scan from trueVisibleCount, not the possibly pending-lock-reduced
            // visibleCount, so the kingdom currently held back for its unlock
            // reveal is never misclassified as an "extra" (red) entry.
            for (s32 i = trueVisibleCount; i < KingdomProgress::cKingdomCount; i++) {
                const s32 logicalWorldId = list->entries[i].kingdomId;
                if (logicalWorldId >= 0 && logicalWorldId < 32 && (visited & (1u << logicalWorldId)) != 0)
                    extra[extraCount++] = logicalWorldId;
            }

            // Mushroom Kingdom, Dark Side, and Darker Side are intentionally
            // outside KingdomProgress's 14-world route. Still add them when
            // they have been physically visited so the Visited Kingdoms Map
            // option treats every kingdom consistently.
            static constexpr s32 sOptionalEndgameWorlds[] = {
                WorldMeta::WorldId_Mushroom,
                WorldMeta::WorldId_Dark,
                WorldMeta::WorldId_Darker,
            };
            for (const s32 logicalWorldId : sOptionalEndgameWorlds) {
                if ((visited & (1u << logicalWorldId)) != 0)
                    extra[extraCount++] = logicalWorldId;
            }

            // Stable insertion sort by visit order; ties (including saves written
            // before order-tracking existed, which all read back as rank 0) keep
            // their original route-order position.
            for (s32 i = 1; i < extraCount; i++) {
                const s32 worldId = extra[i];
                const s8 order = Randomizer::getKingdomVisitOrder(worldId);
                s32 j = i - 1;
                while (j >= 0 && Randomizer::getKingdomVisitOrder(extra[j]) > order) {
                    extra[j + 1] = extra[j];
                    j--;
                }
                extra[j + 1] = worldId;
            }

            for (s32 i = 0; i < extraCount && count < sNumWorlds; i++)
                writeMapEntry(count++, extra[i], false);
        } else if (Randomizer::isVisitedKingdomsMapEnabled() && isUnlockPresentation) {
            // Keep the unlock-next-kingdom presentation limited to the real
            // progression route. The normal map is rebuilt with visited extras
            // as soon as the pending unlock transaction is committed.
            // The world map rebuilds every frame while it is open, so this only
            // reports when the pending count actually moves - otherwise one
            // unlock presentation buries the log in hundreds of identical lines.
            static s32 sLastLoggedHiddenPending = -1;
            if (sLastLoggedHiddenPending != sPendingLockedWorldCount) {
                sLastLoggedHiddenPending = sPendingLockedWorldCount;
                Randomizer::logPush(
                    "WM VISITED hidden during unlock presentation pending=%d",
                    sPendingLockedWorldCount);
            }
        }

        *reinterpret_cast<s32*>(base + 0xb48) = count;
        *reinterpret_cast<s32*>(base + 0xb58) = count;
        *reinterpret_cast<s32*>(base + 0xb5c) = count;
        if (progress) {
            progress->mUnlockWorldNum = count;
            progress->mUnlockWorldStatusFirstBranch = GameProgressData::FirstBranch::None;
            if (list->resolvedBranches & 1) {
                const s32 selected = list->entries[3].kingdomId;
                const KingdomRando::RouteTier* tier = KingdomRando::getTier(3);
                if (Randomizer::isKingdomRandoEnabled()) {
                    progress->mUnlockWorldStatusFirstBranch = tier && selected == tier->worlds[0]
                        ? GameProgressData::FirstBranch::Forest
                        : GameProgressData::FirstBranch::Lake;
                } else {
                    progress->mUnlockWorldStatusFirstBranch = selected == WorldMeta::WorldId_Wooded
                        ? GameProgressData::FirstBranch::Forest
                        : GameProgressData::FirstBranch::Lake;
                }
            }
            progress->mUnlockWorldStatusSecondBranch = GameProgressData::SecondBranch::None;
            if (list->resolvedBranches & 2) {
                const s32 selected = list->entries[8].kingdomId;
                const KingdomRando::RouteTier* tier = KingdomRando::getTier(7);
                if (Randomizer::isKingdomRandoEnabled()) {
                    progress->mUnlockWorldStatusSecondBranch = tier && selected == tier->worlds[0]
                        ? GameProgressData::SecondBranch::Sea
                        : GameProgressData::SecondBranch::Snow;
                } else {
                    progress->mUnlockWorldStatusSecondBranch = selected == WorldMeta::WorldId_Seaside
                        ? GameProgressData::SecondBranch::Sea
                        : GameProgressData::SecondBranch::Snow;
                }
            }
            normalizeStoryProgress(file);
        }
        debugWorldMapOrder(file, count, unlockedWorldId);
    }

    static void syncProgressionWorlds(GameDataFile* file) {
        if (!file)
            return;

        normalizeCloudBossScenario(file);
        renderAuthoritativeProgression(file);
        return;

        // Everything below this point is unreachable (superseded by
        // renderAuthoritativeProgression above) and is kept only for reference; none of the
        // seed-load/mask-authority/branch-ordering logic or its comments describe code that
        // still runs.
        static GameDataFile* sLastSeedFile = nullptr;
        static bool sLastHasSavedSeed = false;
        static u32 sLastSavedSeed = 0xffffffffu;
        static u32 sLastCurrentSeed = 0xffffffffu;

        // Load the seed from the save file if it exists (this is the authoritative source)
        u32 savedSeed = 0;
        const bool hasSavedSeed = file->customGetRandoSeed(&savedSeed);
        if (file != sLastSeedFile || hasSavedSeed != sLastHasSavedSeed || savedSeed != sLastSavedSeed || Randomizer::getSeed() != sLastCurrentSeed) {
            Randomizer::logPush("seed load file=%llx has=%d saved=%u cur=%u kr=%d",
                (unsigned long long)(uintptr_t)file, hasSavedSeed, savedSeed,
                Randomizer::getSeed(), Randomizer::isKingdomRandoEnabled());
            sLastSeedFile = file;
            sLastHasSavedSeed = hasSavedSeed;
            sLastSavedSeed = savedSeed;
            sLastCurrentSeed = Randomizer::getSeed();
        }

        if (hasSavedSeed) {
            if (Randomizer::getSeed() != savedSeed) {
                Randomizer::setSeed(savedSeed);
                Randomizer::reinit();
                sLastCurrentSeed = Randomizer::getSeed();
            }
        } else if (Randomizer::isKingdomRandoEnabled()) {
            // Only save the seed if the save file doesn't have one yet
            file->customSetRandoSeed(Randomizer::getSeed());
            Randomizer::logPush("seed save file=%llx seed=%u",
                (unsigned long long)(uintptr_t)file, Randomizer::getSeed());
            requestSave(file);
        }

        // Commit a unlock the world map requested (GameProgressData::unlockNextWorld fired) before
        // re-deriving state below, so the unlock animation's kingdom lands in the authoritative set.
        // advanceProgressionUnlocks validates it against the frontier and persists it.
        if (sPendingUnlockNextWorldGameId >= 0) {
            const s32 pendingLogical = toLogicalWorldId(sPendingUnlockNextWorldGameId);
            sPendingUnlockNextWorldGameId = -1;
            advanceProgressionUnlocks(file, pendingLogical);
        }

        // The unlock SET is the authority. Load it, ensure the start world, apply the
        // brand-new-save pin, and persist it. mIsUnlockWorld / mUnlockWorldNum / the world-map
        // arrays below are all rendered from this set every sync (never read back as truth), so
        // vanilla globe/warp writes to mIsUnlockWorld can never corrupt progression.
        const u32 loadedMask = loadUnlockSet(file);
        u32 mask = loadedMask | startWorldBit();
        normalizeCloudBossScenario(file);
        if (shouldForceStartWorldOnly(file))
            mask = startWorldBit();
        if (mask != loadedMask)
            storeUnlockSet(file, mask);

        static GameDataFile* sLastExpensiveSyncFile = nullptr;
        static s32 sLastTotalShineNum = -1;
        static s32 sLastCurrentWorldId = -1;
        static u32 sLastExpensiveSyncSeed = 0xffffffffu;
        static u32 sLastUnlockMask = 0xffffffffu;

        s32 currentShineNum = file->getTotalShineNum();
        s32 currentWorldId = file->getCurrentWorldId();

        if (mask != sLastUnlockMask || file != sLastExpensiveSyncFile || currentShineNum != sLastTotalShineNum || currentWorldId != sLastCurrentWorldId || Randomizer::getSeed() != sLastExpensiveSyncSeed) {
            ScenarioPatcher::healAllScenarios(file);
            debugRouteOrder("sync");

            sLastExpensiveSyncFile = file;
            sLastTotalShineNum = currentShineNum;
            sLastCurrentWorldId = currentWorldId;
            sLastExpensiveSyncSeed = Randomizer::getSeed();
            sLastUnlockMask = mask;
        }

        GameProgressData* progress = file->getGameProgressData();
        const bool forceStartWorldOnly = shouldForceStartWorldOnly(file);

        // Accessible worlds come straight from the set (start world is always accessible).
        bool accessible[WorldMeta::sWorldCount];
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            accessible[i] = false;
        accessible[getStartingProgressionWorldId()] = true;
        if (!forceStartWorldOnly) {
            for (s32 logicalWorldId = 0; logicalWorldId < WorldMeta::sWorldCount; logicalWorldId++) {
                if (isWorldInMask(mask, logicalWorldId) && isProgressionRouteWorld(logicalWorldId))
                    accessible[logicalWorldId] = true;
            }
        }

        uintptr_t base = reinterpret_cast<uintptr_t>(file);
        s32* unlockedWorldId = *reinterpret_cast<s32**>(base + 0x5f0); // GameDataFile::mUnlockedWorldId
        s32* worldMapIndex = *reinterpret_cast<s32**>(base + 0xb40); // GameDataFile::mWorldMapIndex
        s32* worldWarpIndex = *reinterpret_cast<s32**>(base + 0xb50); // GameDataFile::mWorldWarpIndex

        for (s32 i = 0; i < sNumWorlds; i++) {
            if (unlockedWorldId)
                unlockedWorldId[i] = -1;
            if (worldMapIndex)
                worldMapIndex[i] = -1;
            if (worldWarpIndex)
                worldWarpIndex[i] = -1;
            if (progress) {
                if (progress->mWorldIdForWorldMap)
                    progress->mWorldIdForWorldMap[i] = -1;
                if (progress->mWorldIdForWorldWarpHole)
                    progress->mWorldIdForWorldWarpHole[i] = -1;
            }
        }

        if (progress && progress->mIsUnlockWorld) {
            for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
                progress->mIsUnlockWorld[i] = false;
        }

        s32 count = 0;
        auto appendWorld = [&](s32 logicalWorldId) {
            if (!isValidTravelWorld(logicalWorldId) || !accessible[logicalWorldId] || count >= sNumWorlds)
                return;

            const s32 gameWorldId = toGameWorldId(logicalWorldId);
            if (!isValidGameWorldId(gameWorldId))
                return;

            if (unlockedWorldId)
                unlockedWorldId[count] = gameWorldId;
            if (worldMapIndex)
                worldMapIndex[count] = gameWorldId;
            if (worldWarpIndex)
                worldWarpIndex[count] = gameWorldId;

            if (progress) {
                if (progress->mWorldIdForWorldMap)
                    progress->mWorldIdForWorldMap[count] = gameWorldId;
                if (progress->mWorldIdForWorldWarpHole)
                    progress->mWorldIdForWorldWarpHole[count] = gameWorldId;
                if (progress->mIsUnlockWorld)
                    progress->mIsUnlockWorld[gameWorldId] = true;
            }

            count++;
        };

        auto appendBranchWorlds = [&](BranchPair branch) {
            // Order by the chosen sibling (read from the mask, since mIsUnlockWorld is being
            // rebuilt right now), then fall back to whichever side is actually accessible first.
            s32 first = branch.first;
            s32 second = branch.second;
            if (branchChoiceFromMask(mask, branch) == branch.second) {
                first = branch.second;
                second = branch.first;
            }

            if (isValidTravelWorld(first) && isValidTravelWorld(second) && !accessible[first] && accessible[second]) {
                const s32 temp = first;
                first = second;
                second = temp;
            }

            appendWorld(first);
            appendWorld(second);
        };

        for (s32 tierIndex = 0; tierIndex < KingdomRando::getTierCount(); tierIndex++) {
            const KingdomRando::RouteTier* tier = KingdomRando::getTier(tierIndex);
            if (!tier)
                continue;
            if (tier->count == 2)
                appendBranchWorlds(getBranchPairForTier(tierIndex));
            else if (tier->count == 1)
                appendWorld(tier->worlds[0]);
        }

        *reinterpret_cast<s32*>(base + 0xb48) = count; // GameDataFile::mWorldMapNum
        *reinterpret_cast<s32*>(base + 0xb58) = count; // GameDataFile::mWorldWarpNum
        *reinterpret_cast<s32*>(base + 0xb5c) = count; // GameDataFile::mUnlockedWorldNum
        debugWorldMapOrder(file, count, unlockedWorldId);
        if (progress) {
            normalizeStoryProgress(file);
            progress->mUnlockWorldNum = count;
            renderBranchEnums(progress, mask);
        }

        // Refresh the derived branch-choice cache the public accessors expose (RandoSaveFile /
        // main.cpp). The mask is authoritative; this is purely a snapshot for display/save-meta.
        sSavedFirstBranchChoiceWorld = branchChoiceFromMask(mask, getFirstBranch());
        sSavedSecondBranchChoiceWorld = branchChoiceFromMask(mask, getSecondBranch());
    }

    static s32 ensurePendingLockedProgressionWorlds(const GameProgressData* progress) {
        if (!progress)
            return 0;
        if (progress == sPendingLockedProgress && sPendingLockedWorldCount > 0)
            return sPendingLockedWorldCount;
        return 0;
    }

    static s32 calcNextLockedProgressionWorldNum(const GameProgressData* progress) {
        return ensurePendingLockedProgressionWorlds(progress);
    }

    static s32 calcNextLockedProgressionWorldId(const GameProgressData* progress, s32 index) {
        const s32 count = ensurePendingLockedProgressionWorlds(progress);
        if (index < 0 || index >= count)
            return -1;
        sWorldMapUnlockMessageWorldId = sPendingLockedWorlds[index];
        return sPendingLockedWorlds[index];
    }

    static bool isCloudCurrentWorld(GameDataHolder* holder) {
        if (!holder)
            return false;

        GameDataHolderAccessor accessor(holder);
        if (!accessor.mData || !accessor.mData->getGameDataFile())
            return false;
        return toLogicalWorldId(GameDataFunction::getCurrentWorldId(accessor)) == WorldMeta::WorldId_Cloud;
    }

    static al::LiveActor* getKoopaBattleEndHostActor(KoopaStateDeadAndDemoBattleEnd* state) {
        if (!state)
            return nullptr;

        return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x18);
    }

    static GameDataHolder* tryGetKoopaBattleEndHolder(KoopaStateDeadAndDemoBattleEnd* state) {
        al::LiveActor* actor = getKoopaBattleEndHostActor(state);
        if (!actor)
            return nullptr;

        GameDataHolderAccessor accessor(actor);
        return accessor.mData;
    }

    static void requestCloudPostBossHomeWarp(GameDataHolder* holder) {
        if (!isCloudCurrentWorld(holder))
            return;

        GameDataHolderWriter writer(holder);
        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (!file)
            return;

        // Beating Bowser is Cloud's completion event (Cloud has no moons): promote its
        // scenario to the mod's >= 3 "boss beaten" marker. That makes Cloud complete, so
        // the next kingdom becomes a frontier candidate. Deliberately DON'T unlock the
        // next kingdom here — leaving it on the frontier lets the world map reveal it
        // with its normal fly-in animation the next time the player visits the globe,
        // instead of it appearing already-unlocked (which skipped the animation before).
        setWorldScenarioNoAtLeast(file, WorldMeta::WorldId_Cloud, 3);
        setWorldMainScenarioNoAtLeast(file, WorldMeta::WorldId_Cloud, 3);
        syncProgressionWorlds(file);

        // The scenario promotion only lives in GameProgressData until the next autosave.
        // Force a save now so a reload right after the Bowser fight still shows Cloud
        // complete (and the next kingdom on the frontier), not the pre-Cloud world map.
        requestSave(file);

        requestOdysseyArrivalWithoutExitDemo(kCloudHomeStageName);
        ChangeStageInfo info(holder, kHomeEntranceName, kCloudHomeStageName, false, 3,
            ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
        GameDataFunction::tryChangeNextStage(writer, &info);
    }

    // Toadette Medal Hunt's tombstone interaction needs a warp target for the real capture/
    // escape sequence (Mario captures Bowser and escapes the collapsing castle
    // toward the Moon). That sequence itself is driven by an in-level scripted
    // actor (DemoHackFirstDirector, ida_context/Demo/DemoHackFirstDirector.md)
    // gated on live fight-progress flags, not something a moon-pickup callback
    // can safely fake. But its stage - MoonWorldKoopa1Stage, the first-phase
    // Moon Kingdom Bowser encounter (a real, separate stage from both
    // SkyWorldHomeStage and the MoonWorldWeddingRoomStage chapel; confirmed via
    // this mod's own extracted entrance data, logic/templates/
    // raw_stage_points.csv: "MoonWorldKoopa1Stage,stage_change,kkk,...,1;2") -
    // is real and has a genuine "kkk" entrance valid for scenarios 1 and 2,
    // unlike the earlier guesses. Warping there directly (scenario 1, the
    // first valid one) is the closest safe, evidence-backed approximation.
    void requestBowserEscapeSequenceWarp(GameDataHolderWriter writer) {
        if (!writer.mData)
            return;

        ChangeStageInfo info(writer.mData, "kkk", "MoonWorldKoopa1Stage", false, 1,
            ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
        GameDataFunction::tryChangeNextStage(writer, &info);
    }

    static const KingdomProgress::KingdomProgressEntry*
    getCurrentFrontierEntry(GameDataFile* file) {
        if (!file)
            return nullptr;
        const s32 frontier = KingdomProgress::getFirstIncompleteIndex(file);
        const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
        if (!list || frontier < 0 || frontier >= KingdomProgress::cKingdomCount)
            return nullptr;
        const KingdomProgress::KingdomProgressEntry& entry = list->entries[frontier];
        return entry.kingdomId == file->getCurrentWorldId() ? &entry : nullptr;
    }

    static bool canDepositInCurrentWorld(GameDataFile* file) {
        const KingdomProgress::KingdomProgressEntry* entry = getCurrentFrontierEntry(file);
        return entry
            && (!entry->requirement.requiresBoss
                || isBossProgressComplete(file, entry->kingdomId));
    }

    // Count of the base (non-QoL-extended) progression list, i.e. without the
    // Visited Kingdoms Map's early-visited extras. The frontier is pinned out
    // during the transient window between completing a kingdom and consuming
    // its reveal animation, matching what renderAuthoritativeProgression
    // mirrors into mUnlockWorldNum for that same window.
    static s32 getBaseUnlockWorldCount(GameDataFile* file) {
        if (!file)
            return 0;
        s32 visibleCount = KingdomProgress::getVisibleCount(file);
        const GameProgressData* progress = file->getGameProgressData();
        if (progress && progress == sPendingLockedProgress && sPendingLockedWorldCount > 0) {
            const s32 frontier = KingdomProgress::getFirstIncompleteIndex(file);
            if (frontier >= 0)
                visibleCount = frontier;
        }
        return visibleCount;
    }

    // How many moons the globe will let the player deposit here.
    //
    // This has to be the same number the HUD counter shows, or the globe refuses
    // a deposit the player can see they have the moons for. The HUD is
    // ShineCounter -> GameDataFunction::getCurrentShineNum, which is exactly
    // getShineNum(currentWorld) - getPayShineNum(currentWorld), so use the same
    // pair here. Both go through MoonHooks' redistribution hook, which is what
    // makes a "Randomize Triple Moons" reward count for its credited kingdom at
    // its real value; reading the raw save total instead is what let the two
    // disagree.
    static s32 getHeldMoonsInCurrentWorld(GameDataFile* file) {
        if (!file)
            return 0;
        const s32 nativeWorldId = file->getCurrentWorldId();
        const s32 held = file->getShineNum(nativeWorldId)
            - file->getPayShineNum(nativeWorldId);
        return held > 0 ? held : 0;
    }

    static void logGlobeState(const char* event, GameDataFile* file) {
        const KingdomProgress::KingdomProgressList* list = file ? KingdomProgress::get(file) : nullptr;
        const s32 frontier = file ? KingdomProgress::getFirstIncompleteIndex(file) : -1;
        const KingdomProgress::KingdomProgressEntry* entry = list && frontier >= 0 && frontier < KingdomProgress::cKingdomCount
            ? &list->entries[frontier]
            : nullptr;
        const s32 currentNative = file ? file->getCurrentWorldId() : -1;
        const s32 shine = file ? file->getShineNum(currentNative) : -1;
        const s32 paid = file ? file->getPayShineNum(currentNative) : -1;
        Randomizer::logPush(
            "GLOBE %s file=%llx curN=%d/L%d F=%d N%d/L%d shine=%d pay=%d held=%d req=%d boss=%d",
            event ? event : "?",
            (unsigned long long)(uintptr_t)file,
            currentNative, toLogicalWorldId(currentNative), frontier,
            entry ? entry->kingdomId : -1,
            entry ? toLogicalWorldId(entry->kingdomId) : -1,
            shine, paid, file ? getHeldMoonsInCurrentWorld(file) : -1,
            entry ? entry->requirement.moonCount : -1,
            entry ? entry->requirement.requiresBoss : -1);
        Randomizer::logPush(
            "GLOBE txn armedFile=%llx armedN=%d evalFile=%llx pending=%d [%d,%d] facade=%d",
            (unsigned long long)(uintptr_t)sGlobeCompletionFile,
            sGlobeCompletionWorldId,
            (unsigned long long)(uintptr_t)sGlobeStartEvaluationFile,
            sPendingLockedWorldCount, sPendingLockedWorlds[0],
            sPendingLockedWorlds[1], sWorldMapTravelFacadeActive);
    }

    static bool shouldSkipCompletedWorldDepositDemo(GameDataFile* file) {
        if (!file)
            return false;
        const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, file->getCurrentWorldId());
        return entry && entry->completeStatus;
    }

    static bool isCurrentTravelWorld(GameDataHolderAccessor accessor, s32 worldId) {
        return accessor.mData && accessor.mData->getGameDataFile() && isValidGameWorldId(worldId)
            && GameDataFunction::getCurrentWorldId(accessor) == toGameWorldId(worldId);
    }

    // Vanilla's Cap Odyssey initialization asks about Cascade before deciding
    // whether the ship actor should exist. This facade is active only inside
    // ShineTowerRocket::init; authoritative map arrays remain Cap-only.
    static bool isCapHomeInitCascadeFacade(GameDataHolderAccessor accessor, s32 worldId) {
        return sAllowCurrentWorldAsUnlockedForHomeInit
            && accessor.mData
            && accessor.mData->getGameDataFile()
            && GameDataFunction::getCurrentWorldId(accessor) == WorldMeta::WorldId_Cap
            && toLogicalWorldId(worldId) == WorldMeta::WorldId_Cascade;
    }

    static bool isAlreadyGoTravelWorldInFile(const GameProgressData* progress, s32 worldId) {
        if (!isValidGameWorldId(worldId))
            return false;
        return isAlreadyGoAccessibleWorld(progress, worldId);
    }

    static void prepareOdysseyTravelState(GameDataFile* file) {
        if (!file)
            return;

        // Map/checkpoint reloads set this bit until the arriving stage consumes it.
        // Odyssey travel is a different transition; carrying the bit across kingdoms
        // makes StageScene search the new kingdom for the previous checkpoint.
        file->mIsWarpCheckpoint = false;
        sCurrentWorldIdForTravelState = file->getCurrentWorldId();
        syncProgressionWorlds(file);

        uintptr_t base = reinterpret_cast<uintptr_t>(file);
        *reinterpret_cast<bool*>(base + 0x691) = true; // GameDataFile::mIsPlayAlreadyWorldWarp

        if (isOdysseyTravelFacadeActive())
            beginPersistentWorldWarpTalkMask(file->getWorldWarpTalkData());
    }

    static void applyOdysseyFixedLook(GameDataHolderWriter writer) {
        if (writer.mData)
            writer->setExistKoopaShip(false);
    }

    static void prepareOdysseyTravelState(GameDataHolderWriter writer) {
        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (isOdysseyFixedFacadeActive())
            applyOdysseyFixedLook(writer);
        if (sWorldMapTravelFacadeActive) {
            beginPersistentFixedHomeStatusMask(file);
            beginPersistentTravelFlagMask(file);
        }
        prepareOdysseyTravelState(file);
    }

    static void prepareOdysseyTravelState(GameDataHolderAccessor accessor) {
        if (accessor.mData)
            prepareOdysseyTravelState(GameDataHolderWriter(accessor.mData));
    }

    static GameDataFile* tryGetGameDataFile(GameDataHolderAccessor accessor) {
        if (!accessor.mData)
            return nullptr;
        return accessor->getGameDataFile();
    }

    static GameDataFile* tryGetActiveDemoGameDataFile(GameDataHolderAccessor accessor) {
        if (!accessor.mData)
            return nullptr;
        return *reinterpret_cast<GameDataFile**>(
            reinterpret_cast<char*>(accessor.mData) + 0x20);
    }

    class ScopedCompletedWorldNoDepositDemo {
    public:
        ScopedCompletedWorldNoDepositDemo(GameDataFile* file)
            : mDidSuppress(shouldSkipCompletedWorldDepositDemo(file)) {
            if (!mDidSuppress)
                return;

            if (sCompletedWorldNoDepositDemoDepth++ == 0)
                sCompletedWorldNoDepositDemoFile = file;
        }

        ~ScopedCompletedWorldNoDepositDemo() {
            if (!mDidSuppress || sCompletedWorldNoDepositDemoDepth <= 0)
                return;

            if (--sCompletedWorldNoDepositDemoDepth == 0)
                sCompletedWorldNoDepositDemoFile = nullptr;
        }

    private:
        bool mDidSuppress = false;
    };

    static const al::Nerve* getRocketNerve(s32 index) {
        if (!sRocketNerveBase)
            return nullptr;

        return reinterpret_cast<const al::Nerve*>(
            reinterpret_cast<uintptr_t>(sRocketNerveBase) + index * kRocketNerveStride);
    }

    static s32 getRocketNerveIndex(ShineTowerRocket* rocket) {
        const al::Nerve* current = rocket ? al::getCurrentNerve(rocket) : nullptr;
        if (!current || !sRocketNerveBase)
            return -1;
        const intptr_t delta = reinterpret_cast<intptr_t>(current)
            - reinterpret_cast<intptr_t>(sRocketNerveBase);
        if (delta < 0 || delta % static_cast<intptr_t>(kRocketNerveStride) != 0)
            return -1;
        return static_cast<s32>(delta / static_cast<intptr_t>(kRocketNerveStride));
    }

    static void cacheRocketNerveBase(ShineTowerRocket* rocket, s32 currentIndex) {
        const al::Nerve* current = al::getCurrentNerve(rocket);
        if (!current)
            return;

        sRocketNerveBase = reinterpret_cast<const al::Nerve*>(
            reinterpret_cast<uintptr_t>(current) - currentIndex * kRocketNerveStride);
    }

    static bool isRocketNerve(ShineTowerRocket* rocket, s32 index) {
        const al::Nerve* expected = getRocketNerve(index);
        return expected && al::getCurrentNerve(rocket) == expected;
    }

    static void setRocketNerve(ShineTowerRocket* rocket, s32 index) {
        const al::Nerve* nerve = getRocketNerve(index);
        if (nerve)
            al::setNerve(rocket, nerve);
    }

    static void beginOdysseyWorldMapTravelFacade(GameDataHolderWriter writer) {
        sWorldMapTravelFacadeActive = true;
        prepareOdysseyTravelState(writer);
    }

    static void startOdysseyWorldMapTravel(ShineTowerRocket* rocket, GameDataHolderWriter writer) {
        beginOdysseyWorldMapTravelFacade(writer);
        setRocketNerve(rocket, RocketNerve_GoToWorldMapWithCamera);
    }

    static HkTrampoline<void, ShineTowerRocket*, const al::ActorInitInfo&>
        shineTowerRocketInitHook = hk::hook::trampoline([](ShineTowerRocket* rocket, const al::ActorInitInfo& info) -> void {
            restorePersistentFixedHomeStatusMask();
            restorePersistentTravelFlagMask();
            restorePersistentWorldWarpTalkMask();
            sWorldMapTravelFacadeActive = false;
            clearPendingLockedProgressionWorlds();
            ScopedOdysseyFixedFacade fixedFacade;
            al::SceneObjHolder* sceneObjHolder = info.actorSceneInfo.sceneObjHolder;
            GameDataFile* file = nullptr;
            if (sceneObjHolder) {
                GameDataHolderWriter writer(sceneObjHolder);
                applyOdysseyFixedLook(writer);
                file = GameDataFunction::getGameDataFile(writer);
                if (file) {
                    syncProgressionWorlds(file);
                    sCurrentWorldIdForTravelState = file->getCurrentWorldId();
                }
            }
            ScopedFixedHomeStatus fixedHomeStatus(file);
            const char* currentStage = file ? file->getStageNameCurrent() : nullptr;
            const bool suppressExitDemo = sNoDemoOdysseyArrivalStage[0]
                && currentStage
                && al::isEqualString(currentStage, sNoDemoOdysseyArrivalStage);
            // A rocket init is the only consumer of this cross-stage request. Consume it
            // even if the destination differs, so a failed/redirected transition can
            // never suppress a later genuine exit from the Odyssey.
            sNoDemoOdysseyArrivalStage[0] = '\0';
            bool wasAllowingCurrentWorld = sAllowCurrentWorldAsUnlockedForHomeInit;
            sAllowCurrentWorldAsUnlockedForHomeInit = true;
            shineTowerRocketInitHook.orig(rocket, info);
            sAllowCurrentWorldAsUnlockedForHomeInit = wasAllowingCurrentWorld;

            GameDataHolderAccessor accessor(rocket);
            const char* playerStartId = GameDataFunction::tryGetPlayerStartId(accessor);
            const bool isAppearFromEntrance = playerStartId && al::isEqualString(playerStartId, "HomeEntrance");
            cacheRocketNerveBase(rocket, isAppearFromEntrance ? RocketNerve_DemoAppearFromEntrance : RocketNerve_Wait);
            if (suppressExitDemo && isAppearFromEntrance) {
                setRocketNerve(rocket, RocketNerve_Wait);
                Randomizer::logPush("Odyssey arrival: suppressed exit demo in %s", currentStage);
            }
        });

    static GameDataHolderAccessor makeShineCounterAccessor(void* counter) {
        if (!counter)
            return GameDataHolderAccessor((GameDataHolder*)nullptr);
        return GameDataHolderAccessor(
            reinterpret_cast<const al::IUseSceneObjHolder*>(reinterpret_cast<uintptr_t>(counter) + 0x38));
    }

    static HkTrampoline<s32, void*, bool*, s32> FindUnlockShineNumHook = hk::hook::trampoline([](void* holderPtr, bool* outIsGameClear, s32 worldId) -> s32 {
        if (outIsGameClear)
            *outIsGameClear = false;
        GameDataHolder* holder = static_cast<GameDataHolder*>(holderPtr);
        GameDataFile* file = holder ? holder->getGameDataFile() : nullptr;
        if (const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, toGameWorldId(worldId)))
            return entry->requirement.moonCount;
        const s32 logicalWorldId = toRequirementWorldId(worldId);
        if (logicalWorldId == WorldMeta::WorldId_Moon && !isTerminalWorld(logicalWorldId))
            return getMoonRequirementForWorld(logicalWorldId);
        if (logicalWorldId == WorldMeta::WorldId_Moon)
            return MoonRequirement::getRequirement();
        return getMoonRequirementForWorld(logicalWorldId);
    });

    static HkTrampoline<const char16_t*, const al::IUseMessageSystem*, const char*, const char*>
        getSystemMessageStringHook = hk::hook::trampoline(
            [](const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) -> const char16_t* {
                // This is the sole owner of the getSystemMessageString trampoline.
                // Other message features route their transformations through here;
                // installing a second trampoline on this symbol corrupts the backup.
                if (fileName && key && std::strcmp(fileName, "MenuOption") == 0
                    && std::strcmp(key, "OptionTop_Data") == 0)
                    return u"Return to Main Menu";

                // Painting cap messages: use the real message when it exists, else
                // fall back to a neutral line so the player never sees "NULL".
                if (isCapMessageWorldWarpKey(fileName, key)) {
                    const char16_t* original = getSystemMessageStringHook.orig(messageSystem, fileName, key);
                    if (original && !isNullMessageString(original))
                        return original;
                    return makeWorldWarpCapFallbackMessage(key);
                }

                if (const char16_t* message = MoonRequirement::tryMakeCapMessage(fileName, key))
                    return message;
                if (const char16_t* message = MoonHooks::tryMakeMoonRockKeyCapMessage(fileName, key))
                    return message;
                if (const char16_t* message = MoonHooks::tryMakeSubareaKeyCapMessage(fileName, key))
                    return message;
                if (const char16_t* message
                    = MovementAbilityLock::tryMakeCapMessage(fileName, key))
                    return message;
                if (const char16_t* message = PurpleCoinRewards::tryGetShopMessageOverride(fileName, key))
                    return message;
                if (const char16_t* message = HintSystem::tryGetSystemMessageOverride(fileName, key))
                    return message;

                const bool isWorldMapUnlockKey = fileName && std::strcmp(fileName, "WorldMap") == 0 && key && (startsWith(key, "UnlockWorld_") || startsWith(key, "DecisionWorld_"));
                const char16_t* originalMessage = getSystemMessageStringHook.orig(messageSystem, fileName, key);

                if (const char16_t* message
                    = CreditsRoll::tryBuildMessage(fileName, key, originalMessage))
                    return message;

                if (const char16_t* message = SphinxRando::tryMakeQuestion(originalMessage))
                    return message;

                if (originalMessage && (!isWorldMapUnlockKey || !isNullMessageString(originalMessage)))
                    return originalMessage;

                if (isWorldMapUnlockKey) {
                    bool isDecision = false;
                    s32 logicalWorldId = tryGetWorldMapUnlockMessageWorldId(fileName, key, &isDecision);
                    if (!isValidTravelWorld(logicalWorldId))
                        logicalWorldId = sWorldMapUnlockMessageWorldId;
                    if (const char16_t* message = makeWorldMapUnlockMessage(logicalWorldId, isDecision)) {
                        sWorldMapUnlockMessageWorldId = logicalWorldId;
                        return message;
                    }
                }

                return nullptr;
            });

    static HkTrampoline<void, void*, const char16_t*, const al::MessageTagDataHolder*, bool>
        talkMessageStartForSystemHook = hk::hook::trampoline(
            [](void* talkMessage, const char16_t* message, const al::MessageTagDataHolder* tagData, bool isOverwrite) -> void {
                if (!message || isNullMessageString(message)) {
                    if (const char16_t* fallback = makeWorldMapUnlockMessage(sWorldMapUnlockMessageWorldId, false))
                        message = fallback;
                }
                talkMessageStartForSystemHook.orig(talkMessage, message, tagData, isOverwrite);
            });

    static HkTrampoline<const char16_t*, const al::LayoutActor*, s32>
        tryGetWorldNameForWorldMapHook = hk::hook::trampoline(
            [](const al::LayoutActor* layout, s32 worldId) -> const char16_t* {
                // Toadette Medals page: piggybacks on the real Mushroom Kingdom
                // ShineList world slot (see HintSystem.cpp's calcWorldNumForShineListHook
                // and findHintByWorldIndex), so its title reads "Toadette Medal Hunt"
                // instead of "Mushroom Kingdom" while that page is open. This is
                // merged into the existing tryGetWorldName hook rather than a
                // second one on the same symbol - double-hooking one function
                // corrupts the trampoline backup and crashes mod init.
                //
                // Scoped by the LAYOUT'S OWN IDENTITY (its archive name contains
                // "Shine"/"Collection"/etc, see HintSystem::isLikelyShineListLayout),
                // not HintSystem::isDirectShineListViewActive(): per
                // ida_context/System/GameDataFunction.md, tryGetWorldName's callers
                // include ShineListLayout's own CONSTRUCTOR (it populates a per-world
                // name cache once, at construction) as well as StageSceneStateWorldMap
                // and other unrelated callers - isDirectShineListViewActive() only
                // reflects "currently viewing" and is not true yet during that
                // construction-time call (confirmed - scoping to it silently never
                // fired), while a name check correctly narrows to ShineList
                // specifically regardless of caller/timing, without renaming
                // Mushroom Kingdom anywhere else (World Map, etc).
                if (worldId == WorldMeta::WorldId_Dark
                    && Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
                    && HintSystem::isLikelyShineListLayout(layout)) {
                    return u"Unlocked Paintings";
                }

                if (worldId == WorldMeta::WorldId_Darker && Randomizer::isMoonRockKeyEnabled()
                    && HintSystem::isLikelyShineListLayout(layout)) {
                    return u"Moon Rock Keys";
                }

                if (worldId == WorldMeta::WorldId_Mushroom && Randomizer::isToadetteMedalModeEnabled()
                    && HintSystem::isLikelyShineListLayout(layout)) {
                    return u"Toadette Medal Hunt";
                }

                // Diagnostic: the override above should fire whenever the
                // ShineList queries the Mushroom Kingdom slot with Toadette Medal
                // mode on, but has been reported as not visibly taking effect.
                // Log every worldId==Mushroom call (regardless of the other two
                // gates) once per distinct layout name, to see in-game whether
                // this ever actually gets called with the expected worldId/mode/
                // layout combination - if it's never logged at all, the ShineList
                // never queries this world id;  if it logs with
                // isLikelyShineListLayout=false, the "this" pointer isn't the
                // ShineListLayout itself and the name check is wrong.
                if (worldId == WorldMeta::WorldId_Mushroom) {
                    static const char* sLoggedNames[8] = {};
                    static s32 sLoggedNameCount = 0;
                    const char* name = layout ? layout->getName() : nullptr;
                    if (!name)
                        name = "<null>";
                    bool alreadyLogged = false;
                    for (s32 i = 0; i < sLoggedNameCount; i++) {
                        if (std::strcmp(sLoggedNames[i], name) == 0) {
                            alreadyLogged = true;
                            break;
                        }
                    }
                    if (!alreadyLogged && sLoggedNameCount < 8) {
                        sLoggedNames[sLoggedNameCount++] = name;
                        Randomizer::logPush(
                            "progression tryGetWorldName worldId=Mushroom layout=%s isShineList=%d toadetteMedalMode=%d",
                            name, HintSystem::isLikelyShineListLayout(layout),
                            Randomizer::isToadetteMedalModeEnabled());
                    }
                }

                const s32 displayWorldId = normalizeWorldMapDisplayWorldId(worldId);
                if (displayWorldId != worldId)
                    return WorldMeta::getDisplayName16ForWorldId(toLogicalWorldId(displayWorldId));
                return tryGetWorldNameForWorldMapHook.orig(layout, worldId);
            });

    static HkTrampoline<u64, void*, s32, bool>
        worldMapMovieUpdaterUpdateHook = hk::hook::trampoline(
            [](void* updater, s32 worldId, bool isAlreadyGoWorld) -> u64 {
                const s32 displayWorldId = normalizeWorldMapDisplayWorldId(worldId);
                const bool pending = isWorldMapPendingUnlockTarget(displayWorldId);
                // Runs once per frame for as long as the map is open. Report a
                // given query only when its inputs change, so a presentation
                // costs one in/out pair instead of one per frame.
                static s32 sLastLoggedMovieKey = -1;
                const s32 movieKey = (worldId & 0xFF) | ((displayWorldId & 0xFF) << 8)
                    | ((isAlreadyGoWorld ? 1 : 0) << 16) | ((pending ? 1 : 0) << 17)
                    | ((sProgressionUnlockDemoDepth & 0xFF) << 18);
                const bool logThisQuery = (pending || sProgressionUnlockDemoDepth > 0)
                    && sLastLoggedMovieKey != movieKey;
                if (logThisQuery) {
                    sLastLoggedMovieKey = movieKey;
                    Randomizer::logPush(
                        "MAP movie in=%d display=%d alreadyIn=%d pending=%d demo=%d",
                        worldId, displayWorldId, isAlreadyGoWorld, pending,
                        sProgressionUnlockDemoDepth);
                }
                // The authoritative progression list deliberately keeps the next
                // kingdom locked until the unlock presentation commits it. The
                // movie updater interprets that locked state as "no movie yet"
                // and renders the black NEW card. Let this presentation-only
                // query see the pending kingdom as visited so its real FMV is
                // available; the pending transaction still drives NEW/unlock.
                if (pending)
                    isAlreadyGoWorld = true;
                const u64 result = worldMapMovieUpdaterUpdateHook.orig(
                    updater, displayWorldId, isAlreadyGoWorld);
                if (logThisQuery)
                    Randomizer::logPush("MAP movie out already=%d result=%llx",
                        isAlreadyGoWorld, (unsigned long long)result);
                return result;
            });

    static HkTrampoline<s32, GameDataHolderAccessor>
        getPayShineNumHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> s32 {
            if (sMoonShineCounterPayOverrideDepth > 0 && MoonRequirement::isMoonWorld(accessor))
                return 0;
            return getPayShineNumHook.orig(accessor);
        });

    // The only caller of this overload in the whole game is the moon-counter pip
    // builder below (0x202D20), always guarded by isWorldSky(). Vanilla zeroes the
    // "still needed" placeholder pips there while scenario==1 (pre-Mecha-Bowser),
    // which is why the moon counter never renders anything in Bowser's Kingdom before
    // the boss fight. Report a post-boss scenario unconditionally so the counter shows
    // moon progress from the kingdom's first visit instead.
    static HkTrampoline<s32, const al::LayoutActor*>
        getScenarioNoForSkyShineCounterHook = hk::hook::trampoline([](const al::LayoutActor* layout) -> s32 {
            const s32 scenario = getScenarioNoForSkyShineCounterHook.orig(layout);
            return scenario == 1 ? 2 : scenario;
        });

    static HkTrampoline<void, void*, s32>
        shineCounterBuildHook = hk::hook::trampoline([](void* counter, s32 currentShineNum) -> void {
            GameDataHolderAccessor accessor = makeShineCounterAccessor(counter);
            if (MoonRequirement::isMoonWorld(accessor) && MoonRequirement::getRequirement() > 0) {
                currentShineNum = MoonRequirement::getCollected(accessor);
                sMoonShineCounterPayOverrideDepth++;
                shineCounterBuildHook.orig(counter, currentShineNum);
                sMoonShineCounterPayOverrideDepth--;
                return;
            }

            shineCounterBuildHook.orig(counter, currentShineNum);
        });

    static HkTrampoline<bool, const SaveObjInfo*>
        saveObjInfoIsOnHook = hk::hook::trampoline([](const SaveObjInfo* saveObjInfo) -> bool {
            if (sChurchDoorInitDepth > 0 && sInitializingChurchDoor && sInitializingChurchDoor->mSaveObjInfo == saveObjInfo) {
                GameDataHolderAccessor accessor(sInitializingChurchDoor);
                const char* stageName = accessor.mData ? GameDataFunction::getCurrentStageName(accessor) : nullptr;
                if (MoonRequirement::isMoonChapelStage(stageName)
                    && (MoonRequirement::isToadetteMedalHuntChapelLocked()
                        || !MoonRequirement::isSatisfied(accessor)))
                    return false;
            }

            return saveObjInfoIsOnHook.orig(saveObjInfo);
        });

    static HkTrampoline<void, ChurchDoor*, const al::ActorInitInfo&>
        churchDoorInitHook = hk::hook::trampoline(
            [](ChurchDoor* self, const al::ActorInitInfo& initInfo) -> void {
                ChurchDoor* previousDoor = sInitializingChurchDoor;
                sInitializingChurchDoor = self;
                sChurchDoorInitDepth++;
                churchDoorInitHook.orig(self, initInfo);
                sChurchDoorInitDepth--;
                sInitializingChurchDoor = previousDoor;
            });

    static HkTrampoline<bool, ChurchDoor*, const al::SensorMsg*, al::HitSensor*, al::HitSensor*>
        churchDoorReceiveMsgHook = hk::hook::trampoline(
            [](ChurchDoor* self, const al::SensorMsg* message, al::HitSensor* other,
                al::HitSensor* sensor) -> bool {
                if (!self)
                    return false;

                if (!message || !rs::isMsgCapTouchWall(message) || rs::isMsgPlayerDisregardTargetMarker(message)) {
                    return churchDoorReceiveMsgHook.orig(self, message, other, sensor);
                }

                GameDataHolderAccessor accessor(self);
                const char* stageName = accessor.mData ? GameDataFunction::getCurrentStageName(accessor) : nullptr;
                if (!MoonRequirement::isMoonChapelStage(stageName)
                    || (!MoonRequirement::isToadetteMedalHuntChapelLocked()
                        && MoonRequirement::isSatisfied(accessor)))
                    return churchDoorReceiveMsgHook.orig(self, message, other, sensor);

                if (other && sensor)
                    rs::requestHitReactionToAttacker(message, other, sensor);
                MoonRequirement::showBlockedMessage(self);
                return true;
            });

    // Peach's Castle spans two stages (the grounds and the interior) and the
    // gravestones are placed in the grounds, not only inside, so gate on the
    // pair rather than the interior alone: the interior-only check silently
    // rejected every hit on a gravestone standing outside.
    static constexpr const char* cToadetteMedalTombstoneStages[] = {
        "PeachWorldCastleStage",
        "PeachWorldHomeStage",
    };
    // Confirmed from a placement dump of PeachWorldCastleStage: the object is
    // "Gravestone" for both UnitConfigName and ModelName, at roughly
    // (-464, 522, -1910). Matched as a substring so a suffixed variant still
    // registers.
    static constexpr const char* cToadetteMedalTombstoneActorName = "Gravestone";

    // Hand-rolled: al::searchSubString is declared in the headers but absent
    // from syms/main.sym, so linking against it fails.
    static bool containsSubString(const char* haystack, const char* needle) {
        if (!haystack || !needle)
            return false;
        for (const char* start = haystack; *start; start++) {
            const char* a = start;
            const char* b = needle;
            while (*b && *a == *b) {
                a++;
                b++;
            }
            if (!*b)
                return true;
        }
        return false;
    }

    static bool isToadetteMedalTombstoneStage(const char* stageName) {
        if (!stageName)
            return false;
        for (const char* stage : cToadetteMedalTombstoneStages) {
            if (std::strcmp(stageName, stage) == 0)
                return true;
        }
        return false;
    }

    // Mushroom Kingdom places SEVERAL Gravestone objects. Tracking only the
    // last one created meant a hat thrown at any of the others was more than
    // 300 units from the one remembered position and did nothing at all.
    static constexpr s32 cMaxGravestones = 16;
    static constexpr float cGravestoneCapHitRadius = 300.0f;
    static constexpr float cGravestoneCapHitHeightOffset = 150.0f;
    static al::LiveActor* sGravestoneActors[cMaxGravestones] = {};
    static s32 sGravestoneCount = 0;

    static bool isNearAnyGravestone(const sead::Vector3f& hitPos, al::LiveActor** outActor,
        float* outDistance) {
        for (s32 i = 0; i < sGravestoneCount; i++) {
            al::LiveActor* actor = sGravestoneActors[i];
            if (!actor)
                continue;
            // Read the position live rather than caching it at creation: an
            // actor's model matrix is not necessarily populated yet when
            // createPlacementActor returns, and a cached (0,0,0) makes the
            // proximity test pass only near the world origin.
            sead::Vector3f pos = al::getTrans(actor);
            pos.y += cGravestoneCapHitHeightOffset;
            const sead::Vector3f diff = hitPos - pos;
            const float distance = diff.length();
            if (distance < cGravestoneCapHitRadius) {
                if (outActor)
                    *outActor = actor;
                if (outDistance)
                    *outDistance = distance;
                return true;
            }
        }
        return false;
    }

    static bool tryHandleToadetteMedalTombstoneCapAttack(al::LiveActor* tombstone) {
        if (!tombstone)
            return false;

        // Both medal win conditions answer at the tombstone; only the pure-medal
        // mode warps from it. The hybrid still has to beat Bowser normally, so
        // its gating lives on the Moon chapel
        // (MoonRequirement::areToadetteMedalsSatisfied) - but it should still
        // report medal progress here rather than leaving the tombstone inert,
        // which is what the old "pure mode only" check made it.
        const Randomizer::BowserWinCondition winCondition = Randomizer::getBowserWinCondition();
        if (winCondition == Randomizer::BowserWinCondition_DefeatBowser)
            return false;

        GameDataHolderAccessor accessor(tombstone);
        const char* stageName = accessor.mData ? GameDataFunction::getCurrentStageName(accessor) : nullptr;
        if (!isToadetteMedalTombstoneStage(stageName)) {
            Randomizer::logPush("Toadette Medal Hunt tombstone hit on unexpected stage=%s",
                stageName ? stageName : "<null>");
            return false;
        }

        GameDataHolderWriter writer(tombstone);
        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (!file)
            return true;

        const s32 collected = MoonRando::getToadetteMedalCollectedCount(file);
        const s32 required = Randomizer::getToadetteMedalRatio().required;
        Randomizer::logPush("Toadette Medal Hunt tombstone hit stage=%s win=%d medals=%d/%d",
            stageName, static_cast<s32>(winCondition), collected, required);
        if (collected < required) {
            HintSystem::showToadetteMedalTombstoneNotReadyMessage(tombstone);
            return true;
        }

        // Hybrid mode: the medals are only half of its win condition, so having
        // them all opens the chapel rather than skipping to Bowser.
        if (winCondition != Randomizer::BowserWinCondition_ToadetteMedals)
            return true;

        if (MoonRando::hasTriggeredToadetteMedalWin())
            return true;

        MoonRando::setToadetteMedalWinTriggered(true);
        CaptureRando::setCaptureUnlocked(writer, "Koopa", true);
        CaptureRando::setCaptureUnlocked(writer, "ElectricWire", true);
        GameDataFunction::setRequireSave(writer);
        Randomizer::logPush("Toadette Medal Hunt tombstone activated - warping to Bowser");
        requestBowserEscapeSequenceWarp(writer);
        return true;
    }

    static HkTrampoline<bool, al::HitSensor*, al::HitSensor*, const sead::Vector3f&, const sead::Vector3f&>
        sendMsgCapTouchWallHook = hk::hook::trampoline(
            [](al::HitSensor* receiver, al::HitSensor* sender, const sead::Vector3f& hitPos, const sead::Vector3f& hitNormal) -> bool {
                al::LiveActor* gravestone = nullptr;
                float distance = 0.0f;
                if (isNearAnyGravestone(hitPos, &gravestone, &distance)) {
                    Randomizer::logPush("Cappy hit Gravestone KCol wall! Dist: %f", distance);
                    if (tryHandleToadetteMedalTombstoneCapAttack(gravestone))
                        return true;
                }
                return sendMsgCapTouchWallHook.orig(receiver, sender, hitPos, hitNormal);
            });

    void onPlacementActorCreated(al::LiveActor* actor, const al::PlacementInfo* placementInfo) {
        if (!actor || !placementInfo)
            return;

        PurpleCoinRewards::registerNpcActorPlacement(actor, placementInfo);

        const char* unitConfigName = nullptr;
        const char* modelName = nullptr;
        placementInfo->getPlacementIter().tryGetStringByKey(&unitConfigName, "UnitConfigName");
        placementInfo->getPlacementIter().tryGetStringByKey(&modelName, "ModelName");
        if (!containsSubString(unitConfigName, cToadetteMedalTombstoneActorName)
            && !containsSubString(modelName, cToadetteMedalTombstoneActorName))
            return;

        if (sGravestoneCount >= cMaxGravestones)
            return;

        sGravestoneActors[sGravestoneCount++] = actor;
        const sead::Vector3f pos = al::getTrans(actor);
        Randomizer::logPush(
            "createPlacementActor: gravestone #%d unit=%s model=%s at (%f, %f, %f)",
            sGravestoneCount, unitConfigName ? unitConfigName : "<null>",
            modelName ? modelName : "<null>", pos.x, pos.y, pos.z);
    }

    static HkReplace<bool, GameDataHolderAccessor, s32>
        isUnlockedWorldHook = hk::hook::replace([](GameDataHolderAccessor accessor, s32 worldId) -> bool {
            if (isOdysseyTravelFacadeActive())
                prepareOdysseyTravelState(accessor);
            if (sAllowCurrentWorldAsUnlockedForHomeInit
                && (isCurrentTravelWorld(accessor, worldId)
                    || isCapHomeInitCascadeFacade(accessor, worldId)))
                return true;
            if (!accessor.mData)
                return false;
            if (isProgressionUnlockDemoWorld(worldId))
                return true;

            GameDataFile* file = tryGetGameDataFile(accessor);
            syncProgressionWorlds(file);
            return isProgressionWorldAccessible(file, worldId);
        });

    static HkTrampoline<bool, const GameProgressData*, s32>
        isUnlockWorldProgressHook = hk::hook::trampoline([](const GameProgressData* progress, s32 index) -> bool {
            if (!progress || !progress->mIsUnlockWorld || !isValidGameWorldId(index))
                return false;
            return progress->mIsUnlockWorld[index];
        });

    static HkReplace<bool, GameDataHolderAccessor, s32>
        isAlreadyGoWorldHook = hk::hook::replace([](GameDataHolderAccessor accessor, s32 worldId) -> bool {
            if (isOdysseyTravelFacadeActive())
                prepareOdysseyTravelState(accessor);
            if (sAllowCurrentWorldAsUnlockedForHomeInit
                && (isCurrentTravelWorld(accessor, worldId)
                    || isCapHomeInitCascadeFacade(accessor, worldId)))
                return true;
            if (isProgressionUnlockDemoWorld(worldId)) {
                Randomizer::logPush("UNLOCK query alreadyGo accessor L%d -> 0 demo", worldId);
                return false;
            }
            GameDataFile* file = tryGetGameDataFile(accessor);
            syncProgressionWorlds(file);
            GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
            return isProgressionWorldAccessible(file, worldId)
                && isAlreadyGoTravelWorldInFile(progress, worldId);
        });

    static HkTrampoline<bool, const GameProgressData*, s32>
        isAlreadyGoWorldProgressHook = hk::hook::trampoline([](const GameProgressData* progress, s32 index) -> bool {
            if (isProgressionUnlockDemoWorld(index)) {
                Randomizer::logPush("UNLOCK query alreadyGo progress L%d -> 0 demo", index);
                return false;
            }
            if (!progress || !progress->mIsUnlockWorld || !isValidGameWorldId(index) || !progress->mIsUnlockWorld[index])
                return false;
            return isAlreadyGoAccessibleWorld(progress, index);
        });

    // ShineTowerRocket (the globe) is the sole caller of these two LiveActor
    // overloads (via GameDataFunction::sub_710030DA68, which decides whether
    // the world the player is standing in is the one that can level up). The
    // Scene/LayoutActor overloads used by the actual World Map screen and
    // achievement panels are left untouched, so they still see the QoL
    // Visited Kingdoms Map extras appended by renderAuthoritativeProgression.
    // Routing the globe's view through the base (non-extended) list keeps
    // those extras a pure display convenience that can never shift which
    // world index the deposit/unlock logic reads as "current".
    static HkTrampoline<s32, const al::LiveActor*>
        getUnlockWorldNumForWorldMapActorHook = hk::hook::trampoline([](const al::LiveActor* actor) -> s32 {
            GameDataHolderAccessor accessor(actor);
            GameDataFile* file = tryGetGameDataFile(accessor);
            return file ? getBaseUnlockWorldCount(file)
                        : getUnlockWorldNumForWorldMapActorHook.orig(actor);
        });

    static HkTrampoline<s32, const al::LiveActor*, s32>
        getUnlockWorldIdForWorldMapActorHook = hk::hook::trampoline([](const al::LiveActor* actor, s32 index) -> s32 {
            GameDataHolderAccessor accessor(actor);
            GameDataFile* file = tryGetGameDataFile(accessor);
            if (!file)
                return getUnlockWorldIdForWorldMapActorHook.orig(actor, index);
            const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
            const s32 count = getBaseUnlockWorldCount(file);
            if (!list || index < 0 || index >= count)
                return -1;
            return list->entries[index].kingdomId;
        });

    static HkTrampoline<s32, GameDataHolderAccessor>
        getCurrentShineNumHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> s32 {
            GameDataFile* file = tryGetGameDataFile(accessor);
            if (file && file == sGlobeStartEvaluationFile) {
                const KingdomProgress::KingdomProgressEntry* entry = getCurrentFrontierEntry(file);
                if (entry && entry->requirement.requiresBoss
                    && !isBossProgressComplete(file, entry->kingdomId)) {
                    Randomizer::logPush(
                        "GLOBE currentShine boss-block W%d -> 0",
                        entry->kingdomId);
                    return 0;
                }
                const s32 held = getHeldMoonsInCurrentWorld(file);
                Randomizer::logPush(
                    "GLOBE currentShine override curN=%d shine=%d pay=%d -> %d",
                    file->getCurrentWorldId(),
                    file->getShineNum(file->getCurrentWorldId()),
                    file->getPayShineNum(file->getCurrentWorldId()), held);
                return held;
            }
            if (sCompletedWorldNoDepositDemoDepth > 0) {
                if (file && file == sCompletedWorldNoDepositDemoFile && shouldSkipCompletedWorldDepositDemo(file))
                    return 0;
            }
            return getCurrentShineNumHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoWorldWarpHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoWorldWarpHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoWorldWarpHoleHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoWorldWarpHoleHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isForwardWorldWarpDemoHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isForwardWorldWarpDemoHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoReturnToHomeHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoReturnToHomeHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoAwardSpecialHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoAwardSpecialHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoLavaEruptHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoLavaEruptHook.orig(accessor);
        });

    static HkTrampoline<bool, GameDataHolderAccessor>
        isPlayDemoPlayerDownForBattleKoopaAfterHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
            if (!tryGetActiveDemoGameDataFile(accessor))
                return false;
            return isPlayDemoPlayerDownForBattleKoopaAfterHook.orig(accessor);
        });

    static bool sCurrentWorldUnlocked = true;
    static hk::hook::a64::AsmBlock<true, 1>* sGlobePatch = nullptr;
    static hk::hook::a64::AsmBlock<true, 1>* sGlobeStartFindKoopaPatch = nullptr;
    static hk::hook::a64::AsmBlock<true, 1>* sGlobeStartSkyNoStartPatch = nullptr;
    static hk::hook::a64::AsmBlock<true, 1>* sGlobeStartInvalidChangeStagePatch = nullptr;
    static s32 sGlobeStartPatchDepth = 0;

    static void installGlobeStartPatches() {
        if (sGlobeStartPatchDepth++ != 0)
            return;

        sGlobeStartFindKoopaPatch->installAtMainOffset(0x30C398);
        sGlobeStartSkyNoStartPatch->installAtMainOffset(0x30C3A4);
        sGlobeStartInvalidChangeStagePatch->installAtMainOffset(0x30C3B0);
    }

    static void uninstallGlobeStartPatches() {
        if (sGlobeStartPatchDepth <= 0)
            return;
        if (--sGlobeStartPatchDepth != 0)
            return;

        sGlobeStartFindKoopaPatch->uninstall();
        sGlobeStartSkyNoStartPatch->uninstall();
        sGlobeStartInvalidChangeStagePatch->uninstall();
    }

    class ScopedGlobeStartPatches {
    public:
        ScopedGlobeStartPatches() { installGlobeStartPatches(); }
        ~ScopedGlobeStartPatches() { uninstallGlobeStartPatches(); }
    };

    static HkTrampoline<void, ShineTowerRocket*>
        shineTowerRocketControlHook = hk::hook::trampoline([](ShineTowerRocket* self) -> void {
            GameDataHolderWriter writer(self);
            const s32 nerveBefore = getRocketNerveIndex(self);
            if (sWorldMapTravelFacadeActive)
                prepareOdysseyTravelState(writer);

            shineTowerRocketControlHook.orig(self);
            const s32 nerveAfter = getRocketNerveIndex(self);
            static ShineTowerRocket* sTraceRocket = nullptr;
            static s32 sTraceNerve = -2;
            if (self != sTraceRocket || nerveAfter != sTraceNerve
                || nerveAfter != nerveBefore) {
                GameDataFile* traceFile = GameDataFunction::getGameDataFile(writer);
                Randomizer::logPush(
                    "ROCKET control actor=%llx nerve=%d->%d facade=%d curN=%d",
                    (unsigned long long)(uintptr_t)self, nerveBefore, nerveAfter,
                    sWorldMapTravelFacadeActive,
                    traceFile ? traceFile->getCurrentWorldId() : -1);
                sTraceRocket = self;
                sTraceNerve = nerveAfter;
            }

            if (isRocketNerve(self, RocketNerve_Wait))
                cacheRocketNerveBase(self, RocketNerve_Wait);
            if (sWorldMapTravelFacadeActive && isRocketNerve(self, RocketNerve_Wait)) {
                restorePersistentFixedHomeStatusMask();
                restorePersistentTravelFlagMask();
                restorePersistentWorldWarpTalkMask();
                clearPendingLockedProgressionWorlds();
                sWorldMapTravelFacadeActive = false;
                sCurrentWorldIdForTravelState = -1;
            }
            if (sWorldMapTravelFacadeActive)
                prepareOdysseyTravelState(writer);

            GameDataHolderAccessor accessor(self);
            GameDataFile* file = tryGetGameDataFile(accessor);
            syncProgressionWorlds(file);
            s32 worldId = file ? file->getCurrentWorldId() : -1;
            bool unlocked = isValidGameWorldId(worldId) && isProgressionWorldAccessible(file, toLogicalWorldId(worldId));
            if (unlocked != sCurrentWorldUnlocked) {
                sCurrentWorldUnlocked = unlocked;
                if (!unlocked)
                    sGlobePatch->installAtMainOffset(0x30C37C);
                else
                    sGlobePatch->uninstall();
            }
        });

    static HkTrampoline<bool, ShineTowerRocket*, const al::SensorMsg*, al::HitSensor*, al::HitSensor*>
        shineTowerRocketReceiveMsgHook = hk::hook::trampoline([](ShineTowerRocket* self, const al::SensorMsg* message,
                                                                  al::HitSensor* other, al::HitSensor* sensor) -> bool {
            GameDataHolderWriter writer(self);
            const bool isGlobeStart = message && sensor && rs::isMsgCapStartLockOn(message) && al::isSensorMapObj(sensor);
            GameDataFile* file = isGlobeStart ? GameDataFunction::getGameDataFile(writer) : nullptr;
            const bool isFrontierGlobe = isGlobeStart && getCurrentFrontierEntry(file);
            const bool canProgressAtGlobe = isFrontierGlobe && canDepositInCurrentWorld(file);
            if (isGlobeStart) {
                Randomizer::logPush(
                    "GLOBE receive begin actor=%llx nerve=%d frontier=%d deposit=%d sensor=%llx other=%llx",
                    (unsigned long long)(uintptr_t)self,
                    getRocketNerveIndex(self), isFrontierGlobe,
                    canProgressAtGlobe,
                    (unsigned long long)(uintptr_t)sensor,
                    (unsigned long long)(uintptr_t)other);
                logGlobeState("receive-before", file);
                KingdomProgress::debugLogState(file, "globe-receive-before");
            }

            bool result = false;
            if (isGlobeStart) {
                if (isFrontierGlobe) {
                    sGlobeStartEvaluationFile = file;
                    if (canProgressAtGlobe) {
                        sGlobeCompletionFile = file;
                        sGlobeCompletionWorldId = file->getCurrentWorldId();
                    }
                }
                ScopedOdysseyFixedFacade fixedFacade;
                ScopedOdysseyTravelFacade travelFacade;
                ScopedGlobeStartPatches globeStartPatches;
                ScopedCompletedWorldNoDepositDemo completedWorldNoDeposit(file);
                prepareOdysseyTravelState(writer);
                result = shineTowerRocketReceiveMsgHook.orig(self, message, other, sensor);
                prepareOdysseyTravelState(writer);
                sGlobeStartEvaluationFile = nullptr;
            } else {
                result = shineTowerRocketReceiveMsgHook.orig(self, message, other, sensor);
            }

            if (isGlobeStart) {
                Randomizer::logPush(
                    "GLOBE receive orig result=%d nerveNow=%d frontier=%d",
                    result, getRocketNerveIndex(self), isFrontierGlobe);
                logGlobeState("receive-after-orig", file);
            }
            if (isGlobeStart && result) {
                if (canProgressAtGlobe) {
                    tryCompleteAcceptedGlobe(file);
                } else {
                    sGlobeCompletionFile = nullptr;
                    sGlobeCompletionWorldId = -1;
                }
                beginOdysseyWorldMapTravelFacade(writer);
                logGlobeState("receive-accepted", file);
            } else if (isGlobeStart) {
                sGlobeStartEvaluationFile = nullptr;
                sGlobeCompletionFile = nullptr;
                sGlobeCompletionWorldId = -1;
                restorePersistentWorldWarpTalkMask();
                logGlobeState("receive-rejected", file);
            }
            return result;
        });

    static HkTrampoline<void, ShineTowerRocket*>
        shineTowerRocketDemoPrepareHook = hk::hook::trampoline([](ShineTowerRocket* self) -> void {
            Randomizer::logPush("ROCKET demoPrepare nerve=%d facade=%d",
                getRocketNerveIndex(self), sWorldMapTravelFacadeActive);
            if (!sWorldMapTravelFacadeActive) {
                shineTowerRocketDemoPrepareHook.orig(self);
                return;
            }

            GameDataHolderWriter writer(self);
            ScopedOdysseyFixedFacade fixedFacade;
            ScopedOdysseyTravelFacade travelFacade;
            prepareOdysseyTravelState(writer);

            shineTowerRocketDemoPrepareHook.orig(self);
            prepareOdysseyTravelState(writer);
        });

    static HkTrampoline<void, ShineTowerRocket*>
        shineTowerRocketDemoWalkPlayerToPointHook = hk::hook::trampoline([](ShineTowerRocket* self) -> void {
            if (sWorldMapTravelFacadeActive && isRocketNerve(self, RocketNerve_DemoWalkPlayerToPointNoShine)) {
                GameDataHolderWriter writer(self);
                GameDataFile* file = GameDataFunction::getGameDataFile(writer);
                Randomizer::logPush(
                    "ROCKET noShineWalk pending=%d playUnlock=%d curN=%d",
                    sPendingLockedWorldCount,
                    shouldPlayNoShineProgressionDemo(file),
                    file ? file->getCurrentWorldId() : -1);
                // None/boss-only completion reaches the vanilla no-shine walk.
                // Let that native sequence advance to its map state, but keep
                // our pending candidate alive after this call. The former
                // ScopedProgressionUnlockDemo cleared it here, before the map's
                // later calcNextLockedWorld/unlockWorld callbacks could consume it.
                ScopedOdysseyTravelFacade travelFacade;
                prepareOdysseyTravelState(writer);
                shineTowerRocketDemoWalkPlayerToPointHook.orig(self);
                if (isRocketNerve(self, RocketNerve_NoStart)) {
                    Randomizer::logPush(
                        "ROCKET noShineWalk bypassing vanilla NoStart rejection");
                    startOdysseyWorldMapTravel(self, writer);
                }
                prepareOdysseyTravelState(writer);
                return;
            }

            shineTowerRocketDemoWalkPlayerToPointHook.orig(self);
        });

    static HkTrampoline<bool, ShineTowerRocket*>
        shineTowerRocketTryStartDemoHook = hk::hook::trampoline([](ShineTowerRocket* self) -> bool {
            Randomizer::logPush("ROCKET tryStartDemo nerve=%d facade=%d pending=%d",
                getRocketNerveIndex(self), sWorldMapTravelFacadeActive,
                sPendingLockedWorldCount);
            if (!sWorldMapTravelFacadeActive)
                return shineTowerRocketTryStartDemoHook.orig(self);

            GameDataHolderWriter writer(self);
            ScopedOdysseyTravelFacade travelFacade;
            prepareOdysseyTravelState(writer);

            bool result = shineTowerRocketTryStartDemoHook.orig(self);
            Randomizer::logPush("ROCKET tryStartDemo result=%d nerveNow=%d",
                result, getRocketNerveIndex(self));
            prepareOdysseyTravelState(writer);
            return result;
        });

    static HkTrampoline<void, ShineTowerRocket*>
        shineTowerRocketDemoSelectGoOtherWorldHook = hk::hook::trampoline([](ShineTowerRocket* self) -> void {
            GameDataHolderWriter writer(self);
            ScopedOdysseyTravelFacade travelFacade;
            startOdysseyWorldMapTravel(self, writer);
        });

    static HkTrampoline<bool, const al::LiveActor*>
        isTalkedCapNearHomeInWaterfallHook = hk::hook::trampoline([](const al::LiveActor* actor) -> bool {
            (void)actor;
            return true;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isExistHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            if (accessor.mData)
                normalizeStoryProgress(tryGetGameDataFile(accessor));
            return true;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isActivateHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            if (accessor.mData)
                normalizeStoryProgress(tryGetGameDataFile(accessor));
            return true;
        });

    static HkReplace<bool, const GameProgressData*>
        isActivateHomeProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return true;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isLaunchHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return true;
        });

    static HkReplace<bool, const GameProgressData*>
        isLaunchHomeProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return true;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isCrashHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return false;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isBossAttackedHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return false;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isRepairHomeHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return false;
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isRepairHomeByCrashedBossHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return false;
        });

    static HkReplace<bool, const GameProgressData*>
        isFindKoopaProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return false;
        });

    static HkReplace<bool, const GameProgressData*>
        isBossAttackedHomeProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return false;
        });

    static HkReplace<bool, const GameProgressData*>
        isCrashHomeProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return false;
        });

    static HkReplace<bool, const GameProgressData*>
        isRepairHomeProgressQueryHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return false;
        });

    static HkReplace<bool, const GameProgressData*>
        isRepairHomeByCrashedBossProgressHook = hk::hook::replace([](const GameProgressData* progress) -> bool {
            (void)progress;
            return false;
        });

    static HkReplace<bool, GameDataHolderAccessor, s32>
        isFindKoopaNextHook = hk::hook::replace([](GameDataHolderAccessor accessor, s32 worldId) -> bool {
            (void)accessor;
            (void)worldId;
            return false;
        });

    static HkReplace<bool, const GameDataHolder*, s32>
        gameDataHolderIsFindKoopaNextHook = hk::hook::replace([](const GameDataHolder* holder, s32 worldId) -> bool {
            (void)holder;
            (void)worldId;
            return false;
        });

    // This GameDataFunction symbol is only a tail-branch wrapper, so use replace to avoid an
    // unrelocated trampoline orig jumping into garbage.
    static HkReplace<bool, GameDataHolderAccessor, s32>
        isBossAttackedHomeNextHook = hk::hook::replace([](GameDataHolderAccessor accessor, s32 worldId) -> bool {
            if (isOdysseyFixedFacadeActive())
                return false;
            return accessor.mData && accessor->isBossAttackedHomeNext(worldId);
        });

    static HkTrampoline<bool, const GameDataHolder*, s32>
        gameDataHolderIsBossAttackedHomeNextHook = hk::hook::trampoline([](const GameDataHolder* holder, s32 worldId) -> bool {
            if (isOdysseyFixedFacadeActive())
                return false;
            return gameDataHolderIsBossAttackedHomeNextHook.orig(holder, worldId);
        });

    static HkReplace<bool, GameDataHolderAccessor>
        isFindKoopaHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> bool {
            (void)accessor;
            return false;
        });

    static HkReplace<void, GameDataHolderWriter>
        activateHomeHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        launchHomeHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        findKoopaHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        crashHomeHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        bossAttackHomeHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        repairHomeHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameDataHolderWriter>
        repairHomeByCrashedBossHook = hk::hook::replace([](GameDataHolderWriter writer) -> void {
            applyOdysseyFixedLook(writer);
            normalizeStoryProgress(GameDataFunction::getGameDataFile(writer));
        });

    static HkReplace<void, GameProgressData*, s32, s32>
        checkAndChangeCorrectStatusHook = hk::hook::replace([](GameProgressData* progress, s32 worldId, s32 nextScenarioNo) -> void {
            // Do not retain progress across this call. The same GameProgressData
            // allocation is reused for other save slots, so any process-global
            // completion marker can bleed Cloud's boss clear into a new save.
            (void)worldId;
            (void)nextScenarioNo;
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        activateHomeProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        launchHomeProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        findKoopaProgressMutateHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        crashHomeProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        bossAttackHomeProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        repairHomeProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*>
        repairHomeByCrashedBossProgressHook = hk::hook::replace([](GameProgressData* progress) -> void {
            normalizeStoryProgress(progress);
        });

    static HkReplace<void, GameProgressData*, s32>
        unlockNextWorldProgressHook = hk::hook::replace([](GameProgressData* progress, s32 worldId) -> void {
            (void)worldId;
            normalizeStoryProgress(progress);
        });

    static HkTrampoline<void, KoopaStateDeadAndDemoBattleEnd*>
        koopaBattleEndKillHook = hk::hook::trampoline([](KoopaStateDeadAndDemoBattleEnd* state) -> void {
            GameDataHolder* holder = tryGetKoopaBattleEndHolder(state);
            const bool shouldWarpHome = isCloudCurrentWorld(holder);
            GameDataFile* file = holder ? holder->getGameDataFile() : nullptr;
            const s32 world = file ? file->getCurrentWorldIdNoDevelop() : -1;

            koopaBattleEndKillHook.orig(state);

            if (file)
                Bingo::recordEvent({ Bingo::GoalType::BossEncounters, 1, -1,
                    0x61000000u | static_cast<u32>(world & 0xFF),
                    Bingo::EventOrigin::Local });

            if (shouldWarpHome)
                requestCloudPostBossHomeWarp(holder);
        });

    static HkReplace<void, GameDataHolderWriter, s32>
        unlockWorldHook = hk::hook::replace([](GameDataHolderWriter writer, s32 worldId) -> void {
            GameDataFile* file = GameDataFunction::getGameDataFile(writer);
            const s32 logicalWorldId = worldId;
            GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
            const bool wasUnlocked = isProgressionWorldUnlocked(progress, logicalWorldId);
            const bool wasPendingUnlock = progress && sPendingLockedProgress == progress
                && isPendingLockedProgressionWorld(logicalWorldId);
            Randomizer::logPush(
                "UNLOCK writer request L%d/N%d was=%d pending=%d demo=%d",
                logicalWorldId, toGameWorldId(logicalWorldId), wasUnlocked,
                wasPendingUnlock, sProgressionUnlockDemoDepth);

            const bool branchCommitted = wasPendingUnlock
                && KingdomProgress::commitBranchChoice(file, toGameWorldId(worldId));
            if (wasPendingUnlock)
                clearPendingLockedProgressionWorlds();
            syncProgressionWorlds(file);
            const bool didUnlockRequested = !wasUnlocked && isProgressionWorldUnlocked(progress, logicalWorldId);
            Randomizer::logPush(
                "UNLOCK writer result L%d branch=%d did=%d now=%d pendingCount=%d",
                logicalWorldId, branchCommitted, didUnlockRequested,
                isProgressionWorldUnlocked(progress, logicalWorldId),
                sPendingLockedWorldCount);
        });

    static HkReplace<void, GameDataFile*, s32>
        unlockWorldFileHook = hk::hook::replace([](GameDataFile* file, s32 worldId) -> void {
            const s32 logicalWorldId = worldId;
            GameProgressData* progress = file ? file->getGameProgressData() : nullptr;
            const bool wasUnlocked = isProgressionWorldUnlocked(progress, logicalWorldId);
            const bool wasPendingUnlock = progress && sPendingLockedProgress == progress
                && isPendingLockedProgressionWorld(logicalWorldId);
            Randomizer::logPush(
                "UNLOCK file request L%d/N%d was=%d pending=%d demo=%d",
                logicalWorldId, toGameWorldId(logicalWorldId), wasUnlocked,
                wasPendingUnlock, sProgressionUnlockDemoDepth);

            const bool branchCommitted = wasPendingUnlock
                && KingdomProgress::commitBranchChoice(file, toGameWorldId(worldId));
            if (wasPendingUnlock)
                clearPendingLockedProgressionWorlds();
            syncProgressionWorlds(file);
            const bool didUnlockRequested = !wasUnlocked && isProgressionWorldUnlocked(progress, logicalWorldId);
            Randomizer::logPush(
                "UNLOCK file result L%d branch=%d did=%d now=%d pendingCount=%d",
                logicalWorldId, branchCommitted, didUnlockRequested,
                isProgressionWorldUnlocked(progress, logicalWorldId),
                sPendingLockedWorldCount);
        });

    static HkTrampoline<void, GameDataFile*, s32>
        addPayShineFileHook = hk::hook::trampoline([](GameDataFile* file, s32 count) -> void {
            const bool allowed = canDepositInCurrentWorld(file);
            Randomizer::logPush("DEPOSIT addPay count=%d allowed=%d", count, allowed);
            logGlobeState("addPay-before", file);
            if (!allowed) {
                Randomizer::logPush("DEPOSIT addPay BLOCKED");
                return;
            }
            addPayShineFileHook.orig(file, count);
            logGlobeState("addPay-after-orig", file);
            const bool completed = tryCompleteAcceptedGlobe(file);
            Randomizer::logPush("DEPOSIT addPay completed=%d", completed);
            syncProgressionWorlds(file);
        });

    static HkTrampoline<void, GameDataFile*>
        addPayShineCurrentAllFileHook = hk::hook::trampoline([](GameDataFile* file) -> void {
            const bool allowed = canDepositInCurrentWorld(file);
            Randomizer::logPush("DEPOSIT addAll allowed=%d", allowed);
            logGlobeState("addAll-before", file);
            if (!allowed) {
                Randomizer::logPush("DEPOSIT addAll BLOCKED");
                return;
            }
            addPayShineCurrentAllFileHook.orig(file);
            logGlobeState("addAll-after-orig", file);
            const bool completed = tryCompleteAcceptedGlobe(file);
            Randomizer::logPush("DEPOSIT addAll completed=%d", completed);
            syncProgressionWorlds(file);
        });

    static HkReplace<s32, const GameProgressData*>
        calcNextLockedWorldNumForWorldMapHook = hk::hook::replace([](const GameProgressData* progress) -> s32 {
            const s32 result = calcNextLockedProgressionWorldNum(progress);
            Randomizer::logPush(
                "MAP calcNext count progress=%llx owner=%llx -> %d [%d,%d]",
                (unsigned long long)(uintptr_t)progress,
                (unsigned long long)(uintptr_t)sPendingLockedProgress,
                result, sPendingLockedWorlds[0], sPendingLockedWorlds[1]);
            return result;
        });

    static HkReplace<s32, const GameProgressData*, s32>
        calcNextLockedWorldIdForWorldMapHook = hk::hook::replace([](const GameProgressData* progress, s32 index) -> s32 {
            const s32 result = calcNextLockedProgressionWorldId(progress, index);
            Randomizer::logPush(
                "MAP calcNext id progress=%llx index=%d -> L%d count=%d",
                (unsigned long long)(uintptr_t)progress, index, result,
                sPendingLockedWorldCount);
            return result;
        });

    void resetTravelFacadeForStageChange() {
        restorePersistentFixedHomeStatusMask();
        restorePersistentTravelFlagMask();
        restorePersistentWorldWarpTalkMask();
        clearPendingLockedProgressionWorlds();
        sWorldMapTravelFacadeActive = false;
        sCurrentWorldIdForTravelState = -1;
        sGlobeCompletionFile = nullptr;
        sGlobeCompletionWorldId = -1;
        sGlobeStartEvaluationFile = nullptr;
        if (!sCurrentWorldUnlocked && sGlobePatch)
            sGlobePatch->uninstall();
        sCurrentWorldUnlocked = true;
    }

    void requestOdysseyArrivalWithoutExitDemo(const char* stageName) {
        if (!stageName || !stageName[0]) {
            sNoDemoOdysseyArrivalStage[0] = '\0';
            return;
        }

        std::strncpy(sNoDemoOdysseyArrivalStage, stageName,
            sizeof(sNoDemoOdysseyArrivalStage) - 1);
        sNoDemoOdysseyArrivalStage[sizeof(sNoDemoOdysseyArrivalStage) - 1] = '\0';
    }

    static bool getForcedStartStageImpl(
        GameDataHolderAccessor accessor,
        const char** outStageName,
        s32* outScenario,
        s32* outWorldId,
        bool isNewSave) {
        if (outStageName)
            *outStageName = nullptr;
        if (outScenario)
            *outScenario = -1;
        if (outWorldId)
            *outWorldId = -1;

        if (!accessor.mData)
            return false;

        GameDataFile* file = accessor.mData->getGameDataFile();
        if (!file)
            return false;

        // A randomized starting position needs the same redirect machinery even
        // when Kingdom Order is off: the save still has to open somewhere other
        // than the vanilla default start.
        if ((!Randomizer::isKingdomRandoEnabled() && !StartPositionRando::isActive())
            || (!isNewSave && !shouldForceStartWorldOnly(file)))
            return false;

        if (!isNewSave) {
            // shouldForceStartWorldOnly() describes progression state: it remains true
            // until the starting kingdom is completed. A generic redirect is only
            // appropriate before the save has entered any kingdom at all. Once
            // currentWorldId is valid, redirecting here would hijack every sub-area,
            // checkpoint, and Odyssey transition made from the starting kingdom.
            const s32 currentWorldId = file->getCurrentWorldId();
            if (isValidGameWorldId(currentWorldId)) {
                Randomizer::logPush(
                    "Start stage redirect skipped: current world already initialized (%d)",
                    currentWorldId);
                return false;
            }
        }

        const s32 startingWorldId = getStartingProgressionWorldId();
        const WorldMeta::Entry* entry = WorldMeta::getByWorldId(startingWorldId);
        if (!entry || !entry->mainStage)
            return false;

        // Randomized Starting Position replaces only the stage: the world id stays
        // the starting kingdom's, which is what a sub-area start needs anyway
        // (GameDataFile::startStage cannot derive a world from a sub-stage name).
        const char* startStageName = entry->mainStage;
        StartPositionRando::Spawn spawn = {};
        if (StartPositionRando::getSpawn(&spawn) && spawn.stage && spawn.stage[0])
            startStageName = spawn.stage;

        // A sub-stage has no scenario table of its own; fall back to the kingdom's.
        s32 scenario = ScenarioPatcher::normalizeScenarioForStage(accessor, startStageName, -1, false);
        if (scenario <= 0 && startStageName != entry->mainStage)
            scenario = ScenarioPatcher::normalizeScenarioForStage(accessor, entry->mainStage, -1, false);
        if (scenario <= 0)
            scenario = 1;

        if (outStageName)
            *outStageName = startStageName;
        if (outScenario)
            *outScenario = scenario;
        if (outWorldId)
            *outWorldId = toGameWorldId(startingWorldId);

        return true;
    }

    bool getForcedStartStage(GameDataHolderAccessor accessor, const char** outStageName, s32* outScenario, s32* outWorldId) {
        return getForcedStartStageImpl(accessor, outStageName, outScenario, outWorldId, false);
    }

    // GameDataFile::startStage resolves mCurrentWorldId through
    // WorldList::tryFindWorldIndexByMainStageName and, when that misses (every
    // sub-stage name does), leaves the field exactly as it found it. So a start
    // inside a sub-area has to be pinned to its kingdom here, BEFORE the stage
    // loads - otherwise the scene comes up with world -1 and
    // Shine::initAfterPlacement crashes inside WorldList::getMainStageName(-1).
    // Same repair the Soft Nuzlocke reroll makes, minus the by-hand main-stage
    // lookup: the kingdom is already known here.
    // Keyed on the crash condition itself - an invalid current world - and on the
    // SETTING rather than on StartPositionRando::isActive(). Both matter:
    //
    //  - the redirect that knows the start node is in a sub-area sits behind
    //    several gates (progression-started flag, KingdomProgress load, an
    //    already-valid world id), and each is a way to reach actor init at -1;
    //  - isActive() is re-derived by StartPositionRando::rebuild on every
    //    Randomizer::reinit(), including the one at the top of the stage-scene
    //    init hook. If that re-derivation comes up empty the save is still sitting
    //    in the sub-area the new-save path already committed to, so the repair has
    //    to work without it.
    //
    // Testing the field directly cannot hijack anything: a -1 world crashes
    // regardless, and the starting kingdom is the only sane value at that point.
    static bool repairCurrentWorldIdForStartPosition(GameDataFile* file) {
        if (!file
            || Randomizer::getStartPositionMode() == Randomizer::StartPositionMode_Vanilla)
            return false;
        if (isValidGameWorldId(file->getCurrentWorldId()))
            return false;

        const s32 gameWorldId = toGameWorldId(getStartingProgressionWorldId());
        if (!isValidGameWorldId(gameWorldId))
            return false;

        file->mCurrentWorldId = gameWorldId;
        file->mCurrentWorldIdForWrite = gameWorldId;
        file->mCurrentWorldIdForDisp = gameWorldId;
        const char* startNodeId = StartPositionRando::getStartNodeId();
        Randomizer::logPush("start position: pinned current world %d for start %s",
            gameWorldId, startNodeId ? startNodeId : "(inactive)");
        return true;
    }

    static bool prepareStartStageImpl(
        GameDataHolderWriter writer,
        const char** outStageName,
        s32* outScenario,
        bool isNewSave) {
        if (outStageName)
            *outStageName = nullptr;
        if (outScenario)
            *outScenario = -1;

        if (!writer.mData)
            return false;

        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (!file)
            return false;

        if (!KingdomProgress::get(file)) {
            Randomizer::logPush("progress load missing during stage prepare file=%llx",
                (unsigned long long)(uintptr_t)file);
            return false;
        }
        syncProgressionWorlds(file);

        const char* forcedStageName = nullptr;
        s32 forcedScenario = -1;
        if (!getForcedStartStageImpl(
                GameDataHolderAccessor(writer.mData),
                &forcedStageName,
                &forcedScenario,
                nullptr,
                isNewSave))
            return false;

        if (outStageName)
            *outStageName = forcedStageName;
        if (outScenario)
            *outScenario = forcedScenario;

        // Randomized Starting Position. A node that IS a loading zone is placed
        // through that zone's own authored PlayerStart, because its recorded
        // coordinates are the trigger volume and standing on them would re-fire
        // the transition. Every other node is a bare coordinate, which
        // EntranceHooks turns into a synthetic PlayerStartInfo under a reserved
        // start id. Either way the id it hands back is what ChangeStageInfo needs.
        StartPositionRando::Spawn spawn = {};
        const bool hasStartPosition = StartPositionRando::getSpawn(&spawn)
            && spawn.stage && std::strcmp(spawn.stage, forcedStageName) == 0;
        // A node that IS a loading zone travels under that zone's own id so the
        // game resolves its authored PlayerStart; its recorded coordinates are the
        // trigger volume, and standing on them would re-fire the transition. Every
        // other node travels under the reserved id and gets a synthetic spawn.
        const char* startEntranceId
            = hasStartPosition && spawn.preferEntranceId ? spawn.entranceId : "";
        const char* forcedStartId
            = hasStartPosition ? EntranceHooks::getStartPositionStartId(startEntranceId) : "";

        GameDataHolderAccessor accessor(writer.mData);
        const char* nextStageName = GameDataFunction::getNextStageName(accessor);
        const bool alreadyPointed = nextStageName
            && std::strcmp(nextStageName, forcedStageName) == 0
            && GameDataFunction::calcNextScenarioNo(accessor) == forcedScenario;

        if (!alreadyPointed) {
            ChangeStageInfo info(writer.mData, forcedStartId, forcedStageName, false, forcedScenario,
                ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
            ScenarioPatcher::normalizeChangeStageInfo(file, &info, 0);
            GameDataFunction::tryChangeNextStage(writer, &info);
        }

        if (hasStartPosition) {
            // After the stage change, never before: changeNextStage's hook clears
            // any pending arrival, so arming first would be thrown away.
            EntranceHooks::armStartPositionArrival(startEntranceId,
                spawn.x, spawn.y, spawn.z, spawn.hasCoordinates);
            // A kingdom's first-arrival cutscene walks Mario back to the Odyssey
            // after the spawn lands, so ask for the position to be re-applied
            // once it hands control back.
            StartPositionRando::armPendingReposition();
            // Not during new-save setup: pinning there makes currentWorldId valid,
            // which is exactly what getForcedStartStageImpl reads to decide the
            // save already entered a kingdom - the redirect (and this arrival)
            // would then never fire at StageScene::init. The scene-init hook calls
            // repairStartPositionWorldId unconditionally, so the pin is not
            // dependent on reaching this line.
            if (!isNewSave)
                repairCurrentWorldIdForStartPosition(file);
        }
        syncProgressionWorlds(file);
        return true;
    }

    void repairStartPositionWorldId(GameDataHolderWriter writer) {
        if (!writer.mData)
            return;
        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (repairCurrentWorldIdForStartPosition(file))
            syncProgressionWorlds(file);
    }

    bool prepareStartStage(GameDataHolderWriter writer, const char** outStageName, s32* outScenario) {
        return prepareStartStageImpl(writer, outStageName, outScenario, false);
    }

    bool prepareNewSaveStartStage(GameDataHolderWriter writer, const char** outStageName, s32* outScenario) {
        return prepareStartStageImpl(writer, outStageName, outScenario, true);
    }

    // Chaos randomization can hand a save a stage or a scenario that crashes on
    // entry, and the save resumes exactly where it left off -- which is the one
    // place the player cannot get back out of to switch the categories off. So
    // while any chaos category is live, loading a file opens in the first
    // kingdom of the kingdom order instead of the kingdom the save was in. The
    // Misc Randomization page's Force Disable Chaos Options is right there, and
    // turning it on also turns this redirect off, so the next load resumes
    // normally.
    //
    // Only the destination moves: progression, moon counts, and the save's own
    // settings are untouched, and the redirect is not written back to the card
    // any differently than an ordinary stage change would be.
    bool redirectLoadForChaosRandomization(GameDataHolderWriter writer) {
        if (!Randomizer::isChaosRandomizationActive() || !writer.mData)
            return false;

        GameDataFile* file = GameDataFunction::getGameDataFile(writer);
        if (!file)
            return false;

        const s32 startingWorldId = getStartingProgressionWorldId();
        const WorldMeta::Entry* entry = WorldMeta::getByWorldId(startingWorldId);
        if (!entry || !entry->mainStage)
            return false;

        GameDataHolderAccessor accessor(writer.mData);
        s32 scenario = ScenarioPatcher::normalizeScenarioForStage(
            accessor, entry->mainStage, -1, false);
        if (scenario <= 0)
            scenario = 1;

        ChangeStageInfo info(writer.mData, "", entry->mainStage, false, scenario,
            ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
        ScenarioPatcher::normalizeChangeStageInfo(file, &info, 0);
        GameDataFunction::tryChangeNextStage(writer, &info);
        Randomizer::logPush(
            "chaos load redirect: opening in %s/%d instead of world %d",
            entry->mainStage, scenario, file->getCurrentWorldId());
        return true;
    }

    static HkReplace<const char*, GameDataHolderAccessor>
        getNextStageNameHook = hk::hook::replace([](GameDataHolderAccessor accessor) -> const char* {
            GameDataFile* file = tryGetGameDataFile(accessor);
            const char* origName = file ? file->mNextStageName.cstr() : "";
            if (!file || !origName || !Randomizer::isKingdomRandoEnabled())
                return origName;

            // This override exists for Odyssey progression travel (including the
            // vanilla hardcoded Wooded/Lake -> Cloud leg). A main-stage name is not
            // enough to identify that transition: paintings, checkpoint reloads,
            // and entrance-rando home arrivals all legitimately target main stages.
            if (!file->isPlayDemoWorldWarp()
                || file->isPlayDemoWorldWarpHole()
                || file->isWarpCheckpoint()
                || file->isPlayDemoReturnToHome())
                return origName;

            // Only override if the original destination was a main kingdom stage
            // (e.g., the hardcoded Wooded->Cloud departure warp)
            const WorldMeta::Entry* origEntry = WorldMeta::findByMainStage(origName);
            if (!origEntry)
                return origName;

            const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
            const s32 frontier = KingdomProgress::getFirstIncompleteIndex(file);
            if (list && frontier >= 0 && frontier < KingdomProgress::cKingdomCount) {
                const WorldMeta::Entry* entry = WorldMeta::getByWorldId(list->entries[frontier].kingdomId);
                if (entry && entry->mainStage) {
                    Randomizer::logPush("KFR Odyssey destination: %s -> %s",
                        origName, entry->mainStage);
                    return entry->mainStage;
                }
            }

            return origName;
        });

    void clearGravestoneState() {
        PurpleCoinRewards::clearNpcActorPlacements();
        for (s32 i = 0; i < sGravestoneCount; i++)
            sGravestoneActors[i] = nullptr;
        sGravestoneCount = 0;
    }

    // onPlacementActorCreated is fed by EnemySwapHooks' hook on
    // al::createPlacementActorFromFactory - but that whole translation unit, and
    // its installHooks call, are compiled out when Chaos Randomization is off
    // (the default). The gravestone was therefore never registered in any build
    // with that feature disabled, which is why no gravestone/near-miss line ever
    // appeared. Own the hook here in that configuration; installing it in both
    // would double-hook the same symbol and corrupt the trampoline backup.
#if !SMOR_FEATURE_CHAOS_ANY
    static HkTrampoline<al::LiveActor*, const al::ActorInitInfo&, const al::PlacementInfo*>
        createPlacementActorHook = hk::hook::trampoline(
            [](const al::ActorInitInfo& initInfo,
                const al::PlacementInfo* placementInfo) -> al::LiveActor* {
                // The Multiplayer capture pool needs to know which capture classes this
                // stage places, and this is the one trampoline on this function - see the
                // sync bridge note in Multiplayer/main.hpp. No-op unless connected.
                mpOnCreatePlacementActor(placementInfo);

                CoinRando::Swap coinSwap = {};
                const char* objectId = nullptr;
                const char* unitConfigName = nullptr;
                const GameDataHolderAccessor accessor(
                    static_cast<GameDataHolder*>(
                        initInfo.actorSceneInfo.gameDataHolder));
                const char* currentStage = accessor.mData
                    ? GameDataFunction::getCurrentStageName(accessor)
                    : nullptr;
                const s16 currentScenario = accessor.mData
                        && accessor.mData->getGameDataFile()
                    ? accessor.mData->getGameDataFile()->mScenarioNoPlacement
                    : -1;
                const bool coinSwapActive = placementInfo
                    && placementInfo->getPlacementIter().tryGetStringByKey(
                        &objectId, "Id")
                    && placementInfo->getPlacementIter().tryGetStringByKey(
                        &unitConfigName, "UnitConfigName")
                    && CoinRando::resolveSwap(
                        *placementInfo, currentStage, currentScenario,
                        &coinSwap);
                if (coinSwapActive)
                    CoinRando::beginIdentityScope(
                        placementInfo, objectId, unitConfigName, coinSwap);
                al::LiveActor* actor = createPlacementActorHook.orig(initInfo, placementInfo);
                if (coinSwapActive)
                    CoinRando::endIdentityScope();
                onPlacementActorCreated(actor, placementInfo);
                mpOnPlacementActorCreated(actor, placementInfo);
                return actor;
            });
#endif

    void installHooks() {
        getNextStageNameHook.installAtSym<"_ZN16GameDataFunction16getNextStageNameE22GameDataHolderAccessor">();
        FindUnlockShineNumHook.installAtSym<"_ZNK14GameDataHolder18findUnlockShineNumEPbi">();
        getSystemMessageStringHook.installAtSym<"_ZN2al22getSystemMessageStringEPKNS_17IUseMessageSystemEPKcS4_">();
        sendMsgCapTouchWallHook.installAtMainOffset(0x0057eb0c);
        shineTowerRocketTryStartDemoHook.installAtSym<"_ZN16ShineTowerRocket12tryStartDemoEv">();
        shineTowerRocketDemoSelectGoOtherWorldHook.installAtSym<"_ZN16ShineTowerRocket25exeDemoSelectGoOtherWorldEv">();
        getCurrentShineNumHook.installAtSym<"_ZN16GameDataFunction18getCurrentShineNumE22GameDataHolderAccessor">();
        isPlayDemoWorldWarpHook.installAtSym<"_ZN16GameDataFunction19isPlayDemoWorldWarpE22GameDataHolderAccessor">();
        isPlayDemoWorldWarpHoleHook.installAtSym<"_ZN16GameDataFunction23isPlayDemoWorldWarpHoleE22GameDataHolderAccessor">();
        isForwardWorldWarpDemoHook.installAtSym<"_ZN16GameDataFunction22isForwardWorldWarpDemoE22GameDataHolderAccessor">();

#if !SMOR_FEATURE_CHAOS_ANY
        createPlacementActorHook.installAtSym<
            "_ZN2al31createPlacementActorFromFactoryERKNS_13ActorInitInfoEPKNS_13PlacementInfoE">();
#endif
        talkMessageStartForSystemHook.installAtMainOffset(0x20da18);
        tryGetWorldNameForWorldMapHook.installAtMainOffset(0x52a24c);
        worldMapMovieUpdaterUpdateHook.installAtMainOffset(0x508e84);
        getPayShineNumHook.installAtSym<"_ZN16GameDataFunction14getPayShineNumE22GameDataHolderAccessor">();
        getScenarioNoForSkyShineCounterHook.installAtMainOffset(0x52824C);
        shineCounterBuildHook.installAtMainOffset(0x202D20);
        saveObjInfoIsOnHook.installAtSym<"_ZNK11SaveObjInfo4isOnEv">();

        sGlobePatch = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"mov w0, wzr", true>());
        sGlobeStartFindKoopaPatch = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"mov w0, wzr", true>());
        sGlobeStartSkyNoStartPatch = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"mov w0, wzr", true>());
        sGlobeStartInvalidChangeStagePatch = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"mov w0, wzr", true>());
        shineTowerRocketControlHook.installAtSym<"_ZN16ShineTowerRocket7controlEv">();
        shineTowerRocketInitHook.installAtSym<"_ZN16ShineTowerRocket4initERKN2al13ActorInitInfoE">();
        shineTowerRocketReceiveMsgHook.installAtSym<"_ZN16ShineTowerRocket10receiveMsgEPKN2al9SensorMsgEPNS0_9HitSensorES5_">();
        shineTowerRocketDemoPrepareHook.installAtSym<"_ZN16ShineTowerRocket14exeDemoPrepareEv">();
        shineTowerRocketDemoWalkPlayerToPointHook.installAtSym<"_ZN16ShineTowerRocket24exeDemoWalkPlayerToPointEv">();
        churchDoorInitHook.installAtSym<"_ZN10ChurchDoor4initERKN2al13ActorInitInfoE">();
        churchDoorReceiveMsgHook.installAtSym<"_ZN10ChurchDoor10receiveMsgEPKN2al9SensorMsgEPNS0_9HitSensorES5_">();
        isPlayDemoReturnToHomeHook.installAtSym<"_ZN16GameDataFunction22isPlayDemoReturnToHomeE22GameDataHolderAccessor">();
        isPlayDemoAwardSpecialHook.installAtSym<"_ZN16GameDataFunction22isPlayDemoAwardSpecialE22GameDataHolderAccessor">();
        isPlayDemoLavaEruptHook.installAtSym<"_ZN16GameDataFunction19isPlayDemoLavaEruptE22GameDataHolderAccessor">();
        isPlayDemoPlayerDownForBattleKoopaAfterHook.installAtSym<"_ZN16GameDataFunction39isPlayDemoPlayerDownForBattleKoopaAfterE22GameDataHolderAccessor">();
        isTalkedCapNearHomeInWaterfallHook.installAtSym<"_ZN16GameDataFunction30isTalkedCapNearHomeInWaterfallEPKN2al9LiveActorE">();
        isActivateHomeHook.installAtSym<"_ZN16GameDataFunction14isActivateHomeE22GameDataHolderAccessor">();
        isActivateHomeProgressHook.installAtSym<"_ZNK16GameProgressData14isActivateHomeEv">();
        isLaunchHomeHook.installAtSym<"_ZN16GameDataFunction12isLaunchHomeE22GameDataHolderAccessor">();
        isLaunchHomeProgressHook.installAtSym<"_ZNK16GameProgressData12isLaunchHomeEv">();
        isCrashHomeHook.installAtSym<"_ZN16GameDataFunction11isCrashHomeE22GameDataHolderAccessor">();
        isBossAttackedHomeHook.installAtSym<"_ZN16GameDataFunction18isBossAttackedHomeE22GameDataHolderAccessor">();
        isRepairHomeHook.installAtSym<"_ZN16GameDataFunction12isRepairHomeE22GameDataHolderAccessor">();
        isRepairHomeByCrashedBossHook.installAtSym<"_ZN16GameDataFunction25isRepairHomeByCrashedBossE22GameDataHolderAccessor">();
        isFindKoopaProgressHook.installAtMainOffset(0x534e24);
        isBossAttackedHomeProgressHook.installAtMainOffset(0x534e34);
        isCrashHomeProgressHook.installAtMainOffset(0x534eac);
        isRepairHomeProgressQueryHook.installAtMainOffset(0x534ed4);
        isRepairHomeByCrashedBossProgressHook.installAtMainOffset(0x534f14);
        isFindKoopaNextHook.installAtMainOffset(0x52c9cc);
        gameDataHolderIsFindKoopaNextHook.installAtMainOffset(0x5305b4);
        isBossAttackedHomeNextHook.installAtSym<"_ZN16GameDataFunction22isBossAttackedHomeNextE22GameDataHolderAccessori">();
        gameDataHolderIsBossAttackedHomeNextHook.installAtSym<"_ZNK14GameDataHolder22isBossAttackedHomeNextEi">();
        isFindKoopaHook.installAtMainOffset(0x52c9d4);
        activateHomeHook.installAtSym<"_ZN16GameDataFunction12activateHomeE20GameDataHolderWriter">();
        launchHomeHook.installAtSym<"_ZN16GameDataFunction10launchHomeE20GameDataHolderWriter">();
        findKoopaHook.installAtMainOffset(0x52c9e0);
        crashHomeHook.installAtSym<"_ZN16GameDataFunction9crashHomeE20GameDataHolderWriter">();
        bossAttackHomeHook.installAtSym<"_ZN16GameDataFunction14bossAttackHomeE20GameDataHolderWriter">();
        repairHomeHook.installAtSym<"_ZN16GameDataFunction10repairHomeE20GameDataHolderWriter">();
        repairHomeByCrashedBossHook.installAtSym<"_ZN16GameDataFunction23repairHomeByCrashedBossE20GameDataHolderWriter">();
        checkAndChangeCorrectStatusHook.installAtMainOffset(0x534d6c);
        activateHomeProgressHook.installAtMainOffset(0x534e54);
        launchHomeProgressHook.installAtMainOffset(0x534e7c);
        findKoopaProgressMutateHook.installAtMainOffset(0x534e94);
        crashHomeProgressHook.installAtMainOffset(0x534ebc);
        bossAttackHomeProgressHook.installAtMainOffset(0x534efc);
        repairHomeProgressHook.installAtSym<"_ZN16GameProgressData10repairHomeEv">();
        repairHomeByCrashedBossProgressHook.installAtSym<"_ZN16GameProgressData23repairHomeByCrashedBossEv">();
        unlockNextWorldProgressHook.installAtMainOffset(0x53511c);
        koopaBattleEndKillHook.installAtMainOffset(0x9dad8);
        unlockWorldHook.installAtSym<"_ZN16GameDataFunction11unlockWorldE20GameDataHolderWriteri">();
        unlockWorldFileHook.installAtMainOffset(0x522aa0);
        addPayShineFileHook.installAtSym<"_ZN12GameDataFile11addPayShineEi">();
        addPayShineCurrentAllFileHook.installAtSym<"_ZN12GameDataFile21addPayShineCurrentAllEv">();
        calcNextLockedWorldNumForWorldMapHook.installAtMainOffset(0x534f98);
        calcNextLockedWorldIdForWorldMapHook.installAtSym<"_ZNK16GameProgressData32calcNextLockedWorldIdForWorldMapEi">();

        // World map travel should be governed by the randomizer's accessible-world list,
        // not vanilla's Hat/Sky story hurry guards.
        hk::hook::a64::assemble<"mov w0, #1">().installAtMainOffset(0x503764); // Cancel from Sky before Moon.
        hk::hook::a64::assemble<"mov w0, #1">().installAtMainOffset(0x503838); // Select Hat before Sand.
        hk::hook::a64::assemble<"mov w0, #1">().installAtMainOffset(0x5039c8); // Select non-Moon from Sky before Moon.

        isUnlockedWorldHook.installAtSym<"_ZN16GameDataFunction15isUnlockedWorldE22GameDataHolderAccessori">();
        isUnlockWorldProgressHook.installAtSym<"_ZNK16GameProgressData13isUnlockWorldEi">();
        isAlreadyGoWorldHook.installAtSym<"_ZN16GameDataFunction16isAlreadyGoWorldE22GameDataHolderAccessori">();
        isAlreadyGoWorldProgressHook.installAtSym<"_ZNK16GameProgressData16isAlreadyGoWorldEi">();
        isExistHomeHook.installAtMainOffset(0x52c2c0);
        getUnlockWorldNumForWorldMapActorHook.installAtMainOffset(0x52af6c);
        getUnlockWorldIdForWorldMapActorHook.installAtMainOffset(0x52ae9c);
    }

} // namespace ProgressionHooks
