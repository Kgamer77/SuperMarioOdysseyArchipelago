/**
 * @file Archipelago/ArchipelagoClient.hpp
 * @brief class responsible for wrapping web socket for
 * Archipelago communications and to interpret raw payloads as easily
 * traversable json objects.
 *
 *
 */
#pragma once

#include <cstddef>
#include <functional>
#include <stdlib.h>
#include <string>

#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/Thread/AsyncFunctorThread.h"

#include "game/Item/CoinCollect.h"
#include "game/Item/CoinCollect2D.h"
#include "game/Item/Shine.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataHolderAccessor.h"

#include "sead/container/seadSafeArray.h"
#include "sead/heap/seadDisposer.h"
#include "sead/prim/seadSafeString.h"

#include "Archipelago/WebSocketClient.hpp"

// Meant to be passed to an function encompassing a json parser to parse the message text into a json object
using OnMessageCallback = std::function<void(const const char*, u64 length)>;

class ArchipelagoClient {
    SEAD_SINGLETON_DISPOSER(ArchipelagoClient)

public:
    ArchipelagoClient();

    static ArchipelagoClient* get();
    GameDataHolderAccessor& getHolder() { return mHolder; }
    void init(const al::LayoutInitInfo& initInfo, GameDataHolderAccessor holder);
    bool startThread();
    static bool isThreadDone();
    void setHostName(std::string hostname) { mServerHost = hostname; }
    void setPort(u16 port) { mPort = port; }
    void setMessageCallback(OnMessageCallback callback) { mMessageCallback = callback; };
    void setPerMessageDeflate(bool value) { mPerMessageDeflate = value; };
    void setPingInterval(int seconds) { mPingInterval = seconds * 60; };
    void resetPingTimer() { mPingTimer = mPingInterval; };

    bool initConnection();

    static void restartConnection();
    static void disconnect();
    static bool isSocketActive() { return sInstance ? sInstance->mWebSocket->isConnected() : false; }
    static bool isSocketOpen() { return sInstance ? sInstance->mWebSocket->isOpen() : false; }
    static void sendMessage(std::string msg);
    static void update();

private:
    bool startConnection();
    void readFunc();
    al::AsyncFunctorThread* mReadThread = nullptr;
    WebSocketClient* mWebSocket;

    // Connection Members
    bool mIsConnectionActive = false;
    std::string mServerHost;
    u16 mPort = 38281;
    OnMessageCallback mMessageCallback = nullptr;
    bool mPerMessageDeflate = false;
    int mPingInterval;
    int mPingTimer = 0;
    bool mWaitingPong = false;

    // Scene / Stage Members
    const StageScene* mCureStageScene = nullptr;
    GameDataHolderAccessor mHolder;
};