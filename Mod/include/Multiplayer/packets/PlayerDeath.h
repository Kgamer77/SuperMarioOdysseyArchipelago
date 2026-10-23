#pragma once

#include "Multiplayer/packets/Packet.h"

// Purely informational (Player Event Log) - no payload beyond the header's
// mUserID. Nothing on the receiving end needs to change save state for
// this; the actual coin loss on death already rides the generic
// subCoinHook sync like any other GameDataFunction::subCoin call.
struct PlayerDeath : public Packet {
    PacketType getType() override { return PacketType::PLAYERDEATH; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.finalize();
    }
};
