#pragma once

#include "Multiplayer/packets/Packet.h"

// Dedicated, idempotent fast-path signal for "this kingdom's Moon Rock Key
// moon was just credited" - additive to the general ShineCollect sync, not
// a replacement. See Client::hasRemoteMoonRockKeyUnlock / MoonHooks.cpp's
// isMoonRockKeyMissingForActor.
struct MoonRockKeyUnlock : public Packet {
    PacketType getType() override { return PacketType::MOONROCKKEYUNLOCK; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(kingdom);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(kingdom);

        reader.finalize();
    }

    int kingdom = -1;
};
