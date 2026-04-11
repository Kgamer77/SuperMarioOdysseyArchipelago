#include "main.hpp"
#include <cmath>
#include <math.h>
#include "al/execute/ExecuteOrder.h"
#include "al/execute/ExecuteTable.h"
#include "al/execute/ExecuteTableHolderDraw.h"
#include "al/util/GraphicsUtil.h"
#include "container/seadSafeArray.h"
#include "game/GameData/GameDataHolderAccessor.h"
#include "game/Player/PlayerActorBase.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Player/PlayerCostumeFunction.h"
#include "game/Player/PlayerCostumeInfo.h"
#include "game/Layouts/ShopLayoutInfo.h"
#include "game/Layouts/CommonHorizontalList.h"
#include "heap/seadHeap.h"
#include "math/seadVector.h"
#include "server/Client.hpp"
#include "puppets/PuppetInfo.h"
#include "actors/PuppetActor.h"
#include "al/LiveActor/LiveActor.h"
#include "al/util.hpp"
#include "al/util/AudioUtil.h"
#include "al/util/CameraUtil.h"
#include "al/util/ControllerUtil.h"
#include "al/util/LiveActorUtil.h"
#include "al/util/HeapUtil.h"
#include "al/util/NerveUtil.h"
#include "al/layout/IUseLayout.h"
#include "debugMenu.hpp"
#include "game/GameData/GameDataFunction.h"
#include "game/HakoniwaSequence/HakoniwaSequence.h"
#include "game/Player/PlayerFunction.h"
#include "game/StageScene/StageScene.h"
#include "helpers.hpp"
#include "layouts/HideAndSeekIcon.h"
#include "logger.hpp"
#include "rs/util.hpp"
#include "server/gamemode/GameModeBase.hpp"
#include "server/hns/HideAndSeekMode.hpp"
#include "server/gamemode/GameModeManager.hpp"
#include "sead/prim/seadSafeString.h"
#include "sead/prim/seadSafeString.hpp"
#include "game/Demo/DemoStateHackFirst.h"
#include "game/Actors/GrowFlowerPot.h"

static int pInfSendTimer = 0;
static int gameInfSendTimer = 0;
static int checksSyncTimer = 0;
static bool isRecordCapture = false;
static int updateCounterTimer = 0;

void updatePlayerInfo(GameDataHolderAccessor holder, PlayerActorBase* playerBase, bool isYukimaru) {
    
    if (pInfSendTimer >= 3) {

        Client::sendPlayerInfPacket(playerBase, isYukimaru);

        if (!isYukimaru) {
            Client::sendHackCapInfPacket(((PlayerActorHakoniwa*)playerBase)->mHackCap);

            Client::sendCaptureInfPacket((PlayerActorHakoniwa*)playerBase);
        }
        
        if (Client::getCapturesFlag()) {
            al::LiveActor* curHack = playerBase->getPlayerHackKeeper()->currentHackActor;
            const char* hackName = playerBase->getPlayerHackKeeper()->getCurrentHackName();
            if (hackName != nullptr && !Client::hasCapture(hackName) && isRecordCapture) {
                if (!(al::isEqualString(hackName, "ElectricWire") && Client::getScenario(0) < 2
                    && GameDataFunction::getCurrentWorldId(holder) == 0)) {
                    //Client::setMessage(1, hackNamehackName);
                    if (!playerBase->getPlayerHackKeeper()->isActiveHackStartDemo()) {
                        bool tryEscape = false;
                        int nonKillCaptures[7] = {10, 13, 24, 25, 28, 29, 37};
                        for (int i = 0; i < 7; i++) {
                            tryEscape =
                                al::isEqualString(captureListNames[nonKillCaptures[i]], hackName);
                            if (tryEscape) {
                                break;
                            }
                        }
                        if (tryEscape) {
                            playerBase->getPlayerHackKeeper()->tryEscapeHack();
                        } else {
                            playerBase->getPlayerHackKeeper()->forceKillHack();
                        }
                        isRecordCapture = false;
                    }
                }
            }
        }

        if (!(al::isEqualString(GameDataFunction::tryGetCurrentMainStageName(holder),
                              "CapWorldHomeStage") &&
              Client::getScenario(0) < 2) &&
            rs::isExistShineChipWatcher(playerBase) &&
            rs::getShineChipCount(playerBase) > 0) {
            Client::startShineChipCount();
        }
        pInfSendTimer = 0;
    }

    if (gameInfSendTimer >= 60) {
        // Check and prevent crashed home softlock
        if (GameDataFunction::isBossAttackedHome(holder)) {
            //Client::setMessage(1, GameDataFunction::getCurrentStageName(holder));
            if (strcmp(GameDataFunction::getCurrentStageName(holder), "BossRaidWorldHomeStage") ==
                0) {

                GameDataFunction::repairHomeByCrashedBoss(holder);
                GameDataFunction::crashHome(holder);
                // isGotShine crashes game here for some reason
                /*int ruinedCount = 0;
                if (GameDataFunction::isGotShine(holder, GameDataFunction::getWorldIndexBoss(),
                                                 0)) {
                    ruinedCount += 3;
                }

                for (int i = 1; i < 9; i++) {
                    if (GameDataFunction::isGotShine(holder, GameDataFunction::getWorldIndexBoss(),
                                                     i)) {
                        ruinedCount++;
                    }
                }
                if (ruinedCount < Client::getRaidCount()) {
                    GameDataFunction::repairHome(holder);
                } else {
                    GameDataFunction::bossAttackHome(holder);
                }*/
            } else {
                GameDataFunction::repairHome(holder); 
            }
        }

        // Edge case where game repairs odyssey in ruined but doesn't unlock bowser kingdom
        if (GameDataFunction::isRepairHomeByCrashedBoss(holder)) {
            GameDataFunction::unlockWorld(holder, GameDataFunction::getWorldIndexSky());
        }

        // Check for lost kingdom softlock state
        if (GameDataFunction::isCrashHome(holder)) {
            if (strcmp(GameDataFunction::getCurrentStageName(holder), "ClashWorldHomeStage") == 0) {
                int lostCount = 0;
                for (int i = 1; i < 25; i++) {
                    if (GameDataFunction::isGotShine(holder, GameDataFunction::getWorldIndexClash(), i))
                        lostCount++;
                }
                if (lostCount < Client::getWorldUnlockCount(GameDataFunction::getWorldIndexClash())) {
                    GameDataFunction::repairHome(holder);
                    GameDataFunction::unlockWorld(holder, GameDataFunction::getWorldIndexClash());
                } else {
                    GameDataFunction::crashHome(holder);
                }
            } else {
                GameDataFunction::repairHome(holder);
            }
        }

        // Death Link handling
        if (isInGame && !PlayerFunction::isPlayerDeadStatus(playerBase) && Client::isApDeath())
        {
            GameDataFunction::killPlayer(holder);
            playerBase->startDemoPuppetable();
            al::setVelocityZero(playerBase);
            rs::faceToCamera(playerBase);
            ((PlayerActorHakoniwa*)playerBase)->mPlayerAnimator->endSubAnim();
            ((PlayerActorHakoniwa*)playerBase)->mPlayerAnimator->startAnimDead();
            Client::setApDeath(false);
        }

        if (isInGame && PlayerFunction::isPlayerDeadStatus(playerBase) && !Client::isDying())
        {
            Client::sendDeathlinkPacket();
            Client::setDying(true);
        }

        if (isInGame && !PlayerFunction::isPlayerDeadStatus(playerBase) && Client::isDying())
        {
            Client::setDying(false);
        }

        if (isInGame && updateCounterTimer >= 1800)
        {
            Client::startShineCount();
            updateCounterTimer = 0;
        }

        if (isYukimaru) {
            Client::sendGameInfPacket(holder);
        } else {
            Client::sendGameInfPacket((PlayerActorHakoniwa*)playerBase, holder);
        }

        gameInfSendTimer = 0;
    }

    pInfSendTimer++;
    gameInfSendTimer++;
    updateCounterTimer++;
}

// ------------- Hooks -------------

int debugPuppetIndex = 0;
int debugCaptureIndex = 0;
static int pageIndex = 0;

static const int maxPages = 3;

void drawMainHook(HakoniwaSequence* curSequence, sead::Viewport* viewport,
                  sead::DrawContext* drawContext) {
    // sead::FrameBuffer *frameBuffer;
    // __asm ("MOV %[result], X21" : [result] "=r" (frameBuffer));

    // if(Application::sInstance->mFramework) {
    //     Application::sInstance->mFramework->mGpuPerf->drawResult((agl::DrawContext *)drawContext,
    //     frameBuffer);
    // }

    Time::calcTime();  // this needs to be ran every frame, so running it here works

    int dispHeight = al::getLayoutDisplayHeight();

    gTextWriter->mViewport = viewport;

    gTextWriter->mColor = sead::Color4f(1.f, 1.f, 1.f, 0.8f);

    al::Scene* curScene = curSequence->curScene;

    if (curScene && isInGame) {
        isInGame = false;
        sead::LookAtCamera* cam = al::getLookAtCamera(curScene, 0);
        sead::Projection* projection = al::getProjectionSead(curScene, 0);

        sead::PrimitiveRenderer* renderer = sead::PrimitiveRenderer::instance();
        renderer->setDrawContext(drawContext);
        renderer->setCamera(*cam);
        renderer->setProjection(*projection);

        if (!debugMode) {
            if (!(Client::getAPChatMessage1() == Client::getAPChatMessage2() &&
                  Client::getAPChatMessage2() == Client::getAPChatMessage3())) {
                if (Client::getAPChatMessage1() == Client::getAPChatMessage2())
                    drawApChatBackground((agl::DrawContext*)drawContext, 3.f);
                else if (Client::getAPChatMessage1().isEmpty())
                    drawApChatBackground((agl::DrawContext*)drawContext, 2.f);
                else
                    drawApChatBackground((agl::DrawContext*)drawContext, 1.f);

                gTextWriter->beginDraw();
                gTextWriter->setCursorFromTopLeft(
                    sead::Vector2f(10.f, (dispHeight * 7 / 10) + 60.f));
                gTextWriter->setScaleFromFontHeight(15.f);

                gTextWriter->printf("%s\n", Client::getAPChatMessage1().cstr());
                gTextWriter->printf("%s\n", Client::getAPChatMessage2().cstr());
                gTextWriter->printf("%s\n", Client::getAPChatMessage3().cstr());
                gTextWriter->endDraw();
            }

            al::executeDraw(curSequence->mLytKit, "２Ｄバック（メイン画面）");
            return;
        }

        // int dispWidth = al::getLayoutDisplayWidth();

        gTextWriter->printf("FPS: %d\n",
                            static_cast<int>(round(Application::sInstance->mFramework->calcFps())));

        drawBackground((agl::DrawContext*)drawContext);

        gTextWriter->setCursorFromTopLeft(sead::Vector2f(10.f, (dispHeight / 3) + 30.f));
        gTextWriter->setScaleFromFontHeight(20.f);

        sead::Heap* clientHeap = Client::getClientHeap();
        sead::Heap* gmHeap = GameModeManager::instance()->getHeap();

        if (clientHeap) {
            gTextWriter->printf("Client Heap Free Size: %f/%f\n",
                                clientHeap->getFreeSize() * 0.001f, clientHeap->getSize() * 0.001f);
            gTextWriter->printf("Gamemode Heap Free Size: %f/%f\n", gmHeap->getFreeSize() * 0.001f,
                                gmHeap->getSize() * 0.001f);
        }

        gTextWriter->printf("Client Socket Connection Status: %s\n",
                            Client::instance()->mSocket->getStateChar());
        gTextWriter->printf("Udp socket status: %s\n",
                            Client::instance()->mSocket->getUdpStateChar());
        // gTextWriter->printf("nn::socket::GetLastErrno: 0x%x\n",
        // Client::instance()->mSocket->socket_errno);
        gTextWriter->printf("Connected Players: %d/%d\n", Client::getConnectCount() + 1,
                            Client::getMaxPlayerCount());

        gTextWriter->printf("Send Queue Count: %d/%d\n",
                            Client::instance()->mSocket->getSendCount(),
                            Client::instance()->mSocket->getSendMaxCount());
        gTextWriter->printf("Recv Queue Count: %d/%d\n",
                            Client::instance()->mSocket->getRecvCount(),
                            Client::instance()->mSocket->getRecvMaxCount());

        PlayerActorBase* playerBase = rs::getPlayerActor(curScene);

        PuppetActor* curPuppet = Client::getPuppet(debugPuppetIndex);

        PuppetActor* debugPuppet = Client::getDebugPuppet();

        if (debugPuppet) {
            curPuppet = debugPuppet;
        }

        gTextWriter->printf("----------- Page %d ------------\n", pageIndex);
        switch (pageIndex) {
        case 0: {
            // PuppetActor *curPuppet = Client::getDebugPuppet();

            if (curPuppet) {
                al::LiveActor* curModel = curPuppet->getCurrentModel();

                PuppetInfo* curPupInfo = curPuppet->getInfo();

                if (curModel && curPupInfo) {
                    // al::LiveActor *curCapture = curPuppet->getCapture(debugCaptureIndex);

                    gTextWriter->printf("Puppet Index: %d\n", debugPuppetIndex);
                    gTextWriter->printf("Player Name: %s\n", curPupInfo->puppetName);
                    gTextWriter->printf("Connection Status: %s\n",
                                        curPupInfo->isConnected ? "Online" : "Offline");
                    gTextWriter->printf("Is in Same Stage: %s\n",
                                        curPupInfo->isInSameStage ? "True" : "False");
                    gTextWriter->printf("Is in Capture: %s\n",
                                        curPupInfo->isCaptured ? "True" : "False");
                    gTextWriter->printf("Puppet Stage: %s\n", curPupInfo->stageName);
                    gTextWriter->printf("Puppet Scenario: %u\n", curPupInfo->scenarioNo);
                    gTextWriter->printf("Puppet Costume: H: %s B: %s\n",
                                        curPupInfo->costumeHead, curPupInfo->costumeBody);
                    // gTextWriter->printf("Packet Coords:\nX: %f\nY: %f\nZ: %f\n",
                    // curPupInfo->playerPos.x, curPupInfo->playerPos.y,
                    // curPupInfo->playerPos.z);
                    //  if (curModel) {
                    //      sead::Vector3f* pupPos = al::getTrans(curModel);
                    //      gTextWriter->printf("In-Game Coords:\nX: %f\nY: %f\nZ: %f\n",
                    //      pupPos->x, pupPos->y, pupPos->z);
                    //  }

                    if (curPupInfo->isCaptured) {
                        gTextWriter->printf("Current Capture: %s\n", curPupInfo->curHack);
                        gTextWriter->printf("Current Packet Animation: %s\n",
                                            curPupInfo->curAnimStr);
                        gTextWriter->printf("Animation Index: %d\n", curPupInfo->curAnim);
                    } else {
                        gTextWriter->printf("Current Packet Animation: %s\n",
                                            curPupInfo->curAnimStr);
                        gTextWriter->printf("Animation Index: %d\n", curPupInfo->curAnim);
                        if (curModel) {
                            gTextWriter->printf("Current Animation: %s\n",
                                                al::getActionName(curModel));
                        }
                    }
                }
            }
        } break;
        case 1: {
            PuppetActor* debugPuppet = Client::getDebugPuppet();
            PuppetInfo* debugInfo = Client::getDebugPuppetInfo();

            if (debugPuppet && debugInfo) {
                al::LiveActor* curModel = debugPuppet->getCurrentModel();

                gTextWriter->printf("Is Debug Puppet Tagged: %s\n", BTOC(debugInfo->isIt));
            }
        } break;
        case 2: {
            PlayerHackKeeper* hackKeeper = playerBase->getPlayerHackKeeper();

            if (hackKeeper) {
                PlayerActorHakoniwa* p1 = (PlayerActorHakoniwa*)
                    playerBase;  // its safe to assume that we're using a playeractorhakoniwa if
                                    // the hack keeper isnt null

                if (hackKeeper->currentHackActor) {
                    al::LiveActor* curHack = hackKeeper->currentHackActor;

                    gTextWriter->printf("Current Hack Animation: %s\n",
                                        al::getActionName(curHack));
                    gTextWriter->printf("Current Hack Name: %s\n",
                                        hackKeeper->getCurrentHackName());
                    sead::Quatf captureRot = curHack->mPoseKeeper->getQuat();
                    gTextWriter->printf("Current Hack Rot: %f %f %f %f\n", captureRot.x,
                                        captureRot.y, captureRot.z, captureRot.w);
                    sead::Quatf calcRot;
                    al::calcQuat(&calcRot, curHack);
                    gTextWriter->printf("Calc Hack Rot: %f %f %f %f\n", calcRot.x, calcRot.y,
                                        calcRot.z, calcRot.w);
                } else {
                    gTextWriter->printf("Cur Action: %s\n",
                                        p1->mPlayerAnimator->mAnimFrameCtrl->getActionName());
                    gTextWriter->printf("Cur Sub Action: %s\n",
                                        p1->mPlayerAnimator->curSubAnim.cstr());
                    gTextWriter->printf("Is Cappy Flying? %s\n",
                                        BTOC(p1->mHackCap->isFlying()));
                    if (p1->mHackCap->isFlying()) {
                        gTextWriter->printf("Cappy Action: %s\n",
                                            al::getActionName(p1->mHackCap));
                        sead::Vector3f* capTrans = al::getTransPtr(p1->mHackCap);
                        sead::Vector3f* capRot = &p1->mHackCap->mJointKeeper->mJointRot;
                        gTextWriter->printf("Cap Coords:\nX: %f\nY: %f\nZ: %f\n", capTrans->x,
                                            capTrans->y, capTrans->z);
                        gTextWriter->printf("Cap Rot:\nX: %f\nY: %f\nZ: %f\n", capRot->x,
                                            capRot->y, capRot->z);
                        gTextWriter->printf("Cap Skew: %f\n",
                                            p1->mHackCap->mJointKeeper->mSkew);
                    }
                }
            }

        } break;
        default:
            break;
        }

        renderer->begin();

        // sead::Matrix34f mat = sead::Matrix34f::ident;
        // mat.setBase(3, sead::Vector3f::zero); // Sets the position of the matrix.
        //  For cubes, you need to put this at the location.
        //  For spheres, you can leave this at 0 0 0 since you set it in its draw function.
        renderer->setModelMatrix(sead::Matrix34f::ident);

        if (curPuppet) {
            renderer->drawSphere4x8(curPuppet->getInfo()->playerPos, 20,
                                    sead::Color4f(1.f, 0.f, 0.f, 0.25f));
            renderer->drawSphere4x8(al::getTrans(curPuppet), 20,
                                    sead::Color4f(0.f, 0.f, 1.f, 0.25f));
        }

        renderer->end();
        
    }

    gTextWriter->endDraw();

    al::executeDraw(curSequence->mLytKit, "２Ｄバック（メイン画面）");

}

bool isGrabShine(GameDataHolderAccessor accessor, int shineIdx) {
    GameDataFile::HintInfo* curHintInfo =
        &accessor.mData->mGameDataFile->mShineHintList[shineIdx];
    if (!curHintInfo->mIsGrand || (curHintInfo->mUniqueID == 159 && Client::getScenario(3) < 3)) {
        if (curHintInfo->mUniqueID == 205 && Client::getScenario(1) > 1 ||
            curHintInfo->mUniqueID == 129)
        {
            return true;
        }
        return Client::hasShine(curHintInfo->mUniqueID);
    }
    return false;
}

bool isGotShineRedirect(const Shine* curShine)
{
    GameDataHolderAccessor accessor = GameDataHolderAccessor(curShine);
    return isGrabShine(accessor, curShine->mShineIdx);
}

bool isGotShineByHintIdx(GameDataHolderAccessor accessor, int worldId, int hintIdx)
{
    if (accessor.mData->mGameDataFile) {
        GameDataFile::HintInfo* curHintInfo =
            accessor.mData->mGameDataFile->findShineByHintIdx(worldId, hintIdx);
        sead::FixedSafeString<60> str;
        str = "";
        str.append("World Id: ");
        str.append(intToCstr(worldId));
        str.append(" shineIdx: ");
        str.append(intToCstr(hintIdx));
        str.append(" Uid: ");
        str.append(intToCstr(curHintInfo->mUniqueID));
        str.append(" UnkBool: ");
        str.append(intToCstr(curHintInfo->unkBool1));
        str.append(" ???: ");
        str.append(intToCstr(curHintInfo->unkInt));
        str.append(" ???: ");
        str.append(intToCstr(curHintInfo->unkInt2));
        //Client::setMessage(1, str.cstr());
        if (curHintInfo) {
            return Client::hasShine(curHintInfo->mUniqueID);
        }
    }
    return false;
}

void sendShinePacket(GameDataHolderAccessor thisPtr, Shine* curShine) {

    GameDataFile::HintInfo* curHintInfo =
    &thisPtr.mData->mGameDataFile->mShineHintList[curShine->mShineIdx];

    Client::setRecentShine(curShine);

    if (curHintInfo->mUniqueID == 0) {
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "CapWorldHomeStage") == 0) {
            Client::sendCheckPacket(1086, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "SandWorldHomeStage") == 0) {
            Client::sendCheckPacket(1096, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "LakeWorldHomeStage") == 0) {
            Client::sendCheckPacket(1094, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "ForestWorldHomeStage") == 0) {
            Client::sendCheckPacket(1089, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "CityWorldHomeStage") == 0) {
            Client::sendCheckPacket(1088, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "SnowWorldHomeStage") == 0) {
            Client::sendCheckPacket(1087, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "SeaWorldHomeStage") == 0) {
            Client::sendCheckPacket(1095, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "LavaWorldHomeStage") == 0) {
            Client::sendCheckPacket(1090, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "SkyWorldHomeStage") == 0) {
            Client::sendCheckPacket(1091, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "MoonWorldHomeStage") == 0) {
            Client::sendCheckPacket(1165, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "PeachWorldHomeStage") == 0) {
            Client::sendCheckPacket(1152, -1);
        }
        if (strcmp(curShine->curShineInfo->stageName.cstr(), "Special1WorldHomeStage") == 0) {
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "WaterfallWorldHomeStage") == 0) {
                Client::sendCheckPacket(1132, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "LakeWorldHomeStage") == 0) {
                Client::sendCheckPacket(1128, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "CloudWorldHomeStage") == 0) {
                Client::sendCheckPacket(1124, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "ClashWorldHomeStage") == 0) {
                Client::sendCheckPacket(1126, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "CityWorldHomeStage") == 0) {
                Client::sendCheckPacket(1130, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "SnowWorldHomeStage") == 0) {
                Client::sendCheckPacket(1129, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "SeaWorldHomeStage") == 0) {
                Client::sendCheckPacket(1127, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "LavaWorldHomeStage") == 0) {
                Client::sendCheckPacket(1123, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "BossRaidWorldHomeStage") == 0) {
                Client::sendCheckPacket(1125, -1);
            }
            if (strcmp(GameDataFunction::tryGetCurrentMainStageName(thisPtr),
                       "PeachWorldHomeStage") == 0) {
                Client::sendCheckPacket(1131, -1);
            }
        }
    } else {
        Client::sendCheckPacket(curHintInfo->mUniqueID, -1);
    }
    // Add some way to sync shinechecks grabbed before connecting, probably handle on connect or something
    Client::addShine(curHintInfo->mUniqueID);
}

void sendItemPacket(GameDataFile thisPtr, ShopItem::ItemInfo* info, bool flag) {

    int itemType = static_cast<int>(info->mType);

    switch (itemType) 
    { 
        case 0:
            Client::sendCheckPacket(getIndexCostumeList(info->mName), itemType);
            break;
        case 1:
            Client::sendCheckPacket(getIndexCostumeList(info->mName), itemType);
            break;
        case 2:
            Client::sendCheckPacket(getIndexSouvenirList(info->mName), itemType);
            break;
        case 3:
            Client::sendCheckPacket(getIndexStickerList(info->mName), itemType);
            break;
    }
    
    Client::addItem(info);
    //thisPtr.buyItem(info, flag);
}

void sendCollectPacket(GameDataHolderAccessor thisPtr, al::PlacementId* placementId)
{
    if (Client::getRegionalsFlag())
    {
        sead::FixedSafeString<0x20> placementString;
        placementId->makeString(&placementString);
        Client::sendCheckPacket(4, placementString.cstr(), GameDataFunction::getCurrentStageName(thisPtr));
    }
    else
    {
        GameDataFunction::addCoinCollect(thisPtr, placementId);
    }
    
    // Add flag in client to determine when option is disabled and pass regularly to GameDataFunction
}

int getUnlockShineNum(GameDataHolder* thisPtr, bool* unkBool, int worldId)
{
    if (worldId < 1 || worldId > 16)
    {
        worldId = 0;
    }
    //*unkBool = thisPtr->mGameDataFile->mGameProgressData->isUnlockWorld(worldId);
    //if (*unkBool) {
    return Client::getWorldUnlockCount(worldId);
    //}
    //return -1;
}

int getUnlockShineNumByAccessor(bool* unkBool, GameDataHolderAccessor accessor)
{
    int worldId = accessor.mData->mGameDataFile->mCurWorldID;
    if (worldId < 1 || worldId > 16)
    {
        worldId = 0;
    }
    //*unkBool = GameDataFunction::isUnlockedWorld(accessor, worldId);
    //if (*unkBool) {
        return Client::getWorldUnlockCount(worldId);
    //}
    return -1;
}

int getUnlockShineNumByGameDataFile(GameDataFile* file, bool* unkBool)
{
    int worldId = file->mCurWorldID;
    if (worldId < 1 || worldId > 16) {
        worldId = 0;
    }
    //*unkBool = file->mGameProgressData->isUnlockWorld(worldId);
    //if (*unkBool) {
    return Client::getWorldUnlockCount(worldId);
    //}
    //return -1;
}

int getUnlockShineNumByWorldId(bool* unkBool, GameDataHolder* thisPtr, int worldId)
{
    return getUnlockShineNum(thisPtr, unkBool, worldId);
}

void onGrandShineStageChange(GameDataHolderWriter holder, ChangeStageInfo const* stageInfo) 
{
    Client::sendStage(holder, stageInfo);
}

void onStageChange(GameDataFile *file,const ChangeStageInfo* stageInfo, int param2)
{
    //Client::setMessage(1, stageInfo->changeStageId.cstr());
    // Add Wooded shop moon warp
    if (!(al::isEqualString(stageInfo->changeStageId.cstr(), "obj846") ||
        al::isEqualString(stageInfo->changeStageId.cstr(), "obj1084"))) {
        if (isPartOf(stageInfo->changeStageName.cstr(), "WorldHomeStage")) {
            if (Client::setScenario(stageInfo->changeStageName.cstr(), stageInfo->scenarioNo)) {
                //Client::setMessage(2, "attempting send to correct scenario");
                Client::sendCorrectScenario(stageInfo);

            } else {
                //Client::setMessage(2, "setScenario false");
                file->changeNextStage(stageInfo, param2);
            }
        } else {
            // Non world transitions
            //Client::setMessage(2, "non world transition");
            file->changeNextStage(stageInfo, param2);
        }
    } else {
        // Catch cap and cascade shop moons
        //Client::setMessage(2, "Shop moon stageID caught");
        file->changeNextStage(stageInfo, param2);
    }
    
}

void setShineLabel(al::IUseLayout* layout, const char* elementLabel)
{
    al::setPaneStringFormat(layout, elementLabel, Client::getShineReplacementText());
}

void setShineColor(Shine* thisPtr, char* stageName, int color, bool isSetMtpColor)
{
    // Get color here using shine unique id
    //Client::setMessage(1, "Set custom shine color");
    int storedColor = Client::getShineColor(thisPtr);
    rs::setStageShineAnimFrame((al::LiveActor*)thisPtr, stageName, storedColor, isSetMtpColor);
}

void setShineModelColor(Shine* thisPtr, char* stageName, int color, bool isSetMtpColor) 
{
    // Get color here using shine unique id
    //Client::setMessage(1, "Set custom other shine color");
    int storedColor = Client::getShineColor(thisPtr);
    rs::setStageShineAnimFrame(thisPtr->mModelShine, stageName, storedColor, isSetMtpColor);
}

const char16_t* getShopItemMessage(al::IUseMessageSystem const* messageSystem, char const* fileName, char const* key) 
{
    const char16_t* msg =  Client::getShopReplacementText(fileName, key);
    sead::WFixedSafeString<200> confirm;
    confirm = confirm.cEmptyString;
    confirm.append(msg);
    if (!confirm.isEmpty())
    {
        return msg;
    }
    // Default to base game text if no ap text exists
    return al::getSystemMessageString(messageSystem, fileName, key);
    
}

bool isBuyItems(ShopItem::ItemInfo* itemInfo) {
    // Add a collected outfits, gifts, stickers based implementation similar to shinechecks
    
    return false;
}

//isExistInHackDictionary for capture tracking
void onAddHack(GameDataHolderWriter writer,const char* hackName)
{
    if (Client::getCapturesFlag()) {
        //Client::setMessage(2, hackName);
        Client::sendCheckPacket(getIndexCaptureList(hackName), 5);
        isRecordCapture = true;
    } else {
        GameDataFunction::addHackDictionary(writer, hackName);
    }
}

void canEndHack(al::LiveActor* actor)
{
    if (actor != nullptr)
    {
        PlayerHackKeeper::endHackStartDemo(actor);
    }
}

void onStartHack(PlayerHackKeeper* keeper, al::HitSensor* hitSensor1, al::HitSensor* hitSensor2, al::LiveActor* actor) 
{
    keeper->startHack(hitSensor1, hitSensor2, actor);
}

bool growOnPlant(GrowFlowerPot* thisPtr) {
    // Instantly bump the growth level
    rs::addGrowFlowerGrowLevel(thisPtr, thisPtr->mPlacementId, 3);

    return al::isActionEnd(thisPtr);
}


// _ZN16HakoniwaSequence15exeBootLoadDataEv = 0x50F29C - 0x50F304
void onNewGameDemoStart(char* name, bool unkBool) {
    for (int i = 0; i < 18; i++) {
        Client::setScenario(i, 1);
    }

    for (int i = 0; i < 25; i++) {
        Client::setShineChecks(i, 0);
    }

    for (int i = 0; i < 12; i++) {
        Client::setOutfitChecks(i, 0);
    }

    for (int i = 0; i < 4; i++) {
        Client::setStickerChecks(i, 0);
    }

    for (int i = 0; i < 5; i++) {
        Client::setSouvenirChecks(i, 0);
    }

    for (int i = 0; i < 8; i++) {
        Client::setCaptureChecks(i, 0);
    }

    Client::setCheckIndex(-1);

    //al::initActorWithArchiveName(thisPtr, info, str, name);
    al::createSceneHeap(name, unkBool);
    return;
}

// First time entering lost in demo from cloud
void onUnlockLost(GameDataHolderWriter writer, int worldIndex)
{
    // Send Beat Bowser in Cloud location
    Client::sendCheckPacket(2500, -1);
    //Client::setScenario(GameDataFunction::getWorldIndexCloud(), 2);
    
    GameDataFunction::unlockWorld(writer, worldIndex);

    return;
}

void onCreditsStart(al::Scene* thisPtr, const al::SceneInitInfo info) {

    //Client::setScenario(GameDataFunction::getWorldIndexPeach(), 2);
    Client::sendCheckPacket(2499, -1);
    
    thisPtr->initDrawSystemInfo(info);
    return;
}

bool skipHackCutscene(DemoStateHackFirst* thisPtr, IUsePlayerHack** param_1, al::SensorMsg* param_2,
    al::HitSensor* param_3, al::HitSensor* param_4)
{
    return false;
}

void stageInitHook(al::ActorInitInfo *info, StageScene *curScene, al::PlacementInfo const *placement, al::LayoutInitInfo const *lytInfo, al::ActorFactory const *factory, al::SceneMsgCtrl *sceneMsgCtrl, al::GameDataHolderBase *dataHolder) {

    al::initActorInitInfo(info, curScene, placement, lytInfo, factory, sceneMsgCtrl,
                          dataHolder);

    Client::clearArrays();

    Client::setSceneInfo(*info, curScene);

    if (GameModeManager::instance()->getGameMode() != NONE) {
        GameModeInitInfo initModeInfo(info, curScene);
        initModeInfo.initServerInfo(GameModeManager::instance()->getGameMode(), Client::getPuppetHolder());

        GameModeManager::instance()->initScene(initModeInfo);
    }

    Client::sendGameInfPacket(info->mActorSceneInfo.mSceneObjHolder);
    GameDataHolderAccessor accessor = GameDataHolderAccessor(info->mActorSceneInfo.mSceneObjHolder);

    if (!GameDataFunction::isHomeShipStage(accessor.mData)) {
        Client::sendChangeStagePacket(info->mActorSceneInfo.mSceneObjHolder);
    }
    int worldId = GameDataFunction::getCurrentWorldId(info->mActorSceneInfo.mSceneObjHolder);
    int worldScenario =
        GameDataFunction::getWorldScenarioNo(info->mActorSceneInfo.mSceneObjHolder, worldId);

    // Enable Cappy on load into Cap Intro
    if (worldId == 0 && worldScenario < 2 && !GameDataFunction::isEnableCap(accessor))
    {
		__asm("MOVZ W0, #0"); // Skips the laying down anim, stopping a softlock on new game (Anim is called: DemoOpeningDown)
        GameDataFunction::enableCap(accessor);
    }

    if (worldScenario > Client::getScenario(worldId))
    {
        Client::setScenario(worldId, worldScenario);
    }

}

PlayerCostumeInfo *setPlayerModel(al::LiveActor *player, const al::ActorInitInfo &initInfo, const char *bodyModel, const char *capModel, al::AudioKeeper *keeper, bool isCloset) {
    Client::sendCostumeInfPacket(bodyModel, capModel);
    return PlayerFunction::initMarioModelActor(player, initInfo, bodyModel, capModel, keeper, isCloset);
}

al::SequenceInitInfo* initInfo;

ulong constructHook() {  // hook for constructing anything we need to globally be accesible

    __asm("STR X21, [X19,#0x208]"); // stores WorldResourceLoader into HakoniwaSequence

    __asm("MOV %[result], X20"
          : [result] "=r"(
              initInfo));  // Save our scenes init info to a gloabl ptr so we can access it later

    Client::createInstance(al::getCurrentHeap());
    GameModeManager::createInstance(al::getCurrentHeap()); // Create the GameModeManager on the current al heap

    return 0x20;
}

bool threadInit(HakoniwaSequence *mainSeq) {  // hook for initializing client class

    al::LayoutInitInfo lytInfo = al::LayoutInitInfo();

    al::initLayoutInitInfo(&lytInfo, mainSeq->mLytKit, 0, mainSeq->mAudioDirector, initInfo->mSystemInfo->mLayoutSys, initInfo->mSystemInfo->mMessageSys, initInfo->mSystemInfo->mGamePadSys);

    Client::instance()->init(lytInfo, mainSeq->mGameDataHolder);

    return GameDataFunction::isPlayDemoOpening(mainSeq->mGameDataHolder);
}

bool hakoniwaSequenceHook(HakoniwaSequence* sequence) {
    StageScene* stageScene = (StageScene*)sequence->curScene;

    static bool isCameraActive = false;

    bool isFirstStep = al::isFirstStep(sequence);

    al::PlayerHolder *pHolder = al::getScenePlayerHolder(stageScene);
    PlayerActorBase* playerBase = al::tryGetPlayerActor(pHolder, 0);
    
    bool isYukimaru = !playerBase->getPlayerInfo();

    isInGame = !stageScene->isPause();

    GameModeManager::instance()->setPaused(stageScene->isPause());
    Client::setStageInfo(stageScene->mHolder);

    Client::update();

    updatePlayerInfo(stageScene->mHolder, playerBase, isYukimaru);

    static bool isDisableMusic = false;

    if (al::isPadHoldZR(-1)) {
        if (al::isPadTriggerUp(-1)) debugMode = !debugMode;
        if (al::isPadTriggerLeft(-1)) pageIndex--;
        if (al::isPadTriggerRight(-1)) pageIndex++;
        if(pageIndex < 0) {
            pageIndex = maxPages - 1;
        }
        if(pageIndex >= maxPages) pageIndex = 0;

    } else if (al::isPadHoldZL(-1)) {

        if (debugMode) {
            if (al::isPadTriggerLeft(-1)) debugPuppetIndex--;
            if (al::isPadTriggerRight(-1)) debugPuppetIndex++;

            if(debugPuppetIndex < 0) {
                debugPuppetIndex = Client::getMaxPlayerCount() - 2;
            }
            if (debugPuppetIndex >= Client::getMaxPlayerCount() - 1)
                debugPuppetIndex = 0;
        }

    } else if (al::isPadHoldL(-1)) {
        if (al::isPadTriggerLeft(-1)) GameModeManager::instance()->toggleActive();
        if (al::isPadTriggerRight(-1)) {
            if (debugMode) {
                
                PuppetInfo* debugPuppet = Client::getDebugPuppetInfo();
                
                if (debugPuppet) {

                    debugPuppet->playerPos = al::getTrans(playerBase);
                    al::calcQuat(&debugPuppet->playerRot, playerBase);

                    PlayerHackKeeper* hackKeeper = playerBase->getPlayerHackKeeper();

                    if (hackKeeper) {
                        const char *hackName = hackKeeper->getCurrentHackName();
                        debugPuppet->isCaptured = hackName != nullptr;
                        if (debugPuppet->isCaptured) {
                            strcpy(debugPuppet->curHack, hackName);
                        } else {
                            strcpy(debugPuppet->curHack, "");
                        }
                    }
                    
                }
            }
        }
        if (al::isPadTriggerUp(-1)) {
            if (debugMode) {
                PuppetActor* debugPuppet = Client::getDebugPuppet();
                if (debugPuppet) {
                    PuppetInfo *info = debugPuppet->getInfo();
                    // info->isIt = !info->isIt;

                    debugPuppet->emitJoinEffect();
                    
                }
            } else {
                isDisableMusic = !isDisableMusic;
            }
        }
    }

    if (isDisableMusic) {
        if (al::isPlayingBgm(stageScene)) {
            al::stopAllBgm(stageScene, 0);
        }
    }

    return isFirstStep;

}

void seadPrintHook(const char *fmt, ...)
{
    va_list args;
	va_start(args, fmt);

    Logger::log(fmt, args);

    va_end(args);
}
