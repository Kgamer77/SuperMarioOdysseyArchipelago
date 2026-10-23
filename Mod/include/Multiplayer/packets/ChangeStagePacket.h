#pragma once

#include "sead/prim/seadSafeString.h"

#include "Multiplayer/packets/Packet.h"

struct ChangeStagePacket : public Packet {
    PacketType getType() override { return PacketType::CHANGESTAGE; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.writeString(changeStage);
        writer.writeString(changeID);
        writer.write(scenarioNo);
        writer.write(subScenarioType);
        writer.write(extraDataFromServer);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.readString(changeStage);
        reader.readString(changeID);
        reader.read(scenarioNo);
        reader.read(subScenarioType);
        reader.read(extraDataFromServer);

        reader.finalize();
    }

    sead::FixedSafeString<0x30> changeStage;
    sead::FixedSafeString<0x10> changeID;
    s8 scenarioNo = -1;
    u8 subScenarioType = -1;
    u16 extraDataFromServer = 0;
};