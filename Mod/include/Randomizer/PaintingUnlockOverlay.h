#pragma once

#include <basis/seadTypes.h>

namespace al {
    class LayoutInitInfo;
}

namespace PaintingUnlockOverlay {

    // Called once each time a StageScene obtains its layout init data. This
    // creates the native PaintingNotif layout from romfs/LayoutData.
    void initLayout(const al::LayoutInitInfo& initInfo);
    // Call when the StageScene is destroyed. The layout lives in that scene's
    // heap, so it dies with it; this drops the pointer (and the pending queue)
    // rather than leaving a dangling one for the next scene to delete.
    void invalidateLayout();
    void queue(const char* sourceKingdom, const char* destinationKingdom, bool alreadyOwned = false);
    // startDelay chains this after another overlay's display (e.g. the starting
    // abilities/captures sequence) instead of using the default fixed delay;
    // pass 0 to use the default.
    void queueFree(const char* sourceKingdom, const char* destinationKingdom, s32 startDelay = 0);
    void clear();
    void draw();

} // namespace PaintingUnlockOverlay
