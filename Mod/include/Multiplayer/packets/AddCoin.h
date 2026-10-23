#pragma once

#include "Multiplayer/packets/Packet.h"

struct AddCoin : public Packet {
    PacketType getType() override { return PacketType::ADDCOIN; }

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
