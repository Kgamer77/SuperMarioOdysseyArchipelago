#pragma once

#include "Multiplayer/packets/Packet.h"

namespace MultiplayerProtocol {
    // 6: moon-rock keys, painting unlocks, scenario advancement and kingdom
    // unlocks are durable shared events rather than live-only broadcasts, and
    // the client publishes a periodic StateVector the server reconciles both
    // ways. A version-5 client relays the first two as unlogged broadcasts and
    // never publishes a vector, so mixing the two silently reintroduces exactly
    // the late-join desync this version exists to close.
    static constexpr u16 cVersion = 6;
    static constexpr s32 cMessageSize = 80;
    static constexpr s32 cEventKeySize = 64;
    static constexpr s32 cSnapshotChunkSize = 4096;
    // ===== STATE VECTOR =====
    //
    // Revision catch-up is exact, but it can only ever hand back events the
    // server already has. It cannot recover progress that was lost BEFORE it
    // reached the server - a refused send queue, a reconnect that discarded the
    // outbound queue, a hook that never fired. The state vector closes that:
    // every client periodically publishes what its own save actually contains,
    // and the server reconciles in BOTH directions (replays what the client
    // lacks, and adopts what the client has that the team log does not).
    //
    // Everything in it is set-shaped and idempotent, which is what makes
    // two-way reconciliation safe. Purple/yellow coins are deliberately absent:
    // they are counted, not set-shaped, so "adopt whatever the client has"
    // would double-count them.
    //
    // 2112 bits covers every uniqueId in the hint list (0x400 entries) plus the
    // 2000-2060 achievement/Toadette range with room to spare.
    static constexpr s32 cStateVectorMoonBits = 2112;
    static constexpr s32 cStateVectorMoonBytes = cStateVectorMoonBits / 8;
    static constexpr s32 cStateVectorKingdoms = 14;
    // Mirrors KingdomProgress::cBranchCount, declared locally rather than
    // including that header into the packet definitions.
    static constexpr s32 cStateVectorBranches = 2;
    // StateVector::flags, taken from what used to be trailing reserved padding
    // so the frame size is unchanged.
    //
    // cStateVectorFlagNoAdopt: this save is not known to have been built from
    // THIS team's event log - replay TO me, but never adopt FROM me.
    // Reconciliation is two-way and only the adopting half can inject a wrong
    // fact, so the two halves are now decided separately. They used to be
    // conflated: an unproven save published NO vector at all, which also
    // switched off the half that was always safe and left that client with no
    // healing whatsoever for the rest of the session.
    static constexpr u8 cStateVectorFlagNoAdopt = 1 << 0;

    // SnapshotRequest::afterRevision sentinels.
    //
    // afterRevision 0 already means "send me the room save", so a client whose
    // applied watermark is still zero had no way to ask for its team's log from
    // the beginning - it simply never asked, and everything it had missed
    // stayed missed. cReplayFromStart is that request. An older server reads it
    // as an ordinary "after revision N", finds nothing past it and replies with
    // nothing, which is a harmless no-op rather than a wrong answer.
    static constexpr u64 cReplayFromSnapshotBase = ~0ULL;
    static constexpr u64 cReplayFromStart = ~0ULL - 1;
    // Four teams, fixed. The file select's picker is a four-value roll and the
    // in-game roster panel is laid out for four columns, so this is not a
    // number either side can raise alone - server/Shared/MultiplayerProtocol.cs
    // has to agree.
    static constexpr s32 cTeamCount = 4;
    // TeamRoster is a fixed-size packet, so the roster has a hard cap that is
    // independent of the server's MaxPlayersPerRoom. A bigger room still works;
    // the panel just stops listing past the 16th member.
    static constexpr s32 cMaxRosterEntries = 16;
    static constexpr s32 cRosterNameSize = 0x20;

    enum class ProtocolStatus : u8
    {
        Accepted,
        VersionMismatch,
        ServerRestarted,
    };

    enum class RoomRole : u8
    {
        Guest,
        SetupOwner,
        Player,
    };

    enum class RoomState : u8
    {
        Configuring,
        Uploading,
        Ready,
        Closing,
    };

    enum class RoomError : u8
    {
        None,
        InvalidCode,
        Full,
        Ended,
        NotOwner,
        TransferRejected,
    };

    enum class SnapshotArtifactType : u8
    {
        RandoSave,
        NativeSave,
        BingoBoard,
        ArrivalLog,
        SharedManifest,
    };

    enum MultiplayerFlags : u8
    {
        FlagNone = 0,
        FlagDeathLink = 1,
        // Host-set, from the file select's Multiplayer page. While CLEAR, the
        // server will not relay player-state packets across team lines, so an
        // opposing player's position never reaches this console at all - the
        // puppet is never created rather than created and hidden.
        FlagSyncOtherTeams = 2,
    };

    enum class SharedEventKind : u8
    {
        Moon,
        PurpleCoin,
        PurpleCoinSpend,
        Checkpoint,
        MoonRock,
        AbilityUnlock,
        CaptureUnlock,
        // A kingdom reached for the first time. It already travels live on
        // GameInf, but only to whoever is connected right then; as an event it
        // is persisted in the team's log and replays on join, so a player who
        // was offline still gets the kingdom revealed rather than staring at
        // "?" moon counts on the tracker for somewhere the team has been.
        //
        // worldId = the kingdom. key = the same number in decimal, which is
        // what the server dedups on.
        KingdomVisit,
        // A hint bought out of the shared team wallet.
        //
        // worldId  = HintSystem::SharedUnlock's stable numeric value.
        // value    = ShineDB table index for location/route/flag hints, or -1
        //            for an economy-only direct item purchase.
        // key      = the table index in decimal, or the portable flag key for
        //            a capture/ability hint.
        // auxiliary= "<category>:<amount>:<buyerPrePurchaseCount>". The final
        //            field records which ladder step the buyer paid; eventId
        //            supplies the at-most-once identity so resetting progressive
        //            counters cannot collide with an older purchase.
        HintUnlock,
        // Global yellow-coin deltas. worldId is unused; value is positive.
        //
        // Unlike every other kind here these are COUNTED, not set-shaped, so
        // they carry a client-nonce eventId and dedup on it. Without that they
        // could not be retried at all: a resend of a "+50 coins" delta with no
        // identity is indistinguishable from earning 50 more.
        YellowCoinAdd,
        YellowCoinSpend,
        // ===== added in protocol 6 =====
        //
        // All four are set-shaped: replaying one is a no-op, which is what lets
        // the outbox retry them freely and the state vector adopt them in
        // either direction.
        //
        // A kingdom's Moon Rock key moon being credited. worldId = kingdom,
        // key = the same number in decimal, value = 0. Previously a
        // MOONROCKKEYUNLOCK broadcast that the server never logged, so a late
        // joiner never learned the rock was open - the reported "moon rock cube
        // state" desync.
        MoonRockKey,
        // A Cloud-kingdom painting warp route being unlocked. value = painting
        // link index, key = the same number in decimal, worldId = -1.
        // Previously a PAINTINGUNLOCK broadcast, with the same gap.
        PaintingUnlock,
        // A kingdom's scenario number advancing. worldId = kingdom,
        // value = the new scenario number, key = "<world>:<scenario>".
        //
        // Applied as max(), never as a decrement: the log holds one event per
        // step reached, and a receiver only ever raises its own number. This
        // used to happen only inside Client::updateShines' scenarioSyncList
        // walk, which needs a live StageScene and therefore could not run
        // during a join replay at the file select.
        ScenarioAdvance,
        // ===== kingdom progression =====
        //
        // KingdomProgress had no multiplayer path at all before protocol 6.
        // These three describe its STATE rather than the mutations that produce
        // it, which is the difference between complete and partial: the
        // frontier only ever advances inside tryCompleteFrontier, whose single
        // caller requires the player to personally deposit moons at the globe,
        // so a team-mate holding identical moons could sit on a stale frontier
        // indefinitely. See KingdomProgress::SyncState.
        //
        // How far along the ordered progression list the team is.
        // value = the completed PREFIX LENGTH (1..14), worldId = -1,
        // key = the count in decimal. Applied as max().
        //
        // A prefix length rather than a per-kingdom mask because
        // KingdomProgress::validate rejects any record where a completed
        // kingdom follows an incomplete one - a per-kingdom OR-merge would
        // produce records the save refuses to store.
        KingdomComplete,
        // Which sibling was taken at a branch point.
        // worldId = branch index (0..1), value = the chosen kingdom id,
        // key = the branch index in decimal.
        //
        // Deduped on the BRANCH rather than on the choice, so the first
        // resolution a team makes wins and a conflicting second comes back
        // Duplicate instead of reordering somebody's list underneath them.
        KingdomBranchOrder,
        // A kingdom flipped to forced-peace by the On Kingdom Complete world
        // peace mode - which fires in the same globe-completion block as the
        // frontier advance, and was equally invisible to a team-mate.
        // worldId = the kingdom, value = 1, key = the kingdom in decimal.
        KingdomForcedPeace,
        TravelingPeachStart,
    };

    enum class SharedEventStatus : u8
    {
        Proposed,
        Accepted,
        Duplicate,
        Rejected,
    };
}

struct ProtocolHello : public Packet {
    PacketType getType() override { return PacketType::PROTOCOLHELLO; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(version);
        writer.write(isReconnect);
        writer.write(expectedServerInstance[0]);
        writer.write(expectedServerInstance[1]);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(version);
        reader.read(isReconnect);
        reader.read(expectedServerInstance[0]);
        reader.read(expectedServerInstance[1]);
        reader.finalize();
    }
    u16 version = MultiplayerProtocol::cVersion;
    u8 isReconnect = 0;
    u64 expectedServerInstance[2] = {};
};

struct ProtocolResult : public Packet {
    PacketType getType() override { return PacketType::PROTOCOLRESULT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(version);
        writer.write(status);
        writer.write(serverInstance[0]);
        writer.write(serverInstance[1]);
        writer.writeString(message);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(version);
        reader.read(status);
        reader.read(serverInstance[0]);
        reader.read(serverInstance[1]);
        reader.readString(message);
        reader.finalize();
    }
    u16 version = 0;
    MultiplayerProtocol::ProtocolStatus status = MultiplayerProtocol::ProtocolStatus::VersionMismatch;
    u64 serverInstance[2] = {};
    sead::FixedSafeString<MultiplayerProtocol::cMessageSize> message;
};

// team sits between flags and revision rather than at the end, because
// revision is now PER TEAM - a reader holding one without the other cannot
// interpret it. Must stay byte-for-byte in step with RoomStatusPacket in
// server/Shared/Packet/Packets/ProtocolPackets.cs.
struct RoomStatus : public Packet {
    PacketType getType() override { return PacketType::ROOMSTATUS; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(role);
        writer.write(state);
        writer.write(error);
        writer.write(flags);
        writer.write(team);
        writer.write(revision);
        writer.write(sessionId[0]);
        writer.write(sessionId[1]);
        writer.writeString(message);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(role);
        reader.read(state);
        reader.read(error);
        reader.read(flags);
        reader.read(team);
        reader.read(revision);
        reader.read(sessionId[0]);
        reader.read(sessionId[1]);
        reader.readString(message);
        reader.finalize();
    }
    MultiplayerProtocol::RoomRole role = MultiplayerProtocol::RoomRole::Guest;
    MultiplayerProtocol::RoomState state = MultiplayerProtocol::RoomState::Closing;
    MultiplayerProtocol::RoomError error = MultiplayerProtocol::RoomError::Ended;
    u8 flags = 0;
    // 0-based; 0 is "Team 1". This is the team the SERVER settled on, which is
    // the one whose event log the revision beside it counts.
    u8 team = 0;
    u64 revision = 0;
    u64 sessionId[2] = {};
    sead::FixedSafeString<MultiplayerProtocol::cMessageSize> message;
};

struct SnapshotBegin : public Packet {
    PacketType getType() override { return PacketType::SNAPSHOTBEGIN; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(transferId[0]);
        writer.write(transferId[1]);
        writer.write(seedFingerprint);
        writer.write(totalSize);
        writer.write(fileCount);
        writer.write(flags);
        writer.write(reserved);
        writer.write(baseRevision);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(transferId[0]);
        reader.read(transferId[1]);
        reader.read(seedFingerprint);
        reader.read(totalSize);
        reader.read(fileCount);
        reader.read(flags);
        reader.read(reserved);
        reader.read(baseRevision);
        reader.finalize();
    }
    u64 transferId[2] = {};
    u64 seedFingerprint = 0;
    u32 totalSize = 0;
    u8 fileCount = 0;
    u8 flags = 0;
    u16 reserved = 0;
    u64 baseRevision = 0;
};

struct SnapshotFile : public Packet {
    PacketType getType() override { return PacketType::SNAPSHOTFILE; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(transferId[0]);
        writer.write(transferId[1]);
        writer.write(fileIndex);
        writer.write(artifactType);
        writer.write(reserved);
        writer.write(fileSize);
        writer.write(crc32);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(transferId[0]);
        reader.read(transferId[1]);
        reader.read(fileIndex);
        reader.read(artifactType);
        reader.read(reserved);
        reader.read(fileSize);
        reader.read(crc32);
        reader.finalize();
    }
    u64 transferId[2] = {};
    u8 fileIndex = 0;
    MultiplayerProtocol::SnapshotArtifactType artifactType = MultiplayerProtocol::SnapshotArtifactType::RandoSave;
    u16 reserved = 0;
    u32 fileSize = 0;
    u32 crc32 = 0;
};

struct SnapshotChunk : public Packet {
    PacketType getType() override { return PacketType::SNAPSHOTCHUNK; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(transferId[0]);
        writer.write(transferId[1]);
        writer.write(fileIndex);
        writer.write(offset);
        writer.write(dataLength);
        writer.writeBytes(bytes, MultiplayerProtocol::cSnapshotChunkSize);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(transferId[0]);
        reader.read(transferId[1]);
        reader.read(fileIndex);
        reader.read(offset);
        reader.read(dataLength);
        reader.readBytes(bytes, MultiplayerProtocol::cSnapshotChunkSize);
        if (dataLength > MultiplayerProtocol::cSnapshotChunkSize)
            mIsFail = true;
        reader.finalize();
    }
    u64 transferId[2] = {};
    u8 fileIndex = 0;
    u32 offset = 0;
    u16 dataLength = 0;
    u8 bytes[MultiplayerProtocol::cSnapshotChunkSize] = {};
};

struct SnapshotCommit : public Packet {
    PacketType getType() override { return PacketType::SNAPSHOTCOMMIT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(transferId[0]);
        writer.write(transferId[1]);
        writer.write(accepted);
        writer.write(revision);
        writer.writeString(message);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(transferId[0]);
        reader.read(transferId[1]);
        reader.read(accepted);
        reader.read(revision);
        reader.readString(message);
        reader.finalize();
    }
    u64 transferId[2] = {};
    u8 accepted = 0;
    u64 revision = 0;
    sead::FixedSafeString<MultiplayerProtocol::cMessageSize> message;
};

struct SnapshotRequest : public Packet {
    PacketType getType() override { return PacketType::SNAPSHOTREQUEST; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(afterRevision);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(afterRevision);
        reader.finalize();
    }
    u64 afterRevision = 0;
};

struct SharedEvent : public Packet {
    PacketType getType() override { return PacketType::SHAREDEVENT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(eventId);
        writer.write(revision);
        writer.write(kind);
        writer.write(status);
        writer.write(worldId);
        writer.write(value);
        writer.writeString(key);
        writer.writeString(auxiliary);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(eventId);
        reader.read(revision);
        reader.read(kind);
        reader.read(status);
        reader.read(worldId);
        reader.read(value);
        reader.readString(key);
        reader.readString(auxiliary);
        reader.finalize();
    }
    u64 eventId = 0;
    u64 revision = 0;
    MultiplayerProtocol::SharedEventKind kind = MultiplayerProtocol::SharedEventKind::Moon;
    MultiplayerProtocol::SharedEventStatus status = MultiplayerProtocol::SharedEventStatus::Proposed;
    s32 worldId = -1;
    s32 value = 0;
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> auxiliary;
};

struct SharedBaseline : public Packet {
    PacketType getType() override { return PacketType::SHAREDBASELINE; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(worldId);
        writer.write(collected);
        writer.write(spent);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(worldId);
        reader.read(collected);
        reader.read(spent);
        reader.finalize();
    }
    s32 worldId = -1;
    s32 collected = 0;
    s32 spent = 0;
};

struct CoinReserveRequest : public Packet {
    PacketType getType() override { return PacketType::COINRESERVEREQUEST; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(requestId);
        writer.write(worldId);
        writer.write(count);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(requestId);
        reader.read(worldId);
        reader.read(count);
        reader.finalize();
    }
    u64 requestId = 0;
    s32 worldId = -1;
    s32 count = 0;
};

struct CoinReserveResult : public Packet {
    PacketType getType() override { return PacketType::COINRESERVERESULT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(requestId);
        writer.write(accepted);
        writer.write(available);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(requestId);
        reader.read(accepted);
        reader.read(available);
        reader.finalize();
    }
    u64 requestId = 0;
    u8 accepted = 0;
    s32 available = 0;
};

struct CoinReserveCommit : public Packet {
    PacketType getType() override { return PacketType::COINRESERVECOMMIT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(requestId);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(requestId);
        reader.finalize();
    }
    u64 requestId = 0;
};

struct CoinReserveRelease : public Packet {
    PacketType getType() override { return PacketType::COINRESERVERELEASE; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(requestId);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(requestId);
        reader.finalize();
    }
    u64 requestId = 0;
};

// Setup-owner -> server. Sets the room's MultiplayerFlags, which the server
// then reflects back to everyone in the next RoomStatus - that is how a guest
// learns death link is on, so there is no server->client counterpart here.
//
// The room's own flags used to be settable only through a snapshot upload
// (SnapshotBegin::flags), which the client has never implemented, so nothing
// could turn any of them on. This is the direct path.
//
// reserved is carried so further room settings cost no packet-format change;
// the server ignores it today.
struct RoomSettings : public Packet {
    PacketType getType() override { return PacketType::ROOMSETTINGS; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(flags);
        writer.writeBytes(reserved, sizeof(reserved));
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(flags);
        reader.readBytes(reserved, sizeof(reserved));
        reader.finalize();
    }
    u8 flags = 0;
    u8 reserved[7] = {};
};

// Client -> server: "put me on team N".
//
// Sent from the file select's Multiplayer page before the snapshot request, so
// the server knows whose event log to replay into the save that is about to be
// built. The reply is an ordinary RoomStatus (carrying the team the server
// settled on) plus a TeamRoster to the whole room, so there is no dedicated
// result packet.
struct TeamSelect : public Packet {
    PacketType getType() override { return PacketType::TEAMSELECT; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(team);
        writer.writeBytes(reserved, sizeof(reserved));
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(team);
        reader.readBytes(reserved, sizeof(reserved));
        reader.finalize();
    }
    u8 team = 0;
    u8 reserved[7] = {};
};

// Server -> every member of a room, on any membership or team change.
//
// The ONLY way this client learns who is on which team. It cannot be derived
// from the puppet array: with cross-team sync off, the server's relay filter
// never delivers an opposing player's Connect packet in the first place, which
// is exactly the point of that filter and exactly why the roster needs its own
// channel. Sent whole rather than as deltas, so a client that misses one is
// corrected by the next instead of drifting.
struct TeamRosterEntry {
    u64 id[2] = {};
    u8 team = 0;
    u8 connected = 0;
    u16 reserved = 0;
    sead::FixedSafeString<MultiplayerProtocol::cRosterNameSize> name;
};

struct TeamRoster : public Packet {
    PacketType getType() override { return PacketType::TEAMROSTER; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(count);
        writer.writeBytes(reserved, sizeof(reserved));
        for (s32 i = 0; i < MultiplayerProtocol::cMaxRosterEntries; i++) {
            writer.write(entries[i].id[0]);
            writer.write(entries[i].id[1]);
            writer.write(entries[i].team);
            writer.write(entries[i].connected);
            writer.write(entries[i].reserved);
            writer.writeString(entries[i].name);
        }
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(count);
        reader.readBytes(reserved, sizeof(reserved));
        for (s32 i = 0; i < MultiplayerProtocol::cMaxRosterEntries; i++) {
            reader.read(entries[i].id[0]);
            reader.read(entries[i].id[1]);
            reader.read(entries[i].team);
            reader.read(entries[i].connected);
            reader.read(entries[i].reserved);
            reader.readString(entries[i].name);
        }
        if (count > MultiplayerProtocol::cMaxRosterEntries)
            count = MultiplayerProtocol::cMaxRosterEntries;
        reader.finalize();
    }
    u8 count = 0;
    u8 reserved[3] = {};
    TeamRosterEntry entries[MultiplayerProtocol::cMaxRosterEntries];
};

struct DeathLink : public Packet {
    PacketType getType() override { return PacketType::DEATHLINK; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.write(eventId);
        writer.write(generation);
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.read(eventId);
        reader.read(generation);
        reader.finalize();
    }
    u64 eventId = 0;
    u64 generation = 0;
};

// Client -> server, every ~30s and on every room join / stage load.
//
// "Here is everything my save actually contains." The server diffs it against
// the team's event log and answers in both directions: it replays the events
// this client is missing, and it adopts - as ordinary proposed events - the
// progress this client has that the log does not. That second direction is the
// one that heals a moon lost before it ever reached the server, which no amount
// of revision catch-up can do.
//
// Fixed size on purpose (packet dispatch rejects a header size that disagrees
// with the registered struct), and small: ~300 bytes, versus 4 KB for a single
// snapshot chunk. Mirrors StateVectorPacket in
// server/Shared/Packet/Packets/RoomGameplayPackets.cs byte for byte.
struct StateVector : public Packet {
    PacketType getType() override { return PacketType::STATEVECTOR; }
    PacketVector serialize() override {
        PacketWriter writer(this);
        writer.writeBytes(moonBits, MultiplayerProtocol::cStateVectorMoonBytes);
        writer.write(kingdomVisited);
        writer.write(moonRocksHit);
        writer.write(moonRockKeys);
        writer.write(paintingUnlocks);
        writer.write(forcedPeace);
        writer.writeBytes(scenarioNo, MultiplayerProtocol::cStateVectorKingdoms);
        writer.write(kingdomCompletedCount);
        writer.writeBytes(reinterpret_cast<const u8*>(branchChoice),
            MultiplayerProtocol::cStateVectorBranches);
        writer.write(flags);
        writer.writeBytes(reserved, sizeof(reserved));
        return writer.finalize();
    }
    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());
        reader.readBytes(moonBits, MultiplayerProtocol::cStateVectorMoonBytes);
        reader.read(kingdomVisited);
        reader.read(moonRocksHit);
        reader.read(moonRockKeys);
        reader.read(paintingUnlocks);
        reader.read(forcedPeace);
        reader.readBytes(scenarioNo, MultiplayerProtocol::cStateVectorKingdoms);
        reader.read(kingdomCompletedCount);
        reader.readBytes(reinterpret_cast<u8*>(branchChoice),
            MultiplayerProtocol::cStateVectorBranches);
        reader.read(flags);
        reader.readBytes(reserved, sizeof(reserved));
        reader.finalize();
    }

    bool hasMoon(s32 shineId) const {
        if (shineId < 0 || shineId >= MultiplayerProtocol::cStateVectorMoonBits)
            return false;
        return (moonBits[shineId >> 3] & (1u << (shineId & 7))) != 0;
    }
    void setMoon(s32 shineId) {
        if (shineId < 0 || shineId >= MultiplayerProtocol::cStateVectorMoonBits)
            return;
        moonBits[shineId >> 3] |= (u8)(1u << (shineId & 7));
    }

    u8 moonBits[MultiplayerProtocol::cStateVectorMoonBytes] = {};
    u32 kingdomVisited = 0;
    u32 moonRocksHit = 0;
    u32 moonRockKeys = 0;
    // Both masks are 32 bits wide, so every index the server accepts for these
    // must be < 32 or the vector could not represent it - and an index the
    // vector cannot represent diffs as permanently missing, which would make
    // the server re-send that fact on every sweep forever.
    u32 paintingUnlocks = 0;
    u32 forcedPeace = 0;
    // Per-kingdom scenario number. 0 means "nothing to say about this
    // kingdom", which is also the value a save that has never been there
    // reports, so an empty vector proposes nothing.
    u8 scenarioNo[MultiplayerProtocol::cStateVectorKingdoms] = {};
    // KingdomProgress::SyncState on the wire. A completed PREFIX LENGTH rather
    // than a per-kingdom mask, because the save's own validator rejects a
    // completed kingdom that follows an incomplete one.
    u8 kingdomCompletedCount = 0;
    s8 branchChoice[MultiplayerProtocol::cStateVectorBranches] = { -1, -1 };
    // MultiplayerProtocol::cStateVectorFlag*. Carved out of the three reserved
    // bytes, so the frame is exactly the size it always was and no protocol
    // version bump is needed - a sender that leaves it zero means "adoption is
    // allowed", which is the behaviour every published vector already had.
    u8 flags = 0;
    u8 reserved[2] = {};
};

static_assert(sizeof(u64) * 2 + sizeof(u16) + sizeof(u8) == 19, "ProtocolHello wire size changed");
