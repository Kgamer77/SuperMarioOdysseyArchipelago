#pragma once

#include "sead/prim/seadSafeString.h"

#include "Multiplayer/packets/Packet.h"

struct PlayerConnect : public Packet {
    PacketType getType() override { return PacketType::PLAYERCON; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(conType);
        writer.write(maxPlayerCount);
        writer.writeString(clientName);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(conType);
        reader.read(maxPlayerCount);
        reader.readString(clientName);

        reader.finalize();
    }

    ConnectionTypes conType;
    u16 maxPlayerCount = USHRT_MAX;
    sead::FixedSafeString<COSTUMEBUFSIZE> clientName;
};