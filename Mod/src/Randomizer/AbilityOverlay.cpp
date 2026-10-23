#include "Randomizer/AbilityOverlay.h"

#include <basis/seadTypes.h>
#include <math/seadVector.h>

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "nn/util.h"

#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/UnlockRewards.h"

#include <math.h>
#include <string.h>

namespace AbilityOverlay {

    namespace {

        struct OverlayEntry {
            char text[128];
            s32 index; // position within its own batch (ability batch or capture batch)
            s32 count; // size of its own batch, NOT the whole combined queue
            bool again;
            bool fool;
        };

        static constexpr s32 kMaxEntries = 12;
        static constexpr s32 kDefaultStartDelay = 70;
        static constexpr s32 kHoldFrames = 90;

        // Ported from AbilityLocker.cpp's popup animation: the notif pane sits
        // above its resting spot and lerps down into place, then lerps back up
        // and out when it's done, same as ability2StartPos -> ability2EndPos.
        static constexpr f32 kHiddenOffsetY = -120.0f;
        static constexpr f32 kSlideInSpeed = 0.05f;
        static constexpr f32 kSlideOutSpeed = 0.1f;
        static constexpr f32 kSlideNearThreshold = 1.0f;

        static OverlayEntry sEntries[kMaxEntries];
        static s32 sEntryCount = 0;
        static s32 sCurrentEntry = 0;
        static s32 sDelayTimer = 0;
        static s32 sTimer = 0;
        static s32 sStartDelay = kDefaultStartDelay;

        // Tracks where the current in-progress batch (ability or capture)
        // started within sEntries, so its size can be back-filled into every
        // entry in that batch once the batch is finished.
        static s32 sBatchStart = 0;

        // One moon can hand out several different kinds of reward at once - a
        // pair of captures, an ability, a Subarea Key, a Hint Token. A reward
        // group spans all of them so the player sees one run of "(1 / 4)" ...
        // "(4 / 4)" instead of four popups each silently claiming to be the
        // whole story. While a group is open the individual producers' own
        // start/finishBatch calls fold into it; with no group open they still
        // number themselves, which is what every non-moon caller relies on.
        static s32 sGroupStart = 0;
        static s32 sGroupDepth = 0;

        class AbilityNotif;
        static AbilityNotif* sLayout = nullptr;

        class AbilityNotif : public al::LayoutActor {
        public:
            explicit AbilityNotif(const al::LayoutInitInfo& initInfo);

            void showEntry(const OverlayEntry& entry);
            void updateTextPane(const OverlayEntry& entry);
            void exeAppear();
            void exeWait();
            void exeEnd();

        private:
            sead::Vector2f mBaseTrans;
            sead::Vector2f mHiddenTrans;
        };

        NERVE_IMPL(AbilityNotif, Appear)
        NERVE_IMPL(AbilityNotif, Wait)
        NERVE_IMPL(AbilityNotif, End)
        NERVES_MAKE_STRUCT(AbilityNotif, Appear, Wait, End)

        AbilityNotif::AbilityNotif(const al::LayoutInitInfo& initInfo)
            : al::LayoutActor("AbilityNotif") {
            al::initLayoutActor(this, initInfo, "AbilityNotif", nullptr);
            mBaseTrans = al::getLocalTrans(this);
            mHiddenTrans = sead::Vector2f(mBaseTrans.x, mBaseTrans.y + kHiddenOffsetY);
            initNerve(&NrvAbilityNotif.Wait, 0);
            kill();
        }

        void AbilityNotif::updateTextPane(const OverlayEntry& entry) {
            if (!al::isExistPane(this, "TxtAbility"))
                return;

            const char* suffix = entry.fool ? "" : (entry.again ? Randomizer::UnlockRewards::kAgainSuffix : "");

            if (entry.count > 1) {
                al::setPaneStringFormat(this, "TxtAbility", "%s%s (%d / %d)",
                    entry.text, suffix, entry.index + 1, entry.count);
            } else {
                al::setPaneStringFormat(this, "TxtAbility", "%s%s", entry.text, suffix);
            }
        }

        void AbilityNotif::showEntry(const OverlayEntry& entry) {
            updateTextPane(entry);
            al::startAction(this, "Appear", nullptr);
            al::setNerve(this, &NrvAbilityNotif.Appear);
            if (!isAlive())
                al::LayoutActor::appear();
        }

        void AbilityNotif::exeAppear() {
            if (al::isFirstStep(this))
                al::setLocalTrans(this, mHiddenTrans);

            const sead::Vector2f curPos = al::getLocalTrans(this);
            sead::Vector2f nextPos;
            al::lerpVec(&nextPos, curPos, mBaseTrans, kSlideInSpeed);
            al::setLocalTrans(this, nextPos);

            const bool isDone = al::isNear(curPos, mBaseTrans, kSlideNearThreshold);
            if (isDone && al::isActionEnd(this, nullptr))
                al::setNerve(this, &NrvAbilityNotif.Wait);
        }

        void AbilityNotif::exeWait() {
            if (al::isFirstStep(this))
                al::startAction(this, "Wait", nullptr);

            if (al::isGreaterEqualStep(this, kHoldFrames))
                al::setNerve(this, &NrvAbilityNotif.End);
        }

        void AbilityNotif::exeEnd() {
            if (al::isFirstStep(this))
                al::startAction(this, "End", nullptr);

            const sead::Vector2f curPos = al::getLocalTrans(this);
            sead::Vector2f nextPos;
            al::lerpVec(&nextPos, curPos, mHiddenTrans, kSlideOutSpeed);
            al::setLocalTrans(this, nextPos);

            const bool isDone = al::isNear(curPos, mHiddenTrans, kSlideNearThreshold);
            if (!isDone || !al::isActionEnd(this, nullptr))
                return;

            sCurrentEntry++;
            if (sCurrentEntry < sEntryCount) {
                showEntry(sEntries[sCurrentEntry]);
                return;
            }

            sEntryCount = 0;
            sCurrentEntry = 0;
            sDelayTimer = 0;
            sBatchStart = 0;
            sGroupStart = 0;
            sStartDelay = kDefaultStartDelay;
            kill();
        }

        // Note: deliberately does not close an open reward group. Callers inside
        // one (applyUnlockFeedback opens the queue for its moon) reset the
        // entries, not the grouping; sGroupStart follows sEntryCount back to 0
        // so the group simply spans the fresh queue.
        static void resetQueue(s32 startDelay = kDefaultStartDelay) {
            sEntryCount = 0;
            sCurrentEntry = 0;
            sDelayTimer = 0;
            sBatchStart = 0;
            sGroupStart = 0;
            sStartDelay = startDelay;
        }

        static void numberRange(s32 first, s32 last) {
            const s32 count = last - first;
            for (s32 i = first; i < last; i++) {
                sEntries[i].index = i - first;
                sEntries[i].count = count;
            }
        }

        // Call before appending a new group of entries (all abilities from one
        // event, or all captures from one event) that should be numbered
        // against each other (1/3, 2/3, 3/3) rather than against the whole
        // combined queue.
        static void startBatch() {
            if (sGroupDepth > 0)
                return; // the enclosing reward group owns the numbering
            sBatchStart = sEntryCount;
        }

        // Call after appending a batch's entries to back-fill index/count
        // across just that batch.
        static void finishBatch() {
            if (sGroupDepth > 0)
                return; // numbered when the enclosing reward group closes
            numberRange(sBatchStart, sEntryCount);
        }

        static OverlayEntry* allocEntry() {
            if (sEntryCount >= kMaxEntries)
                return nullptr;

            OverlayEntry& entry = sEntries[sEntryCount];
            sEntryCount++;
            // sEntries is a reused static buffer, so a fresh slot still holds
            // the previous occupant's metadata. Every field is set here rather
            // than left to the caller: a producer that forgets one used to
            // inherit a stale batch size and announce a lone reward as "1 / 3".
            entry.text[0] = '\0';
            entry.index = 0;
            entry.count = 1;
            entry.again = false;
            entry.fool = false;
            if (!sLayout || !sLayout->isAlive())
                sDelayTimer = 0;
            return &entry;
        }

    } // namespace

    void initLayout(const al::LayoutInitInfo& initInfo) {
        // See PaintingUnlockOverlay::initLayout - same ownership rule. The actor
        // comes out of the StageScene's heap, so this delete is only ever valid
        // for a repeat init within one live scene; across scenes the pointer is
        // already dangling and invalidateLayout() will have cleared it.
        if (sLayout)
            delete sLayout;
        sLayout = new AbilityNotif(initInfo);
    }

    void invalidateLayout() {
        sLayout = nullptr;
        resetQueue();
    }

    static bool sStartingEquipmentPending = false;

    void requestStartingEquipment() {
        sStartingEquipmentPending = true;
    }

    static void queueStartingEquipmentNow() {
        // Auto-grants may already have queued capture/ability/key entries before
        // the normal startup request is consumed. Preserve those entries and add
        // configured starting equipment as another numbered batch.
        if (sEntryCount <= 0)
            resetQueue();

        if (!Randomizer::isLockedAbilitiesEnabled()) {
            return;
        }

        startBatch();

        if (const AbilityRando::AbilityEntry* capThrow = AbilityRando::getStartingCapThrow()) {
            OverlayEntry* entry = allocEntry();
            if (entry) {
                nn::util::SNPrintf(entry->text, sizeof(entry->text),
                    "%s", capThrow->displayName);
                entry->again = false;
                entry->fool = false;
            }
        }

        const s32 startingAbilityCount = AbilityRando::getStartingAbilityCount();
        for (s32 i = 0; i < startingAbilityCount; i++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getStartingAbility(i);
            if (!ability)
                continue;

            OverlayEntry* entry = allocEntry();
            if (!entry)
                break;

            nn::util::SNPrintf(entry->text, sizeof(entry->text),
                "%s", ability->displayName);
            entry->again = false;
            entry->fool = false;
        }

        finishBatch();
    }

    s32 tryQueueStartingEquipment() {
        if (!sStartingEquipmentPending)
            return -1;

        sStartingEquipmentPending = false;
        queueStartingEquipmentNow();
        CaptureOverlay::queueStartingEquipment(0);

        constexpr s32 kApproxSlideInFrames = 94;
        constexpr s32 kApproxSlideOutFrames = 46;
        constexpr s32 kEntryFrames = kApproxSlideInFrames + kHoldFrames + kApproxSlideOutFrames;
        return sEntryCount > 0 ? sStartDelay + sEntryCount * kEntryFrames : 0;
    }

    void queueLastUnlocks() {
        const s32 abilityCount = AbilityRando::getLastUnlockedAbilityCount();
        if (abilityCount <= 0)
            return;

        resetQueue();
        startBatch();
        for (s32 i = 0; i < abilityCount; i++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getLastUnlockedAbility(i);
            if (!ability)
                continue;

            OverlayEntry* entry = allocEntry();
            if (!entry)
                break;

            nn::util::SNPrintf(entry->text, sizeof(entry->text),
                "%s", ability->displayName);
            entry->again = false;
            entry->fool = false;
        }

        finishBatch();
    }

    void queueForMoon() {
        const s32 unlockedCount = AbilityRando::getLastUnlockedAbilityCount();
        const s32 duplicateCount = AbilityRando::getLastDuplicateAbilityCount();
        if (unlockedCount + duplicateCount <= 0)
            return;

        // Note: no resetQueue() here -- the caller (applyUnlockFeedback) is
        // expected to call beginQueue() once up front, before calling both
        // CaptureOverlay::queueForMoon() and this function, so capture and
        // ability entries for the same moon event append as separate,
        // independently-numbered batches into one combined popup sequence.
        startBatch();

        for (s32 i = 0; i < unlockedCount; i++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getLastUnlockedAbility(i);
            if (!ability)
                continue;

            OverlayEntry* entry = allocEntry();
            if (!entry)
                break;

            nn::util::SNPrintf(entry->text, sizeof(entry->text),
                "%s", ability->displayName);
            entry->again = false;
            entry->fool = false;
        }

        for (s32 i = 0; i < duplicateCount; i++) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getLastDuplicateAbility(i);
            if (!ability)
                continue;

            OverlayEntry* entry = allocEntry();
            if (!entry)
                break;

            nn::util::SNPrintf(entry->text, sizeof(entry->text),
                "%s", ability->displayName);
            entry->again = true;
            entry->fool = false;
        }

        finishBatch();
    }

    void queueUnlock(const char* displayName) {
        // Purchases want immediate feedback while the shop UI is still open,
        // so this bypasses the default fixed delay entirely.
        resetQueue(0);
        startBatch();

        OverlayEntry* entry = allocEntry();
        if (entry) {
            nn::util::SNPrintf(entry->text, sizeof(entry->text),
                "%s", displayName ? displayName : "???");
            entry->again = false;
            entry->fool = false;
        }

        finishBatch();
    }

    void beginQueue(s32 startDelay) {
        // Public entry point for callers (including CaptureOverlay) that want
        // to start a fresh combined queue before appending batches.
        resetQueue(startDelay);
    }

    void beginQueueAppend(s32 startDelay) {
        if (sEntryCount <= 0) {
            resetQueue(startDelay);
            return;
        }
        if (sStartDelay > startDelay)
            sStartDelay = startDelay;
    }

    void appendEntry(const char* displayName, bool again) {
        // Used by CaptureOverlay to add its own batch of capture-unlock
        // entries into this overlay's shared queue/actor, since both now
        // render through the same AbilityNotif archive and TxtAbility pane.
        // Callers must bracket their group of appendEntry() calls with
        // startBatchPublic()/finishBatchPublic() so the batch is numbered
        // against itself (1/3, 2/3, 3/3) rather than the whole combined queue.
        OverlayEntry* entry = allocEntry();
        if (!entry)
            return;

        nn::util::SNPrintf(entry->text, sizeof(entry->text),
            "%s", displayName ? displayName : "???");
        entry->again = again;
        entry->fool = false;
    }

    void startBatchPublic() {
        startBatch();
    }

    void finishBatchPublic() {
        finishBatch();
    }

    void queueAutoGrant(const char* displayName) {
        beginQueueAppend(kDefaultStartDelay);
        startBatch();
        appendEntry(displayName ? displayName : "Granted at start", false);
        finishBatch();
    }

    bool hasQueuedEntries() {
        return sEntryCount > 0;
    }

    void beginRewardGroup() {
        if (sGroupDepth++ > 0)
            return;
        sGroupStart = sEntryCount;
    }

    void endRewardGroup() {
        if (sGroupDepth == 0 || --sGroupDepth > 0)
            return;
        numberRange(sGroupStart, sEntryCount);
    }

    void queueFool(Randomizer::FoolPunishment punishment) {
        startBatch();
        OverlayEntry* entry = allocEntry();
        if (entry) {
            nn::util::SNPrintf(entry->text, sizeof(entry->text), "%s",
                Randomizer::getFoolPunishmentText(punishment));
            entry->again = false;
            entry->fool = true;
        }
        finishBatch();
    }

    void clear() {
        resetQueue();
        if (sLayout && sLayout->isAlive())
            sLayout->kill();
    }

    void draw() {
        if (!sLayout || sEntryCount <= 0 || sLayout->isAlive())
            return;

        if (sDelayTimer < sStartDelay) {
            sDelayTimer++;
            return;
        }

        sCurrentEntry = 0;
        sLayout->showEntry(sEntries[sCurrentEntry]);
    }

} // namespace AbilityOverlay
