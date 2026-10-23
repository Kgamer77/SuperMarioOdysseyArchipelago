#pragma once

#include "hk/diag/diag.h"

#include "nn/account.h"

#include "sead/math/seadQuat.h" // IWYU pragma: keep
#include "sead/math/seadVector.h" // IWYU pragma: keep
#include "sead/prim/seadSafeString.h"

#include <cstring>
#include <vector>

#include "Multiplayer/main.hpp"

#define PACKBUFSIZE 0x30
#define COSTUMEBUFSIZE 0x20
#define MESSAGESIZE 0x4B

#define MAXPACKSIZE 0x1200

enum PacketType : short
{
    UNKNOWN,
    CLIENTINIT,
    PLAYERINF,
    HACKCAPINF,
    GAMEINF,
    TAGINF,
    PLAYERCON,
    PLAYERDC,
    COSTUMEINF,
    SHINECOLL,
    CAPTUREINF,
    CHANGESTAGE,
    CMD,
    COINCOLLECTCOLL,
    CHECKPOINTGET,
    MOONROCKHIT,
    GAMESTART,
    // Dedicated-server room support (see docs/multiplayer-database-guide.md) -
    // sent once right after CLIENTINIT to tag this connection with a room
    // code; the server only relays packets between clients sharing the same
    // room. Not part of the base SmoOnlineServer protocol (ordinals 0-12) -
    // both this mod's server fork and this packet only exist together.
    ROOMJOIN,
    // Purple-coin shop Ability/Capture purchases were never synced: unlike a
    // shop Moon (which routes through MoonHooks::grantMoonDirect ->
    // GameDataFunction::setGotShine -> mpOnSetGotShine, so it already sends
    // a ShineCollect), buying an Ability or Capture only wrote the local
    // save flag - the other player never found out. These two mirror that
    // shop-Moon path for the two reward kinds that didn't have one.
    ABILITYUNLOCK,
    CAPTUREUNLOCK,
    // Regional coin spending was never synced: collection already relays
    // via CoinCollectCollect, but a purchase only decremented the buyer's
    // own mUseCoinCollectNum locally. Since balance = got - used and got is
    // already shared, relaying used closes the loop into a real shared
    // pool.
    COINCOLLECTUSE,
    PROTOCOLHELLO,
    PROTOCOLRESULT,
    ROOMSTATUS,
    SNAPSHOTBEGIN,
    SNAPSHOTFILE,
    SNAPSHOTCHUNK,
    SNAPSHOTCOMMIT,
    SNAPSHOTREQUEST,
    SHAREDEVENT,
    SHAREDBASELINE,
    COINRESERVEREQUEST,
    COINRESERVERESULT,
    COINRESERVECOMMIT,
    COINRESERVERELEASE,
    DEATHLINK,
    ROOMSETTINGS,
    // Teams. TEAMSELECT is this client telling the server which team to put it
    // on (sent from the file select, before the snapshot request); TEAMROSTER
    // is the server's per-room answer, and the only source for the in-game
    // roster panel - a player hidden by the cross-team relay filter never
    // reaches the puppet array at all.
    TEAMSELECT,
    TEAMROSTER,
    // Regular (yellow) coin pickups were never synced: only the regional
    // (purple) coin pool was shared (see the COINRESERVE* protocol above).
    // This relays the plain coin-total delta so both players' HUD counts
    // stay in sync too.
    ADDCOIN,
    // Mirrors CoinCollectUse (regional coin spend) but for the plain global
    // coin total -- covers regular-coin shop purchases and other spend
    // paths (Fool/curse penalties, hint costs), same as ADDCOIN covers
    // GameDataFunction::addCoin.
    SUBCOIN,
    // Purely a Player Event Log signal - fired from the same "genuine Mario
    // death" trampoline Nuzlocke/TimerMode/DEATHLINK's Client::
    // onPlayerDeathDetected already answer to (missAndRestartStageHook in
    // EntranceHooks.cpp). The coin loss that comes with dying isn't carried
    // here - it already rides SUBCOIN like any other GameDataFunction::
    // subCoin call. Distinct from DEATHLINK above, which kills the OTHER
    // player when death-link mode is enabled; this is purely informational
    // and always fires regardless of that setting.
    PLAYERDEATH,
    // Dedicated, idempotent fast-path signals additive to the general
    // ShineCollect sync - see mpOnSetGotShine (syncHooks.cpp). Both
    // mechanics already eventually propagate through the normal moon-sync
    // pipeline, but that path is scene-readiness-gated and has a
    // documented history of occasionally missing a credit; these two
    // specifically gate real soft-locks (can't open a Moon Rock / can't
    // use a painting route) so they get their own reliable, unconditional
    // signal rather than relying on that pipeline alone.
    MOONROCKKEYUNLOCK,
    PAINTINGUNLOCK,
    // Generic per-actor sync (SyncedActor). Unlike every packet above, the
    // server does not understand this one's contents: the payload is an
    // opaque blob written by whichever SyncedActor subclass owns the actor,
    // and the server only relays it to the rest of the room. That is what
    // lets a new synced actor type ship without touching the server at all.
    ACTORSYNC,
    // Periodic "here is everything my save actually has" publication, and the
    // only mechanism that can recover progress lost BEFORE it reached the
    // server. See RoomProtocol.h's StateVector for the full reasoning.
    STATEVECTOR,
    End // end of enum for bounds checking
};

constexpr static const char* packetNames[] = { "Unknown", "Client Initialization",
    "Player Info", "Player Cap Info",
    "Game Info", "Tag Info",
    "Player Connect", "Player Disconnect",
    "Costume Info", "Moon Collection",
    "Capture Info", "Change Stage",
    "Server Command", "Regional Coin Collection",
    "Checkpoint Get", "Moon Rock Hit",
    "Game Start", "Room Join",
    "Ability Unlock", "Capture Unlock",
    "Regional Coin Use", "Protocol Hello", "Protocol Result",
    "Room Status", "Snapshot Begin", "Snapshot File", "Snapshot Chunk",
    "Snapshot Commit", "Snapshot Request", "Shared Event", "Shared Baseline",
    "Coin Reserve Request", "Coin Reserve Result", "Coin Reserve Commit",
    "Coin Reserve Release", "Death Link", "Room Settings", "Team Select",
    "Team Roster", "Coin Add", "Coin Sub", "Player Death",
    "Moon Rock Key Unlock", "Painting Unlock", "Actor Sync", "State Vector" };

static_assert(sizeof(packetNames) / sizeof(packetNames[0]) == PacketType::End,
    "Packet name table must match PacketType");

enum ConnectionTypes
{
    INIT,
    RECONNECT
};

// uid part 1 + uid part 2 + packet type + packet size
static constexpr s32 sHeaderSize = sizeof(u64) + sizeof(u64) + sizeof(PacketType) + sizeof(short);

template <typename T>
class PacketAllocator {
public:
    using value_type = T;

    PacketAllocator() noexcept = default;

    template <typename U>
    PacketAllocator(const PacketAllocator<U>& other) noexcept { }

    T* allocate(std::size_t n) {
        if (n == 0)
            return nullptr;
        void* ptr = gHeap->alloc(n * sizeof(T));
        if (!ptr)
            HK_ABORT("Crashed while allocating for a vector!");
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (p)
            gHeap->free(p);
    }

    bool operator==(const PacketAllocator& other) const noexcept = default;
};

using PacketVector = std::vector<u8, PacketAllocator<u8>>;

struct Packet {
    virtual ~Packet() = default;
    virtual PacketType getType() = 0;
    virtual PacketVector serialize() = 0;
    virtual void deserialize(const PacketVector& data) = 0;

    nn::account::Uid mUserID; // User ID of the packet owner
    PacketType mType = PacketType::UNKNOWN;
    short mPacketSize = 0; // represents packet size without size of header
    bool mIsFail = false;
};

class PacketWriter {
public:
    PacketWriter(Packet* packet)
        : mPacket(packet) {
        packet->mType = packet->getType();
        writeToFullData(packet->mUserID.m_Storage[0]);
        writeToFullData(packet->mUserID.m_Storage[1]);
        writeToFullData(packet->getType());
    }

    template <typename T>
    void writeToFullData(T value) {
        const u8* ptr = reinterpret_cast<const u8*>(&value);
        mFullData.insert(mFullData.end(), ptr, ptr + sizeof(T));
    }

    template <typename T>
    void write(T value) {
        const u8* ptr = reinterpret_cast<const u8*>(&value);
        mData.insert(mData.end(), ptr, ptr + sizeof(T));
    }

    template <s32 L>
    void writeString(sead::FixedSafeString<L>& str) {
        s32 len = str.calcLength();
        mData.insert(mData.end(), str.getStringTop(), str.getStringTop() + len);

        if (len < L)
            mData.insert(mData.end(), L - len, 0);
    }

    void writeBytes(const u8* data, size byteCount) {
        if (data && byteCount)
            mData.insert(mData.end(), data, data + byteCount);
    }

    PacketVector finalize() {
        u16 sizeNoHeader = mData.size();
        mPacket->mPacketSize = sizeNoHeader;
        writeToFullData(sizeNoHeader);
        mFullData.insert(mFullData.end(), mData.data(), mData.data() + mData.size());
        return std::move(mFullData);
    }

private:
    Packet* mPacket = nullptr;
    PacketVector mFullData;
    PacketVector mData;
    size mSizeOffset = 0;
};

class PacketReader {
public:
    PacketReader(Packet* packet, const u8* data, size size)
        : mPacket(packet)
        , mData(data)
        , mSize(size + sHeaderSize) {
        read(packet->mUserID.m_Storage[0]);
        read(packet->mUserID.m_Storage[1]);
        read(packet->mType);
        read(packet->mPacketSize);
    }

    template <typename T>
    void read(T& value) {
        if (mIsReadFail || mOffset + sizeof(T) > mSize) {
            hk::diag::logLine("Reading a value failed because it would go out of bounds. Offset: %zu Size: "
                              "%zu Type: %hd Type String: %s",
                mOffset, mSize, mPacket->mType, packetNames[mPacket->mType]);
            mIsReadFail = true;
            return;
        }

        memcpy(&value, &mData[mOffset], sizeof(T));
        mOffset += sizeof(T);
    }

    template <s32 L>
    void readString(sead::FixedSafeString<L>& str) {
        if (mIsReadFail || mOffset + L > mSize) {
            hk::diag::logLine(
                "Reading a string failed because it would go out of bounds. Offset: %zu Size %zu", mOffset,
                mSize);
            mIsReadFail = true;
            str = "";
            return;
        }

        const char* strData = reinterpret_cast<const char*>(&mData[mOffset]);

        str.copy(strData, L);
        mOffset += L;
    }

    void readBytes(u8* value, size byteCount) {
        if (mIsReadFail || !value || mOffset + byteCount > mSize) {
            mIsReadFail = true;
            return;
        }
        memcpy(value, &mData[mOffset], byteCount);
        mOffset += byteCount;
    }

    void finalize() { mPacket->mIsFail = mIsReadFail; }

private:
    Packet* mPacket = nullptr;
    const u8* mData = nullptr;
    size mSize = 0;
    size mOffset = 0;
    bool mIsReadFail = false;
};

// all packet types

// IWYU pragma: begin_keep
#include "Multiplayer/packets/AbilityUnlock.h"
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
// Out of alphabetical order on purpose: PacketFactory.h's create() switch
// references RoomProtocol.h's packet structs (ProtocolHello, RoomStatus,
// etc.), so RoomProtocol.h must finish parsing before PacketFactory.h does.
// In any translation unit that reaches this file THROUGH RoomProtocol.h
// itself (it #includes Packet.h at its own top), #pragma once would
// otherwise make RoomProtocol.h's second, real inclusion here a no-op while
// its types are still mid-definition, leaving them undeclared by the time
// PacketFactory.h needs them.
#include "Multiplayer/packets/PacketFactory.h"
#include "Multiplayer/packets/PacketHeader.h"
#include "Multiplayer/packets/PaintingUnlock.h"
#include "Multiplayer/packets/PlayerConnect.h"
#include "Multiplayer/packets/PlayerDC.h"
#include "Multiplayer/packets/PlayerDeath.h"
#include "Multiplayer/packets/PlayerInfPacket.h"
#include "Multiplayer/packets/RoomJoin.h"
#include "Multiplayer/packets/RoomProtocol.h"
#include "Multiplayer/packets/ShineCollect.h"
#include "Multiplayer/packets/SubCoin.h"
// IWYU pragma: end_keep
