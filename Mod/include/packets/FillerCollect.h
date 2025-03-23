
#pragma once

#include "Packet.h"


struct PACKED FillerCollect : Packet {
    FillerCollect() : Packet() {this->mType = PacketType::FILLERCOLL; mPacketSize = sizeof(FillerCollect) - sizeof(Packet);};
    FillerCollect(int itemType) : Packet() {
        this->mType = PacketType::FILLERCOLL;
        mPacketSize = sizeof(FillerCollect) - sizeof(Packet);
        type = itemType;
    }
    int type = 0;
};