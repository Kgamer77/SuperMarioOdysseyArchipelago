#pragma once

#include "game/System/FixedHeapArray.h"

#include <basis/seadTypes.h>
#include <container/seadPtrArray.h>
#include <math/seadVector.h>
#include <prim/seadBitFlag.h>
#include <prim/seadSafeString.h>
#include <stream/seadStream.h>

#include "Library/Base/StringUtil.h"
#include "Library/Placement/PlacementId.h"
#include "Npc/SessionEventProgress.h"
#include "Npc/SessionMusicianType.h"
#include "System/UniqObjInfo.h"
#include "Util/ScenePrepoFunction.h"

constexpr s32 sNumWorlds = 20;

namespace al {
    struct ActorInitInfo;
    class ByamlWriter;
    class IUseSceneObjHolder;
    class PlacementId;
    class PlacementInfo;
} // namespace al

namespace ShopItem {
    struct ItemInfo;
}

struct CollectBgm;
class AchievementSaveData;
class BossSaveData;
class ChangeStageInfo;
class GameDataHolder;
class GameProgressData;
class HintPhotoData;
class MoonRockData;
class NetworkUploadFlag;
class PlayerHitPointData;
class QuestInfo;
class RaceRecord;
class SearchAmiiboDataTable;
class SequenceDemoSkipData;
class SessionMusicianType;
class ShineInfo;
class ShopTalkData;
class SphinxQuizData;
class TimeBalloonSaveData;
class VisitStageData;
class WorldWarpTalkData;

class GameDataFile {
public:
    enum class CapStatus
    {
        None,
        Removed,
        GotBack
    };

    enum RaceType
    {
        RaceType_None,
        RaceType_Flag,
        RaceType_Yukimaru,
        RaceType_YukimaruTutorial,
    };

    enum RaceResult
    {
        RaceResult_None,
        RaceResult_Win,
        RaceResult_Lose,
        RaceResult_Second,
        RaceResult_Third,
        RaceResult_Cancel,
    };

    enum HintStatus
    {
        HintStatus_Lock,
        HintStatus_UnlockByNpc,
        HintStatus_UnlockByAmiibo,
    };

    enum AchievementStatus
    {
        AchievementStatus_None,
        AchievementStatus_Unlocked,
        AchievementStatus_Got,
    };

    struct SaveObjS32 {
        UniqObjInfo objInfo;
        s32 value;
    };

    static_assert(sizeof(SaveObjS32) == 0x138);

    struct GrowFlower {
        sead::FixedSafeString<128> potObj;
        sead::FixedSafeString<128> seedObj;
        u64 time;
        u32 growLevel;
    };

    static_assert(sizeof(GrowFlower) == 0x140);

    struct CollectBgmInfo {
        const char* name = nullptr;
        const char* situationName = nullptr;
        bool isCollected = false;

        CollectBgmInfo() = default;

        CollectBgmInfo(const char* name, const char* situationName)
            : name(name)
            , situationName(situationName) { }
    };

    static_assert(sizeof(CollectBgmInfo) == 0x18);

    struct HintInfo {
        void clear();

        bool isDisableByWorldWarpHole(bool is_game_clear) const;
        bool isEnableUnlock(s32 world_id, bool is_moon_rock, s32 scenario_no, bool is_game_clear) const;
        bool isHintStatusUnlock(s32 world_id, s32 scenario_no, bool is_moon_rock) const;
        bool isHintStatusUnlockByNpc() const;
        bool isHintStatusUnlockByAmiibo() const;
        bool isEnableNameUnlockByScenario(s32 world_id, s32 scenario_no, bool is_game_clear) const;

        sead::FixedSafeString<128> stageName;
        sead::FixedSafeString<128> objId;
        const char* scenarioName;
        sead::FixedSafeString<64> objectName;
        sead::Vector3f trans;
        sead::Vector3f originalTrans;
        s64 _1a8[4] = {};
        s32 mainScenarioNo;
        s32 worldId;
        bool isMoonRock;
        bool isGet;
        bool isAchievement;
        bool isGrand;
        bool isShop;
        s32 _1d8;
        s32 hintStatus;
        AchievementStatus status;
        u64 getTime;
        s32 uniqueId;
        s32 hintIdx;
        sead::FixedSafeString<32> optionalId;
        sead::BitFlag32 progressBitFlag;
        bool isDisabled;
        bool isEnableHintInCeremony;
    };

    static_assert(sizeof(HintInfo) == 0x238);

    struct CoinCollectInfo {
        void clear();

        s32 worldId;
        sead::FixedSafeString<128> stageName;
        sead::FixedSafeString<128> objId;
        s32 uniqueId;
        bool isGet;
    };

    static_assert(sizeof(CoinCollectInfo) == 0x140);

    struct CheckpointInfo {
        UniqObjInfo objInfo = {};
        sead::BitFlag32 scenarios = 0;
        sead::Vector3f trans = sead::Vector3f::zero;
        bool isGet = false;
    };

    static_assert(sizeof(CheckpointInfo) == 0x148);

    enum class CountType
    {
        Value_0,
        Value_1,
        Value_2
    };

    GameDataFile(GameDataHolder* game_data_holder);
    bool tryReadByamlData(const u8* data);
    CoinCollectInfo* tryFindCoinCollectInfo(const char* stage_name, const char* obj_id) const;
    s32 tryFindShineIndexByUniqueId(s32 unique_id) const;
    s32 tryFindCoinCollectIndexByUniqueId(s32 unique_id) const;
    void unlockAchievementShineName();
    bool isKidsMode() const;
    s32 getScenarioNo(s32 world_id) const;
    s32 getMainScenarioNo(s32 world_id) const;
    bool isEmpty() const;
    void initializeData();
    void initializeCheckpointTable();
    void generateSaveDataIdForPrepo();
    void resetMapIcon();
    void wearDefault();
    void initializeHintList();
    void initializeCoinCollectList();
    void resetTempData();
    bool isGameClear() const;
    bool isUnlockedWorld(s32 world_id) const;
    bool isAlreadyGoWorld(s32 world_id) const;
    void updateWorldMapIndex();
    void updateWorldWarpIndex();
    void addPlayTime(s32 time, const al::IUseSceneObjHolder*);
    s64 getPlayTimeTotal() const;
    void updateSaveTime();
    void updateSaveTimeForDisp();
    void updateSaveInfoForDisp();
    u64 getLastUpdateTime() const;
    void generateSaveDataIdForPrepoForWrite();
    void resetSaveDataIdForPrepoForWrite();
    void startStage(const char* stage_name, s32 scenario_no);
    bool isRaceStart() const;
    bool checkIsHomeStage(const char* stage_name) const;
    void setGameClear();
    PlayerHitPointData* getPlayerHitPointData() const;
    void startDemoStage(const char* stage_name);
    void changeNextStage(const ChangeStageInfo* info, s32 race_type);
    void changeNextStageWithDemoWorldWarp(const char* stage_name);
    void changeNextStageWithWorldWarpHole(const char* name);
    void returnPrevStage();
    void restartStage();
    s32 calcNextScenarioNo() const;
    const char* getStageNameCurrent() const;
    const char* tryGetStageNameCurrent() const;
    const char* getStageNameNext() const;
    void changeWipeType(const char* type);
    void setActivateHome();
    bool isGoToCeremonyFromInsideHomeShip() const;
    const char* getPlayerStartId() const;
    void setCheckpointId(const al::PlacementId* placement_id);
    void setRestartPointId(const al::PlacementId* placement_id);
    void clearStartId();
    const char* tryGetRestartPointIdString() const;
    void endStage();
    void missAndRestartStage();
    void setMissRestartInfo(const al::PlacementInfo& info);
    bool isUseMissRestartInfo() const;
    bool isFirstTimeNextWorld() const;
    void getAchievement(const char* name);
    bool isGotShine(const ShineInfo* info) const;
    bool isGotShine(s32 index) const;
    bool checkGotShine(const char* obj_id) const;
    void setGotShine(const ShineInfo* info);
    s32 getShineNum() const;
    s32 getShineNum(s32 world_id) const;
    s32 getTotalShineNum() const;
    s32 getTotalShopShineNum() const;
    bool tryGetNextMainScenarioLabel(sead::BufferedSafeString* out_label, sead::BufferedSafeString* out_stage_name) const;
    bool tryGetNextMainScenarioPos(sead::Vector3f* out) const;
    const HintInfo* tryFindNextMainScenarioInfo() const;
    void addPayShine(s32 count);
    void addPayShineCurrentAll();
    s32 getPayShineNum(s32 world_id = -1) const;
    s32 getTotalPayShineNum() const;
    bool isPayShineAllInAllWorld() const;
    void addKey(s32 count);
    s32 getKeyNum() const;
    void addOpenDoorLockNum(s32 count);
    void setStartedObj(const al::PlacementId* placement_id);
    bool isStartedObj(const al::PlacementId* placement_id, const char* stage_name = nullptr) const;
    bool isStartedObj(const char* stage_name, const char* obj_id) const;
    void setSaveObjS32(const al::PlacementId* placement_id, s32 value);
    const SaveObjS32* tryFindSaveObjS32(const al::PlacementId* placement_id);
    bool isExistSessionMember(const SessionMusicianType& type) const;
    void addSessionMember(const SessionMusicianType& type);
    void addCoinCollect(const al::PlacementId* placement_id);
    bool isGotCoinCollect(const al::PlacementId* placement_id) const;
    s32 getCoinCollectNum() const;
    s32 getCoinCollectGotNum() const;
    s32 getCoinCollectGotNum(s32 world_id) const;
    void useCoinCollect(s32 count);
    const char* tryFindExistCoinCollectStageName(s32 world_id) const;
    void payCoinToSphinx();
    bool isPayCoinToSphinx() const;
    void answerCorrectSphinxQuiz();
    void answerCorrectSphinxQuizAll();
    bool isAnswerCorrectSphinxQuiz(s32 index) const;
    bool isAnswerCorrectSphinxQuizAll(s32 index) const;
    bool isTalkAlreadyLocalLanguage() const;
    void talkLocalLanguage();
    bool isFirstWorldTravelingStatus() const;
    void saveWorldTravelingStatus(const char* status);
    const char* getWorldTravelingStatus() const;
    bool isStartWorldTravelingPeach() const;
    void startWorldTravelingPeach();
    void setGrowFlowerTime(const al::PlacementId* pot_placement_id, const al::PlacementId* seed_placement_id, u64 time);
    void setGrowFlowerTime(const al::PlacementId* pot_placement_id, u64 time);
    u64 getGrowFlowerTime(const al::PlacementId* pot_placement_id) const;
    void addGrowFlowerGrowLevel(const al::PlacementId* pot_placement_id, u32 level);
    u32 getGrowFlowerGrowLevel(const al::PlacementId* pot_placement_id) const;
    bool isUsedGrowFlowerSeed(const al::PlacementId* seed_placement_id) const;
    const char* findGrowFlowerPotIdFromSeedId(const al::PlacementId* seed_placement_id);
    void addCoin(s32 count);
    void addPlayerJumpCount();
    s32 getPlayerJumpCount() const;
    void addPlayerThrowCapCount();
    s32 getPlayerThrowCapCount() const;
    bool readFromStream(sead::ReadStream* stream, u8* buffer);
    bool tryReadByamlDataFromStream(sead::ReadStream* stream, u8* buffer, s32 size);
    void writeToStream(sead::WriteStream* stream, sead::Heap* heap) const;
    bool isPlayDemoPlayerDownForBattleKoopaAfter() const;
    s32 getCheckpointNumMaxInWorld() const;
    const sead::Vector3f& getCheckpointTransInWorld(const char* obj_id) const;
    bool isGotCheckpointInWorld(s32 index) const;
    const char* getCheckpointObjIdInWorld(s32 index) const;
    bool isGotCheckpoint(al::PlacementId* placement_id) const;
    void changeNextSceneByGotCheckpoint(s32 index);
    void changeNextSceneByWarp();
    void changeNextSceneByHome();
    void startYukimaruRace();
    void startYukimaruRaceTutorial();
    void startRaceManRace();
    void registerCheckpointTrans(const al::PlacementId* placement_id, const sead::Vector3f& trans);
    s32 calcGetCheckpointNum() const;
    bool isEnableUnlockHint() const;
    s32 calcRestHintNum() const;
    void unlockHint();
    void unlockHintImpl(s32 hint_status);
    void unlockHintAmiibo();
    void unlockHintAddByMoonRock();
    s32 calcHintNum() const;
    const sead::Vector3f& calcHintTrans(s32 index) const;
    const HintInfo& findHint(s32 index) const;
    const sead::Vector3f& calcHintTransMostEasy() const;
    const HintInfo* findHintInfoMostEasy() const;
    s32 calcHintMoonRockNum() const;
    const sead::Vector3f& calcHintMoonRockTrans(s32 index) const;
    const HintInfo& findHintMoonRock(s32 index) const;
    bool tryUnlockShineName(s32 world_id, s32 index);
    bool isOpenMoonRock(s32 world_id) const;
    void calcShineIndexTableNameAvailable(s32*, s32*, s32);
    void calcShineIndexTableNameUnlockable(s32*, s32*, s32);
    bool isUnlockAchievementShineName() const;
    void unlockWorld(s32 world_id);
    void noPlayDemoWorldWarp();
    s32 calcWorldWarpHoleThroughNum() const;
    s32 getWorldWarpHoleThroughNumMax() const;
    void enteredStage();
    void buyDefaultItem();
    void buyItem(const ShopItem::ItemInfo* info, bool is_save_prepo);
    sead::FixedSafeString<64>* tryFindItemList(const ShopItem::ItemInfo* info) const;
    bool isBuyItem(const ShopItem::ItemInfo* info) const;
    bool isBuyItem(const char* name, const sead::FixedSafeString<64>* item_list) const;
    s32 calcHaveClothNum() const;
    s32 calcHaveCapNum() const;
    s32 calcHaveStickerNum() const;
    s32 calcHaveGiftNum() const;
    void buyItemAll();
    void wearCostume(const char* name);
    void wearCap(const char* name);
    void addHackDictionary(const char* hack_name);
    bool isExistInHackDictionary(const char* hack_name) const;
    const HintInfo* findShine(s32 world_id, s32 index) const;
    bool isGotShine(s32 world_id, s32 index) const;
    bool isOpenShineName(s32 world_id, s32 index) const;
    s32 calcShineNumInOneShine(s32 world_id, s32 index) const;
    bool checkAchievementShine(s32 world_id, s32 index) const;
    s32 getWorldTotalShineNum(s32 world_id) const;
    s32 getWorldTotalShineNumMax(s32 world_id) const;
    void winRace();
    RaceRecord* findRaceRecord(const char* name) const;
    void incrementRaceLoseCount(s32 level);
    s32 getRaceLoseCount(s32 level) const;
    void setJumpingRopeBestCount(s32 count);
    void setUpdateJumpingRopeScoreFlag();
    void setVolleyballBestCount(s32 count);
    void setUpdateVolleyballScoreFlag();
    bool isExistJango() const;
    void setJangoTrans(const sead::Vector3f& trans);
    void setAmiiboNpcTrans(const sead::Vector3f& trans);
    void setTimeBalloonNpcTrans(const sead::Vector3f& trans);
    void setPoetterTrans(const sead::Vector3f& trans);
    void setShopNpcTrans(const sead::Vector3f& trans, const char* name, s32 type);
    void setMoonRockTrans(const sead::Vector3f& trans);
    void setMiniGameInfo(const sead::Vector3f& trans, const char* name);
    s32 calcMiniGameNum() const;
    s32 getMiniGameNumMax() const;
    const sead::Vector3f& getMiniGameTrans(s32 index) const;
    const char* getMiniGameName(s32 index) const;
    bool isExistTimeBalloonNpc() const;
    const sead::Vector3f& getTimeBalloonNpcTrans() const;
    bool isExistPoetter() const;
    const sead::Vector3f& getPoetterTrans() const;
    bool isAlreadyShowExplainCheckpointFlag() const;
    void showExplainCheckpointFlag();
    const sead::Vector3f& getShopNpcTrans(s32 index) const;
    bool isShopSellout(s32 index) const;
    s32 calcShopNum() const;
    s32 getShopNpcIconNumMax() const;
    s32 getScenarioNo() const;
    s32 getScenarioNoPlacement() const;
    bool isClearWorldMainScenario(s32 world_id) const;
    s32 calcCheckpointIndexInScenario(s32 index) const;
    bool isFlagOnTalkMessageInfo(s32 index) const;
    void setFlagOnTalkMessageInfo(s32 index);
    bool isTalkKakku() const;
    void talkKakku();
    bool isTalkWorldTravelingPeach() const;
    void talkWorldTravelingPeach();
    bool isTalkCollectBgmNpc() const;
    void talkCollectBgmNpc();
    s32 getTokimekiMayorNpcFavorabilityRating() const;
    void setTokimekiMayorNpcFavorabilityRating(s32 rating);
    bool isFirstNetwork() const;
    void noFirstNetwork();
    void setKidsMode(bool is_kids_mode);
    bool isPlayScenarioCamera(const QuestInfo* info) const;
    bool isNextMainShine(const QuestInfo* info) const;
    bool isNextMainShine(s32 index) const;
    bool isMainShine(s32 index) const;
    bool isLatestGetMainShine(const ShineInfo* info) const;
    bool calcIsGetMainShineAll(const al::IUseSceneObjHolder* scene_obj_holder) const;
    bool calcIsGetShineAllInWorld(s32 world_id) const;
    s32 tryFindLinkedShineIndex(const al::ActorInitInfo& actor_info, const al::IUseSceneObjHolder* scene_obj_holder) const;
    s32 tryFindLinkedShineIndex(const al::ActorInitInfo& actor_info, s32 link_index, const al::IUseSceneObjHolder* scene_obj_holder) const;
    s32 tryFindLinkedShineIndexByLinkName(const al::IUseSceneObjHolder* scene_obj_holder, const al::ActorInitInfo& actor_info, const char* link_name) const;
    s32 calcLinkedShineNum(const al::ActorInitInfo& actor_info) const;
    s32 tryFindShineIndex(const al::ActorInitInfo& actor_info) const;
    s32 tryFindShineIndex(const char* stage_name, const char* obj_id) const;
    void disableHintById(s32 index);
    void enableHintById(s32 index);
    void setStartShine(const ShineInfo* info);
    s32 getStartShineNextIndex() const;
    void setHintTrans(s32 index, const sead::Vector3f& trans);
    void setOriginalHintTrans(s32);
    void resetHintTrans(s32 index);
    void registerShineInfo(const ShineInfo* info, const sead::Vector3f& trans);
    s32 calcRestShineInStageWithWorldProgress(const char* stage_name) const;
    s32 calcGetShineNumByObjectNameOrOptionalId(const char* object_name_or_optional_id, CountType count_type) const;
    s32 calcGetShineNumByObjectNameWithWorldId(const char* object_name, s32 world_id) const;
    s32 calcAllShineNumByObjectNameOrOptionalId(const char* object_name_or_optional_id) const;
    s32 calcGetShineNumByStageName(const char* stage_name) const;
    bool tryFindAndInitShineInfoByOptionalId(ShineInfo* info, const char* optional_id);
    s32 tryFindUniqueId(const ShineInfo* shine_info) const;
    s32 findUnlockShineNumCurrentWorld(bool* out_is_game_clear) const;
    s32 getMainScenarioNoCurrent() const;
    void setMainScenarioNo(s32 scenario_no);
    bool isCollectedBgm(const char* name, const char* situation_name) const;
    const CollectBgm& getCollectBgmByIndex(s32 index) const;
    bool trySetCollectedBgm(const char* name, const char* situation_name);
    s32 getCollectedBgmNum() const;
    s32 getCollectedBgmMaxNum() const;
    void setGotShine(s32 index);
    void setGotShine(const HintInfo* info);
    bool isEnableOpenMoonRock(s32 world_id) const;
    bool tryWriteByByaml(sead::WriteStream* stream, sead::Heap* heap) const;
    bool tryWriteByByaml(al::ByamlWriter* writer) const;

    const ShineInfo* getLatestGetShineInfo() const { return mLatestGetShineInfo; }

    s64 getSaveDataIdForPrepo() const { return mSaveDataIdForPrepo; }

    const char* getCheckpointWarpObjId() const { return mCheckpointWarpObjId.cstr(); }

    s32 getOpenDoorLockNum() const { return mOpenDoorLockNum; }

    s32 getCoinNum() const { return mCoinNum; }

    s32 getTotalCoinNum() const { return mTotalCoinNum; }

    bool isPlayDemoOpening() const { return mIsPlayDemoOpening; }

    bool isEnableCap() const {
        return mIsMeetCap && mIsEnableCap && mCapStatusForJango != CapStatus::Removed && mCapStatusForJangoSubArea != CapStatus::Removed;
    }

    bool isMeetCap() const { return mIsMeetCap; }

    void meetCap() { mIsMeetCap = true; }

    void disableCapByPlacement() { mIsEnableCap = false; }

    const SessionEventProgress& getSessionEventProgress() const { return mSessionEventProgress; }

    void setSessionEventProgress(const SessionEventProgress& progress) { mSessionEventProgress = progress; }

    bool isHintNpcFirstTalk() const { return mIsHintNpcFirstTalk; }

    void hintNpcFirstTalk() { mIsHintNpcFirstTalk = true; }

    bool isKinopioBrigadeNpcFirstTalk() const { return mIsKinopioBrigadeNpcFirstTalk; }

    void kinopioBrigadeNpcFirstTalk() { mIsKinopioBrigadeNpcFirstTalk = true; }

    CapStatus getCapStatusForJango() const { return mCapStatusForJango; }

    void setCapStatusForJango(CapStatus status) { mCapStatusForJango = status; }

    CapStatus getCapStatusForJangoSubArea() const { return mCapStatusForJangoSubArea; }

    void setCapStatusForJangoSubArea(CapStatus status) { mCapStatusForJangoSubArea = status; }

    s32 getJangoCount() const { return mJangoCount; }

    void addJangoCount() { mJangoCount++; }

    TimeBalloonSaveData* getTimeBalloonSaveData() const { return mTimeBalloonSaveData; }

    bool isTalkFirstAmiiboNpc() const { return mIsTalkFirstAmiiboNpc; }

    void offTalkFirstAmiiboNpc() { mIsTalkFirstAmiiboNpc = false; }

    WorldWarpTalkData* getWorldWarpTalkData() const { return mWorldWarpTalkData; }

    VisitStageData* getVisitStageData() const { return mVisitStageData; }

    GameProgressData* getGameProgressData() const { return mGameProgressData; }

    MoonRockData* getMoonRockData() const { return mMoonRockData; }

    NetworkUploadFlag* getNetworkUploadFlag() const { return mNetworkUploadFlag; }

    HintPhotoData* getHintPhotoData() const { return mHintPhotoData; }

    const char* getCurrentCostumeName() const { return mCurrentCostumeName.cstr(); }

    const char* getCurrentCapName() const { return mCurrentCapName.cstr(); }

    bool isCostumeRandom() const { return mIsCostumeRandom; }

    void setCostumeRandom(bool is_costume_random) { mIsCostumeRandom = is_costume_random; }

    bool isCapRandom() const { return mIsCapRandom; }

    void setCapRandom(bool is_cap_random) { mIsCapRandom = is_cap_random; }

    bool isRideSphinx() const { return mIsRideSphinx; }

    void rideSphinx() { mIsRideSphinx = true; }

    bool isRideMotorcycle() const { return mIsRideMotorcycle; }

    void rideMotorcycle() { mIsRideMotorcycle = true; }

    s32 getJumpingRopeBestCount() const { return mJumpingRopeBestCount; }

    s32 getJumpingRopeDayCount() const { return mJumpingRopeDayCount; }

    void setJumpingRopeDayCount(s32 count) { mJumpingRopeDayCount = count; }

    bool isExistRecordJumpingRope() const { return mIsExistRecordJumpingRope; }

    void existRecordJumpingRope() { mIsExistRecordJumpingRope = true; }

    bool isExistRecordInDayJumpingRope() const { return mIsExistRecordInDayJumpingRope; }

    void existRecordInDayJumpingRope() { mIsExistRecordInDayJumpingRope = true; }

    s32 getVolleyballBestCount() const { return mVolleyballBestCount; }

    s32 getVolleyballDayCount() const { return mVolleyballDayCount; }

    void setVolleyballDayCount(s32 count) { mVolleyballDayCount = count; }

    bool isExistRecordVolleyball() const { return mIsExistRecordVolleyball; }

    void existRecordVolleyball() { mIsExistRecordVolleyball = true; }

    bool isExistRecordInDayVolleyball() const { return mIsExistRecordInDayVolleyball; }

    void existRecordInDayVolleyball() { mIsExistRecordInDayVolleyball = true; }

    s32 getNextWorldId() const { return mNextWorldId; }

    s32 getPrevWorldId() const { return mPrevWorldId; }

    void disablePlayDemoPlayerDownForBattleKoopaAfter() { mIsPlayDemoPlayerDownForBattleKoopaAfter = false; }

    bool isAppearCourseName() const { return mIsAppearCourseName; }

    void setStageHakoniwa() { mIsStageHakoniwa = true; }

    s32 getCurrentWorldId() const { return mCurrentWorldId; }

    s32 getCurrentWorldIdNoDevelop() const { return sead::Mathi::max(mCurrentWorldId, 0); }

    bool isPlayDemoWorldWarp() const { return _9f8 == 1; }

    bool isPlayDemoReturnToHome() const { return mIsPlayDemoReturnToHome; }

    void requestPlayDemoReturnToHome() { mIsPlayDemoReturnToHome = true; }

    bool isPlayDemoAwardSpecial() const { return mIsPlayDemoAwardSpecial; }

    void requestPlayDemoAwardSpecial() { mIsPlayDemoAwardSpecial = true; }

    bool isPlayDemoWorldWarpHole() const { return mIsPlayDemoWorldWarpHole; }

    bool isEnterStageFirst() const { return mIsEnterStageFirst; }

    bool isWarpCheckpoint() const { return mIsWarpCheckpoint; }

    bool get_a2c() const { return _a2c; }

    ChangeStageInfo* getChangeStageInfo() const { return mChangeStageInfo; }

    void startKoopaCapture() { mIsStartKoopaCapture = true; }

    bool isStartKoopaCapture() const { return mIsStartKoopaCapture; }

    RaceType getRaceType() const { return mRaceType; }

    RaceResult getRaceResult() const { return mRaceResult; }

    void setRaceResult(RaceResult result) { mRaceResult = result; }

    s32 getRaceRivalLevel() const { return mRaceRivalLevel; }

    void setRaceRivalLevel(s32 level) { mRaceRivalLevel = level; }

    s32 getLastRaceRanking() const { return mLastRaceRanking; }

    void setLastRaceRanking(s32 ranking) { mLastRaceRanking = ranking; }

    ShopTalkData* getShopTalkData() const { return mShopTalkData; }

    s32 getTotalPayShineNumForDisp() const { return mTotalPayShineNumForDisp; }

    s32 getCurrentWorldIdForDisp() const { return mCurrentWorldIdForDisp; }

    const sead::Vector3f& getHomeTrans() const { return mHomeTrans; }

    void setHomeTrans(const sead::Vector3f& trans) {
        mHomeTrans.set(trans);
        mIsExistHome = true;
    }

    void setRaceStartNpcTrans(const sead::Vector3f& trans) {
        mRaceStartNpcTrans.set(trans);
        mIsExistRaceStartNpc = true;
    }

    bool isExistRaceStartNpc() const { return mIsExistRaceStartNpc; }

    const sead::Vector3f& getRaceStartTrans() const { return mRaceStartTrans; }

    void setRaceStartTrans(const sead::Vector3f& trans) {
        mRaceStartTrans.set(trans);
        mIsExistRaceStart = true;
    }

    const sead::Vector3f& getRaceGoalTrans() const { return mRaceGoalTrans; }

    void setRaceGoalTrans(const sead::Vector3f& trans) {
        mRaceGoalTrans.set(trans);
        mIsExistRaceGoal = true;
    }

    const sead::Vector3f& getHintNpcTrans() const { return mHintNpcTrans; }

    void setHintNpcTrans(const sead::Vector3f& trans) {
        mHintNpcTrans.set(trans);
        mIsExistHintNpc = true;
    }

    bool isExistHintNpc() const { return mIsExistHintNpc; }

    const sead::Vector3f& getJangoTrans() const { return mJangoTrans; }

    void disableJangoTrans() { mIsEnableJangoTrans = false; }

    const sead::Vector3f& getAmiiboNpcTrans() const { return mAmiiboNpcTrans; }

    bool isExistAmiiboNpc() const { return mIsExistAmiiboNpc; }

    const sead::Vector3f& getMoonRockTrans() const { return mMoonRockTrans; }

    const HintInfo* getLatestHint() const { return mLatestHint; }

    bool isMissEndPrevStage() const { return mIsMissEndPrevStage; }

    bool isKoopaLv3() const { return mIsKoopaLv3; }

    bool isEnableCapMessageLifeOneKidsMode() const { return mIsEnableCapMessageLifeOneKidsMode; }

    void disableCapMessageLifeOneKidsMode() { mIsEnableCapMessageLifeOneKidsMode = false; }

    sead::FixedSafeString<64>* getCapList() { return mItemCap.begin(); }

    sead::FixedSafeString<64>* getClothList() { return mItemCloth.begin(); }

    sead::FixedSafeString<64>* getStickerList() { return mItemSticker.begin(); }

    sead::FixedSafeString<64>* getGiftList() { return mItemGift.begin(); }

    // custom methods

    // custom impl of findShine that uses shine UID instead of index to get the right HintInfo
    HintInfo* findShine(int shineUid) {
        for (int x = 0; x < 0x400; x++) {
            GameDataFile::HintInfo* curInfo = &mHintList[x];
            if (curInfo->uniqueId == shineUid) {
                return curInfo;
            }
        }
        return nullptr;
    }

    // Moon has two distinct 2D maps: the zone embedded in MoonWorldHomeStage and
    // Galaxy2DExStage. CoinCollectInfo registration can retain any of those stage
    // identities, while the live actor commonly reports MoonWorldHomeStage.
    // Resolve every canonical alias before falling back to the owning world's
    // record. The world fallback is deliberately limited to Moon so object IDs
    // shared by unrelated stages cannot be mistaken for each other.
    CoinCollectInfo* tryFindCoinCollectInfoWithStageAliases(
        const char* stage, const char* objID, int worldID = -1) const {
        if (!stage || !objID)
            return nullptr;

        CoinCollectInfo* info = tryFindCoinCollectInfo(stage, objID);
        if (info)
            return info;

        static constexpr const char* cMoonCoinStages[] = {
            "MoonWorldHomeStage",
            "MoonWorldHome2DZone",
            "Galaxy2DExStage",
        };
        bool isMoonCoinStage = false;
        for (const char* alias : cMoonCoinStages) {
            if (std::strcmp(stage, alias) == 0)
                isMoonCoinStage = true;
        }
        if (!isMoonCoinStage)
            return nullptr;

        for (const char* alias : cMoonCoinStages) {
            if (std::strcmp(stage, alias) == 0)
                continue;
            if (CoinCollectInfo* aliasInfo = tryFindCoinCollectInfo(alias, objID))
                return aliasInfo;
        }

        if (worldID != 13)
            return nullptr;
        for (s32 i = 0; i < mCoinCollectList.size(); i++) {
            CoinCollectInfo* candidate = mCoinCollectList[i];
            if (candidate && candidate->worldId == worldID
                && std::strcmp(candidate->objId.cstr(), objID) == 0)
                return candidate;
        }
        return nullptr;
    }

    bool customHasCoinCollectInfo(const al::PlacementId* placeID, int worldID, const char* stage) const {
        if (!placeID || worldID < 0 || worldID >= sNumWorlds)
            return false;
        al::StringTmp<128> objID;
        placeID->makeString(&objID);
        return tryFindCoinCollectInfoWithStageAliases(stage, objID.cstr(), worldID) != nullptr;
    }

    // custom impl of addCoinCollect that uses a provided stage and world instead of your current ones
    bool customAddCoinCollect(const al::PlacementId* placeID, int worldID, const char* stage) {
        if (worldID < 0 || worldID >= sNumWorlds)
            return false;
        if (!placeID)
            return false;
        al::StringTmp<128> objID;
        placeID->makeString(&objID);
        if (const CoinCollectInfo* existing = tryFindCoinCollectInfoWithStageAliases(stage, objID.cstr(), worldID)) {
            if (existing->isGet)
                return false;
        }
        if (!stage || !stage[0])
            return false;
        if (GameDataFile::CoinCollectInfo* info = tryFindCoinCollectInfoWithStageAliases(stage, objID.cstr(), worldID)) {
            info->isGet = true;
            mCoinCollectGotNum[worldID]++;
            return true;
        }
        return false;
    }

    // custom impl of isGotCoinCollect that uses a provided stage instead of your current one
    bool customIsGotCoinCollect(const al::PlacementId* placeID, const char* stage, int worldID = -1) const {
        if (!placeID)
            return false;
        al::StringTmp<128> objID;
        placeID->makeString(&objID);
        const CoinCollectInfo* info = tryFindCoinCollectInfoWithStageAliases(stage, objID.cstr(), worldID);
        return info && info->isGet;
    }

    s32 customGetCoinCollectMax(int worldID) const {
        if (worldID < 0 || worldID >= mCoinCollectTable.size())
            return 0;
        const WorldCoinCollectList* list = mCoinCollectTable[worldID];
        return list ? list->list.size() : 0;
    }

    s32 customGetCoinCollectGotNum(int worldID) const {
        if (worldID < 0 || worldID >= sNumWorlds)
            return 0;
        return mCoinCollectGotNum[worldID];
    }

    s32 customGetCoinCollectNum(int worldID) const {
        if (worldID < 0 || worldID >= sNumWorlds)
            return 0;
        return mCoinCollectGotNum[worldID] - mUseCoinCollectNum[worldID];
    }

    bool customUseCoinCollect(int worldID, int count) {
        if (worldID < 0 || worldID >= sNumWorlds)
            return false;
        mUseCoinCollectNum[worldID] += count;
        return true;
    }

    // get the total shine count, only including unique shines (no more than 1 shop shine per kingdom)
    s32 getTotalUniqueShineNum() {
        int shines = 0;
        for (s32 i = 0; i < sNumWorlds; i++) {
            shines += mShineNum[i]; // all shines including shop
            shines -= std::max(mShopShineNum[i] - 1, 0); // subtract shop moons except for 1 per kingdom
        }
        return shines;
    }

    s32 customGetTotalShopShineNum() const {
        s32 shines = 0;
        for (s32 i = 0; i < sNumWorlds; i++)
            shines += mShopShineNum[i];
        return shines;
    }

    // Adds a shop moon to an explicitly selected kingdom. This is used by
    // synthetic shop entries that do not have a physical ShineInfo record.
    bool customAddShopShine(s32 worldID, s32 count = 1) {
        if (worldID < 0 || worldID >= sNumWorlds || count <= 0)
            return false;

        mShineNum[worldID] += count;
        mPayShineNum[worldID] += count;
        mShopShineNum[worldID] += count;
        mTotalPayShineNumForDisp += count;
        return true;
    }

    // these three functions are custom impls of checkpoint functions from 0b-0f's decomp of GameDataFile and
    // they are used for checkpoint sync
    // UniqObjInfo* customSetCheckpointId(const al::PlacementId* placement_id) {
    //     UniqObjInfo* result = nullptr;
    //     if (!placement_id)
    //         return result;
    //     al::StringTmp<128> obj_id = "";
    //     placement_id->makeString(&obj_id);
    //     if (CheckpointInfo* info = tryFindCheckpointInfoImpl(mCheckpointTable.begin(),
    //             mCurrentStageName.cstr(), obj_id.cstr())) {
    //         info->isGet = true;
    //         if (UniqObjInfo* got_info = addGotCheckpoint(mGotCheckpoint.begin(), info->objInfo.getStageName(),
    //                 info->objInfo.getObjId())) {
    //             got_info->setStageName(info->objInfo.getStageName());
    //             got_info->mObjId.copy(info->objInfo.getObjId());
    //             mGotCheckpointNum++;
    //             result = got_info;
    //         }
    //     } else {
    //         CheckpointMasterList::CheckpointData data = CheckpointMasterList::getCheckpointDataFromMasterList(obj_id.cstr());
    //         if (UniqObjInfo* got_info = addGotCheckpoint(mGotCheckpoint.begin(), data.stageName, data.objId)) {
    //             got_info->setStageName(data.stageName);
    //             got_info->mObjId.copy(data.objId);
    //             mGotCheckpointNum++;
    //             result = got_info;
    //         }
    //     }
    //     // mCheckpointName.copy(obj_id, 128);
    //     // _290.format("%s", mCurrentStageName.cstr());
    //     // _908.clear();
    //     // mPlayerStartId.clear();
    //     // _160.clear();
    //     return result;
    // }

    // static UniqObjInfo* addGotCheckpoint(UniqObjInfo* list, const char* stage_name, const char* obj_id) {
    //     if (!list || !stage_name || !stage_name[0] || !obj_id || !obj_id[0])
    //         return nullptr;
    //     for (s32 i = 0; i < 320; i++) {
    //         if (list[i].mStageName.isEmpty() && list[i].mObjId.isEmpty())
    //             return &list[i];
    //         if (al::isEqualString(list[i].getStageName(), stage_name) && al::isEqualString(list[i].getObjId(), obj_id))
    //             break;
    //     }
    //     return nullptr;
    // }
    //
    // static CheckpointInfo* tryFindCheckpointInfoImpl(CheckpointInfo** table, const char* stage_name,
    //     const char* obj_id) {
    //     for (s32 i = 0; i < sNumWorlds; i++)
    //         for (s32 j = 0; j < 16; j++)
    //             if (al::isEqualString(stage_name, table[i][j].objInfo.mStageName) && al::isEqualString(obj_id, table[i][j].objInfo.getObjId()))
    //                 return &table[i][j];
    //     return nullptr;
    // }

    // some getters/setters for private member variables
    FixedHeapArray<HintInfo, 1024> getHintList() const { return mHintList; }
    // The acquisition lists GameDataFile::isGotShine(const ShineInfo*) actually
    // scans.  HintInfo::isGet is a parallel record of the same fact, kept in
    // step only because the native setGotShine writes both; anything that edits
    // one behind the game's back has to edit the other or the save ends up
    // telling two different stories about which moons are held.
    FixedHeapArray<ShineInfo*, 1024>& getGotShineList() { return mGotShine; }
    FixedHeapArray<ShineInfo*, 1024>& getGotGrandShineList() { return mGotGrandShine; }
    // Mutable companion to getLatestGetShineInfo().  Emptying this one means
    // clearing what it points at, the way initializeData does - the pointer
    // itself stays valid, and callers that read it do not null-check.
    ShineInfo* customGetLatestGetShineInfo() { return mLatestGetShineInfo; }
    bool& getIsEnableCap() { return mIsEnableCap; }
    // The hint the hint NPC named last.  Vanilla only ever writes it from
    // unlockHintImpl; anything that picks the named hint itself has to write it
    // too, because every "what did he just say" reader resolves through it.
    void setLatestHint(const HintInfo* hint) { mLatestHint = hint; }
    GameDataHolder* getGameDataHolder() { return mGameDataHolder; }
    sead::FixedSafeString<128> getPlayerStartId() { return mPlayerStartId; }
    FixedHeapArray<s32, sNumWorlds>& getScenarioNumArr() { return mScenarioNo; }
    FixedHeapArray<s32, sNumWorlds>& getMainScenarioNumArr() { return mMainScenarioNo; }
    FixedHeapArray<CheckpointInfo*, sNumWorlds>& getCheckpointTable() { return mCheckpointTable; }
    FixedHeapArray<UniqObjInfo, 320>& getGotCheckpointTable() { return mGotCheckpoint; }

    // The retail updateWorldMapIndex/updateWorldWarpIndex methods are not
    // exported by every game build.  Keep the same compact postgame table
    // update local to the custom layout so synthetic audit saves do not create
    // an unresolved call into the game binary.
    void customPrepareWorldTravelTables(s32 worldCount) {
        if (worldCount < 0)
            worldCount = 0;
        if (worldCount > sNumWorlds)
            worldCount = sNumWorlds;

        for (s32 i = 0; i < sNumWorlds; i++) {
            const s32 worldId = i < worldCount ? i : -1;
            mUnlockedWorldId[i] = worldId;
            mWorldMapIndex[i] = worldId;
            mWorldWarpIndex[i] = worldId;
            mIsWorldWarpHoleThrough[i] = i < worldCount;
        }
        mWorldMapNum = worldCount;
        mWorldWarpNum = worldCount;
        mUnlockedWorldNum = worldCount;
    }

    bool customIsStartWorldTravelingPeach() const { return mIsStartWorldTravelingPeach; }
    void customStartWorldTravelingPeach() { mIsStartWorldTravelingPeach = true; }

    // end custom methods

    // custom methods

    // custom impl of findShine that uses shine UID instead of index to get the right HintInfo

    const HintInfo* customFindShine(s32 shineUid, const char* stage) const {
        if (!stage || !stage[0])
            return nullptr;
        for (int x = 0; x < 0x400; x++) {
            const GameDataFile::HintInfo* curInfo = &mHintList[x];
            if (curInfo->uniqueId == shineUid && std::strcmp(curInfo->stageName.cstr(), stage) == 0)
                return curInfo;
        }
        return nullptr;
    }

    HintInfo* customFindShine(s32 shineUid, const char* stage) {
        if (!stage || !stage[0])
            return nullptr;
        for (int x = 0; x < 0x400; x++) {
            GameDataFile::HintInfo* curInfo = &mHintList[x];
            if (curInfo->uniqueId == shineUid && std::strcmp(curInfo->stageName.cstr(), stage) == 0)
                return curInfo;
        }
        return nullptr;
    }

    bool customIsGotShine(s32 shineUid, const char* stage) const {
        const HintInfo* info = customFindShine(shineUid, stage);
        return info && info->isGet;
    }

    // Soft Nuzlocke resets only world progression.  Player inventory, coins,
    // regional-coin collection/spending, records, outfits, and other profile
    // data intentionally remain untouched.
    // The native GameDataFile has no public "new route" reset, so keep the
    // carefully scoped mutable pieces here instead of calling initializeData()
    // and accidentally wiping the profile portion of the save.
    void customResetSoftNuzlockeWorldState() {
        for (s32 i = 0; i < sNumWorlds; i++) {
            mShopShineNum[i] = 0;
            mMainScenarioNo[i] = 0;
            mScenarioNo[i] = 0;
            mPayShineNum[i] = 0;
            mUnlockedWorldId[i] = 0;
            mIsWorldWarpHoleThrough[i] = false;
            mWorldMapIndex[i] = 0;
            mWorldWarpIndex[i] = 0;
            // Regional (purple) coins are world state, like checkpoints and
            // switches: the reroll puts them back in the levels, so the
            // collected/spent ledger has to go back with them or the counter
            // keeps crediting coins that are lying on the ground again.
            mCoinCollectGotNum[i] = 0;
            mUseCoinCollectNum[i] = 0;
            // mTotalShineNum / mTotalMoonRockShineNum are deliberately NOT reset.
            // They are not run progress: initializeHintList() fills them once from
            // ShineInfo with how many moons *exist* per world, and nothing ever
            // recomputes them afterwards. Zeroing them left every kingdom
            // reporting a total of 0, which emptied the collection list and then
            // crashed ShineListLayout::updatePosLayout on a null HintInfo when it
            // scrolled with no valid rows.
        }
        mIsPayShineAllInAllWorld = false;
        mTotalPayShineNumForDisp = 0;
        mUnlockedWorldNum = 1;
        mWorldMapNum = 0;
        mWorldWarpNum = 0;
        mCurrentWorldId = -1;
        mCurrentWorldIdForWrite = -1;
        mCurrentWorldIdForDisp = 0;
        mNextWorldId = 0;
        mPrevWorldId = 0;
        mCheckpointName.clear();
        mCheckpointWarpObjId.clear();
        mPlayerStartId.clear();
        mPlayerStartIdForSave.clear();
        _160.clear();
        _290.clear();
        _908.clear();
        mIsWarpCheckpoint = false;
        mIsUseMissRestartInfo = false;
        mMissRestartInfo = nullptr;
        mGotCheckpointNum = 0;
        for (s32 i = 0; i < 320; i++)
            mGotCheckpoint[i] = {};
        for (s32 world = 0; world < sNumWorlds; world++) {
            for (s32 i = 0; i < 16; i++) {
                CheckpointInfo& checkpoint = mCheckpointTable[world][i];
                checkpoint.scenarios = 0;
                checkpoint.isGet = false;
            }
        }
        for (s32 i = 0; i < 512; i++)
            mStartedObj[i] = {};
        for (s32 i = 0; i < 256; i++)
            mSaveObjS32[i] = {};
        for (s32 i = 0; i < 16; i++)
            mGrowFlower[i] = {};
        for (s32 i = 0; i < mCoinCollectList.size(); i++) {
            if (CoinCollectInfo* coin = mCoinCollectList[i])
                coin->isGet = false;
        }
        // Souvenirs and stickers are the only items the save still records as
        // *bought*: the seed hands out every costume and cap for free at file
        // creation, so these two lists are exactly what the regional coins paid
        // for, and they go when the coins do.  Both are dense, empty-terminated
        // arrays (buyItem fills the first blank, isBuyItem stops at it), so
        // clearing every slot is the correct emptying.
        for (s32 i = 0; i < mItemGift.size(); i++)
            mItemGift[i].clear();
        for (s32 i = 0; i < mItemSticker.size(); i++)
            mItemSticker[i].clear();
    }

    void customSetShineCollected(s32 shineUid, const char* stage, bool collected) {
        if (HintInfo* info = customFindShine(shineUid, stage))
            info->isGet = collected;
    }

    // Half of the emptying - see Nuzlocke's clearAllCollectedMoons for the other
    // half, and getGotShineList above for why there are two halves at all.
    //
    // Sweeping ShineDB and naming each record through customSetShineCollected
    // only reaches records some ShineDB entry reproduces exactly, by both
    // uniqueId and stage.  ShineDB is the smaller table - it does not enumerate
    // every shine the save keeps a record for - so a sweep like that silently
    // leaves the remainder set.  The hint list is the storage itself, so walking
    // it is the only emptying that cannot miss.
    void customClearAllShineCollected() {
        for (s32 i = 0; i < 1024; i++)
            mHintList[i].isGet = false;
    }

    // The native counters are cache-like mirrors of HintInfo::isGet.  Rebuild
    // them after a Soft Nuzlocke removes arbitrary reward identities.  Only the
    // *collected* counts belong here: mTotalShineNum is the per-world moon
    // population, owned by initializeHintList(), and clearing it here left
    // nothing to restore it.
    void customRebuildShineCounters() {
        for (s32 i = 0; i < sNumWorlds; i++) {
            mShineNum[i] = 0;
            mShopShineNum[i] = 0;
        }
        for (s32 i = 0; i < 1024; i++) {
            const HintInfo& info = mHintList[i];
            if (!info.isGet || info.worldId < 0 || info.worldId >= sNumWorlds)
                continue;
            const s32 value = info.isGrand ? 3 : 1;
            mShineNum[info.worldId] += value;
            if (info.isShop)
                mShopShineNum[info.worldId] += value;
        }
    }

    bool customTrySpendCoin(s32 count) {
        if (count <= 0)
            return true;
        if (mCoinNum < count)
            return false;
        mCoinNum -= count;
        return true;
    }

    // custom impl of addCoinCollect that uses a provided stage and world instead of your current ones

    // some getters/setters for private member variables

    void customSetWorldWarpHoleDisplayWorldId(s32 worldId) {
        _b60 = worldId;
    }

    s32 customGetWorldWarpHoleDisplayWorldId() const { return _b60; }

    bool customIsExistInHackDictionary(const char* hackName) const {
        if (!hackName || !hackName[0])
            return false;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty() && std::strcmp(mHackDictionary[i].cstr(), hackName) == 0)
                return true;
        }
        return false;
    }

    void customAddHackDictionary(const char* hackName) {
        if (!hackName || !hackName[0] || customIsExistInHackDictionary(hackName))
            return;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty()) {
                mHackDictionary[i].copy(hackName);
                return;
            }
        }
    }

    // A dictionary record may contain a small value after an exact key, using
    // the form "key:value".  Do not use a raw prefix match for persistent
    // gameplay state: e.g. a key named "HintCapture_Foo" must not overwrite
    // "HintCapture_Foobar".  These helpers make the separator part of the
    // match, report capacity failures, and verify every write.
    const char* customFindHackDictionaryKey(const char* key) const {
        if (!key || !key[0])
            return nullptr;
        const size_t keyLen = std::strlen(key);
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                continue;
            const char* record = mHackDictionary[i].cstr();
            if (std::strncmp(record, key, keyLen) == 0 && record[keyLen] == ':')
                return record;
        }
        return nullptr;
    }

    bool customCanUpsertHackDictionaryKey(const char* key) const {
        if (!key || !key[0])
            return false;
        if (customFindHackDictionaryKey(key))
            return true;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                return true;
        }
        return false;
    }

    s32 customGetHackDictionaryFreeCount() const {
        s32 count = 0;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                count++;
        }
        return count;
    }

    bool customUpsertHackDictionaryKey(const char* key, const char* record) {
        if (!key || !key[0] || !record || !record[0] || std::strlen(record) >= 128)
            return false;
        const size_t keyLen = std::strlen(key);
        if (std::strncmp(record, key, keyLen) != 0 || record[keyLen] != ':')
            return false;

        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                continue;
            const char* existing = mHackDictionary[i].cstr();
            if (std::strncmp(existing, key, keyLen) != 0 || existing[keyLen] != ':')
                continue;
            mHackDictionary[i].copy(record);
            return std::strcmp(mHackDictionary[i].cstr(), record) == 0;
        }

        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty())
                continue;
            mHackDictionary[i].copy(record);
            return std::strcmp(mHackDictionary[i].cstr(), record) == 0;
        }
        return false;
    }

    bool customRemoveHackDictionaryKey(const char* key) {
        if (!key || !key[0])
            return false;
        const size_t keyLen = std::strlen(key);
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                continue;
            const char* record = mHackDictionary[i].cstr();
            if (std::strncmp(record, key, keyLen) != 0 || record[keyLen] != ':')
                continue;
            for (s32 j = i; j < mHackDictionary.size() - 1; j++) {
                if (mHackDictionary[j + 1].isEmpty()) {
                    mHackDictionary[j].clear();
                    return true;
                }
                mHackDictionary[j].copy(mHackDictionary[j + 1]);
            }
            mHackDictionary[mHackDictionary.size() - 1].clear();
            return true;
        }
        return false;
    }

    // Replaces the single record beginning with `prefix`, or inserts it when
    // absent.  Unlike customAddHackDictionary this reports a full dictionary
    // (and verifies the write), which callers that persist gameplay state need
    // in order to avoid silently losing progress.
    bool customReplaceHackDictionaryPrefix(const char* prefix, const char* record) {
        if (!prefix || !prefix[0] || !record || !record[0]
            || std::strlen(record) >= 128)
            return false;

        const size_t prefixLen = std::strlen(prefix);
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty()
                || std::strncmp(mHackDictionary[i].cstr(), prefix, prefixLen) != 0)
                continue;
            mHackDictionary[i].copy(record);
            return std::strcmp(mHackDictionary[i].cstr(), record) == 0;
        }

        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty())
                continue;
            mHackDictionary[i].copy(record);
            return std::strcmp(mHackDictionary[i].cstr(), record) == 0;
        }
        return false;
    }

    void customRemoveHackDictionary(const char* hackName) {
        if (!hackName || !hackName[0])
            return;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty() && std::strcmp(mHackDictionary[i].cstr(), hackName) == 0) {
                for (s32 j = i; j < mHackDictionary.size() - 1; j++) {
                    if (mHackDictionary[j + 1].isEmpty()) {
                        mHackDictionary[j].clear();
                        return;
                    }
                    mHackDictionary[j].copy(mHackDictionary[j + 1]);
                }
                mHackDictionary[mHackDictionary.size() - 1].clear();
                return;
            }
        }
    }

    void customRemoveHackDictionaryPrefix(const char* prefix) {
        if (!prefix || !prefix[0])
            return;

        const size_t prefixLen = std::strlen(prefix);
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty()) {
                const char* str = mHackDictionary[i].cstr();
                if (std::strncmp(str, prefix, prefixLen) == 0) {
                    for (s32 j = i; j < mHackDictionary.size() - 1; j++) {
                        if (mHackDictionary[j + 1].isEmpty()) {
                            mHackDictionary[j].clear();
                            break;
                        }
                        mHackDictionary[j].copy(mHackDictionary[j + 1]);
                    }
                    mHackDictionary[mHackDictionary.size() - 1].clear();
                    i--;
                }
            }
        }
    }

    const char* customFindHackDictionaryPrefix(const char* prefix) const {
        if (!prefix)
            return nullptr;
        const size_t prefixLen = std::strlen(prefix);
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (mHackDictionary[i].isEmpty())
                continue;
            const char* str = mHackDictionary[i].cstr();
            if (std::strncmp(str, prefix, prefixLen) == 0)
                return str;
        }
        return nullptr;
    }

    // Diagnostic only. When a prefix lookup above comes back null, this
    // separates the two very different reasons: 0 means nothing was ever
    // deserialised into this file (the save did not load at all), while a
    // non-zero count means the save loaded fine and simply does not carry the
    // record that was asked for.
    s32 customCountHackDictionary() const {
        s32 used = 0;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty())
                used++;
        }
        return used;
    }

    void customSetRandoSeed(u32 seed) {
        customRemoveHackDictionaryPrefix("RandoSeed:");

        char seedStr[32];
        nn::util::SNPrintf(seedStr, sizeof(seedStr), "RandoSeed:%u", seed);
        customAddHackDictionary(seedStr);
    }

    bool customGetRandoSeed(u32* outSeed) const {
        if (!outSeed)
            return false;
        for (s32 i = 0; i < mHackDictionary.size(); i++) {
            if (!mHackDictionary[i].isEmpty()) {
                const char* str = mHackDictionary[i].cstr();
                if (std::strncmp(str, "RandoSeed:", 10) == 0) {
                    *outSeed = (u32)strtoul(str + 10, nullptr, 10);
                    return true;
                }
            }
        }
        return false;
    }

    UniqObjInfo* customSetCheckpointId(const al::PlacementId* placement_id) {
        UniqObjInfo* result = nullptr;
        al::StringTmp<128> obj_id = "";
        placement_id->makeString(&obj_id);
        if (CheckpointInfo* info = customTryFindCheckpointInfoImpl(mCheckpointTable.begin(), obj_id.cstr())) {
            info->isGet = true;
            if (UniqObjInfo* got_info = customAddGotCheckpoint(mGotCheckpoint.begin(), info->objInfo.getObjId())) {
                got_info->setStageName(info->objInfo.getStageName());
                got_info->mObjId.copy(info->objInfo.getObjId());
                mGotCheckpointNum++;
                result = got_info;
            }
        }
        return result;
    }

    static UniqObjInfo* customAddGotCheckpoint(UniqObjInfo* list, const char* obj_id) {
        for (s32 i = 0; i < 320; i++) {
            if (list[i].mStageName.isEmpty() && list[i].mObjId.isEmpty())
                return &list[i];
            if (al::isEqualString(list[i].getObjId(), obj_id))
                break;
        }
        return nullptr;
    }

    static CheckpointInfo* customTryFindCheckpointInfoImpl(CheckpointInfo** table, const char* obj_id) {
        for (s32 i = 0; i < sNumWorlds; i++)
            for (s32 j = 0; j < 16; j++)
                if (al::isEqualString(obj_id, table[i][j].objInfo.getObjId()))
                    return &table[i][j];
        return nullptr;
    }

    // end custom methods

    // AP Custom Methods

    void addShine(int world, int amt) {
        if (world < 0)
            world = 0;
        mShineNum[world] += amt;
    }

    // end custom methods

private:
    struct WorldHintList {
        sead::PtrArray<HintInfo> list;
        s32 index;
    };

    static_assert(sizeof(WorldHintList) == 0x18);

    struct WorldCoinCollectList {
        s32 index;
        sead::PtrArray<CoinCollectInfo> list;
    };

    static_assert(sizeof(WorldCoinCollectList) == 0x18);

    struct ShopNpcInfo {
        sead::FixedSafeString<64> name;
        sead::Vector3f trans;
        s32 type;
    };

    static_assert(sizeof(ShopNpcInfo) == 0x68);

    struct MiniGameInfo {
        sead::Vector3f trans;
        bool isExist;
        sead::FixedSafeString<64> name;

        void clear() {
            trans = sead::Vector3f::zero;
            isExist = false;
            name.clear();
        }
    };

    static_assert(sizeof(MiniGameInfo) == 0x68);

    FixedHeapArray<ShineInfo*, 1024> mGotShine;
    FixedHeapArray<ShineInfo*, 1024> mGotGrandShine;
    ShineInfo* mLatestGetShineInfo = nullptr;
    FixedHeapArray<s32, sNumWorlds> mShopShineNum;
    FixedHeapArray<s32, sNumWorlds> mMainScenarioNo;
    s32 mStartShineIndex = -1;
    sead::FixedSafeString<128> mPlayerStartId;
    sead::FixedSafeString<128> mPlayerStartIdForSave;
    sead::FixedSafeString<128> _160;
    sead::FixedSafeString<128> mCheckpointName;
    sead::FixedSafeString<128> _290;
    sead::FixedSafeString<128> mCurrentStageName;
    sead::FixedSafeString<128> mPrevStageName;
    bool _458 = false;
    bool _459 = false;
    FixedHeapArray<bool, sNumWorlds> mIsWorldWarpHoleThrough;
    u64 mSaveTimeForDisp = 0;
    u64 mSaveTime = 0;
    s64 mSaveDataIdForPrepo = rs::prepo::generateSaveDataId();
    s64 mSaveDataIdForPrepoForWrite = rs::prepo::generateSaveDataId();
    FixedHeapArray<UniqObjInfo, 320> mGotCheckpoint;
    sead::FixedSafeString<256> mCheckpointWarpObjId;
    FixedHeapArray<s32, sNumWorlds> mPayShineNum;
    bool mIsPayShineAllInAllWorld = false;
    s32 mKeyNum = 0;
    s32 mOpenDoorLockNum = 0;
    FixedHeapArray<UniqObjInfo, 512> mStartedObj;
    FixedHeapArray<SaveObjS32, 256> mSaveObjS32;
    FixedHeapArray<GrowFlower, 16> mGrowFlower;
    s32 mCoinNum = 0;
    s32 mTotalCoinNum = 0;
    s32 mPlayerJumpCount = 0;
    s32 mPlayerThrowCapCount = 0;
    FixedHeapArray<s32, sNumWorlds> mUseCoinCollectNum;
    FixedHeapArray<s32, sNumWorlds> mUnlockedWorldId;
    bool mIsPlayDemoOpening = true;
    bool mIsMeetCap = false;
    bool mIsEnableCap = true;
    FixedHeapArray<bool, SessionMusicianType::size()> mIsExistSessionMember;
    SessionEventProgress mSessionEventProgress = SessionEventProgress::Entry;
    bool mIsPayCoinToSphinx = false;
    SphinxQuizData* mSphinxQuizData = nullptr;
    FixedHeapArray<bool, sNumWorlds> mIsTalkLocalLanguage;
    bool mIsHintNpcFirstTalk = false;
    bool mIsKinopioBrigadeNpcFirstTalk = false;
    CapStatus mCapStatusForJango = CapStatus::None;
    CapStatus mCapStatusForJangoSubArea = CapStatus::None;
    s32 mJangoCount = 0;
    TimeBalloonSaveData* mTimeBalloonSaveData = nullptr;
    sead::FixedSafeString<64> mWorldTravelingStatus;
    bool mIsStartWorldTravelingPeach = false;
    bool mIsPlayAlreadyWorldWarp = false;
    bool mIsTalkFirstAmiiboNpc = true;
    bool mIsUnlockAchievement = false;
    WorldWarpTalkData* mWorldWarpTalkData = nullptr;
    VisitStageData* mVisitStageData = nullptr;
    GameProgressData* mGameProgressData = nullptr;
    MoonRockData* mMoonRockData = nullptr;
    BossSaveData* mBossSaveData = nullptr;
    AchievementSaveData* mAchievementSaveData = nullptr;
    SearchAmiiboDataTable* mSearchAmiiboDataTable = nullptr;
    NetworkUploadFlag* mNetworkUploadFlag = nullptr;
    SequenceDemoSkipData* mSequenceDemoSkipData = nullptr;
    HintPhotoData* mHintPhotoData = nullptr;
    s64 mPlayTimeTotal = 0;
    FixedHeapArray<sead::FixedSafeString<64>, 64> mItemCap;
    FixedHeapArray<sead::FixedSafeString<64>, 64> mItemCloth;
    FixedHeapArray<sead::FixedSafeString<64>, 64> mItemSticker;
    FixedHeapArray<sead::FixedSafeString<64>, 64> mItemGift;
    sead::FixedSafeString<64> mCurrentCostumeName;
    sead::FixedSafeString<64> mCurrentCapName;
    bool mIsCostumeRandom = false;
    bool mIsCapRandom = false;
    FixedHeapArray<sead::FixedSafeString<128>, 160> mHackDictionary;
    bool mIsRideSphinx = false;
    bool mIsRideMotorcycle = false;
    FixedHeapArray<s32, sNumWorlds> mScenarioNo;
    sead::PtrArray<RaceRecord> mRaceRecord;
    s32 mRaceLoseCountLv1 = 0;
    s32 mRaceLoseCountLv2 = 0;
    s32 mJumpingRopeBestCount = 0;
    s32 mJumpingRopeDayCount = 0;
    bool mIsExistRecordJumpingRope = false;
    bool mIsExistRecordInDayJumpingRope = false;
    s32 mVolleyballBestCount = 0;
    s32 mVolleyballDayCount = 0;
    bool mIsExistRecordVolleyball = false;
    bool mIsExistRecordInDayVolleyball = false;
    s32 mNextWorldId = 0;
    s32 mPrevWorldId = 0;
    bool mIsPlayDemoPlayerDownForBattleKoopaAfter = true;
    FixedHeapArray<bool, 256> mFlagTalkMessage;
    bool mIsKidsMode = false;
    sead::PtrArray<CollectBgmInfo> mCollectBgmList;
    bool mIsTalkKakku = false;
    bool mIsTalkWorldTravelingPeach = false;
    bool mIsTalkCollectBgmNpc = false;
    s32 mTokimekiMayorNpcFavorabilityRating = 0;
    bool mIsFirstNetwork = true;
    GameDataHolder* mGameDataHolder;
    FixedHeapArray<s32, sNumWorlds> mShineNum;
    PlayerHitPointData* mPlayerHitPointData = nullptr;

public:
    sead::FixedSafeString<128> mNextStageName;
    bool mIsAppearCourseName = false;
    bool mIsStageHakoniwa = true;
    s32 mGotCheckpointNum = 0;
    sead::FixedSafeString<128> _908;
    FixedHeapArray<HintInfo, 1024> mHintList;
    sead::PtrArray<WorldHintList> mHintTable;
    sead::PtrArray<WorldHintList> mHintTableByIdx;
    sead::PtrArray<WorldCoinCollectList> mCoinCollectTable;
    sead::PtrArray<CoinCollectInfo> mCoinCollectList;
    FixedHeapArray<CheckpointInfo*, sNumWorlds> mCheckpointTable;
    s32 mCurrentWorldId = -1;
    s32 mCurrentWorldIdForWrite = -1;
    s32 _9f8 = 0;
    bool mIsPlayDemoReturnToHome = false;
    bool mIsPlayDemoAwardSpecial = false;
    bool mIsPlayDemoWorldWarpHole = false;
    bool _9ff = false;
    FixedHeapArray<s32, sNumWorlds> mCoinCollectGotNum;
    bool mIsEnterStageFirst = true;
    bool _a09 = false;
    bool mIsWarpCheckpoint = false;
    FixedHeapArray<s32, sNumWorlds> mTotalShineNum;
    FixedHeapArray<s32, sNumWorlds> mTotalMoonRockShineNum;
    s32 mTotalAchievementNum = 0;
    s32 mScenarioNoPlacement = -1;
    s32 mScenarioNoOverride = -1;
    bool _a2c = false;
    ChangeStageInfo* mChangeStageInfo = nullptr;
    ChangeStageInfo* mMissRestartInfo = nullptr;
    bool mIsUseMissRestartInfo = false;
    bool mIsStartKoopaCapture = false;
    RaceType mRaceType = RaceType_None;
    RaceResult mRaceResult = RaceResult_None;
    s32 mRaceRivalLevel = -1;
    s32 mLastRaceRanking = 0;
    FixedHeapArray<ShopNpcInfo, 4> mShopNpcInfo;
    ShopTalkData* mShopTalkData = nullptr;
    FixedHeapArray<MiniGameInfo, 4> mMiniGameInfo;
    bool mIsShowExplainCheckpointFlag = false;
    s32 mTotalPayShineNumForDisp = 0;
    s32 mCurrentWorldIdForDisp = 0;
    sead::Vector3f mHomeTrans = sead::Vector3f::zero;
    bool mIsExistHome = false;
    sead::Vector3f mRaceStartNpcTrans = sead::Vector3f::zero;
    bool mIsExistRaceStartNpc = false;
    sead::Vector3f mRaceStartTrans = sead::Vector3f::zero;
    bool mIsExistRaceStart = false;
    sead::Vector3f mRaceGoalTrans = sead::Vector3f::zero;
    bool mIsExistRaceGoal = false;
    sead::Vector3f mHintNpcTrans = sead::Vector3f::zero;
    bool mIsExistHintNpc = false;
    sead::Vector3f _acc = sead::Vector3f::zero;
    bool _ad8 = false;
    sead::Vector3f mJangoTrans = sead::Vector3f::zero;
    bool mIsEnableJangoTrans = false;
    sead::Vector3f mAmiiboNpcTrans = sead::Vector3f::zero;
    bool mIsExistAmiiboNpc = false;
    sead::Vector3f mTimeBalloonNpcTrans = sead::Vector3f::zero;
    bool mIsExistTimeBalloonNpc = false;
    sead::Vector3f mPoetterTrans = sead::Vector3f::zero;
    bool mIsExistPoetter = false;
    sead::Vector3f mMoonRockTrans = sead::Vector3f::zero;
    bool mIsExistMoonRock = false;
    const HintInfo* mLatestHint = nullptr;
    bool mIsMissEndPrevStage = false;
    FixedHeapArray<s32, sNumWorlds> mWorldMapIndex;
    s32 mWorldMapNum = 0;
    FixedHeapArray<s32, sNumWorlds> mWorldWarpIndex;
    s32 mWorldWarpNum = 0;
    s32 mUnlockedWorldNum = 1;
    s32 _b60 = 0;
    bool mIsKoopaLv3 = false;
    bool mIsEnableCapMessageLifeOneKidsMode = true;
};

static_assert(sizeof(GameDataFile) == 0xb68);
