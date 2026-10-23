#pragma once

#include "Multiplayer/packets/Packet.h"

struct CoinCollectCollect : public Packet {
    PacketType getType() override { return PacketType::COINCOLLECTCOLL; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(placeID);
        writer.write(worldID);
        writer.writeString(stage);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(placeID);
        reader.read(worldID);
        reader.readString(stage);

        reader.finalize();
    }

    sead::FixedSafeString<0x40> placeID;
    int worldID = 0;
    sead::FixedSafeString<0x40> stage;
};
