#pragma once

#include <math/seadVector.h>
#include <prim/seadSafeString.h>

#include "Library/LiveActor/LiveActor.h"
#include "Util/IUseDimension.h"

struct ShineInfo;
class QuestInfo;
class ChangeStageInfo;
class FukankunZoomCapMessage;

enum ShineType
{
    Normal,
    Dot,
    Grand
};

namespace al {
    class RateParamV3f;
    class ParabolicPathMovement;
    class MtxConnector;
} // namespace al

class Shine : public al::LiveActor, public IUseDimension {
public:
    Shine(const char*);

    void init(const al::ActorInitInfo&) override;
    al::LiveActor* getCurrentModel();
    bool tryExpandShadowAndClipping();
    void initAppearDemo(const al::ActorInitInfo&);
    void onAppear();
    void offAppear();
    void hideAllModel();
    void invalidateKillSensor();
    void initAfterPlacement() override;
    void getDirect();
    void updateHintTrans(const sead::Vector3f&) const;
    void appear() override;
    void makeActorAlive() override;
    void makeActorDead() override;
    void control() override;
    void updateModelActorPose();
    void attackSensor(al::HitSensor* self, al::HitSensor* other) override;
    bool receiveMsg(const al::SensorMsg* message, al::HitSensor* other, al::HitSensor* self) override;
    void showCurrentModel();
    void appearPopup();
    void addDemoActorWithModel();
    void get();
    void endClipped() override;
    void initAppearDemoFromHost(const al::ActorInitInfo&, const sead::Vector3f&);
    void initAppearDemoFromHost(const al::ActorInitInfo&);
    void initAppearDemoActorCamera(const al::ActorInitInfo&);
    void createShineEffectInsideObject(const al::ActorInitInfo&, const sead::Vector3f&, const char*);
    bool isGot() const;
    bool isEmptyShineForDemoGetGrand() const;
    void setShopShine();
    bool isEndAppear() const;
    bool isEndAppearGK() const;
    void onSwitchGet();
    s32 getColorFrame() const;
    void setHintPhotoShine(const al::ActorInitInfo&);
    bool appearCommon();
    bool tryChangeCoin();
    void tryAppearOrDemoAppear();

    void appearPopup(const sead::Vector3f&);
    void appearPopupDelay(s32);
    void appearPopupSlot(const sead::Vector3f&);
    void appearWarp(const sead::Vector3f&, const sead::Vector3f&);
    void appearStatic();
    void appearPopupWithoutDemo();
    void appearPopupGrandByBoss(s32);
    void appearPopupWithoutWarp();
    void appearAndJoinBossDemo(const char*, const sead::Quatf&, const sead::Vector3f&);

    void endBossDemo();
    void endBossDemoAndStartFall(f32);
    void appearWait();
    void appearWait(const sead::Vector3f&);
    void startHold();
    void startFall();
    void getDirectWithDemo();
    void addDemoModelActor();
    void setGrandShine();
    void exeWaitRequestDemo();
    void exeWaitKill();
    void exeDemoAppear();
    bool tryWaitCameraInterpole() const;
    bool tryStartAppearDemo();
    void calcCameraAt();

    void exeDemoMove();
    void updateIgnoreFrame();
    void exeDemoWait();
    void exeDemoGet();
    void exeDemoGetMain();
    void exeDemoGetGrand();
    void exeBossDemo();
    void exeBossDemoAfterFall();
    void exeBossDemoAfterLanding();
    void exeBossDemoFall();
    void exeBossDemoFallSlowdown();
    void exeBossDemoRise();
    void exeBossDemoRiseDamp();
    void exeAppearSlot();
    void exeAppearSlotDown();
    void exeAppear();
    void exeAppearWait();
    void exeAppearDown();
    void exeAppearStatic();
    void exeAppearEnd();
    void exeAppearWaitCameraInterpole();
    void exeWait();
    void exeGot();
    void exeHold();
    void exeFall();
    void exeDelay();
    void exeHide();
    void exeReaction();
    void exeCoin();

    void updateModelActorResetPosition();
    ActorDimensionKeeper* getActorDimensionKeeper() const override;

    bool isMainShine() const { return mIsMainShine; }

public:
    al::MtxConnector* mMtxConnector;
    int _118;
    bool mIsGotShine;
    ShineInfo* curShineInfo; // 0x120
    void* _128;
    void* _130;
    void* _138;
    void* _140;
    void* _148;
    void* _150;
    void* _158;
    void* _16c;
    void* _168;
    void* _170;
    void* _178;
    void* _180;
    al::RateParamV3f* mRateParam;
    void* _190;
    al::ParabolicPathMovement* _198;
    ShineType mModelType;
    void* _1A8;
    bool _1B0;
    ChangeStageInfo* mChangeStageInfo;
    int _1C0;
    int _1C4;
    sead::FixedSafeString<0x80> mShineLabel;
    void* _260;
    int _268;
    bool _26C;
    void* _270;
    QuestInfo* shineQuestInfo; // 0x278
    void* _280; // 0x280
    ActorDimensionKeeper* mDimensionKeeper; // 0x288
    int mShineIdx; // 0x290
    bool mIsMainShine;
    void* _298;
    void* _2A0;
    void* _2A8;
    void* _2B0;
    void* _2B8;
    int _2C0;
    __attribute__((packed)) void* _2C4;
    int _2CC;
    int _2D0;
    bool mIsAddHeight;
    int _2D8;
    al::LiveActor* mModelEmpty;
    al::LiveActor* mModelShine;
    float _2F0;
    u16 _2F4;
    int _2F8;
    bool mIsNoRotate;
    void* _300;
    bool mIsUseDemoCam;
    struct WaterSurfaceShadow* mWaterShadow;
    FukankunZoomCapMessage* _318;
    int _320;
    int _324;
    bool _328;
    void* _330;
    bool mIsCheckGroundHeightMoon;
    bool mIsHintPhoto;
    void* _340;
    bool _348;
    void* _350;
    bool mIsUseAppearDemoForce;
    int _35C;
    int _360;
    int _364;
    int _368;
    bool mIsPowerStar;
    bool mIsAppearDemoHeightHigh;
    void* _370;
    u16 _378;
    int _37C;
};

static_assert(sizeof(Shine) == 0x380);

namespace ShineFunction {
    const char* getMovePointLinkName();
}
