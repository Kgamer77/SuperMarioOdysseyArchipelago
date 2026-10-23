#pragma once

#include "Multiplayer/packets/Packet.h"

// Dedicated, idempotent fast-path signal for "this painting link was just
// unlocked" - additive to the general ShineCollect sync, not a
// replacement. See Client::hasRemotePaintingUnlock /
// PaintingRando::isPaintingUnlockedInternal.
struct PaintingUnlock : public Packet {
    PacketType getType() override { return PacketType::PAINTINGUNLOCK; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(paintingIndex);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(paintingIndex);

        reader.finalize();
    }

    int paintingIndex = -1;
};
