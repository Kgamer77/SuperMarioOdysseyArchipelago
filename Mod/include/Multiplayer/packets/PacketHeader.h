#pragma once

#include "Multiplayer/packets/Packet.h"

struct PacketHeader : public Packet {
    PacketType getType() override { return PacketType::UNKNOWN; }

    PacketVector serialize() override { return PacketVector(); }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.finalize();
    }
};