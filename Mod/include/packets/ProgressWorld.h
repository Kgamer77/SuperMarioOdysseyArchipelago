#pragma once

#include "Packet.h"

struct PACKED ProgressWorld : Packet {
    ProgressWorld() : Packet() {
        this->mType = PacketType::PROGRESS;
        mPacketSize = sizeof(ProgressWorld) - sizeof(Packet);
    };
    int worldID = 0;
    short scenario = -1;
};