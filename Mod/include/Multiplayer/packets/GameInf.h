#pragma once

#include "al/Library/Base/StringUtil.h"

#include "Multiplayer/packets/Packet.h"

struct GameInf : public Packet {
    PacketType getType() override { return PacketType::GAMEINF; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(is2D);
        writer.write(scenarioNo);
        writer.writeString(stageName);
        writer.write(gameMode);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(is2D);
        reader.read(scenarioNo);
        reader.readString(stageName);
        reader.read(gameMode);

        reader.finalize();
    }

    u8 is2D = false; // fake bool
    u8 scenarioNo = -1;
    sead::FixedSafeString<0x40> stageName;
    s8 gameMode = -1;

    bool operator==(const GameInf& rhs) const {
        return (is2D == rhs.is2D && scenarioNo == rhs.scenarioNo && al::isEqualString(stageName, rhs.stageName));
    }

    bool operator!=(const GameInf& rhs) const { return !operator==(rhs); }
};