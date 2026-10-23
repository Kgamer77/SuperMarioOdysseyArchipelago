#pragma once

#include "sead/thread/seadAtomic.h"
#include "sead/thread/seadMessageQueue.h"

#include "al/Library/Thread/AsyncFunctorThread.h"

#include <experimental/memory>
#include <netinet/in.h>

#include "Multiplayer/SocketBase.hpp"
#include "Multiplayer/packets/Packet.h"

class SocketClient : public SocketBase {
public:
    enum SocketClientState
    {
        WAIT = 0,
        INIT = 1,
        RESET = 2,
        RECONNECT = 3
    };

    SocketClient();

    void update();
    bool exeInit();
    void exeReset();

    void init(const char* ip, u16 port);
    void closeSocket();
    Packet* tryGetPacket();

    // tryGetPacket() pops BLOCKING and unbounded: if the peer never sends
    // anything it parks the calling thread forever with no error state set
    // anywhere. That is survivable for the steady-state read loop, which has
    // nothing else to do, but NOT for the connect handshake - a server that
    // accepts the TCP connection and then says nothing left the client wedged
    // reading a reply that was never coming, still reporting "connected",
    // with no error for any caller to surface. Confirmed live: the handshake
    // stopped dead after CLIENTINIT and only the room-assignment wait 30
    // seconds later noticed anything was wrong.
    //
    // Returns nullptr if nothing arrived within timeoutMs.
    Packet* tryGetPacketTimeout(s32 timeoutMs);

    void startThreads();

    bool send(Packet* packet);
    bool recv();

    bool queuePacket(Packet* packet);
    bool trySendQueue();

    void sendFunc();
    void recvFunc();

    void setSockState(SockState state) { mSockState = state; };

    bool isConnected() { return mSockState == SockState::CONNECTED; }

    void resetFirstConnect() { mIsFirstConnect = true; }
    bool wasReconnect() const { return mLastConnectWasReconnect; }
    void signalReset() {
        if (mState == WAIT)
            mState = RESET;
    }

    u32 getSendCount() { return mSendQueue.mMessageQueueInner._count; }
    u32 getSendMaxCount() { return mSendQueue.mMessageQueueInner._maxCount; }

    u32 getRecvCount() { return mRecvQueue.mMessageQueueInner._count; }
    u32 getRecvMaxCount() { return mRecvQueue.mMessageQueueInner._maxCount; }

    u32 getEventRecvCount() { return mEventRecvQueue.mMessageQueueInner._count; }
    u32 getEventRecvMaxCount() { return mEventRecvQueue.mMessageQueueInner._maxCount; }

    SocketClientState getSocketClientState() { return mState; }

private:
    al::AsyncFunctorThread* mSocketThread = nullptr;
    al::AsyncFunctorThread* mRecvThread = nullptr;
    al::AsyncFunctorThread* mSendThread = nullptr;

    // Two receive queues, because the two kinds of traffic have opposite
    // failure modes.
    //
    // mRecvQueue carries position snapshots (PLAYERINF, HACKCAPINF). They
    // arrive every few frames and each supersedes the last, so dropping one
    // when the consumer falls behind costs a fraction of a second of
    // interpolation. Dropping them is the correct back-pressure.
    //
    // mEventRecvQueue carries everything else - moons, coins, checkpoints,
    // ability and capture unlocks, shared events, captures, room traffic.
    // Every one of those is edge-triggered and sent exactly once; dropping a
    // single one costs the other player that item permanently. They must never
    // queue behind, or be evicted by, a burst of position updates.
    sead::MessageQueue mRecvQueue;
    sead::MessageQueue mEventRecvQueue;
    sead::MessageQueue mSendQueue;

    bool mIsFirstConnect = true;
    bool mLastConnectWasReconnect = false;

    sead::Atomic<SocketClientState> mState = INIT;

    /**
     * @param str a string containing an IPv4 address or a hostname that can be resolved via DNS
     * @param out IPv4 address
     * @return if this function was successfull and out contains a valid IP address
     */
    bool stringToIPAddress(const char* str, in_addr* out);
};
