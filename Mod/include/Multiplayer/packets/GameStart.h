#pragma once

#include "Multiplayer/packets/Packet.h"

struct GameStart : public Packet {
    PacketType getType() override { return PacketType::GAMESTART; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.finalize();
    }
};