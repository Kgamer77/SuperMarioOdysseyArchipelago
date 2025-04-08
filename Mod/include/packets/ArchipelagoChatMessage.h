#pragma once

#include "Packet.h"

struct PACKED ArchipelagoChatMessage : Packet {
    ArchipelagoChatMessage() : Packet() {
        this->mType = PacketType::APCHATMESSAGE;
        mPacketSize = sizeof(ArchipelagoChatMessage) - sizeof(Packet);
    };
    char message1[APMESSAGESIZE] = {};
    char message2[APMESSAGESIZE] = {};
    char message3[APMESSAGESIZE] = {};
};