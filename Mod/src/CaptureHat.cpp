#include "CaptureHat.h"

#include "sead/prim/seadSafeString.h"

#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/LiveActor/LiveActorFunction.h"
#include "al/Library/Model/ModelKeeper.h"

#include "game/Player/HackCap.h"
#include "game/Player/PlayerFunction.h"

#include "hk/hook/Trampoline.h"

#include "Randomizer/Randomizer.h"

namespace {

    // A stand-in for HackCap's equipment hat, built from the cap the player is
    // actually wearing.
    //
    // Swapping the archive the game's own equipment hat is built from is not an
    // option: HackCap::init builds it with the "Display" init-file suffix and
    // then looks up two sub-actors ("Eye" and "DisplayDepthShadow") on it, and
    // only MarioCap.szs carries the Display-suffixed files that declare them.
    // Every other cap archive (MarioSailorCap, MarioKingCap, ...) holds nothing
    // but InitModel/InitSubActor/InitAudio/InitEffect and its .bfres, so the
    // same construction on one of them would come up without those sub-actors.
    //
    // PlayerFunction::initCapModelActor is what those archives are built
    // through instead: it names the shared MarioCapAnimation archive itself
    // rather than reading an AnimArc key that the costume archives do not
    // carry. It is the path the game uses for HackCap's own cap and the one
    // PuppetCapActor uses for a remote player's, so it works for every costume.
    class CaptureHatActor : public al::LiveActor {
    public:
        explicit CaptureHatActor(const char* name)
            : al::LiveActor(name) { }

        void initCostume(const al::ActorInitInfo& initInfo, const char* capModelName) {
            // The demo variant, which is also what PuppetCapActor builds a
            // remote player's cap through: it registers the draw and model
            // update executors but skips the movement executor and every hit
            // sensor, which is all a hat sitting on an enemy's head needs.
            PlayerFunction::initCapModelActorDemo(this, initInfo, capModelName);

            al::ModelKeeper* modelKeeper = getModelKeeper();
            mHasModel = modelKeeper && modelKeeper->getModelCtrl();

            if (mHasModel) {
                // Never occlusion-culled, the same as the hat this stands in
                // for. It reaches into the model, so it cannot run for a cap
                // that failed to build one.
                al::invalidateOcclusionQuery(this);
            }

            // Strip what the cap builder hangs off any cap: Cappy's eyes, a
            // parts model that syncs its appear and hide with its parent, and
            // the silhouette model that outlines a cap through walls. Neither
            // belongs on a hat sitting on an enemy's head - vanilla's worn hat
            // has its own eyes actor and sends it to "Disappear" the moment a
            // hack starts. Clearing the sync flags is what makes the kill
            // stick: makeActorAlive revives every sub-actor that still syncs
            // appear, so a kill on its own would last until the first capture.
            if (getSubActorKeeper()) {
                al::offSyncAppearSubActorAll(this);
                al::offSyncHideSubActorAll(this);
                for (s32 i = 0; i < al::getSubActorNum(this); i++) {
                    al::LiveActor* subActor = al::getSubActor(this, i);
                    if (subActor)
                        subActor->makeActorDead();
                }
            }

            makeActorDead();
        }

        bool hasModel() const { return mHasModel; }

    private:
        bool mHasModel = false;
    };

    // One replacement per HackCap, and a scene has exactly one. A single slot
    // cannot go stale across scenes: HackCap::init runs for the new cap before
    // anything can pose it and rewrites both fields, and a pointer that does
    // not match the cap being posed simply falls back to vanilla.
    CaptureHatActor* sHat = nullptr;
    const HackCap* sHatOwner = nullptr;

    // Set while the vanilla equipment hat is out of the draw list because of us
    // rather than because the game hid it - see restoreEquipmentHat.
    bool sHidEquipmentHat = false;

    void createReplacementHat(HackCap* hackCap, const al::ActorInitInfo& initInfo) {
        sHat = nullptr;
        sHatOwner = nullptr;
        sHidEquipmentHat = false;

        if (!hackCap)
            return;

        const char* capType = hackCap->mTypeName;
        if (!capType || !capType[0])
            return;

        sead::FixedSafeString<0x40> capModelName;
        PlayerFunction::createCapModelName(&capModelName, capType);

        // Nothing to replace when the equipment hat already is this costume's
        // cap. createCapModelName folds the invisible cap onto "MarioCap" too,
        // which is the archive HackCap::init hardcodes, so that costume keeps
        // vanilla behavior and costs nothing.
        if (capModelName.isEqual("MarioCap"))
            return;

        // Built here rather than on the first capture: this runs inside
        // PlayerActorHakoniwa::init, where the archive is already resident
        // (HackCap just built its own cap model from it) and the draw and
        // executor tables still accept new entries.
        CaptureHatActor* hat = new CaptureHatActor("CaptureHat");
        hat->initCostume(initInfo, capModelName.cstr());

        // The archive is the one HackCap itself was just built from, so this
        // should not happen. If it ever does, the actor stays dead and the
        // vanilla hat is left on screen.
        if (!hat->hasModel())
            return;

        sHat = hat;
        sHatOwner = hackCap;

        // Once per stage load, and only for a costume that actually needs the
        // replacement. The cap type name is read straight out of HackCap at a
        // fixed offset, so this is the line that says the offset still resolves
        // to a costume name rather than to whatever moved into 0x138.
        Randomizer::logPush("capture hat: %s -> %s", capType, capModelName.cstr());
    }

    // Hand the equipment hat's own visibility back before the game looks at it.
    //
    // Whether that hat is on screen is the only signal that says a hat is being
    // worn at all, and hiding it is how the wrong hat is kept from drawing - so
    // the flag has to be the game's again by the time updateCapPose reads it.
    // Several of that function's branches leave the hat's visibility untouched
    // for a whole state, and a hide of ours left standing across one of those
    // would read back as "no hat worn" and take the replacement down with it.
    void restoreEquipmentHat(HackCap* hackCap) {
        if (!sHidEquipmentHat || sHatOwner != hackCap)
            return;

        sHidEquipmentHat = false;

        // A hat the game has killed in the meantime keeps the hidden flag:
        // showing a dead actor's model would put it back in the draw list at a
        // stale pose. The game's own show path clears the flag when it revives
        // the hat.
        al::LiveActor* equipmentHat = hackCap->mEquipmentHat;
        if (equipmentHat && al::isAlive(equipmentHat))
            al::showModelIfHide(equipmentHat);
    }

    void updateReplacementHat(HackCap* hackCap) {
        if (!sHat || sHatOwner != hackCap)
            return;

        al::LiveActor* equipmentHat = hackCap->mEquipmentHat;

        // Vanilla state, untouched by us (restoreEquipmentHat ran before the
        // original): the game shows the equipment hat exactly while a hat is
        // being worn on a capture.
        const bool isWorn
            = equipmentHat && al::isAlive(equipmentHat) && !al::isHideModel(equipmentHat);

        if (!isWorn) {
            if (al::isAlive(sHat))
                sHat->makeActorDead();
            return;
        }

        // Take the vanilla hat out of the draw list only. The actor stays alive
        // and keeps animating, which is what the two parts models hanging off
        // it need: its depth-shadow companion (not flagged IsSyncHide, so it
        // survives this and still casts the hat's shadow) and the lock-on eyes.
        al::hideModelIfShow(equipmentHat);
        sHidEquipmentHat = true;

        // The lock-on eyes ARE flagged IsSyncHide, so the hide above takes them
        // down with the hat. They are what the player sees blinking on the hat
        // while it sits on a lock-on target, and they are fixed to a joint of
        // the skeleton every cap costume shares - all of them animate out of
        // MarioCapAnimation - so they land in the right place on the
        // replacement. Put them back.
        al::LiveActor* lockOnEyes = hackCap->mLockOnEyes;
        if (lockOnEyes && al::isAlive(lockOnEyes))
            al::showModelIfHide(lockOnEyes);

        // HackCap is the pose source for the worn hat - updateCapPose copies
        // its transform onto the equipment hat and hands it HackCap's scale,
        // which is where the shrink that fits the hat to a Frog or a Goomba
        // comes from. It is also exactly what Client::sendHackCapInfPacket
        // transmits for a worn cap, so the hat drawn here is the one every
        // remote player is already drawing for this capture.
        al::setTrans(sHat, al::getTrans(hackCap));
        al::setQuat(sHat, al::getQuat(hackCap));
        al::setScale(sHat, al::getScale(hackCap));
        al::setModelAlphaMask(sHat, al::getModelAlphaMask(equipmentHat));

        if (al::isDead(sHat)) {
            sHat->makeActorAlive();

            // The cap builder gives every cap an AO shadow blob, and a worn hat
            // must not drop one on the ground under the enemy - which is why
            // the game invalidates its own hat's shadow on the frame it shows
            // it. makeActorAlive is what puts the shadow back, so this follows
            // every wake rather than being done once at init.
            if (sHat->getShadowKeeper())
                al::invalidateShadow(sHat);
        }

        // This pose is written after the hat's own movement executor has run
        // for the frame, so build its matrix here rather than leave the hat a
        // frame behind the enemy it is sitting on.
        al::resetPosition(sHat);
        sHat->calcAnim();
    }

    static HkTrampoline<void, HackCap*, const al::ActorInitInfo&> hackCapInitHook
        = hk::hook::trampoline([](HackCap* hackCap, const al::ActorInitInfo& initInfo) -> void {
              hackCapInitHook.orig(hackCap, initInfo);
              createReplacementHat(hackCap, initInfo);
          });

    // The last thing to touch the equipment hat each frame: it is called from
    // PlayerActorHakoniwa::executeAfterCapTarget, an executor phase of its own,
    // so both the hat's pose and its visibility for this frame are final by the
    // time the original returns.
    static HkTrampoline<void, HackCap*> updateCapPoseHook
        = hk::hook::trampoline([](HackCap* hackCap) -> void {
              restoreEquipmentHat(hackCap);
              updateCapPoseHook.orig(hackCap);
              updateReplacementHat(hackCap);
          });

} // namespace

void CaptureHat::installHooks() {
    hackCapInitHook.installAtSym<"_ZN7HackCap4initERKN2al13ActorInitInfoE">();
    updateCapPoseHook.installAtSym<"_ZN7HackCap13updateCapPoseEv">();
}
