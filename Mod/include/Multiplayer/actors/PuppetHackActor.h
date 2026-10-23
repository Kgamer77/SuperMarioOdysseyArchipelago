#pragma once

#include "sead/math/seadMatrix.h"
#include "sead/math/seadQuat.h"
#include "sead/math/seadVector.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/LiveActor/LiveActor.h"

#include "Multiplayer/puppets/LateModelDrawer.h"
#include "Multiplayer/puppets/PuppetInfo.h"

// One capture model, leased out by PuppetCapturePool.
//
// Deliberately NOT one-per-puppet-per-capture-type, which is what the original
// design called for and what made capture sync unaffordable. The actor is bound
// to a single archive at init - which is why it can never change type - but it
// is NOT bound to a player: a puppet borrows one for as long as it is riding
// that capture and hands it back on release. A stage therefore pays for the
// peak number of simultaneous riders of a type, not for every puppet slot times
// every capture the stage happens to place.
//
// It is never registered as a placement actor, so nothing in the scene drives
// its game-specific behavior. Its generic movement/draw executors are still
// registered so LiveActor can maintain the model, while the leasing
// PuppetActor supplies the capture action and pose.
class PuppetHackActor : public al::LiveActor {
public:
    PuppetHackActor(const char* name);
    void makeActorAlive() override;
    virtual void init(const al::ActorInitInfo&) override;
    void initYoshi(const al::ActorInitInfo&);
    void finishInit(const al::ActorInitInfo&, bool isEarly, const LateModelDrawer::DepthShadowCounts& shadowCounts);

    // hackType is the sync key (what PuppetInfo::curHack carries); archiveName
    // is the object archive the model is built from. They differ wherever
    // classHackNames maps a class onto another capture's name - a Goomba syncs
    // as "Kuribo" but the stage placed "KuriboPossessed", and only the placed
    // archive is guaranteed to be resident.
    //
    // Must be called before init(): init() reads both.
    void initOnline(PuppetInfo* info, const char* hackType, const char* archiveName, const char* suffix = nullptr);

    void startAction(sead::SafeString actName);

    // Mario's hat and eyes on the captured enemy. Every capturable archive
    // carries HackOn/HackOff action and visibility animations, but the
    // existence checks keep reduced archives valid too.
    void startHackAnim(bool isOn);

    // Legacy manual-update entry point. Pooled captures are registered in the
    // ordinary movement/draw executors and the leasing PuppetActor poses and
    // animates them through its PlayerModelHolder, so this is intentionally
    // unused in the normal path. It remains available as a diagnostic
    // fallback if a model ever renders in place but freezes.
    void update();

    // A pooled actor outlives any one lease, so the puppet it speaks for is
    // rebound on every acquire and cleared on release.
    void setInfo(PuppetInfo* info) { mInfo = info; }

    // Read back by the capture adapters, which need the synced state of
    // whichever puppet currently holds this lease. Null while the actor
    // is idle in the pool, so every caller must check.
    PuppetInfo* getInfo() const { return mInfo; }

    // False when init() bailed because the archive had no model behind it -
    // the capture key and the archive name are allowed to differ, and not
    // every name that reaches us has a real .szs. Such an actor is inert
    // (init() stops before any executor registration) and must never be
    // pooled.
    bool hasModel() const { return mHasModel; }

    const char* getHackType() const { return mHackType.cstr(); }
    bool isHackType(const char* hackType) const {
        return hackType && mHackType.isEqual(hackType);
    }

    // The hack type is not enough to identify a pooled model any more. Several
    // captures are one hack name over a family of archives - the anagram
    // letters, the Fukuwarai face pieces, the two New Donk manholes - and the
    // pool now keeps one model per archive so a puppet can be handed the exact
    // piece its player is riding. See CaptureSync::Entry::variants.
    const char* getArchiveName() const { return mArchiveName.cstr(); }
    bool isArchive(const char* archiveName) const {
        return archiveName && mArchiveName.isEqual(archiveName);
    }

    // Mirror a capture's action onto its own sub-actors.
    //
    // Several capture archives are two models that have to be posed together
    // and are animated by two separate startAction calls in the real actor's
    // own code. The clearest is the fishing Lakitu: every JugemFishing nerve
    // does
    //
    //     al::StringTmp<64> name("Hack%s", "Move");
    //     al::startAction(this, name);
    //     al::startAction(al::getSubActor(this, <name>), "Move");
    //
    // (ida_context/Enemy/JugemFishing.md, exeHackMove 0x0013a470 and its
    // dozen neighbours) - the parent gets "Hack<X>" while the sub-actor gets
    // the bare "<X>", and in the non-hack states both get the same name. The
    // sub-actor's own name is an unresolved string constant in the dump, but it
    // is the only one JugemFishing ever addresses and the symptom says which
    // half of the model it is: a pooled Lakitu was only ever given the parent's
    // action, so its other half stayed in its bind pose and Lakitu sat inside
    // the cloud instead of on it.
    //
    // Both spellings are tried and both are guarded by al::isExistAction, so an
    // archive whose sub-actors have no such action is untouched.
    void startActionOnSubActors(const char* actName);

private:
    PuppetInfo* mInfo = nullptr;
    sead::FixedSafeString<0x20> mHackType;
    sead::FixedSafeString<0x20> mArchiveName;
    const char* mSuffix = nullptr;
    bool mHasModel = false;

public:
    float mExtra[4] = {};

    // The same channel as mExtra, for the captures whose extra state is a
    // rotation rather than four independent scalars - today the Shiverian
    // Racer's rolling ball. A joint controller keeps the ADDRESS and reads it
    // every frame, so it has to be a field of the actor and not a temporary,
    // exactly as Yukimaru::init binds its own +0x148 quaternion.
    sead::Quatf mExtraQuat = sead::Quatf::unit;

    // Same channel again, for the captures whose extra state is a direction
    // rather than a rotation - today the fork/signpost bend axis. Bound by
    // address to five al::JointLocalAxisRotators, so it is a field for the
    // same reason mExtraQuat is.
    sead::Vector3f mExtraVec = sead::Vector3f::ex;

    // ...and for the one whose joint is driven by a whole matrix: Fukankun
    // binds its "Head" joint to a sead::Matrix34f it owns and rotates that
    // matrix in place. Left uninitialised here on purpose - an adapter's
    // setupModel() is the only thing that binds it, and it seeds it to the
    // identity in the same breath.
    sead::Matrix34f mExtraMtx;

    // Joint controllers an adapter created and has to keep writing into.
    // al::JointLocalAxisRotator copies its axis at construction instead of
    // reading the bound reference, exactly as vanilla does, so the pointers
    // have to survive - see AlUnlinked::setAxisRotatorAxis.
    static constexpr s32 kMaxAdapterCtrls = 8;
    void* mAdapterCtrls[kMaxAdapterCtrls] = {};
    s32 mAdapterCtrlCount = 0;

    // The first sub-actor the capture's adapter built, remembered rather than
    // looked up. Every name-based sub-actor lookup in this engine goes through
    // alSubActorFunction::findSubActor, which calls getName() on each entry and
    // compares it - the exact search that faulted on an archive whose own
    // sub-actor has a null name (see finishInit). Holding the pointer we
    // created ourselves avoids the search entirely.
    al::LiveActor* mAdapterSubActor = nullptr;
};
