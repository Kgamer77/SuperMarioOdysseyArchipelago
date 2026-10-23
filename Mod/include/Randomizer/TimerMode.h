#pragma once

#include <basis/seadTypes.h>

class GameDataFile;
class PlayerActorHakoniwa;
class StageScene;

namespace ShineDB {
    struct ShineEntry;
}

// Timer Mode (Randomizer::RunMode::Timer): the run starts with a countdown
// clock, every moon collected buys more time, and the clock hitting zero ends
// the run the same way a hard Nuzlocke death does (see Nuzlocke.cpp - the death
// coordinator, its game-over panel and the save-slot deletion are shared).
//
// The clock only advances while Mario is actually playing: it is frozen during
// demos/cutscenes and while the game is paused, so a long moon-get sequence or
// a trip through the pause menu costs nothing.
namespace TimerMode {

    // Per-frame tick, driven from the HakoniwaSequence update hook after the
    // native update has run.  Every argument may be null; the tick is simply
    // skipped when the scene isn't in a playable state.
    void update(StageScene* scene, PlayerActorHakoniwa* player, GameDataFile* file);

    // ── opening lock ──────────────────────────────────────────────────────────
    // The clock does not start on its own.  Mario is held still until the player
    // presses ZL + ZR, exactly like the Bingo board's opening lock, so nobody
    // loses seconds to a loading screen or to reading the HUD.  The lock is
    // session-scoped rather than saved: reloading a run re-arms it, and the
    // clock resumes from the value the save carried.

    // Arms the lock once the run first hands Mario over to the player (i.e. not
    // during the opening demo).  Idempotent.
    void notifyMarioControlAvailable();
    // Must run BEFORE the native sequence update, so the chord is consumed
    // before gameplay reacts to those buttons.
    void handleInput();
    bool shouldBlockGameplayInput();
    bool consumeStartTrigger();
    // False until the chord has been pressed; the clock is frozen until then.
    bool isStarted();

    // Called from the setGotShine hook for the item that was actually credited.
    // Repeat pickups award nothing - only a genuinely new moon buys time.
    //
    // This is the single funnel for every acquisition route, so a moon handed
    // over in the native already-collected presentation (the "treat completed
    // kingdom moons as collected" option) buys its time on the first pickup
    // exactly like any other moon: that route still commits through
    // GameDataFunction::setGotShine, and `alreadyCollected` is read from the raw
    // save rather than from what the Shine actor is displaying.
    void awardMoon(const ShineDB::ShineEntry* item, bool alreadyCollected);

    // ── penalties ─────────────────────────────────────────────────────────────
    // Optional dials (both default to Off) that take time back off the clock.
    // Neither can arm the timeout on its own: they floor the clock at zero and
    // let update() reach the expiry from the same place it always does.

    // One hit of damage. Driven from PlayerHitPointData::damage, which retail
    // only reaches through PlayerDamageKeeper - a void fall, poison, lava and
    // drowning all take PlayerHitPointData::kill instead, so those are excluded
    // by construction and are covered by the death penalty alone.
    void notifyPlayerDamage();

    // One death, from GameDataFile::missAndRestartStage - the same "genuine
    // Mario death" signal the Nuzlocke coordinator arms on. Minigame and manual
    // reloads never reach it, and the mode's own timeout death is ignored.
    void notifyPlayerDeath();

    // True from the frame the clock runs out until the death coordinator has
    // taken over.  Nuzlocke::armMarioDeath consults this so a timeout arms the
    // hard-death flow while ordinary Timer Mode deaths stay vanilla.
    bool isTimeoutDeathPending();
    void notifyDeathArmed();

    // Dropped state for a run that is over (title screen, other run modes), so a
    // later run in the same session starts from a clean clock.
    void reset();

    // HUD countdown.  Drawn from the same ImGui pass as the other overlays.
    void draw();

    // Remaining clock in frames; -1 before the timer has been established for a
    // save.  Exposed for the HUD and diagnostics.
    s32 getRemainingFrames();

} // namespace TimerMode
