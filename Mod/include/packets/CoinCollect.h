
#pragma once

#include "Packet.h"


struct PACKED ItemCollect : Packet {
    ItemCollect() : Packet() {this->mType = PacketType::ITEMCOLL; mPacketSize = sizeof(ItemCollect) - sizeof(Packet);};
    ItemCollect(const char* itemName, int itemType) : Packet() {
        this->mType = PacketType::ITEMCOLL;
        mPacketSize = sizeof(ItemCollect) - sizeof(Packet);
        strcpy(name, itemName);
        type = itemType;
    }
    char name[ITEMNAMESIZE] = {};
    int type = 0;
};