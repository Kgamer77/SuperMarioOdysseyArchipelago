#pragma once

#include "Multiplayer/packets/Packet.h"

// One synced actor's state, as written by its own SyncedActor subclass.
//
// The payload is opaque to the server: every other packet in this protocol has
// a C# counterpart that parses its fields, so adding a synced field means
// touching the server and rebuilding it. Here the server only relays the blob,
// so a new SyncedActor subclass - or a new field on an existing one - is a
// client-only change.
//
// FIXED wire size, variable meaningful content, exactly like
// SnapshotChunkPacket: the server rejects any packet whose header size does
// not match its registered struct's Size, so the frame cannot shrink with the
// payload. cPayloadSize must stay in step with MultiplayerProtocol
// .ActorSyncPayloadSize on the server.
//
// mKind identifies which subclass wrote the payload so the receiver knows how
// to read it. mActorUid is the sender's registry index; it is only meaningful
// together with the sender's user id, since clients index independently.
struct ActorSync : public Packet {
    PacketType getType() override { return PacketType::ACTORSYNC; }

    // 128 covers Phase 1 (a vec3 plus a quat is 28) with room for the costume
    // and capture identity a puppet subclass would carry in Phase 2, so the
    // wire format does not need revisiting then.
    static constexpr u16 cPayloadSize = 128;

    // The server rejects any packet whose header size disagrees with its
    // registered struct, and a silent mismatch here does not fail cleanly -
    // it misparses, which reads as a client bug. CapPacket going 0x50 -> 0x5d
    // is exactly how that goes wrong, so pin the frame at compile time.
    // Server side: MultiplayerProtocol.ActorSyncPayloadSize + 6.
    static constexpr s32 cWireSize = 1 + 1 + 2 + 2 + cPayloadSize;
    static_assert(cWireSize == 134, "ActorSync frame must match ActorSyncPacket.Size on the server");

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(mKind);
        writer.write(mOwner);
        writer.write(mActorUid);
        writer.write(mUsedSize);
        // Always the full buffer - see the fixed-size note above.
        writer.writeBytes(mPayload, cPayloadSize);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(mKind);
        reader.read(mOwner);
        reader.read(mActorUid);
        reader.read(mUsedSize);
        reader.readBytes(mPayload, cPayloadSize);

        reader.finalize();

        // mUsedSize is attacker-controlled; every reader of mPayload bounds
        // itself by it, so clamp before anything downstream trusts it.
        if (mUsedSize > cPayloadSize) {
            hk::diag::logLine("[ActorSync] used size %u exceeds %u - dropping", mUsedSize, cPayloadSize);
            mUsedSize = 0;
            mIsFail = true;
        }
    }

    u8 mKind = 0;
    u8 mOwner = 0;
    u16 mActorUid = 0;
    // How much of mPayload the subclass actually wrote.
    u16 mUsedSize = 0;
    u8 mPayload[cPayloadSize] = {};
};
