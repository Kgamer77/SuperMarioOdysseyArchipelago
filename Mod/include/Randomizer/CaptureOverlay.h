#pragma once

#include <basis/seadTypes.h>

namespace al {
    class LayoutInitInfo;
}
namespace ShineDB {
    struct ShineEntry;
}

namespace CaptureOverlay {
    void initLayout(const al::LayoutInitInfo& initInfo);
    s32 queueStartingEquipment(s32 startDelay);
    void queueForMoon(const ShineDB::ShineEntry& moon, u32 duplicateSlotMask = 0);
    void queueUnlock(const char* displayName);
    void clear();
    void draw();
}