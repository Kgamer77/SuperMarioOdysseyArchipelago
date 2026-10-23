#include "Archipelago/ArchipelagoClient.hpp"

#include "nn/os.h"
#include "nn/socket.h"

#include "sead/heap/seadHeapMgr.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Scene/SceneUtil.h"

#include "helpers.hpp"
#include "hk/svc/api.h"

#include "Archipelago/ArchipelagoSocket.h"

SEAD_SINGLETON_DISPOSER_IMPL(ArchipelagoClient)

ArchipelagoClient::ArchipelagoClient() {
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    mReadThread = new al::AsyncFunctorThread("ArchipelagoClientReadThread", al::FunctorV0M(this, &ArchipelagoClient::readFunc), 0,
        16_KB, sead::CoreId::cMain);

    mWebSocket = new WebSocketClient();
}

void ArchipelagoClient::init(const al::LayoutInitInfo& initInfo, GameDataHolderAccessor holder) {
    {
        sead::ScopedCurrentHeapSetter setter(al::getSequenceHeap());
        // If sonnect status ends up being used init here as well as other icons...
    }

    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    mHolder = holder;

    // Connection will gated behinf the Archipelago button on the title screen
    // similar to the normal SMOO client to keep consistency in the project.

    // startThread doesn't run here to prevent the same incorrect state
    // of the thread when initializing it without an ip.
}

ArchipelagoClient* ArchipelagoClient::get() {
    return sInstance;
}

bool ArchipelagoClient::startThread() {
    if (mReadThread->isDone()) {
        mReadThread->start();
        hk::diag::logLine("Archipelago Read Thread started successfully.");
        return true;
    } else {
        hk::diag::logLine("Archipelago Read Thread has already started or other unknown reason.");
        return false;
    }
}

bool ArchipelagoClient::isThreadDone() { return sInstance ? sInstance->mReadThread->isDone() : true; }

bool ArchipelagoClient::initConnection() {
    startConnection();
}

bool ArchipelagoClient::startConnection() {
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    mWebSocket->init(mServerHost.c_str(), mPort);

    // Wait until web socket is initialized and open
    while (mWebSocket->getWebSocketClientState() == WebSocketClient::INIT || mWebSocket->getWebSocketClientState() == WebSocketClient::CONNECTING) {
        hk::diag::logLine("Log state: %s", mWebSocket->getStateChar());
        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(100));
    }

    startThread();
}

void ArchipelagoClient::restartConnection() {
    sInstance->mWebSocket->signalReset();
}

void ArchipelagoClient::disconnect() {
    if (!sInstance)
        return;

    if (!sInstance->mWebSocket->isConnected() && sInstance->mServerHost.empty())
        return; // Not connected or connection already dropped.

    hk::diag::logLine("ArchipelagoClient::disconnect: dropping websocket connection.");

    sInstance->mServerHost.clear();
    sInstance->mWebSocket->closeSocket();
}

void ArchipelagoClient::sendMessage(std::string msg) {
    if (!sInstance || !sInstance->mWebSocket || !isSocketOpen())
        return;

    WebSocketDataFrame::WebSocketPerMessageDeflate deflate = sInstance->mPerMessageDeflate ? WebSocketDataFrame::DeflateExtensionEnabled : WebSocketDataFrame::DeflateExtensionDisabled;
    WebSocketDataFrame df(WebSocketDataFrame::FINAL, deflate, WebSocketDataFrame::MASKED, msg.size(), msg.c_str());

    if (!sInstance->mWebSocket->queueDataFrame(&df))
        delete &df;
}

void ArchipelagoClient::update() {
    if (!sInstance)
        return;

    WebSocketDataFrame::WebSocketPerMessageDeflate deflate = sInstance->mPerMessageDeflate ? WebSocketDataFrame::DeflateExtensionEnabled : WebSocketDataFrame::DeflateExtensionDisabled;

    // Run ping interval for web socket heartbeat
    // Payload for ping and pong frames is capped at 127 bytes
    // and is used to add a unique way of identifyfing the sent/received ping/pong
    // as the payload contents is equivalent for a ping/pong pair
    // when a ping farme is sent, expect a pong to be recieved before next ping
    // or the connection is guaranteed dead
    // Most libraries keep the ping interval independent of all other packet handling
    // though the ping timer can be reset on packet received since
    // receiving a new packet guarantees the connection is still alive.
    if (sInstance->mPingTimer <= 0) {
        DataFrameVector ping;
        WebSocketDataFrame df(WebSocketDataFrame::FINAL,
            deflate, WebSocketDataFrame::PING,
            WebSocketDataFrame::MASKED, 0, ping);
        sInstance->mWebSocket->queueDataFrame(&df);
        sInstance->mWaitingPong = true;

        sInstance->resetPingTimer();
    }
    sInstance->mPingTimer -= 1;

    // Mostly for clearing different event queues and
    // guaranteeing the events recieved from archipelago
    // are handled and synced properly
    // though most of those events for recieving items
    // are handled in different places
    // as of this version due to the handling being from
    // the ap gamemode implementation
    // which mat be kept due to better
    // enforcing the Archipelago Server Lobby
    // is always correct rule regarding
    // both game and inventory state.
}

void ArchipelagoClient::readFunc() {
    sead::ScopedCurrentHeapSetter setter(ArchipelagoSocket::sArchipelagoSocketHeap);

    hk::diag::logLine("Starting Arhcipelago Client Read Thread.");

    if (!mWebSocket->isConnected() && !startConnection()) {
        hk::diag::logLine("Failed to connect to Archipelago.");
        mWebSocket->signalReset();
        hk::diag::logLine("Archipelago Read Thread ending.");
        return;
    }

    while (mWebSocket->isConnected()) {
        HK_ABORT_UNLESS(mWebSocket != nullptr, "ArchipelagoClient::mWebSocket was nullptr");
        auto curFrame = mWebSocket->tryGetFrame();

        if (curFrame) {
            // check for pong frames
            if (mWaitingPong && curFrame->getOpcode() == WebSocketDataFrame::PONG)
                mWaitingPong = false;

            // Only proceed to callback if callback is not null
            if (mMessageCallback) {
                mMessageCallback(curFrame->getPayloadCstr(), curFrame->getPayloadSize());
            }
            // Refer to update about connection assumptions.
            resetPingTimer();

            delete curFrame;

        } else {
            hk::diag::logLine("WebSocketClient::tryGetPacket() returned nullptr! Errno: 0x%x",
                mWebSocket->mSockErrno);
            break;
        }
    }

    mWebSocket->signalReset();
    hk::diag::logLine("Archipelago Read Thread ending.");
}
