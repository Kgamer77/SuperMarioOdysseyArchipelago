#include "Archipelago/WebSocketClient.hpp"

#include "hk/diag/diag.h"

#include "nn/os.h"
#include "nn/socket.h"
#include "vapours/results/results_common.hpp"

#include "sead/heap/seadHeapMgr.h"

#include "al/Library/Thread/AsyncFunctorThread.h"
#include "al/Library/Thread/FunctorV0M.h"

#include <cerrno>
#include <cstring>
#include <experimental/memory>
#include <experimental/utility>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>

#include "Archipelago/ArchipelagoClient.hpp"
#include "Archipelago/ArchipelagoSocket.h"
#include "Archipelago/TLSHooks.h"
#include "Archipelago/WebSocketDataFrameFactory.h"

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

WebSocketClient::WebSocketClient()
    : SocketBase("WebSocketClient") {
    // Define ArchipelagoSocket::sArchipelagoSocketHeap as a global variable in it's own main.hpp
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    mRecvQueue.allocate(100, ArchipelagoSocket::sArchipelagoSocketHeap);
    mSendQueue.allocate(100, ArchipelagoSocket::sArchipelagoSocketHeap);

    mSocketThread = new al::AsyncFunctorThread(
        "WebSocketMainThread", al::FunctorV0M(this, &WebSocketClient::update), 0, 16_KB, sead::CoreId::cMain);
    mRecvThread = new al::AsyncFunctorThread(
        "WebSocketRecvThread", al::FunctorV0M(this, &WebSocketClient::recvFunc), 0, 16_KB, sead::CoreId::cMain);
    mSendThread = new al::AsyncFunctorThread(
        "WebSocketSendThread", al::FunctorV0M(this, &WebSocketClient::sendFunc), 0, 16_KB, sead::CoreId::cMain);
}

void WebSocketClient::update() {
    // didnt use al nerves to be safe but idk maybe that couldve worked too
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

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
            ArchipelagoClient::instance()->startThread();
            break;
        case CONNECTING:
            break;
        case OPEN:
            break;
        case CLOSING:
            break;
        case CLOSED:
            break;
        }

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }
}

void WebSocketClient::init(const char* ip, u16 port) {
    mSockIp = ip;
    mPort = port;

    if (mSocketThread->isDone())
        mSocketThread->start();
    mState = INIT;
}

bool WebSocketClient::exeInit() {
    hk::diag::logLine("web socket client init");

    in_addr hostAddress = { 0 };
    sockaddr_in serverAddress = { 0 };

    hk::diag::logLine("WebSocketClient::exeInit: %s:%d sock %s", getIP(), mPort, getStateChar());

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
            hk::diag::logLine("Failed to create Web Socket");
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
            hk::diag::logLine("Web Socket Connection Failed!");
            mSockErrno = socket::GetLastErrno();
            mSockState = SockState::CONNFAIL;

            mState = WAIT;
            return false;
        }

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(500));
    }

    // mbed tls
    ensurePlatformInitialized();

    mbedtls_ssl_init(&mSsl);
    mbedtls_ssl_config_init(&mConf);
    mbedtls_ctr_drbg_init(&mCtrDrbg);
    mbedtls_entropy_init(&mEntropy);

    s32 result = mbedtls_ctr_drbg_seed(
        &mCtrDrbg, mbedtls_entropy_func, &mEntropy, (const unsigned char*)"smo-archipelago",
        strlen("smo-archipelago"));
    if (result == 0) {
        result = mbedtls_ssl_config_defaults(
            &mConf, MBEDTLS_SSL_IS_CLIENT, MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    }

    if (result != 0) {
        // snprintf(outError, outErrorSize, "SSL setup failed: %d", result);
        mbedtls_ssl_free(&mSsl);
        mbedtls_ssl_config_free(&mConf);
        mbedtls_ctr_drbg_free(&mCtrDrbg);
        mbedtls_entropy_free(&mEntropy);
        nn::socket::Close(mSockFd);
        mSockFd = -1;
        return false;
    }

    // No cert-chain verification - we're talking to one specific known host, not browsing.
    mbedtls_ssl_conf_authmode(&mConf, MBEDTLS_SSL_VERIFY_NONE);
    mbedtls_ssl_conf_rng(&mConf, mbedtls_ctr_drbg_random, &mCtrDrbg);

    result = mbedtls_ssl_setup(&mSsl, &mConf);
    if (result == 0) {
        mbedtls_ssl_set_hostname(&mSsl, getIP());
        mbedtls_ssl_set_bio(&mSsl, &mSockFd, nxSslSend, nxSslRecv, nullptr);
        result = mbedtls_ssl_handshake(&mSsl);
    }
    if (result != 0) {
        char errBuf[128];
        mbedtls_strerror(result, errBuf, sizeof(errBuf));
        // snprintf(outError, outErrorSize, "TLS handshake failed: %s", errBuf);
        mbedtls_ssl_free(&mSsl);
        mbedtls_ssl_config_free(&mConf);
        mbedtls_ctr_drbg_free(&mCtrDrbg);
        mbedtls_entropy_free(&mEntropy);
        nn::socket::Close(mSockFd);
        mSockFd = -1;
        return false;
    }

    mSockState = SockState::CONNECTED;

    // OPT-IN, off by default - see ServerConfig::useNonBlockingRecv().
    //
    // On Ryujinx a blocking recv outstanding on this socket also blocks a
    // concurrent mbedtls_ssl_write on the SAME fd from the send thread, which never
    // returns. Traced end to end: the initial PlayerConnect goes out fine
    // because exeInit() sends it BEFORE startThreads(), and the very next
    // packet - the first through the send queue - hangs forever in
    // mbedtls_ssl_write. It is not just the handshake; every gameplay packet uses
    // that queue, so a parked recv means nothing can be sent at all.
    //
    // Real hardware does NOT have this problem - multiplayer works there today
    // with the blocking path - so hardware stays on it untouched and only a
    // card that opts in changes behaviour. recv()'s EAGAIN/EWOULDBLOCK retry
    // path was already written for this and has never once fired; the opt-in is
    // what makes it real.

    // Fix with apwebclient main implementation
    if (ArchipelagoSocket::useNonBlockingRecv()) {
        set_sock_flags(cHosMsgDontWait);
        // MpDebugLog::write("WebSocketClient::exeInit: non-blocking recv enabled (emulator workaround)");
    }

    hk::diag::logLine("Web Socket fd: %d", mSockFd);

    // Setting state to connected means that the tcp socket is using http(s)
    // to upgrade the conenction to web socket protocol.
    mState = CONNECTING;

    // WebSocket spec (RFC 6455) requires 16 random bytes
    unsigned char random_bytes[16];
    mbedtls_ctr_drbg_random(&mCtrDrbg, random_bytes, sizeof(random_bytes));

    // Base64 encoded size calculation: 16 bytes -> 24 chars + 1 null terminator
    size_t olen = 0;

    if (mbedtls_base64_encode((unsigned char*)mClientKey, sizeof(mClientKey), &olen, random_bytes, sizeof(random_bytes)) != 0) {
        return false;
    }

    char httpReq[512];
    u64 reqSize = snprintf(httpReq, sizeof(httpReq),
        "GET /chat HTTP/1.1\r\n"
        "Host: %s:%s\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: %s\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n",
        getIP(), getPort(), mClientKey);

    // mLastConnectWasReconnect = !mIsFirstConnect;
    // initPacket->conType = mIsFirstConnect ? ConnectionTypes::INIT : ConnectionTypes::RECONNECT;
    // mIsFirstConnect = false;

    if (send(httpReq, reqSize)) {
        startThreads();
        return true;
    }

    return false;
}

void WebSocketClient::exeReset() {
    closeSocket();

    os::YieldThread();
    os::SleepThread(TimeSpan::FromMilliSeconds(100));

    // Free up all blocked threads
    mSendQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);
    mRecvQueue.push(0, sead::MessageQueue::BlockType::NonBlocking);

    while (!(mRecvThread->isDone() && mSendThread->isDone() && ArchipelagoClient::isThreadDone())) {
        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }

    // clear send and recv queue (idk man)
    for (s32 i = 0; i < 100; i++) {
        delete reinterpret_cast<WebSocketDataFrame*>(mSendQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
        delete reinterpret_cast<WebSocketDataFrame*>(mRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
    }

    mState = RECONNECT;
}

void WebSocketClient::startThreads() {
    if (mRecvThread->isDone())
        mRecvThread->start();
    if (mSendThread->isDone())
        mSendThread->start();
}

bool WebSocketClient::send(const char* httpRequest, u64 requestSize) {
    if (mSockState != SockState::CONNECTED) {
        hk::diag::logLine("Unable To Send! Web Socket Not Connected.");
        mSockErrno = socket::GetLastErrno();

        delete httpRequest;
        return false;
    }

    size valsent = 0;
    while (valsent < requestSize) {
        s32 result = mbedtls_ssl_write(&mSsl, (const unsigned char*)httpRequest + valsent, requestSize - valsent);

        mSockErrno = socket::GetLastErrno();

        if (result <= 0) {
            hk::diag::logLine(
                "Data Frame send failed! Sent %d this iteration, %zu so far, out of %zu.",
                result, valsent, requestSize);

            delete httpRequest;
            return false;
        }

        valsent += result;
    }

    delete httpRequest;
    return true;
}

bool WebSocketClient::send(WebSocketDataFrame* dataFrame) {
    if (mSockState != SockState::CONNECTED) {
        hk::diag::logLine("Unable To Send! Web Socket Not Connected.");
        mSockErrno = socket::GetLastErrno();

        delete dataFrame;
        return false;
    }

    DataFrameVector frameData = dataFrame->serialize();

    size valsent = 0;
    while (valsent < frameData.size()) {
        s32 result = mbedtls_ssl_write(&mSsl, frameData.data() + valsent, frameData.size() - valsent);

        mSockErrno = socket::GetLastErrno();

        if (result <= 0) {
            hk::diag::logLine(
                "Data Frame send failed! Sent %d this iteration, %zu so far, out of %zu.",
                result, valsent, frameData.size());

            delete dataFrame;
            return false;
        }

        valsent += result;
    }

    delete dataFrame;
    return true;
}

bool WebSocketClient::recv() {
    if (mSockState != SockState::CONNECTED) {
        hk::diag::logLine("Unable To Receive! Web Socket Not Connected.");
        mSockErrno = socket::GetLastErrno();
        return false;
    }

    // interpret as http response
    if (mState == CONNECTING) {
        unsigned char response[512];
        int readAmount = mbedtls_ssl_read(&mSsl, response, 512);
        if (readAmount > 0) {
            response[readAmount] = 0;

            char upgrade[] = "websocket";
            char connection[] = "upgrade";
            const char magic[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

            char concatStr[sizeof(mClientKey) + sizeof(magic)];
            strcat(concatStr, mClientKey);
            strcat(concatStr, magic);
            unsigned char sha1Hash[20];

            if (mbedtls_sha1(reinterpret_cast<unsigned char*>(concatStr), 69, sha1Hash) != 0) {
                return false;
            }
            u8 header = 0;
            u8 headerField = 0;
            u8 interpreter = 0;
            bool isFail = false;
            for (u64 i = 0; i < readAmount; i++) {
                if (response[i] == ' ') {
                    headerField += 1;
                    interpreter = i + 1;
                } else if (response[i] == '\r') {
                    header += 1;
                    headerField = 0;
                }

                if (header == 0 && headerField == 1) {
                    if (response[interpreter] == '1' && response[interpreter + 1] == '0' && response[interpreter + 2] == '1') {
                        i = interpreter + 2;
                    }
                }

                if (header > 0) {
                    // To Lower
                    if (response[i] <= 'Z') {
                        response[i] = response[i] + 32;
                    }

                    // Connection
                    if (response[i] = 'c') {
                        while (response[i] != '\r' || isFail) {
                            if (response[i] = ':') {
                                // skip to field value start
                                i += 2;
                                for (int k = 0; k < sizeof(connection); k++, i++) {
                                    if (response[i] != connection[k]) {
                                        isFail = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Sec-WebSocket-Accept
                    if (response[i] = 's') {
                        while (response[i] != '\r' || isFail) {
                            if (response[i] = ':') {
                                // skip to field value start
                                i += 2;
                                for (int k = 0; k < sizeof(sha1Hash); k++, i++) {
                                    if (response[i] != sha1Hash[k]) {
                                        isFail = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }

                    // Upgrade
                    if (response[i] = 'u') {
                        while (response[i] != '\r' || isFail) {
                            if (response[i] = ':') {
                                // skip to field value start
                                i += 2;
                                for (int k = 0; k < sizeof(upgrade); k++, i++) {
                                    if (response[i] != upgrade[k]) {
                                        isFail = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
                }
            }
            if (isFail) {
                return false;
            }
            mState = OPEN;
            delete &response;
            return true;
        }
    }

    DataFrameVector frameData(WebSocketDataFrame::WebSocketHeaderSizes::MinimumHeader);
    size valread = 0;
    s32 retries = 0;

    // read the minimum header size
    while (valread < WebSocketDataFrame::WebSocketHeaderSizes::MinimumHeader) {
        s32 result = mbedtls_ssl_read(&mSsl, frameData.data() + valread, WebSocketDataFrame::WebSocketHeaderSizes::MinimumHeader - valread);

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

    WebSocketDataFrame* frame = DataFrameFactory::create(frameData);
    if (!frame) {
        hk::diag::logLine("Factory couldn't produce data frame");
        return false;
    }

    if (frame->isFail()) {
        hk::diag::logLine("The header failed to deserialize properly.");
        return false;
    }

    size_t extendedSize = frame->getExtendedHeaderSize();
    frameData.resize(extendedSize);
    valread = 0;
    retries = 0;

    // Read the optional extended portion of the header
    while (valread < extendedSize) {
        s32 result = mbedtls_ssl_read(&mSsl, frameData.data(),
            extendedSize - valread);

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

    if (extendedSize > 0)
        frame->deserializeExtendedHeader(frameData);

    extendedSize = frame->getPayloadSize();
    frameData.resize(extendedSize);
    valread = 0;
    retries = 0;

    // Read the payload data
    while (valread < extendedSize) {
        s32 result = mbedtls_ssl_read(&mSsl, frameData.data(),
            extendedSize - valread);

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

    frame->deserializePayload(frameData);

    if (frame->isFail()) {
        hk::diag::logLine("The data frame failed to deserialize properly.");

        delete frame;
        return false;
    }

    s64 ptr = reinterpret_cast<s64>(frame);

    // NonBlocking push() silently drops (returns false) instead of blocking
    // when the queue (capacity 100) is full - if the consumer thread
    // (Client::readFunc(), a separate AsyncFunctorThread) ever falls behind
    // that far, the packet this just heap-allocated becomes permanently
    // unreachable: never freed, never delivered, never logged. Confirmed
    // real ArchipelagoSocket::sArchipelagoSocketHeap-leak gap, not just a theoretical one - explicitly delete it
    // on the drop path instead of leaking it.
    if (!mRecvQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking)) {
        // A full queue here is ordinary
        // back-pressure and the next frame's packet supersedes this one, so
        // drop it and keep the connection: returning false made recvFunc tear
        // down an otherwise healthy room connection during expensive stage
        // setup, just when the consumer was briefly unable to drain.

        delete frame;
        return true;
    }

    return true;
}

void WebSocketClient::closeSocket() {
    hk::diag::logLine("Closing Socket.");
    // MpDebugLog::write("WebSocketClient::closeSocket: deliberate close requested");

    mSockState = SockState::DISCONNECTED;
    mState = CLOSED;

    // Nothing to tear down: either no socket was ever opened, or exeInit()'s
    // own failure path already closed this fd. Both retry loops below would
    // otherwise run their full 20 x 100ms against a dead descriptor - 4 seconds
    // of blocking, and a wall of EBADF in the log, on every failed connect
    // cycle. Confirmed against a live capture of exactly that spam.
    if (mSockFd < 0) {
        // MpDebugLog::write("WebSocketClient::closeSocket: fd already invalid, nothing to close");
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
            hk::diag::logLine("Failed to close web socket!");

        if (socket::Close(mSockFd).IsSuccess())
            break;

        if (socket::GetLastErrno() == EBADF)
            break;

        os::YieldThread();
        os::SleepThread(TimeSpan::FromMilliSeconds(100));
    }

    mSockFd = -1;
}

bool WebSocketClient::stringToIPAddress(const char* str, in_addr* out) {
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

void WebSocketClient::sendFunc() {
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    hk::diag::logLine("Starting Send Thread.");
    while (mSockState != SockState::DISCONNECTED && trySendQueue()) {
    }

    mSockState = SockState::DISCONNECTED;
    mState = CLOSED;

    // hk::diag::logLine only reaches a live debug console - it's gone by the
    // time anyone notices a disconnect after the fact. This is the one exit
    // point for the send thread regardless of cause (send failure, or
    // closeSocket() called deliberately elsewhere setting DISCONNECTED out
    // from under the loop), so one persisted line here covers both: errno=0
    // with no preceding "Closing Socket." line in this same log points at an
    // unexpected drop, not a deliberate one.
    // MpDebugLog::write("WebSocketClient::sendFunc: send thread ending, errno=%d", mSockErrno);
    hk::diag::logLine("Sending packet failed!");
    hk::diag::logLine("Ending Send Thread.");

    signalReset();
}

void WebSocketClient::recvFunc() {
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    // ??? - purpose unknown, inherited. Left in place, but now issued with the
    // socket's own flags rather than a hardcoded 0, so it does not stay a
    // BLOCKING receive when the rest of the loop is non-blocking. Identical to
    // the old call in the default mode, where mSockFlags is 0.
    mbedtls_ssl_read(&mSsl, nullptr, 0);

    hk::diag::logLine("Starting Recv Thread.");

    while (mSockState != SockState::DISCONNECTED && recv()) {
    }

    mSockState = SockState::DISCONNECTED;
    mState = CLOSED;

    // See the matching comment in sendFunc() - same reasoning, other thread.
    // MpDebugLog::write("WebSocketClient::recvFunc: recv thread ending, errno=%d", mSockErrno);
    hk::diag::logLine("Receiving Packet Failed!");
    hk::diag::logLine("Ending Recv Thread.");

    signalReset();
}

bool WebSocketClient::queueDataFrame(WebSocketDataFrame* frame) {
    if (mSockState == SockState::CONNECTED) {
        s64 ptr = reinterpret_cast<s64>(frame);
        // Same NonBlocking-drop gap as recv() above - a full send queue
        // (capacity 100) previously leaked the frame silently instead of
        // freeing it, and misreported success to the caller besides.
        if (!mSendQueue.push(ptr, sead::MessageQueue::BlockType::NonBlocking)) {
            // MpDebugLog::write("WebSocketClient::queuePacket: send queue full, dropping packet");
            delete frame;
            return false;
        }

        return true;
    }

    delete frame;

    return false;
}

bool WebSocketClient::trySendQueue() {
    WebSocketDataFrame* frame = reinterpret_cast<WebSocketDataFrame*>(mSendQueue.pop(sead::MessageQueue::BlockType::Blocking));
    if (!frame) {
        hk::diag::logLine("WebSocketClient::trySendQueue: packet was nullptr");
        return false;
    }

    return send(frame);
}

WebSocketDataFrame* WebSocketClient::tryGetFrame() {
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
    while (mSockState == SockState::CONNECTED && mState == OPEN) {
        if (WebSocketDataFrame* frame = reinterpret_cast<WebSocketDataFrame*>(
                mRecvQueue.pop(sead::MessageQueue::BlockType::Blocking)))
            return frame;

        // Zero: a wake sentinel, not a packet. recv() pushes one whenever it
        // queues an event, so this is the normal path for every non-snapshot
        // packet - go round and pick that event up off the event queue.
    }

    return nullptr;
}

WebSocketDataFrame* WebSocketClient::tryGetFrameTimeout(s32 timeoutMs) {
    // Polls NonBlocking rather than waiting on the queue, so losing the
    // connection mid-wait (mSockState flipping to DISCONNECTED) ends the wait
    // on the next tick instead of needing exeReset()'s unblock push to arrive.
    constexpr s32 cPollIntervalMs = 10;
    for (s32 waited = 0; waited < timeoutMs; waited += cPollIntervalMs) {
        if (mSockState != SockState::CONNECTED || mState != OPEN)
            return nullptr;

        WebSocketDataFrame* frame = reinterpret_cast<WebSocketDataFrame*>(
            mRecvQueue.pop(sead::MessageQueue::BlockType::NonBlocking));
        if (frame)
            return frame;

        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(cPollIntervalMs));
    }

    return nullptr;
}
