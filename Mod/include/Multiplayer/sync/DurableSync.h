#pragma once

#include <basis/seadTypes.h>

#include "Multiplayer/packets/Packet.h"

// Guaranteed delivery for the progress that must never be lost, and the
// persisted record of what this client has actually applied.
//
// WHY THIS EXISTS
//
// Every durable fact - a moon, a checkpoint, a moon rock, an unlock - used to
// reach the server as one fire-and-forget SharedEvent, and there were six
// separate ways for it to vanish without a trace:
//
//   * SocketClient::queuePacket drops (and deletes) when the 100-slot send
//     queue is full, and returned false into callers that ignored it.
//   * The same call deletes the packet outright when the socket is not
//     CONNECTED, so anything collected while reconnecting was simply gone.
//   * SocketClient::send deletes the packet on a mid-frame socket error.
//   * SocketClient::exeReset pops and deletes the WHOLE pending send queue on
//     every reconnect - one TCP hiccup could discard a burst of moons.
//   * Client::sendShineCollectPacket latched lastCollectedShine BEFORE the
//     queue attempt, so a refused moon was lost AND un-resendable.
//   * The server can answer Rejected (room not Ready yet, a failed durable
//     append), and Client::updateSharedEvent threw that answer away.
//
// requestEventCatchUp cannot fix any of these. It asks the server for events
// the server already has; a moon that never arrived is not in the log to ask
// for. The loss is on the SEND side, so the repair has to be too.
//
// HOW
//
// The transport is now allowed to lose anything. Every durable fact is written
// to the SD card the moment it happens, re-sent on a timer until the server
// acknowledges it, and only then retired. A crash, a power loss, a quit to
// title or an hour offline all resume from that file.
//
// THE ACKNOWLEDGEMENT ALREADY EXISTED
//
// No new packet was needed. RoomManager.AcceptEvent fans the accepted (or
// duplicate, or rejected) event out to every team member INCLUDING the sender,
// carrying back the eventId the sender chose. Client::updateSharedEvent was
// already receiving it and discarding it at its `if (isOwn) return;`. That echo
// is the ack; onEcho() is what consumes it.
//
// RETRY SAFETY
//
// Every kind routed through here is idempotent server-side, which is what makes
// blind retransmission correct rather than merely convenient:
//
//   * Moon / Checkpoint / MoonRock / MoonRockKey / PaintingUnlock /
//     KingdomVisit / KingdomComplete / KingdomBranchOrder / KingdomForcedPeace /
//     ScenarioAdvance / AbilityUnlock / CaptureUnlock / PurpleCoin dedup on a
//     canonical payload identity. KingdomBranchOrder dedups on the branch
//     rather than the choice, so a conflicting second resolution is refused
//     instead of flipping an ordering the team already settled.
//   * HintUnlock and the yellow-coin deltas are COUNTED rather than set-shaped,
//     so they dedup on the client nonce in eventId instead. That nonce is the
//     only reason a coin delta may be retried at all - a resent "+50" is
//     otherwise indistinguishable from earning 50 more.
//
// A retry of anything already accepted comes back Duplicate, which retires the
// entry exactly like an acceptance.
//
// WHERE THIS SITS - the three mechanisms and why none of them is redundant
//
// Progress sync has three moving parts. They are not three attempts at the same
// job; each covers a failure the others structurally cannot, and knowing which
// is which is the difference between debugging this and guessing at it.
//
//   1. THIS FILE - timely delivery of facts this client produced.
//      Covers: the send path losing a packet. Latency ~2s.
//      Cannot cover: a fact that was never submitted (a hook that did not fire,
//      a crash before update() persisted it, a save edited out of band).
//
//   2. requestEventCatchUp - ordered replay of the team log from the applied
//      watermark (Client::requestEventCatchUp).
//      Covers: the receive path losing a packet, and reconnecting after time
//      offline. This is the only one that preserves ORDER, which is why
//      counted facts - purple and yellow coins, hint purchases - depend on it.
//      Cannot cover: anything the server never learned about. It can only ever
//      hand back events the log already holds.
//      Note: a watermark of zero asks with cReplayFromStart rather than not
//      asking at all. afterRevision 0 already means "send me the room save", so
//      this case used to be skipped entirely - and since the watermark only
//      advances contiguously it could never climb off zero by itself, leaving
//      such a client permanently silent.
//
//   3. Client::sendStateVector -> RoomManager.ReconcileState - convergence on
//      what the saves actually contain, reconciled in BOTH directions. ~30s.
//      Covers: everything above, plus the case neither of the others can touch,
//      because it compares state rather than replaying messages.
//      Cannot cover: counted facts. You cannot diff a running total, which is
//      why coins are deliberately absent from the vector.
//      The two directions are gated separately (cStateVectorFlagNoAdopt): the
//      server replaying the team's own events back to us is always safe, while
//      adopting from a save that may carry ANOTHER team's progress is not. A
//      vector used to be withheld entirely in that situation, which switched
//      off the safe half too - see isSaveProvenForTeam.
//
// So the division is by the SHAPE of the fact, not by how important it is:
//
//   set-shaped (a moon, an unlock, a frontier position) - the state vector is
//     the correctness guarantee; the outbox is the latency optimisation.
//   counted or ordered (coins, hint purchases) - the outbox and the ordered
//     log ARE the correctness guarantee; nothing else can heal them, which is
//     why they had to be given nonce identities before they could be retried.
//
// A new synced fact should be classified this way before it is implemented. If
// it is set-shaped, it belongs in the state vector and the rest follows; if it
// is counted, it needs an identity of its own and the vector must not see it.
namespace DurableSync {

    // One durable fact in flight. 256 of them is far more than a session can
    // realistically accumulate unacknowledged (that would mean 256 moons
    // collected with no server contact at all), and the whole ring is only
    // written to disk as its LIVE entries, so an empty outbox costs a header.
    static constexpr s32 cCapacity = 256;
    static constexpr s32 cKeySize = MultiplayerProtocol::cEventKeySize;

    // ===== room identity =====

    // Called from updateRoomStatus, which runs on the read thread. Records the
    // identity only and flags a load; update() does the actual SD read on the
    // game thread. A room code, team or session id that differs from the
    // persisted file's discards it - a new room, or a server that wiped this
    // one, must not resume somebody else's outbox or watermark.
    void noteRoom(const char* roomCode, u8 team, const u64 sessionId[2]);
    bool isReady();

    // ===== sending =====

    // Records the fact, persists it, and attempts an immediate send. The send
    // failing is a non-event: the entry is already durable and the retransmit
    // tick owns it from here. Returns the assigned eventId, or 0 if the outbox
    // is not attached to a room (solo play) or is genuinely full.
    //
    // Deduplicates against entries already in flight, so a hook that fires
    // twice for the same fact does not occupy two slots.
    u64 submit(MultiplayerProtocol::SharedEventKind kind, s32 worldId, s32 value, const char* key,
        const char* auxiliary);

    // The server's verdict on one of OUR events. Safe to call from the read
    // thread; the SD write it implies is deferred to update().
    void onEcho(u64 eventId, MultiplayerProtocol::SharedEventStatus status);

    // ===== applied-revision watermark =====
    //
    // Deliberately NOT the same number as RoomStatus::revision, which is the
    // server's HEAD. Conflating the two is a correctness bug on the reconnect
    // path: adopting the head on join makes a client that has applied nothing
    // believe it has seen everything, so its next catch-up asks for events
    // after the head and is told there are none. Everything missed while
    // offline is skipped, permanently.
    //
    // This is instead the highest revision actually APPLIED here, and it only
    // ever advances contiguously - an event arriving past a hole applies (every
    // downstream apply is idempotent) but must not move the watermark over the
    // hole, or the periodic sweep would stop asking for the missing one.
    u64 getAppliedRevision();
    // Advances only when revision == applied + 1. Returns whether it moved.
    bool tryAdvanceAppliedRevision(u64 revision);
    // For the join path, where the snapshot replay has just delivered a whole
    // log in order and the commit names the revision it ended at. Also the
    // proof isSaveProvenForTeam wants, so it clears that mark.
    void setAppliedRevision(u64 revision);

    // Whether this save is known NOT to be carrying progress from a different
    // team's log. False only after a team change that no snapshot replay has
    // followed - the one transition that leaves a save describing a run this
    // team never made.
    //
    // The server reconciles the state vector in BOTH directions, and only the
    // adopting direction is dangerous: replaying the team's own events into
    // this save is always correct, while adopting from an unproven save would
    // inject another team's whole run into this team's log. So this rides the
    // vector as cStateVectorFlagNoAdopt and switches off that half alone. It
    // used to be expressed by refusing to publish a vector at all, which also
    // disabled the safe half - and, because the same condition gated the
    // catch-up sweep, left the client with no recovery mechanism of any kind.
    bool isSaveProvenForTeam();

    // ===== persisted state masks =====
    //
    // The part of the state vector that has no home in the save itself.
    //
    // Everything else the vector carries is read straight out of the save that
    // owns it when the vector is built - moons from the hint list, visited
    // kingdoms and forced peace from the randomizer state, scenario numbers
    // from the scenario array, kingdom progression from its own record.
    // Mirroring any of those here would be a second source of truth for state
    // that already has one, and the two would eventually disagree. Only these
    // three have nowhere else to live, so they are recorded as they happen and
    // persisted alongside the outbox.
    bool hasMoonRock(s32 worldId);
    void setMoonRock(s32 worldId);
    bool hasMoonRockKey(s32 worldId);
    void setMoonRockKey(s32 worldId);
    bool hasPainting(s32 paintingIndex);
    void setPainting(s32 paintingIndex);

    u32 getMoonRockMask();
    u32 getMoonRockKeyMask();
    u32 getPaintingMask();

    // ===== driving =====

    // Game thread, once per frame. Performs the deferred load, persists a dirty
    // state, and runs the retransmit tick.
    void update();
    // Flush everything pending right now - called when the room turns Ready and
    // on a fresh connection, where a backlog is most likely and most urgent.
    void flushNow();
    // Drops all state and detaches from the room. Leaving multiplayer.
    void reset();

    // ===== diagnostics =====
    s32 getPendingCount();
    s32 getRejectedCount();
    const char* getLastRejectionText();

} // namespace DurableSync
