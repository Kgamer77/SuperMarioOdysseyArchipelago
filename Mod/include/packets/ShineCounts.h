#pragma once

#include "Packet.h"

struct PACKED ShineCounts : Packet {
    ShineCounts() : Packet() {
        this->mType = PacketType::SHINECOUNTS;
        mPacketSize = sizeof(ShineCounts) - sizeof(Packet);
    };
    ushort clash = 0;
    ushort raid = 0;
    bool regionals = false;
};