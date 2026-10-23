#pragma once

#include "sead/basis/seadTypes.h"

namespace al {
    class LiveActor;
    struct ActorInitInfo;
}

// Repairs the draw tables of models built after the stage's placement sweep.
//
// al::initExecutorDraw() creates a fresh al::ModelDrawerBase per registered
// actor model, but the table that drawer actually stores models into
// (ModelDrawerBase::mModels, allocated by its createTable()) is only ever
// filled in by al::ExecutorListActorModelDrawBase::createList(), which the
// scene runs exactly once, at init, after placement finishes. Anything the mod
// builds during gameplay therefore ends up with a drawer whose mModels is
// still nullptr, and the first frame the model is made alive the draw pipeline
// does mModels[mModelNum] = model - a store to address 0x0.
//
// That is the crash memory.txt records against al::ModelDrawerBase::
// addModelToTable, and it is one shared root cause for both remaining
// features: the live costume rebuild (PuppetActor::tryRefreshCostume) and the
// capture models (PuppetHackActor::init) build models the same way, long after
// createList() has run.
//
// createTable() is a virtual, so this is dispatched through the object's own
// vtable and needs no linker symbol - which matters here, because the direct
// symbols for most of al's resource/model plumbing are not exported to this
// mod (four separate attempts this session died at ld.lld undefined symbol).
namespace LateModelDrawer {

    // Installs the defensive guard for the native ModelDrawerForward::draw
    // entry point. Forward-only drawers have a legitimate no-op form in
    // which createTable() leaves the mesh/display resources null; the native
    // draw function assumes its caller never puts a model in that drawer.
    // Late registration can temporarily violate that invariant, so keep the
    // global draw boundary safe as well as repairing individual actors.
    void installHooks();

    // A late model is allowed to use the normal model/executor plumbing, but
    // it must not add itself to the scene's fixed-size clipping and depth
    // shadow registries. The scope is deliberately nestable: a costume build
    // can call helpers which build more actors of their own.
    class ScopedLateBuild {
    public:
        explicit ScopedLateBuild(bool active = true);
        ~ScopedLateBuild();

        ScopedLateBuild(const ScopedLateBuild&) = delete;
        ScopedLateBuild& operator=(const ScopedLateBuild&) = delete;

    private:
        bool mActive;
    };

    bool isLateBuildActive();

    // PlayerFunction::initMarioModelActor is the canonical constructor for a
    // Mario costume. PuppetActor calls it too, but that call must not be
    // mistaken for the local player's costume and sent back over the wire.
    // Keep this scope separate from ScopedLateBuild because the initial
    // puppet costume is built during normal stage initialization.
    class ScopedPuppetCostumeBuild {
    public:
        ScopedPuppetCostumeBuild();
        ~ScopedPuppetCostumeBuild();

        ScopedPuppetCostumeBuild(const ScopedPuppetCostumeBuild&) = delete;
        ScopedPuppetCostumeBuild& operator=(const ScopedPuppetCostumeBuild&) = delete;
    };

    bool isPuppetCostumeBuildActive();

    // Grows the already-created movement and model-draw tables before a late
    // actor registers with them. IDA shows that the game allocates these
    // arrays during scene setup and appends without a bounds check. This is
    // needed even when createMissingTables() has repaired a newly-created
    // drawer: a late actor normally reuses the existing executor/drawer by
    // name, whose original allocation would otherwise still be full.
    void prepareForLateRegistration(const al::ActorInitInfo& initInfo, const char* tag);

    // The growth side tables contain raw scene-heap addresses.  Clear them at
    // every new stage so address reuse cannot make a new native allocation
    // inherit the previous scene's larger recorded capacity.
    void resetTrackedBuffers();

    // Walks `actor` and its sub-actors, and for every drawer that never got a
    // table, creates one. Legitimate no-display passes are retained. A
    // resource-less no-op is detached from this actor's private list while the
    // shared/global executor list is preserved. Safe to call on an actor built
    // at scene init (every drawer already has its table, so it does nothing),
    // and safe with nullptr.
    //
    // Must be called after the model is built and BEFORE it is ever made
    // alive - the crash happens on the first draw request, so there is no
    // second chance. `tag` only labels the mp debug log lines.
    void createMissingTables(const al::ActorInitInfo& initInfo, al::LiveActor* actor,
        const char* tag);

    // Dumps every movement executor an actor is registered with: its count,
    // capacity, buffer, and the contents of each slot. Called the moment a
    // pooled capture model is woken, because that is the transition where
    // al::ExecutorActorExecuteBase::addActor is supposed to put the actor into
    // the buffer createMissingTables built - and a crash report alone cannot
    // say whether it did.
    void logExecutorState(al::LiveActor* actor, const char* tag);

    // True when `actor` sits in the active list of at least one of the
    // movement executors it registered with - i.e. the engine is calling its
    // movement() every frame.
    //
    // This matters because movement() is the ONLY thing that advances a
    // model's animation: al::LiveActor::movement calls ActorActionKeeper::
    // updatePrev, ModelKeeper::update, ActorActionKeeper::updatePost and
    // ModelKeeper::updateLast. calcAnim(), which the draw side calls, only
    // EVALUATES the animation at its current frame and builds the matrix. An
    // actor that is drawn but not moved therefore renders perfectly, in the
    // right place, frozen on frame 0 - and every al::startAction /
    // al::startVisAnim aimed at it is queued and never committed.
    //
    // False means either no update executor was registered at all (a
    // registration whose list name matched nothing leaves mUpdaterCount at 0
    // and fails silently) or the wake request never reached one.
    bool isDrivenByMovementExecutor(al::LiveActor* actor);

    // Called a couple of frames after an actor is woken.  Verifies that every
    // movement updater and visible model drawer actually contains the actor,
    // logs its pose/flags, re-queues bounded movement requests, and (when
    // allowed) invokes each missing drawer's idempotent specialized add path.
    // Returns true once the actor and its alive sub-actors are fully active.
    bool verifyActivationAndRequestRepair(al::LiveActor* actor, const char* tag,
        bool allowDrawRepair);

    // Capture actors use the engine's ordinary alive/dead registration path.
    // prepareForLateRegistration() grows shared executor lists and the
    // tag-matched model-draw tables before a late actor can register, so no
    // private slot manipulation is needed when a capture is leased.

    // Building a model registers it with al::DepthShadowMapDirector, whose
    // storage is another declare-once/allocate-once structure finalised during
    // the scene's init-after-placement work (declareUseDepthShadowMap is
    // literally "_68 += num" in the decomp header). A model built during
    // gameplay therefore pushes the director's counters past what it actually
    // allocated, and the frame that model is first drawn the director walks
    // its list and dereferences a null ShadowInfo - crash reports 01787918155
    // and 01787919304, identical register-for-register.
    //
    // The counters are plain public s32s reached entirely through inline
    // accessors (ActorSceneInfo -> GraphicsSystemInfo::getShadowDirector ->
    // ShadowDirector::getDepthShadowMapDirector), so no linker symbol is
    // involved. Snapshot them around a late build and put them back: the
    // entries our model added stay in memory but sit past the counts, so the
    // director never walks them. The puppet loses its depth shadow, which is
    // the same price createMissingTables already pays, and nothing else about
    // the model changes.
    struct DepthShadowCounts {
        s32 declared = -1;
        bool valid = false;
    };

    // Both are no-ops (and log nothing) if the director cannot be reached.
    DepthShadowCounts snapshotDepthShadow(const al::ActorInitInfo& initInfo);
    void restoreDepthShadow(const al::ActorInitInfo& initInfo, const DepthShadowCounts& saved,
        const char* tag);

} // namespace LateModelDrawer
