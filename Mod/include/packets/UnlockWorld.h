#pragma once

#include "Packet.h"

struct PACKED UnlockWorld : Packet {
    UnlockWorld() : Packet() {
        this->mType = PacketType::UNLOCKWORLD;
        mPacketSize = sizeof(UnlockWorld) - sizeof(Packet);
    };
    int worldID = 0;
};