#include "Randomizer/CaptureOverlay.h"

#include <basis/seadTypes.h>

#include "nn/util.h"

#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/Randomizer.h"
#include "ShineDB.h"

// CaptureOverlay no longer owns its own layout actor/archive. It renders
// through AbilityOverlay's AbilityNotif actor and TxtAbility pane, so all
// queueing here forwards into AbilityOverlay's shared entry queue. This
// keeps capture-unlock and ability-unlock notifications on one popup
// instance instead of two independent (and potentially overlapping) ones.
namespace CaptureOverlay {

    s32 queueStartingEquipment(s32 startDelay) {
        if (!Randomizer::isCaptureRandoEnabled())
            return startDelay;

        const s32 captureCount = CaptureRando::getStartingCaptureCount();
        if (captureCount <= 0)
            return startDelay;

        AbilityOverlay::startBatchPublic();
        for (s32 i = 0; i < captureCount; ++i) {
            const CaptureRando::CaptureEntry* capture = CaptureRando::getStartingCapture(i);
            if (!capture)
                continue;
            AbilityOverlay::appendEntry(capture->displayName, false);
        }
        AbilityOverlay::finishBatchPublic();

        return startDelay;
    }

    void queueForMoon(const ShineDB::ShineEntry& moon, u32 duplicateSlotMask) {
        const s32 captureCount = CaptureRando::getCaptureCountForMoon(moon);
        if (captureCount <= 0)
            return;

        AbilityOverlay::startBatchPublic();
        for (s32 i = 0; i < captureCount; ++i) {
            const CaptureRando::CaptureEntry* capture = CaptureRando::getCaptureForMoon(moon, i);
            if (!capture)
                continue;
            const bool again = (duplicateSlotMask & (1u << i)) != 0;
            AbilityOverlay::appendEntry(capture->displayName, again);
        }
        AbilityOverlay::finishBatchPublic();
    }

    void queueUnlock(const char* displayName) {
        AbilityOverlay::beginQueue(0);
        AbilityOverlay::startBatchPublic();
        AbilityOverlay::appendEntry(displayName, false);
        AbilityOverlay::finishBatchPublic();
    }

    void clear() {
        AbilityOverlay::clear();
    }

    void draw() {
        // No-op: AbilityOverlay::draw() drives the shared actor now.
    }

} // namespace CaptureOverlay