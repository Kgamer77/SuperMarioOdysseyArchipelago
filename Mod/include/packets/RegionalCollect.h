#pragma once

#include "Packet.h"

struct PACKED RegionalCollect : Packet {
    RegionalCollect() : Packet() {
        this->mType = PacketType::REGCOLL;
        mPacketSize = sizeof(RegionalCollect) - sizeof(Packet);
    };
    RegionalCollect(const char* objectId, const char* currentWorld) : Packet() {
        this->mType = PacketType::ITEMCOLL;
        mPacketSize = sizeof(RegionalCollect) - sizeof(Packet);
        strcpy(name, objectId);
        strcpy(worldName, currentWorld);
    }
    char* objId[OBJECTIDSIZE] = {};
    char* worldName[0x30] = {};
};