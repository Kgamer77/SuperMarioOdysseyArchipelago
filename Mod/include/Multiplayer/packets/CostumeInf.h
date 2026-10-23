#pragma once

#include "Multiplayer/packets/Packet.h"

struct CostumeInf : public Packet {
    PacketType getType() override { return PacketType::COSTUMEINF; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(bodyModel);
        writer.writeString(capModel);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(bodyModel);
        reader.readString(capModel);

        reader.finalize();
    }

    sead::FixedSafeString<COSTUMEBUFSIZE> bodyModel;
    sead::FixedSafeString<COSTUMEBUFSIZE> capModel;
};