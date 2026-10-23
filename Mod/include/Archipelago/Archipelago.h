#pragma once

#include <sead/container/seadPtrArray.h>
#include <sead/container/seadSafeArray.h>
#include <sead/prim/seadSafeString.h>

#include "al/Library/Scene/Scene.h"
#include "al/Library/Scene/SceneObjHolder.h"

#include "game/Item/Shine.h"
#include "game/Layout/ShopLayoutInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataFunction.h"

#include <basis/seadTypes.h>
#include <mutex>
#include <string>
#include <vector>

#include "APCppNX/Archipelago.h"
#include "Archipelago/ArchipelagoHelpers.hpp"
#include "Archipelago/ArchipelagoHintArrow.h"
#include "Archipelago/ArchipelagoSocket.h"
#include "Archipelago/CaptureData.h"
#include "Archipelago/RegionalCoinData.h"
#include "Archipelago/ShopData.h"
#include "Archipelago/StageData.h"
#include "cJSON/cJSON.h"

namespace Archipelago {

    struct stageConnection {
        short toStageIdIndex;
        short toStageNameIndex;
    };

    struct replaceText {
        u8 slotNameIndex;
        u8 itemNameIndex;
    };

    struct shopReplaceText {
        u8 gameIndex;
        u8 slotNameIndex;
        u8 itemNameIndex;
        u8 itemClassification;
    };

    struct scenarioShine {
        u8 worldId;
        short shineUid;
        u8 scenario;
    };

    enum CheckType : s8
    {
        SentCheck = -11,
        ShopMoonScout = -10,
        MoonRockScout = -9,
        StickerScout = -8,
        SouvenirScout = -7,
        CapScout = -6,
        ClothesScout = -5,
        LifeUpHeart = -4,
        LifeHeart = -3,
        Coins = -2,
        Moon = -1,
        Clothes = 0,
        Cap = 1,
        Souvenir = 2,
        Sticker = 3,
        RegionalCoin = 4,
        Capture = 5,
        MoonRock = 6,
        HealthUpgrade = 7,
        WalletUpgrade = 8,
        Ability = 9
    };

    enum AbilityId : u8
    {
        AbilityId_Jump,
        AbilityId_DoubleJump,
        AbilityId_TripleJump,
        AbilityId_SideFlip,
        AbilityId_BackFlip,
        AbilityId_LongJump,
        AbilityId_WallJump,
        AbilityId_Spin,
        AbilityId_SpinJump,
        AbilityId_GroundPound,
        AbilityId_GroundPoundJump,
        AbilityId_NeutralThrow,
        AbilityId_UpThrow,
        AbilityId_DownThrow,
        AbilityId_SpinThrow,
        AbilityId_Crouch,
        AbilityId_Roll,
        AbilityId_RollBoost,
        AbilityId_Vault,
        AbilityId_Dive,
        AbilityId_Climb,
        AbilityId_LedgeGrab,
        AbilityId_CapReturnJump,
        AbilityId_Jaxi,
        AbilityId_Scooter,
        AbilityId_End,
    };

    // Constants
    static constexpr int kArchipelagoNameSize = 0x40;
    static constexpr int kArchipelagoSeedSize = 20;
    static constexpr u8 kArchipelagoMaxPortDigits = 5;
    static constexpr ushort kArchipelagoDefaultPort = 38281;
    static constexpr const char* kArchipelagoGame = "Super Mario Odyssey";

    // Members
    static bool sArchipelagoActive = false;

    // Underlying Archielago Info
    // Separate IP for Archipelago Client
    static sead::FixedSafeString<kArchipelagoNameSize> sApClientIP;
    static sead::FixedSafeString<kArchipelagoNameSize> sHostName;
    static u16 sArchipelagoPort = kArchipelagoDefaultPort;
    static sead::FixedSafeString<kArchipelagoNameSize> sSlotName;
    static sead::FixedSafeString<kArchipelagoNameSize> sPassword;
    static sead::FixedSafeString<kArchipelagoSeedSize> sSeedName;

    static vector<int> sCheckedLocations;

    // Slot Data
    // Goal
    static u8 sGoal = -1;

    // shine pay counts
    static sead::SafeArray<int, 17> sWorldPayCounts;
    static bool sDeathLinkEnabled = false;
    static bool sCapturesEnabled = false;
    static bool sIsRecordCapture = false;
    static bool sIsEntranceRandomizationEnabled = false;
    static bool sIsAbilityLockEnabled = false;
    static bool sIsMotionInputRebound = false;
    static bool sIsConnectInit = false;
    static bool sIsFirstConnect = true;

    // Current State
    // Possible switch from sead::safearrays for string data to sead::PtrArray to possibly safe space if needed.
    static sead::SafeArray<int, 17> sWorldScenarios;
    static bool sDying = false;
    static bool sApDeath = false;
    static int sCheckIndex = 0;

    static std::map<int, std::map<int, int>> sCoinValues;

    static vector<AP_NetworkItem> sScoutedMoons;

    // List of 37 ints to track which shine's have been grabbed
    static sead::SafeArray<u8, 148> sCollectedShines;

    // List of 11 u8s for tracking which caps and clothes have been grabbed
    static sead::SafeArray<u8, 11> sCollectedOutfits;

    // List of 3 u8s for tracking which stickers have been grabbed
    static sead::SafeArray<u8, 3> sCollectedStickers;

    // List of 4 u8s for tracking which souvenirs have been grabbed
    static sead::SafeArray<u8, 4> sCollectedSouvenirs;

    // List of 11 u8s for tracking which caps and clothes have been scouted
    static sead::SafeArray<u8, 11> sScoutedOutfits;

    // List of 3 u8s for tracking which stickers have been scouted
    static sead::SafeArray<u8, 3> sScoutedStickers;

    // List of 4 u8s for tracking which souvenirs have been scouted
    static sead::SafeArray<u8, 4> sScoutedSouvenirs;

    // List of 4 u8s for tracking which souvenirs have been scouted
    static sead::SafeArray<u8, 2> sScoutedShopMoons;

    // List of 7 u8s for tracking which captures have been grabbed
    static sead::SafeArray<u8, 7> sCollectedCaptures;
    static sead::SafeArray<u8, 7> sCheckedCaptures;

    // List of 7 u8s for tracking which captures have been grabbed l trailing byte buffer
    static sead::SafeArray<u8, 126> sCollectedRegionals;

    // List of 3 u8s for tracking which moon rocks have been collected
    static sead::SafeArray<u8, 3> sCollectedMoonRocks;

    // List of 3 u8s for tracking which moon rocks have been scouted
    static sead::SafeArray<u8, 3> sScoutedMoonRocks;

    static sead::SafeArray<u8, 4> sUnlockedAbilities;

    // Moon Text Replacement Handling
    static int sRecentShineUid = 0;
    static sead::SafeArray<replaceText, 100> shineTextReplacements;

    // Moon Color Replacement
    static sead::SafeArray<s8, 1170> sShineColors;

    // Moon model override
    // For when custom models and the like are added
    // sead::SafeArray<s8, 1170> mModelType;

    // 17 caps
    static sead::SafeArray<shopReplaceText, 17> sShopCapTextReplacements;
    // 20 outfits
    static sead::SafeArray<shopReplaceText, 20> sShopClothTextReplacements;
    // 9 slots to correspond with the maximum regional items in one shop
    static sead::SafeArray<shopReplaceText, 9> sShopRegionalTextReplacements;
    // Offsets Regional Text Replacement index bu the number of Caps in a shop
    // This guarantees replacements are alligned with the correct item
    static int sRegionalOffset = 0;
    // One shop moon slot per world
    static shopReplaceText sShopMoonTextReplacements;
    // coin cap + outfit = 37 indexes  0 - 36
    // 46 with regionals 37 - 45
    // 47 with moon slot 46

    // Fetch Via LocationScouts and GetDataPackage
    static sead::SafeArray<sead::FixedSafeString<kArchipelagoNameSize>, 47> sGameNames;
    // moon data 47 - 146
    // moon rock 147
    // cappy message parts 148 - 167
    // coin shop item and slot names would be treated as common due to there always available nature
    static sead::SafeArray<sead::FixedSafeString<kArchipelagoNameSize>, 168> sSlotNames;
    static sead::SafeArray<sead::FixedSafeString<kArchipelagoNameSize>, 168> sItemNames;

    // need duplicates of theese lists in player.py for tracking current cached names state might
    // not be needed

    // this could free up space in a kingdoms shine replace space
    // leaving more room for caching other data
    // could be taken advantage of using the player.py parallel

    // additional entries in slot and item will be required for achievements
    // although they can be allieviated by just overwriting the shine
    // replace data with them while only in the castle sub area
    // just without removing the replace for the sub area moons

    // simplify shopReplaceText using replaceText

    static int sLastShopMoonReplaceIndex = -1;

    // Still in Slot Data
    // Loading Zone Replacement
    // 239 one for each StageId
    // May need more for alternate loading zones in sub areas that don't use a unique
    // stage id like forks exit
    // Flag for if Entrance Randomization is active
    // stageConnections are indexed by stageId
    // Overworld connections
    static sead::SafeArray<stageConnection, 239> sOverworldStageConnections;
    // Sub area connections
    static sead::SafeArray<stageConnection, 239> sSubAreaStageConnections;

    static sead::FixedSafeString<128> sLastERStageId;
    static sead::FixedSafeString<128> sLastERStageName;

    static sead::FixedSafeString<128> sLastExitStageId;
    static sead::FixedSafeString<128> sLastExitStageName;

    // Array for storing all story shines and multi moons in a given stage
    // For activating onSwitchGet behaviors when loading into a stage
    static sead::PtrArray<Shine> sStoryShineArray;

    // World index used by shine list total count fetching
    static int sCurWorldShineList = 0;

    // The world relative to the type of
    // regionals coins found in a stage
    static int sRelativeWorldCoinCollect = -1;

    static bool sDefeatedBowserInCloud = false;
    static bool sDefeatedKlepto = false;

    // Scene Members
    static al::SceneObjHolder* sSceneObjHolder;
    static StageScene* sCurScene;

    // ===== Scene Actors =====
    static ArchipelagoHintArrow* mHintArrow = nullptr; // Arrow that points to a targeted object
    // ptr to the targeted actor so arrow can be deactivated when coin is collected
    static al::LiveActor* mCoinCollectHintTarget = nullptr;

    // Functions
    // ===== Arcipelago Setters / Getters =====
    inline bool isArchipelagoActive() {
        return sArchipelagoActive;
    };
    void startArchipelago();
    void initScene(al::ActorInitInfo* initInfo, al::Scene* scene);
    inline StageScene* getCurScene() {
        return sCurScene;
    };

    inline void setHostName(const char* str) {
        sHostName = str;
    };
    inline const char* getHost() {
        return sHostName.cstr();
    };
    inline void setSlot(const char* str) {
        sSlotName = str;
    };
    inline const char* getSlot() {
        return sSlotName.cstr();
    };
    inline void setPassword(const char* str) {
        sPassword = str;
    };
    inline const char* getPassword() {
        return sPassword.cstr();
    };
    inline void setPort(ushort port) {
        sArchipelagoPort = port;
    };
    inline u16 getPort() {
        return sArchipelagoPort;
    };

    void setScenario(int worldID, int scenario);
    bool setScenario(const char* worldName, int scenario);
    int getScenario(const char* worldName);
    int getScenario(int worldID);
    void sendCorrectScenario(const ChangeStageInfo* info);

    bool has(u8 entry, int index);
    u8 add(u8 entry, int index);

    void addShine(int uid);
    bool hasShine(int uid);
    int getShineChecks(int index);
    void setShineChecks(int index, int checks);

    void addOutfit(const ShopItem::ItemInfo* info);
    bool hasOutfit(const ShopItem::ItemInfo* info);
    int getOutfitChecks(int index);
    void setOutfitChecks(int index, int checks);

    void addSticker(const ShopItem::ItemInfo* info);
    bool hasSticker(const ShopItem::ItemInfo* info);
    int getStickerChecks(int index);
    void setStickerChecks(int index, int checks);

    void addSouvenir(const ShopItem::ItemInfo* info);
    bool hasSouvenir(const ShopItem::ItemInfo* info);
    int getSouvenirChecks(int index);
    void setSouvenirChecks(int index, int checks);

    bool hasItem(const ShopItem::ItemInfo* info);
    void addItem(const ShopItem::ItemInfo* info);

    void addScoutedOutfit(int index);
    bool hasScoutedOutfit(int index);

    void addScoutedSticker(int index);
    bool hasScoutedSticker(int index);

    void addScoutedSouvenir(int index);
    bool hasScoutedSouvenir(int index);

    void addScoutedShopMoon(int index);
    bool hasScoutedShopMoon(int index);

    bool hasScoutedItem(int type, int index);
    void addScoutedItem(int type, int index);

    void addCapture(const char* capture);
    bool hasCapture(const char* capture);
    int getCaptureChecks(int index);
    void setCaptureChecks(int index, int checks);
    void addCaptureCheck(const char* capture);
    bool hasCaptureCheck(const char* capture);
    void setIsRecordCapture(bool value);

    void addRegionalCoin(const char* placementId);
    void addRegionalCoin(int index);
    bool hasRegionalCoin(const char* placementId);
    bool hasRegionalCoin(int index);

    void unlockAbility(int index);
    bool isAbilityUnlocked(int index);

    inline void setDefeatedBowserCloud(bool value) {
        sDefeatedBowserInCloud = value;
    }
    inline bool getDefeatedBowserCloud() {
        return sDefeatedBowserInCloud;
    }
    inline void setDefeatedKlepto(bool value) {
        sDefeatedKlepto = value;
    }
    inline bool getDefeatedKlepto() {
        return sDefeatedKlepto;
    };

    CheckType getCheckType(int itemId);
    int getCheckKingdom(int itemId);
    int getInGameId(CheckType checkType, int itemId);
    int inGameIdToLocationId(CheckType checkType, int inGameId);

    void setCheckIndex(int index);
    inline int getCheckIndex() {
        return sCheckIndex;
    };

    inline void setRecentShineUid(int index) {
        sRecentShineUid = index;
    };
    inline int getRecentShineUid() {
        return sRecentShineUid;
    }

    void setWorldUnlockCount(int worldId, int count);
    int getWorldUnlockCount(int worldId);
    inline void setDeathLinkFlag(bool value) {
        sDeathLinkEnabled = value;
    };
    inline bool isDeathLinkEnabled() {
        return sDeathLinkEnabled;
    };
    inline void setCapturesFlag(bool value) {
        sCapturesEnabled = value;
    };
    inline bool getCapturesFlag() {
        return sCapturesEnabled;
    };
    inline void setERFlag(bool value) {
        sIsEntranceRandomizationEnabled = value;
    };
    inline bool getERFlag() {
        return sIsEntranceRandomizationEnabled;
    };
    inline void setAbilityLock(bool value) {
        sIsAbilityLockEnabled = value;
    };
    inline bool isAbilityLockEnabled() {
        return sIsAbilityLockEnabled;
    };
    inline void setMotionRebind(bool value) {
        sIsMotionInputRebound = value;
    };
    inline bool isMotionRebound() {
        return sIsMotionInputRebound;
    };

    void setMoonRequirements(std::string data);
    void setShineColors(std::map<int, int> data);
    void setCoinValues(std::string data);
    void setEntranceBindings(std::string data);

    bool chooseTalkatooSpokenUtf8(int world_id, int index, char* out, u32 out_cap);
    // void setConnectInitFlag(bool value) {
    //     sIsConnectInit = value;
    // };
    // bool getConnectInitFlag() {
    //     return sIsConnectInit;
    // };
    // void setFirstConnectFlag(bool value) {
    //     sIsFirstConnect = value;
    // };
    // bool getFirstConnectFlag() {
    //     return sIsFirstConnect;
    // };
    inline void setCurWorldShineList(int worldId) {
        sCurWorldShineList = worldId;
    };
    inline int getCurWorldShineList() {
        return sCurWorldShineList;
    };
    inline void setRelativeWorldCoinCollect(int worldId) {
        sRelativeWorldCoinCollect = worldId;
    };
    void setRelativeWorldCoinCollect(const char* stageName);
    inline int getRelativeWorldCoinCollect() {
        return sRelativeWorldCoinCollect;
    };
    // void setIsNeedUpdateCounter(bool value) {
    //     sIsNeedUpdateCounter = value;
    // };
    // bool getIsNeedUpdateCounter() {
    //     return sIsNeedUpdateCounter;
    // };
    inline void registerStoryShine(Shine* shine) {
        sStoryShineArray.pushBack(shine);
    };
    inline Shine* getStoryShine(int index) {
        return sStoryShineArray[index];
    };
    inline void setGoal(int value) {
        sGoal = static_cast<u8>(value);
    };
    inline u8 getGoal() {
        return sGoal;
    };

    void setGameName(int index, const char* name);
    void setSlotName(int index, const char* name);
    void setItemName(int index, const char* name);

    void setShineTextReplacement(int index, replaceText replace);
    // void setsShineColors(int index, u8 replace);
    void setClothesTextReplacement(int index, shopReplaceText replace);
    void setCapTextReplacement(int index, shopReplaceText replace);
    void setRegionalTextReplacement(int index, shopReplaceText replace);
    void setShopMoonTextReplacement(shopReplaceText replace);
    void setOverWorldStageConnection(int index, stageConnection replace);
    void setSubAreaStageConnection(int index, stageConnection replace);

    const char* getShineReplacementText();
    int getShineColor(Shine* curShine);
    const char16_t* getShopReplacementText(const char* fileName, const char* key);

    void setDying(bool value);
    void setApDeath(bool value);
    inline bool isDying() {
        return sDying;
    }
    inline bool isApDeath() {
        return sApDeath;
    }

    inline const char* getLastERStageId() {
        return sLastERStageId.cstr();
    };
    inline const char* getLastERStageName() {
        return sLastERStageName.cstr();
    };
    ChangeStageInfo* getLastERTransition();

    ChangeStageInfo* handleER(const ChangeStageInfo* info);

    void locationInfoCallback(vector<AP_NetworkItem> items);
    void deathlinkCallback();
    void killPlayer();

    bool isTargetAlive();
    bool trySetHintTargetValid();

    void clearArrays();
    void clearCollectibles();
    void clearScenarios();

    // ===== Archipelago Utility Methods =====
    void sendStage(GameDataHolderWriter writer, const ChangeStageInfo* stageInfo);
    void sendBack();
    void isSubArea(GameDataHolderAccessor accessor, bool* isInSubArea, sead::FixedSafeString<32> stageId);
    void getCustomStageId(GameDataHolderAccessor accessor, const ChangeStageInfo* info, sead::FixedSafeString<64>* stageId);
    void correctCustomStageId(sead::FixedSafeString<64>* toStageId);
    int getNumGotShines();
    int getNumCoinCollect();
    void handleDeathlink(PlayerActorBase* playerBase, PlayerActorHakoniwa* playerHakoniwa, GameDataHolderWriter writer);
    const char* getDeathContext();
    void handleCaptureSanity(PlayerActorBase* playerBase, GameDataHolderAccessor accessor);
    void getNearestRegional(StageScene* stageScene, PlayerActorBase* playerBase);
    void handleSoftLocks(GameDataHolderAccessor accessor, GameDataHolderWriter writer);
    void updateCounter(PlayerActorBase* playerBase, GameDataHolderAccessor accessor);
    void drawAbilityLockWindow();
    bool infoMenu();
    int getRelativeWorldCoinCollectCheckGotNum(GameDataHolderAccessor accessor);
    void calculateShineScenarios();
    int isMoonRockScenario(int worldId);
    int getSubAreaScenario(const char* toStageName);
    void startShineCount();
    void startShineChipCount();

    // ===== Archipeligo Check Senders =====
    void sendCheck(int locationId, int itemType);
    void sendMoonCheck(int uid);
    void sendShopCheck(const ShopItem::ItemInfo* itemInfo);
    void sendRegionalCoinCheck(const char* objId, const char* stageName);
    void sendCaptureCheck(const char* hackName);

    void receiveCheck(int64_t itemId, bool isNotify);

    PlayerActorHakoniwa* getPlayerActorHakoniwa(); // Returns nullptr if the player is not a PlayerActorHakoniwa

    // ===== Archipelago Status Methods
    bool isConnected();
    bool isRefused();
} // namespace Archipelago