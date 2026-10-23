#pragma once

#include "sead/container/seadPtrArray.h"

#include "Multiplayer/actors/PuppetActor.h"

class PuppetHolder {
public:
    PuppetHolder(int size);

    void update();

    bool tryRegisterPuppet(PuppetActor* puppet);

    bool checkInfoIsInStage(PuppetInfo* info);

    int getSize() { return mPuppetArr.size(); }

    PuppetActor* getPuppetActor(int idx) { return mPuppetArr[idx]; };

    void setStageInfo(const char* stageName, u8 scenarioNo);

    void clearPuppets() { mPuppetArr.clear(); }

    bool resizeHolder(int size);

private:
    sead::PtrArray<PuppetActor> mPuppetArr = sead::PtrArray<PuppetActor>();

    sead::FixedSafeString<0x40> mStageName;

    u8 mScenarioNo = 0;
};