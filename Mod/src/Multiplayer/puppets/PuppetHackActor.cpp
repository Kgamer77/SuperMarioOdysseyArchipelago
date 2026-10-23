#include "Multiplayer/actors/PuppetHackActor.h"

#include "sead/heap/seadHeapMgr.h"
#include "sead/math/seadMatrix.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/algorithms/CaptureSyncTable.h"
#include "Multiplayer/puppets/LateModelDrawer.h"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "al/Library/Base/StringUtil.h"
#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorInitFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorInitUtil.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseKeeper.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActorFlag.h"
#include "al/Library/LiveActor/LiveActorFunction.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Resource/ActorResource.h"
#include "al/Library/Resource/ResourceFunction.h"
#include "hk/ro/RoUtil.h"

PuppetHackActor::PuppetHackActor(const char* name)
    : al::LiveActor(name) { }

void PuppetHackActor::initOnline(PuppetInfo* info, const char* hackType, const char* archiveName, const char* suffix) {
    mInfo = info;
    mHackType = hackType;
    mArchiveName = archiveName;
    mSuffix = suffix;
}

void PuppetHackActor::init(const al::ActorInitInfo& initInfo) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    const bool isEarly = PuppetCapturePool::isEarlyBuildPhase();

    LateModelDrawer::ScopedLateBuild lateScope(!isEarly);
    if (!isEarly)
        LateModelDrawer::prepareForLateRegistration(initInfo, mArchiveName.cstr());

    const LateModelDrawer::DepthShadowCounts shadowCounts
        = isEarly ? LateModelDrawer::DepthShadowCounts {}
                  : LateModelDrawer::snapshotDepthShadow(initInfo);

    al::initActorWithArchiveName(this, initInfo, mArchiveName.cstr(), mSuffix);

    finishInit(initInfo, isEarly, shadowCounts);
}

void PuppetHackActor::initYoshi(const al::ActorInitInfo& initInfo) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    const bool isEarly = PuppetCapturePool::isEarlyBuildPhase();

    LateModelDrawer::ScopedLateBuild lateScope(!isEarly);
    if (!isEarly)
        LateModelDrawer::prepareForLateRegistration(initInfo, mArchiveName.cstr());

    const LateModelDrawer::DepthShadowCounts shadowCounts
        = isEarly ? LateModelDrawer::DepthShadowCounts {}
                  : LateModelDrawer::snapshotDepthShadow(initInfo);

    static constexpr ptrdiff_t kInitYoshiModelActorOffset = 0x00445af8;
    using Fn = void (*)(al::LiveActor*, const al::ActorInitInfo&, const char*);
    reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + kInitYoshiModelActorOffset)(this, initInfo, mArchiveName.cstr());

    finishInit(initInfo, isEarly, shadowCounts);
}

void PuppetHackActor::finishInit(const al::ActorInitInfo& initInfo, bool isEarly, const LateModelDrawer::DepthShadowCounts& shadowCounts) {

    // A malformed or unsupported archive can leave a LiveActor without a
    // drawable model even though the generic initializer returned. Treat it
    // as a pool miss so the owning puppet keeps its normal body.
    al::ModelKeeper* modelKeeper = getModelKeeper();
    if (!modelKeeper || !modelKeeper->getModelCtrl()) {
        MpDebugLog::write("[PuppetHackActor %p] archive %s did not produce a model",
            this, mArchiveName.cstr());
        if (!isEarly)
            LateModelDrawer::restoreDepthShadow(initInfo, shadowCounts, mArchiveName.cstr());
        makeActorDead();
        return;
    }

    // The archive initializer may create a list that did not exist in the
    // first pre-registration pass. Grow shared lists once more before the
    // pool can wake this actor through PlayerModelHolder. Late path only -
    // an early build has no shared list to outgrow yet.
    if (!isEarly)
        LateModelDrawer::prepareForLateRegistration(initInfo, mArchiveName.cstr());

    // A capture model is a puppet, not an enemy: whatever sensors its own
    // InitActor byml set up would let it damage, be trampled by, or be
    // captured by the local player. Drop them all, exactly as PuppetActor does
    // before installing its own harmless three.
    if (mHitSensorKeeper) {
        mHitSensorKeeper->clear();
    }

    // And no collision, for the same reason and then some. The archive's own
    // InitCollision gives a capture model a collider and registers it as a
    // collision surface, which is wrong twice over:
    //
    //  - It makes a remote player's capture solid. Walking into where someone
    //    else's Frog is standing should not stop you; the capture is a
    //    presentation of where that player is, not an obstacle.
    //  - It costs real time. Every registered capture participates in the
    //    scene's collision queries every frame despite being driven entirely
    //    from a network position, which is the shape of the lag reported with
    //    several captures active.
    //
    // Safe to null outright because mNerveKeeper is cleared below, so the
    // archive's own state machine - the only thing that would read mCollider -
    // never runs. The position is dictated by the remote player and nothing
    // else; there is nothing here for collision to decide.
    mCollider = nullptr;
    mCollisionParts = nullptr;

    // ...and the two flags that make al::LiveActor::movement() read the collider
    // we just took away. Straight from the disassembly (LiveActor::movement
    // +0x54..+0x5c):
    //
    //     if (mFlags->isMaterialCodeValid) {
    //         if (al::isCollidedGround(this))              <- dereferences mCollider
    //             al::setMaterialCode(this, al::getCollidedFloorMaterialCodeName(this));
    //         if (mFlags->isPuddleMaterialValid)
    //             al::updateMaterialCodePuddle(this);
    //     }
    //
    // An archive that sets isMaterialCodeValid therefore faults the frame its
    // model is first made alive - which is exactly how Jaxi crashed
    // (al::isCollidedGround, LR al::LiveActor::movement+0x5c, on the main
    // thread under ExecutorActorMovementCalcAnim). Most capture archives leave
    // it off, so this went unnoticed until SphinxRide.
    //
    // Nothing is lost: the material code is what picks footstep sounds and
    // puddle ripples from the ground underneath, and a model driven entirely
    // from a network position is not standing on anything.
    if (mFlags) {
        mFlags->isMaterialCodeValid = false;
        mFlags->isPuddleMaterialValid = false;
    }

    // NO clipping calls here, deliberately. The intent was "puppets are drawn
    // wherever the remote player is, which is further out than the archive's
    // own clipping radius expects", but al::initActorClipping is never called
    // on this actor, so it has no entry in the scene's ClippingActorHolder -
    // and al::validateClipping on an unregistered actor goes straight to
    // al::ClippingActorInfoList::remove() for something that was never in the
    // list. That corrupts the list for everyone: crash report 01787927224 is a
    // REAL placed Frog, in its own FrogStateWaitJump::exeWait, faulting inside
    // that same remove() - solo, with nobody connected and this actor still
    // inert and never leased. We only had to build the model to do the damage.
    //
    // Registering properly via initActorClipping is not the answer either: that
    // is one more list the scene sizes during init-after-placement, which is
    // the exact shape of every other crash in this feature.
    //
    // Nothing is lost by staying out. An actor with no clipping keeper is never
    // clipped, i.e. always drawn, which is what was wanted anyway. PuppetCapActor
    // - the sibling that has never crashed once all session - does no clipping
    // setup either.

    // ...except that the paragraph above is only true of a LATE build. It
    // holds because initActorClippingLateHook (syncHooks.cpp) suppresses
    // al::initActorClipping whenever LateModelDrawer::ScopedLateBuild is
    // active, so a late capture never enters the clipping registry at all.
    //
    // An early build does not set that scope - it must not, because it is
    // taking part in the scene's own initialisation - so the archive
    // initializer's al::initActorClipping runs for real and the capture DOES
    // get registered with the ClippingDirector. Measured: the puppet's body
    // model and PuppetCapActor both log invalidClip=1, while an early-built
    // Hack_Frog logged invalidClip=0 - the one puppet-side actor with live
    // clipping.
    //
    // That is fatal here, and quietly so. al::LiveActor::movement() begins
    //     if (!mFlags->isDead && (!mFlags->isClipped || mFlags->isDrawClipped))
    // so the moment the director clips this actor, movement() returns without
    // doing anything - no ModelKeeper::update, no ActorActionKeeper update.
    // Drawing carries on from a separate flag, so the result is a capture that
    // renders perfectly, in the right place, frozen, with the HackOn animation
    // that reveals the cap and moustache started but never committed.
    //
    // al::invalidateClipping puts it back on the same footing as every other
    // puppet-side actor: it clears isClipped (through the actor's own
    // endClipped virtual) and removes it from the ClippingActorHolder for
    // good. Only valid because the early path actually registered it - calling
    // this on a LATE build would ask the holder to remove an actor that was
    // never added, which is the list corruption the paragraph above is about.
    if (isEarly)
        al::invalidateClipping(this);

    // No silhouette hide here, deliberately - it was the crash that appeared
    // the moment the corrected capture table started building archives it had
    // never built before. Trace: buildOne -> PuppetHackActor::init ->
    // finishInit -> al::hideSilhouetteModelIfShow -> al::isEqualString with a
    // null first argument, on the scene initialisation thread.
    //
    // al::hideSilhouetteModelIfShow is only safe on an actor that is KNOWN to
    // own a silhouette sub-actor. Its body is:
    //
    //     keeper = actor->mSubActorKeeper;
    //     if (keeper) {
    //         found = alSubActorFunction::findSubActor(keeper, <name>);
    //         if (!*(u8*)(found[32] + 5)) al::hideSilhouetteModel(actor);
    //     }
    //
    // Two ways that kills us on a foreign archive. findSubActor RETURNS NULL
    // on a miss and the result is dereferenced unconditionally, so any archive
    // with a sub-actor keeper but no silhouette faults on address 0x100. And
    // the search itself calls getName() on every sub-actor and compares it,
    // which is where we actually died - at least one archive we now build has
    // a sub-actor whose name is null. Every vanilla caller creates its own
    // silhouette first, so neither path is reachable in the game.
    //
    // PuppetActor and PuppetCapActor still call it, and correctly: both pass a
    // player model, which always has one. A capture archive is whatever the
    // stage happened to place.
    //
    // Nothing is lost. A silhouette is the see-through-wall outline; an archive
    // without one is unaffected and one with it simply keeps drawing it. If a
    // capture ever needs its outline suppressed, walk al::getSubActorNum /
    // al::getSubActor BY INDEX and skip null names - never the name-matching
    // helpers, which all share this search.

    // al::initActorWithArchiveName builds the model, the pose and the
    // executors from the archive, but it does NOT create an ActorActionKeeper
    // - that is an actor-specific step the real Frog does for itself in
    // Frog::init(). Without one this actor has no action table, so
    // al::isExistAction is false for every name, al::startAction and
    // al::tryStartActionIfNotPlaying have nothing to drive, and
    // al::startVisAnim has no visibility animation to select. A capture then
    // renders correctly and stays frozen in its bind pose with the cap and
    // moustache still hidden, because "HackOn" never starts and neither does
    // any action PuppetActor sends it.
    //
    // The archive name is the action-table key (initMarioModelPuppet does the
    // same for body models), while the resource lookups take the ObjectData/
    // path. Guarded on the keeper actually being absent so this stays correct
    // if a future initializer starts providing one.
    if (!getActorActionKeeper()) {
        al::StringTmp<0x100> objectPath("ObjectData/%s", mArchiveName.cstr());

        al::ActorResource* actionResource = al::findOrCreateActorResourceWithAnimResource(
            initInfo.actorResourceHolder, objectPath.cstr(), objectPath.cstr(), nullptr, false);

        // A non-null ActorResource is not proof the archive exists: the type
        // key and the archive name are allowed to differ, and the lookup
        // creates a hollow entry rather than failing. The model check above
        // already passed, so this is belt and braces - but a null mModelRes
        // here would fault inside the keeper's own resource read.
        if (actionResource && actionResource->getModelRes()) {
            al::initActorActionKeeper(this, actionResource, mArchiveName.cstr(), nullptr);
            MpDebugLog::write("[PuppetHackActor %p] action keeper for %s -> %p",
                this, mArchiveName.cstr(), getActorActionKeeper());
        } else {
            MpDebugLog::write(
                "[PuppetHackActor %p] no action resource for %s - capture will not animate",
                this, objectPath.cstr());
        }
    }

    const CaptureSync::Entry* entry = CaptureSync::findByHackName(mHackType.cstr());
    if (entry && entry->adapter && entry->adapter->setupModel) {
        entry->adapter->setupModel(this, initInfo);
    }

    // Frog::init() leaves a newly-created capture in the same state: the
    // regular model is visible, while the capture transition is held at
    // HackOff.  The generic archive initializer creates the action/visibility
    // keepers but does not run the capture actor's game-specific init, so do
    // this small part of that native setup explicitly.  Without it a pooled
    // actor can be selected with an uninitialized/hidden capture presentation
    // and remain invisible until the remote action happens to replace it.
    startHackAnim(false);

    // A LATE capture model is built during gameplay
    // (PuppetCapturePool::update), so the drawers initExecutorDraw just
    // created above never went through the scene's createList() pass and have
    // no table to store into. Left alone, such an actor builds fine and then
    // crashes the frame a puppet first leases it. See LateModelDrawer.h.
    //
    // An early build must NOT do this: createExecutorListTable() has not run
    // yet and will create those same tables itself, correctly sized to
    // include this model. Creating them here first is how an early build
    // would end up with a table the engine then replaces - i.e. a registered
    // model that draws nowhere.
    if (!isEarly) {
        LateModelDrawer::createMissingTables(initInfo, this, mArchiveName.cstr());
        LateModelDrawer::restoreDepthShadow(initInfo, shadowCounts, mArchiveName.cstr());
    }

    // Drop the archive's own nerve keeper. A capture model is a puppet, not a
    // live enemy: with it in place the Frog's own state machine runs every
    // frame (al::LiveActor::movement -> al::NerveKeeper::update ->
    // FrogStateWaitJump::exeWait, exactly the chain in crash report
    // 01787927224) and drives the actor wherever its AI decides, overwriting
    // the remote player's position that PuppetActor pushes in. That is why the
    // capture rendered at 0,0,0 instead of on the player.
    //
    // Same reasoning as the hit sensors being cleared just above - anything
    // that lets this stand-in act like a real enemy is unwanted. Animation does
    // not depend on it: the leasing puppet starts the remote capture's action
    // on this actor every frame.
    mNerveKeeper = nullptr;

    mHasModel = true;

    // Born idle: the pool leases it first, then PuppetActor registers/selects
    // it and makes it alive only after PlayerModelHolder::registerModel()
    // returns.
    makeActorDead();
}

void PuppetHackActor::startAction(sead::SafeString actName) {
    if (actName.isEmpty())
        return;

    if (al::tryStartActionIfNotPlaying(this, actName.cstr())) {
        const char* curActName = al::getActionName(this);
        if (curActName) {
            if (al::isSklAnimExist(this, curActName)) {
                al::clearSklAnimInterpole(this);
            }
        }
    }
}

void PuppetHackActor::startActionOnSubActors(const char* actName) {
    if (!actName || actName[0] == '\0' || !getSubActorKeeper())
        return;

    // The bare name the parent's "Hack<X>" corresponds to, when it has one.
    // al::StringTmp is avoided deliberately: this runs every frame for every
    // captured puppet, and the only transformation needed is dropping a
    // four-character prefix.
    const char* plainName = nullptr;
    if (actName[0] == 'H' && actName[1] == 'a' && actName[2] == 'c' && actName[3] == 'k'
        && actName[4] != '\0')
        plainName = actName + 4;

    const s32 subNum = al::getSubActorNum(this);
    for (s32 i = 0; i < subNum; i++) {
        al::LiveActor* sub = al::getSubActor(this, i);
        if (!sub)
            continue;

        // No action keeper means al::isExistAction would be asking a null
        // table; it answers false, and startAction on such an actor does
        // nothing anyway.
        const char* wanted = nullptr;
        if (al::isExistAction(sub, actName))
            wanted = actName;
        else if (plainName && al::isExistAction(sub, plainName))
            wanted = plainName;

        if (!wanted)
            continue;

        if (al::tryStartActionIfNotPlaying(sub, wanted)) {
            const char* started = al::getActionName(sub);
            if (started && al::isSklAnimExist(sub, started))
                al::clearSklAnimInterpole(sub);
        }
    }
}

void PuppetHackActor::startHackAnim(bool isOn) {
    // Native capture init/state code drives both the action and visibility
    // animators.  The generic archive initializer gives us the keepers, but it
    // cannot run FrogStateHack (and there is no one common capture subclass),
    // so mirror the two exported native calls here.  The existence checks keep
    // captures with a reduced animation set valid.
    // Capture archives do not agree on one pair of names. Counted across the
    // whole decompiled game:
    //
    //     HackOff        151    HackOn          49    <- Frog, and most
    //     HackOffCapOff   32    HackOnCapOn     25    <- KuriboWing family
    //     HackOffCapOn    22    HackOnCapOff    18    <- the mixed states
    //
    // An archive carries one convention or the other, never both, and asking
    // for a name it does not have silently does nothing at all. That is
    // exactly what the live log showed: Frog reported action=1 visAnim=1 and
    // animated, while KuriboWing reported action=0 visAnim=0 and could never
    // have shown a cap no matter what else was fixed - its animations are
    // called HackOnCapOn/HackOffCapOff.
    //
    // A puppet riding a capture is hacked AND wearing Mario's cap, so in the
    // four-state convention that is HackOnCapOn; a pooled model waiting to be
    // leased is neither, i.e. HackOffCapOff. Try the specific name first and
    // fall back to the plain pair.
    const char* candidates[2];
    if (isOn) {
        candidates[0] = "HackOnCapOn";
        candidates[1] = "HackOn";
    } else {
        candidates[0] = "HackOffCapOff";
        candidates[1] = "HackOff";
    }

    const char* animName = nullptr;
    bool hasAction = false;
    bool hasVisAnim = false;
    for (const char* candidate : candidates) {
        const bool action = al::isExistAction(this, candidate);
        const bool visAnim = al::isVisAnimExist(this, candidate);
        if (!action && !visAnim)
            continue;

        animName = candidate;
        hasAction = action;
        hasVisAnim = visAnim;
        break;
    }

    if (hasAction)
        al::startAction(this, animName);

    if (hasVisAnim)
        al::startVisAnim(this, animName);

    // One line per lease, not per frame. This is the measurement that
    // separates the two ways a capture can end up frozen and hatless:
    //
    //   keeper=0, or action=0 visAnim=0  -> the archive gave this actor no
    //       action table, so nothing can ever be started on it and the cap
    //       and moustache have no animation to reveal them.
    //   keeper set, action/visAnim 1, but `now` still not animName on the
    //       next lease -> the animation IS being started and simply never
    //       committed, i.e. nothing is calling this actor's movement().
    MpDebugLog::write(
        "[PuppetHackActor %p] startHackAnim(%s) archive=%s picked=%s keeper=%p action=%d visAnim=%d now=%s subActors=%d",
        this, isOn ? "on" : "off", mArchiveName.cstr(), animName ? animName : "(none match)",
        getActorActionKeeper(), hasAction ? 1 : 0, hasVisAnim ? 1 : 0,
        al::getActionName(this) ? al::getActionName(this) : "(none)",
        getSubActorKeeper() ? al::getSubActorNum(this) : -1);
}

void PuppetHackActor::makeActorAlive() {
    // PlayerModelChangerHakoniwa revives a selected model and then explicitly
    // shows it. Keep the same invariant for pooled capture actors, since their
    // model was initialized dead while it waited in the pool.
    al::LiveActor::makeActorAlive();
    // showModelIfHide() is a no-op when LiveActorFlag::isModelHidden is already
    // clear.  Archive visibility state and executor membership can still be
    // asleep in that case, so use the native unconditional show operation;
    // its request table deduplicates repeated draw requests.
    al::showModel(this);
}

void PuppetHackActor::update() {
    // Position and rotation are NOT set here any more. The leasing puppet
    // registers this actor in its own PlayerModelHolder and selects it with
    // changeModel(), so PuppetActor::syncPose() poses it exactly the way it
    // poses a body model - the one path in this feature that has always
    // rendered in the right place. Hand-posing it here is what five rounds of
    // 0,0,0 were spent on.
    //
    // Kept: the capture action, which the send side names out of PlayerAnims -
    // the only table wide enough for a Cheep Cheep's or a Frog's action names
    // as well as Mario's. A miss decodes to the empty string and startAction
    // ignores it, leaving whatever the model was already playing.
    if (mInfo)
        startAction(PlayerAnims::FindStr(mInfo->curCaptureAnim));
}
