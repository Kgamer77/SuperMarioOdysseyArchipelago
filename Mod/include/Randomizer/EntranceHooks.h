#pragma once

namespace al {
    class ActorInitInfo;
}

namespace EntranceHooks {

    void bypassNextTransition();
    void resetBypass();
    // Subarea Keys: counts down the window during which the door the player was
    // just bounced out of stays inert, so a contact-triggered loading zone they
    // are still standing in cannot re-fire. Drive once per gameplay frame.
    void tickSubareaDenialCooldown();
    // Randomized Starting Position. The start id a new save's opening spawn
    // should travel under: `entranceId` itself when the start node is a loading
    // zone whose authored PlayerStart we want, otherwise a reserved id that
    // deliberately resolves to nothing so the coord-spawn path builds a
    // synthetic PlayerStartInfo. Pure - safe to call before the transition.
    const char* getStartPositionStartId(const char* entranceId);

    // Arms the pending-arrival machinery for that spawn. Must run AFTER any
    // GameDataFile::changeNextStage for the transition: its hook clears pending
    // arrivals, so arming first would be wiped. Cleared like any other arrival
    // once the transition completes.
    void armStartPositionArrival(const char* entranceId, float x, float y, float z,
        bool hasCoordinates);
    bool shouldHideRandoFakeExitPipeActor(const al::ActorInitInfo& info);
    void installHooks();

} // namespace EntranceHooks
