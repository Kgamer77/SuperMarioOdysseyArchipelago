#include "Multiplayer/server/SocketClient.hpp"

#include "hk/diag/diag.h"

#include "nn/os.h"
#include "nn/socket.h"
#include "vapours/results/results_common.hpp"

#include "sead/heap/seadHeapMgr.h"

#include "al/Library/Thread/AsyncFunctorThread.h"
#include "al/Library/Thread/FunctorV0M.h"

#include <cerrno>
#include <experimental/memory>
#include <experimental/utility>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/RoomSnapshot.h"
#include "Multiplayer/ServerConfig.h"
#include "Multiplayer/SocketBase.hpp"
#include "Multiplayer/main.hpp"
#include "Multiplayer/packets/Packet.h"
#include "Multiplayer/packets/PacketFactory.h"
#include "Multiplayer/packets/PlayerConnect.h"
#include "Multiplayer/server/Client.hpp"

using namespace nn;

// The mod compiles against musl's <sys/socket.h>, whose SOL_SOCKET/SO_* values
// are the *Linux* ones. Horizon's nn::socket is a BSD stack using the BSD
// values, so every option below was silently reinterpreted: SOL_SOCKET(1)
// landed as IPPROTO_IP, and SO_REUSEADDR(2)/SO_REUSEPORT(15)/SO_SNDTIMEO(21)/
// SO_RCVTIMEO(20) as IP_HDRINCL/IP_RSVP_ON/IP_IPSEC_POLICY/IP_RECVIF ->
// ENOPROTOOPT, ignored return value, option never actually set. TCP_NODELAY
// was unaffected only because IPPROTO_TCP(6)/TCP_NODELAY(1) happen to agree
// between Linux and BSD. Spelled out here rather than #undef-ing the musl
// macros so the two constant namespaces stay visibly distinct.
static constexpr s32 cHosSolSocket = 0xFFFF;
static constexpr s32 cHosSoReuseAddr = 0x0004;
static constexpr s32 cHosSoReusePort = 0x0200;
// Unused on purpose: the send/recv timeouts these belong to are deliberately
// not set (see exeInit). Kept because they are the CORRECT BSD values and
// whatever eventually bounds a stalled socket will want them.
[[maybe_unused]] static constexpr s32 cHosSoSndTimeo = 0x1005;
[[maybe_unused]] static constexpr s32 cHosSoRcvTimeo = 0x1006;

// BSD MSG_DONTWAIT. NOT musl's MSG_DONTWAIT (0x40) - on BSD 0x40 is
// MSG_WAITALL, so passing the Linux constant here would ask recv to block until
// the buffer is FULL: the exact opposite of the intent, and strictly worse than
// the blocking call it replaces. Same Linux/BSD split that silently broke the
// socket options above, spelled out for the same reason.
static constexpr s32 cHosMsgDontWait = 0x80;

// How long recvFunc() idles when nothing is waiting, in the non-blocking mode
// only. Small enough to stay well inside a frame, large enough not to spin.
static constexpr s64 cRecvIdleSleepMs = 2;

SocketClient::SocketClient()
    : SocketBase("SocketClient") {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    mRecvQueue.allocate(100, gHeap);
    // Deeper than the snapshot queue and separately allocated: an event
    // backlog is rare, but when it happens (a stage load stalling the consumer
    // while the other player collects a handful of things) every slot is a
    // moon that would otherwise be lost.
    mEventRecvQueue.allocate(256, gHeap);
    mSendQueue.allocate(100, gHeap);

    mSocketThread = new al::AsyncFunctorThread(
        "SocketMainThread", al::FunctorV0M(this, &SocketClient::update), 0, 16_KB, sead::CoreId::cMain);
    mRecvThread = new al::AsyncFunctorThread(
        "SocketRecvThread", al::FunctorV0M(this, &SocketClient::recvFunc), 0, 16_KB, sead::CoreId::cMain);
    mSendThread = new al::AsyncFunctorThread(
        "SocketSendThread", al::FunctorV0M(this, &SocketClient::sendFunc), 0, 16_KB, sead::CoreId::cMain);
}

void SocketClient::update() {
    // didnt use al nerves to be safe but idk maybe that couldve worked too
    sead::ScopedCurrentHeapSetter setter(gHeap);

    while (true) {
        switch (mState) {
        case WAIT:
            break;
        case INIT:
            exeInit();
            break;
        case RESET:
            exeReset();
            break;
        case RECONNECT:
            Client::instance()->startThread();
            break;
        }
        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }
}

void SocketClient::init(const char* ip, u16 port) {
    mSockIp = ip;
    mPort = port;

    if (mSocketThread->isDone())
        mSocketThread->start();

    mState = INIT;
}

bool SocketClient::exeInit() {
    hk::diag::logLine("socket client init");

    in_addr hostAddress = { 0 };
    sockaddr_in serverAddress = { 0 };

    hk::diag::logLine("SocketClient::exeInit: %s:%d sock %s", getIP(), mPort, getStateChar());

    if (!stringToIPAddress(getIP(), &serverAddress.sin_addr)) {
        hk::diag::logLine("IP address is invalid or hostname not resolveable.");
        mSockErrno = socket::GetLastErrno();
        mSockState = SockState::INVALIP;

        mState = WAIT;
        return false;
    }

    serverAddress.sin_port = socket::InetHtons(mPort);
    serverAddress.sin_family = socket::InetHtons(AF_INET);

    for (s32 fails = 0; fails <= 20; fails++) {
        if (fails == 20) {
            hk::diag::logLine("Socket Unavailable.");
            mSockErrno = socket::GetLastErrno();
            mSockState = SockState::UNAVAILABLE;

            mState = WAIT;
            return false;
        }

        if ((mSockFd = socket::Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
            hk::diag::logLine("Failed to create Socket");
            os::YieldThread();
            os::SleepThread(TimeSpan::FromMilliSeconds(500));
            continue;
        }

        s32 optValue = 1;

        // TCP_NODELAY is a TCP-level option. Passing SOL_SOCKET caused HOS
        // emulators to decode it as an unrelated IP option and report
        // ENOPROTOOPT; real BSD implementations reject the same pairing.
        socket::SetSockOpt(mSockFd, IPPROTO_TCP, TCP_NODELAY, &optValue, sizeof(optValue));
        socket::SetSockOpt(mSockFd, cHosSolSocket, cHosSoReuseAddr, &optValue, sizeof(optValue));
        socket::SetSockOpt(mSockFd, cHosSolSocket, cHosSoReusePort, &optValue, sizeof(optValue));

        // DELIBERATELY NOT SETTING SO_SNDTIMEO / SO_RCVTIMEO.
        //
        // There used to be a `timeval sockTimeout = { 15, 0 }` pair here, meant
        // to bound a stalled send()/recv() so a degraded connection would fail
        // cleanly into signalReset() -> reconnect instead of hanging a thread.
        // It never actually did anything: it was passed with musl's LINUX
        // SOL_SOCKET/SO_*TIMEO values, which Horizon's BSD stack rejected with
        // ENOPROTOOPT, and the return value was ignored. Every build that ever
        // shipped ran with no timeouts at all.
        //
        // Correcting the constants (see the block above) made these two live
        // for the first time - and immediately broke multiplayer outright on
        // Ryujinx: every connect succeeded, then the first recv() failed with
        // ETIMEDOUT ~15ms later, producing a connect/disconnect storm at the
        // socket retry interval. 15ms against an intended 15 SECONDS points at
        // the timeout payload being read as a plain millisecond count rather
        // than a timeval, so tv_sec=15 arrived as 15ms.
        //
        // Restoring "no timeouts" is the behaviour every working build has had.
        // If the hang this was aimed at needs solving, it needs a mechanism
        // that does not depend on how the host decodes a timeval - a
        // non-blocking socket with an explicit poll, most likely - not this.
        // Note that recv()'s EAGAIN/EWOULDBLOCK retry path is unreachable again
        // as a result; it always was.

        if (socket::Connect(mSockFd, (sockaddr*)&serverAddress, sizeof(serverAddress)).IsSuccess())
            break;

        hk::diag::logLine("Failed to connect to server");

        // shutdown and close so we dont create millions of fds
        socket::Shutdown(mSockFd, SHUT_RDWR);
        socket::Close(mSockFd);
        // Mark it gone. exeReset() -> closeSocket() runs after this on the
        // failure path and would otherwise spend its full retry budget tearing
        // down an fd that is already closed.
        mSockFd = -1;

        // different error than the one above
        if (fails == 19) {
            hk::diag::logLine("Socket Connection Failed!");
            mSockErrno = socket::GetLastErrno();
            mSockState = SockState::CONNFAIL;

            mState = WAIT;
            return false;
        }

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(500));
    }

    mSockState = SockState::CONNECTED;

    // OPT-IN, off by default - see ServerConfig::useNonBlockingRecv().
    //
    // On Ryujinx a blocking recv outstanding on this socket also blocks a
    // concurrent socket::Send on the SAME fd from the send thread, which never
    // returns. Traced end to end: the initial PlayerConnect goes out fine
    // because exeInit() sends it BEFORE startThreads(), and the very next
    // packet - the first through the send queue - hangs forever in
    // socket::Send. It is not just the handshake; every gameplay packet uses
    // that queue, so a parked recv means nothing can be sent at all.
    //
    // Real hardware does NOT have this problem - multiplayer works there today
    // with the blocking path - so hardware stays on it untouched and only a
    // card that opts in changes behaviour. recv()'s EAGAIN/EWOULDBLOCK retry
    // path was already written for this and has never once fired; the opt-in is
    // what makes it real.
    if (ServerConfig::useNonBlockingRecv()) {
        set_sock_flags(cHosMsgDontWait);
        MpDebugLog::write("SocketClient::exeInit: non-blocking recv enabled (emulator workaround)");
    }

    hk::diag::logLine("Socket fd: %d", mSockFd);

    auto initPacket = new (gHeap) PlayerConnect;

    initPacket->mUserID = Client::getClientId();
    initPacket->clientName = Client::getUsername();

    mLastConnectWasReconnect = !mIsFirstConnect;
    initPacket->conType = mIsFirstConnect ? ConnectionTypes::INIT : ConnectionTypes::RECONNECT;
    mIsFirstConnect = false;

    if (send(initPacket)) {
        mState = WAIT;
        startThreads();
        return true;
    }

    return false;
}

void SocketClient::exeReset() {
    closeSocket();

    os::YieldThread();
    os::SleepThread(TimeSpan::FromMilliSeconds(100));

    // Free up all blocked threads
    mSendQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);
    mRecvQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);
    mEventRecvQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);

    while (!(mRecvThread->isDone() && mSendThread->isDone() && Client::isThreadDone())) {
        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }

    // clear send and recv queue (idk man)
    for (s32 i = 0; i < 100; i++) {
        delete reinterpret_cast<Packet*>(mSendQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
        delete reinterpret_cast<Packet*>(mRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
    }
    for (s32 i = 0; i < 256; i++)
        delete reinterpret_cast<Packet*>(mEventRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));

    mState = RECONNECT;
}

void SocketClient::startThreads() {
    if (mRecvThread->isDone())
        mRecvThread->start();
    if (mSendThread->isDone())
        mSendThread->start();
}

bool SocketClient::send(Packet* packet) {
    if (mSockState != SockState::CONNECTED) {
        hk::diag::logLine("Unable To Send! Socket Not Connected.");
        mSockErrno = socket::GetLastErrno();

        delete packet;
        return false;
    }

    PacketVector packetData = packet->serialize();

    if (packet->mType != PLAYERINF && packet->mType != HACKCAPINF)
        hk::diag::logLine("Sending packet: %s", packetNames[packet->mType]);

    size valsent = 0;
    while (valsent < packetData.size()) {
        s32 result = socket::Send(mSockFd, packetData.data() + valsent, packetData.size() - valsent, 0);

        mSockErrno = socket::GetLastErrno();

        if (result <= 0) {
            hk::diag::logLine(
                "Packet send failed! Packet type is %hd. Sent %d this iteration, %zu so far, out of %zu.",
                packet->mType, result, valsent, packetData.size());

            delete packet;
            return false;
        }

        valsent += result;
    }

    delete packet;
    return true;
}

bool SocketClient::recv() {
    if (mSockState != SockState::CONNECTED) {
        hk::diag::logLine("Unable To Receive! Socket Not Connected.");
        mSockErrno = socket::GetLastErrno();
        return false;
    }

    PacketVector packetData(sHeaderSize);
    size valread = 0;
    s32 retries = 0;

    // read only the size of a header
    while (valread < sHeaderSize) {
        s32 result = socket::Recv(mSockFd, packetData.data() + valread, sHeaderSize - valread, mSockFlags);

        mSockErrno = socket::GetLastErrno();

        if (result <= 0) {
            if (mSockErrno == EAGAIN || mSockErrno == EWOULDBLOCK) {
                if (valread == 0) {
                    // Nothing waiting. recvFunc() calls straight back in here,
                    // so without this the non-blocking mode would turn the read
                    // loop into a busy spin on a shared core. Unreachable in
                    // the default blocking mode, which never returns EAGAIN.
                    nn::os::YieldThread();
                    nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(cRecvIdleSleepMs));
                    return true;
                }

                if (++retries > 100) {
                    hk::diag::logLine("Timed out trying to get remaining packet header data!");
                    return false;
                }

                nn::os::YieldThread();
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(10));
                continue;
            }

            hk::diag::logLine("Header Read Failed! Value: %d Total Read: %zu", result, valread);
            return false;
        }

        valread += result;
    }

    PacketHeader header;
    header.deserialize(packetData);

    if (header.mIsFail) {
        hk::diag::logLine("The header failed to deserialize properly.");
        return false;
    }

    if (header.mPacketSize < 0 || header.mPacketSize > MAXPACKSIZE) {
        hk::diag::logLine("Packet is too big! Size: %hd", header.mPacketSize);
        return false;
    }

    if (header.mType <= PacketType::UNKNOWN || header.mType >= PacketType::End) {
        hk::diag::logLine("Packet has invalid type: %hd", header.mType);
        return false;
    }

    if (header.mType != PLAYERINF && header.mType != HACKCAPINF) {
        hk::diag::logLine("Received packet (from %lu%lu): Size: %hd Type: %hd%s%s",
            header.mUserID.m_Storage[0], header.mUserID.m_Storage[1], header.mPacketSize,
            header.mType, packetNames[header.mType] ? " Type String: " : "",
            packetNames[header.mType] ? packetNames[header.mType] : "");
    }

    packetData.resize(sHeaderSize + header.mPacketSize);
    valread = 0;
    retries = 0;

    while (valread < header.mPacketSize) {
        s32 result = socket::Recv(mSockFd, packetData.data() + sHeaderSize + valread,
            header.mPacketSize - valread, mSockFlags);

        mSockErrno = socket::GetLastErrno();

        if (result <= 0) {
            if (mSockErrno == EAGAIN || mSockErrno == EWOULDBLOCK) {
                if (++retries > 100) {
                    hk::diag::logLine("Timed out trying to get remaining packet body data!");
                    return false;
                }

                nn::os::YieldThread();
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(10));
                continue;
            }

            hk::diag::logLine("Body Read Failed! Value: %d Total Read: %zu", result, valread);
            return false;
        }

        // Progress resets the budget: the 100-retry ceiling exists to catch a
        // stalled peer, not to cap how long a large but steadily-arriving body
        // (a snapshot chunk) may take. Only reachable in non-blocking mode.
        retries = 0;
        valread += result;
    }

    // unused in SR so just ignore
    if (header.mType == TAGINF)
        return true;

    auto packet = PacketFactory::create(header.mType);

    if (packet)
        packet->deserialize(packetData);
    else {
        hk::diag::logLine("Factory couldn't produce packet of type %d", header.mType);
        return false;
    }

    if (packet->mIsFail) {
        hk::diag::logLine("The packet failed to deserialize properly.");

        delete packet;
        return false;
    }

    // Room snapshot traffic never reaches mRecvQueue: it is handled right here
    // on this thread instead. A SnapshotChunk is ~4.2 KB and gHeap is 512 KB
    // in total, so the 100-slot queue below could hold ~415 KB of a streaming
    // room save and exhaust the heap - and doing the SD write on this thread
    // rather than a consumer's is also the only backpressure the protocol has.
    // Both reasons are spelled out at RoomSnapshot::tryHandlePacket().
    if (RoomSnapshot::tryHandlePacket(packet)) {
        delete packet;
        return true;
    }

    s64 ptr = reinterpret_cast<s64>(packet);

    // Route by consequence-of-loss, not by size or frequency.
    //
    // PLAYERINF and HACKCAPINF are re-sent every few frames and each one
    // supersedes the last, so a dropped one costs a fraction of a second of
    // interpolation. Everything else - SHINECOLL, COINCOLLECTCOLL/USE,
    // CHECKPOINTGET, MOONROCKHIT, ABILITYUNLOCK, CAPTUREUNLOCK, SHAREDEVENT,
    // CAPTUREINF, and the room/protocol traffic - is edge-triggered and sent
    // exactly once. Losing one of those is a moon the other player never gets.
    //
    // They therefore go to different queues: the snapshot queue may drop under
    // pressure, the event queue may not, and a flood of position updates can
    // no longer evict or delay a single moon.
    const bool isSupersededSnapshot
        = header.mType == PacketType::PLAYERINF || header.mType == PacketType::HACKCAPINF;

    if (!isSupersededSnapshot) {
        if (!mEventRecvQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking)) {
            // 256 slots of one-shot events behind an unresponsive consumer.
            // Wait rather than discard: this is the receive thread, so waiting
            // only delays reading more of the socket, which is exactly what
            // TCP back-pressure is for. Bounded so a genuinely wedged consumer
            // cannot hang this thread forever.
            constexpr s32 cMaxQueueWaitTurns = 1000;
            bool delivered = false;
            for (s32 i = 0; i < cMaxQueueWaitTurns && !delivered; i++) {
                nn::os::YieldThread();
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(1));
                delivered
                    = mEventRecvQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking);
            }

            if (!delivered) {
                MpDebugLog::write(
                    "SocketClient::recv: LOST EVENT PACKET type=%d - event queue still full after %d ms",
                    header.mType, cMaxQueueWaitTurns);
                delete packet;
                return true;
            }

            MpDebugLog::write(
                "SocketClient::recv: event queue was full, packet type=%d delivered after a wait",
                header.mType);
        }

        // Wake a consumer parked on the blocking pop of the snapshot queue.
        // Zero is already this queue's established "wake up, nothing here"
        // sentinel (exeReset uses it), and tryGetPacket answers a null pop by
        // re-checking the event queue. Failing to push it is harmless: a full
        // snapshot queue means the consumer is about to be woken anyway.
        mRecvQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);
        return true;
    }

    // NonBlocking push() silently drops (returns false) instead of blocking
    // when the queue (capacity 100) is full - if the consumer thread
    // (Client::readFunc(), a separate AsyncFunctorThread) ever falls behind
    // that far, the packet this just heap-allocated becomes permanently
    // unreachable: never freed, never delivered, never logged. Confirmed
    // real gHeap-leak gap, not just a theoretical one - explicitly delete it
    // on the drop path instead of leaking it.
    if (!mRecvQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking)) {
        // Snapshot only, by the routing above. A full queue here is ordinary
        // back-pressure and the next frame's packet supersedes this one, so
        // drop it and keep the connection: returning false made recvFunc tear
        // down an otherwise healthy room connection during expensive stage
        // setup, just when the consumer was briefly unable to drain.
        MpDebugLog::write("SocketClient::recv: recv queue full, dropping snapshot packet type=%d",
            header.mType);
        delete packet;
        return true;
    }

    return true;
}

void SocketClient::closeSocket() {
    hk::diag::logLine("Closing Socket.");
    MpDebugLog::write("SocketClient::closeSocket: deliberate close requested");

    mSockState = SockState::DISCONNECTED;

    // Nothing to tear down: either no socket was ever opened, or exeInit()'s
    // own failure path already closed this fd. Both retry loops below would
    // otherwise run their full 20 x 100ms against a dead descriptor - 4 seconds
    // of blocking, and a wall of EBADF in the log, on every failed connect
    // cycle. Confirmed against a live capture of exactly that spam.
    if (mSockFd < 0) {
        MpDebugLog::write("SocketClient::closeSocket: fd already invalid, nothing to close");
        return;
    }

    s32 fails = 0;
    for (fails = 0; fails <= 20; fails++) {
        if (fails == 20)
            hk::diag::logLine("Failed to shutdown socket!");

        // Shutdown to unblock read func
        if (socket::Shutdown(mSockFd, SHUT_RDWR) == 0)
            break;

        // EBADF means the descriptor is gone; ENOTCONN means there is no
        // connection left to shut down. Neither can change by trying again, so
        // sleeping 100ms and retrying 20 times just delays the reconnect.
        const s32 shutdownErr = socket::GetLastErrno();
        if (shutdownErr == EBADF || shutdownErr == ENOTCONN)
            break;

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }

    for (fails = 0; fails <= 20; fails++) {
        if (fails == 20)
            hk::diag::logLine("Failed to close socket!");

        if (socket::Close(mSockFd).IsSuccess())
            break;

        if (socket::GetLastErrno() == EBADF)
            break;

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }

    mSockFd = -1;
}

bool SocketClient::stringToIPAddress(const char* str, in_addr* out) {
    // string to IPv4
    if (socket::InetAton(str, out)) {
        return true;
    }

    // get IPs via DNS
    hostent* he = socket::GetHostByName(str);
    if (!he) {
        return false;
    }

    // might give us multiple IP addresses, so pick the first one
    in_addr** addr_list = (in_addr**)he->h_addr_list;
    if (addr_list[0]) {
        *out = *addr_list[0];
        return true;
    }

    return false;
}

void SocketClient::sendFunc() {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    hk::diag::logLine("Starting Send Thread.");
    while (mSockState != SockState::DISCONNECTED && trySendQueue()) {
    }

    mSockState = SockState::DISCONNECTED;

    // hk::diag::logLine only reaches a live debug console - it's gone by the
    // time anyone notices a disconnect after the fact. This is the one exit
    // point for the send thread regardless of cause (send failure, or
    // closeSocket() called deliberately elsewhere setting DISCONNECTED out
    // from under the loop), so one persisted line here covers both: errno=0
    // with no preceding "Closing Socket." line in this same log points at an
    // unexpected drop, not a deliberate one.
    MpDebugLog::write("SocketClient::sendFunc: send thread ending, errno=%d", mSockErrno);
    hk::diag::logLine("Sending packet failed!");
    hk::diag::logLine("Ending Send Thread.");

    signalReset();
}

void SocketClient::recvFunc() {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    // ??? - purpose unknown, inherited. Left in place, but now issued with the
    // socket's own flags rather than a hardcoded 0, so it does not stay a
    // BLOCKING receive when the rest of the loop is non-blocking. Identical to
    // the old call in the default mode, where mSockFlags is 0.
    socket::Recv(mSockFd, nullptr, 0, mSockFlags);

    hk::diag::logLine("Starting Recv Thread.");

    while (mSockState != SockState::DISCONNECTED && recv()) {
    }

    mSockState = SockState::DISCONNECTED;

    // See the matching comment in sendFunc() - same reasoning, other thread.
    MpDebugLog::write("SocketClient::recvFunc: recv thread ending, errno=%d", mSockErrno);
    hk::diag::logLine("Receiving Packet Failed!");
    hk::diag::logLine("Ending Recv Thread.");

    signalReset();
}

bool SocketClient::queuePacket(Packet* packet) {
    if (mSockState == SockState::CONNECTED) {
        s64 ptr = reinterpret_cast<s64>(packet);
        // Same NonBlocking-drop gap as recv() above - a full send queue
        // (capacity 100) previously leaked the packet silently instead of
        // freeing it, and misreported success to the caller besides.
        if (!mSendQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking)) {
            MpDebugLog::write("SocketClient::queuePacket: send queue full, dropping packet");
            delete packet;
            return false;
        }

        return true;
    }

    delete packet;

    return false;
}

bool SocketClient::trySendQueue() {
    Packet* packet = reinterpret_cast<Packet*>(mSendQueue.pop(sead::MessageQueue::BlockType::Blocking));
    if (!packet) {
        hk::diag::logLine("SocketClient::trySendQueue: packet was nullptr");
        return false;
    }

    return send(packet);
}

Packet* SocketClient::tryGetPacket() {
    // A null return means ONE thing to every caller: the connection is gone.
    // Client::readFunc breaks its loop and calls signalReset on it, and
    // Client::startConnection aborts the handshake on it. So this must never
    // hand back null while the socket is still healthy - which is exactly what
    // a wake sentinel would do if it were allowed to escape, producing a
    // connect/join/drop loop the moment the first event packet arrived.
    //
    // Hence the loop: keep going until there is a real packet or the socket
    // actually leaves CONNECTED (exeReset flips that state, so its own unblock
    // push still ends the wait as before).
    while (mSockState == SockState::CONNECTED) {
        // Events first, unconditionally. A one-shot moon must never wait
        // behind a backlog of position snapshots.
        if (Packet* event = reinterpret_cast<Packet*>(
                mEventRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking)))
            return event;

        if (Packet* packet = reinterpret_cast<Packet*>(
                mRecvQueue.pop(sead::MessageQueue::BlockType::Blocking)))
            return packet;

        // Zero: a wake sentinel, not a packet. recv() pushes one whenever it
        // queues an event, so this is the normal path for every non-snapshot
        // packet - go round and pick that event up off the event queue.
    }

    return nullptr;
}

Packet* SocketClient::tryGetPacketTimeout(s32 timeoutMs) {
    // Polls NonBlocking rather than waiting on the queue, so losing the
    // connection mid-wait (mSockState flipping to DISCONNECTED) ends the wait
    // on the next tick instead of needing exeReset()'s unblock push to arrive.
    constexpr s32 cPollIntervalMs = 10;
    for (s32 waited = 0; waited < timeoutMs; waited += cPollIntervalMs) {
        if (mSockState != SockState::CONNECTED)
            return nullptr;

        Packet* packet = reinterpret_cast<Packet*>(
            mEventRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
        if (!packet)
            packet = reinterpret_cast<Packet*>(
                mRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
        if (packet)
            return packet;

        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(cPollIntervalMs));
    }

    return nullptr;
}
