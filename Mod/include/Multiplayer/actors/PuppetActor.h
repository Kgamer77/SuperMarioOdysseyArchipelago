#pragma once

#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Play/Layout/BalloonMessage.h"

#include "game/Player/PlayerCostumeInfo.h"
#include "game/Player/PlayerModelHolder.h"

#include "Multiplayer/actors/PuppetCapActor.h"
#include "Multiplayer/actors/PuppetHackActor.h"
#include "Multiplayer/algorithms/CaptureTypes.h"
#include "Multiplayer/layouts/NameTag.h"

#include "Multiplayer/puppets/PuppetInfo.h"

class PuppetActor : public al::LiveActor {
public:
    // Model-holder budget: two entries for the base costume (Normal /
    // Normal2D) and two more per costume rebuild.
    //
    // Captures ARE in here now, one slot per capture type this puppet rides.
    // An earlier design kept them out because PlayerModelHolder cannot
    // unregister, so an entry is held for the life of the stage - but that
    // reasoning was about OWNING a model. The actors are still owned and shared
    // by PuppetCapturePool; a holder entry is only a name and a pointer, so
    // there is no extra model memory. What it buys is that a capture is
    // displayed exactly like a costume - selected with changeModel(), posed by
    // syncPose(), animated by startAction() - instead of being hand-driven,
    // which is what left it rendering at the world origin.
    //
    // Past kCaptureSlots a puppet simply keeps its own body for the extra
    // types, the same outcome as a capture the pool never built.
    static constexpr s32 kCostumeGenerations = 3;
    // One holder slot per distinct capture ARCHIVE this puppet displays over a
    // stage, not per capture name: the seven pieces of a Fukuwarai face and the
    // five anagram letters are separate models under one name, and a player
    // works through all of them. Matched to PuppetCapturePool::kMaxActors so
    // the puppet is never the tighter of the two ceilings; a slot is eight
    // bytes until something is registered in it.
    static constexpr s32 kCaptureSlots = 12;
    static constexpr s32 kModelSlots = 2 * kCostumeGenerations + kCaptureSlots;

    PuppetActor(const char* name);
    ~PuppetActor();

    virtual void init(const al::ActorInitInfo&) override;
    virtual void control(void) override;
    virtual void makeActorAlive(void) override;
    virtual void makeActorDead(void) override;

    virtual void attackSensor(al::HitSensor*, al::HitSensor*) override;
    virtual bool receiveMsg(const al::SensorMsg*, al::HitSensor*, al::HitSensor*) override;

    virtual const char* getName() const override {
        if (mPupInfo)
            return mPupInfo->puppetName.cstr();
        return "";
    }

    void initOnline(PuppetInfo* pupInfo);

    void startAction(const char* actName);
    void hairControl();

    void setBlendWeight(int index, float weight);

    bool isNeedBlending();

    PuppetInfo* getInfo() { return mPupInfo; }

    al::LiveActor* getCurrentModel();

    // True while a leased capture model is standing in for the body.
    bool isCaptureModel() const { return mIsCaptureModel; }

    void emitJoinEffect();

    bool mIsDebug = false;

private:
    void changeModel(const char* newModel);

    void syncPose();

    // Starts (and, when the enemy's own state machine would have, restarts)
    // the action a leased capture is playing.
    // `remoteFrame` is the sender's own phase for this action (PuppetInfo::
    // captureAnimFrame, negative when the peer does not report one). It is what
    // separates an action the sender is genuinely looping from one it has
    // played once and is holding - see the body.
    void startCaptureAction(al::LiveActor* capture, const char* actName, f32 remoteFrame);

    // Puts a leased capture's animation on the same frame the sender's is on,
    // so everything drawn from one packet agrees with everything else in it.
    void syncCaptureAnimFrame(al::LiveActor* capture, f32 remoteFrame);

    // Keeps a leased capture's animation running. See the definition - the
    // decision is made by measuring the animation frame, not by asking the
    // executor tables who owns this actor.
    void updateCaptureMovement();
    void resetCaptureMovementState();

    void scheduleActivationCheck(al::LiveActor* actor, const char* tag);
    void updateActivationCheck();

    // Builds one Normal/Normal2D pair under the given holder names and returns
    // its costume info. Shared by init() and the rebuild below so a swapped
    // costume goes through exactly the same construction as the first one.
    PlayerCostumeInfo* buildCostumeModels(const al::ActorInitInfo& initInfo, const char* bodyName,
        const char* capName, const char* normalName, const char* normal2DName, bool lateBuild = false);

    // A costume is baked into a model at construction, and puppets are
    // constructed at stage init - so a costume that arrives afterwards (which
    // is every costume from a player who joins while you are already in a
    // stage) never showed. This rebuilds the pair when PuppetInfo's costume
    // changes.
    // Returns true when this call performed a late native model build (even
    // if that build failed), so the thrown cap can be built on another frame.
    bool tryRefreshCostume(PuppetInfo* frameInfo);

    // The separately-thrown Cappy model also bakes its costume at init.  It
    // has an independent generation/memo because a successful body rebuild
    // must not mask a failed cap replacement.
    bool tryRefreshThrownCap(PuppetInfo* frameInfo);

    // Applies PuppetInfo's capture state by leasing (or handing back) a model
    // from PuppetCapturePool.
    void tryRefreshCapture(PuppetInfo* frameInfo);

    // Hands the lease back and brings the ordinary body out of hiding. Safe to
    // call when nothing is leased.
    void releaseCapture();

    void tryRefreshRide(PuppetInfo* frameInfo);
    void updateRideAction(PuppetInfo* frameInfo);
    void releaseRide();

    PlayerCostumeInfo* mCostumeInfo = nullptr;
    // Every model pair owns a small PlayerCostumeInfo used while its actors
    // are being built. Keep all generations alive until the scene tears down;
    // old pairs remain registered in PlayerModelHolder and freeing only the
    // current info leaves the ownership ambiguous.
    PlayerCostumeInfo* mCostumeInfos[kCostumeGenerations] = {};
    PuppetInfo* mPupInfo = nullptr;
    PuppetCapActor* mPuppetCap = nullptr;
    // The thrown cap model is costume-specific too.  As with body costume
    // generations, old actors remain registered (dead) until scene teardown;
    // replacing one in-place would reinitialize live model/executor keepers.
    PuppetCapActor* mPuppetCaps[kCostumeGenerations] = {};
    PuppetCapActor* mPendingPuppetCap = nullptr;
    PlayerModelHolder* mModelHolder = nullptr;
    NameTag* mNameTag = nullptr;

    // Copy, not a reference: every member of ActorInitInfo is a pointer to
    // something that lives as long as the scene (and so as long as this
    // puppet), which is what makes retaining it safe. Needed because model
    // construction requires one and a costume swap happens from control(),
    // which has none.
    al::ActorInitInfo mInitInfo;

    // Holder names of the pair currently in use, and how many rebuilds have
    // happened. Old pairs stay registered and dead rather than being freed -
    // the model actors may still be referenced by the costume info that built
    // them - so the generation cap is what bounds that.
    sead::FixedSafeString<0x20> mNormalModelName { "Normal" };
    sead::FixedSafeString<0x20> mNormal2DModelName { "Normal2D" };
    sead::FixedSafeString<0x20> mBuiltBodyName;
    sead::FixedSafeString<0x20> mBuiltCapName;
    sead::FixedSafeString<0x20> mBuiltThrownCapName;
    sead::FixedSafeString<0x20> mPendingThrownCapName;
    sead::FixedSafeString<0x20> mThrownCapLimitName;
    // Memoizes the last request refused solely because the bounded holder has
    // no more costume generations. This prevents a rejected outfit from
    // emitting a log line and retrying an allocation on every frame while
    // still allowing a different request to be considered.
    sead::FixedSafeString<0x20> mCostumeLimitBodyName;
    sead::FixedSafeString<0x20> mCostumeLimitCapName;
    s32 mCostumeGeneration = 0;
    s32 mThrownCapGeneration = 0;
    // Set when tryRefreshCostume() has just built a new costume pair and put
    // it to sleep - control() applies the actual visible swap on the NEXT
    // frame, never the one that built it. See tryRefreshCostume()'s comment
    // for why this specific separation is what's believed safe.
    bool mPendingCostumeApply = false;

    // The model currently on loan from PuppetCapturePool, and the curHack value
    // that was last acted on.
    //
    // The second one is a memo, not a duplicate of the first: only a request
    // that was successfully applied is retained. Transient pool/holder
    // failures remain retryable on a later update.
    PuppetHackActor* mCaptureActor = nullptr;
    sead::FixedSafeString<0x40> mRequestedCapture;
    // ...and which member of that hack's archive family was leased, because a
    // player sliding from the M to the A is a change of MODEL with no change of
    // name. Part of the same memo: both have to match for the lease to stand.
    s32 mRequestedCaptureVariant = -1;

    // A ride is a SECOND leased model, not a replacement for the body: the
    // player sits on Jaxi or the Scooter rather than becoming it, so both
    // have to be drawn. It is deliberately kept out of the PlayerModelHolder
    // for that reason - the holder selects exactly one model - and is posed
    // by hand from the same packet transform, the way PuppetCapActor is.
    PuppetHackActor* mRideActor = nullptr;
    sead::FixedSafeString<0x40> mRequestedRide;

    // Motion state for the ride's idle/moving action. Held for a few frames
    // rather than read per frame: packets arrive slower than the frame rate, so
    // the position is unchanged on most frames and a naive test would flicker
    // the vehicle between Wait and Run.
    sead::Vector3f mLastRidePos = sead::Vector3f::zero;
    bool mHasLastRidePos = false;
    s32 mRideMovingFrames = 0;
    // Last capture request written to the log. Purely a de-duplication key so
    // an un-servable capture does not log every frame; it has no effect on
    // whether the request itself is retried.
    sead::FixedSafeString<0x40> mLoggedCaptureRequest;

    bool mIs2DModel = false;

    bool mIsCaptureModel = false;

    // Capture animation drive state - see updateCaptureMovement(). The frame is
    // the previous pass's skeletal animation frame, the counter is how long it
    // has sat still, and the probe is the periodic frame on which this puppet
    // deliberately does nothing so it can tell whether the engine took over.
    f32 mCaptureAnimFrame = -1.0f;
    // Last phase received from the sender and applied, so a value that has not
    // changed is understood as "no new packet" rather than re-applied every
    // frame on top of the local advance.
    f32 mAppliedCaptureFrame = -1.0f;
    // The peer's previous phase, kept so a genuine restart on the SENDER can be
    // told apart from our own copy simply running out of animation. See
    // startCaptureAction().
    f32 mLastRemoteCaptureFrame = -1.0f;
    // True for the one pass in which a phase correction was applied, so the
    // freeze detector does not mistake this puppet's own write for the
    // animation running.
    bool mCaptureFrameSynced = false;
    s32 mCaptureAnimStallFrames = 0;
    s32 mCaptureMovementProbeTimer = 0;
    bool mCaptureMovementProbe = false;
    bool mCaptureMovementTakeover = false;
    bool mCaptureDriveDiagLogged = false;

    // A wake/swap is applied through the engine's deferred request tables.
    // Inspect it only after later native frames have had a chance to drain
    // those tables, with bounded retries if a request was missed.
    al::LiveActor* mActivationCheckActor = nullptr;
    sead::FixedSafeString<0x40> mActivationCheckTag;
    s32 mActivationCheckDelay = 0;
    s32 mActivationCheckAttempts = 0;

    float mClosingSpeed = 0;
};

PlayerCostumeInfo* initMarioModelPuppet(al::LiveActor* player, const al::ActorInitInfo& initInfo,
    const char* bodyName, const char* capName, int subActorNum,
    al::AudioKeeper* audioKeeper, bool lateBuild = false);
PlayerHeadCostumeInfo* initMarioHeadCostumeInfo(al::LiveActor* player, const al::ActorInitInfo& initInfo,
    const char* headModelName, const char* capModelName,
    const char* headType, const char* headSuffix);
