#pragma once

#include "sead/basis/seadTypes.h"

#include "Packet.h"

enum TagUpdateType : u8 { TIME = 1 << 0, STATE = 1 << 1 };

struct PACKED TagInf : Packet {
    TagInf() : Packet() {
        this->mType = PacketType::TAGINF;
        mPacketSize = sizeof(TagInf) - sizeof(Packet);
    };
    TagUpdateType updateType;
    bool1 isIt = false;
    u8 seconds;
    u16 minutes;
};