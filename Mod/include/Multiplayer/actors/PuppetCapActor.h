#pragma once

#include "sead/prim/seadSafeString.h"

#include "al/Library/LiveActor/LiveActor.h"

#include "game/Player/HackCap/HackCapJointControlKeeper.h"

#include "Multiplayer/puppets/PuppetInfo.h"

class PuppetCapActor : public al::LiveActor {
public:
    PuppetCapActor(const char* name);
    virtual void init(const al::ActorInitInfo&) override;
    virtual void makeActorAlive(void) override;
    // virtual void initAfterPlacement() override;
    virtual void control(void) override;
    // virtual void movement(void) override;

    virtual void attackSensor(al::HitSensor*, al::HitSensor*) override;
    virtual bool receiveMsg(const al::SensorMsg*, al::HitSensor*, al::HitSensor*) override;

    void initOnline(PuppetInfo* info);

    // Builds a cap for an explicit costume snapshot.  Late costume packets
    // cannot safely reinitialize the existing actor, so PuppetActor creates a
    // bounded replacement and calls this under the late-registration guards.
    void initCostume(const al::ActorInitInfo& initInfo, const char* costumeCapName,
        bool lateBuild);
    bool hasModel() const { return mHasModel; }

    // Seeds a newly-built dead replacement before PuppetActor publishes and
    // wakes it, avoiding a one-frame draw at the archive's default pose.
    void applySnapshot(const PuppetInfo& info);

    void startAction(const char* actName);
    void update();

    // Public access for direct manipulation
    HackCapJointControlKeeper* mJointKeeper = nullptr;

private:
    void updateActivationCheck();

    PuppetInfo* mInfo = nullptr;
    sead::FixedSafeString<0x40> mCapModelName;
    bool mHasModel = false;
    bool mActivationCheckPending = false;
    s32 mActivationCheckDelay = 0;
    s32 mActivationCheckAttempts = 0;
    float mClosingSpeed = 0;
};
