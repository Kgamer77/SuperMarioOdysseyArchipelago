#pragma once

#include "Multiplayer/packets/Packet.h"

struct MoonRockHit : public Packet {
    PacketType getType() override { return PacketType::MOONROCKHIT; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(worldId);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(worldId);

        reader.finalize();
    }

    int worldId = 0;
};