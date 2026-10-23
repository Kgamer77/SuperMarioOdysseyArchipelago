#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FoolPunishment.h"

namespace al {
    class LayoutInitInfo;
}

namespace AbilityOverlay {

    // Called once each time a StageScene obtains its layout init data. This
    // creates the native AbilityNotif layout from romfs/LayoutData.
    void initLayout(const al::LayoutInitInfo& initInfo);
    // Call when the StageScene is destroyed - the layout belongs to that scene's
    // heap. See PaintingUnlockOverlay::invalidateLayout.
    void invalidateLayout();

    void requestStartingEquipment();
    // Returns the total frame count the ability+capture display chain will run for
    // (0 if neither queued anything), or -1 if there was nothing pending this call.
    s32 tryQueueStartingEquipment();
    void queueLastUnlocks();
    // Shows the most recent AbilityRando::unlockForMoon results: newly unlocked abilities
    // plain, and any duplicate slots (already owned -- a Multi moon can have both in one
    // collection) with an "(again)" suffix.
    void queueForMoon();
    // Immediately shows a single "Ability Unlocked" banner (Purple Shop purchases).
    void queueUnlock(const char* displayName);
    void queueFool(Randomizer::FoolPunishment punishment);
    void clear();
    void draw();

    void beginQueue(s32 startDelay);
    // Keeps the current queue when several startup grants arrive in sequence;
    // starts a new queue only when there is nothing to append to.
    void beginQueueAppend(s32 startDelay);
    void appendEntry(const char* displayName, bool again);
    void startBatchPublic();
    void finishBatchPublic();
    // Adds a plain-language entry for a safety-net item that has no more
    // specific capture, ability, key, or painting notification.
    void queueAutoGrant(const char* displayName);
    bool hasQueuedEntries();

    // Everything one event hands out, numbered as a single run. Nestable, and
    // while one is open the inner startBatchPublic/finishBatchPublic pairs fold
    // into it instead of numbering themselves - so a moon granting two captures
    // and a Subarea Key reads "(1 / 3)" through "(3 / 3)" rather than three
    // popups that each look like the only one.
    void beginRewardGroup();
    void endRewardGroup();

} // namespace AbilityOverlay
