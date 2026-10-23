#pragma once

#include "Multiplayer/packets/Packet.h"

struct CaptureUnlock : public Packet {
    PacketType getType() override { return PacketType::CAPTUREUNLOCK; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(captureIndex);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(captureIndex);

        reader.finalize();
    }

    int captureIndex = -1; // CaptureRando::getCapture() index
};
