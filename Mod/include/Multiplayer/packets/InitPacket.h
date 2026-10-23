#pragma once

#include "Multiplayer/packets/Packet.h"

struct InitPacket : public Packet {
    PacketType getType() override { return PacketType::CLIENTINIT; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(maxPlayers);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(maxPlayers);

        reader.finalize();
    }

    u16 maxPlayers = 0;
};