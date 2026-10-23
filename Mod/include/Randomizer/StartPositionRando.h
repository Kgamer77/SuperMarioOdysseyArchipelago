#pragma once

#include <basis/seadTypes.h>

class GameDataHolder;
class PlayerActorHakoniwa;

// Randomized Starting Position: picks the logic node a brand new save opens at.
//
// Vanilla leaves the player wherever the Odyssey drops him (the starting
// kingdom's main stage, default spawn). The Overworld mode moves him to the
// overworld-side endpoint of a sub-area door in that kingdom; Max additionally
// allows any coordinate-bearing node inside a sub-area chain, as long as the
// logic can prove he walks back out to the starting kingdom's overworld with
// only his starting equipment.
//
// Odyssey mode is a fixed start rather than a roll: the file opens inside the
// ship (HomeShipInsideStage), so the kingdom stays hidden until the player walks
// out. It has no logic node - getStartNode() stays -1 while isActive() is true -
// because the ship's only exit is the unshuffled CurrentWorldHome door that puts
// him exactly where a vanilla file starts.
//
// The choice is a pure function of (seed, attempt) and every dependency it
// reads is rebuilt in the same order, so a reloaded save reconstructs exactly
// the same start without storing it. See MoonRando::initState for how the
// opening walk is modeled as *temporary* access, and MoonRando::canStartAtNode
// for the walk-back proof this module drives.
namespace StartPositionRando {

    void clear();

    // Gives up the randomized node and falls back to the ordinary Odyssey
    // drop-off in the same kingdom, exactly like a rebuild whose every probe
    // failed. Returns false when there was nothing to demote (vanilla mode, the
    // Odyssey start, or a drop-off already).
    //
    // MoonRando calls this when the post-finalize walk-back proof fails: the
    // node was chosen against the vanilla entrance stand-in, and the real wiring
    // can strand it. Deliberately does NOT invalidate rebuild()'s cache, so
    // every later rebuild(seed, attempt) for this attempt keeps the demoted
    // start rather than handing the stranded node back out.
    bool demoteToVanillaDropoff();

    // Selects the start node. Must run after the entrance shuffle and the
    // starting-equipment roll: both feed the walk-back proof.
    void rebuild(u32 seed, u32 attempt);

    bool isActive();

    // True when the pause-menu Return to Start action has a valid destination.
    // This is broader than isActive(): a randomized Overworld/Max roll may land
    // on the ordinary Odyssey drop-off, which has no logic node or forced stage
    // but is still a meaningful destination for the warp.
    bool canReturnToStart();

    // LogicData node/stage indexes, or -1 when inactive. getStartNode() is also
    // -1 for the Odyssey start, which has no node; test isActive() (or
    // getStartStageIndex()) when asking "is a non-vanilla start committed".
    s32 getStartNode();
    s32 getStartStageIndex();
    // Logical WorldId of the kingdom the run opens in. A randomized vanilla
    // drop-off keeps this populated even though isActive() is false; it is -1
    // before a rebuild or in the Vanilla mode.
    s32 getStartKingdom();
    // True when the chosen node is not on the starting kingdom's main stage.
    // Such a start needs the manual GameDataFile world-id repair, because
    // GameDataFile::startStage only resolves a world from a main stage name.
    bool isStartInSubArea();

    // The fixed Inside-the-Odyssey opening deliberately withholds all progress
    // counters until the player first leaves the ship. The state is consumed by
    // the first non-Odyssey stage, so Return to Start and later ship visits keep
    // the normal HUD.
    void armOpeningHudHide();
    bool shouldHideOpeningHud(const char* currentStage);

    // Physical placement of the chosen node.
    struct Spawn {
        // Stage to load. Never null while active.
        const char* stage;
        // The loading-zone id this node belongs to, or "" when it has none.
        const char* entranceId;
        // True for nodes that ARE a loading zone (entrance sides, stage_change).
        // Their recorded coordinates are the trigger volume, so spawning on them
        // re-fires the transition; resolve the authored PlayerStart for
        // entranceId instead and keep the coordinates only as a fallback.
        bool preferEntranceId;
        bool hasCoordinates;
        float x;
        float y;
        float z;
    };
    bool getSpawn(Spawn* out);

    // Node id of the chosen start, for the spoiler log. Null when inactive.
    const char* getStartNodeId();

    // Return to Start: sends the player back to the opening position from the
    // pause menu. Requests the stage change and arms the arrival, so the caller
    // still has to end the menu (the stage change lands when the scene does).
    // No-ops and returns false when neither a randomized node/Odyssey start nor
    // a randomized vanilla drop-off was selected.
    //
    // The logic depends on this existing: the start node is modeled as
    // permanently reachable precisely because this warp can always reach it.
    bool warpToStart(GameDataHolder* holder);

    // A kingdom's first-arrival cutscene puts Mario back at the Odyssey, undoing
    // the spawn we armed. Arming this asks for the start to be re-applied once
    // the demo hands control back; updatePendingReposition drives it from the
    // per-frame stage update and clears itself when it fires.
    //
    // The correction is a real warpToStart, not a transform write: moving the
    // actor alone leaves his velocity and collision in whatever state the demo
    // left them, which drops him out of the sky rather than standing him at the
    // entrance. It no-ops when the spawn survived the opening, so kingdoms
    // without an arrival cutscene never pay for an extra reload.
    void armPendingReposition();
    void clearPendingReposition();
    void updatePendingReposition(
        PlayerActorHakoniwa* player, GameDataHolder* holder, const char* currentStage);

} // namespace StartPositionRando
