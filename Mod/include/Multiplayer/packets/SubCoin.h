#pragma once

#include "Multiplayer/packets/Packet.h"

struct SubCoin : public Packet {
    PacketType getType() override { return PacketType::SUBCOIN; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(count);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(count);

        reader.finalize();
    }

    int count = 0;
};
