#pragma once

#include "EntranceDB.h"
#include "Randomizer/EntranceRando.h"

// Per-save record of the shuffled doors Mario has actually come OUT of.
//
// Kingdom-visit tracking (Randomizer::getVisitedKingdoms) only answers "has he
// stood in this kingdom", which the tracker turns into "he can start from its
// front door". That understates a shuffled arrival: walking a Sand door into
// Bowser's Kingdom can surface him at some far-flung loading zone that Bowser's
// own overworld route graph never reaches from the Odyssey landing site. Those
// checks then belong to no kingdom at all - the source kingdom's local view
// clamps at Bowser's front door, and Bowser's own view starts from the wrong
// node.
//
// An arrival is a permanent reachability proof, not a transient position: the
// door that produced it is still there and still connected the same way, so the
// player can always repeat the trip. That is what makes it safe to fold into
// the save-derived model the tracker uses (see MoonRando::buildLiveLogicState),
// which otherwise deliberately ignores where the player happens to be standing.
//
// Recorded as (link, ConnKind) pairs - and, for paintings, (link, direction) -
// rather than logic node ids: that is the unit EntranceHooks and PaintingRando
// already have at the moment they apply a transition, it survives logic-table
// regeneration, and MoonRando resolves it to the same node the placement logic
// would have credited.
namespace ArrivalLog {

    // ConnKind_SubNear/SubFar/HomeOut/HomeIn, i.e. every kind except _None.
    static constexpr s32 cKindCount = 4;
    static constexpr s32 cLinkCount = EntranceDB::sLinkCount;

    void reset();

    // Marks the destination side of a connection as physically arrived at.
    // Callers must NOT record an arrival that was diverted to the Odyssey: the
    // player never reaches the entrance's node in that case, exactly as
    // EntranceFallbacks::arrivalPlacesPlayerAtEntrance describes.
    void recordArrival(EntranceRando::Conn conn);

    bool hasArrived(s32 link, EntranceRando::ConnKind kind);

    // Painting warps have the same gap as shuffled doors: a link can land Mario
    // on a node his destination kingdom's own route graph never reaches from its
    // Odyssey landing site. Indexed exactly as expandStageAccessFromPaintings
    // does - `forward` means the transition matched painting link
    // `paintingIndex` on its SOURCE side (arriving at getMappedDestinationNode),
    // and the return direction means it matched on the DESTINATION side
    // (arriving at getMappedReturnSourceNode). A blocked/locked painting must
    // not be recorded: Mario never left.
    void recordPaintingArrival(s32 paintingIndex, bool forward);

    bool hasPaintingArrival(s32 paintingIndex, bool forward);

    // One compact summary survives alongside the arrival bits. The file-select
    // flow uses it to retain the exact number of start grants for the run log.
    void recordAutoGrantSummary(s32 count);
    s32 getAutoGrantSummaryCount();

    // Cheap change signal for consumers that cache a reachability computation
    // (the tracker's per-kingdom hint status). Changes whenever a new arrival is
    // recorded or the log is reset/reloaded.
    u32 getSignature();

    // Slot persistence, mirroring the Bingo board sidecar. The log lives beside
    // rando.bin rather than inside it because it changes during play while the
    // rando.bin payload is written once at seed creation.
    bool load(s32 slot);
    bool save(s32 slot);
    // Writes only when something changed since the last successful save.
    bool flushIfDirty(s32 slot);
    bool remove(s32 slot);

} // namespace ArrivalLog
