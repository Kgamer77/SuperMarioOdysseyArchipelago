#pragma once

#include "Multiplayer/packets/Packet.h"

struct CheckpointGet : public Packet {
    PacketType getType() override { return PacketType::CHECKPOINTGET; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(objId);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(objId);

        reader.finalize();
    }

    sead::FixedSafeString<0x40> objId;
};
