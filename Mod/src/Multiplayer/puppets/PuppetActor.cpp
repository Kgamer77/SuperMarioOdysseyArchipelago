#include "Multiplayer/actors/PuppetActor.h"
#include "Multiplayer/algorithms/AlUnlinked.h"
#include "Multiplayer/algorithms/CaptureSyncTable.h"
#include "Multiplayer/algorithms/RideTransform.h"

#include "Multiplayer/puppets/LateModelDrawer.h"

#include "hk/diag/diag.h"

#include "sead/heap/seadHeapMgr.h"
#include "sead/math/seadQuat.h"

#include "al/Library/Action/ActorActionKeeper.h"
#include "al/Library/Base/StringUtil.h"
#include "al/Library/Draw/GraphicsSystemInfo.h"
#include "al/Library/Effect/EffectSystemInfo.h"
#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/Light/ModelMaterialCategory.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitFunction.h"
#include "al/Library/LiveActor/ActorInitUtil.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseKeeper.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/ActorResourceFunction.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/LiveActor/LiveActorFlag.h"
#include "al/Library/LiveActor/LiveActorFunction.h"
#include "al/Library/LiveActor/LiveActorKeeper.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Obj/PartsModel.h"
#include "al/Library/Play/Layout/BalloonMessage.h"
#include "al/Library/Resource/ActorResource.h"
#include "al/Library/Resource/ResourceFunction.h"
#include "al/Library/Yaml/ByamlIter.h"
#include "al/Project/Action/ActionPadAndCameraCtrl.h"
#include "al/Project/HitSensor/HitSensor.h"

#include "game/Player/PlayerCostumeFunction.h"
#include "game/Player/PlayerCostumeInfo.h"
#include "game/Player/PlayerFunction.h"
#include "game/Util/PlayerUtil.h"
#include "game/Util/SensorMsgFunction.h"

#include <cstddef>
#include <cstdint>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/packets/HackCapInf.h"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/server/DeltaTime.hpp"
#include "Scene/StageSceneStateModConfig.hpp"
#include "helpers.hpp"

#include "hk/ro/RoUtil.h"

// A leased capture whose skeletal animation frame has not moved for this many
// consecutive frames is frozen, whatever the executor tables say about it.
// Short enough that nobody sees the freeze, long enough that a one-frame
// action or a paused frame does not trip it.
static constexpr s32 cCaptureStallFrames = 8;

// While hand-driving a capture, skip our own movement() call once every this
// many frames and watch whether the animation advances anyway. That is the only
// way to notice that the engine has started driving the same actor, which would
// otherwise silently double every capture animation's speed.
static constexpr s32 cCaptureProbeInterval = 120;

// How long a capture may stay frozen while this puppet is already calling its
// movement() before that is worth one line in the log.
static constexpr s32 cCaptureDriveDiagFrames = 30;

static const char* subActorNames[] = {
    "顔", // Face
    "目", // Eye
    "頭", // Head
    "左手", // Left Hand
    "右手" // Right Hand
};

PuppetActor::PuppetActor(const char* name)
    : al::LiveActor(name) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    mPuppetCap = new PuppetCapActor(name);
    mPuppetCaps[0] = mPuppetCap;
    // Costume pairs plus a slot per capture type this puppet rides - see
    // kModelSlots. The capture actors themselves stay owned by
    // PuppetCapturePool; the holder only borrows a pointer so they can be
    // displayed like a costume.
    mModelHolder = new PlayerModelHolder(kModelSlots);
}

PuppetActor::~PuppetActor() {
    releaseCapture();
    releaseRide();
    for (s32 i = 0; i < kCostumeGenerations; i++) {
        if (mCostumeInfos[i]) {
            delete mCostumeInfos[i];
            mCostumeInfos[i] = nullptr;
        }
    }
    mCostumeInfo = nullptr;
    mPupInfo = nullptr;
    for (s32 i = 0; i < kCostumeGenerations; i++) {
        if (mPuppetCaps[i]) {
            delete mPuppetCaps[i];
            mPuppetCaps[i] = nullptr;
        }
    }
    mPuppetCap = nullptr;
    mPendingPuppetCap = nullptr;
    delete mModelHolder;
    delete mNameTag;
}

void PuppetActor::init(const al::ActorInitInfo& initInfo) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    mPuppetCap->init(initInfo);
    al::initActorWithArchiveName(this, initInfo, "PlayerActorHakoniwa", nullptr);

    const char* bodyName = "Mario";
    const char* capName = "Mario";

    if (mPupInfo) {
        bodyName = tryGetPuppetBodyName(mPupInfo);
        capName = tryGetPuppetCapName(mPupInfo);

        mNameTag = new NameTag(this, al::getLayoutInitInfo(initInfo), 4900.0f, 5000.0f, mPupInfo->puppetName.cstr());
    }

    // Retained so a costume that arrives after this point can still build a
    // replacement pair from control() - see tryRefreshCostume().
    mInitInfo = initInfo;
    mBuiltBodyName = bodyName;
    mBuiltCapName = capName;
    mBuiltThrownCapName = capName;

    mCostumeInfo = buildCostumeModels(
        initInfo, bodyName, capName, mNormalModelName.cstr(), mNormal2DModelName.cstr());
    mCostumeInfos[0] = mCostumeInfo;

    if (!mCostumeInfo) {
        hk::diag::logLine("[PuppetActor] ERROR: failed to build initial costume %s/%s", bodyName,
            capName);
        return;
    }

    mModelHolder->changeModel(mNormalModelName.cstr());

    startAction("Wait");

    // Clear existing sensors loaded from BYML
    if (mHitSensorKeeper) {
        mHitSensorKeeper->clear();
    }

    initHitSensor(3);
    al::addHitSensor(this, initInfo, "Body", static_cast<u32>(al::HitSensorType::Npc), 50.0f, 16,
        sead::Vector3f(0.0f, 75.0f, 0.0f));
    al::addHitSensor(this, initInfo, "Head", static_cast<u32>(al::HitSensorType::Npc), 40.0f, 16,
        sead::Vector3f(0.0f, 110.0f, 0.0f));
    al::addHitSensor(this, initInfo, "Foot", static_cast<u32>(al::HitSensorType::Npc), 40.0f, 1,
        sead::Vector3f(0.0f, 40.0f, 0.0f));
}

PlayerCostumeInfo* PuppetActor::buildCostumeModels(const al::ActorInitInfo& initInfo,
    const char* bodyName, const char* capName, const char* normalName, const char* normal2DName,
    bool lateBuild) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    LateModelDrawer::ScopedLateBuild lateScope(lateBuild);

    if (lateBuild)
        LateModelDrawer::prepareForLateRegistration(initInfo, normalName);

    al::LiveActor* normalModel = new al::LiveActor("Normal");

    // Use the game's complete constructor here. The hand-written version used
    // the Mario part executor for the root body as well, which omits the
    // ordinary body's draw passes: the face/head could be visible while the
    // outfit body was absent. The scope prevents this remote model from being
    // reported as another local costume packet by syncHooks.cpp.
    LateModelDrawer::ScopedPuppetCostumeBuild costumeScope;
    PlayerCostumeInfo* costumeInfo = PlayerFunction::initMarioModelActor(
        normalModel, initInfo, bodyName, capName, nullptr, false);

    if (!costumeInfo || !costumeInfo->mBodyInfo) {
        MpDebugLog::write("[Puppet %p] failed to build costume body=%s cap=%s", this,
            bodyName ? bodyName : "(null)", capName ? capName : "(null)");
        return nullptr;
    }

    if (normalModel->mActionKeeper && normalModel->mActionKeeper->mPadAndCameraCtrl)
        normalModel->mActionKeeper->mPadAndCameraCtrl->mRumbleCount = 0;

    if (mModelHolder->mBuffer.size() >= mModelHolder->mBuffer.capacity()) {
        MpDebugLog::write("[Puppet %p] model holder full while adding %s", this, normalName);
        return nullptr;
    }
    mModelHolder->registerModel(normalModel, normalName);

    al::LiveActor* normal2DModel = new al::LiveActor("Normal2D");

    if (lateBuild)
        LateModelDrawer::prepareForLateRegistration(initInfo, normal2DName);

    PlayerFunction::initMarioModelActor2D(normal2DModel, initInfo,
        al::StringTmp<0x40>("%s2D", costumeInfo->mBodyInfo->costumeName).cstr(),
        PlayerFunction::isInvisibleCap(costumeInfo));

    if (mModelHolder->mBuffer.size() >= mModelHolder->mBuffer.capacity()) {
        MpDebugLog::write("[Puppet %p] model holder full while adding %s", this, normal2DName);
        return nullptr;
    }
    mModelHolder->registerModel(normal2DModel, normal2DName);

    // The native Mario builder can create shader-specific executor lists while
    // it is constructing sub-actors. The first preparation pass has to happen
    // before registration so existing lists have room for the new actor; this
    // second pass also covers a list that did not exist until the builder
    // selected a material category for this particular outfit.
    if (lateBuild)
        LateModelDrawer::prepareForLateRegistration(initInfo, normalName);

    if (!lateBuild) {
        al::setClippingInfo(normalModel, 999999999.0f, 0);
        al::setClippingNearDistance(normalModel, 999999999.0f);

        al::setClippingInfo(normal2DModel, 999999999.0f, 0);
        al::setClippingNearDistance(normal2DModel, 999999999.0f);
    }

    al::hideSilhouetteModelIfShow(normalModel);

    al::LiveActor* headModel = al::getSubActor(normalModel, "頭");
    if (headModel) {
        al::LiveActor* capEyes = al::tryGetSubActor(headModel, "キャップの目");
        if (capEyes)
            capEyes->kill();
        al::startVisAnimForAction(headModel, "CapOn");
    }

    if (!lateBuild) {
        al::validateClipping(normalModel);
        al::validateClipping(normal2DModel);
    }

    // NOTE: the drawer fixup deliberately does NOT live here, even though this
    // is where the models are built. init() calls this too, from inside the
    // stage's own placement window, and there a table-less drawer is simply
    // one the scene has not reached yet - createList() is still to come and
    // will do the job properly. Only tryRefreshCostume()'s rebuild is genuinely
    // late, so the fixup is applied there. See LateModelDrawer.h.
    return costumeInfo;
}

// Outfit updates can arrive after the stage's placement sweep, so this path
// builds a replacement pair during gameplay. LateModelDrawer grows the shared
// executor/draw tables first, while ScopedLateBuild keeps the replacement out
// of the fixed clipping/depth-shadow registries. The pair is still activated
// one frame later, after construction has fully completed.
static constexpr bool cEnableLiveCostumeRebuild = true;

bool PuppetActor::tryRefreshCostume(PuppetInfo* frameInfo) {
    if (!cEnableLiveCostumeRebuild)
        return false;

    if (!frameInfo)
        return false;

    const char* bodyName = tryGetPuppetBodyName(frameInfo);
    const char* capName = tryGetPuppetCapName(frameInfo);
    if (!bodyName || !capName)
        return false;
    if (mBuiltBodyName.isEqual(bodyName) && mBuiltCapName.isEqual(capName))
        return false;

    // Bounded because each rebuild leaves its predecessor registered: the old
    // model actors can still be referenced by the PlayerCostumeInfo that built
    // them, so freeing them here would be the riskier choice. Three
    // generations is far more outfit changes than a session realistically
    // sees, and past that the puppet simply keeps the costume it has.
    if (mCostumeGeneration + 1 >= kCostumeGenerations) {
        // Keep the last successfully built names intact. Marking the
        // requested names as built here would make a transient generation
        // limit permanently discard the outfit: the actor would still be
        // wearing the previous pair, but future control() calls would never
        // retry the request after a rebind or a later successful build.
        if (!mCostumeLimitBodyName.isEqual(bodyName) || !mCostumeLimitCapName.isEqual(capName)) {
            mCostumeLimitBodyName = bodyName;
            mCostumeLimitCapName = capName;
            MpDebugLog::write("[Puppet %p] costume generation limit reached for %s/%s", this,
                bodyName, capName);
        }
        return false;
    }

    const s32 nextGeneration = mCostumeGeneration + 1;
    sead::FixedSafeString<0x20> nextNormalModelName;
    sead::FixedSafeString<0x20> nextNormal2DModelName;
    nextNormalModelName.format("Normal%d", nextGeneration);
    nextNormal2DModelName.format("Normal2D%d", nextGeneration);

    // Bracket the whole build: the depth-shadow registration happens deep
    // inside the native Mario builders, not at a call site we control. See
    // LateModelDrawer.h.
    const LateModelDrawer::DepthShadowCounts shadowCounts
        = LateModelDrawer::snapshotDepthShadow(mInitInfo);

    PlayerCostumeInfo* newCostumeInfo = buildCostumeModels(mInitInfo, bodyName, capName,
        nextNormalModelName.cstr(), nextNormal2DModelName.cstr(), true);

    LateModelDrawer::restoreDepthShadow(mInitInfo, shadowCounts, nextNormalModelName.cstr());

    if (!newCostumeInfo) {
        MpDebugLog::write("[Puppet %p] costume rebuild failed for %s/%s", this, bodyName, capName);
        return true;
    }

    mCostumeGeneration = nextGeneration;
    mNormalModelName = nextNormalModelName;
    mNormal2DModelName = nextNormal2DModelName;
    mCostumeInfo = newCostumeInfo;
    mCostumeInfos[mCostumeGeneration] = newCostumeInfo;

    mBuiltBodyName = bodyName;
    mBuiltCapName = capName;
    mCostumeLimitBodyName.clear();
    mCostumeLimitCapName.clear();

    // Two earlier attempts at a live costume swap crashed the game
    // (mp_debug_*.log pointer matches against real crash reports' fault-
    // site registers, see memory.txt) - both built and made the new model
    // alive within the SAME control() call. Neither actually tested
    // separating construction from activation onto different frames.
    // PuppetCapturePool's reuse path proves that shape IS safe (build once,
    // sleep, wake later on a different frame - never crashed all session),
    // so apply it here too: put the freshly built pair to sleep right away
    // instead of switching to it now, and let control() perform the actual
    // visible swap on the NEXT frame via changeModel() (mPendingCostumeApply
    // below) - never the same frame that built it.
    al::LiveActor* freshNormal = mModelHolder->tryFindModelActor(mNormalModelName.cstr());
    al::LiveActor* freshNormal2D = mModelHolder->tryFindModelActor(mNormal2DModelName.cstr());

    // Unlike the pair init() builds, these are born long after the scene's
    // createList() pass, so their drawers have no tables and their depth/shadow
    // passes have nothing behind them. Must happen before the models can ever
    // be made alive - both crashes this costs us fire on the first frame the
    // model is drawn, so there is no second chance. See LateModelDrawer.h.
    LateModelDrawer::createMissingTables(mInitInfo, freshNormal, mNormalModelName.cstr());
    LateModelDrawer::createMissingTables(mInitInfo, freshNormal2D, mNormal2DModelName.cstr());

    if (freshNormal && !al::isDead(freshNormal))
        freshNormal->makeActorDead();
    if (freshNormal2D && !al::isDead(freshNormal2D))
        freshNormal2D->makeActorDead();

    MpDebugLog::write("[Puppet %p] costume built %s/%s freshNormal=%p freshNormal2D=%p - activating next frame",
        this, mNormalModelName.cstr(), mNormal2DModelName.cstr(), freshNormal, freshNormal2D);

    mPendingCostumeApply = true;
    return true;
}

bool PuppetActor::tryRefreshThrownCap(PuppetInfo* frameInfo) {
    if (!frameInfo || mPendingPuppetCap)
        return false;

    const char* capName = tryGetPuppetCapName(frameInfo);
    if (!capName || !capName[0] || mBuiltThrownCapName.isEqual(capName))
        return false;

    // Every attempt creates an executor-registered actor that must remain
    // retained, even if its archive did not produce a model.  Count attempts
    // against the same small generation budget so a malformed packet cannot
    // allocate one actor every frame.
    if (mThrownCapGeneration + 1 >= kCostumeGenerations) {
        if (!mThrownCapLimitName.isEqual(capName)) {
            mThrownCapLimitName = capName;
            MpDebugLog::write("[Puppet %p] thrown-cap generation limit reached for %s",
                this, capName);
        }
        return false;
    }

    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    const s32 nextGeneration = mThrownCapGeneration + 1;
    PuppetCapActor* freshCap = new PuppetCapActor("PuppetCap");
    freshCap->initCostume(mInitInfo, capName, true);
    freshCap->initOnline(mPupInfo);

    mThrownCapGeneration = nextGeneration;
    mPuppetCaps[nextGeneration] = freshCap;

    if (!freshCap->hasModel()) {
        MpDebugLog::write("[Puppet %p] thrown-cap build failed for %s; retaining current cap",
            this, capName);
        return true;
    }

    mPendingPuppetCap = freshCap;
    mPendingThrownCapName = capName;
    mThrownCapLimitName.clear();
    MpDebugLog::write("[Puppet %p] thrown cap %s built actor=%p - applying next frame",
        this, capName, freshCap);
    return true;
}

void PuppetActor::tryRefreshCapture(PuppetInfo* frameInfo) {
    if (!frameInfo)
        return;

    // Capture changes happen from the live puppet update path, after the
    // scene's normal current-heap scope has ended. PlayerModelHolder::registerModel
    // allocates its entry through the current heap, so establish the same scene
    // heap used by the costume builders before touching the holder or pool.
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    const bool wantsCapture = frameInfo->isCaptured && !frameInfo->curHack.isEmpty();
    const char* wanted = wantsCapture ? frameInfo->curHack.cstr() : "";
    const s32 wantedVariant = wantsCapture ? frameInfo->captureVariant : -1;

    // A successful request is the only state that is memoized. A pool miss or
    // a holder-registration failure is transient (the pool may finish building
    // the real actor later), so it must remain retryable.
    if (!wantsCapture) {
        if (mCaptureActor || mIsCaptureModel || !mRequestedCapture.isEmpty()) {
            MpDebugLog::write("[Puppet %p] releasing capture wanted='%s' memo='%s'",
                this, wanted, mRequestedCapture.cstr());
            releaseCapture();
        }
        return;
    }

    if (mCaptureActor && mIsCaptureModel && mRequestedCapture.isEqual(wanted)
        && mRequestedCaptureVariant == wantedVariant)
        return;

    // Logged once per distinct request, not once per frame.
    //
    // Failures are deliberately not memoized (see above) so the request is
    // retried until the pool can serve it - correct, but for a capture this
    // stage will never have it retries forever. Unthrottled that produced 119
    // identical lines for one capture and rotated mp_debug 34 times in a
    // session, destroying the history needed to diagnose anything else. The
    // retry behaviour is untouched; only the logging is de-duplicated.
    const bool isNewRequest = !mLoggedCaptureRequest.isEqual(wanted);
    if (isNewRequest) {
        mLoggedCaptureRequest = wanted;
        MpDebugLog::write(
            "[Puppet %p] capture request change: wanted='%s' memo='%s' isCaptured=%d curHack='%s' inStage=%d",
            this, wanted, mRequestedCapture.cstr(), frameInfo->isCaptured ? 1 : 0,
            frameInfo->curHack.cstr(), frameInfo->isInSameStage ? 1 : 0);
    }

    // releaseCapture() clears the applied-request memo. If there is no active
    // capture, clear a stale memo explicitly before attempting this request.
    if (mCaptureActor || mIsCaptureModel)
        releaseCapture();
    else {
        mRequestedCapture.clear();
        mRequestedCaptureVariant = -1;
    }

    if (!mModelHolder) {
        MpDebugLog::write("[Puppet %p] no model holder for capture %s - retrying", this, wanted);
        return;
    }

    // A refusal is normal, not an error: the pool only builds models for
    // captures this stage actually placed, and it will not exceed its own
    // ceiling. Keeping the player's own body is better than showing nothing.
    PuppetHackActor* leased = PuppetCapturePool::acquire(wanted, wantedVariant, mPupInfo);
    if (!leased) {
        if (isNewRequest)
            MpDebugLog::write("[Puppet %p] capture refused for %s (pool empty/full)", this, wanted);
        return;
    }

    // Display the capture the way a costume is displayed: as an entry in this
    // puppet's own model holder, selected with changeModel(). That is what puts
    // it on the proven path - syncPose() poses it and startAction() animates
    // it, both keyed off getCurrentModel(), with no hand-driving anywhere.
    //
    // The pool reuses one actor per capture type per stage. A released actor
    // can be leased by another puppet before this puppet asks for the same
    // capture again, so the holder entry must be rebound instead of treated as
    // permanently tied to the first lease.
    // Keyed on the ARCHIVE, not the hack name: two members of one family - the
    // M and the A, the nose and the moustache - are different models under the
    // same name, and a single slot would make each swap kill the other's model
    // through changeModel(). An ordinary capture's archive is stable, so its
    // slot name is stable too.
    al::StringTmp<0x40> slotName("Hack_%s", leased->getArchiveName());
    PlayerModelHolder::Entry* entry = nullptr;
    for (s32 i = 0; i < mModelHolder->mBuffer.size(); ++i) {
        PlayerModelHolder::Entry* candidate = mModelHolder->mBuffer[i];
        if (candidate && al::isEqualString(candidate->name.cstr(), slotName.cstr())) {
            entry = candidate;
            break;
        }
    }

    if (!entry) {
        if (mModelHolder->mBuffer.size() >= mModelHolder->mBuffer.capacity()) {
            MpDebugLog::write("[Puppet %p] no free model slot for %s - keeping body", this, wanted);
            PuppetCapturePool::release(leased);
            return;
        }
        mModelHolder->registerModel(leased, slotName.cstr());

        // registerModel has no success return value. Find the actual entry
        // rather than relying on tryFindModelActor(), which cannot distinguish
        // a missing entry from an existing entry whose actor was cleared.
        for (s32 i = 0; i < mModelHolder->mBuffer.size(); ++i) {
            PlayerModelHolder::Entry* candidate = mModelHolder->mBuffer[i];
            if (candidate && al::isEqualString(candidate->name.cstr(), slotName.cstr())) {
                entry = candidate;
                break;
            }
        }
        if (!entry) {
            MpDebugLog::write("[Puppet %p] failed to register model slot %s - keeping body", this,
                slotName.cstr());
            PuppetCapturePool::release(leased);
            return;
        }
    }

    if (entry->actor != leased)
        MpDebugLog::write("[Puppet %p] rebinding slot %s from %p to %p",
            this, slotName.cstr(), entry->actor, leased);
    entry->actor = leased;

    mCaptureActor = leased;
    mIsCaptureModel = true;

    // A pooled actor arrives with whatever animation state its last rider left
    // on it, so the freeze measurement starts from scratch on every lease.
    resetCaptureMovementState();

    // changeModel kills the outgoing body model and revives the capture, so the
    // body needs no separate makeActorDead.
    al::LiveActor* curModel = getCurrentModel();
    changeModel(slotName.cstr());

    if (getCurrentModel() != leased) {
        MpDebugLog::write("[Puppet %p] holder did not select capture slot %s - retrying",
            this, slotName.cstr());
        mCaptureActor = nullptr;
        mIsCaptureModel = false;
        mRequestedCapture.clear();
        PuppetCapturePool::release(leased);
        return;
    }

    mRequestedCapture = wanted;
    mRequestedCaptureVariant = wantedVariant;
    leased->startHackAnim(true);

    // The capture is now registered and alive. Keep the executor snapshot at
    // the actual wake point; logging it from the pool before registerModel()
    // made it impossible to distinguish a holder-registration failure from a
    // later movement/draw failure.
    LateModelDrawer::logExecutorState(leased, wanted);

    // The thrown cap is NOT killed here any more. A captured player is still
    // wearing Cappy - on the enemy's head - and this puppet's cap actor is what
    // draws it. control()'s visibility block runs later in this same frame and
    // decides from the sender's cap state, so leave it alone.

    // Diagnostic for the recurring ModelDrawerBase::addModelToTable draw-
    // pipeline crash (Data Abort, address 0 - see memory.txt): capture
    // switching is the highest-frequency model-swap path in this file, so
    // its pointers are the most useful ones to cross-reference against a
    // future crash's X0/X1 registers.
    al::ModelKeeper* leasedKeeper = leased->getModelKeeper();
    MpDebugLog::write("[Puppet %p] capture acquired hack=%s leased=%p leasedModelCtrl=%p bodyModel=%p",
        this, wanted, leased, leasedKeeper ? leasedKeeper->getModelCtrl() : nullptr, curModel);
}

void PuppetActor::tryRefreshRide(PuppetInfo* frameInfo) {
    if (!frameInfo)
        return;

    // Same heap discipline as tryRefreshCapture: this runs from the live puppet
    // update, after the scene's own current-heap scope has ended.
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    const bool wantsRide = frameInfo->isRiding && !frameInfo->curHack.isEmpty();
    const char* wanted = wantsRide ? frameInfo->curHack.cstr() : "";

    if (!wantsRide) {
        if (mRideActor)
            releaseRide();
        return;
    }

    if (mRideActor && mRequestedRide.isEqual(wanted))
        return;

    if (mRideActor)
        releaseRide();

    // A refusal is normal - the pool only builds what this stage placed - and
    // costs nothing but the missing vehicle, so it stays retryable.
    // No variant: neither vehicle is one name over a family of archives.
    PuppetHackActor* leased = PuppetCapturePool::acquire(wanted, -1, mPupInfo);
    if (!leased)
        return;

    // No PlayerModelHolder registration, so nothing else will wake it.
    leased->makeActorAlive();

    mRideActor = leased;
    mRequestedRide = wanted;
    MpDebugLog::write("[Puppet %p] ride acquired %s leased=%p", this, wanted, leased);
}

void PuppetActor::updateRideAction(PuppetInfo* frameInfo) {
    if (!mRideActor || !frameInfo)
        return;

    // Motion is measured from the synced position, so the animation always
    // agrees with what the vehicle is visibly doing - no extra wire, and no
    // dependence on the sender's own state machine.
    //
    // The hold is what makes it usable: PlayerInf arrives every few frames, so
    // on most frames the position is identical to the last one and a bare
    // comparison would read as "stopped". cRideMoveHoldFrames outlives the gap
    // between packets, so the vehicle only settles to idle when the player has
    // genuinely stopped.
    static constexpr s32 cRideMoveHoldFrames = 12;
    static constexpr f32 cRideMoveEpsilonSq = 1.0f;

    if (mHasLastRidePos) {
        const sead::Vector3f delta = frameInfo->playerPos - mLastRidePos;
        const f32 movedSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (movedSq > cRideMoveEpsilonSq)
            mRideMovingFrames = cRideMoveHoldFrames;
        else if (mRideMovingFrames > 0)
            --mRideMovingFrames;
    }

    mLastRidePos = frameInfo->playerPos;
    mHasLastRidePos = true;

    const char* action = CaptureSync::findRideAction(mRequestedRide.cstr(), mRideMovingFrames > 0);
    if (action)
        mRideActor->startAction(action);
}

void PuppetActor::releaseRide() {
    PuppetHackActor* leased = mRideActor;
    mRideActor = nullptr;
    mRequestedRide.clear();
    mHasLastRidePos = false;
    mRideMovingFrames = 0;

    if (leased) {
        MpDebugLog::write("[Puppet %p] ride released leased=%p", this, leased);
        // release() puts it back to sleep for us.
        PuppetCapturePool::release(leased);
    }
}

void PuppetActor::releaseCapture() {
    PuppetHackActor* leased = mCaptureActor;
    mCaptureActor = nullptr;
    mRequestedCapture.clear();
    mRequestedCaptureVariant = -1;
    resetCaptureMovementState();

    if (mIsCaptureModel) {
        mIsCaptureModel = false;

        // changeModel rather than simply reviving what the holder points at: a
        // costume that changed mid-capture deliberately did not take effect
        // (tryRefreshCostume leaves the swap to whoever owns the displayed
        // model), so this is where the current pair is finally applied.
        // Re-selecting the pair already in use is a no-op beyond the dead/alive
        // flip that has to happen anyway.
        //
        // BEFORE handing the actor back, not after: changeModel calls
        // makeActorDead on the outgoing model, which is the capture. Releasing
        // first would leave it alive and still this puppet's current model at
        // the moment another puppet could lease it.
        changeModel(mIs2DModel ? mNormal2DModelName.cstr() : mNormalModelName.cstr());

        startAction("Wait");
    }

    if (leased) {
        MpDebugLog::write("[Puppet %p] capture released leased=%p", this, leased);
        PuppetCapturePool::release(leased);
    }
}

void PuppetActor::initOnline(PuppetInfo* pupInfo) {
    mPupInfo = pupInfo;

    mPuppetCap->initOnline(pupInfo);
}

void PuppetActor::control() {
    // ClientReadThread owns the incoming records. Copy one coherent frame
    // under the hand-off guard, then release it before touching models. A
    // late costume build can take many milliseconds and may synchronously
    // execute game hooks; holding the shared guard across that work lets the
    // reader starve the game thread while it waits to publish the next pose.
    PuppetInfo frameInfo;
    {
        PuppetStateGuard stateGuard;
        if (!mPupInfo)
            return;
        frameInfo = *mPupInfo;
    }
    PuppetInfo* const info = &frameInfo;

    {
        // A thrown-cap replacement is independent of the body holder and can
        // be applied even while the puppet is riding a capture.  Construction
        // happened on an earlier control frame; kill the old registered actor
        // before publishing the new pointer, then the normal visibility block
        // below wakes it if Cappy is currently in flight.
        if (mPendingPuppetCap) {
            const char* wantedCap = tryGetPuppetCapName(info);
            if (!wantedCap || !mPendingThrownCapName.isEqual(wantedCap)) {
                MpDebugLog::write(
                    "[Puppet %p] discarding stale pending cap %s (now wants %s) actor=%p",
                    this, mPendingThrownCapName.cstr(), wantedCap ? wantedCap : "(null)",
                    mPendingPuppetCap);
                mPendingPuppetCap = nullptr;
                mPendingThrownCapName.clear();
            } else {
                // Seed every visible field while the replacement is still
                // dead, before publishing it to the visibility/wake path.
                mPendingPuppetCap->applySnapshot(*info);

                PuppetCapActor* oldCap = mPuppetCap;
                if (oldCap && al::isAlive(oldCap))
                    oldCap->makeActorDead();
                mPuppetCap = mPendingPuppetCap;
                mBuiltThrownCapName = mPendingThrownCapName;
                mPendingPuppetCap = nullptr;
                mPendingThrownCapName.clear();
                MpDebugLog::write("[Puppet %p] applied pending cap old=%p new=%p", this,
                    oldCap, mPuppetCap);
            }
        }

        // Must run BEFORE tryRefreshCostume() below, and specifically not in
        // the same control() call that set it - see tryRefreshCostume()'s
        // comment. This applies a swap that was built on a STRICTLY EARLIER
        // frame; tryRefreshCostume() runs after and will see
        // mBuiltBodyName/mBuiltCapName already match, so it won't re-trigger
        // a build this same frame.
        if (mPendingCostumeApply) {
            if (!mIsCaptureModel) {
                mPendingCostumeApply = false;
                MpDebugLog::write("[Puppet %p] applying pending costume -> %s", this,
                    mIs2DModel ? mNormal2DModelName.cstr() : mNormalModelName.cstr());
                changeModel(mIs2DModel ? mNormal2DModelName.cstr() : mNormalModelName.cstr());
                startAction("Wait");
                MpDebugLog::write("[Puppet %p] costume activation complete", this);
            }
            // else: still riding a capture - leave the flag set so this is
            // retried next frame instead of losing the swap. releaseCapture()
            // already takes the new pair via changeModel() when the capture
            // ends, at which point this flag being stale and true is
            // harmless (it'll just re-apply the same, already-current name).
        }

        // Only one family of native late model builders runs in a control
        // frame.  The body pair is built first; the independent thrown-cap
        // replacement follows on a later frame, and each is activated later
        // still.  Capture leasing does not construct a model here (the pool
        // built it earlier), so it remains safe to process afterwards.
        const bool builtCostumeThisFrame = tryRefreshCostume(info);
        if (!builtCostumeThisFrame)
            tryRefreshThrownCap(info);
        tryRefreshCapture(info);
        tryRefreshRide(info);

        al::LiveActor* curModel = getCurrentModel();
        if (!curModel)
            return;

        // Animation Updating
        //
        // Two different action SOURCES, keyed off which model is current: the
        // remote player's own action for a body model, the remote player's
        // CAPTURE's action for a capture. Both travel in the same PlayerInf
        // field and both are named out of PlayerAnims - which is wide enough
        // for either, and is what makes a Frog's "Run" survive the trip (see
        // PuppetInfo::curCaptureAnim). startAction() already targets
        // getCurrentModel(), so with the capture registered in the model holder
        // it needs no special casing - only the name source differs.
        //
        // Blend weights stay body-only: they are Mario's animation blend slots.
        if (mIsCaptureModel) {
            startCaptureAction(curModel, PlayerAnims::FindStr(info->curCaptureAnim),
                info->captureAnimFrame);
            syncCaptureAnimFrame(curModel, info->captureAnimFrame);

            const CaptureSync::Entry* entry = CaptureSync::findByHackName(info->curHack.cstr());
            if (entry && entry->adapter && entry->adapter->apply) {
                entry->adapter->apply(static_cast<PuppetHackActor*>(curModel), &info->blendWeights[2]);
            }

            // After both of the above have read it: startCaptureAction needs
            // the PREVIOUS packet's phase to tell a sender that restarted its
            // action from one that is simply holding the last frame.
            mLastRemoteCaptureFrame = info->captureAnimFrame;
        } else {
            if (!al::isActionPlaying(curModel, info->curSubAnimStr.cstr())) {
                startAction(info->curAnimStr.cstr());
            } else if (al::isActionEnd(curModel)) {
                startAction(info->curAnimStr.cstr());
            }

            // A rider's blend slots are carrying the vehicle's transform, not
            // blend weights - see PlayerInfPacket.h. Feeding them to the body
            // would put an offset of several hundred units into an animation
            // blend.
            if (isNeedBlending() && !info->hasRideTransform) {
                for (size_t i = 0; i < 6; i++) {
                    setBlendWeight(i, info->blendWeights[i]);
                }
            }
        }

        // Position & Rotation Handling

        // Use smooth movement if low latency is disabled, otherwise snap directly
        al::setTrans(this, info->playerPos);
        al::setQuat(this, info->playerRot);

        // No hand-driving of the capture at all now. It is the current model,
        // so syncPose() below poses it and the action block above animates it -
        // exactly what happens to a body model. A body model is never manually
        // update()d by this class either; its per-frame work comes from its own
        // executors.

        // Model Updating

        // The 2D swap only owns the model while a capture does not - otherwise
        // it would yank a captured puppet straight back to Mario every frame.
        if (!mIsCaptureModel) {
            if (!mIs2DModel && info->is2D) {
                changeModel(mNormal2DModelName.cstr());
                mIs2DModel = true;

            } else if (mIs2DModel && !info->is2D) {
                changeModel(mNormalModelName.cstr());
                mIs2DModel = false;
            }
        } else {
            mIs2DModel = info->is2D;
        }

        // A model swap changes the object whose sub-actors carry Mario's
        // costume visibility state. Keep the native hair/beard/strap rules in
        // sync after selecting the final model for this frame. Capture models
        // intentionally do not have Mario's sub-actor layout.
        curModel = getCurrentModel();
        if (!mIsCaptureModel)
            hairControl();

        // Visibility Updating

        // Two ways for a remote Cappy to be visible, and the second one was
        // missing.
        //
        // Thrown: the classic case - Cappy is in flight, away from its owner.
        //
        // Worn on a capture: the hat on a captured enemy is NOT part of the
        // capture archive. HackCap::exeHack hides HackCap's own model and shows
        // its separate "MarioCap"/"Display" actor, posed from HackCap's own
        // transform. The capture archive's HackOn animation only reveals the
        // moustache - which is precisely what a synced capture showed. So keep
        // this puppet's cap actor alive while it rides a capture and let it sit
        // at the pose the sender transmits for it.
        // The worn hat is only shown when this puppet is also being given the
        // capture's animation phase. Without it the hat is posed from the
        // sender's skeleton while the capture plays on its own, and it swims
        // around the head instead of sitting on it.
        const bool isCapVisible = info->isCapThrow
            || (cSyncCapOnHack && mIsCaptureModel && info->isCapOnHack
                && info->captureAnimFrame >= 0.f);

        if (isCapVisible) {
            if (al::isDead(mPuppetCap)) {
                mPuppetCap->makeActorAlive();
                al::setTrans(mPuppetCap, info->capPos);
            }
        } else {
            if (al::isAlive(mPuppetCap)) {
                mPuppetCap->makeActorDead();

                // startAction(mInfo->curSubAnimStr);

                al::LiveActor* headModel = al::tryGetSubActor(curModel, "頭");
                if (headModel) {
                    al::startVisAnimForAction(headModel, "CapOn");
                }
            }
        }

        if (mNameTag) {
            if (!mNameTag->mIsAlive)
                mNameTag->appear();
            mNameTag->mIsAlive = true;
        }

        // Sub-Actor Updating

        mPuppetCap->update();

        // Capture movement
        //
        // Placed after the action block above so the action set this frame is
        // the one that gets committed, and before syncPose() so the pose
        // written afterwards is what calcAnim() builds the matrix from in the
        // draw phase.
        if (mIsCaptureModel && mCaptureActor && al::isAlive(mCaptureActor))
            updateCaptureMovement();

        // Syncing

        syncPose();

        // The wrapper received the correct packet pose, but live diagnostics
        // showed copyPose() leaving both fully-active body models and leased
        // capture models at the origin.  Preserve copyPose for scale and the
        // remaining native pose state, then make the coherent network
        // translation/quaternion authoritative on the selected model.
        curModel = getCurrentModel();
        if (curModel) {
            al::setTrans(curModel, info->playerPos);
            al::setQuat(curModel, info->playerRot);
        }

        // The vehicle rides the same transform as the body. No offset: the
        // rider's own animation already places Mario on the seat, and the
        // packet position is the player's, which is where the vehicle is.
        // The vehicle is NOT at the rider's transform - Mario is bound to a
        // point on it, so it sits at a fixed offset and rotation in his local
        // frame. That relationship is measured on the sender and arrives in the
        // blend slots; until the first such packet lands the vehicle is left
        // where it is rather than snapped to the rider, which is what put the
        // scooter beside the puppet instead of under it.
        if (mRideActor && info->hasRideTransform) {
            const sead::Vector3f offset = RideTransform::rotate(info->playerRot, info->rideOffset);
            sead::Vector3f ridePos = info->playerPos;
            ridePos.x += offset.x;
            ridePos.y += offset.y;
            ridePos.z += offset.z;

            al::setTrans(mRideActor, ridePos);
            al::setQuat(mRideActor,
                RideTransform::mul(info->playerRot,
                    RideTransform::yawOnly(info->rideQuatY, info->rideQuatW)));
        }

        if (mRideActor)
            updateRideAction(info);

        updateActivationCheck();
    }
}

void PuppetActor::startCaptureAction(al::LiveActor* capture, const char* actName,
    f32 remoteFrame) {
    if (!capture || !actName || actName[0] == '\0')
        return;

    // Slot 1 is the capture's second partial-animation slot - Yoshi's tongue
    // rides there while the body plays its ordinary action. Only archives that
    // set up more than one slot have it.
    //
    // al::isPartialAnimPlaying is declared in the OdysseyHeaders but does not
    // exist in the binary; a null playing-name asks the same question and links.
    if (AlUnlinked::getPartialSklAnimSlotNum(capture) > 1) {
        const char* playing = AlUnlinked::getPlayingPartialSklAnimName(capture, 1);
        if (mPupInfo->curCaptureSubAnim != PlayerAnims::Type::Unknown) {
            const char* subActName = PlayerAnims::FindStr(mPupInfo->curCaptureSubAnim);
            if (subActName && subActName[0] != '\0') {
                if (!playing || !al::isEqualString(playing, subActName))
                    AlUnlinked::startPartialSklAnim(capture, subActName, 0, 1, nullptr);
            }
        } else if (playing) {
            AlUnlinked::clearPartialSklAnim(capture, 1);
        }
    }

    const char* current = al::getActionName(capture);

    // A different action: ordinary start, same as a body model.
    if (!current || !al::isEqualString(current, actName)) {
        if (al::tryStartActionIfNotPlaying(capture, actName)) {
            const char* started = al::getActionName(capture);
            if (started && al::isSklAnimExist(capture, started))
                al::clearSklAnimInterpole(capture);
            // Several captures are two models posed together, and the real
            // actor starts the action on both halves - see
            // PuppetHackActor::startActionOnSubActors.
            static_cast<PuppetHackActor*>(capture)->startActionOnSubActors(actName);
        }
        return;
    }

    // The SAME action, already playing, and out of frames.
    //
    // Enemies loop their locomotion by restarting a one-shot action rather
    // than by playing a looping animation. FrogStateHack is literally:
    //
    //     if (speed > 12.0 && al::isActionPlaying(this, "Run")
    //         && al::isActionEnd(this))
    //         al::startAction(this, "Run");
    //
    // - one hop per start. The restart never changes the action NAME, so
    // nothing about it reaches this side: the puppet played a single hop and
    // held its last frame while the position kept sliding. Nor can
    // tryStartActionIfNotPlaying fix it - "not playing" means "a different
    // action is current", and an ended action is still the current one.
    //
    // So mirror the enemy's own rule: while the sender keeps reporting this
    // action, restart it whenever our copy runs out.
    if (!al::isActionEnd(capture))
        return;

    // ...but ONLY when the sender is looping it too, and this is the whole of
    // the "binoculars stay at the base" and "the rocket never takes off" bug.
    //
    // Not every capture action is a loop. Plenty are one-shot STATE changes
    // that end in a pose and hold it: Fukankun plays "Up" to raise the scope out
    // of its stand and then sits in "UpWait" (ida_context/MapObj/Fukankun.md,
    // exeCaptureStartEnd 0x0289110 / exeCaptureView 0x028987c), and
    // BazookaElectric plays "FlyUp" as the rocket leaves the launcher
    // (BazookaElectric.md, exeLaunchPossession 0x0219b64). Neither actor moves
    // its own transform for those - the entire visible motion is in the
    // animation - so a copy that rewinds to frame 0 every time it reaches the
    // end never leaves the ground, which is exactly what was reported: the
    // synced hat rose (it is sent as an absolute position) and the capture
    // under it did not.
    //
    // The sender's own phase answers it. While it keeps climbing or holds at
    // the end, the sender has not restarted and neither should we - the phase
    // sync below is what keeps the two copies together. Only a phase that goes
    // BACKWARDS is a real restart, which is precisely what a looping enemy
    // action does every cycle, so the Frog keeps hopping.
    //
    // A peer that reports no phase at all (captureAnimFrame < 0) keeps the old
    // unconditional behaviour; there is nothing better to go on.
    if (remoteFrame >= 0.f && !(mLastRemoteCaptureFrame > remoteFrame))
        return;

    // Only once the animation has actually run. An action with no skeletal
    // animation - or one whose animation never advances - reports "ended"
    // permanently, and restarting that every frame would re-fire its effects
    // and sounds forever without a single frame of movement.
    al::ModelKeeper* modelKeeper = capture->getModelKeeper();
    const bool hasRunFrames = modelKeeper && modelKeeper->getAnimSkl()
        && al::getSklAnimFrame(capture, 0) > 0.0f;
    if (!hasRunFrames)
        return;

    // Unconditional start, not the try-if-not-playing form: the whole point is
    // to rewind an action that is already current.
    al::startAction(capture, actName);
    if (al::isSklAnimExist(capture, actName))
        al::clearSklAnimInterpole(capture);
    static_cast<PuppetHackActor*>(capture)->startActionOnSubActors(actName);
}

void PuppetActor::syncCaptureAnimFrame(al::LiveActor* capture, f32 remoteFrame) {
    // Negative means this peer does not report a phase; the capture keeps
    // running the action on its own, which is what shipped before.
    if (!capture || remoteFrame < 0.0f)
        return;

    al::ModelKeeper* modelKeeper = capture->getModelKeeper();
    if (!modelKeeper || !modelKeeper->getAnimSkl())
        return;

    // An AnimPlayerSkl is NOT enough, and this is what crashed the RC Car
    // (crash report 01788365528: Data Abort at 0, PC al::AnimPlayerSkl::
    // setSklAnimFrame+0x30, LR in this module, reached through
    // al::LiveActor::movement -> PuppetActor::control).
    //
    // AnimPlayerSkl keeps an array of animation objects, one per blend slot,
    // and every entry starts null; a slot only gets an object once an
    // animation has actually been STARTED in it. Its readers are all written
    // for that - getSklAnimFrame, getPlayingSklAnimName and isSklAnimPlaying
    // each test the slot pointer and answer 0 / null / false - but
    // setSklAnimFrame is the one that does not:
    //
    //     LDR X8, [X8]        ; mAnimObjArray[index]
    //     LDR X20, [X8,#8]    ; <- faults on address 8 when that is null
    //
    // al::setActionFrame does not protect against this either: its guard is
    // al::isSklAnimExist(actor), which only asks whether the actor has an
    // AnimPlayerSkl at all - exactly the check above. A capture whose archive
    // has an animation player but whose current action drives no skeletal
    // animation (the RC Car's, whose motion is all joint springs) therefore
    // reaches the fault with both guards satisfied.
    //
    // isSklAnimPlaying(actor, 0) is the missing question: it is precisely
    // "mAnimObjArray[0] != nullptr", and it is null-safe.
    if (!al::isSklAnimPlaying(capture, 0))
        return;

    // Only when the value is NEW - i.e. a packet has landed since the last
    // time this ran. In between, the local movement() pass advances the
    // animation exactly as the sender's does, which is the right prediction
    // and keeps the model smooth at 60 while packets arrive at half that.
    //
    // Correcting on arrival rather than "when the drift exceeds a threshold"
    // is deliberate. The received frame is a couple of frames old, so the
    // local copy legitimately runs slightly ahead of it; a threshold rule
    // would read that constant, harmless lead as error and yank the animation
    // backwards forever. What matters is not that the two are equal in
    // absolute time but that everything drawn this frame comes from the SAME
    // instant of the sender's: the capture's pose, its position, and the cap
    // sitting on its head all arrive in one packet, so applying the phase when
    // that packet lands puts them back in agreement - which is precisely what
    // the swimming hat was: a hat from one instant on a head from another.
    //
    // The exception is a copy that has fallen BEHIND a phase that is no longer
    // changing. A sender parked at the end of a one-shot action reports the
    // same frame indefinitely, so "unchanged" stops meaning "no new packet" and
    // starts meaning "nothing will ever correct us again" - and a local copy
    // that rewound is stuck rewound. That is the second half of the binoculars
    // and mini-rocket bug: the sender held "Up" / "FlyUp" at its last frame
    // while our copy sat back near zero, and the constant value could not pull
    // it forward.
    //
    // Deliberately one-sided. Running AHEAD of the reported phase is the normal,
    // healthy state - the packet is a couple of frames old and the local
    // movement() pass predicts past it - and during a packet stall that lead
    // grows without anything being wrong; snapping back for it would turn a
    // dropped packet into a visible hitch. Only being far behind a phase that
    // has stopped moving is evidence of a copy that lost its place, and the gap
    // is wide enough that ordinary jitter never reaches it.
    static constexpr f32 cCaptureFrameRewindGap = 8.0f;
    if (remoteFrame == mAppliedCaptureFrame
        && remoteFrame - al::getSklAnimFrame(capture, 0) < cCaptureFrameRewindGap)
        return;

    mAppliedCaptureFrame = remoteFrame;

    // Tell the freeze detector that this frame's movement came from a packet,
    // not from the animation running. Without it, writing a new frame here
    // every time a packet lands would look exactly like a healthy animation
    // and updateCaptureMovement() would never notice that nothing is calling
    // movement() - leaving the capture stepping at the packet rate instead of
    // playing at the frame rate.
    mCaptureFrameSynced = true;

    // This is al::setActionFrame's entire body (it checks isSklAnimExist and
    // forwards to slot 0), spelled out to avoid another symbol.
    al::setSklAnimFrame(capture, remoteFrame, 0);
}

void PuppetActor::updateCaptureMovement() {
    al::LiveActor* capture = mCaptureActor;
    if (!capture)
        return;

    // Why this is measured rather than deduced.
    //
    // al::LiveActor::movement() is the only thing that advances an animation:
    // it runs ActorActionKeeper::updatePrev, ModelKeeper::update,
    // ActorActionKeeper::updatePost and ModelKeeper::updateLast. calcAnim(),
    // which the draw side always runs and which has no dead/clipped guard at
    // all, only EVALUATES whatever frame the animation is currently on and
    // builds the matrix from the actor's pose. So an actor that is drawn and
    // posed but never moved renders perfectly, follows the player around, and
    // is frozen on one frame - which is exactly what a synced capture looked
    // like: sliding, not animating.
    //
    // The previous version asked whether the capture was registered in a
    // movement executor and hand-drove it only when it was not. That question
    // has the wrong answer available: membership in an updater list is not
    // proof that the list runs this actor's movement(), and any early return
    // inside movement() (the isDead / isClipped guard at its top) looks
    // identical from the outside. Registered-but-frozen is the state that
    // shipped.
    //
    // So ask the only question that matters - did the skeletal animation frame
    // move? - and take over when it did not.
    al::ModelKeeper* modelKeeper = capture->getModelKeeper();
    const f32 frame = modelKeeper && modelKeeper->getAnimSkl()
        ? al::getSklAnimFrame(capture, 0)
        : 0.0f;
    const bool advanced = frame != mCaptureAnimFrame;
    mCaptureAnimFrame = frame;

    // Set on the frame we deliberately skipped our own movement() call, so
    // `advanced` this frame is entirely the engine's doing.
    const bool wasProbe = mCaptureMovementProbe;
    mCaptureMovementProbe = false;

    // A frame this puppet wrote itself (a phase correction from a packet)
    // proves nothing either way about whether anything is driving movement().
    // Rebase on it and pass no verdict; the frames in between packets - which
    // at 30Hz packets and 60Hz rendering is most of them - still answer the
    // question.
    if (mCaptureFrameSynced) {
        mCaptureFrameSynced = false;
        if (mCaptureMovementTakeover && mCaptureMovementProbeTimer > 0)
            --mCaptureMovementProbeTimer;
        if (mCaptureMovementTakeover)
            capture->movement();
        return;
    }

    if (advanced)
        mCaptureAnimStallFrames = 0;
    else
        ++mCaptureAnimStallFrames;

    if (!mCaptureMovementTakeover) {
        const bool engineDriven = LateModelDrawer::isDrivenByMovementExecutor(capture);

        if (!engineDriven || mCaptureAnimStallFrames >= cCaptureStallFrames) {
            mCaptureMovementTakeover = true;
            mCaptureAnimStallFrames = 0;
            mCaptureMovementProbeTimer = cCaptureProbeInterval;
            MpDebugLog::write(
                "[Puppet %p] capture %s frozen (engineDriven=%d frame=%.2f action=%s) - driving movement() by hand",
                this, mRequestedCapture.cstr(), engineDriven ? 1 : 0, frame,
                al::getActionName(capture) ? al::getActionName(capture) : "(none)");
        }
    } else if (wasProbe && advanced) {
        // The frame moved on a pass where this puppet did nothing, so the
        // engine is driving the same actor after all. Stop, before every
        // capture animation plays at double speed.
        mCaptureMovementTakeover = false;
        mCaptureAnimStallFrames = 0;
        MpDebugLog::write(
            "[Puppet %p] capture %s advanced on its own - handing movement() back to the engine",
            this, mRequestedCapture.cstr());
    }

    if (!mCaptureMovementTakeover)
        return;

    // Still frozen while this puppet is the one calling movement() every frame?
    // Then the freeze is inside movement() itself, and there are only two ways
    // in: the guard at the top of al::LiveActor::movement (isDead, or isClipped
    // without isDrawClipped) or an action that was never started. Both are
    // recorded here, once per lease, so the next session's log says which.
    if (mCaptureAnimStallFrames >= cCaptureDriveDiagFrames && !mCaptureDriveDiagLogged) {
        mCaptureDriveDiagLogged = true;
        const al::LiveActorFlag* flags = capture->getFlags();
        MpDebugLog::write(
            "[Puppet %p] capture %s STILL frozen while hand-driven: dead=%d clipped=%d drawClipped=%d hidden=%d animSkl=%p action=%s frame=%.2f",
            this, mRequestedCapture.cstr(), flags && flags->isDead ? 1 : 0,
            flags && flags->isClipped ? 1 : 0, flags && flags->isDrawClipped ? 1 : 0,
            flags && flags->isModelHidden ? 1 : 0,
            modelKeeper ? static_cast<void*>(modelKeeper->getAnimSkl()) : nullptr,
            al::getActionName(capture) ? al::getActionName(capture) : "(none)", frame);
    }

    // One skipped frame every couple of seconds is the price of noticing that
    // the engine has taken this actor over (a pool model is shared, and the
    // executor tables are repaired behind our back). It is not visible; a
    // permanently doubled animation speed would be.
    if (--mCaptureMovementProbeTimer <= 0) {
        mCaptureMovementProbeTimer = cCaptureProbeInterval;
        mCaptureMovementProbe = true;
        return;
    }

    // A PuppetHackActor is safe to move by hand: its nerve keeper is null, its
    // hit sensors are cleared and it does not override control(), so movement()
    // reduces to exactly the animation bookkeeping this is here for.
    capture->movement();
}

void PuppetActor::resetCaptureMovementState() {
    mAppliedCaptureFrame = -1.0f;
    mLastRemoteCaptureFrame = -1.0f;
    mCaptureFrameSynced = false;
    mCaptureAnimFrame = -1.0f;
    mCaptureAnimStallFrames = 0;
    mCaptureMovementProbeTimer = 0;
    mCaptureMovementProbe = false;
    mCaptureMovementTakeover = false;
    mCaptureDriveDiagLogged = false;
}

void PuppetActor::setBlendWeight(int index, float weight) {
    al::LiveActor* curModel = getCurrentModel();
    if (curModel && curModel->mActionKeeper && index >= 0 && index < 6) {
        al::setSklAnimBlendWeight(curModel, weight, index);
    }
}

void PuppetActor::makeActorAlive() {
    al::LiveActor* curModel = getCurrentModel();

    if (curModel && al::isDead(curModel)) {
        curModel->makeActorAlive();
        // Keep the same post-activation visibility step used by
        // PlayerModelChangerHakoniwa::changeModel(). This also covers the
        // initial costume, whose holder entry is selected directly during
        // init(), and models revived after the puppet leaves a dead state.
        al::showModel(curModel);
    }

    if (curModel)
        scheduleActivationCheck(curModel,
            mIsCaptureModel && !mRequestedCapture.isEmpty()
                ? mRequestedCapture.cstr()
                : mNormalModelName.cstr());

    // update name tag when puppet becomes active again
    if (mPupInfo) {
        if (mNameTag) {
            mNameTag->setText(mPupInfo->puppetName.cstr());
        }
    }

    al::LiveActor::makeActorAlive();
}

void PuppetActor::makeActorDead() {
    // Logged because this is one of only two paths that can drop a capture, and
    // a lease/release thrash in mp_debug cannot otherwise be told apart from
    // tryRefreshCapture() changing its mind.
    MpDebugLog::write("[Puppet %p] makeActorDead (capture=%p inStage=%d)", this, mCaptureActor,
        mPupInfo && mPupInfo->isInSameStage ? 1 : 0);

    // First, so the pool gets its models back the moment this puppet leaves
    // the stage or disconnects, rather than holding them for the rest of it.
    releaseCapture();
    releaseRide();

    mActivationCheckActor = nullptr;
    mActivationCheckTag.clear();
    mActivationCheckDelay = 0;
    mActivationCheckAttempts = 0;

    al::LiveActor* curModel = getCurrentModel();

    if (curModel && !al::isDead(curModel)) {
        curModel->makeActorDead();
    }

    if (mPuppetCap && !al::isDead(mPuppetCap))
        mPuppetCap->makeActorDead();

    al::LiveActor::makeActorDead();
    MpDebugLog::write("[Puppet %p] makeActorDead complete", this);
}

void PuppetActor::attackSensor(al::HitSensor* source, al::HitSensor* target) {
    return;

    // if (!al::sendMsgPush(target, source)) {
    //     rs::sendMsgPushToPlayer(target, source);
    // }
}

bool PuppetActor::receiveMsg(const al::SensorMsg* msg, al::HitSensor* source, al::HitSensor* target) {
    return false;

    // if ((al::isMsgPlayerTrampleReflect(msg) || rs::isMsgPlayerAndCapObjHipDropReflectAll(msg)) && al::isSensorName(target, "Body")) {
    //     rs::requestHitReactionToAttacker(msg, target, source);
    //     return true;
    // }
}

void PuppetActor::startAction(const char* actName) {
    al::LiveActor* curModel = getCurrentModel();

    if (!curModel || !actName || actName[0] == '\0')
        return;

    if (al::tryStartActionIfNotPlaying(curModel, actName)) {
        const char* curActName = al::getActionName(curModel);
        if (curActName) {
            if (al::isSklAnimExist(curModel, curActName)) {
                al::clearSklAnimInterpole(curModel);
            }
        }
    }

    for (size_t i = 0; i < 5; i++) {
        al::LiveActor* subActor = al::tryGetSubActor(curModel, subActorNames[i]);
        const char* curActName = al::getActionName(curModel);
        if (subActor && curActName) {
            if (al::tryStartActionIfNotPlaying(subActor, curActName)) {
                if (al::isSklAnimExist(curModel, curActName)) {
                    al::clearSklAnimInterpole(curModel);
                }
            }
        }
    }

    al::LiveActor* faceActor = al::tryGetSubActor(curModel, "顔");

    if (faceActor) {
        al::StringTmp<0x80> faceAnim("%sFullFace", actName);
        if (al::tryStartActionIfNotPlaying(faceActor, faceAnim.cstr())) {
            if (al::isSklAnimExist(faceActor, faceAnim.cstr())) {
                al::clearSklAnimInterpole(faceActor);
            }
        }
    }
}

void PuppetActor::hairControl() {
    al::LiveActor* curModel = getCurrentModel();

    if (!curModel || !mCostumeInfo || !mCostumeInfo->mBodyInfo || !mCostumeInfo->mHeadInfo || !curModel->getSubActorKeeper())
        return;

    al::LiveActor* headModel = al::tryGetSubActor(curModel, "頭");
    if (!headModel)
        return;

    if (mCostumeInfo->isNeedSyncBodyHair()) {
        al::LiveActor* hairModel = al::tryGetSubActor(curModel, "髪");
        if (hairModel)
            PlayerFunction::syncBodyHairVisibility(hairModel, headModel);
    }
    if (mCostumeInfo->isSyncFaceBeard()) {
        al::LiveActor* faceModel = al::tryGetSubActor(curModel, "顔");
        if (faceModel)
            PlayerFunction::syncMarioFaceBeardVisibility(faceModel, headModel);
    }
    if (mCostumeInfo->isSyncStrap())
        PlayerFunction::syncMarioHeadStrapVisibility(headModel);
    if (PlayerFunction::isNeedHairControl(mCostumeInfo->mBodyInfo, mCostumeInfo->mHeadInfo->costumeName))
        PlayerFunction::hideHairVisibility(headModel);
}

bool PuppetActor::isNeedBlending() {
    al::LiveActor* curModel = getCurrentModel();
    if (!curModel)
        return false;

    const char* curActName = al::getActionName(curModel);
    if (curActName) {
        return al::isEqualSubString(curActName, "Move") || al::isEqualSubString(curActName, "Sand") || al::isEqualSubString(curActName, "MotorcycleRide");
    } else {
        return false;
    }
}

void PuppetActor::changeModel(const char* newModel) {
    // Diagnostic for the ModelDrawerBase::addModelToTable / ExecutorActor-
    // ExecuteBase::addActor draw+movement-pipeline crash family (Data
    // Abort, address 0 - see memory.txt): logging both the outgoing and
    // incoming model/ModelCtrl so a future crash's registers can be
    // matched against one of these. Two targeted fixes both failed here
    // (forcing a movement()/calcAnim() pass on the new model; skipping
    // makeActorDead() on the old one) - root-caused to tryRefreshCostume()
    // building genuinely NEW models live, mid-session, which is now
    // handled by the late-registration path above. The same sequence is used
    // for initial models, rebuilt costume pairs,
    // and pooled capture actors. Missing-model checks below keep a malformed
    // or partially initialized pair from turning a packet into a null deref.
    if (!mModelHolder || !newModel)
        return;

    al::LiveActor* newModelActor = mModelHolder->tryFindModelActor(newModel);
    if (!newModelActor) {
        MpDebugLog::write("[Puppet %p] changeModel refused missing model %s", this, newModel);
        return;
    }

    al::LiveActor* oldModel = getCurrentModel();
    al::ModelKeeper* oldKeeper = oldModel ? oldModel->getModelKeeper() : nullptr;
    MpDebugLog::write("[Puppet %p] changeModel -> %s oldModel=%p oldModelCtrl=%p", this, newModel,
        oldModel, oldKeeper ? oldKeeper->getModelCtrl() : nullptr);

    if (oldModel && oldModel != newModelActor)
        oldModel->makeActorDead();

    if (oldModel == newModelActor) {
        if (al::isDead(newModelActor))
            newModelActor->makeActorAlive();
        al::showModel(newModelActor);
        syncPose();
        scheduleActivationCheck(newModelActor, newModel);
        return;
    }

    mModelHolder->changeModel(newModel);
    newModelActor = getCurrentModel();
    if (!newModelActor) {
        MpDebugLog::write("[Puppet %p] changeModel lost model %s", this, newModel);
        return;
    }

    newModelActor->makeActorAlive();

    // PlayerModelChangerHakoniwa::changeModel() performs this native
    // visibility synchronization after reviving the selected model.  Use the
    // unconditional form: showModelIfHide() skips both ModelKeeper::show and
    // the draw request whenever the flag already says visible, even if this
    // model was just removed from its executor while asleep.
    al::showModel(newModelActor);

    // Seed the incoming model with this puppet's current pose before anything
    // can draw it.
    //
    // al::resetPosition() used to be here, mirroring the native
    // PlayerModelChanger. That is wrong for a pooled capture: its actor was
    // built from an EMPTY PlacementInfo (PuppetCapturePool::onStageInit
    // substitutes one, because the scene's own is not a stable per-object
    // placement), so "reset to the placement position" means the WORLD ORIGIN.
    // A freshly leased capture therefore appeared at 0,0,0 - which in Cap
    // Kingdom is out past the fence - and stayed there until syncPose() ran
    // later in the frame, or longer if anything delayed it.
    //
    // syncPose() is what the puppet uses every frame anyway. Called here it
    // costs one extra copy and guarantees the first frame the model is visible
    // is already the right one. It reads this puppet's pose from the PREVIOUS
    // frame (control() sets the new one a few lines further down), which is
    // within a frame of correct rather than a whole world away.
    syncPose();
    scheduleActivationCheck(newModelActor, newModel);

    al::ModelKeeper* newKeeper = newModelActor->getModelKeeper();
    MpDebugLog::write("[Puppet %p] changeModel -> %s done newModel=%p newModelCtrl=%p", this, newModel,
        newModelActor, newKeeper ? newKeeper->getModelCtrl() : nullptr);
}

al::LiveActor* PuppetActor::getCurrentModel() {
    return mModelHolder && mModelHolder->mCurrentModel ? mModelHolder->mCurrentModel->actor : nullptr;
}

void PuppetActor::syncPose() {
    // Deliberately NOT skipped while a capture is displayed. The capture model
    // is the current model now, so this poses it exactly the way it poses a
    // body model. The early return that used to be here existed because the
    // capture was posed by hand in control(), which never worked - the pose
    // read back correctly every frame and the model still drew at the origin.
    al::LiveActor* curModel = getCurrentModel();

    if (!curModel)
        return;

    // PlayerModelChangerHakoniwa::syncHost() copies the player's scale before
    // copyPose(). This matters for a late-built capture: its empty placement
    // info is not the player's SRT, and a zero/default scale there makes the
    // otherwise valid capture model completely invisible. Captures use this
    // same current-model slot, so follow the native path rather than only
    // copying translation and rotation.
    if (mIsCaptureModel || !mIs2DModel)
        al::setScale(curModel, al::getScale(this));

    al::copyPose(curModel, this);
}

void PuppetActor::scheduleActivationCheck(al::LiveActor* actor, const char* tag) {
    mActivationCheckActor = actor;
    mActivationCheckTag = tag ? tag : "(unnamed)";
    // changeModel() can run from this actor's own control(), before the draw
    // request phase later in the same native frame.  Two control passes keep
    // the inspection on the far side of both movement and draw request drains.
    mActivationCheckDelay = 2;
    mActivationCheckAttempts = 0;
}

void PuppetActor::updateActivationCheck() {
    if (!mActivationCheckActor)
        return;

    // A newer costume/capture transition supersedes this one.  Every change
    // schedules its own check, so never repair a model that is no longer the
    // holder's selected actor.
    if (mActivationCheckActor != getCurrentModel()) {
        mActivationCheckActor = nullptr;
        mActivationCheckTag.clear();
        return;
    }

    if (mActivationCheckDelay > 0) {
        --mActivationCheckDelay;
        return;
    }

    // Keep draw repair enabled for all three bounded checks. A late actor can
    // cross an older native remove request on the first eligible frame; the
    // drawer's addModelToTable() is idempotent for an already-active model.
    const bool allowDrawRepair = true;
    ++mActivationCheckAttempts;
    if (LateModelDrawer::verifyActivationAndRequestRepair(
            mActivationCheckActor, mActivationCheckTag.cstr(), allowDrawRepair)) {
        mActivationCheckActor = nullptr;
        mActivationCheckTag.clear();
        return;
    }

    constexpr s32 cMaxActivationChecks = 3;
    if (mActivationCheckAttempts < cMaxActivationChecks) {
        mActivationCheckDelay = 2;
        return;
    }

    MpDebugLog::write("[Activation] %s: still incomplete after %d bounded repairs actor=%p",
        mActivationCheckTag.cstr(), mActivationCheckAttempts, mActivationCheckActor);
    mActivationCheckActor = nullptr;
    mActivationCheckTag.clear();
}

void PuppetActor::emitJoinEffect() {
    al::tryDeleteEffect(this, "Disappear"); // remove previous effect (if played previously)

    al::tryEmitEffect(this, "Disappear", nullptr);
}

const char* executorName = "ＮＰＣ";

namespace {

    // PlayerFunction::initMarioModelActor does not use the generic NPC draw
    // registration for its costume parts. It calls this game-private helper,
    // which selects the update/draw lists from the model's shader assignment
    // (the helper is sub_00448B8C in ida_context/Player/PlayerFunction.md).
    // The function is not exported to the mod, so keep the call offset-based
    // like the other fixed-version game helpers in this project.
    constexpr uintptr_t cInitMarioPartExecutorOffset = 0x00448b8c;

    using InitMarioPartExecutorFn = void (*)(al::LiveActor*, const al::ActorInitInfo&,
        bool, bool, bool, bool);

    void initMarioPartExecutor(al::LiveActor* actor, const al::ActorInitInfo& initInfo,
        bool isInvisible) {
        if (!actor)
            return;

        hk::ro::RoModule* mainModule = hk::ro::getMainModule();
        if (!mainModule) {
            // This is only a defensive fallback for an incomplete module
            // list. During normal gameplay the main module is always present;
            // retaining a valid generic registration is safer than leaving a
            // costume part with no executor at all if that invariant changes.
            al::initExecutorUpdate(actor, initInfo, executorName);
            if (!isInvisible)
                al::initExecutorDraw(actor, initInfo, executorName);
            return;
        }

        // Native callers pass four boolean policy flags after ActorInitInfo.
        // For an ordinary remote costume they are all clear; the first flag
        // mirrors the native MarioInvisible check.
        reinterpret_cast<InitMarioPartExecutorFn>(
            mainModule->range().start() + cInitMarioPartExecutorOffset)(
            actor, initInfo, isInvisible, false, false, false);
    }

} // namespace

PlayerCostumeInfo* initMarioModelPuppet(al::LiveActor* player, const al::ActorInitInfo& initInfo,
    const char* bodyName, const char* capName, int subActorNum,
    al::AudioKeeper* audioKeeper, bool lateBuild) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    LateModelDrawer::ScopedLateBuild lateScope(lateBuild);
    if (lateBuild)
        LateModelDrawer::prepareForLateRegistration(initInfo, bodyName);

    // hk::diag::logLine("Loading Resources for Mario Puppet Model.");

    al::ActorResource* modelRes = al::findOrCreateActorResourceWithAnimResource(
        initInfo.actorResourceHolder, al::StringTmp<0x100>("ObjectData/%s", bodyName).cstr(),
        al::StringTmp<0x100>("ObjectData/%s", "PlayerAnimation").cstr(), 0, false);

    if (!modelRes || !modelRes->mModelRes) {
        MpDebugLog::write("[PuppetModel] no model resource for body=%s", bodyName ? bodyName : "(null)");
        return nullptr;
    }

    // hk::diag::logLine("Creating Body Costume Info.");

    PlayerBodyCostumeInfo* bodyInfo = PlayerCostumeFunction::createBodyCostumeInfo(modelRes->mModelRes, bodyName);

    if (!bodyInfo) {
        MpDebugLog::write("[PuppetModel] no body costume info for body=%s", bodyName ? bodyName : "(null)");
        return nullptr;
    }

    // hk::diag::logLine("Initializing Basic Actor Data.");

    al::initActorSceneInfo(player, initInfo);
    al::initActorPoseTQGSV(player);
    // initInfo.placementInfo is now always a real, valid, permanently-empty
    // PlacementInfo (createPuppetActorFromFactory, initHooks.cpp) rather
    // than null or a dangling pointer, so al::initActorSRT is safe to call
    // again here - restored after an earlier, more conservative version of
    // this fix removed the call entirely (back when placementInfo was
    // simply nulled, which fixed the dangling-pointer crash here but broke
    // a DIFFERENT call - PlayerFunction::initCapModelActorDemo,
    // PuppetCapActor.cpp - that dereferences placementInfo unconditionally
    // with no null-check at all). Skipping initActorSRT was never proven
    // necessary on its own merits, just convenient at the time; calling it
    // for real, with a genuinely empty (mData == nullptr) placement iter
    // to read against, lets it run its normal fallback/default path
    // instead of us guessing at which of its side effects (if any) matter
    // for later systems like model drawing.
    al::initActorSRT(player, initInfo);

    al::initActorModelKeeper(player, initInfo, al::StringTmp<0x100>("ObjectData/%s", bodyName).cstr(), 6,
        al::StringTmp<0x100>("ObjectData/%s", "PlayerAnimation").cstr());

    // hk::diag::logLine("Creating Material Category for Player Type");

    al::ModelMaterialCategory::tryCreate(player->mModelKeeper->mModelCtrl, "Player",
        initInfo.actorSceneInfo.graphicsSystemInfo->mMaterialCategoryKeeper);

    // hk::diag::logLine("Initing Skeleton.");

    al::initPartialSklAnim(player, 1, 1, 32);
    al::addPartialSklAnimPartsListRecursive(player, "Spine1", 0);

    // hk::diag::logLine("Setting Up Executor Info.");

    const bool isInvisible = capName && al::isEqualString(capName, "MarioInvisible");
    // Use the same private selector as the native Mario body/part path. The
    // old puppet code unconditionally registered NPC[ForwardOnly] and
    // NPC[Indirect], which is unsafe for a model whose materials have no
    // matching shader assignment: ModelDrawerForward::createTable() then
    // intentionally leaves its resources null, but registration still gave
    // it mModelNum == 1. The next global draw reached prepDraw() with null
    // mDrawDisplayList/mMeshDrawerTable (the exact crash in the supplied
    // register dump). The selector registers only the passes the model can
    // actually use and also preserves the native extra NPC passes.
    initMarioPartExecutor(player, initInfo, isInvisible);

    // hk::diag::logLine("Getting InitEffect Byml from resource.");

    al::ByamlIter iter;
    if (al::tryGetActorInitFileIter(&iter, modelRes->mModelRes, "InitEffect", 0)) {
        const char* effectKeeperName;
        if (iter.tryGetStringByKey(&effectKeeperName, "Name")) {
            // hk::diag::logLine("Initializing Effect Keeper.");

            al::initActorEffectKeeper(player, initInfo, effectKeeperName);
        }
    }

    // hk::diag::logLine("Initing Player Audio.");

    PlayerFunction::initMarioAudio(player, initInfo, modelRes->mModelRes, false, audioKeeper);
    al::initActorActionKeeper(player, modelRes, bodyName, 0);
    al::setMaterialProgrammable(player);

    // hk::diag::logLine("Creating Sub-Actor Keeper.");

    al::SubActorKeeper* actorKeeper = al::SubActorKeeper::tryCreate(player, 0, subActorNum);

    if (actorKeeper) {
        player->initSubActorKeeper(actorKeeper);
        actorKeeper->init(initInfo, 0, subActorNum);
    }

    // hk::diag::logLine("Initializing Sub-Actors.");

    int subModelNum = al::getSubActorNum(player);

    if (subModelNum >= 1) {
        for (int i = 0; i < subModelNum; i++) {
            al::LiveActor* subActor = al::getSubActor(player, i);
            const char* actorName = subActor->getName();

            if (!al::isEqualString(actorName, "シルエットモデル")) {
                initMarioPartExecutor(subActor, initInfo, isInvisible);
                al::setMaterialProgrammable(subActor);
            }
        }
    }

    // hk::diag::logLine("Creating Clipping.");

    if (!lateBuild) {
        al::initActorClipping(player, initInfo);
        al::invalidateClipping(player);
    }

    // hk::diag::logLine("Getting Cap Model/Head Model Name.");

    const char* capModelName;

    if (bodyInfo->isUseHeadSuffix) {
        if (al::isEqualString(bodyInfo->costumeName, capName)) {
            capModelName = "";
        } else {
            capModelName = capName;
        }
    } else {
        capModelName = "";
    }

    const char* headType;

    if (!al::isEqualSubString(capName, "Mario64")) {
        if (bodyInfo->isUseShortHead && al::isEqualString(capName, "MarioPeach")) {
            headType = "Short";
        } else {
            headType = "";
        }
    } else if (al::isEqualString(bodyInfo->costumeName, "Mario64")) {
        headType = "";
    } else if (al::isEqualString(bodyInfo->costumeName, "Mario64Metal")) {
        headType = "Metal";
    } else {
        headType = "Other";
    }

    // hk::diag::logLine("Creating Head Costume Info. Cap Model: %s. Head Type: %s. Cap Name: %s.",
    // capModelName, headType, capName);

    PlayerHeadCostumeInfo* headInfo = initMarioHeadCostumeInfo(player, initInfo, "頭", capName, headType, capModelName);

    // hk::diag::logLine("Creating Costume Info.");

    PlayerCostumeInfo* costumeInfo = new PlayerCostumeInfo();
    costumeInfo->init(bodyInfo, headInfo);

    // PlayerFunction::initMarioModelActor applies these two joint visibility
    // rules after constructing the head parts. The puppet uses a custom model
    // builder, so reproduce them here; otherwise earrings and the head strap
    // retain the archive defaults when body/head costumes are mixed.
    al::LiveActor* faceModel = al::tryGetSubActor(player, "顔");
    if (faceModel && al::isExistJoint(faceModel, "Earrings"))
        al::setJointVisibility(faceModel, "Earrings", costumeInfo->isEnableEarring());

    al::LiveActor* headModel = al::tryGetSubActor(player, "頭");
    if (headModel && costumeInfo->mHeadInfo && costumeInfo->mHeadInfo->isUseStrap && al::isExistJoint(headModel, "Strap")) {
        al::setJointVisibility(headModel, "Strap", !costumeInfo->mBodyInfo->isUseBeard);
    }

    if (costumeInfo->isNeedBodyHair()) {
        hk::diag::logLine("Creating Body Hair Parts Model.");

        al::PartsModel* partsModel = new al::PartsModel("髪");

        partsModel->initPartsFixFile(
            player, initInfo,
            al::StringTmp<0x100>("%sHair%s", bodyName, costumeInfo->isEnableHairNoCap() ? "NoCap" : "")
                .cstr(),
            0, "Hair");

        initMarioPartExecutor(partsModel, initInfo, isInvisible);
        al::setMaterialProgrammable(partsModel);
        partsModel->makeActorDead();
        al::onSyncAppearSubActor(player, partsModel);
        al::onSyncClippingSubActor(player, partsModel);
        al::onSyncAlphaMaskSubActor(player, partsModel);
        al::onSyncHideSubActor(player, partsModel);
    }

    // hk::diag::logLine("Initing Depth Model.");

    if (!lateBuild)
        PlayerFunction::initMarioDepthModel(player, false, false);

    // hk::diag::logLine("Creating Retarget Info.");

    rs::createPlayerSklRetargettingInfo(player, sead::Vector3f::ones);

    // hk::diag::logLine("Making Player Model Dead.");

    player->makeActorDead();
    return costumeInfo;
}

PlayerHeadCostumeInfo* initMarioHeadCostumeInfo(al::LiveActor* player, const al::ActorInitInfo& initInfo,
    const char* headModelName, const char* capModelName,
    const char* headType, const char* headSuffix) {
    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

    al::PartsModel* headModel = new al::PartsModel(headModelName);

    al::StringTmp<0x80> headArcName("%sHead%s", capModelName, headType);
    al::StringTmp<0x100> arcSuffix("Head");
    if (headSuffix)
        arcSuffix.format("Head%s", headSuffix);

    headModel->initPartsFixFile(player, initInfo, headArcName.cstr(), 0, arcSuffix.cstr());
    al::setMaterialProgrammable(headModel);

    const bool isInvisible = capModelName && al::isEqualString(capModelName, "MarioInvisible");
    initMarioPartExecutor(headModel, initInfo, isInvisible);

    headModel->makeActorDead();

    al::onSyncAppearSubActor(player, headModel);
    al::onSyncClippingSubActor(player, headModel);
    al::onSyncAlphaMaskSubActor(player, headModel);
    al::onSyncHideSubActor(player, headModel);

    al::PartsModel* capEyesModel = new al::PartsModel("キャップの目");
    capEyesModel->initPartsFixFile(headModel, initInfo, "CapManHeroEyes", "", 0);

    al::onSyncClippingSubActor(headModel, capEyesModel);
    al::onSyncAlphaMaskSubActor(headModel, capEyesModel);
    al::onSyncHideSubActor(headModel, capEyesModel);

    al::setMaterialProgrammable(headModel);
    headModel->makeActorDead();

    return PlayerCostumeFunction::createHeadCostumeInfo(al::getModelResource(headModel), capModelName, false);
}
