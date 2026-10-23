#pragma once

#include <basis/seadTypes.h>

namespace al {
    class IUseSceneObjHolder;
}

// Gameplay side of the Movement Abilities page: the hooks that actually withhold
// Moon Kingdom's low gravity, swimming, carrying, Jaxi, the Motor Scooter, the
// Rocket Flower, talking to NPCs, the pipes into 2D and the timed challenges.
// Which of them are withheld is decided by AbilityRando (see isAbilityUnlocked);
// nothing here consults the settings directly beyond that.
namespace MovementAbilityLock {

    void installHooks();

    // Cappy's refusal lines for the locks that speak up, routed through the shared
    // getSystemMessageString override in ProgressionHooks.
    const char16_t* tryMakeCapMessage(const char* fileName, const char* key);

    // Per-frame from StageScene: shows a queued refusal line and drives the
    // "spawned underwater without Swim" retreat.
    void update(const al::IUseSceneObjHolder* holder);

    // Restarts the arrival window. update() calls this itself the first frame it
    // sees a new current-stage name, which is exactly when a load has finished;
    // it is exposed for anything that relocates the player without one.
    void notifyStageEntered();

    // Records where an underwater arrival should be pushed back to: the stage
    // being left and the door to come back out of. Called by EntranceHooks as it
    // routes a transition, because that is the only place that knows which side
    // of a shuffled door pair the player used. Passing nullptr/empty clears it,
    // which downgrades an underwater arrival to the ordinary safety-point bubble.
    void noteRetreatTarget(
        const char* stage, const char* entranceId, const char* destStage);

    // True between the bubble starting and the retreat transition firing. The
    // arrival itself still counts as a visit - the player really did get there,
    // and the door that took them is still there - so nothing suppresses the
    // arrival log; this is for callers that want to know a retreat is in flight.
    bool isUnderwaterRetreatActive();

} // namespace MovementAbilityLock
