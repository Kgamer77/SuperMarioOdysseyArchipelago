#pragma once

#include "sead/thread/seadAtomic.h"
#include "sead/thread/seadMessageQueue.h"

#include "al/Library/Thread/AsyncFunctorThread.h"

#include <experimental/memory>
#include <netinet/in.h>

#include "Archipelago/WebSocketDataFrame.h"
#include "Multiplayer/SocketBase.hpp"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ssl.h"
#include "types.h"

class WebSocketClient : public SocketBase {
public:
    enum WebSocketClientState
    {
        WAIT = 0,
        INIT = 1,
        RESET = 2,
        RECONNECT = 3,
        CONNECTING,
        OPEN,
        CLOSING,
        CLOSED
    };

    WebSocketClient();

    void update();
    bool exeInit();
    void exeReset();

    void init(const char* ip, u16 port);
    void closeSocket();

    void startThreads();

    // Send http requests. Primarily for upgrading the http socket to a websocket.
    bool send(const char* httpRequest, u64 size);

    // Send Websocket data frames
    bool send(WebSocketDataFrame* dataFrame);
    bool recv();

    bool queueDataFrame(WebSocketDataFrame* frame);
    bool trySendQueue();
    WebSocketDataFrame* tryGetFrame();
    WebSocketDataFrame* tryGetFrameTimeout(s32 timeoutMs);
    void signalReset() {
        if (mState == WAIT)
            mState = RESET;
    }

    void sendFunc();
    void recvFunc();

    // Sets the state of the underlying SocketBase
    void setSockState(SockState state) { mSockState = state; };
    // Checks is the underlying SocketBase is CONNECTED
    bool isConnected() { return mSockState == SockState::CONNECTED; };
    bool isOpen() { return mState == WebSocketClientState::OPEN; };

    WebSocketClientState getWebSocketClientState() { return mState; }

    u32 getSendCount() { return mSendQueue.mMessageQueueInner._count; }
    u32 getSendMaxCount() { return mSendQueue.mMessageQueueInner._maxCount; }

    u32 getRecvCount() { return mRecvQueue.mMessageQueueInner._count; }
    u32 getRecvMaxCount() { return mRecvQueue.mMessageQueueInner._maxCount; }

private:
    al::AsyncFunctorThread* mSocketThread = nullptr;
    al::AsyncFunctorThread* mRecvThread = nullptr;
    al::AsyncFunctorThread* mSendThread = nullptr;

    sead::MessageQueue mRecvQueue;
    sead::MessageQueue mSendQueue;

    sead::Atomic<WebSocketClientState> mState = CONNECTING;

    mbedtls_ssl_context mSsl;
    mbedtls_ssl_config mConf;
    mbedtls_ctr_drbg_context mCtrDrbg;
    mbedtls_entropy_context mEntropy;

    char mClientKey[32] = { 0 };

    /**
     * @param str a string containing an IPv4 address or a hostname that can be resolved via DNS
     * @param out IPv4 address
     * @return if this function was successfull and out contains a valid IP address
     */
    bool stringToIPAddress(const char* str, in_addr* out);
};