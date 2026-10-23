#pragma once

#include "sead/prim/seadSafeString.h"

#include "Multiplayer/packets/Packet.h"

struct CaptureInf : public Packet {
    PacketType getType() override { return PacketType::CAPTUREINF; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(hackName);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(hackName);

        reader.finalize();
    }

    sead::FixedSafeString<0x20> hackName;
};