#pragma once

#include "hk/diag/diag.h"

#include <experimental/memory>

#include "Multiplayer/main.hpp"
#include "Multiplayer/packets/AbilityUnlock.h"
#include "Multiplayer/packets/ActorSync.h"
#include "Multiplayer/packets/AddCoin.h"
#include "Multiplayer/packets/CaptureInf.h"
#include "Multiplayer/packets/CaptureUnlock.h"
#include "Multiplayer/packets/ChangeStagePacket.h"
#include "Multiplayer/packets/CheckpointGet.h"
#include "Multiplayer/packets/CoinCollectCollect.h"
#include "Multiplayer/packets/CoinCollectUse.h"
#include "Multiplayer/packets/CostumeInf.h"
#include "Multiplayer/packets/GameInf.h"
#include "Multiplayer/packets/GameStart.h"
#include "Multiplayer/packets/HackCapInf.h"
#include "Multiplayer/packets/InitPacket.h"
#include "Multiplayer/packets/MoonRockHit.h"
#include "Multiplayer/packets/MoonRockKeyUnlock.h"
#include "Multiplayer/packets/Packet.h"
#include "Multiplayer/packets/PaintingUnlock.h"
#include "Multiplayer/packets/PlayerConnect.h"
#include "Multiplayer/packets/PlayerDC.h"
#include "Multiplayer/packets/PlayerDeath.h"
#include "Multiplayer/packets/PlayerInfPacket.h"
#include "Multiplayer/packets/RoomJoin.h"
#include "Multiplayer/packets/RoomProtocol.h"
#include "Multiplayer/packets/ShineCollect.h"
#include "Multiplayer/packets/SubCoin.h"

namespace PacketFactory {
    inline Packet* create(PacketType type) {
        switch (type) {
        case PacketType::CLIENTINIT:
            return new (gHeap) InitPacket;

        case PacketType::PLAYERINF:
            return new (gHeap) PlayerInf;

        case PacketType::HACKCAPINF:
            return new (gHeap) HackCapInf;

        case PacketType::GAMEINF:
            return new (gHeap) GameInf;

        case PacketType::PLAYERCON:
            return new (gHeap) PlayerConnect;

        case PacketType::PLAYERDC:
            return new (gHeap) PlayerDC;

        case PacketType::COSTUMEINF:
            return new (gHeap) CostumeInf;

        case PacketType::SHINECOLL:
            return new (gHeap) ShineCollect;

        case PacketType::CAPTUREINF:
            return new (gHeap) CaptureInf;

        case PacketType::CHANGESTAGE:
            return new (gHeap) ChangeStagePacket;

        case PacketType::COINCOLLECTCOLL:
            return new (gHeap) CoinCollectCollect;

        case PacketType::CHECKPOINTGET:
            return new (gHeap) CheckpointGet;

        case PacketType::MOONROCKHIT:
            return new (gHeap) MoonRockHit;

        case PacketType::GAMESTART:
            return new (gHeap) GameStart;

        case PacketType::ROOMJOIN:
            return new (gHeap) RoomJoin;

        case PacketType::ABILITYUNLOCK:
            return new (gHeap) AbilityUnlock;

        case PacketType::CAPTUREUNLOCK:
            return new (gHeap) CaptureUnlock;

        case PacketType::COINCOLLECTUSE:
            return new (gHeap) CoinCollectUse;

        case PacketType::PROTOCOLHELLO:
            return new (gHeap) ProtocolHello;
        case PacketType::PROTOCOLRESULT:
            return new (gHeap) ProtocolResult;
        case PacketType::ROOMSTATUS:
            return new (gHeap) RoomStatus;
        case PacketType::SNAPSHOTBEGIN:
            return new (gHeap) SnapshotBegin;
        case PacketType::SNAPSHOTFILE:
            return new (gHeap) SnapshotFile;
        case PacketType::SNAPSHOTCHUNK:
            return new (gHeap) SnapshotChunk;
        case PacketType::SNAPSHOTCOMMIT:
            return new (gHeap) SnapshotCommit;
        case PacketType::SNAPSHOTREQUEST:
            return new (gHeap) SnapshotRequest;
        case PacketType::SHAREDEVENT:
            return new (gHeap) SharedEvent;
        case PacketType::SHAREDBASELINE:
            return new (gHeap) SharedBaseline;
        case PacketType::COINRESERVEREQUEST:
            return new (gHeap) CoinReserveRequest;
        case PacketType::COINRESERVERESULT:
            return new (gHeap) CoinReserveResult;
        case PacketType::COINRESERVECOMMIT:
            return new (gHeap) CoinReserveCommit;
        case PacketType::COINRESERVERELEASE:
            return new (gHeap) CoinReserveRelease;
        case PacketType::DEATHLINK:
            return new (gHeap) DeathLink;
        case PacketType::ROOMSETTINGS:
            return new (gHeap) RoomSettings;
        case PacketType::TEAMSELECT:
            return new (gHeap) TeamSelect;
        case PacketType::TEAMROSTER:
            return new (gHeap) TeamRoster;

        case PacketType::ADDCOIN:
            return new (gHeap) AddCoin;

        case PacketType::SUBCOIN:
            return new (gHeap) SubCoin;

        case PacketType::PLAYERDEATH:
            return new (gHeap) PlayerDeath;

        case PacketType::MOONROCKKEYUNLOCK:
            return new (gHeap) MoonRockKeyUnlock;

        case PacketType::PAINTINGUNLOCK:
            return new (gHeap) PaintingUnlock;
        case PacketType::ACTORSYNC:
            return new (gHeap) ActorSync;
        case PacketType::STATEVECTOR:
            return new (gHeap) StateVector;

        default:
            hk::diag::logLine("Invalid packet type in factory. Dropping packet.");
            return nullptr;
        }
    }
} // namespace PacketFactory
