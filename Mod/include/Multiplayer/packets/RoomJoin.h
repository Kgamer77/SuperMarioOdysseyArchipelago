#pragma once

#include "Multiplayer/packets/Packet.h"

// Sent once, immediately after the server's CLIENTINIT response, to tag this
// connection with a room code. The dedicated server only relays gameplay
// packets between clients sharing the same room - see PacketType::ROOMJOIN's
// comment in Packet.h.
struct RoomJoin : public Packet {
    PacketType getType() override { return PacketType::ROOMJOIN; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(roomCode);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(roomCode);

        reader.finalize();
    }

    sead::FixedSafeString<0x20> roomCode;
};
