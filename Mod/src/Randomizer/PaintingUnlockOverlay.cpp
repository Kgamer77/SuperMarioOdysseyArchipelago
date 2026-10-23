#include "Randomizer/PaintingUnlockOverlay.h"

#include <basis/seadTypes.h>
#include <math/seadVector.h>

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "nn/util.h"

#include "Randomizer/WorldMeta.h"

#include <math.h>
#include <string.h>

namespace PaintingUnlockOverlay {

    namespace {

        struct OverlayEntry {
            char text[160];
            char sourceKingdom[32];
            char destKingdom[32];
        };

        static constexpr s32 kMaxEntries = 8;
        static constexpr s32 kDefaultStartDelay = 70;
        static constexpr s32 kHoldFrames = 300;
        static constexpr s32 kSlideInFrames = 18;
        static constexpr s32 kSlideOutFrames = 15;
        static constexpr f32 kHiddenOffsetY = -200.0f;
        static constexpr f32 kWobbleAmplitudeDeg = 6.0f;
        static constexpr f32 kWobbleFramesPerCycle = 160.0f;

        static constexpr const char* kKingdomPaneNames[] = {
            "Bowser", "Sand", "Lake", "Wooded", "Mushroom",
            "Seaside", "Metro", "Snow", "Cascade", "Luncheon"
        };
        static constexpr s32 kKingdomPaneCount = sizeof(kKingdomPaneNames) / sizeof(kKingdomPaneNames[0]);

        static OverlayEntry sEntries[kMaxEntries];
        static s32 sEntryCount = 0;
        static s32 sCurrentEntry = 0;
        static s32 sDelayTimer = 0;
        static s32 sStartDelay = kDefaultStartDelay;
        class PaintingNotif;
        static PaintingNotif* sLayout = nullptr;

        static f32 easeOutQuart(f32 t) {
            f32 inv = 1.0f - t;
            return 1.0f - inv * inv * inv * inv;
        }

        static f32 easeInQuart(f32 t) {
            return t * t * t * t;
        }

        class PaintingNotif : public al::LayoutActor {
        public:
            explicit PaintingNotif(const al::LayoutInitInfo& initInfo);

            void showEntry(const OverlayEntry& entry);
            void updatePaintingIcons(const OverlayEntry& entry);
            void updateWobble();
            void clearWobble();
            void exeAppear();
            void exeWait();
            void exeEnd();

        private:
            s32 mSlideStep = 0;
            s32 mWobbleFrame = 0;
            sead::Vector2f mBaseTrans;
            char mSourcePaneName[40] = {};
            char mDestPaneName[40] = {};
        };

        NERVE_IMPL(PaintingNotif, Appear)
        NERVE_IMPL(PaintingNotif, Wait)
        NERVE_IMPL(PaintingNotif, End)
        NERVES_MAKE_STRUCT(PaintingNotif, Appear, Wait, End)

        PaintingNotif::PaintingNotif(const al::LayoutInitInfo& initInfo)
            : al::LayoutActor("PaintingNotif") {
            al::initLayoutActor(this, initInfo, "PaintingNotif", nullptr);
            mBaseTrans = al::getLocalTrans(this);
            initNerve(&NrvPaintingNotif.Wait, 0);
            kill();
        }

        void PaintingNotif::updatePaintingIcons(const OverlayEntry& entry) {
            char paneName[40];
            mSourcePaneName[0] = '\0';
            mDestPaneName[0] = '\0';

            for (s32 i = 0; i < kKingdomPaneCount; i++) {
                const char* kingdom = kKingdomPaneNames[i];
                bool isSource = kingdom[0] != '\0' && strncmp(kingdom, entry.sourceKingdom, sizeof(entry.sourceKingdom)) == 0;
                bool isDest = kingdom[0] != '\0' && strncmp(kingdom, entry.destKingdom, sizeof(entry.destKingdom)) == 0;

                nn::util::SNPrintf(paneName, sizeof(paneName), "%sPaintingLeft", kingdom);
                if (al::isExistPane(this, paneName)) {
                    if (isSource) {
                        al::showPane(this, paneName);
                        strlcpy(mSourcePaneName, paneName, sizeof(mSourcePaneName));
                    } else {
                        al::hidePane(this, paneName);
                    }
                }

                nn::util::SNPrintf(paneName, sizeof(paneName), "%sPaintingRight", kingdom);
                if (al::isExistPane(this, paneName)) {
                    if (isDest) {
                        al::showPane(this, paneName);
                        strlcpy(mDestPaneName, paneName, sizeof(mDestPaneName));
                    } else {
                        al::hidePane(this, paneName);
                    }
                }
            }

            mWobbleFrame = 0;
        }

        void PaintingNotif::updateWobble() {
            mWobbleFrame++;
            f32 phase = (mWobbleFrame / kWobbleFramesPerCycle) * 2.0f * 3.14159265f;
            f32 sourceAngle = sinf(phase) * kWobbleAmplitudeDeg;
            f32 destAngle = sinf(phase + 3.14159265f) * kWobbleAmplitudeDeg;

            if (mSourcePaneName[0] != '\0')
                al::setPaneLocalRotate(this, mSourcePaneName, sead::Vector3f(0.0f, 0.0f, sourceAngle));
            if (mDestPaneName[0] != '\0')
                al::setPaneLocalRotate(this, mDestPaneName, sead::Vector3f(0.0f, 0.0f, destAngle));
        }

        void PaintingNotif::clearWobble() {
            if (mSourcePaneName[0] != '\0')
                al::setPaneLocalRotate(this, mSourcePaneName, sead::Vector3f(0.0f, 0.0f, 0.0f));
            if (mDestPaneName[0] != '\0')
                al::setPaneLocalRotate(this, mDestPaneName, sead::Vector3f(0.0f, 0.0f, 0.0f));
        }

        void PaintingNotif::showEntry(const OverlayEntry& entry) {
            al::setPaneStringFormat(this, "PaintingText", "%s", entry.text);
            updatePaintingIcons(entry);
            al::startAction(this, "Appear", nullptr);
            al::setNerve(this, &NrvPaintingNotif.Appear);
            if (!isAlive())
                al::LayoutActor::appear();
        }

        void PaintingNotif::exeAppear() {
            if (al::isFirstStep(this)) {
                mSlideStep = 0;
                al::setLocalTrans(this, sead::Vector2f(mBaseTrans.x, mBaseTrans.y + kHiddenOffsetY));
            }

            mSlideStep++;
            f32 linearT = static_cast<f32>(mSlideStep) / static_cast<f32>(kSlideInFrames);
            if (linearT > 1.0f)
                linearT = 1.0f;
            f32 easedT = easeOutQuart(linearT);
            al::setLocalTrans(this, sead::Vector2f(mBaseTrans.x, mBaseTrans.y + kHiddenOffsetY * (1.0f - easedT)));

            updateWobble();

            if (linearT >= 1.0f && al::isActionEnd(this, nullptr))
                al::setNerve(this, &NrvPaintingNotif.Wait);
        }

        void PaintingNotif::exeWait() {
            if (al::isFirstStep(this))
                al::startAction(this, "Wait", nullptr);

            updateWobble();

            if (al::isGreaterEqualStep(this, kHoldFrames))
                al::setNerve(this, &NrvPaintingNotif.End);
        }

        void PaintingNotif::exeEnd() {
            if (al::isFirstStep(this)) {
                mSlideStep = 0;
                al::startAction(this, "End", nullptr);
            }

            mSlideStep++;
            f32 linearT = static_cast<f32>(mSlideStep) / static_cast<f32>(kSlideOutFrames);
            if (linearT > 1.0f)
                linearT = 1.0f;
            f32 easedT = easeInQuart(linearT);
            al::setLocalTrans(this, sead::Vector2f(mBaseTrans.x, mBaseTrans.y + kHiddenOffsetY * easedT));

            updateWobble();

            if (linearT < 1.0f || !al::isActionEnd(this, nullptr))
                return;

            clearWobble();

            sCurrentEntry++;
            if (sCurrentEntry < sEntryCount) {
                showEntry(sEntries[sCurrentEntry]);
                return;
            }

            sEntryCount = 0;
            sCurrentEntry = 0;
            sDelayTimer = 0;
            sStartDelay = kDefaultStartDelay;
            kill();
        }

        static OverlayEntry* allocEntry() {
            if (sEntryCount >= kMaxEntries)
                return nullptr;

            OverlayEntry& entry = sEntries[sEntryCount];
            sEntryCount++;
            if (!sLayout || !sLayout->isAlive())
                sDelayTimer = 0;
            return &entry;
        }

        static void fillEntry(OverlayEntry* entry, const char* sourceKingdom,
            const char* destinationKingdom, const char* fmt) {
            nn::util::SNPrintf(entry->text, sizeof(entry->text), fmt,
                sourceKingdom ? sourceKingdom : "???",
                destinationKingdom ? destinationKingdom : "???");

            const char* sourcePane = WorldMeta::getPaintingPaneNameForDisplayName(sourceKingdom);
            const char* destPane = WorldMeta::getPaintingPaneNameForDisplayName(destinationKingdom);

            strlcpy(entry->sourceKingdom, sourcePane ? sourcePane : "",
                sizeof(entry->sourceKingdom));
            strlcpy(entry->destKingdom, destPane ? destPane : "",
                sizeof(entry->destKingdom));
        }

    } // namespace

    void initLayout(const al::LayoutInitInfo& initInfo) {
        // Only ever deletes a layout belonging to the *live* scene.  A
        // PaintingNotif is allocated out of the StageScene's heap, and that heap
        // is reclaimed wholesale when the scene is destroyed - so once a scene
        // has gone, sLayout is dangling and deleting it hands the allocator back
        // memory that already belongs to whatever was built next.  The reroll
        // does exactly that, several times a run.  invalidateLayout() drops the
        // pointer at teardown, which leaves this delete for the only case it was
        // ever correct for: a second init inside one scene.
        if (sLayout)
            delete sLayout;
        sLayout = new PaintingNotif(initInfo);
    }

    void invalidateLayout() {
        // The scene heap owns the actor; there is nothing to free here, only a
        // pointer to stop believing in.
        sLayout = nullptr;
        sEntryCount = 0;
        sCurrentEntry = 0;
        sDelayTimer = 0;
        sStartDelay = kDefaultStartDelay;
    }

    void queue(const char* sourceKingdom, const char* destinationKingdom, bool alreadyOwned) {
        sStartDelay = kDefaultStartDelay;
        OverlayEntry* entry = allocEntry();
        if (!entry)
            return;

        fillEntry(entry, sourceKingdom, destinationKingdom,
            alreadyOwned ? "A painting link has opened:\n%s - %s (again)"
                         : "A painting link has opened:\n%s - %s");
    }

    void queueFree(const char* sourceKingdom, const char* destinationKingdom, s32 startDelay) {
        sStartDelay = startDelay > 0 ? startDelay : kDefaultStartDelay;
        OverlayEntry* entry = allocEntry();
        if (!entry)
            return;

        fillEntry(entry, sourceKingdom, destinationKingdom,
            "A painting link starts open:\n%s - %s");
    }

    void clear() {
        sEntryCount = 0;
        sCurrentEntry = 0;
        sDelayTimer = 0;
        sStartDelay = kDefaultStartDelay;
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

} // namespace PaintingUnlockOverlay
