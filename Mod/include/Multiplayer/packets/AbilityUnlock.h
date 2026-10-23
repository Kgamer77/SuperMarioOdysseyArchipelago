#pragma once

#include "Multiplayer/packets/Packet.h"

struct AbilityUnlock : public Packet {
    PacketType getType() override { return PacketType::ABILITYUNLOCK; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(abilityId);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(abilityId);

        reader.finalize();
    }

    int abilityId = -1; // AbilityRando::AbilityId
};
