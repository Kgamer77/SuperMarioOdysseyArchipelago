#pragma once

#include "Packet.h"

struct PACKED RegionalCollect : Packet {
    RegionalCollect() : Packet() {
        this->mType = PacketType::REGCOLL;
        mPacketSize = sizeof(RegionalCollect) - sizeof(Packet);
    };
    char objId[OBJECTIDSIZE] = {};
    char worldName[0x30] = {};
};