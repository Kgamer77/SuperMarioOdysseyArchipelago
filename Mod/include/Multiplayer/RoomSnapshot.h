#pragma once

#include <basis/seadTypes.h>

#include "SD/SDHooks.h"

struct Packet;

// Room snapshot transfer: how a room's seed actually gets from the setup owner
// to everyone else.
//
// The wire format (Multiplayer/packets/RoomProtocol.h) and the WHOLE server
// half (server/Server/RoomManager.cs - BeginUpload/DeclareFile/ReceiveChunk/
// CommitUpload/DownloadSnapshot) already existed; the client half never did.
// Client::readFunc() had no case for any of the five SNAPSHOT* packet types and
// nothing ever sent one, so a room never left RoomState::Configuring, every
// non-owner stayed RoomRole::Guest forever, and joining a room got you a normal
// file select where you made your OWN unrelated seed. That is the gap this
// closes.
//
// Two directions, one at a time (a client is either the setup owner uploading
// or a guest downloading, never both):
//
//   upload()        setup owner, blocking. Reads the slot's rando.bin
//                   (+ board.bin for a board run) off the SD card and
//                   pushes them to the server, which flips the room to
//                   RoomState::Ready on commit.
//   beginDownload() guest, asynchronous. Asks for the room save and lets the
//                   packet handlers below stream it to the guest save root; the
//                   file select polls getState() until it lands.
//
// tryHandlePacket() runs on SocketClient's RECEIVE thread rather than
// Client::readFunc()'s, so everything the UI reads back is either atomic or
// written once before the state flips. See its comment for why that thread.
namespace RoomSnapshot {

    enum class State : u8
    {
        Idle,
        // Setup owner.
        Uploading,
        UploadDone,
        UploadFailed,
        // Guest. Waiting means the request was refused because the owner has
        // not finished setting the seed up yet - that is a normal "come back
        // in a moment", not a failure, so it retries rather than erroring out.
        Waiting,
        Downloading,
        DownloadDone,
        DownloadFailed,
        // The guest has initialized its own native save and the server is now
        // replaying the selected team's durable event log into it.
        Replaying,
        ReplayDone,
        ReplayFailed,
    };

    // ===== setup owner =====

    // Uploads `slot`'s save as the room snapshot and blocks until the server
    // accepts or rejects it. Safe to call when not connected / not the setup
    // owner: it returns false without touching the socket.
    bool upload(s32 slot, SaveRedirect::Category category);

    // ===== guest =====

    // Wipes the guest save root, then asks the server for the room save.
    // Returns false if there is no room to ask.
    bool beginDownload(s32 slot);
    // Re-sends the request. Called from the join screen while the state is
    // Waiting, i.e. the owner is still configuring.
    void retryDownload();
    // Called only after guest NewGame has initialized and written File1.bin.
    // Uses SnapshotRequest's reserved UINT64_MAX value as the apply-ready ACK.
    bool beginReplay();
    // Drops any in-flight transfer and returns to Idle (backing out of the
    // join screen).
    void cancel();

    State getState();
    // Human-readable line for the join screen - the server's own rejection
    // reason where there is one.
    const char* getMessage();
    // 0-100 across the whole transfer, for the progress row.
    s32 getProgressPercent();
    // Which slot beginDownload() wrote into, for the caller to load.
    s32 getDownloadSlot();

    // ===== packet handling =====

    // Consumes the five SNAPSHOT* packet types, returning true if `packet` was
    // handled here. The replay-complete commit deliberately returns false so
    // it follows the replayed SharedEvents through the normal receive FIFO.
    //
    // Called from SocketClient::recv() - on the receive thread, BEFORE the
    // packet is queued - and deliberately not from Client::readFunc() like
    // every other handler. Two hard reasons, not a preference:
    //
    //   * gHeap is 512 KB in total (Multiplayer/hooks/initHooks.cpp) and one
    //     SnapshotChunk is ~4.2 KB of it. The receive queue holds 100 packets,
    //     so a room save streaming in at full speed would put ~415 KB of
    //     chunks in flight at once and exhaust the heap - and PacketAllocator
    //     aborts the process outright when it cannot allocate.
    //   * Writing each chunk to the SD card on the receive thread is the only
    //     flow control this protocol has. The server pushes the whole snapshot
    //     as fast as it can with no pacing of its own, so not reading the
    //     socket faster than the card can absorb is what keeps TCP's own
    //     window from being the thing that fails.
    //
    // The seed-transfer packets all go this way, not just the chunks: splitting
    // them across two threads would let a chunk overtake the SnapshotFile that
    // says which artifact it belongs to. Only replay completion takes the FIFO
    // path described above; no file bytes are involved by then.
    bool tryHandlePacket(Packet* packet);
    // Handles the replay-complete SnapshotCommit after it has followed all
    // replayed SharedEvents through Client::readFunc's FIFO.
    void handleQueuedReplayCommit(Packet* packet);

} // namespace RoomSnapshot
