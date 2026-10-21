#pragma once

#include "al/Library/Collision/PartsMtxConnector.h"
#include "al/Library/Obj/CollisionObj.h"

#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/DokanInfo.h"
#include "game/Util/ActorDimensionKeeper.h"
#include "game/Util/IUseDimension.h"

#include "Library/LiveActor/LiveActor.h"
#include "types.h"

class DokanStageChange : public al::LiveActor, public IUseDimension {
public:
    DokanStageChange(const char*);
    void init(const al::ActorInitInfo&) override;
    void initAfterPlacement() override;
    void appear();
    void kill();
    void control() override;
    void endClipped() override;
    void attackSensor(al::HitSensor*, al::HitSensor*) override;
    bool receiveMsg(const al::SensorMsg*, al::HitSensor*, al::HitSensor*) override;
    void exeWait();
    void exeReadyBind();
    void exeIn();
    void exeAppear();
    void exeOut();
    void exeDisappear();
    ActorDimensionKeeper* getActorDimensionKeeper() const override;

    void* _110; // 0x110 // DokanPuppetController size 0xa8
    DokanInfo* _118; // 0x118 // DokanInfo size 0x18
    ChangeStageInfo* mChangeStageInfo; // 0x120 // ChangeStageInfo
    al::MtxConnector* _128; // 0x128 // MtxConnector
    ActorDimensionKeeper* _130; // 0x130 // ActorDimensionKeeper
    void* _138; // 0x138 // YoishiFruit size 0x160
    al::CollisionObj* _140; // 0x140 // CollisionObj
    sead::Vector3<float>* _148; // 0x148 // Used In calcShadowMaskSize may be int
    void* _150; // 0x150
    bool mIsShowTutorial; // 0x154
    bool _155; // 0x155
    bool _156; // 0x156
    bool _157; // 0x157
    bool _158; // 0x158
};