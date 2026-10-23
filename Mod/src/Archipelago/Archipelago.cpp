
#include "Archipelago/Archipelago.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"

#include "game/Player/PlayerAnimator.h"
#include "game/Player/PlayerFunction.h"
#include "game/System/FixedHeapArray.h"
#include "game/System/GameDataFile.h"
#include "game/Util/DemoUtil.h"
#include "game/Util/ObjUtil.h"

using namespace Archipelago;

void Archipelago::startArchipelago() {
    // sead::FixedSafeString<128> connectInfo = sead::FixedSafeString<128>();
    // connectInfo.format("%s:%d", getHost(), getPort());
    AP_Init(getHost(), getPort(), kArchipelagoGame, getSlot(), getPassword());

    // AP_SetItemClearCallback();
    AP_SetLocationInfoCallback(locationInfoCallback);
    AP_SetItemRecvCallback(receiveCheck);
    // AP_SetLocationCheckedCallback();

    AP_RegisterSlotDataIntCallback("goal", setGoal);
    AP_RegisterSlotDataIntCallback("capture_sanity", setCapturesFlag);
    AP_RegisterSlotDataIntCallback("ability_sanity", setAbilityLock);
    AP_RegisterSlotDataIntCallback("entrance_randomization", setERFlag);
    AP_RegisterSlotDataIntCallback("death_link", setDeathLinkFlag);
    AP_RegisterSlotDataRawCallback("counts", setMoonRequirements);
    AP_RegisterSlotDataMapIntIntCallback("shine_colors", setShineColors);
    AP_RegisterSlotDataRawCallback("coin_values", setMoonRequirements);
    AP_RegisterSlotDataRawCallback("entrances", setMoonRequirements);

    // AP_RegisterSlotDataRawCallback();

    AP_SetDeathLinkSupported(true);
    AP_SetDeathLinkRecvCallback(deathlinkCallback);

    AP_Start();

    // Use AP_Get_Connection_status for a layout similar to player counter for displaying if AP connected in pause menu
}

void Archipelago::initScene(al::ActorInitInfo* initInfo, al::Scene* scene) {
    sSceneObjHolder = initInfo->actorSceneInfo.sceneObjHolder;
    sCurScene = (StageScene*)scene;
    GameDataHolderAccessor accessor(sCurScene);

    // Create hint arrow
    mHintArrow = new ArchipelagoHintArrow("CheckHintArrow");
    mHintArrow->init(*initInfo);

    const char* stageName = GameDataFunction::getCurrentStageName(accessor);

    FixedHeapArray<GameDataFile::HintInfo, 1024> hintList = accessor.mData->getGameDataFile()->getHintList();
    set<int64_t> locations;
    for (int i = 0; i < 0x400; i++) {
        GameDataFile::HintInfo curInfo = hintList[i];
        if (al::isEqualString(stageName, curInfo.stageName))
            locations.insert(static_cast<int64_t>(curInfo.uniqueId));
    }
    AP_SendLocationScouts(locations, 0);
}

bool isConnected() {
    return AP_GetConnectionStatus() == AP_ConnectionStatus::Connected;
}

bool isRefused() {
    return AP_GetConnectionStatus() == AP_ConnectionStatus::ConnectionRefused;
};

bool Archipelago::has(u8 entry, int index) {
    if (index >= 8)
        index %= 8;

    if (entry & (1 << index))
        return true;

    return false;
}

u8 Archipelago::add(u8 entry, int index) {
    if (index >= 8)
        index %= 8;
    return entry | (1 << index);
}

void Archipelago::addShine(int uid) {
    u8 checkedShineEntry = sCollectedShines[uid / 8];

    sCollectedShines[uid / 8] = add(checkedShineEntry, uid);
}

bool Archipelago::hasShine(int uid) {
    u8 checkedShineEntry = sCollectedShines[uid / 8];

    return has(checkedShineEntry, uid);
}

void Archipelago::addRegionalCoin(const char* placementId) {
    GameDataHolderAccessor accessor(sCurScene);
    const char* stageName = GameDataFunction::getCurrentStageName(accessor);
    int index = getIndexRegionalCoinId(stageName, placementId);
    int relIndex = getRelativeIndexRegionalCoinId(stageName, placementId);

    if (index < 0 || relIndex < 0) {
        // sead::FixedSafeString<128> errorStr = sead::FixedSafeString<128>();
        // errorStr = "";
        // errorStr.append("ERROR: Regional Coin ");
        // errorStr.append(placementId);
        // errorStr.append(" not in placement ids list.");
        // Client::addMessage(errorStr.cstr());
        return;
    }

    addRegionalCoin(index);
}

void Archipelago::addRegionalCoin(int index) {
    // GameDataHolderAccessor accessor(mCurScene);

    // if (index == -1) {
    //     sead::FixedSafeString<128> errorStr = sead::FixedSafeString<128>();
    //     errorStr = "";
    //     errorStr.append("ERROR: Regional Coin ");
    //     errorStr.append(placementId);
    //     errorStr.append(" not in placement ids list.");
    //     Client::addMessage(errorStr.cstr());
    //     return;
    // }

    u8 checkedRegionalsEntry = sCollectedRegionals[index / 8];

    sCollectedRegionals[index / 8] = add(checkedRegionalsEntry, index);
}

void Archipelago::setMoonRequirements(std::string data) {
    cJSON* reqs = cJSON_ParseWithLength(data.c_str(), data.size());

    const char* kingdoms[] = { "Cap", "Cascade", "Sand", "Wooded", "Lake", "Cloud", "Lost", "Metro", "Seaside",
        "Snow", "Luncheon", "Ruined", "Bowser's", "Moon", "Mushroom", "Dark Side", "Darker Side" };

    for (int i = 0; i < 17; i++) {
        sWorldPayCounts[i] = cJSON_GetObjectItem(reqs, kingdoms[i])->valueint;
    }
    cJSON_Delete(reqs);
}

void Archipelago::setShineColors(std::map<int, int> data) {
    for (int i = 0; i < 1170; i++) {
        if (data.count(i))
            sShineColors[i] = static_cast<u8>(data[i]);
    }
}

void Archipelago::setCoinValues(std::string data) {
    // cJSON* coinValues = cJSON_ParseWithLength(data.c_str(), data.size());

    // for (unsigned int i = 0; i < root.size(); i++) {
    //     for (auto map_idx_itr : root[i]["coin_values"].getMemberNames()) {
    //         int key = std::stoi(map_idx_itr);
    //         sCoinValues[key] = std::map<int, int>();
    //         for (auto map_data_iter : root[i]["coin_values"][map_idx_itr].getMemberNames()) {
    //             int loc_id = std::stoi(map_data_iter);
    //             sCoinValues[key][loc_id] = root[i]["coin_values"][map_idx_itr][map_data_iter].asInt();
    //         }
    //     }
    // }
}

void Archipelago::setEntranceBindings(std::string data) {
    cJSON* erBindings = cJSON_ParseWithLength(data.c_str(), data.size());
    cJSON* overWorld = cJSON_GetObjectItem(cJSON_GetObjectItem(erBindings, "entrances"), "over_world");
    cJSON* subArea = cJSON_GetObjectItem(cJSON_GetObjectItem(erBindings, "entrances"), "sub_area");
    cJSON* map_idx = NULL;
    cJSON_ArrayForEach(map_idx, overWorld) {
        int key = std::stoi(map_idx->string);
        sOverworldStageConnections[key] = {
            static_cast<short>(cJSON_GetArrayItem(map_idx, 0)->valueint),
            static_cast<short>(cJSON_GetArrayItem(map_idx, 1)->valueint)
        };
    }

    map_idx = NULL;
    cJSON_ArrayForEach(map_idx, subArea) {
        int key = std::stoi(map_idx->string);
        sSubAreaStageConnections[key] = {
            static_cast<short>(cJSON_GetArrayItem(map_idx, 0)->valueint),
            static_cast<short>(cJSON_GetArrayItem(map_idx, 1)->valueint)
        };
    }
    cJSON_Delete(erBindings);
}

/*
    @brief Gets `ItemType` from Archipelago Item Id
    @param item: the id of the item.
    @return The in-game `ItemType` enum value corresponding to the id range.
*/
Archipelago::CheckType Archipelago::getCheckType(int itemId) {
    // Moons
    if (itemId < 39)
        return CheckType::Moon;

    // Region Coins
    else if (itemId < 65)
        return CheckType::RegionalCoin;

    // Abilities
    else if (itemId < 87)
        return CheckType::Ability;

    // Caps
    else if (itemId < 2539 || itemId == 2577 || itemId == 2581)
        return CheckType::Cap;

    // Clothes
    else if (2581 > itemId && itemId > 2538)
        return CheckType::Clothes;

    // Sticker
    else if (2599 > itemId && itemId > 2581)
        return CheckType::Sticker;

    // Souvenir
    else if (2625 > itemId && itemId > 2598)
        return CheckType::Souvenir;

    // Captures
    else if (4024 < itemId && itemId < 4077)
        return CheckType::Capture;

    // Filler
    else // itemId > 9990:
        return CheckType::Coins;
}

int Archipelago::getCheckKingdom(int itemId) {
    int kingdom = 0;
    switch (itemId) {
    case 1:
        kingdom = GameDataFunction::getWorldIndexHat();
    case 2:
        kingdom = GameDataFunction::getWorldIndexWaterfall();
    case 3:
        kingdom = GameDataFunction::getWorldIndexSand();
    case 4:
        kingdom = GameDataFunction::getWorldIndexForest();
    case 5:
        kingdom = GameDataFunction::getWorldIndexLake();
    case 6:
        kingdom = GameDataFunction::getWorldIndexCloud();
    case 7:
        kingdom = GameDataFunction::getWorldIndexClash();
    case 8:
        kingdom = GameDataFunction::getWorldIndexCity();
    case 9:
        kingdom = GameDataFunction::getWorldIndexSea();
    case 10:
        kingdom = GameDataFunction::getWorldIndexSnow();
    case 11:
        kingdom = GameDataFunction::getWorldIndexLava();
    case 12:
        kingdom = GameDataFunction::getWorldIndexBoss();
    case 13:
        kingdom = GameDataFunction::getWorldIndexSky();
    case 14:
        kingdom = GameDataFunction::getWorldIndexMoon();
    case 15:
        kingdom = GameDataFunction::getWorldIndexPeach();
    case 16:
        kingdom = GameDataFunction::getWorldIndexSpecial1();
    case 17:
        kingdom = GameDataFunction::getWorldIndexSpecial2();
    case 18:
        kingdom = GameDataFunction::getWorldIndexWaterfall();
    case 19:
        kingdom = GameDataFunction::getWorldIndexSand();
    case 20:
        kingdom = GameDataFunction::getWorldIndexForest();
    case 21:
        kingdom = GameDataFunction::getWorldIndexCity();
    case 22:
        kingdom = GameDataFunction::getWorldIndexSea();
    case 23:
        kingdom = GameDataFunction::getWorldIndexSnow();
    case 24:
        kingdom = GameDataFunction::getWorldIndexLava();
    case 25:
        kingdom = GameDataFunction::getWorldIndexSky();
    case 26:
        kingdom = GameDataFunction::getWorldIndexWaterfall();
    case 27:
        kingdom = GameDataFunction::getWorldIndexSand();
    case 28:
        kingdom = GameDataFunction::getWorldIndexForest();
    case 29:
        kingdom = GameDataFunction::getWorldIndexLake();
    case 30:
        kingdom = GameDataFunction::getWorldIndexCity();
    case 31:
        kingdom = GameDataFunction::getWorldIndexSea();
    case 32:
        kingdom = GameDataFunction::getWorldIndexSnow();
    case 33:
        kingdom = GameDataFunction::getWorldIndexLava();
    case 34:
        kingdom = GameDataFunction::getWorldIndexBoss();
    case 35:
        kingdom = GameDataFunction::getWorldIndexSky();
    case 36:
        kingdom = GameDataFunction::getWorldIndexPeach();
    case 38:
        kingdom = GameDataFunction::getWorldIndexSpecial1();
    }

    return kingdom;
}

int Archipelago::getInGameId(CheckType checkType, int itemId) {
    int inGameId = -1;
    switch (checkType) {
        // case CheckType::Moon:
        //     inGameId = player.get_next_moon(itemId);

    case CheckType::RegionalCoin:
        inGameId = itemId;

    case CheckType::Capture:
        inGameId = itemId - 4025;

    case CheckType::Clothes:
        inGameId = itemId - 2538;

    case CheckType::Cap:
        inGameId = itemId < 2539 ? (itemId - 2500) : (itemId - 2538);

    case CheckType::Souvenir:
        inGameId = itemId - 2599;

    case CheckType::Sticker:
        inGameId = itemId - 2582;

    case CheckType::Ability:
        inGameId = itemId - 65;

    case CheckType::Coins:
        inGameId = itemId;
    }

    return inGameId;
}

int Archipelago::inGameIdToLocationId(CheckType checkType, int inGameId) {
    int locationId = -1;
    switch (checkType) {
        // case CheckType::Moon:
        //     inGameId = player.get_next_moon(itemId);

    case CheckType::RegionalCoin:
        locationId = inGameId;

    case CheckType::Capture:
        locationId = inGameId + 4025;

    case CheckType::Clothes:
        locationId = inGameId + 2538;

    case CheckType::Cap:
        locationId = inGameId < 39 ? (inGameId + 2500) : (inGameId + 2538);

    case CheckType::Souvenir:
        locationId = inGameId + 2599;

    case CheckType::Sticker:
        locationId = inGameId + 2582;

    case CheckType::Ability:
        locationId = inGameId + 65;

    case CheckType::Coins:
        locationId = inGameId;
    }

    return locationId;
}

void Archipelago::receiveCheck(int64_t itemId, bool isNotify) {
    // ArchipelagoMode* archipelago = GameModeManager::instance()->getMode<ArchipelagoMode>();
    CheckType itemType = Archipelago::getCheckType(itemId);
    int itemKingdom = getCheckKingdom(itemId);
    int inGameId = getInGameId(itemType, itemId);
    // bool isLocal = packet->senderName[0] == '\0';
    // archipelago->setCappySlotName(packet->senderName);
    // cappyMessage gotItemMessage = {255, 255, 255, false};

    // struct ShopItem::ShopAmiiboInfo amiiboData = {1, 1};
    struct ShopItem::ItemInfo info = {};
    info.index = 1;
    info.type = static_cast<ShopItem::ItemType>(itemType);
    // sead::Buffer<ShopItem::ShopAmiiboInfo> amiiboBuffer({1,1});
    // info.amiiboInfoList = amiiboBuffer;
    info.isAOC = true;

    struct ShopItem::ItemInfo* infoPtr;
    GameDataHolderWriter writer(getCurScene());

    GameDataFile* curFile = writer.mData->mPlayingFile;
    GameProgressData* progress = curFile->getGameProgressData();
    // bool updateIndex = archipelago->getCheckIndex() < packet->index;
    // sead::FixedSafeString<40> indexMessage;
    // indexMessage = "";
    // indexMessage.append("Received item index ");
    // indexMessage.append(intToCstr(packet->index));
    // // setMessage(1, indexMessage.cstr());
    // indexMessage = "";
    // indexMessage.append("Current item index ");
    // indexMessage.append(intToCstr(Archipelago::getCheckIndex()));
    // setMessage(2, indexMessage.cstr());
    // Error Handling
    sead::FixedSafeString<128> recCheck = sead::FixedSafeString<128>();

    switch (itemType) {
    case CheckType::Coins:
        // setMessage(3, "Coins Received");
        // Change Coin amount handling to actually retireve from slot data once slot data is in an Archipelago data member
        if (isNotify) {
            GameDataFunction::addCoin(writer, 100);
        }
        // gotItemMessage.itemType = CappyMessageTypes::CappyCoins;
        // gotItemMessage.itemIndex = packet->amount;
        break;

    case CheckType::Moon:
        // if (collectedShineCount < curCollectedShines.size() - 1) {
        //     curCollectedShines[collectedShineCount] = packet->locationId;
        //     collectedShineCount++;
        //     // archipelago->setIsNeedUpdateCounter(true);
        // }
        // Queue/play shine update on layout
        if (isNotify) {
            curFile->addShine(itemKingdom, 1);
            startShineCount();
        }
        // gotItemMessage.itemType = packet->amount > 15 ? CappyMessageTypes::CappyMultiMoon : CappyMessageTypes::CappyMoon;
        // gotItemMessage.itemIndex = packet->amount % 17;
        break;

    case CheckType::Clothes:
        strcpy(info.name, costumeNamesByCheckId[inGameId]);
        info.type = static_cast<ShopItem::ItemType>(itemType);
        infoPtr = &info;
        curFile->buyItem(infoPtr, false);
        if (isNotify) {
            GameDataFunction::wearCostume(writer, info.name);
        }
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = CappyMessageTypes::CappyClothes;
        // gotItemMessage.itemIndex = packet->locationId;
        break;

    case CheckType::Cap:
        strcpy(info.name, costumeNamesByCheckId[inGameId]);
        info.type = static_cast<ShopItem::ItemType>(itemType);
        infoPtr = &info;
        curFile->buyItem(infoPtr, false);
        if (isNotify) {
            GameDataFunction::wearCap(writer, info.name);
        }
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = CappyMessageTypes::CappyCap;
        // gotItemMessage.itemIndex = inGameId;
        break;

    case CheckType::Souvenir:
        strcpy(info.name, souvenirNames[inGameId]);
        info.type = static_cast<ShopItem::ItemType>(itemType);
        infoPtr = &info;
        curFile->buyItem(infoPtr, false);
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = CappyMessageTypes::CappySouvenir;
        // gotItemMessage.itemIndex = inGameId;
        break;

    case CheckType::Sticker:
        strcpy(info.name, stickerNames[inGameId]);
        info.type = static_cast<ShopItem::ItemType>(itemType);
        infoPtr = &info;
        curFile->buyItem(infoPtr, false);
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = CappyMessageTypes::CappySticker;
        // gotItemMessage.itemIndex = inGameId;
        break;

    case CheckType::RegionalCoin:
        // Call coin layout cout change anim
        if (isNotify) {
            curFile->mCoinCollectGotNum[itemKingdom] += 1;
        }

        // if (!getCurScene() && sInstance->mPendingCoinCollectCount < sMaxPendingCoinCollects) {
        //     PendingCoinCollect& pending = sInstance->mPendingCoinCollects[sInstance->mPendingCoinCollectCount++];
        //     strcpy(pending.placeID, packet->objId);
        //     pending.worldID = packet->amount;
        //     strcpy(pending.stage, packet->stage);
        // } else {
        //     const al::PlacementId placementId(packet->objId, nullptr, nullptr);
        //     writer.mData->getGameDataFile()->customAddCoinCollect(&placementId, packet->amount, packet->stage);
        //     sead::FixedSafeString<128> recCoin = sead::FixedSafeString<128>();
        //     recCoin = "Received Coin at ";
        //     recCoin.append(packet->objId);
        //     recCoin.append(", ");
        //     recCoin.append(intToCstr(packet->amount));
        //     recCoin.append(", ");
        //     recCoin.append(packet->stage);
        //     // addMessage(recCoin.cstr());
        // }
        // gotItemMessage.itemType = CappyMessageTypes::CappyRegionalCoin;
        // gotItemMessage.itemIndex = packet->amount;
        break;

    case CheckType::Capture:
        // archipelago->addCapture(captureListNames[inGameId]);
        GameDataFunction::addHackDictionary(writer, captureListNames[inGameId]);
        recCheck = "Received Capture ";
        recCheck.append(captureListNames[inGameId]);
        // addMessage(recCheck.cstr());
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = CappyMessageTypes::CappyCapture;
        // gotItemMessage.itemIndex = inGameId;
        break;

    case CheckType::Ability:
        unlockAbility(inGameId);
        // gotItemMessage.itemType = CappyMessageTypes::CappyAbility;
        // gotItemMessage.itemIndex = inGameId;
        break;

    case CheckType::SentCheck:
        // archipelago->setCappyItemName(packet->objId);
        // gotItemMessage.itemType = packet->amount;
        // gotItemMessage.isOutgoing = true;
        // updateIndex = true;
        break;

    default:
        recCheck = "Received Invalid Check Type ";
        recCheck.append(intToCstr(itemType));
        // addMessage(recCheck.cstr());
        break;
    }

    // updateIndex becomes isNotify, but may revert to previous tracking system if isNotify doesn't work as thought
    if (isNotify) {
        // if (itemType != CheckType::SentCheck)
        // archipelago->setCheckIndex(packet->index);
        // archipelago->enqueueCappyMessage(gotItemMessage);
    }
}

void Archipelago::sendCheck(int locationId, int itemType) {
    set<int64_t> checks;
    checks.insert(locationId);
    AP_SendItem(checks);
}

void Archipelago::sendMoonCheck(int uid) {
    // Talkatoo% block. Moons the player hasn't been told about by Talkatoo
    // don't credit. The cosmetic get-cinematic still plays because we never
    // hit this method until after sendShinePacketHook has fired; the cutscene
    // title pane is repurposed via mIsTalkatooBlockedLabelPending +
    // getShineReplacementText. In our virtualization model Orig is never
    // called in AP mode (see sendShinePacketHook), so the moon remains
    // re-collectible on next stage entry — the player can return for it once
    // Talkatoo names it.
    //
    // TODO(progression): scenario-advancing Multi Moons should be exempt
    // (otherwise the player can soft-lock when the named pool fills up before
    // the next progression moon is named). Wire either a per-uid exempt list
    // from the server (preferred — matches apworld's `progression: true` flag)
    // or the existing rs::isProgressionMoon-style check once available.
    // if (sTalkatooMode && !isMoonNamed(uid)) {
    //     sIsTalkatooBlockedLabelPending = true;
    //     return;
    // }
    Archipelago::sendCheck(uid, CheckType::Moon);
    if (AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated)
        addShine(uid);
}

void Archipelago::sendShopCheck(const ShopItem::ItemInfo* itemInfo) {
    int itemType = static_cast<int>(itemInfo->type);

    switch (itemType) {
    case 0:
        Archipelago::sendCheck(inGameIdToLocationId(CheckType::Clothes, getIndexApCostumeList(itemInfo->name)), CheckType::Clothes);
        break;
    case 1:
        Archipelago::sendCheck(inGameIdToLocationId(CheckType::Cap, getIndexApCostumeList(itemInfo->name)), CheckType::Cap);
        break;
    case 2:
        Archipelago::sendCheck(inGameIdToLocationId(CheckType::Souvenir, getIndexSouvenirList(itemInfo->name)), CheckType::Souvenir);
        break;
    case 3:
        Archipelago::sendCheck(inGameIdToLocationId(CheckType::Sticker, getIndexStickerList(itemInfo->name)), CheckType::Sticker);
        break;
    }
}

void Archipelago::sendRegionalCoinCheck(const char* objId, const char* stageName) {
    int locationId = getRegionalCoinLocationId(stageName, objId);

    if (locationId < 0) {
        return;
    }

    Archipelago::sendCheck(locationId, CheckType::RegionalCoin);

    if (AP_GetConnectionStatus() == AP_ConnectionStatus::Authenticated)
        addRegionalCoin(objId);
}

void Archipelago::sendCaptureCheck(const char* hackName) {
    Archipelago::sendCheck(getIndexCaptureList(hackName), CheckType::Capture);
}

PlayerActorHakoniwa* Archipelago::getPlayerActorHakoniwa() {
    PlayerActorBase* playerBase = (PlayerActorBase*)rs::getPlayerActor(getCurScene());
    bool isYukimaru = !playerBase->getPlayerInfo();

    if (isYukimaru)
        return nullptr;

    return (PlayerActorHakoniwa*)playerBase;
}

void Archipelago::locationInfoCallback(vector<AP_NetworkItem> items) {
    sScoutedMoons.clear();

    for (AP_NetworkItem item : items)
        sScoutedMoons.push_back(item);
}

void Archipelago::deathlinkCallback() {
    setApDeath(true);
    setDying(true);
}

void Archipelago::killPlayer() {
    PlayerActorHakoniwa* mainPlayer = getPlayerActorHakoniwa();
    if (!mainPlayer)
        return;
    GameDataFunction::killPlayer(GameDataHolderWriter(mainPlayer));
    mainPlayer->startDemoPuppetable();
    al::setVelocityZero(mainPlayer);
    rs::faceToCamera(mainPlayer);
    mainPlayer->mAnimator->endSubAnim();
    mainPlayer->mAnimator->startAnimDead();
    setApDeath(false);
}

// Death Link handling
// Check for other specific demos being active
// to prevent crashes when recieving a deathlink in a cutscene
void Archipelago::handleDeathlink(PlayerActorBase* playerBase, PlayerActorHakoniwa* playerHakoniwa, GameDataHolderWriter writer) {
    if (!PlayerFunction::isPlayerDeadStatus(playerBase) && isApDeath() && !rs::isActiveDemo(getCurScene())) {
        killPlayer();
    }

    if (PlayerFunction::isPlayerDeadStatus(playerBase) && !isDying()) {
        if (isDeathLinkEnabled())
            AP_DeathLinkSend(getDeathContext());
        setDying(true);
    }

    if (!PlayerFunction::isPlayerDeadStatus(playerBase) && isDying()) {
        setDying(false);
    }
}

const char* Archipelago::getDeathContext() {
    PlayerActorHakoniwa* mainPlayer = getPlayerActorHakoniwa();
    if (!mainPlayer)
        return "";

    std::string deathMessage = "";
    const char* deathAnim = mainPlayer->mAnimator->mCurAnim.cstr();
    deathMessage.append(getSlot());
    // if (al::isEqualString(deathAnim, "Fall")) // 3d
    if (al::isEqualString(deathAnim, "DeadFall")) // 2d
        deathMessage.append(" fell in a pit.");
    if (al::isEqualString(deathAnim, "DeadFire"))
        deathMessage.append(" burned to death.");
    if (al::isEqualString(deathAnim, "Dead"))
        deathMessage.append(" wasn't watching their health.");
    if (al::isEqualString(deathAnim, "DeadPoison"))
        deathMessage.append(" tried to swim in grape juice.");
    if (al::isEqualString(deathAnim, "DeadWallow"))
        deathMessage.append(" couldn't breath.");
    if (al::isEqualString(deathAnim, "DeadWater"))
        deathMessage.append(" drowned.");
    if (al::isEqualString(deathAnim, "DeadIce"))
        deathMessage.append(" froze.");
    if (al::isEqualString(deathAnim, "DeadSand"))
        deathMessage.append(" sunk in quicksand.");

    return deathMessage.c_str();
}

bool Archipelago::isTargetAlive() {
    if (mCoinCollectHintTarget) {
        return al::isAlive(mCoinCollectHintTarget);
    }

    return false;
}

void Archipelago::startShineCount() {
    sCurScene->stageSceneLayout->startShineCountAnim(false);
    startShineChipCount();
}

void Archipelago::startShineChipCount() {
    sCurScene->stageSceneLayout->updateCounterParts();
}