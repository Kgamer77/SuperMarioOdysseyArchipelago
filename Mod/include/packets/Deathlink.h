#pragma once

#include "Packet.h"

struct PACKED Deathlink : Packet {
    Deathlink() : Packet() {
        this->mType = PacketType::DEATHLINK;
        mPacketSize = sizeof(Deathlink) - sizeof(Packet);
    };
};