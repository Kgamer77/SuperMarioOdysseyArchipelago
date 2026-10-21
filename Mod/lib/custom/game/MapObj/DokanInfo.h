#pragma once
#include "al/Library/LiveActor/LiveActor.h"

enum DokanType
{
    Dokan = 0,
    Dokan2D = 1,
    DokanCrack = 2
};

// Size 0x18
class DokanInfo {
    DokanInfo(al::LiveActor*, al::ActorInitInfo*);

    int _8; // 0x8 // maybe enum
    DokanType mDokanType; // 0xc
    al::LiveActor* _08; // 0x08
    int _10; // 0x10 // Maybe enum
    bool mIsExitOnly; // 0x14
    bool _15; // 0x15
    bool mIsMoon; // 0x16
    bool mIsInvalidChangeStage; // 0x17
};