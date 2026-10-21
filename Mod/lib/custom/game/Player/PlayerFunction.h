#pragma once

#include "Library/Light/ModelMaterialCategory.h"
#include "Library/LiveActor/ActorInitInfo.h"
#include "Library/LiveActor/LiveActor.h"
#include "Library/LiveActor/LiveActorFunction.h"
#include "Library/Model/ModelCtrl.h"
#include "Library/Obj/PartsFunction.h"
#include "Library/Resource/ActorResource.h"
#include "Player/PlayerConst.h"
#include "Player/PlayerCostumeInfo.h"

class PlayerJointControlPartsDynamics;

class PlayerFunction {
public:
    static int getPlayerInputPort(const al::LiveActor*);
    static bool tryActivateAmiiboPreventDamage(const al::LiveActor*);
    static bool isPlayerDeadStatus(const al::LiveActor* player);
    static void syncBodyHairVisibility(al::LiveActor*, al::LiveActor*);
    static void syncMarioFaceBeardVisibility(al::LiveActor*, al::LiveActor*);
    static void syncMarioHeadStrapVisibility(al::LiveActor*);
    static bool isNeedHairControl(const PlayerBodyCostumeInfo*, const char*);
    static bool isInvisibleCap(const PlayerCostumeInfo*);
    static void hideHairVisibility(al::LiveActor*);

    static PlayerConst* createMarioConst(const char*);
    static void createCapModelName(sead::BufferedSafeStringBase<char>*, const char*);

    static void initMarioModelActor2D(al::LiveActor* actor, const al::ActorInitInfo& initInfo, const char* model2DName, bool isInvisCap);
    static al::Resource* initCapModelActor(al::LiveActor*, const al::ActorInitInfo&, const char*);
    static al::Resource* initCapModelActorDemo(al::LiveActor*, const al::ActorInitInfo&, const char*);
    static PlayerCostumeInfo* initMarioModelActor(al::LiveActor* player, const al::ActorInitInfo& initInfo, const char* modelName, const char* capType,
        al::AudioKeeper* keeper, bool isCloset);

    static PlayerCostumeInfo* initMarioModelActorDemo(PlayerJointControlPartsDynamics** jointCtrlPtr, al::LiveActor* player, const al::ActorInitInfo& initInfo,
        const char* bodyName, const char* capName, const PlayerConst* pConst, sead::Vector3f* noseScale,
        sead::Vector3f* earScale, bool isCloset);

    static PlayerCostumeInfo* initMarioModelCommon(al::LiveActor* player, const al::ActorInitInfo& initInfo, const char* bodyName, const char* capName,
        int subActorNum, bool isDemo, al::AudioKeeper* audioKeeper, bool guessIsChromaKey, bool isCloset);
    // not a real symbol, func at 0x445A24
    static void initMarioAudio(al::LiveActor* player, const al::ActorInitInfo& initInfo, al::Resource* modelRes, bool isDemo, al::AudioKeeper* audioKeeper);
    // not a real symbol, func at 0x448B8C
    static void initMarioSubModel(al::LiveActor* subactor, const al::ActorInitInfo& initInfo, bool isInvisible, bool isDemo, bool isChromaKey, bool isCloset);
    // not a real symbol, func at 0x445128
    static PlayerHeadCostumeInfo* initMarioHeadCostumeInfo(al::LiveActor* player, const al::ActorInitInfo& initInfo, const char*, const char*, const char*,
        const char*, bool, bool, bool, bool, bool);
    // not a real symbol, func at 0x445DF4
    static void initMarioDepthModel(al::LiveActor* player, bool isDemo, bool isChromaKey);
};
