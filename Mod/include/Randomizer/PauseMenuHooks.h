#pragma once

class StageSceneStateModConfig;

namespace PauseMenuHooks {

    void installHooks();

    // The one persistent ModConfig sub-state instance (created once, reused for
    // every pause-menu open); null before the pause menu has been constructed.
    StageSceneStateModConfig* getModConfigInstance();

    // Audit runs must leave the pause-menu host before their sequence-side
    // validator can request a reload. The request is consumed by the pause
    // state's ModConfig executor on its next normal tick.
    void requestAuditMenuClose();
    bool consumeAuditMenuCloseRequest();

    // Set right before transitioning the title's Wait nerve to FileSelect for
    // the Multiplayer row, so exeFileSelect's first tick knows to run the
    // blocking room-code keyboard prompt before anything else (see
    // StageSceneStatePauseMenu.cpp).
    bool consumePendingRoomCodePrompt();

} // namespace PauseMenuHooks
