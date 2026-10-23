#pragma once

#include "sead/basis/seadTypes.h"

namespace al {
    struct ActorInitInfo;
}

class PuppetHackActor;
struct PuppetInfo;

// Scene-scoped pool of capture models shared by every puppet.
//
// Builds a model for a capture type the moment the REAL placed enemy of
// that class is observed finishing construction (notifyRealActorCreated(),
// fed from the mpOnPlacementActorCreated sync bridge) - not lazily on first
// use, and not on any timer/heuristic. Two earlier mechanisms crashed the
// game trying to get this timing right (full detail in memory.txt):
//   1. Lazily, on first acquire() of a type nothing had leased yet (the
//      original design) - crashed inside al::ModelDrawerBase::
//      addModelToTable / al::ExecutorActorExecuteBase::addActor: a model
//      built after the stage's placement sweep finishes is never
//      accounted for in whatever fixed-size table the engine's per-frame
//      movement/draw sweeps use.
//   2. Eagerly, via a trampoline on al::SceneObjHolder::
//      initAfterPlacementSceneObj - crashed on EVERY boot: that exact
//      symbol isn't in this game's hook-target symbol database, so the
//      runtime lookup inside installAtSym itself aborted, before any
//      gameplay ever ran.
//   3. Eagerly, via a frame-delay heuristic (60 frames after
//      onStageInit()) - crashed inside al::Resource::tryGetByml:
//      registerPlacement() only means "this stage places this class", not
//      "its archive is loaded into memory yet" - the heuristic fired
//      before the real resource was resident.
//   4. notifyRealActorCreated() calling buildOne() directly, synchronously,
//      the moment a real actor of the target class was observed - crashed
//      inside al::Resource::tryGetByml AGAIN, but for a different reason
//      than #3: the residency theory was actually right (confirmed by a
//      real crash log this time showing the crash on a background "Scene
//      Initialization Thread", not MainThread), but calling buildOne() -
//      itself a resource load + actor construction - reentrantly, from
//      INSIDE the engine's own placement-sweep/resource-loading callback
//      chain on that same background thread, isn't safe regardless of
//      residency.
// The fix: notifyRealActorCreated() only marks a type pending now: it never
// calls buildOne() itself. The build happens later, and WHERE it happens is
// the whole ballgame:
//
//   - Ordinarily, from a trampoline on al::LiveActorKit::endInit (see
//     installHooks()). That function runs once the placement sweep has
//     finished and, at the very end of itself, calls
//     GraphicsSystemInfo::initAfterPlacement and
//     ExecuteDirector::createExecutorListTable - the two passes that size the
//     depth shadow director and every movement/draw table in the scene.
//     Building on entry to it means our models are registered BEFORE that
//     sizing and are therefore counted by it, exactly like a placed actor's.
//     This is what fixes #1 properly rather than repairing around it:
//     LateModelDrawer exists because a model built after that line is in no
//     table, and a model built before it is in all of them.
//
//   - As a fallback, from update() (Client::update(), main thread) for a type
//     whose real enemy only shows up after scene init - a sub-area load, say.
//     Those are still late builds and still need LateModelDrawer.
//
// Both keep #3's fix (only build once a real actor has proven the archive
// resident) and #4's (never build from the background placement callback the
// observation happened on - endInit is past the sweep, not inside it). And
// neither can repeat #1's mistake, because buildOne() puts the new model
// straight to sleep (makeActorDead()) instead of ever making it alive the
// same frame it was built - it's only ever woken later, on a different
// frame, through acquire()'s reuse path below.
//
// Residency is the reason registerPlacement() exists. A capture model may only
// be built from an archive the stage has already loaded; al::findResource is
// not among the symbols available to link against, so instead the placement
// hook tells us which capture classes this stage actually places, and only
// those can ever be leased. Anything else falls back to the puppet's own body,
// which PuppetActor already handles.
namespace PuppetCapturePool {

    // Distinct capture classes tracked per stage. The busiest vanilla stages are
    // comfortably under this; past it the extra types simply do not get models.
    inline constexpr s32 kMaxTypes = 32;

    // Ceiling on capture models built at once across all types. Past it, the
    // extra types simply fall back to the puppet's own body.
    inline constexpr s32 kMaxActors = 12;

    // Stage lifecycle. onStageInit takes the copy of ActorInitInfo that later
    // creations are built from; onStageKill drops every pointer WITHOUT deleting,
    // because the actors live on the scene heap that is being torn down.
    void onStageInit(const al::ActorInitInfo& initInfo);
    void onStageKill();

    // Fed by the mpOnPlacementActorCreated sync bridge for EVERY placed actor
    // (cheap rejection via CaptureTypes::FindType for non-capturable ones).
    // Called from wherever that bridge itself runs - NOT necessarily the main thread
    // (see the top-of-file comment) - so this only marks the type pending;
    // it never builds anything itself.
    void onPlacementActorCreated(const char* className, const char* placementModelName, const char* actorModelName);

    // Installs the scene-init build hook. Call once at boot, next to
    // LateModelDrawer::installHooks().
    void installHooks();

    // Call once per frame (from Client::update(), main thread) while a
    // stage is loaded. Builds a model for any type notifyRealActorCreated()
    // marked pending - see the top-of-file comment for why the build can't
    // happen on notifyRealActorCreated()'s own calling thread.
    //
    // With the endInit hook installed this is now only the FALLBACK path,
    // for a type whose real actor appears after the scene finished
    // initialising (a sub-area load, say). Those builds are still late and
    // still go through LateModelDrawer; the ordinary case does not.
    void update();

    // True only while the endInit drain below is constructing a model, i.e.
    // while the build is happening inside the scene's own initialisation and
    // none of LateModelDrawer's repairs apply. Read by PuppetHackActor::init
    // to choose between the two construction paths. Only ever true on the
    // thread doing that build, inside the pool lock.
    bool isEarlyBuildPhase();

    // Borrow a model for `hackType` (a PuppetInfo::curHack value) on behalf
    // of `info`. Returns nullptr if no model has been built for this type
    // yet (its real enemy hasn't been seen this stage), every built model of
    // this type is already leased, or the stage isn't ready - the caller
    // treats a null return as "show the ordinary body".
    //
    // `variant` is PuppetInfo::captureVariant: an index into the hack's
    // CaptureSync::Entry::variants family, or -1 for the captures (nearly all
    // of them) that are one hack name over one archive. It is a REQUEST, not a
    // requirement - a variant whose archive this stage has not loaded, or that
    // there is no room left to build, falls back to whatever model the type
    // already has, which is the behaviour every caller had before variants
    // existed.
    PuppetHackActor* acquire(const char* hackType, s32 variant, PuppetInfo* info);

    // Hand a model back. Safe with nullptr and with an actor from a dead stage.
    void release(PuppetHackActor* actor);

    // Diagnostics for the mp debug log.
    s32 getLiveCount();
    s32 getLeasedCount();
    s32 getTypeCount();

} // namespace PuppetCapturePool
