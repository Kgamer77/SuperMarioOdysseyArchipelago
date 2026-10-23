#pragma once

#include "Multiplayer/packets/Packet.h"

struct CoinCollectUse : public Packet {
    PacketType getType() override { return PacketType::COINCOLLECTUSE; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(worldID);
        writer.write(count);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(worldID);
        reader.read(count);

        reader.finalize();
    }

    int worldID = -1;
    int count = 0;
};
