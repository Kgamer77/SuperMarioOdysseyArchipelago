#pragma once

#include "al/Library/Scene/ISceneObj.h"

class CapMessageMoonNotifier : al::ISceneObj, al::LiveActor {
public:
    CapMessageMoonNotifier();
    bool tryShowCapMessageMoonNotify();

    void* qword110;
    u8 unk118 = 0;
    int unkInt120 = 0;
    int unlockShineNum = 0; // 0x128
};