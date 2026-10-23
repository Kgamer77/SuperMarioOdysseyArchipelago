#include "Multiplayer/server/Client.hpp"

#include "Multiplayer/algorithms/AlUnlinked.h"
#include "Multiplayer/algorithms/CaptureSyncTable.h"
#include "Multiplayer/algorithms/RideTransform.h"

#include "game/Player/PlayerBindKeeper.h"

#include "Randomizer/CoinRando.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/PaintingRando.h"

#include "hk/diag/diag.h"

#include "nn/account.h"
#include "nn/os.h"
#include "nn/socket.h"

#include "sead/heap/seadHeapMgr.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/Yaml/ByamlIter.h"
#include "al/Library/Yaml/ByamlUtil.h"

#include "game/Item/ShineInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/CheckpointFlag.h"
#include "game/MapObj/CheckpointFlagWatcher.h"
#include "game/Player/HackCap.h"
#include "game/Player/PlayerAnimFrameCtrl.h"
#include "game/Player/PlayerAnimator.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Scene/StageScene.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/CustomGameDataFunction.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/UniqObjInfo.h"
#include "game/Util/AchievementUtil.h"
#include "game/Util/ActorDimensionKeeper.h"
#include "game/Util/DemoUtil.h"

#include <climits>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <experimental/memory>
#include <experimental/utility>
#include <netinet/in.h>
#include <sys/socket.h>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/RoomSnapshot.h"
#include "Multiplayer/ServerConfig.h"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/layouts/SpeedrunIcon.h"
#include "Multiplayer/logger.hpp"
#include "Multiplayer/main.hpp"
#include "Multiplayer/packets/CostumeInf.h"
#include "Multiplayer/packets/GameInf.h"
#include "Multiplayer/packets/Packet.h"
#include "Multiplayer/packets/PlayerDC.h"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "Multiplayer/server/SocketClient.hpp"
#include "Multiplayer/sync/DurableSync.h"
#include "Multiplayer/sync/SyncedActor.h"
#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "ShineDB.h"
#include "helpers.hpp"
#include "hk/svc/api.h"

SEAD_SINGLETON_DISPOSER_IMPL(Client)

namespace {
    // ===== received-moon table lock =====
    //
    // curCollectedShines/collectedShineCount are written by updateShineInfo on
    // the READ thread and drained by updateShines on the GAME thread, and until
    // now nothing synchronised the two. updateShines walked the table and then
    // wiped it whole with resetCollectedShines(), so a moon the read thread
    // stored into a slot the walk had already passed was erased without ever
    // being applied - and the non-atomic collectedShineCount++ racing that
    // wholesale `= 0` could leave a live entry behind a zero count, which
    // isNeedUpdateShines() reads as "nothing to do".
    //
    // Both are silent losses of a moon that the sender had and the receiver
    // never got, and their window is widest during exactly the bursts the sync
    // mechanisms produce - a catch-up or reconciliation replay arriving while
    // the game thread is inside applyRemoteShine for the previous one.
    //
    // Raw std::atomic with explicit acquire/release rather than sead::Atomic,
    // which hardcodes relaxed ordering and so establishes no happens-before at
    // all on ARM, and bounded acquire so a packet handler can never be wedged -
    // the same rules DurableSync::ScopedLock documents and follows.
    std::atomic<bool> sShineTableLock { false };
    constexpr s32 cShineLockMaxAttempts = 200;

    class ShineTableLock {
    public:
        ShineTableLock() {
            bool expected = false;
            for (s32 attempts = 0; !sShineTableLock.compare_exchange_weak(
                     expected, true, std::memory_order_acquire, std::memory_order_relaxed);
                 attempts++) {
                expected = false;
                if (attempts >= cShineLockMaxAttempts)
                    return;
                nn::os::YieldThread();
            }
            mAcquired = true;
        }
        ~ShineTableLock() {
            if (mAcquired)
                sShineTableLock.store(false, std::memory_order_release);
        }
        bool isAcquired() const { return mAcquired; }

        ShineTableLock(const ShineTableLock&) = delete;
        ShineTableLock& operator=(const ShineTableLock&) = delete;

    private:
        bool mAcquired = false;
    };

    // One seed can produce a location hint, a route hint and a direct purchase
    // for each of the native save's 1024 hint rows, plus capture/ability flags.
    // Keep the full late-join snapshot, but keep it out of Client's ExpHeap
    // allocation so constructing the singleton cannot starve its read thread.
    constexpr s32 cMaxPendingHintUnlocks = 4096;
    constexpr s32 cPendingHintUnlockQueueSize = cMaxPendingHintUnlocks + 1;
    PendingHintUnlock sPendingHintUnlocks[cPendingHintUnlockQueueSize];
    static_assert(sizeof(PendingHintUnlock) == 16,
        "Pending hint replay entries must remain compact");

    // Deep enough for a full catch-up replay or a state-vector reconciliation
    // burst to land without the read thread ever having to drop one - the
    // server caps a reconciliation at 64 adoptions, and a join replay is
    // bounded by the team log, of which only the unlocks and coin deltas reach
    // here. Same module-storage rule as the hint queue above.
    // Deep enough for a whole join replay, and kept in module storage rather
    // than inline in the singleton for the same reason as the hint queue above.
    constexpr s32 sMaxPendingCoinCollects = 1024;
    constexpr s32 sMaxPendingCheckpoints = 512;
    PendingCoinCollect sPendingCoinCollects[sMaxPendingCoinCollects];
    PendingCheckpoint sPendingCheckpoints[sMaxPendingCheckpoints];

    constexpr s32 cMaxPendingRemoteActions = 512;
    constexpr s32 cPendingRemoteActionQueueSize = cMaxPendingRemoteActions + 1;
    PendingRemoteAction sPendingRemoteActions[cPendingRemoteActionQueueSize];
    static_assert(sizeof(PendingRemoteAction) == 32,
        "Pending remote actions must remain compact");

    u64 getPuppetStateThreadToken() {
        // Horizon gives every thread a distinct TLS region, including the
        // NSS module's custom worker threads. Its address is therefore a
        // stable, nonzero owner token and avoids depending on an emulator's
        // handling of the GetThreadId SVC pseudo-handle.
        return reinterpret_cast<u64>(hk::svc::getTLSPtr());
    }

    bool isFinitePosition(const sead::Vector3f& position) {
        constexpr float cMaxCoordinate = 10000000.0f;
        return std::isfinite(position.x) && std::isfinite(position.y) && std::isfinite(position.z)
            && std::abs(position.x) <= cMaxCoordinate && std::abs(position.y) <= cMaxCoordinate
            && std::abs(position.z) <= cMaxCoordinate;
    }

    // The worn-cap scale rides in HackCapInf::capJointRot (see
    // sendHackCapInfPacket). Bounds rather than a unit-length test: this is a
    // scale, and a garbage one would blow a remote hat up to fill the screen.
    bool isFiniteScale(const sead::Quatf& scale) {
        constexpr float cMinScale = 0.01f;
        constexpr float cMaxScale = 10.0f;
        return std::isfinite(scale.x) && std::isfinite(scale.y) && std::isfinite(scale.z)
            && scale.x >= cMinScale && scale.x <= cMaxScale && scale.y >= cMinScale
            && scale.y <= cMaxScale && scale.z >= cMinScale && scale.z <= cMaxScale;
    }

    bool isFiniteRotation(const sead::Quatf& rotation) {
        if (!std::isfinite(rotation.x) || !std::isfinite(rotation.y)
            || !std::isfinite(rotation.z) || !std::isfinite(rotation.w))
            return false;

        const float lengthSquared = rotation.x * rotation.x + rotation.y * rotation.y
            + rotation.z * rotation.z + rotation.w * rotation.w;
        return lengthSquared > 0.25f && lengthSquared < 4.0f;
    }

    // Last capture action name that could not be encoded, so the log gets one
    // line per distinct name instead of one per frame. A plain buffer rather
    // than a sead string: nothing guarantees this mod's .init_array runs, so
    // file-scope objects with constructors are avoided throughout.
    char sLastUnknownCaptureAnim[0x40] = {};

    bool noteUnknownCaptureAnim(const char* actName) {
        if (!actName)
            return false;
        if (std::strncmp(sLastUnknownCaptureAnim, actName, sizeof(sLastUnknownCaptureAnim) - 1) == 0)
            return false;

        std::strncpy(sLastUnknownCaptureAnim, actName, sizeof(sLastUnknownCaptureAnim) - 1);
        sLastUnknownCaptureAnim[sizeof(sLastUnknownCaptureAnim) - 1] = '\0';
        return true;
    }
}

/**
 * @brief Construct a new Client::Client object
 */
Client::Client() {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    mReadThread = new al::AsyncFunctorThread("ClientReadThread", al::FunctorV0M(this, &Client::readFunc), 0,
        16_KB, sead::CoreId::cMain);

    mKeyboard = new Keyboard();

    mSocket = new SocketClient();

    mPuppetHolder = new PuppetHolder(maxPuppets);

    for (size_t i = 0; i < MAXPUPINDEX; i++) {
        mPuppetInfoArr[i] = new PuppetInfo();

        mPuppetInfoArr[i]->puppetName.format("Puppet%zu", i);
    }

    curCollectedShines.fill(-1);

    collectedShineCount = 0;

    mShineArray.allocBuffer(100, gHeap); // max of 100 shine actors in buffer

    mCoinCollectArray.allocBuffer(100, gHeap);

    mCoinCollect2DArray.allocBuffer(25, gHeap);

    mPendingCoinCollectCount = 0;

    mPendingCheckpointCount = 0;

    mPendingHintUnlockHead.store(0, std::memory_order_relaxed);
    mPendingHintUnlockTail.store(0, std::memory_order_relaxed);

    nn::account::GetLastOpenedUser(&mUserID);

    nn::account::Nickname playerName;
    nn::account::GetNickname(&playerName, mUserID);
    Logger::setLogName(playerName.m_Buffer); // set Debug logger name to player name

    mUsername = playerName.m_Buffer;

    mUserID.print();

    hk::diag::logLine("Player Name: %s", playerName.m_Buffer);

    // hk::diag::logLine("%s Build Number: %s", playerName.m_Buffer, BUILDVERSTR);
}

/**
 * @brief initializes client class using initInfo obtained from StageScene::init
 *
 * @param initInfo init info used to create layouts used by client
 */
void Client::init(const al::LayoutInitInfo& initInfo, GameDataHolderAccessor holder) {
    {
        sead::ScopedCurrentHeapSetter setter(al::getSequenceHeap());
        mConnectStatus = new al::SimpleLayoutAppearWaitEnd("", "SaveMessage", initInfo, 0, false);
        SpeedrunIcon::sInstance = new SpeedrunIcon("SpeedrunIcon", initInfo);
    }

    sead::ScopedCurrentHeapSetter setter(gHeap);

    if (PlayerEventLog::instance())
        PlayerEventLog::deleteInstance();
    PlayerEventLog::createInstance(gHeap);

    if (mConnectStatus) {
        al::setPaneString(mConnectStatus, "TxtSave", u"Connecting to Server.", 0);
        al::setPaneString(mConnectStatus, "TxtSaveSh", u"Connecting to Server.", 0);
        // Force an explicit hidden resting state. Before connectWithRoomCode()
        // existed, readFunc() ran unconditionally at boot and always gave this
        // an appear()+end() cycle - even a solo player with no room code hit
        // the immediate-fail branch, which still calls end(). Now readFunc()
        // never runs at all for a player who never opens the Multiplayer menu,
        // so this needs to be explicitly put in its hidden state here instead
        // of relying on that cycle - otherwise "Connecting to Server." can
        // show for the entire single-player session with nothing to hide it.
        mConnectStatus->end();
    }

    mHolder = holder;

    // Connecting no longer happens here at boot at all - it's gated entirely
    // behind the title screen's Multiplayer button now
    // (PauseMenuHooks.cpp -> Client::connectWithRoomCode() once a room code
    // is entered), so single-player boots straight through with no prompt.
    //
    // Deliberately NOT calling startThread() here even as a no-op (an
    // earlier version of this change did, reasoning an empty mServerIP would
    // make it harmlessly start-then-immediately-end). Dev's read on a real
    // "disconnected right after entering the room code" report: that harmless-
    // looking first start/end cycle likely wasn't actually harmless - it may
    // leave mReadThread/mSocket in a slightly different state than a thread
    // that starts completely fresh, which the later real
    // connectWithRoomCode()-triggered start would then inherit. Simplest fix
    // matching the "no multi code runs unless you click the button" goal:
    // mReadThread simply never starts here. connectWithRoomCode() is the only
    // startThread() call site now, ever, for any given session.

    // hk::diag::logLine("Heap Free Size: %f/%f", mHeap->getFreeSize() * 0.001f, mHeap->getSize() * 0.001f);
}

Client* Client::get() {
    return sInstance;
}

/**
 * @brief starts client read thread
 *
 * @return true if read thread was sucessfully started
 * @return false if read thread was unable to start, or thread was already started.
 */
bool Client::startThread() {
    if (mReadThread->isDone()) {
        mReadThread->start();
        hk::diag::logLine("Read Thread Sucessfully Started.");
        return true;
    } else {
        hk::diag::logLine("Read Thread has already started! Or other unknown reason.");
        return false;
    }
}

void Client::restartConnection() {
    sInstance->disconnectAll();
    sInstance->mSocket->signalReset();
}

/**
 * @brief Connects to the dedicated server with a room code entered from the
 * title screen's Multiplayer button (PauseMenuHooks.cpp), replacing the old
 * boot-time keyboard prompt that used to live in init(). Safe to call any
 * time after boot: startThread() is already designed to be re-callable
 * (see its own doc comment) once a previous attempt has finished/ended.
 * @param roomCode room code to join
 */
void Client::connectWithRoomCode(const char* roomCode) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    // Re-read the operator-controlled endpoint at the moment Multiplayer is
    // entered.  Loading it only from the early SD mount hook left a stale
    // compile-time address cached for the whole process if the file was
    // created or edited after that hook ran (particularly visible on
    // emulators with a host-mounted SD directory).
    ServerConfig::load();
    sInstance->mServerIP.copy(ServerConfig::getIp());
    sInstance->mServerPort = ServerConfig::getPort();
    char normalized[17] = {};
    s32 length = 0;
    if (roomCode) {
        for (const char* cursor = roomCode; *cursor && length < 16; cursor++) {
            char value = *cursor;
            if (value >= 'a' && value <= 'z')
                value = value - 'a' + 'A';
            if ((value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9'))
                normalized[length++] = value;
            else if (value != ' ' && value != '\t' && value != '\r' && value != '\n') {
                length = 0;
                break;
            }
        }
    }
    if (length <= 0) {
        sInstance->mRoomError = MultiplayerProtocol::RoomError::InvalidCode;
        sInstance->mRoomStatusMessage.copy("Room codes must contain 1-16 letters or digits.");
        return;
    }
    sInstance->mRoomCode.copy(normalized);
    sInstance->mRoomAssigned = false;
    sInstance->mProtocolAccepted = false;
    sInstance->mRoomError = MultiplayerProtocol::RoomError::None;
    sInstance->mRoomState = MultiplayerProtocol::RoomState::Closing;
    sInstance->mRoomStatusMessage.copy("Connecting to multiplayer server...");
    sInstance->mPublishedKingdoms.store(0);
    sInstance->mPendingKingdoms.store(0);
    sInstance->mPendingTravelingPeachStart.store(false, std::memory_order_relaxed);
    sInstance->mTravelingPeachPublished.store(false, std::memory_order_relaxed);
    sInstance->mTravelingPeachRetryFrames = 300;
    sInstance->mPendingHintUnlockHead.store(0, std::memory_order_relaxed);
    sInstance->mPendingHintUnlockTail.store(0, std::memory_order_relaxed);

    hk::diag::logLine("Client::connectWithRoomCode: room code '%s', connecting to %s:%d",
        sInstance->mRoomCode.cstr(), sInstance->mServerIP.cstr(), sInstance->mServerPort);

    sInstance->startThread();
}

/**
 * @brief Opens the room-code keyboard (the same prompt init() used to show
 * at boot, before this moved behind the title screen's Multiplayer button)
 * and, if a code was entered, connects via connectWithRoomCode(). Blocking -
 * safe to call from a per-frame title-menu nerve executor: this exact
 * blocking-keyboard shape is already proven safe called from inside one,
 * see StageSceneStateFileSelect::exeOpenKeyboardTrackerRoomCode() (a
 * different, unrelated room code - the web Tracker's - from this same
 * title-hosted nerve).
 * @return true if a room code was entered and a connection attempt started,
 * false if the keyboard was cancelled/left blank (caller should not proceed
 * into multiplayer file-select in that case).
 */
bool Client::promptRoomCodeAndConnect() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return false;
    }

    sInstance->mKeyboard->setHeaderText("Multiplayer Room Code");
    sInstance->mKeyboard->setSubText("Enter the room code to join.");
    sInstance->mKeyboard->openKeyboard("", [](nn::swkbd::KeyboardConfig& config) {
        config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
        config.textMaxLength = 16;
        config.textMinLength = 0;
        config.isUseUtf8 = true;
        config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
    });
    while (!sInstance->mKeyboard->isDone())
        nn::os::YieldThread();

    if (sInstance->mKeyboard->isCancelled() || !sInstance->mKeyboard->getResult()
        || !sInstance->mKeyboard->getResult()[0]) {
        hk::diag::logLine("Client::promptRoomCodeAndConnect: cancelled/empty, not connecting");
        return false;
    }

    return connectAndAwaitRoom(sInstance->mKeyboard->getResult());
}

/**
 * @brief connectWithRoomCode() plus the bounded wait for room assignment.
 * Split out of promptRoomCodeAndConnect() so the title Multiplayer menu's
 * Connect row (which already has the room name typed, so it needs no
 * keyboard) shares the exact same success criteria.
 * @param roomCode room code to join
 * @return true once the server has assigned a room role, false on any room
 * error or if the assignment never arrived.
 */
bool Client::connectAndAwaitRoom(const char* roomCode) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return false;
    }

    connectWithRoomCode(roomCode);
    if (hasRoomError())
        return false;

    // File-select needs the setup-owner/guest assignment before choosing its
    // path. The socket and client threads keep running while this title nerve
    // yields, so wait only for the bounded handshake rather than guessing a
    // role and rebuilding the wrong menus.
    for (s32 wait = 0; wait < 600 && !isRoomAssigned() && !hasRoomError(); wait++) {
        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(50));
    }
    if (!isRoomAssigned()) {
        if (!hasRoomError()) {
            sInstance->mRoomError = MultiplayerProtocol::RoomError::Ended;
            sInstance->mRoomStatusMessage.copy("Timed out waiting for room assignment.");
        }
        return false;
    }
    return true;
}

void Client::disconnect() {
    if (!sInstance)
        return;

    if (!sInstance->mSocket->isConnected() && sInstance->mServerIP.isEmpty())
        return; // nothing to do - never connected, or already dropped

    hk::diag::logLine("Client::disconnect: dropping connection, staying solo");

    // Clear first: sendFunc()/recvFunc() ending (which closeSocket() below
    // triggers) call signalReset() themselves on their way out, and that
    // auto-reconnects (SocketClient::exeReset() -> RECONNECT ->
    // Client::startThread()) unless there's no room code left to reconnect
    // with by the time it runs - see startConnection()'s empty-mServerIP
    // check.
    sInstance->mServerIP.copy("");
    sInstance->mRoomCode.copy("");
    sInstance->mRoomAssigned = false;
    sInstance->mProtocolAccepted = false;
    sInstance->mRoomState = MultiplayerProtocol::RoomState::Closing;
    sInstance->mRoomError = MultiplayerProtocol::RoomError::None;
    sInstance->mRoomFlags = 0;
    sInstance->mPublishedKingdoms.store(0);
    sInstance->mPendingKingdoms.store(0);
    sInstance->mPendingTravelingPeachStart.store(false, std::memory_order_relaxed);
    sInstance->mTravelingPeachPublished.store(false, std::memory_order_relaxed);
    sInstance->mTravelingPeachRetryFrames = 0;
    sInstance->mPendingHintUnlockHead.store(0, std::memory_order_relaxed);
    sInstance->mPendingHintUnlockTail.store(0, std::memory_order_relaxed);
    for (s32 i = 0; i < SNumMoonRocks; i++) {
        sInstance->mPendingScenarioNo[i] = 0;
        sInstance->mPublishedScenarioNo[i] = 0;
    }
    sInstance->mScenarioBaselineSeeded = false;
    sInstance->mSocket->resetFirstConnect();

    // Detach the durable state, which flushes anything still dirty to the SD
    // card first. The FILE is deliberately left in place: rejoining the same
    // room and team later - this session or next week - restores the
    // unacknowledged outbox and the applied watermark exactly as they were.
    //
    // Detaching matters because the persisted masks back
    // hasRemoteMoonRockKeyUnlock/hasRemotePaintingUnlock, and a player who
    // leaves multiplayer for a solo file must not carry the team's unlocks into
    // it. (The RAM arrays this replaced were never cleared here either, so a
    // quit-to-title into a solo save had the same leak - it just went unnoticed
    // because it could not outlive the process.)
    DurableSync::reset();

    // Drop whatever remote puppets are still shown locally - they're not
    // coming back without a fresh connection.
    sInstance->disconnectAll();

    sInstance->mSocket->closeSocket();

    // Online-only (its own update() runs unconditionally every frame once
    // constructed, and events only fire from packet handlers/self-event
    // hooks gated on isSocketActive()) - tear it down with the connection
    // instead of leaving it sitting around solo. Same pattern already used
    // to reset it at Client::init().
    if (PlayerEventLog::instance())
        PlayerEventLog::deleteInstance();
}

/**
 * @brief starts a connection using client's TCP socket class, pulling up the software keyboard for
 * user inputted IP if save file does not have one saved.
 *
 * @return true if successful connection to server
 * @return false if connection was unable to establish
 */
// How long each connect-handshake reply gets before the attempt is failed and
// reported. Generous: this covers a real server that is merely slow, while
// still being far below the room-assignment wait in connectAndAwaitRoom() so
// the specific failing step is what gets reported, not a vague overall timeout.
static constexpr s32 cHandshakeTimeoutMs = 10000;

bool Client::startConnection() {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    // No auto-popup keyboard prompt for an unconfigured server IP/port -
    // this used to force the player through the IP then Port keyboard every
    // boot, before they'd even reached the title screen, on any fresh save.
    // The room-code-based Online (Host/Join) flow is the primary way to
    // connect now; an unset IP here just means "not connected yet," not
    // "prompt immediately." openKeyboardIP()/openKeyboardPort() below are
    // unused now that this was their only call site - left in rather than
    // deleted in case manual direct-IP entry needs a menu entry point later.
    if (mServerIP.isEmpty() || !mServerPort)
        return false;

    MpDebugLog::write("Client::startConnection: connecting to %s:%d, room '%s', uid0=%llx",
        mServerIP.cstr(), mServerPort, mRoomCode.cstr(), (unsigned long long)mUserID.m_Storage[0]);

    mSocket->init(mServerIP.cstr(), mServerPort);

    while (mSocket->getSocketClientState() == SocketClient::INIT) {
        hk::diag::logLine("log state: %s", mSocket->getStateChar());
        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(100));
    }

    mIsConnectionActive = mSocket->getLogState() == SockState::CONNECTED;
    MpDebugLog::write("Client::startConnection: socket state after init wait = %s (active=%d)",
        mSocket->getStateChar(), mIsConnectionActive);

    if (mIsConnectionActive) {
        hk::diag::logLine("Successful Connection. Waiting to receive init packet.");

        while (true) {
            auto curPacket = mSocket->tryGetPacket();

            if (curPacket) {
                if (curPacket->mType == PacketType::CLIENTINIT) {
                    InitPacket* initPacket = static_cast<InitPacket*>(curPacket);

                    hk::diag::logLine("Server Max Player Size: %d", initPacket->maxPlayers);
                    MpDebugLog::write("Client::startConnection: received CLIENTINIT, maxPlayers=%d",
                        initPacket->maxPlayers);

                    maxPuppets = initPacket->maxPlayers - 1;
                    mPuppetHolder->resizeHolder(maxPuppets);

                    delete curPacket;

                    auto protocolHello = new ProtocolHello;
                    protocolHello->mUserID = mUserID;
                    protocolHello->version = MultiplayerProtocol::cVersion;
                    protocolHello->isReconnect = mSocket->wasReconnect() ? 1 : 0;
                    protocolHello->expectedServerInstance[0] = mServerInstance[0];
                    protocolHello->expectedServerInstance[1] = mServerInstance[1];
                    if (!mSocket->queuePacket(protocolHello)) {
                        mRoomError = MultiplayerProtocol::RoomError::Ended;
                        mRoomStatusMessage.copy("Failed to send protocol handshake.");
                        mIsConnectionActive = false;
                        break;
                    }

                    MpDebugLog::write("Client::startConnection: ProtocolHello sent, awaiting result");

                    Packet* protocolPacket = mSocket->tryGetPacketTimeout(cHandshakeTimeoutMs);
                    if (!protocolPacket || protocolPacket->mType != PacketType::PROTOCOLRESULT) {
                        MpDebugLog::write(
                            "Client::startConnection: no PROTOCOLRESULT (got %s type=%d)",
                            protocolPacket ? "wrong packet" : "nothing",
                            protocolPacket ? (int)protocolPacket->mType : -1);
                        delete protocolPacket;
                        mRoomError = MultiplayerProtocol::RoomError::Ended;
                        mRoomStatusMessage.copy("Server did not answer the protocol handshake.");
                        mIsConnectionActive = false;
                        break;
                    }
                    ProtocolResult* protocolResult = static_cast<ProtocolResult*>(protocolPacket);
                    MpDebugLog::write(
                        "Client::startConnection: PROTOCOLRESULT version=%d status=%d msg='%s'",
                        (int)protocolResult->version, (int)protocolResult->status,
                        protocolResult->message.cstr());
                    mServerInstance[0] = protocolResult->serverInstance[0];
                    mServerInstance[1] = protocolResult->serverInstance[1];
                    mRoomStatusMessage.copy(protocolResult->message);
                    mProtocolAccepted = protocolResult->version == MultiplayerProtocol::cVersion
                        && protocolResult->status == MultiplayerProtocol::ProtocolStatus::Accepted;
                    if (!mProtocolAccepted) {
                        mRoomError = MultiplayerProtocol::RoomError::Ended;
                        delete protocolPacket;
                        mIsConnectionActive = false;
                        break;
                    }
                    delete protocolPacket;

                    // Tag this connection with the room code picked at boot
                    // (see init()) - the dedicated server only relays
                    // gameplay packets between clients sharing the same
                    // room. mRoomCode is never empty here: init() clears
                    // mServerIP (making this whole function return early,
                    // above) rather than connecting with no room code.
                    auto roomJoin = new RoomJoin;
                    // mUserID defaults to uninitialized garbage otherwise -
                    // every other outgoing packet sets this explicitly
                    // (e.g. exeInit()'s PlayerConnect); missing it here sent
                    // a bogus id that didn't match this connection's
                    // registered Connect-handshake id, and the server
                    // (correctly) disconnects on any packet whose id doesn't
                    // match - confirmed live: repeated instant reconnect-
                    // then-disconnect loops in the server log.
                    roomJoin->mUserID = mUserID;
                    roomJoin->roomCode = mRoomCode;
                    const bool roomJoinQueued = mSocket->queuePacket(roomJoin);
                    MpDebugLog::write("Client::startConnection: RoomJoin '%s' queued=%d",
                        mRoomCode.cstr(), roomJoinQueued);

                    if (!roomJoinQueued) {
                        mRoomError = MultiplayerProtocol::RoomError::Ended;
                        mRoomStatusMessage.copy("Failed to send room code.");
                        mIsConnectionActive = false;
                        break;
                    }

                    Packet* statusPacket = mSocket->tryGetPacketTimeout(cHandshakeTimeoutMs);
                    if (!statusPacket || statusPacket->mType != PacketType::ROOMSTATUS) {
                        MpDebugLog::write(
                            "Client::startConnection: no ROOMSTATUS (got %s type=%d)",
                            statusPacket ? "wrong packet" : "nothing",
                            statusPacket ? (int)statusPacket->mType : -1);
                        delete statusPacket;
                        mRoomError = MultiplayerProtocol::RoomError::Ended;
                        mRoomStatusMessage.copy("Server did not assign a room role.");
                        mIsConnectionActive = false;
                        break;
                    }
                    updateRoomStatus(static_cast<RoomStatus*>(statusPacket));
                    delete statusPacket;
                    if (!mRoomAssigned) {
                        mIsConnectionActive = false;
                        break;
                    }

                    // The room now knows our role, so it is safe to push the
                    // seed's multiplayer settings; the server drops them unless
                    // we are the setup owner.
                    sendRoomSettings();

                    // The main thread updates these cached packets while the
                    // read thread is reconnecting. Copy them under the same
                    // hand-off lock used by the puppet records, then queue
                    // only the local copies after the lock is released.
                    GameInf cachedGame;
                    CostumeInf cachedCostume;
                    CaptureInf cachedCapture;
                    bool hasGame = false;
                    bool hasCostume = false;
                    bool hasCapture = false;
                    {
                        PuppetStateGuard stateGuard;
                        if (lastGameInfPacket.mUserID.IsValid()) {
                            cachedGame = lastGameInfPacket;
                            hasGame = true;
                        }
                        if (!lastCostumeInfPacket.bodyModel.isEmpty()) {
                            cachedCostume = lastCostumeInfPacket;
                            hasCostume = true;
                        }
                        if (!lastCaptureInfPacket.hackName.isEmpty()) {
                            cachedCapture = lastCaptureInfPacket;
                            hasCapture = true;
                        }
                    }
                    if (hasGame) {
                        cachedGame.mUserID = mUserID;
                        mSocket->queuePacket(new (gHeap) GameInf(cachedGame));
                    }
                    if (hasCostume) {
                        cachedCostume.mUserID = mUserID;
                        mSocket->queuePacket(new (gHeap) CostumeInf(cachedCostume));
                    }
                    if (hasCapture) {
                        cachedCapture.mUserID = mUserID;
                        mSocket->queuePacket(new (gHeap) CaptureInf(cachedCapture));
                    }

                    break;
                }

                MpDebugLog::write("Client::startConnection: discarding unexpected packet type=%d while waiting for CLIENTINIT",
                    curPacket->mType);
                delete curPacket;
            } else {
                hk::diag::logLine("Receive failed! Stopping Connection.");
                MpDebugLog::write("Client::startConnection: tryGetPacket returned null while waiting for CLIENTINIT, aborting");
                mIsConnectionActive = false;
                break;
            }
        }
    }

    return mIsConnectionActive;
}

/**
 * @brief Opens up OS's software keyboard in order to change the currently used server IP.
 * @returns whether or not a new IP has been defined and needs to be saved.
 */
bool Client::openKeyboardIP() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is null!");
        return false;
    }

    sInstance->mKeyboard->openKeyboard(ServerConfig::getIp(), [](nn::swkbd::KeyboardConfig& config) {
        config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
        config.textMaxLength = MAX_HOSTNAME_LENGTH;
        config.textMinLength = 1;
        config.isUseUtf8 = true;
        config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
    });

    hostname prevIp;
    prevIp.copy(ServerConfig::getIp());

    while (!sInstance->mKeyboard->isDone())
        nn::os::YieldThread();

    if (!sInstance->mKeyboard->isCancelled() && sInstance->mKeyboard->getResult()
        && sInstance->mKeyboard->getResult()[0])
        // Persists to server.ini immediately - the override survives a
        // reboot the same way the shipped default does.
        ServerConfig::setOverride(sInstance->mKeyboard->getResult(), 0);

    return std::strcmp(prevIp.cstr(), ServerConfig::getIp()) != 0;
}

/**
 * @brief Opens up OS's software keyboard in order to change the currently used server port.
 * @returns whether or not a new port has been defined and needs to be saved.
 */
bool Client::openKeyboardPort() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is null!");
        return false;
    }

    char buf[0x6];
    snprintf(buf, 0x6, "%u", ServerConfig::getPort());

    sInstance->mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
        config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
        config.textMaxLength = 5;
        config.textMinLength = 2;
        config.isUseUtf8 = true;
        config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
    });

    const int prevPort = ServerConfig::getPort();

    while (!sInstance->mKeyboard->isDone())
        nn::os::YieldThread();

    if (!sInstance->mKeyboard->isCancelled() && sInstance->mKeyboard->getResult()
        && sInstance->mKeyboard->getResult()[0]) {
        const int newPort = atoi(sInstance->mKeyboard->getResult());
        if (newPort > 0)
            ServerConfig::setOverride(nullptr, newPort);
    }

    return prevPort != ServerConfig::getPort();
}

/**
 * @brief Sets the server IP address
 * @param ip The IP address or hostname to set
 */
void Client::setServerIP(const char* ip) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is null!");
        return;
    }

    hostname prevIp = sInstance->mServerIP;
    sInstance->mServerIP = ip;

    bool isFirstConnect = prevIp != sInstance->mServerIP;
    // sInstance->mSocket->setIsFirstConn(isFirstConnect);
}

/**
 * @brief Sets the server port
 * @param port The port number to set
 */
void Client::setServerPort(int port) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is null!");
        return;
    }

    int prevPort = sInstance->mServerPort;
    sInstance->mServerPort = port;

    bool isFirstConnect = prevPort != sInstance->mServerPort;
    // sInstance->mSocket->setIsFirstConn(isFirstConnect);
}

/**
 * @brief main thread function for read thread, responsible for processing packets from server
 *
 */
void Client::readFunc() {
    sead::ScopedCurrentHeapSetter setter(gHeap);

    hk::diag::logLine("Starting Client read thread");

    if (mConnectStatus)
        mConnectStatus->appear();

    if (mConnectStatus)
        al::startAction(mConnectStatus, "Loop", "Loop");

    // Skip startConnection() if a previous call already got mSocket to
    // CONNECTED (e.g. startThread() called again while already connected).
    if (!mSocket->isConnected() && !startConnection()) {
        hk::diag::logLine("Failed to Connect to Server.");

        mSocket->signalReset();
        hk::diag::logLine("Client Read Thread ending.");

        if (mConnectStatus)
            mConnectStatus->end();

        return;
    }

    if (mConnectStatus)
        mConnectStatus->end();

    while (mIsConnectionActive) {
        HK_ABORT_UNLESS(mSocket != nullptr, "Client::mSocket was nullptr");
        auto curPacket = mSocket->tryGetPacket();

        if (curPacket) {
            switch (curPacket->mType) {
            case PacketType::PLAYERINF:
                updatePlayerInfo(dynamic_cast<PlayerInf*>(curPacket));
                break;
            case PacketType::GAMEINF:
                updateGameInfo(dynamic_cast<GameInf*>(curPacket));
                break;
            case PacketType::HACKCAPINF:
                updateHackCapInfo(dynamic_cast<HackCapInf*>(curPacket));
                break;
            case PacketType::PLAYERCON: {
                updatePlayerConnect(dynamic_cast<PlayerConnect*>(curPacket));

                GameInf cachedGame;
                PlayerInf cachedPlayer;
                CostumeInf cachedCostume;
                CaptureInf cachedCapture;
                bool hasGame = false;
                bool hasPlayer = false;
                bool hasCostume = false;
                bool hasCapture = false;
                {
                    PuppetStateGuard stateGuard;
                    if (lastGameInfPacket != emptyGameInfPacket) {
                        cachedGame = lastGameInfPacket;
                        hasGame = true;
                    }
                    if (mHasLastPlayerInfPacket) {
                        cachedPlayer = lastPlayerInfPacket;
                        hasPlayer = true;
                    }
                    // Send a newly sampled pose on the next game-thread tick.
                    // The cached response is useful for a late join, but it
                    // may have crossed the peer's PLAYERCON on the wire.
                    mForcePlayerInfSend = true;
                    if (!lastCostumeInfPacket.bodyModel.isEmpty()) {
                        cachedCostume = lastCostumeInfPacket;
                        hasCostume = true;
                    }
                    if (!lastCaptureInfPacket.hackName.isEmpty()) {
                        cachedCapture = lastCaptureInfPacket;
                        hasCapture = true;
                    }
                }
                if (hasGame) {
                    cachedGame.mUserID = mUserID;
                    mSocket->send(new (gHeap) GameInf(cachedGame));
                }

                if (hasPlayer) {
                    cachedPlayer.mUserID = mUserID;
                    mSocket->send(new (gHeap) PlayerInf(cachedPlayer));
                }

                if (hasCostume) {
                    cachedCostume.mUserID = mUserID;
                    mSocket->send(new (gHeap) CostumeInf(cachedCostume));
                }

                if (hasCapture) {
                    cachedCapture.mUserID = mUserID;
                    mSocket->send(new (gHeap) CaptureInf(cachedCapture));
                }

                break;
            }
            case PacketType::COSTUMEINF:
                updateCostumeInfo(dynamic_cast<CostumeInf*>(curPacket));
                break;
            case PacketType::CAPTUREINF:
                updateCaptureInfo(dynamic_cast<CaptureInf*>(curPacket));
                break;
            case PacketType::SHINECOLL:
                updateShineInfo(dynamic_cast<ShineCollect*>(curPacket));
                break;
            case PacketType::PLAYERDC:
                curPacket->mUserID.print("Received Player Disconnect!");
                disconnectPlayer(dynamic_cast<PlayerDC*>(curPacket));
                break;
            case PacketType::CHANGESTAGE:
                sendToStage(dynamic_cast<ChangeStagePacket*>(curPacket));
                break;
            case PacketType::COINCOLLECTCOLL:
                updateCoinCollects(dynamic_cast<CoinCollectCollect*>(curPacket));
                break;
            case PacketType::CHECKPOINTGET:
                updateCheckpoints(dynamic_cast<CheckpointGet*>(curPacket));
                break;
            case PacketType::MOONROCKHIT:
                updateMoonRocks(dynamic_cast<MoonRockHit*>(curPacket));
                break;
            case PacketType::ABILITYUNLOCK:
                updateAbilityUnlock(dynamic_cast<AbilityUnlock*>(curPacket));
                break;
            case PacketType::CAPTUREUNLOCK:
                updateCaptureUnlock(dynamic_cast<CaptureUnlock*>(curPacket));
                break;
            case PacketType::COINCOLLECTUSE:
                updateCoinCollectUse(dynamic_cast<CoinCollectUse*>(curPacket));
                break;
            case PacketType::ROOMSTATUS:
                updateRoomStatus(dynamic_cast<RoomStatus*>(curPacket));
                break;
            case PacketType::TEAMROSTER:
                updateTeamRoster(dynamic_cast<TeamRoster*>(curPacket));
                break;
            // Snapshot chunks do not come through this queue. The room save transfer - the
            // half of the room protocol the client never implemented, and the
            // reason joining a room used to drop you on a plain file select
            // instead of into the host's seed - is handled one level down, in
            // SocketClient::recv(), so its 4 KB chunks never enter this
            // queue. Replay's final SnapshotCommit is the one exception: it
            // follows the replayed SharedEvents through this same FIFO so the
            // UI cannot leave until those events have actually been applied.
            // See RoomSnapshot::tryHandlePacket() for why.
            case PacketType::SNAPSHOTCOMMIT:
                RoomSnapshot::handleQueuedReplayCommit(curPacket);
                break;
            case PacketType::SHAREDEVENT:
                updateSharedEvent(dynamic_cast<SharedEvent*>(curPacket));
                break;
            case PacketType::COINRESERVERESULT:
                updateCoinReserveResult(dynamic_cast<CoinReserveResult*>(curPacket));
                break;
            case PacketType::DEATHLINK:
                updateDeathLink(dynamic_cast<DeathLink*>(curPacket));
                break;
            case PacketType::ADDCOIN:
                updateAddCoin(dynamic_cast<AddCoin*>(curPacket));
                break;
            case PacketType::SUBCOIN:
                updateSubCoin(dynamic_cast<SubCoin*>(curPacket));
                break;
            case PacketType::PLAYERDEATH:
                updatePlayerDeath(dynamic_cast<PlayerDeath*>(curPacket));
                break;
            case PacketType::MOONROCKKEYUNLOCK:
                updateMoonRockKeyUnlock(dynamic_cast<MoonRockKeyUnlock*>(curPacket));
                break;
            case PacketType::PAINTINGUNLOCK:
                updatePaintingUnlock(dynamic_cast<PaintingUnlock*>(curPacket));
                break;
            case PacketType::ACTORSYNC: {
                ActorSync* actorSync = dynamic_cast<ActorSync*>(curPacket);
                if (actorSync && !actorSync->mIsFail) {
                    SyncedActor::onReceive(actorSync->mUserID.m_Storage[0], actorSync->mActorUid,
                        actorSync->mKind, actorSync->mPayload, actorSync->mUsedSize);
                }
                break;
            }
            case PacketType::GAMESTART:
                PlayerEventLog::addEvent(curPacket->mUserID, PlayerEventLog::START, "");
                break;
            case PacketType::CLIENTINIT: {
                InitPacket* initPacket = dynamic_cast<InitPacket*>(curPacket);
                hk::diag::logLine("Server Max Player Size: %d", initPacket->maxPlayers);
                maxPuppets = initPacket->maxPlayers - 1;
                mPuppetHolder->resizeHolder(maxPuppets);

                break;
            }
            default:
                hk::diag::logLine("Discarding Unknown Packet Type.");
                break;
            }

            delete curPacket;
        } else {
            hk::diag::logLine("SocketClient::tryGetPacket() returned nullptr! Errno: 0x%x",
                mSocket->mSockErrno);
            break;
        }
    }

    mSocket->signalReset();
    hk::diag::logLine("Client Read Thread ending.");
}

// What the local player is riding, or null.
//
// Neither Jaxi nor the Scooter is a capture - they create no CapTargetInfo, so
// PlayerHackKeeper never sees them - and they mount the player with a bind.
// PlayerBindKeeper holds the vehicle's own sensor while Mario is on it, and the
// sensor's host is the vehicle.
static al::LiveActor* tryGetLocalRideActor(const PlayerActorHakoniwa* player) {
    if (!player || !player->mBindKeeper)
        return nullptr;

    al::HitSensor* bindSensor = player->mBindKeeper->getBindSensor();
    if (!bindSensor)
        return nullptr;

    al::LiveActor* host = AlUnlinked::getSensorHost(bindSensor);
    return CaptureSync::findRideName(host) ? host : nullptr;
}

void Client::sendPlayerInfPacket(const PlayerActorBase* playerBase, bool isYukimaru) {
    sead::ScopedCurrentHeapSetter setter(gHeap);
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    PuppetStateGuard stateGuard;

    if (!playerBase) {
        hk::diag::logLine("Error: Null Player Reference");
        return;
    }

    auto packet = new PlayerInf;
    packet->mUserID = sInstance->mUserID;

    PlayerActorHakoniwa* player = isYukimaru
        ? nullptr
        : const_cast<PlayerActorHakoniwa*>(static_cast<const PlayerActorHakoniwa*>(playerBase));
    PlayerHackKeeper* hackKeeper = player ? player->mHackKeeper : nullptr;
    al::LiveActor* hackActor = hackKeeper ? hackKeeper->getHack() : nullptr;

    // While captured, Mario's own actor is hidden and can remain at a reset
    // transform.  The leased remote capture must follow the actual hack actor,
    // so sample position, rotation, and (below) action from one frame-local
    // source.  getHack() is inline and its +0x68 field is confirmed by the
    // retail setupHack/endHack code.
    const al::LiveActor* poseSource = hackActor ? hackActor : playerBase;
    packet->playerPos = al::getTrans(poseSource);

    al::calcQuat(&packet->playerRot, poseSource);

    // A transition frame can expose an incompletely initialized hack actor.
    // Falling back to Mario keeps the packet valid; the next frame will use
    // the capture actor once its pose is ready.
    if ((!isFinitePosition(packet->playerPos) || !isFiniteRotation(packet->playerRot))
        && poseSource != playerBase) {
        packet->playerPos = al::getTrans(playerBase);
        al::calcQuat(&packet->playerRot, playerBase);
    }

    // During the first frame of a join/scene transition the engine can expose
    // an uninitialised transform. Do not publish it and make the server drop
    // the packet; the next valid frame will establish the puppet's pose.
    if (!isFinitePosition(packet->playerPos) || !isFiniteRotation(packet->playerRot)) {
        delete packet;
        return;
    }

    if (!isYukimaru) {
        for (size_t i = 0; i < 6; i++) {
            const float weight = player->mAnimator->getBlendWeight(i);
            // Some actions leave unused blend slots at sentinel/non-finite
            // values. Receivers have always ignored those individual slots;
            // put the previous safe value on the wire so a strict server does
            // not discard the otherwise valid position and rotation with it.
            packet->animBlendWeights[i]
                = std::isfinite(weight) && weight >= 0.0f && weight <= 1.0f
                ? weight
                : (sInstance->mHasLastPlayerInfPacket
                          ? sInstance->lastPlayerInfPacket.animBlendWeights[i]
                          : 0.0f);
        }

        const char* hackName = hackKeeper ? hackKeeper->getCurrentHackName() : nullptr;

        // isHack() rather than hackName != nullptr. getCurrentHackName() returns
        // null on scattered frames in the middle of a capture the player is
        // plainly still riding: measured on both clients, same mHackKeeper
        // pointer throughout, name alternating 'Frog' / (null) frame to frame.
        // Because sendCaptureInfPacket is edge-triggered on this flag, each
        // flicker sent a real "capture ended" / "capture started" pair, and the
        // receiver dutifully tore the puppet's capture model down and rebuilt
        // it - which is why a captured player read as invisible.
        //
        // isHack() is the plain mIsHack bool the keeper sets in startHack/
        // endHack, so it does not flicker; it just carries no name, hence the
        // latch below.
        // Two independent signals, because neither is trustworthy alone.
        //
        // getCurrentHackName() is a real function and was the original source
        // of truth, but it returns null on scattered frames in the middle of a
        // capture the player is plainly still riding (measured on both clients:
        // same mHackKeeper pointer, name alternating 'Frog'/(null)).
        //
        // isHack() reads mIsHack straight out of the decomp header's struct
        // layout. The header only static_asserts the TOTAL size of
        // PlayerHackKeeper, not where any individual field sits, so a wrong
        // offset would silently read false forever - and switching to it alone
        // is exactly when captures stopped being announced at all.
        //
        // So: either signal can start a capture, and it only ends after the
        // name has been null for a run of frames. Correct whichever one is the
        // reliable one, and the log below records both so we find out.
        // isHack() is BROKEN and is kept only as a third opinion: the log
        // reported keeperSaysHack=0 on every single line, including ones where
        // hackName was 'Frog'. mIsHack is one of seven consecutive bools whose
        // offsets the decomp header never pins down (it static_asserts only the
        // struct's total size), so it is reading the wrong byte.
        //
        // rs::isPlayerHack() would be the right answer - an out-of-line
        // function, correct by construction the way getCurrentHackName() is -
        // but it does not link (ld.lld undefined symbol, the sixth such
        // function this session).
        //
        // mHackActor IS that answer, and it is now trusted, because the
        // decomp pins both of its edges rather than leaving the offset to a
        // header's guess:
        //
        //   PlayerHackKeeper::startHack  *((_QWORD *)this + 13) = SensorHost
        //   PlayerHackKeeper::endHack    *(_QWORD *)(a1 + 104)  = 0
        //
        // 13 * 8 == 104 == 0x68 == mHackActor. Every way out of a capture goes
        // through that one endHack (rs::endHack, endHackDir, endHackWithDamage
        // and the rest all call it), so the field is null the instant the
        // player leaves and non-null for the whole ride - including the frames
        // where getCurrentHackName() blinks. It also can't be the wrong offset
        // in practice: the capture animation this mod sends is read off this
        // very pointer with al::getActionName, and remote captures animate.
        const bool keeperSaysHack = hackKeeper && hackKeeper->isHack();

        if (hackName != nullptr) {
            sInstance->lastHackName = hackName;
            sInstance->hackNullFrames = 0;

            // Which member of this hack's archive family the player is on, for
            // the captures whose HackName covers several models - the anagram
            // letters, the Fukuwarai face pieces, the two manholes. See
            // PuppetInfo::captureVariant.
            //
            // Latched here rather than in sendCaptureInfPacket because this is
            // the function that already holds the hack actor, and because it
            // has to be read at the same instant as the name: a capture that
            // ends between the two calls must not leave a stale index behind.
            // main.cpp sends the two packets back to back, in this order.
            //
            // al::getModelName is not in the mod's link map; the keeper's own
            // name field is the same read (al::getModelName is literally
            // "return actor->mModelKeeper->mName", ida_context/Library/
            // LiveActor/ActorModelFunction.md 0x008e2000).
            sInstance->lastHackVariant = -1;
            if (hackActor) {
                const al::ModelKeeper* hackModelKeeper = hackActor->getModelKeeper();
                const char* hackModelName
                    = hackModelKeeper ? hackModelKeeper->getName() : nullptr;
                sInstance->lastHackVariant = CaptureSync::findVariantIndex(
                    CaptureSync::findByHackName(hackName), hackModelName);
            }
        } else if (sInstance->hackNullFrames < 0x7FFFFFF) {
            sInstance->hackNullFrames++;
        }

        // A capture is on while the keeper still points at an actor - and,
        // as a bound on that pointer being stale, while the name has been seen
        // recently.
        //
        // AND, not OR, and that is the whole fix for "the puppet stays in the
        // capture for a second after I leave it". The old expression ORed the
        // name grace in, so leaving a capture left lastHackName set and
        // hackNullFrames counting, and the capture-end packet could not go out
        // until the grace expired: 90 frames, a second and a half of the other
        // player standing there as a Frog after they had already hopped out.
        //
        // Reversing the roles fixes both directions at once. mHackActor is
        // cleared inside endHack, so the end is now detected on the very frame
        // it happens; the name is only consulted to give up on a pointer that
        // somehow outlived its capture. That makes the grace a safety bound
        // rather than the mechanism, so it can be short - it only has to clear
        // the longest measured blackout of getCurrentHackName() (10 consecutive
        // frames mid-capture), not exceed the longest capture.
        constexpr s32 cHackNullGraceFrames = 30;
        const bool isHacking = hackActor != nullptr
            && (hackName != nullptr
                || (!sInstance->lastHackName.isEmpty()
                    && sInstance->hackNullFrames < cHackNullGraceFrames));

        if (sInstance->isClientCaptured != isHacking)
            MpDebugLog::write(
                "[Send] isClientCaptured %d -> %d keeperSaysHack=%d hackActor=%p hackName='%s' latched='%s' nullFrames=%d sent=%d",
                sInstance->isClientCaptured ? 1 : 0, isHacking ? 1 : 0,
                keeperSaysHack ? 1 : 0, hackActor,
                hackName ? hackName : "(null)", sInstance->lastHackName.cstr(),
                sInstance->hackNullFrames, sInstance->isSentCaptureInf ? 1 : 0);

        sInstance->isClientCaptured = isHacking;

        // Cleared only once the capture is genuinely over, so the first frame
        // of the NEXT one cannot announce the previous capture's model.
        if (!isHacking) {
            sInstance->lastHackName.clear();
            sInstance->lastHackVariant = -1;
        }

        // isHacking, not hackName, for the same reason: on a frame where the
        // name is momentarily null the player is still riding the capture, and
        // falling through to the else would send Mario's own action names for
        // it. mHackActor is a plain field and stays valid across those frames.
        if (isHacking && hackActor) {

            const char* actName = al::getActionName(hackActor);

            if (actName) {
                // This is the captured actor's own action ("Run", "HackLand",
                // "SwimWait", ...), which is why it goes into curCaptureAnim on
                // the far side rather than being read as one of Mario's.
                //
                // It is encoded with PlayerAnims and NOT with CaptureAnims,
                // despite the latter's name. CaptureAnims is a strict subset of
                // PlayerAnims (all 385 of its strings are among PlayerAnims'
                // 922) and it lacks the ordinary ones: a hacked Frog spends its
                // whole life in FrogStateHack playing "Run", "Wait", "Fall" and
                // "Brake", none of which CaptureAnims can name. Those frames
                // encoded to Unknown and the remote capture never animated.
                packet->actName = PlayerAnims::FindType(actName);

                // 922 names is not "every name in the game", so an exotic
                // capture can still play something unnameable - and from the
                // far side that is invisible: the capture just never changes
                // action. Say so once per distinct name.
                if (packet->actName == PlayerAnims::Type::Unknown
                    && noteUnknownCaptureAnim(actName)) {
                    MpDebugLog::write(
                        "[Send] capture action '%s' (hack=%s) is not in PlayerAnims - remote capture cannot play it",
                        actName, sInstance->lastHackName.cstr());
                }

                const char* subActName = AlUnlinked::getPlayingSklAnimName(hackActor, 1);
                packet->subActName = subActName ? PlayerAnims::FindType(subActName) : PlayerAnims::Type::Unknown;

                // Phase, not just which animation. See PlayerInfPacket.h for
                // why it rides in Mario's blend slots, and
                // PuppetInfo::captureAnimFrame for why the name on its own
                // leaves the two copies running out of step.
                const al::ModelKeeper* hackModel = hackActor->getModelKeeper();
                if (hackModel && hackModel->getAnimSkl()) {
                    packet->animBlendWeights[cCaptureAnimFrameSlot]
                        = al::getSklAnimFrame(hackActor, 0);
                    packet->animBlendWeights[cCaptureAnimMarkerSlot]
                        = cCaptureAnimFrameMarker;
                }

                const CaptureSync::Entry* entry = CaptureSync::findByHackName(sInstance->lastHackName.cstr());
                if (entry && entry->adapter && entry->adapter->sample) {
                    entry->adapter->sample(hackActor, &packet->animBlendWeights[2]);
                }
            } else {
                packet->actName = PlayerAnims::Type::Unknown;
                packet->subActName = PlayerAnims::Type::Unknown;
            }
        } else {
            packet->actName = PlayerAnims::FindType(player->mAnimator->mAnimFrameCtrl->getActionName());
            packet->subActName = PlayerAnims::FindType(player->mAnimator->mCurSubAnim.cstr());
        }

    } else {
        // The Bound Bowl Grand Prix. During the race the local player is not a
        // PlayerActorHakoniwa at all - it is a YukimaruRacePlayer, which has no
        // PlayerInfo, which is exactly what isYukimaru is derived from - so
        // there is no PlayerHackKeeper to ask and every ordinary capture signal
        // above is unavailable.
        //
        // This branch used to clear the capture state and send nothing but a
        // position, which is precisely what the Shiverian Racer was reported
        // as: "not synced, just Mario bouncing around". The remote puppet was
        // being told the racer's position and left wearing Mario's body.
        //
        // Nothing here has to be inferred. There is only one thing the player
        // can be while this branch runs, so the hack name is a constant, and
        // the racer's own actor is both the pose source and the animation
        // source. The receiver then takes the ordinary capture path: the pool
        // has a "Yukimaru" model wherever a racer is placed (see the
        // YukimaruRacer rows in CaptureSyncTable), and the snowball rotation
        // travels through the same adapter the captured lobby Shiverian uses.
        constexpr const char* cYukimaruHackName = "Yukimaru";

        for (size_t i = 0; i < 6; i++) {
            packet->animBlendWeights[i] = 0;
        }

        // The racer's own action ("Run", "Wait", ...), read off the actor that
        // is actually on screen, and named out of PlayerAnims like every other
        // capture's - the receiver decodes it into curCaptureAnim.
        const char* actName = al::getActionName(playerBase);
        packet->actName
            = actName ? PlayerAnims::FindType(actName) : PlayerAnims::Type::Unknown;
        packet->subActName = PlayerAnims::Type::Unknown;

        // Animation phase, same channel and same marker as an ordinary
        // capture's - without it the puppet's racer would step at the packet
        // rate and the hat would swim (see PuppetActor::syncCaptureAnimFrame).
        const al::ModelKeeper* racerModel = playerBase->getModelKeeper();
        if (racerModel && racerModel->getAnimSkl()) {
            packet->animBlendWeights[cCaptureAnimFrameSlot]
                = al::getSklAnimFrame(playerBase, 0);
            packet->animBlendWeights[cCaptureAnimMarkerSlot] = cCaptureAnimFrameMarker;
        }

        // YukimaruRacePlayer binds its snowball's "Rotate" joint to a quat at
        // +0x130, where the captured Yukimaru keeps the same thing at +0x148 -
        // different classes, same idea (ida_context/MapObj/YukimaruRacePlayer
        // .md 0x034ee28 vs Npc/Yukimaru.md 0x03f130c). The adapter's own
        // sample() reads the Yukimaru offset, so this one is sampled here and
        // decoded by the very same applyYukimaru on the far side.
        CaptureSync::sampleYukimaruRacePlayer(playerBase, &packet->animBlendWeights[2]);

        if (!sInstance->isClientCaptured)
            MpDebugLog::write("[Send] isClientCaptured 0 -> 1 (yukimaru race)");

        sInstance->isClientCaptured = true;
        sInstance->lastHackName = cYukimaruHackName;
        // One archive, no family.
        sInstance->lastHackVariant = -1;
        sInstance->hackNullFrames = 0;
    }

    // The vehicle's transform relative to the rider. Measured rather than
    // assumed: a rider is bound to a point ON the vehicle, so placing the
    // puppet's copy at the packet transform put the scooter beside the puppet
    // instead of under it. See PlayerInfPacket.h for the slot layout.
    if (al::LiveActor* rideActor = tryGetLocalRideActor(player)) {
        // Measured against packet->playerRot, NOT al::getQuat(player), and this
        // is the whole fix for a vehicle that arrived in the right place facing
        // the wrong way.
        //
        // The packet's rotation comes from al::calcQuat above - which derives a
        // quaternion from the actor's pose however that pose is stored - while
        // al::getQuat returns the pose keeper's own quat. For a player actor
        // those are different rotations, so a relative transform measured
        // against one and re-applied to the other is off by whatever separates
        // them. Position was unaffected because al::getTrans is the same read
        // either way, which is exactly how it looked: right place, wrong facing.
        //
        // Reading the vehicle with calcQuat too keeps both sides of the
        // subtraction in one basis, so playerRot * rel reproduces the vehicle's
        // orientation exactly whatever that basis turns out to be.
        sead::Quatf vehicleQuat = sead::Quatf::unit;
        al::calcQuat(&vehicleQuat, rideActor);

        const sead::Vector3f localOffset = RideTransform::unrotate(
            packet->playerRot, al::getTrans(rideActor) - packet->playerPos);
        const sead::Quatf relative
            = RideTransform::mul(RideTransform::conjugate(packet->playerRot), vehicleQuat);

        packet->animBlendWeights[cRideOffsetXSlot] = localOffset.x;
        packet->animBlendWeights[cRideOffsetYSlot] = localOffset.y;
        packet->animBlendWeights[cRideOffsetZSlot] = localOffset.z;
        packet->animBlendWeights[cRideQuatYSlot] = relative.y;
        packet->animBlendWeights[cRideQuatWSlot] = relative.w;
        packet->animBlendWeights[cRideMarkerSlot] = cRideMarker;

        // Once per mount, not once per session: the first version logged a
        // single line taken while Mario was still climbing on, so the numbers in
        // it were a transient. Keyed on the actor so remounting re-measures.
        //
        // Prints the WHOLE relative quaternion, not just the two components that
        // travel, so a vehicle that leans or pitches is visible here before
        // anyone spends wire on carrying it.
        static const al::LiveActor* sLoggedRideActor = nullptr;
        if (sLoggedRideActor != rideActor) {
            sLoggedRideActor = rideActor;
            MpDebugLog::write(
                "[Send] ride transform offset=(%.1f,%.1f,%.1f) rel=(x %.3f y %.3f z %.3f w %.3f)",
                localOffset.x, localOffset.y, localOffset.z, relative.x, relative.y, relative.z,
                relative.w);
        }
    }

    if (sInstance->mForcePlayerInfSend || sInstance->lastPlayerInfPacket != *packet) {
        sInstance->lastPlayerInfPacket = *packet;
        sInstance->mHasLastPlayerInfPacket = true;
        sInstance->mForcePlayerInfSend = false;
        sInstance->mSocket->queuePacket(packet);
    } else {
        delete packet;
    }
}

/**
 * @brief sends info related to player's cap actor to server
 *
 * @param hackCap pointer to cap actor, used to get translation, animation, and state info
 */
void Client::sendHackCapInfPacket(const HackCap* hackCap) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    const bool isFlying = hackCap->isFlying();

    // Cappy is also visible - and somewhere completely different - while the
    // player is riding a capture. The game hides HackCap's own model on the
    // first frame of the hack and shows its second "MarioCap"/"Display" actor
    // instead, and that display actor is posed by copyPose() straight from
    // HackCap's own trans/quat (HackCap::updateCapPose). So the pose that has
    // always been sent for a thrown cap is exactly the pose of the hat sitting
    // on the captured enemy - it was simply never sent in that state, which is
    // why remote captures wore a moustache and no hat.
    //
    // The two states are told apart by the value, not just the truthiness, of
    // isCapVisible: 1 = thrown/flying, 2 = worn on a capture. The packet layout
    // is unchanged (it was always a u8 "fake bool"), so a client that predates
    // this still reads 2 as "cap visible here", which is where the cap is.
    const bool isOnHack = cSyncCapOnHack && !isFlying && sInstance->isClientCaptured;

    if (isFlying || isOnHack) {
        auto packet = new (gHeap) HackCapInf;
        packet->mUserID = sInstance->mUserID;
        packet->capPos = al::getTrans(hackCap);

        packet->isCapVisible = isFlying ? cCapVisibleThrown : cCapVisibleOnHack;

        if (isFlying) {
            packet->capJointRot.x = hackCap->mJointKeeper->mJointRot.x;
            packet->capJointRot.y = hackCap->mJointKeeper->mJointRot.y;
            packet->capJointRot.z = hackCap->mJointKeeper->mJointRot.z;
            packet->capJointRot.w = hackCap->mJointKeeper->mSkew;
        } else {
            // Worn on a capture: the joint control this field normally carries
            // is throw geometry (brim bend and skew), and none of it applies to
            // a hat sitting still on an enemy's head. What DOES apply, and had
            // nowhere to travel, is the cap's scale: the game shrinks Cappy to
            // fit whatever it is riding (CapTargetInfo::
            // calcLockOnFollowTargetScale sets HackCap's scale, which
            // updateCapPose copies onto the visible display cap), so an
            // unscaled remote hat is comically large on a Frog.
            //
            // So this field carries the scale in this state, with w=0 marking
            // it. Deliberately not a new packet field: HackCapInf's byte layout
            // is shared with the relay server and every other client, and the
            // four floats here are dead weight for exactly as long as the cap
            // is worn.
            const sead::Vector3f& capScale = al::getScale(hackCap);
            packet->capJointRot.x = capScale.x;
            packet->capJointRot.y = capScale.y;
            packet->capJointRot.z = capScale.z;
            packet->capJointRot.w = 0.0f;
        }
        packet->capRot = al::getQuat(hackCap);

        // While hacking, HackCap's own action is whatever the throw that
        // started the capture left on it - its model is hidden from the first
        // frame of the hack, so the game never corrects it - and
        // al::getActionName can be null there. The hat the player actually sees
        // is the display cap, which the game puts in "NoAction" when a hack
        // starts (HackCap::forceHack). Send that, so the puppet's cap is a
        // still hat on the enemy's head rather than a frozen mid-spin throw.
        if (isFlying) {
            const char* capAnim = al::getActionName(hackCap);
            packet->capAnim = capAnim ? capAnim : "Wait";
        } else {
            packet->capAnim = "NoAction";
        }

        sInstance->mSocket->queuePacket(packet);

        sInstance->isSentHackInf = true;

    } else if (sInstance->isSentHackInf) {
        auto packet = new (gHeap) HackCapInf;
        packet->mUserID = sInstance->mUserID;
        packet->isCapVisible = cCapVisibleNone;
        packet->capPos = sead::Vector3f::zero;
        packet->capJointRot = sead::Quatf::unit;
        packet->capRot = sead::Quatf::unit;
        sInstance->mSocket->queuePacket(packet);
        sInstance->isSentHackInf = false;
    }
}

/**
 * @brief Sends both stage info and player 2D info to the server.
 * @param player
 * @param holder
 */
void Client::sendGameInfPacket(const PlayerActorHakoniwa* player, GameDataHolderAccessor holder) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    PuppetStateGuard stateGuard;

    auto packet = new (gHeap) GameInf;
    packet->mUserID = sInstance->mUserID;

    if (player) {
        packet->is2D = player->mDimensionKeeper->mIs2D;
    } else {
        packet->is2D = false;
    }

    packet->scenarioNo = holder.mData->getGameDataFile()->getScenarioNo();

    packet->stageName = GameDataFunction::getCurrentStageName(holder);

    packet->gameMode = -1;

    if (*packet != sInstance->lastGameInfPacket) {
        sInstance->lastGameInfPacket = *packet;
        sInstance->mSocket->queuePacket(packet);
    } else
        delete packet;
}

/**
 * @brief Sends only stage info to the server.
 * @param holder
 */
void Client::sendGameInfPacket(GameDataHolderAccessor holder) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    PuppetStateGuard stateGuard;

    auto packet = new (gHeap) GameInf;
    packet->mUserID = sInstance->mUserID;

    packet->is2D = false;

    packet->scenarioNo = holder.mData->getGameDataFile()->getScenarioNo();

    packet->stageName = GameDataFunction::getCurrentStageName(holder);

    packet->gameMode = -1;

    sInstance->lastGameInfPacket = *packet;

    sInstance->mSocket->queuePacket(packet);
}

/**
 * @brief Sends costume info packet.
 * @param body
 * @param cap
 */
void Client::sendCostumeInfPacket(const char* body, const char* cap) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    PuppetStateGuard stateGuard;

    auto packet = new (gHeap) CostumeInf;
    packet->bodyModel = body;
    packet->capModel = cap;
    packet->mUserID = sInstance->mUserID;
    sInstance->lastCostumeInfPacket = *packet;
    sInstance->mSocket->queuePacket(packet);
}

/**
 * @brief Sends capture info packet.
 * @param player
 */
void Client::queueActorSync(ActorSync* packet) {
    if (!packet)
        return;

    if (!sInstance || !sInstance->mSocket || !isSocketActive()) {
        delete packet;
        return;
    }

    packet->mUserID = sInstance->mUserID;

    // queuePacket returns false when the send queue is full. Dropping an actor
    // update on a full queue is correct - it is per-frame state that will be
    // resent on the next pass, unlike the one-shot packets elsewhere in this
    // file that must not be lost.
    if (!sInstance->mSocket->queuePacket(packet))
        delete packet;
}

void Client::sendCaptureInfPacket(const PlayerActorHakoniwa* player) {
    if (!sInstance) {
        return;
    }

    PuppetStateGuard stateGuard;

    // Requires a name, not just the flag. isHack() can go true a frame or more
    // before getCurrentHackName() first returns anything, and this send is
    // edge-triggered: announcing the capture on that first frame would put an
    // empty name into a packet that means "captured", the receiver would read
    // that as "not captured" (updateCaptureInfo derives isCaptured from the
    // name's length), and isSentCaptureInf would latch true so the correction
    // never went out. That is exactly what happened - neither client logged a
    // single "capture request change" for a capture both players could see.
    // Waiting for the name costs at most a frame or two of delay.
    const bool captured = sInstance->isClientCaptured && !sInstance->lastHackName.isEmpty();

    // A ride is not a capture and PlayerHackKeeper never sees it, so the only
    // way to ask what the player is on is to follow the bind: the vehicle's own
    // sensor is what PlayerBindKeeper holds while Mario is mounted, and its
    // host is the vehicle. CaptureSync::findRideName turns that actor back into
    // the registry key its placement carried - a name is not derivable from the
    // actor itself.
    const char* rideName = nullptr;
    if (!captured)
        rideName = CaptureSync::findRideName(tryGetLocalRideActor(player));

    const bool active = captured || rideName != nullptr;

    // "Manhole#1" rather than "Manhole" for the captures whose one HackName
    // covers a family of models - see PuppetInfo::captureVariant. A ride never
    // has one, and neither does the overwhelming majority of captures, in which
    // case this is the bare name it always was.
    char wireNameBuf[CaptureSync::cMaxWireNameLen];
    CaptureSync::encodeWireName(wireNameBuf, CaptureSync::cMaxWireNameLen,
        captured ? sInstance->lastHackName.cstr() : (rideName ? rideName : ""),
        captured ? sInstance->lastHackVariant : -1);
    const char* wireHackName = wireNameBuf;

    // The variant is part of the name, so a player who slides from one letter
    // to the next re-announces without any extra machinery: the string changed.
    const bool captureNameChanged = !al::isEqualString(
        sInstance->lastCaptureInfPacket.hackName.cstr(), wireHackName);

    // A capture-to-capture transition does not toggle isClientCaptured. It
    // still needs a new packet, otherwise remote puppets remain bound to the
    // previous capture actor until a later end/start cycle.
    if (active && (!sInstance->isSentCaptureInf || captureNameChanged)) {
        auto packet = new (gHeap) CaptureInf;
        packet->mUserID = sInstance->mUserID;
        // The latched name, not a fresh getCurrentHackName() - that call can
        // return null on the very frame the transition is sent, which would put
        // an empty name in a packet that means "captured".
        packet->hackName = wireHackName;
        MpDebugLog::write("[Send] CaptureInf %s hackName='%s' (%s)",
            sInstance->isSentCaptureInf ? "change" : "start", wireHackName,
            captured ? "capture" : "ride");
        sInstance->lastCaptureInfPacket = *packet;
        sInstance->mSocket->queuePacket(packet);
        sInstance->isSentCaptureInf = true;
    } else if (!active && sInstance->isSentCaptureInf) {
        auto packet = new (gHeap) CaptureInf;
        packet->mUserID = sInstance->mUserID;
        packet->hackName.clear();
        MpDebugLog::write("[Send] CaptureInf end");
        sInstance->lastCaptureInfPacket = *packet;
        sInstance->mSocket->queuePacket(packet);
        sInstance->isSentCaptureInf = false;
    }
}

/**
 * @brief Sends shine collect packet.
 * @param shineID
 */
void Client::sendShineCollectPacket(int shineID, int abilityId) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    // The lastCollectedShine latch that used to guard this is gone, and its
    // removal is the point rather than a side effect. It was set BEFORE the
    // queue attempt and the attempt's return value was discarded, so a moon the
    // send queue refused was lost AND permanently un-resendable: the latch then
    // suppressed every retry of that same id. Deduplication belongs in the
    // outbox, which dedups on the fact rather than on "the last one we tried",
    // and which cannot turn a failed send into a permanent loss.
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", shineID);
    // A Multi (triple) moon grants three abilities, and the receiving player
    // cannot derive ANY of them: unlockForMoon resolves against the sender's
    // own progression, so what it granted is knowable only to the sender. Value
    // has always carried the first; the second and third ride in auxiliary as
    // "id,id", which the server validates with IsAbilityIdList and, like Value,
    // deliberately leaves out of the event identity so the same moon still
    // dedups across players with different unlock state.
    //
    // Empty for the ordinary single-ability moon, which is also what an older
    // sender produces - so this stays compatible in that direction.
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> extraAbilities;
    for (s32 i = 1; i < AbilityRando::getLastUnlockedAbilityCount() && i < 3; i++) {
        const AbilityRando::AbilityEntry* extra = AbilityRando::getLastUnlockedAbility(i);
        if (!extra)
            continue;
        if (extraAbilities.isEmpty())
            extraAbilities.format("%d", static_cast<s32>(extra->id));
        else
            extraAbilities.appendWithFormat(",%d", static_cast<s32>(extra->id));
    }

    DurableSync::submit(MultiplayerProtocol::SharedEventKind::Moon, -1, abilityId, key.cstr(),
        extraAbilities.cstr());
}

/**
 * @brief Sends coin collect packet.
 * @param placeID
 * @param worldID
 * @param stage
 */
void Client::sendCoinCollectCollectPacket(const char* placeID, int worldID, const char* stage) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::PurpleCoin, worldID, 1, placeID, stage);
}

/**
 * @brief Sends checkpoint get packet.
 * @param objId
 */
void Client::sendCheckpointGetPacket(const char* objId) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    DurableSync::submit(MultiplayerProtocol::SharedEventKind::Checkpoint, -1, 0, objId, "");
}

/**
 * @brief Sends moon rock hit packet.
 * @param worldid
 */

void Client::sendMoonRockHitPacket(int worldId) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    if (worldId < 0 || worldId >= SNumMoonRocks)
        return;

    // Record it locally too. The state vector publishes this mask, so a moon
    // rock the server never heard about is still recoverable from the save
    // rather than only from the packet that may have been lost.
    DurableSync::setMoonRock(worldId);

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", worldId);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::MoonRock, worldId, 0, key.cstr(), "");
}

/**
 * @brief Sends an ability-unlock packet (purple-coin shop purchase).
 * @param abilityId AbilityRando::AbilityId of the ability that was just granted.
 */
void Client::sendAbilityUnlockPacket(int abilityId) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", abilityId);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::AbilityUnlock, -1, abilityId, key.cstr(), "");
}

/**
 * @brief Sends a capture-unlock packet (purple-coin shop purchase).
 * @param captureIndex CaptureRando::getCapture() index of the capture that was just granted.
 */
void Client::sendCaptureUnlockPacket(int captureIndex) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", captureIndex);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::CaptureUnlock, -1, captureIndex, key.cstr(), "");
}

/**
 * @brief Sends a regional-coin spend packet (purple-coin shop purchase).
 * @param worldID World the spend happened in.
 * @param count Number of coins spent.
 */
void Client::sendCoinCollectUsePacket(int worldID, int count) {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        MpDebugLog::write("sendCoinCollectUsePacket: sInstance null, worldID=%d count=%d dropped", worldID, count);
        return;
    }

    u64 requestId = 0;
    if (reserveCoinSpend(worldID, count, &requestId))
        commitCoinSpend(requestId);
}

bool Client::reserveCoinSpend(int worldID, int count, u64* requestId) {
    if (!sInstance || !requestId || !isSocketActive() || !isRoomReady() || count <= 0)
        return false;
    const u64 id = sInstance->mUserID.m_Storage[0]
        ^ (sInstance->mNextEventId++ * 0x9E3779B97F4A7C15ULL);
    sInstance->mCoinReserveResultId.store(0);
    sInstance->mCoinReserveDecision.store(0);
    auto packet = new (gHeap) CoinReserveRequest;
    packet->mUserID = sInstance->mUserID;
    packet->requestId = id;
    packet->worldId = worldID;
    packet->count = count;
    if (!sInstance->mSocket->queuePacket(packet))
        return false;
    for (s32 wait = 0; wait < 300; wait++) {
        if (sInstance->mCoinReserveResultId.load() == id) {
            *requestId = id;
            return sInstance->mCoinReserveDecision.load() > 0;
        }
        nn::os::YieldThread();
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(10));
    }
    releaseCoinSpend(id);
    return false;
}

void Client::commitCoinSpend(u64 requestId) {
    if (!sInstance || !requestId)
        return;
    auto packet = new (gHeap) CoinReserveCommit;
    packet->mUserID = sInstance->mUserID;
    packet->requestId = requestId;
    sInstance->mSocket->queuePacket(packet);
}

void Client::releaseCoinSpend(u64 requestId) {
    if (!sInstance || !requestId)
        return;
    auto packet = new (gHeap) CoinReserveRelease;
    packet->mUserID = sInstance->mUserID;
    packet->requestId = requestId;
    sInstance->mSocket->queuePacket(packet);
}

/**
 * @brief Sends a regular (yellow) coin pickup packet, so peers' coin totals
 *        stay in sync with each other, the same way regional coins already do.
 * @param count Number of coins just added to the local total.
 */
void Client::sendAddCoinPacket(int count) {
    if (!sInstance || count <= 0 || count > 999999)
        return;

    // Yellow-coin deltas are the one COUNTED thing in the outbox, and they only
    // belong there because the event now carries a per-client nonce the server
    // dedups on. Without that identity they could not be retried at all: a
    // resent "+50" is indistinguishable from earning 50 more, so the old bare
    // AddCoin packet had to be strictly fire-and-forget. The nonce makes them
    // at-most-once, which makes them as safe to retransmit as a moon.
    DurableSync::submit(MultiplayerProtocol::SharedEventKind::YellowCoinAdd, -1, count, "", "");
}

/**
 * @brief Sends a regular (yellow) coin spend packet (e.g. a shop purchase or
 *        Fool/curse penalty), so peers' coin totals stay in sync with each
 *        other, the same way regional coin spends already do.
 * @param count Number of coins just subtracted from the local total.
 */
void Client::sendSubCoinPacket(int count) {
    if (!sInstance || count <= 0 || count > 999999)
        return;

    // See sendAddCoinPacket for why a counted delta may ride the outbox.
    DurableSync::submit(MultiplayerProtocol::SharedEventKind::YellowCoinSpend, -1, count, "", "");
}

/**
 * @brief Sends a player-death packet, purely for the Player Event Log - the
 *        coin loss that comes with dying already rides sendSubCoinPacket
 *        like any other coin change.
 */
void Client::sendPlayerDeathPacket() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    auto packet = new (gHeap) PlayerDeath;
    packet->mUserID = sInstance->mUserID;

    sInstance->mSocket->queuePacket(packet);
}

/**
 * @brief Records and publishes a Moon Rock Key unlock.
 *
 * This used to build a bare MOONROCKKEYUNLOCK packet, which the server had no
 * case for: it fell through RoomManager's switch to the generic Server.Broadcast
 * relay, so it was never room-scoped, never team-scoped, never written to the
 * team log and never replayed on join. A player who joined afterwards - or who
 * simply restarted the game - never learned the rock was open, which is a real
 * soft-lock rather than a cosmetic gap. It is now a durable shared event like
 * every other unlock, plus a locally persisted bit so the state vector can
 * recover it even if the event itself is lost.
 *
 * @param kingdom World id of the kingdom whose key moon was just credited.
 */
void Client::sendMoonRockKeyUnlockPacket(int kingdom) {
    if (!sInstance || kingdom < 0 || kingdom >= SNumMoonRocks)
        return;

    DurableSync::setMoonRockKey(kingdom);

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", kingdom);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::MoonRockKey, kingdom, 0, key.cstr(), "");
}

/**
 * @brief Records and publishes a painting warp-route unlock.
 *
 * Same history as sendMoonRockKeyUnlockPacket above: previously an unlogged
 * PAINTINGUNLOCK broadcast, now a durable event plus a persisted bit.
 *
 * @param paintingIndex Index into the painting-link table that was just
 *        unlocked.
 */
void Client::sendPaintingUnlockPacket(int paintingIndex) {
    if (!sInstance || paintingIndex < 0 || paintingIndex >= SNumPaintingLinks)
        return;

    DurableSync::setPainting(paintingIndex);

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", paintingIndex);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::PaintingUnlock, -1, paintingIndex, key.cstr(), "");
}

/**
 * @brief Publishes a kingdom's scenario number advancing.
 *
 * Scenario advancement used to happen only inside updateShines()'s
 * scenarioSyncList walk, which requires a live StageScene - so it could not run
 * at all during a join replay at the file select, which is exactly when a late
 * joiner needs it. As a durable event it lands in the team log, replays on
 * join, and applies without a scene.
 *
 * Applied as max() on every receiver, never as a decrement.
 */
void Client::sendScenarioAdvancePacket(int worldId, int scenarioNo) {
    if (!sInstance || worldId < 0 || worldId >= SNumMoonRocks || scenarioNo <= 0
        || scenarioNo > 255)
        return;

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d:%d", worldId, scenarioNo);
    DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::ScenarioAdvance, worldId, scenarioNo, key.cstr(), "");
}

// Kingdom progression, published as STATE rather than as the mutations that
// produce it, and diffed from update() rather than sent from each mutation
// site - the same idiom, and the same reasoning, as syncKingdomVisits.
//
// The first attempt at this sent the branch choice from
// advanceProgressionUnlocks, which is where the player makes it. That was
// partial by construction: the frontier itself advances only inside
// KingdomProgress::tryCompleteFrontier, whose single caller requires the player
// to personally deposit moons at the globe, and forced peace flips in that same
// block. A team-mate holding identical moons could therefore sit on a stale
// frontier indefinitely with nothing to tell them otherwise. Describing the
// state cannot miss a mutation, because it never enumerates them.
void Client::syncKingdomProgress() {
    if (!sInstance || !isSocketActive() || !isRoomReady() || !DurableSync::isReady())
        return;

    GameDataFile* gdf = sInstance->mHolder.mData ? sInstance->mHolder->getGameDataFile() : nullptr;
    if (!gdf)
        return;

    KingdomProgress::SyncState state;
    if (!KingdomProgress::captureSyncState(gdf, &state))
        return;

    const u32 forcedPeace = Randomizer::getForcedPeaceKingdoms();

    // As with syncScenarioNumbers, the first observation after attaching to a
    // room is a baseline rather than a burst of events: the state vector
    // publishes the starting position in one packet, which the server
    // reconciles in a single pass.
    if (!sInstance->mKingdomBaselineSeeded) {
        sInstance->mPublishedCompletedCount = state.completedCount;
        for (s32 branch = 0; branch < KingdomProgress::cBranchCount; branch++)
            sInstance->mPublishedBranchChoice[branch] = state.branchChoice[branch];
        sInstance->mPublishedForcedPeace = forcedPeace;
        sInstance->mKingdomBaselineSeeded = true;
        return;
    }

    if (state.completedCount > sInstance->mPublishedCompletedCount) {
        sInstance->mPublishedCompletedCount = state.completedCount;
        sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
        key.format("%d", (s32)state.completedCount);
        DurableSync::submit(MultiplayerProtocol::SharedEventKind::KingdomComplete, -1,
            state.completedCount, key.cstr(), "");
    }

    for (s32 branch = 0; branch < KingdomProgress::cBranchCount; branch++) {
        const s8 choice = state.branchChoice[branch];
        if (choice < 0 || sInstance->mPublishedBranchChoice[branch] >= 0)
            continue;
        sInstance->mPublishedBranchChoice[branch] = choice;
        sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
        key.format("%d", branch);
        DurableSync::submit(MultiplayerProtocol::SharedEventKind::KingdomBranchOrder, branch,
            choice, key.cstr(), "");
    }

    const u32 newPeace = forcedPeace & ~sInstance->mPublishedForcedPeace;
    if (newPeace != 0) {
        sInstance->mPublishedForcedPeace |= newPeace;
        for (s32 world = 0; world < 32; world++) {
            if ((newPeace & (1u << world)) == 0)
                continue;
            sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
            key.format("%d", world);
            DurableSync::submit(
                MultiplayerProtocol::SharedEventKind::KingdomForcedPeace, world, 1, key.cstr(), "");
        }
    }
}

/**
 * @brief Sends game start packet.
 */
void Client::sendGameStartPacket() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    auto packet = new (gHeap) GameStart;
    packet->mUserID = sInstance->mUserID;

    sInstance->mSocket->queuePacket(packet);
}

/**
 * @brief Updates player info from packet.
 * @param packet
 */
void Client::updatePlayerInfo(PlayerInf* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);

    if (!curInfo)
        return;

    if (!curInfo->isConnected)
        curInfo->isConnected = true;

    const bool hasValidPose
        = isFinitePosition(packet->playerPos) && isFiniteRotation(packet->playerRot);
    if (hasValidPose) {
        const bool wasWaitingForPose = !curInfo->hasPlayerPose;
        curInfo->playerPos = packet->playerPos;
        curInfo->playerRot = packet->playerRot;
        curInfo->hasPlayerPose = true;

        if (wasWaitingForPose) {
            MpDebugLog::write(
                "[Pose] first valid player pose uid=%llx pos=(%.2f,%.2f,%.2f) rot=(%.3f,%.3f,%.3f,%.3f)",
                (unsigned long long)packet->mUserID.m_Storage[0], packet->playerPos.x,
                packet->playerPos.y, packet->playerPos.z, packet->playerRot.x,
                packet->playerRot.y, packet->playerRot.z, packet->playerRot.w);
        }
    }

    // Same field, same table (PlayerAnims) - only the actor it is aimed at
    // differs. Keeping the last valid value rather than overwriting it with
    // Unknown would freeze a capture on a stale action, so an unnamed action
    // clears it and PuppetActor leaves the model playing what it has.
    if (packet->actName == PlayerAnims::Type::Unknown
        || PlayerAnims::FindStr(packet->actName)[0] == '\0') {
        curInfo->curCaptureAnim = PlayerAnims::Type::Unknown;
    } else {
        curInfo->curCaptureAnim = packet->actName;
    }

    if (packet->subActName == PlayerAnims::Type::Unknown
        || PlayerAnims::FindStr(packet->subActName)[0] == '\0') {
        curInfo->curCaptureSubAnim = PlayerAnims::Type::Unknown;
    } else {
        curInfo->curCaptureSubAnim = packet->subActName;
    }

    // The capture's animation phase, present only when the marker says these
    // two slots are carrying one. A peer that does not send it leaves this
    // negative, and the puppet falls back to running the animation on its own
    // phase (and keeps Cappy off the capture, which is the part that needs the
    // two sides to agree).
    const float frameMarker = packet->animBlendWeights[cCaptureAnimMarkerSlot];
    const float captureFrame = packet->animBlendWeights[cCaptureAnimFrameSlot];
    if (frameMarker == cCaptureAnimFrameMarker && std::isfinite(captureFrame)
        && captureFrame >= 0.f) {
        curInfo->captureAnimFrame = captureFrame;
    } else {
        curInfo->captureAnimFrame = -1.f;
    }

    // The capture-state packet and the per-frame player packet are separate
    // TCP messages. They can cross at the application level when a player
    // starts/ends a capture, so do not use isCaptured to reinterpret this raw
    // action field. PuppetActor uses curCaptureAnim while a capture model is
    // actually leased; the body string remains a Mario action and is refreshed
    // as soon as the next normal frame arrives.
    if (!curInfo->isCaptured) {
        const char* playerAction = PlayerAnims::FindStr(packet->actName);
        if (playerAction[0] != '\0') {
            curInfo->curAnimStr = playerAction;
        } else {
            curInfo->curAnimStr = "Wait";
            if (packet->actName != PlayerAnims::Type::Unknown)
                hk::diag::logLine("[ERROR] %s: actName was out of bounds: %d", __func__, packet->actName);
        }
    }

    if (packet->subActName != PlayerAnims::Type::Unknown) {
        curInfo->curSubAnimStr = PlayerAnims::FindStr(packet->subActName);
        if (curInfo->curSubAnimStr.isEmpty())
            hk::diag::logLine("[ERROR] %s: subActName was out of bounds: %d", __func__, packet->subActName);
    } else {
        curInfo->curSubAnimStr.clear();
    }

    curInfo->curAnim = packet->actName;
    curInfo->curSubAnim = packet->subActName;

    for (size_t i = 0; i < 6; i++) {
        if (std::isfinite(packet->animBlendWeights[i]) && packet->animBlendWeights[i] >= 0.f
            && packet->animBlendWeights[i] <= 1.f) {
            curInfo->blendWeights[i] = packet->animBlendWeights[i];

            // Ride payload, not blend weights. Decoded here so the puppet never
            // sees the raw slots; PuppetActor skips its blending loop for a rider.
            curInfo->hasRideTransform = packet->animBlendWeights[cRideMarkerSlot] == cRideMarker;
            if (curInfo->hasRideTransform) {
                curInfo->rideOffset.x = packet->animBlendWeights[cRideOffsetXSlot];
                curInfo->rideOffset.y = packet->animBlendWeights[cRideOffsetYSlot];
                curInfo->rideOffset.z = packet->animBlendWeights[cRideOffsetZSlot];
                curInfo->rideQuatY = packet->animBlendWeights[cRideQuatYSlot];
                curInfo->rideQuatW = packet->animBlendWeights[cRideQuatWSlot];
            }
        }
    }

    // Only when the cap has no transform of its own. While it is worn on a
    // capture it does have one - the head of the captured enemy, well above the
    // player's own position - and overwriting it here every frame would drop
    // the hat through the capture and onto the floor.
    if (hasValidPose && !curInfo->isCapThrow && !curInfo->isCapOnHack) {
        curInfo->capPos = packet->playerPos;
    }
}

/**
 * @brief Updates hack cap info from packet.
 * @param packet
 */
void Client::updateHackCapInfo(HackCapInf* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);
    if (!curInfo)
        return;

    if (isFinitePosition(packet->capPos))
        curInfo->capPos = packet->capPos;

    if (packet->isCapVisible == cCapVisibleOnHack) {
        // capJointRot carries the worn cap's scale in this state, not a
        // rotation - see the sender. isFiniteRotation would reject most of the
        // useful range (it wants a unit-ish quaternion), so it is deliberately
        // not applied here.
        if (isFiniteScale(packet->capJointRot))
            curInfo->capScale.set(packet->capJointRot.x, packet->capJointRot.y,
                packet->capJointRot.z);
        else
            curInfo->capScale.set(1.f, 1.f, 1.f);

        // No throw geometry while the cap is worn: this quat is unpacked into
        // the joint keeper's rotation (xyz) and skew (w), and all four are zero
        // for a hat sitting still.
        curInfo->capRot = sead::Quatf(0.f, 0.f, 0.f, 0.f);
    } else {
        curInfo->capScale.set(1.f, 1.f, 1.f);
        if (isFiniteRotation(packet->capJointRot))
            curInfo->capRot = packet->capJointRot;
    }

    if (isFiniteRotation(packet->capRot))
        curInfo->capQuat = packet->capRot;
    curInfo->isCapThrow = packet->isCapVisible == cCapVisibleThrown;
    curInfo->isCapOnHack = packet->isCapVisible == cCapVisibleOnHack;
    curInfo->capAnim = packet->capAnim;
}

/**
 * @brief Updates capture info from packet.
 * @param packet
 */
void Client::updateCaptureInfo(CaptureInf* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);

    if (!curInfo)
        return;

    // The name may carry a "#N" variant suffix (CaptureSync::encodeWireName),
    // so split it before anything looks the row up. A peer on an older build
    // sends the bare name and decodeWireName answers -1 for it, which is the
    // "no preference" the pool already handles.
    char bareName[CaptureSync::cMaxWireNameLen];
    const s32 variant
        = CaptureSync::decodeWireName(bareName, CaptureSync::cMaxWireNameLen,
            packet->hackName.cstr());

    // The same string carries both a capture and a ride; only the table knows
    // which. A ride keeps the rider's own body and adds a vehicle underneath,
    // so isCaptured must stay false for one or PuppetActor would swap the body
    // for a scooter.
    const CaptureSync::Entry* entry = CaptureSync::findByHackName(bareName);
    const bool isRide = entry && entry->build == CaptureSync::Build::Ride;

    const bool wasCaptured = curInfo->isCaptured;
    const bool wasRiding = curInfo->isRiding;
    const bool isActive = bareName[0] != '\0';

    curInfo->isRiding = isActive && isRide;
    curInfo->isCaptured = isActive && !isRide;

    if (isActive) {
        curInfo->curHack = bareName;
        curInfo->captureVariant = static_cast<s8>(variant);
    } else {
        curInfo->curHack.clear();
        curInfo->captureVariant = -1;
        // A late end packet must not leave a capture-table index in the body
        // action slot while the next PlayerInf is still in flight.
        curInfo->curAnim = PlayerAnims::Type::Unknown;
        curInfo->curSubAnim = PlayerAnims::Type::Unknown;
        curInfo->curAnimStr = "Wait";
        curInfo->curSubAnimStr.clear();
    }

    if (wasCaptured != curInfo->isCaptured || wasRiding != curInfo->isRiding)
        MpDebugLog::write("updateCaptureInfo: uid=%llx captured=%d riding=%d hack=%s variant=%d",
            (unsigned long long)packet->mUserID.m_Storage[0], curInfo->isCaptured ? 1 : 0,
            curInfo->isRiding ? 1 : 0, curInfo->curHack.cstr(), curInfo->captureVariant);
}

/**
 * @brief Updates costume info from packet.
 * @param packet
 */
void Client::updateCostumeInfo(CostumeInf* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);

    if (!curInfo)
        return;

    curInfo->costumeBody = packet->bodyModel;
    curInfo->costumeHead = packet->capModel;
}

/**
 * @brief Stores shine ID from packet for later processing on the game thread.
 * @param packet
 */
void Client::updateShineInfo(ShineCollect* packet) {
    if (!packet)
        return;

    // Diagnostic for a real report: a moon someone collected didn't sync to
    // their partner, who had to re-collect it themselves to get credit -
    // meanwhile the COLLECTING player's own log showed that exact moon
    // coming back through this same receive path (self-echo?) with no live
    // stage actor involved. Logs whether this packet is this client's own
    // (echoed back to the sender) vs genuinely from someone else, plus the
    // sender's raw uid so both players' logs can be cross-referenced for the
    // same packet. isSelf=1 for an unexpectedly large fraction of moons, or
    // for a moon the OTHER player reports never receiving, would confirm
    // packets are looping back to their own sender instead of/as well as
    // reaching the intended recipient.
    MpDebugLog::write("updateShineInfo: shineID=%d senderUid0=%llx isSelf=%d",
        packet->shineId, (unsigned long long)packet->mUserID.m_Storage[0],
        packet->mUserID == Client::getClientId() ? 1 : 0);

    // Record this moon, then apply it. Three separate ways this used to lose
    // a moon outright, all of them silent:
    //
    //  1. Capacity. The guard was "collectedShineCount < size() - 1" wrapping
    //     the ENTIRE body, so once the table filled the moon was neither
    //     recorded nor applied - no log, no event, no grant. Nothing clears
    //     this table, so a long run filled it and every subsequent moon from
    //     the other player vanished.
    //  2. Duplicates. No membership check, so a self-echo, a reconnect or a
    //     server replay burned another slot for a moon already known,
    //     reaching (1) sooner.
    //  3. Holes. collectedShineCount was used BOTH as the append index here
    //     and as the loop bound in updateShines(). removeShine() blanks a slot
    //     in place and decrements the count, so after any removal the next
    //     append overwrote a live entry and updateShines() stopped short of
    //     the tail.
    //
    // Now: find the moon or a genuinely free slot, and treat the record as
    // bookkeeping rather than a precondition - a full table logs loudly and
    // still applies the moon, because failing to remember it must never mean
    // failing to grant it.
    // The scan and the store are one operation and have to be taken together:
    // this is the read thread, and updateShines() drains the same table on the
    // game thread. See ShineTableLock.
    bool alreadyRecorded = false;
    bool tableFull = false;
    {
        // Not acquiring is deliberately NOT a reason to drop the moon. The
        // unsynchronised store is the behaviour this table had for its whole
        // life and its failure mode is a rare lost apply that the state vector
        // still heals; refusing to record is a guaranteed one. 200 yields
        // against two short scans should never actually be reached.
        ShineTableLock lock;
        if (!lock.isAcquired())
            MpDebugLog::write(
                "updateShineInfo: shine table lock timed out - recording shineID=%d unsynchronised",
                packet->shineId);

        s32 freeSlot = -1;
        for (s32 i = 0; i < static_cast<s32>(curCollectedShines.size()); i++) {
            if (curCollectedShines[i] == packet->shineId) {
                alreadyRecorded = true;
                break;
            }
            if (freeSlot < 0 && curCollectedShines[i] < 0)
                freeSlot = i;
        }

        if (!alreadyRecorded) {
            if (freeSlot >= 0) {
                curCollectedShines[freeSlot] = packet->shineId;
                collectedShineCount++;
            } else {
                tableFull = true;
            }
        }
    }

    if (alreadyRecorded)
        MpDebugLog::write("updateShineInfo: shineID=%d already recorded - duplicate ignored",
            packet->shineId);

    if (!alreadyRecorded) {
        if (tableFull) {
            MpDebugLog::write(
                "updateShineInfo: shine record table FULL (%d entries) - shineID=%d applied but NOT recorded",
                static_cast<s32>(curCollectedShines.size()), packet->shineId);
        }

        if (packet->shineId >= 2000 && packet->shineId <= 2060) {
            // Same own-echo rule as the ordinary moon path below.
            if (packet->mUserID != Client::getClientId()) {
                PlayerEventLog::addEvent(
                    packet->mUserID, PlayerEventLog::SHINE,
                    PlayerEventLog::getAchievementMessage(toadetteMoons[packet->shineId - 2000]));
            }
            return;
        }

        GameDataFile::HintInfo* hintInfo = CustomGameDataFunction::getHintInfoByUniqueID(mHolder, packet->shineId);
        // findShine() answers null for a uid that is not in this save's hint
        // list, and every field access below dereferences this - on the read
        // thread, inside a packet handler. The identical call in updateShines()
        // has always been null-checked; this one was not, so a single event
        // carrying an out-of-range key (a legacy sender, a hand-edited log, a
        // reconciliation adopting a moon id this seed does not use) took the
        // game down rather than being ignored. The moon is already recorded
        // above, so returning here costs only the event-log line.
        if (!hintInfo) {
            MpDebugLog::write("updateShineInfo: shineID=%d is not in this save's hint list",
                packet->shineId);
            return;
        }
        // Under moon rando, packet->shineId is the *credited* moon (see the
        // NOTE in mpOnSetGotShine), so hintInfo identifies the destination
        // slot. getShineMessageWithSource reports it by its physical
        // location name plus the kingdom it's credited to when they differ
        // (e.g. "Bonneter Blockade (Sand)" for a moon physically sitting at
        // Bonneter Blockade but credited toward Sand Kingdom); getShineExtras
        // separately reports Moon Rock Key/painting/ability-capture info,
        // each rendered in its own color.
        const char* shineMessage = PlayerEventLog::getShineMessageWithSource(
            hintInfo->uniqueId, hintInfo->stageName, hintInfo->objId);
        PlayerEventLog::ShineExtras extras = PlayerEventLog::getShineExtras(hintInfo->uniqueId, hintInfo->stageName);
        // packet->abilityId rides along on the wire (ShineCollect.h) since
        // this player has no way to derive what the sender's own
        // unlockForMoon call granted - see mpOnSetGotShine (syncHooks.cpp)
        // for the send side and PlayerEventLog::fillAbilityById's comment.
        PlayerEventLog::fillAbilityById(extras, packet->abilityId, packet->extraAbilityIds.cstr());
        // Never log a moon back to the player who collected it.
        //
        // The local collection is already logged by mpOnSetGotShine
        // (syncHooks.cpp:151) the moment it happens. Anything arriving here
        // carrying OUR uid is therefore an echo, not news: the server's
        // broadcast of what we just sent, or a replay of it during a
        // reconcile after a rejoin. Both used to add a second identical row -
        // "the server re-logging what I already got".
        //
        // Guarded here rather than only in updateSharedEvent's own-echo scope
        // because this function has TWO callers: the shared-event apply (which
        // that scope covers) and the direct PacketType::SHINECOLL case, which
        // it does not. The state work above still runs either way - only the
        // duplicate log line is dropped.
        if (packet->mUserID != Client::getClientId())
            PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::SHINE, shineMessage, extras);
    }
}

/**
 * @brief Updates player connect info from packet.
 * @param packet
 */
void Client::updatePlayerConnect(PlayerConnect* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, true);

    if (!curInfo) {
        return;
    }

    if (curInfo->isConnected) {
        hk::diag::logLine("Info is already being used by another connected player!");
        packet->mUserID.print("Connection ID");
        curInfo->playerID.print("Target Info");

    } else {
        packet->mUserID.print("Player Connected! ID");

        // A free slot can belong to a player who disconnected a moment ago.
        // Reusing only the ID/connected bit leaves that player's costume,
        // capture, stage and action state attached to the newcomer until the
        // corresponding packets arrive (and can make the puppet build or
        // lease the wrong model). Reset the complete wire snapshot first so a
        // join is always an independent initial state.
        *curInfo = PuppetInfo();
        curInfo->playerID = packet->mUserID;
        curInfo->isConnected = true;
        curInfo->puppetName = packet->clientName;

        PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::CONNECT, "");
    }
}

/**
 * @brief Updates game info from packet, and handles scenario sync.
 * @param packet
 */
void Client::updateGameInfo(GameInf* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);

    if (!curInfo)
        return;

    if (curInfo->isConnected) {
        curInfo->scenarioNo = packet->scenarioNo;

        if (!packet->stageName.isEmpty() && packet->stageName.calcLength() > 3) {
            curInfo->stageName = packet->stageName;
            // Randomizer::sVisitedKingdoms (what gates the TrackerOverlay "?"
            // spoiler hide, and the world-map name coloring) is local-only -
            // only the local player's own StageScene::init calls
            // recordKingdomVisit(). A partner reaching a kingdom's home stage
            // first never reveals it on this client. recordKingdomVisit()
            // already does the stage-name -> kingdom mapping (including the
            // Shiveria Town special case) and dedups against the existing
            // bitmask on its own, so it's safe to call on every GameInf
            // update, not just on a stage-name change.
            //
            // Team-gated, and NOT on the cross-team sync flag. That flag
            // decides whether you can SEE a rival; this is progress - which
            // kingdoms have been reached - and kingdom progress is per team
            // like everything else. Turning rivals visible would otherwise
            // quietly reveal their map for you, which is a bigger concession
            // than "let me watch them run past".
            if (getTeamOf(packet->mUserID) == getTeam())
                Randomizer::recordKingdomVisit(packet->stageName.cstr());
        }

        curInfo->is2D = packet->is2D;
        curInfo->gameMode = packet->gameMode;
    }
}

/**
 * @brief Sends the player to a new stage as directed by a ChangeStagePacket.
 * @param packet
 */
void Client::sendToStage(ChangeStagePacket* packet) {
    if (!packet)
        return;

    GameDataHolderWriter accessor(mHolder);

    hk::diag::logLine("Sending Player to %s at Entrance %s in Scenario %d", packet->changeStage.cstr(),
        packet->changeID.cstr(), packet->scenarioNo);

    ChangeStageInfo info(accessor.mData, packet->changeID.cstr(), packet->changeStage.cstr(), false,
        packet->scenarioNo,
        static_cast<ChangeStageInfo::SubScenarioType>(packet->subScenarioType));
    info.setWipeType("FadeBlack");
    GameDataFunction::tryChangeNextStage(accessor, &info);
}

/**
 * @brief Handles a player disconnect packet.
 * @param packet
 */
void Client::disconnectPlayer(PlayerDC* packet) {
    if (!packet)
        return;

    PuppetStateGuard stateGuard;

    PuppetInfo* curInfo = findPuppetInfo(packet->mUserID, false);

    if (!curInfo || !curInfo->isConnected) {
        return;
    }

    curInfo->isConnected = false;

    curInfo->scenarioNo = -1;
    curInfo->stageName.clear();
    curInfo->isInSameStage = false;
    curInfo->costumeBody.clear();
    curInfo->costumeHead.clear();
    curInfo->curHack.clear();
    curInfo->captureVariant = -1;
    curInfo->isCaptured = false;
    curInfo->isStartCapture = false;
    curInfo->curAnim = PlayerAnims::Type::Unknown;
    curInfo->curSubAnim = PlayerAnims::Type::Unknown;
    curInfo->curCaptureAnim = PlayerAnims::Type::Unknown;
    curInfo->curCaptureSubAnim = PlayerAnims::Type::Unknown;
    curInfo->curAnimStr = "Wait";
    curInfo->curSubAnimStr.clear();
    curInfo->capAnim.clear();
    curInfo->capScale.set(1.f, 1.f, 1.f);
    curInfo->isCapThrow = false;
    curInfo->isCapOnHack = false;
    curInfo->isHoldThrow = false;

    mShouldStopRumble = true;

    PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::DISCONNECT, "");
}

void Client::disconnectAll() {
    for (s32 i = 0; i < getMaxPlayerCount() - 1; i++) {
        PuppetInfo* curInfo = sInstance->mPuppetInfoArr[i];

        if (curInfo->isConnected) {
            PlayerDC dc;
            dc.mUserID = curInfo->playerID;
            disconnectPlayer(&dc);
        }
    }
};

/**
 * @brief Checks if a shine has already been collected this session.
 * @param shineId
 */
bool Client::isShineCollected(int shineId) {
    ShineTableLock lock;
    for (size_t i = 0; i < curCollectedShines.size(); i++) {
        if (curCollectedShines[i] >= 0) {
            if (curCollectedShines[i] == shineId) {
                return true;
            }
        }
    }

    return false;
}

/**
 * @brief Finds a PuppetInfo slot by user ID.
 * @param id
 * @param isFindAvailable if true, returns first free slot when no match found
 */
PuppetInfo* Client::findPuppetInfo(const nn::account::Uid& id, bool isFindAvailable) {
    if (!sInstance)
        return nullptr;

    PuppetInfo* firstAvailable = nullptr;

    for (s32 i = 0; i < getMaxPlayerCount() - 1; i++) {
        PuppetInfo* curInfo = sInstance->mPuppetInfoArr[i];

        if (curInfo->playerID == id) {
            return curInfo;
        } else if (isFindAvailable && !firstAvailable && !curInfo->isConnected) {
            firstAvailable = curInfo;
        }
    }

    // if (!firstAvailable) {
    //     hk::diag::logLine("Unable to find Assigned Puppet for Player!");
    //     id.print("User ID");
    // }

    return firstAvailable;
}

/**
 * @brief Sets the current stage name and scenario number, and updates the puppet holder.
 * @param holder
 */
void Client::setStageInfo(HakoniwaSequence* sequence) {
    if (sInstance) {
        sInstance->mCurStageScene = (StageScene*)sequence->mCurrentScene;
        sInstance->mStageName = sequence->mStageName;
        sInstance->mScenario = sInstance->mHolder->getGameDataFile()->getScenarioNo();

        sInstance->mPuppetHolder->setStageInfo(sInstance->mStageName.cstr(), sInstance->mScenario);
    }
}

bool Client::tryAddPuppet(PuppetActor* puppet) {
    if (sInstance) {
        return sInstance->mPuppetHolder->tryRegisterPuppet(puppet);
    } else {
        return false;
    }
}

void Client::lockPuppetState() {
    Client* instance = sInstance;
    if (!instance)
        return;

    const u64 currentThreadToken = getPuppetStateThreadToken();

    // Model construction can synchronously invoke a hooked costume
    // initializer, which takes PuppetStateGuard again on this same thread.
    // Do not use C++ thread_local storage here: this code runs in the custom
    // NSS module's worker threads, where relying on ELF TLS is not safe.
    if (currentThreadToken != 0
        && instance->mPuppetStateLockOwner.load(std::memory_order_acquire) == currentThreadToken) {
        instance->mPuppetStateLockDepth.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    while (instance->mPuppetStateLock.test_and_set(std::memory_order_acquire)) {
        // ClientReadThread shares the game's main core. A pure yield loop can
        // immediately reschedule the waiting reader at the same priority and
        // starve the game thread that owns the lock, especially while a late
        // costume build spans a long frame. Sleep briefly so the owner gets a
        // real scheduling window; this path is only contention back-pressure.
        nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(1));
    }

    instance->mPuppetStateLockOwner.store(currentThreadToken, std::memory_order_release);
    instance->mPuppetStateLockDepth.store(1, std::memory_order_relaxed);
}

void Client::unlockPuppetState() {
    Client* instance = sInstance;
    if (!instance)
        return;

    const u64 currentThreadToken = getPuppetStateThreadToken();
    if (currentThreadToken == 0
        || instance->mPuppetStateLockOwner.load(std::memory_order_acquire) != currentThreadToken)
        return;

    if (instance->mPuppetStateLockDepth.fetch_sub(1, std::memory_order_relaxed) == 1) {
        instance->mPuppetStateLockOwner.store(0, std::memory_order_release);
        instance->mPuppetStateLock.clear(std::memory_order_release);
    }
}

PuppetActor* Client::getPuppet(int idx) {
    if (sInstance) {
        return sInstance->mPuppetHolder->getPuppetActor(idx);
    } else {
        return nullptr;
    }
}

PuppetInfo* Client::getLatestInfo() {
    if (sInstance) {
        return Client::getPuppetInfo(sInstance->mPuppetHolder->getSize() - 1);
    } else {
        return nullptr;
    }
}

PuppetInfo* Client::getPuppetInfo(int idx) {
    if (sInstance) {
        PuppetInfo* curInfo = sInstance->mPuppetInfoArr[idx];

        if (!curInfo) {
            hk::diag::logLine("Attempting to Access Puppet Out of Bounds! Value: %d", idx);
            return nullptr;
        }

        return curInfo;
    } else {
        return nullptr;
    }
}

// Discards every moon still waiting to be applied, so it is NOT part of the
// normal drain - updateShines() takes its entries out one slot at a time
// instead, precisely so that anything the read thread stores meanwhile
// survives. This is for tearing the table down, and it takes the lock because
// the read thread can be inside updateShineInfo() when it runs.
void Client::resetCollectedShines() {
    ShineTableLock lock;
    collectedShineCount = 0;
    curCollectedShines.fill(-1);
}

void Client::removeShine(int shineId) {
    ShineTableLock lock;
    for (size_t i = 0; i < curCollectedShines.size(); i++) {
        if (curCollectedShines[i] == shineId) {
            curCollectedShines[i] = -1;
            collectedShineCount--;
        }
    }
}

bool Client::isNeedUpdateShines() {
    if (!sInstance)
        return false;
    // Under the lock: this is only a hint - a stale answer costs at most one
    // frame, since updateShineInfo sets it again on the next moon - but reading
    // a count the read thread is mid-increment on is a data race whatever it is
    // used for, and this is called every single frame.
    ShineTableLock lock;
    return sInstance->collectedShineCount > 0;
}

struct storyShine {
    s32 uid;
    s32 worldId;
    const char* homeStage;
    s32 scenario;
};

struct moonRock {
    s32 worldId;
    s32 scenario;
};

inline constexpr storyShine scenarioSyncList[16] = {
    { 218, 1, "WaterfallWorldHomeStage", 2 }, { 495, 2, "SandWorldHomeStage", 2 },
    { 560, 2, "SandWorldHomeStage", 3 }, { 130, 3, "ForestWorldHomeStage", 2 },
    { 181, 3, "ForestWorldHomeStage", 3 }, { 424, 4, "LakeWorldHomeStage", 2 },
    { 37, 7, "CityWorldHomeStage", 2 }, { 95, 7, "CityWorldHomeStage", 4 },
    { 437, 8, "SeaWorldHomeStage", 2 }, { 1020, 9, "SnowWorldHomeStage", 2 },
    { 292, 10, "LavaWorldHomeStage", 2 }, { 290, 10, "LavaWorldHomeStage", 3 },
    { 795, 11, "BossRaidWorldHomeStage", 2 }, { 332, 12, "SkyWorldHomeStage", 2 },
    { 1055, 15, "Special1WorldHomeStage", 2 }, { 1061, 16, "Special2WorldHomeStage", 2 }
};

inline constexpr s32 moonRockScenarios[14] = { 4, 4, 5, 5, 4, 4, 4, 8, 4, 4, 8, 4, 4, 3 };

void Client::updateMoonRocks(MoonRockHit* packet) {
    if (!packet)
        return;

    if (!sInstance)
        return;

    if (packet->worldId < 0 || packet->worldId >= SNumMoonRocks)
        return;

    // Own-echo guard - see updateShineInfo. The local hit is logged by
    // syncHooks.cpp:294, and this function is reached both directly
    // (PacketType::MOONROCKHIT) and from the shared-event apply.
    if (packet->mUserID != Client::getClientId())
        PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::MOONROCK, worldNames[packet->worldId]);

    // Record it locally whether or not it can be applied yet - the state vector
    // republishes this mask, so a moon rock survives even if the event that
    // carried it is lost.
    DurableSync::setMoonRock(packet->worldId);

    // Deferred rather than applied here: this writes the scenario array and can
    // request a stage change, and this is the read thread. See
    // PendingRemoteAction - the stage change in particular is the reported
    // two-cutscenes soft-lock.
    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::MoonRock;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueA = packet->worldId;
    sInstance->enqueueRemoteAction(pending);
}

// Game thread, and only while no demo is running.
void Client::applyMoonRock(s32 worldId) {
    if (worldId < 0 || worldId >= SNumMoonRocks)
        return;

    if (!GameDataFunction::isGameClear(mHolder)) {
        mPendingMoonRocks[worldId] = true;
        return;
    }

    GameDataFile* gdf = mHolder.mData ? mHolder->getGameDataFile() : nullptr;

    if (!gdf) {
        hk::diag::logLine("applyMoonRock: GameDataFile null, dropping");
        return;
    }

    // sub scenarios like kfr are reflected in getScenarioNo but not in the scenario array; dont wanna
    // warp someone out of kfr
    bool isSubScenario = gdf->getScenarioNumArr()[worldId] != gdf->getScenarioNo();
    // dont warp if youre already in the scenario
    bool isAlreadyMoonRock = gdf->getScenarioNumArr()[worldId] == moonRockScenarios[worldId];

    gdf->getScenarioNumArr()[worldId] = moonRockScenarios[worldId];
    // Keep the outbound scenario diff in step, so syncScenarioNumbers does not
    // propose this advance straight back at the team that just sent it.
    if (moonRockScenarios[worldId] > mPublishedScenarioNo[worldId])
        mPublishedScenarioNo[worldId] = (u8)moonRockScenarios[worldId];

    if (!isSubScenario && !isAlreadyMoonRock && al::isEqualString(homeStageNames[worldId], gdf->getStageNameCurrent())) {
        ChangeStageInfo info(mHolder, "MoonRock", gdf->getStageNameCurrent());
        info.mWipeType = "FadeWhite";
        GameDataFunction::tryChangeNextStage(gdf->getGameDataHolder(), &info);
    }
}

void Client::updateAbilityUnlock(AbilityUnlock* packet) {
    if (!packet || !sInstance)
        return;

    // Read-thread validation only. The save write, the overlay and the event
    // log all happen on the game thread - see PendingRemoteAction.
    if (!AbilityRando::getAbility(packet->abilityId))
        return;

    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::AbilityUnlock;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueA = packet->abilityId;
    sInstance->enqueueRemoteAction(pending);
}

void Client::updateCaptureUnlock(CaptureUnlock* packet) {
    if (!packet || !sInstance)
        return;

    if (!CaptureRando::getCapture(packet->captureIndex))
        return;

    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::CaptureUnlock;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueA = packet->captureIndex;
    sInstance->enqueueRemoteAction(pending);
}

void Client::updateCoinCollectUse(CoinCollectUse* packet) {
    if (!packet || !sInstance)
        return;

    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::CoinCollectUse;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueA = packet->worldID;
    pending.valueB = packet->count;
    sInstance->enqueueRemoteAction(pending);
}

void Client::updateRoomStatus(RoomStatus* packet) {
    if (!packet || !sInstance)
        return;
    const bool wasReady = mRoomState == MultiplayerProtocol::RoomState::Ready;
    mRoomRole = packet->role;
    mRoomState = packet->state;
    mRoomError = packet->error;
    mRoomFlags = packet->flags;
    // The revision beside it counts within THIS team's log, so the two are
    // stored together or not at all - see RoomStatus in RoomProtocol.h.
    mRoomTeam = packet->team < MultiplayerProtocol::cTeamCount ? packet->team : 0;
    // The server's HEAD revision, and nothing more. This used to be written
    // straight into the single number catch-up asks from, which is a
    // correctness bug on the reconnect path and the reason a returning player
    // silently skipped everything their team did while they were away: adopting
    // the head means a client that has applied none of those events believes it
    // has seen all of them, so its next requestEventCatchUp asks for events
    // after the head and is correctly told there are none. What this client has
    // actually applied is DurableSync's watermark, which is persisted per room
    // and team and only ever advances over events that really arrived.
    mRoomHeadRevision = packet->revision;
    mRoomSessionId[0] = packet->sessionId[0];
    mRoomSessionId[1] = packet->sessionId[1];
    mRoomStatusMessage.copy(packet->message);
    const bool wasAssigned = mRoomAssigned;
    mRoomAssigned = packet->error == MultiplayerProtocol::RoomError::None;

    // On the FIRST status of a join the SERVER's team wins, and the local
    // picker adopts it.
    //
    // This used to be the other way round - a mismatch here pushed the local
    // pick at the server - which was right while a room lived only as long as
    // somebody was connected to it: the server had no memory of you, so the
    // pick was the only thing that could possibly know your team. It is now
    // exactly wrong. A room remembers which team each member was on for its
    // whole life, across a full disconnect and across a server restart
    // (server/Server/RoomStore.cs), and that memory is the entire point of
    // being able to resume: the save you are about to be handed is rebuilt
    // from THAT team's event log. Announcing a stale team-1 default over it
    // would move a returning player off their own run and replay somebody
    // else's progress into their save.
    //
    // The picker still wins whenever the player actually uses it - that goes
    // through setLocalTeam(), which sends its own TeamSelect - and the file
    // select sends one more immediately before the snapshot request, which is
    // the one that has to be ordered against it.
    if (!wasAssigned && mRoomAssigned) {
        mLocalTeam = mRoomTeam;
        // And the file select's own picker with it, which reads this rather
        // than mLocalTeam - otherwise a resuming player is shown "Team 1" on
        // the Multiplayer page while the server has them on team 3, and the
        // first thing they would do about it is roll the row and actually
        // move themselves off their own run.
        Randomizer::setSelectedTeam(mRoomTeam);
    }

    // Attach the durable outbox and watermark to this room+team+session. It
    // reloads a matching persisted file - resuming an interrupted session with
    // its unacknowledged events and its applied revision intact - and discards
    // one belonging to a different room, team or server session.
    //
    // This runs on the read thread, so it only records the identity; the SD
    // read itself happens on the next game-thread tick in DurableSync::update.
    if (mRoomAssigned)
        DurableSync::noteRoom(mRoomCode.cstr(), mRoomTeam, mRoomSessionId);

    // The room turning Ready is the single most likely moment for a backlog to
    // exist and to become sendable: everything collected while the host was
    // still uploading the seed was rejected with room-not-Ready, and is sitting
    // in the outbox on a long backoff waiting for exactly this.
    if (!wasReady && mRoomState == MultiplayerProtocol::RoomState::Ready)
        DurableSync::flushNow();

    MpDebugLog::write(
        "RoomStatus role=%d state=%d error=%d team=%d head=%llu applied=%llu message=%s",
        (int)mRoomRole, (int)mRoomState, (int)mRoomError, (int)mRoomTeam,
        (unsigned long long)mRoomHeadRevision,
        (unsigned long long)DurableSync::getAppliedRevision(), mRoomStatusMessage.cstr());
}

void Client::updateTeamRoster(TeamRoster* packet) {
    if (!packet || !sInstance)
        return;

    const s32 count = packet->count < MultiplayerProtocol::cMaxRosterEntries
        ? (s32)packet->count
        : MultiplayerProtocol::cMaxRosterEntries;
    for (s32 i = 0; i < count; i++)
        mRoster[i] = packet->entries[i];
    // Written before the count so the UI thread, which walks 0..mRosterCount
    // without a lock, can never read an entry this loop has not filled yet.
    mRosterCount = count;

    // The roster is also the authority on our own team once it lands: a
    // RoomStatus can be in flight when someone else's change rewrites it.
    for (s32 i = 0; i < count; i++) {
        if (mRoster[i].id[0] == mUserID.m_Storage[0] && mRoster[i].id[1] == mUserID.m_Storage[1]) {
            mRoomTeam = mRoster[i].team < MultiplayerProtocol::cTeamCount ? mRoster[i].team : 0;
            break;
        }
    }

    MpDebugLog::write("TeamRoster count=%d ownTeam=%d", count, (int)mRoomTeam);
}

void Client::setLocalTeam(u8 team) {
    if (!sInstance)
        return;
    sInstance->mLocalTeam = team < MultiplayerProtocol::cTeamCount ? team : 0;
    // Push it straight away when there is a room to push it to. The file
    // select also sends one explicitly right before joining, which covers the
    // case where the pick was made while disconnected.
    if (isSocketActive() && isRoomAssigned())
        sendTeamSelect();
}

void Client::sendTeamSelect() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    auto packet = new (gHeap) TeamSelect;
    packet->mUserID = sInstance->mUserID;
    packet->team = sInstance->mLocalTeam;

    // Read nothing off `packet` past this point - the send thread owns it.
    const u8 team = sInstance->mLocalTeam;
    const bool queued = sInstance->mSocket->queuePacket(packet);
    MpDebugLog::write("Client::sendTeamSelect: team=%d queued=%d", (int)team, queued ? 1 : 0);
}

// Shared events the client proposes directly, rather than through a dedicated
// packet the server translates (as ShineCollect and friends are). The server
// accepts a client-proposed SharedEvent as-is, assigns it a revision within the
// sender's team and fans it out - which is exactly what these two need, since
// their whole point is to be replayed out of the team log on a later join.
// Every durable proposal goes through the outbox now, so the isSocketActive/
// isRoomReady gate that used to sit here is deliberately gone: an event
// produced while the socket is down or the room is still Configuring is
// recorded and retransmitted rather than discarded. That gate is precisely how
// a moon collected during a reconnect - or before the host finished uploading
// the seed - used to be lost with no trace.
static bool queueSharedEvent(MultiplayerProtocol::SharedEventKind kind, s32 worldId, s32 value,
    const char* key, const char* auxiliary) {
    return DurableSync::submit(kind, worldId, value, key, auxiliary) != 0;
}

bool Client::sendKingdomVisitPacket(int worldId) {
    if (!sInstance || worldId < 0 || worldId >= 32)
        return false;
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    key.format("%d", worldId);
    const bool queued = queueSharedEvent(
        MultiplayerProtocol::SharedEventKind::KingdomVisit, worldId, 0, key.cstr(), "");
    MpDebugLog::write("Client::sendKingdomVisitPacket: world=%d queued=%d", worldId,
        queued ? 1 : 0);
    return queued;
}

bool Client::sendHintUnlockPacket(int unlockKind, int shineIndex, const char* flagKey, int category,
    int amount, u32 buyerPrePurchaseCount) {
    if (!sInstance)
        return false;

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> key;
    if (flagKey && flagKey[0])
        key.copy(flagKey);
    else
        key.format("%d", shineIndex);

    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> auxiliary;
    auxiliary.format("%d:%d:%u", category, amount, buyerPrePurchaseCount);

    // The nonce-mixed event id this used to build for itself is now assigned by
    // the outbox, which uses the identical scheme for every kind - hint
    // purchases are simply the first place it was needed, because they are
    // repeatable and so cannot dedup on their payload.
    const bool queued = queueSharedEvent(MultiplayerProtocol::SharedEventKind::HintUnlock,
        unlockKind, shineIndex, key.cstr(), auxiliary.cstr());
    MpDebugLog::write(
        "Client::sendHintUnlockPacket: kind=%d shine=%d key=%s cat=%d amount=%d count=%u queued=%d",
        unlockKind, shineIndex, key.cstr(), category, amount, buyerPrePurchaseCount,
        queued ? 1 : 0);
    return queued;
}

// Same pattern as syncKingdomVisits below, and for the same reason: driven from
// update() rather than from the places that change a scenario number, so every
// path that can raise one is covered without each having to remember to. There
// are a lot of them - story moons, moon rocks, the Metro festival script,
// post-peace advancement - and missing any one of them silently desyncs that
// kingdom.
void Client::syncScenarioNumbers() {
    if (!sInstance || !isSocketActive() || !isRoomReady() || !DurableSync::isReady())
        return;

    GameDataFile* gdf = sInstance->mHolder.mData ? sInstance->mHolder->getGameDataFile() : nullptr;
    if (!gdf)
        return;

    auto scenarios = gdf->getScenarioNumArr();

    // The first observation after attaching to a room is a baseline, not a
    // burst of events: whatever this save already had is published by the state
    // vector instead, which the server reconciles in one pass rather than as
    // one durable event per kingdom.
    if (!sInstance->mScenarioBaselineSeeded) {
        for (s32 world = 0; world < SNumMoonRocks; world++) {
            const s32 scenario = scenarios[world];
            sInstance->mPublishedScenarioNo[world]
                = (u8)(scenario > 0 ? (scenario < 255 ? scenario : 255) : 0);
        }
        sInstance->mScenarioBaselineSeeded = true;
        return;
    }

    for (s32 world = 0; world < SNumMoonRocks; world++) {
        const s32 scenario = scenarios[world];
        if (scenario <= 0 || scenario <= sInstance->mPublishedScenarioNo[world])
            continue;
        sInstance->mPublishedScenarioNo[world] = (u8)(scenario < 255 ? scenario : 255);
        sendScenarioAdvancePacket(world, scenario);
    }
}

void Client::syncKingdomVisits() {
    if (!sInstance || !isSocketActive() || !isRoomReady())
        return;

    const u32 visited = Randomizer::getVisitedKingdoms();
    const u32 published = sInstance->mPublishedKingdoms.load();
    u32 pending = visited & ~published;
    if (pending == 0)
        return;

    for (s32 worldId = 0; worldId < 32 && pending != 0; worldId++) {
        if ((pending & (1u << worldId)) == 0)
            continue;
        pending &= ~(1u << worldId);
        if (sInstance->sendKingdomVisitPacket(worldId))
            sInstance->mPublishedKingdoms.fetchOr(1u << worldId);
    }
}

void Client::syncTravelingPeachStart() {
    if (!sInstance || !isSocketActive() || !isRoomReady())
        return;

    if (sInstance->mTravelingPeachRetryFrames > 0) {
        sInstance->mTravelingPeachRetryFrames--;
        return;
    }

    if (!sInstance->mHolder.mData)
        return;

    GameDataFile* file = sInstance->mHolder->getGameDataFile();
    if (!file || !file->customIsStartWorldTravelingPeach()
        || sInstance->mTravelingPeachPublished.load(std::memory_order_acquire))
        return;

    // Was queueSharedEvent(..., mNextEventId++, ...), which the desync work
    // replaced: DurableSync::submit assigns the eventId itself, persists the
    // fact before sending, dedupes against anything already in flight, and owns
    // the retransmit. It returns 0 only for solo play or a genuinely full
    // outbox. The retry counter below is kept anyway so this does not call
    // submit on every frame while waiting.
    const u64 eventId = DurableSync::submit(
        MultiplayerProtocol::SharedEventKind::TravelingPeachStart, -1, 1, "start", "");
    const bool queued = eventId != 0;

    sInstance->mTravelingPeachRetryFrames = queued ? 60 : 1;
    if (queued) {
        sInstance->mTravelingPeachPublished.store(true, std::memory_order_release);
        PlayerEventLog::addSelfEvent(PlayerEventLog::PEACH_QUEST, "");
    }
}

static bool parseHintUnlockAuxiliary(const char* text, u8* outCategory, s32* outAmount) {
    if (!text || !outCategory || !outAmount)
        return false;

    char* end = nullptr;
    const long category = std::strtol(text, &end, 10);
    if (end == text || *end++ != ':' || category < 0
        || category >= Randomizer::kHintPriceCategoryCount)
        return false;

    char* amountEnd = nullptr;
    const long amount = std::strtol(end, &amountEnd, 10);
    if (amountEnd == end || *amountEnd++ != ':' || amount <= 0 || amount > INT_MAX)
        return false;

    // The buyer's pre-purchase category count records the paid ladder step for
    // diagnostics. The receiver does not need its value, but still validates
    // the field so malformed packets never enter the game-thread queue.
    char* countEnd = nullptr;
    std::strtoul(amountEnd, &countEnd, 10);
    if (countEnd == amountEnd || *countEnd != '\0')
        return false;

    *outCategory = static_cast<u8>(category);
    *outAmount = static_cast<s32>(amount);
    return true;
}

static bool encodeHintFlagIndex(s32 unlockKind, u8 category, const char* flagKey,
    u16* outIndexPlusOne) {
    if (!outIndexPlusOne)
        return false;

    *outIndexPlusOne = 0;
    const auto unlock = static_cast<HintSystem::SharedUnlock>(unlockKind);
    if (unlock < HintSystem::SharedUnlock::EconomyOnly
        || unlock > HintSystem::SharedUnlock::FlagHint)
        return false;
    if (unlock != HintSystem::SharedUnlock::FlagHint)
        return true;
    if (!flagKey)
        return false;

    constexpr const char* cCapturePrefix = "HintCapture_";
    constexpr const char* cAbilityPrefix = "HintAbility_";
    constexpr size_t cFlagPrefixLength = 12;
    const auto priceCategory = static_cast<Randomizer::HintPriceCategory>(category);

    if (priceCategory == Randomizer::HintPriceCategory::Captures) {
        if (std::strncmp(flagKey, cCapturePrefix, cFlagPrefixLength) != 0
            || !flagKey[cFlagPrefixLength])
            return false;

        const CaptureRando::CaptureEntry* target = CaptureRando::findCaptureByHackName(flagKey + cFlagPrefixLength);
        for (s32 i = 0; target && i < CaptureRando::getCaptureCount(); i++) {
            if (CaptureRando::getCapture(i) == target) {
                *outIndexPlusOne = static_cast<u16>(i + 1);
                return true;
            }
        }
        return false;
    }

    if (priceCategory == Randomizer::HintPriceCategory::Abilities) {
        if (std::strncmp(flagKey, cAbilityPrefix, cFlagPrefixLength) != 0
            || !flagKey[cFlagPrefixLength])
            return false;

        const AbilityRando::AbilityEntry* ability = AbilityRando::findAbilityByKey(flagKey + cFlagPrefixLength);
        if (!ability)
            return false;
        *outIndexPlusOne = static_cast<u16>(ability->id + 1);
        return true;
    }

    return false;
}

static bool decodeHintFlagKey(const PendingHintUnlock& pending,
    sead::FixedSafeString<MultiplayerProtocol::cEventKeySize>* outFlagKey) {
    if (!outFlagKey)
        return false;

    outFlagKey->clear();
    if (static_cast<HintSystem::SharedUnlock>(pending.unlockKind)
        != HintSystem::SharedUnlock::FlagHint)
        return true;
    if (pending.flagIndexPlusOne == 0)
        return false;

    const s32 index = static_cast<s32>(pending.flagIndexPlusOne) - 1;
    const auto category = static_cast<Randomizer::HintPriceCategory>(pending.category);
    if (category == Randomizer::HintPriceCategory::Captures) {
        const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(index);
        if (!capture)
            return false;
        outFlagKey->format("HintCapture_%s", capture->hackName);
        return true;
    }
    if (category == Randomizer::HintPriceCategory::Abilities) {
        const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(index);
        if (!ability)
            return false;
        outFlagKey->format("HintAbility_%s", ability->key);
        return true;
    }
    return false;
}

bool Client::enqueueHintUnlock(const PendingHintUnlock& pending) {
    const s32 tail = mPendingHintUnlockTail.load(std::memory_order_relaxed);
    const s32 next = (tail + 1) % cPendingHintUnlockQueueSize;
    if (next == mPendingHintUnlockHead.load(std::memory_order_acquire)) {
        MpDebugLog::write("Client::enqueueHintUnlock: queue full, dropping remote purchase");
        return false;
    }
    sPendingHintUnlocks[tail] = pending;
    mPendingHintUnlockTail.store(next, std::memory_order_release);
    return true;
}

bool Client::enqueueRemoteAction(const PendingRemoteAction& pending) {
    const s32 tail = mPendingRemoteActionTail.load(std::memory_order_relaxed);
    const s32 next = (tail + 1) % cPendingRemoteActionQueueSize;
    if (next == mPendingRemoteActionHead.load(std::memory_order_acquire)) {
        // 512 deep, so reaching this means the game thread has not drained for
        // a very long time. Say so loudly - and note the loss is recoverable:
        // every kind carried here is either set-shaped (the state vector will
        // re-adopt it) or a coin delta the server still holds in the team log.
        MpDebugLog::write("Client::enqueueRemoteAction: queue full, dropping kind=%d",
            (int)pending.kind);
        return false;
    }
    sPendingRemoteActions[tail] = pending;
    mPendingRemoteActionTail.store(next, std::memory_order_release);
    return true;
}

// Whether a cutscene owns the player right now.
//
// Nothing that can start a demo, request a stage change, or mutate progression
// underneath one may run while this is true. Both queries are needed: the
// second covers the window where a demo has taken the player but is still
// puppetable, which is exactly when a second request slips in.
bool Client::isDemoActiveNow() {
    if (!sInstance || !sInstance->mCurStageScene || !gIsSceneAlive)
        return false;
    if (rs::isActiveDemo(sInstance->mCurStageScene))
        return true;

    // Was rs::isActiveDemoWithPlayer(scene), which DOES NOT LINK - declared in
    // the decomp's DemoUtil.h but not among the symbols available to us, the
    // same trap several al/rs functions in this project have sprung.
    //
    // isActiveDemoPlayerPuppetable is what the comment above actually describes
    // ("a demo has taken the player but is still puppetable"), and it is proven
    // to link - src/main.cpp and StartPositionRando.cpp both already use it. It
    // only has a LiveActor overload, hence going via the player rather than the
    // scene.
    al::PlayerHolder* holder = al::getScenePlayerHolder(sInstance->mCurStageScene);
    al::LiveActor* player = holder ? al::tryGetPlayerActor(holder, 0) : nullptr;
    return player && rs::isActiveDemoPlayerPuppetable(player);
}

void Client::drainRemoteActions() {
    // The whole point of the queue. A remote moon rock asking for a stage
    // change, or a remote unlock firing its overlay, in the middle of a
    // cutscene is the reported soft-lock: GameDataHolder latches one
    // stage-change request per scene and drops the second silently, so the
    // demo's own exit transition can be consumed by a request the player never
    // made. Waiting costs a few frames and nothing else - the entries stay
    // queued in order.
    if (isDemoActiveNow())
        return;

    s32 head = mPendingRemoteActionHead.load(std::memory_order_relaxed);
    const s32 tail = mPendingRemoteActionTail.load(std::memory_order_acquire);
    while (head != tail) {
        applyRemoteAction(sPendingRemoteActions[head]);
        head = (head + 1) % cPendingRemoteActionQueueSize;
        mPendingRemoteActionHead.store(head, std::memory_order_release);

        // A moon rock can start a stage change. Stop the moment one does rather
        // than pouring the rest of the queue into a scene that is on its way
        // out; the next frame picks up where this left off.
        if (isDemoActiveNow())
            return;
    }
}

void Client::applyRemoteAction(const PendingRemoteAction& pending) {
    nn::account::Uid sender;
    sender.m_Storage[0] = pending.senderId[0];
    sender.m_Storage[1] = pending.senderId[1];

    switch (pending.kind) {
    case PendingRemoteAction::AbilityUnlock: {
        const AbilityRando::AbilityEntry* ability = AbilityRando::getAbility(pending.valueA);
        if (!ability)
            return;
        GameDataHolderWriter writer(mHolder);
        if (AbilityRando::isAbilityUnlocked(writer, *ability))
            return;
        AbilityRando::setAbilityUnlocked(writer, *ability, true);
        AbilityOverlay::queueUnlock(ability->displayName ? ability->displayName : ability->key);
        PlayerEventLog::addEvent(sender, PlayerEventLog::PURPLE_BUY,
            ability->displayName ? ability->displayName : ability->key);
        return;
    }
    case PendingRemoteAction::CaptureUnlock: {
        const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(pending.valueA);
        if (!capture)
            return;
        GameDataHolderWriter writer(mHolder);
        if (CaptureRando::isCaptureUnlocked(writer, *capture))
            return;
        CaptureRando::setCaptureUnlocked(writer, *capture, true);
        CaptureOverlay::queueUnlock(capture->displayName ? capture->displayName : capture->hackName);
        PlayerEventLog::addEvent(sender, PlayerEventLog::PURPLE_BUY,
            capture->displayName ? capture->displayName : capture->hackName);
        return;
    }
    case PendingRemoteAction::MoonRock:
        applyMoonRock(pending.valueA);
        return;
    case PendingRemoteAction::CoinCollectUse: {
        GameDataFile* gdf = mHolder.mData ? mHolder->getGameDataFile() : nullptr;
        if (!gdf) {
            MpDebugLog::write("applyRemoteAction: CoinCollectUse GameDataFile null, world=%d",
                pending.valueA);
            return;
        }
        const bool applied = gdf->customUseCoinCollect(pending.valueA, pending.valueB);
        MpDebugLog::write("applyRemoteAction: CoinCollectUse world=%d count=%d applied=%d",
            pending.valueA, pending.valueB, applied ? 1 : 0);
        return;
    }
    case PendingRemoteAction::AddCoin: {
        if (!mHolder.mData || !mHolder->getGameDataFile())
            return;
        GameDataHolderWriter writer(mHolder);
        // Re-invokes the real GameDataFunction::addCoin so the receiving
        // player's own coin counter/UI updates like a normal pickup would.
        // SuppressCoinSyncScope stops addCoinHook from mistaking this re-entry
        // for a fresh local pickup and re-broadcasting it back out.
        SuppressCoinSyncScope coinSyncScope;
        GameDataFunction::addCoin(writer, pending.valueB);
        PlayerEventLog::addEvent(
            sender, PlayerEventLog::COIN, "", PlayerEventLog::ShineExtras(), pending.valueB);
        return;
    }
    case PendingRemoteAction::SubCoin: {
        if (!mHolder.mData || !mHolder->getGameDataFile())
            return;
        GameDataHolderWriter writer(mHolder);
        // Same reasoning as AddCoin, guarded so subCoinHook does not
        // re-broadcast it.
        SuppressCoinSyncScope coinSyncScope;
        GameDataFunction::subCoin(writer, pending.valueB);
        return;
    }
    default:
        return;
    }
}

void Client::drainHintUnlocks() {
    GameDataFile* file = mHolder->getGameDataFile();
    if (!file)
        return;

    s32 head = mPendingHintUnlockHead.load(std::memory_order_relaxed);
    const s32 tail = mPendingHintUnlockTail.load(std::memory_order_acquire);
    while (head != tail) {
        const PendingHintUnlock& pending = sPendingHintUnlocks[head];
        sead::FixedSafeString<MultiplayerProtocol::cEventKeySize> flagKey;
        const bool validFlag = decodeHintFlagKey(pending, &flagKey);
        const bool applied = validFlag && HintSystem::applyRemoteHintUnlock(file, static_cast<HintSystem::SharedUnlock>(pending.unlockKind), pending.shineIndex, flagKey.cstr(), static_cast<Randomizer::HintPriceCategory>(pending.category), pending.amount);
        MpDebugLog::write(
            "Client::drainHintUnlocks: kind=%d shine=%d cat=%d amount=%d applied=%d",
            pending.unlockKind, pending.shineIndex, (int)pending.category, pending.amount,
            applied ? 1 : 0);
        head = (head + 1) % cPendingHintUnlockQueueSize;
        mPendingHintUnlockHead.store(head, std::memory_order_release);
    }
}

u8 Client::getTeamOf(const nn::account::Uid& id) {
    if (!sInstance)
        return 0;
    if (id.m_Storage[0] == sInstance->mUserID.m_Storage[0]
        && id.m_Storage[1] == sInstance->mUserID.m_Storage[1])
        return getTeam();
    for (s32 i = 0; i < sInstance->mRosterCount; i++) {
        if (sInstance->mRoster[i].id[0] == id.m_Storage[0]
            && sInstance->mRoster[i].id[1] == id.m_Storage[1])
            return sInstance->mRoster[i].team;
    }
    return 0;
}

void Client::requestEventCatchUp(const char* reason) {
    // Room-assigned as well as connected. RoomManager only handles a
    // SnapshotRequest for a client that is in a room; outside one it declines
    // the packet, which drops it through to the generic relay and broadcasts it
    // at everybody. Previously unreachable because the only caller that could
    // fire outside a room was gated on a non-zero applied revision, which
    // implies a room - that gate is gone now, so the condition it happened to
    // stand in for is stated directly.
    if (!sInstance || !isSocketActive() || !isRoomAssigned())
        return;

    // One in flight at a time. The reply is a stream of ordinary SharedEvents,
    // so a second request while the first is still arriving would just
    // duplicate work the receive path then has to deduplicate.
    if (sInstance->mEventCatchUpCooldown > 0)
        return;
    sInstance->mEventCatchUpCooldown = 600;

    auto request = new (gHeap) SnapshotRequest;
    request->mUserID = getClientId();
    // The server reads this field as: 0 = send me the room save,
    // cReplayFromSnapshotBase = replay from my team's snapshot base (the join
    // path), cReplayFromStart = replay my team's log from the beginning, and
    // ANY OTHER VALUE = replay every event after that revision.
    //
    // The APPLIED watermark, never the server's head. Asking from the head is
    // how a reconnecting client used to ask for nothing at all.
    //
    // A watermark of zero cannot be sent as zero - that is the "send me the
    // room save" request - so this case used to be excluded from the sweep
    // entirely, on the grounds that the state vector would cover it. It did
    // not: the vector was gated on the SAME condition, so a client sitting at
    // zero in a room whose log had already moved was silent in every
    // direction and stayed that way, since the watermark only advances
    // contiguously and could never reach 1 on its own. cReplayFromStart is how
    // it bootstraps: the replay arrives in order from revision 1, and
    // tryAdvanceAppliedRevision walks the watermark up behind it.
    //
    // That lands on revision 1 because a team log always begins there:
    // TeamState.BaseRevision is whatever the host's SnapshotBegin declared, and
    // RoomSnapshot::upload hard-codes it to zero. Were a log ever to start
    // above zero, the contiguous advance could not take its first step and this
    // sweep would re-request the same replay once a minute - bounded, but worth
    // knowing before that field is ever made variable.
    const u64 applied = DurableSync::getAppliedRevision();
    request->afterRevision = applied != 0 ? applied : MultiplayerProtocol::cReplayFromStart;

    MpDebugLog::write("requestEventCatchUp(%s): asking for events after applied revision %llu%s",
        reason ? reason : "?", (unsigned long long)applied,
        applied != 0 ? "" : " (from the start of the log)");

    if (!queueRoomPacket(request))
        MpDebugLog::write("requestEventCatchUp(%s): send queue refused the request", reason);
}

// Whether re-applying this kind on a client that already holds it is a no-op.
//
// This is the same set-shaped/counted split DurableSync.h draws, read from the
// receiving end: a set-shaped fact is a bit that is either present or not, so
// applying it twice cannot be told from applying it once, while a counted one
// is a running total that a second apply would move again. Only the set-shaped
// half may be applied from an event this client itself proposed.
static bool isIdempotentEventKind(MultiplayerProtocol::SharedEventKind kind) {
    switch (kind) {
    case MultiplayerProtocol::SharedEventKind::Moon:
    case MultiplayerProtocol::SharedEventKind::Checkpoint:
    case MultiplayerProtocol::SharedEventKind::MoonRock:
    case MultiplayerProtocol::SharedEventKind::MoonRockKey:
    case MultiplayerProtocol::SharedEventKind::PaintingUnlock:
    case MultiplayerProtocol::SharedEventKind::AbilityUnlock:
    case MultiplayerProtocol::SharedEventKind::CaptureUnlock:
    case MultiplayerProtocol::SharedEventKind::KingdomVisit:
    case MultiplayerProtocol::SharedEventKind::KingdomComplete:
    case MultiplayerProtocol::SharedEventKind::KingdomBranchOrder:
    case MultiplayerProtocol::SharedEventKind::KingdomForcedPeace:
    case MultiplayerProtocol::SharedEventKind::ScenarioAdvance:
        return true;
    // PurpleCoin, PurpleCoinSpend, YellowCoinAdd, YellowCoinSpend, HintUnlock.
    default:
        return false;
    }
}

void Client::updateSharedEvent(SharedEvent* packet) {
    if (!packet || !sInstance)
        return;

    const bool isOwn = packet->mUserID.m_Storage[0] == mUserID.m_Storage[0]
        && packet->mUserID.m_Storage[1] == mUserID.m_Storage[1];

    // THE ACKNOWLEDGEMENT. RoomManager.AcceptEvent fans the verdict out to the
    // whole team including the proposer, carrying back the eventId the proposer
    // chose - so the ack this client needs has always been arriving here and
    // was always being dropped one line below at the isOwn early return.
    // Consuming it is what retires an outbox entry.
    //
    // Gated on isOwn because eventIds are per-client nonces: another player's
    // id means nothing in our ring and a collision, however unlikely, would
    // retire the wrong fact.
    if (isOwn)
        DurableSync::onEcho(packet->eventId, packet->status);

    // The head is what the server says exists; the watermark is what we have
    // actually applied. Keeping them apart is the fix for the reconnect path:
    // updateRoomStatus used to slam the head straight into the one number that
    // catch-up asks from, so a client that had applied nothing believed it had
    // seen everything and never asked for any of it.
    if (packet->revision > mRoomHeadRevision)
        mRoomHeadRevision = packet->revision;

    if (packet->status == MultiplayerProtocol::SharedEventStatus::Accepted) {
        // Revisions are contiguous within a team's log, so a jump of more than
        // one is proof that events were lost in transit. Ask for them before
        // applying this one: the catch-up stream and this packet are both
        // ordinary events and both go through the deduplicating receive paths,
        // so ordering and overlap are harmless.
        // No longer excused when applied == 0. That exemption existed only
        // because catch-up had no way to ask from zero, so noticing the gap
        // would have been useless; cReplayFromStart gives it one, and a client
        // at zero in a room with history is precisely the one with the largest
        // gap to close.
        const u64 applied = DurableSync::getAppliedRevision();
        if (packet->revision > applied + 1) {
            MpDebugLog::write("updateSharedEvent: revision gap - applied %llu, received %llu",
                (unsigned long long)applied, (unsigned long long)packet->revision);
            requestEventCatchUp("gap");
        }
        // Advances only when this is exactly the next revision. An event that
        // arrives past a hole is still applied below - every apply here is
        // idempotent - but the watermark must stay behind the hole so the
        // sweep keeps asking for what is missing.
        DurableSync::tryAdvanceAppliedRevision(packet->revision);
    }

    // The Traveling Peach publish is acknowledged here too. Duplicate counts:
    // it means the team already has the fact, which is exactly as good as
    // having just published it and must stop the retry either way.
    if (isOwn && packet->kind == MultiplayerProtocol::SharedEventKind::TravelingPeachStart
        && (packet->status == MultiplayerProtocol::SharedEventStatus::Accepted
            || packet->status == MultiplayerProtocol::SharedEventStatus::Duplicate)) {
        mTravelingPeachPublished.store(true, std::memory_order_release);
    }

    if (packet->status != MultiplayerProtocol::SharedEventStatus::Accepted)
        return;

    // Our OWN event, coming back. Two very different things arrive here and
    // this used to discard both:
    //
    //   * the live echo of something we just did, already applied locally, and
    //   * a reconciliation REPLAY. RoomManager.ReconcileState re-sends the
    //     log's ORIGINAL entry, carrying the original proposer's id - so a fact
    //     WE once proposed but no longer hold comes back looking exactly like
    //     our own echo. Dropping it meant the server, still seeing the gap in
    //     our next state vector, re-sent it every single sweep and we threw it
    //     away every single time: the healer running forever while nothing
    //     changed, and a moon the other player had that we never got back.
    //
    // Own events are therefore allowed through, but only for the set-shaped
    // kinds, whose applies are idempotent by construction (DurableSync.h) and
    // all of which re-check the save before touching it. The counted kinds -
    // purple and yellow coins, hint purchases - must never be re-applied from
    // our own echo, because "apply it again" is indistinguishable from "earn
    // it again". Nothing here re-broadcasts, so there is no echo loop:
    // updateShineInfo's grant runs under RemoteShineGrantScope, which is what
    // suppresses mpOnSetGotShine's send.
    if (isOwn && !isIdempotentEventKind(packet->kind))
        return;

    // Our own echo still gets APPLIED - that is what converges state and what
    // stops the server re-sending it every sweep - but it must not write a
    // second event-log row. Without this, collecting a moon showed up twice in
    // the sidebar: once when you took it, and again when the server handed it
    // back. Only the log is silenced; every apply below runs exactly as it did.
    PlayerEventLog::SuppressScope ownEchoLogGuard(isOwn);

    switch (packet->kind) {
    case MultiplayerProtocol::SharedEventKind::Moon: {
        ShineCollect translated;
        translated.mUserID = packet->mUserID;
        translated.shineId = std::atoi(packet->key.cstr());
        translated.abilityId = packet->value;
        translated.extraAbilityIds = packet->auxiliary;
        updateShineInfo(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::PurpleCoin: {
        CoinCollectCollect translated;
        translated.mUserID = packet->mUserID;
        translated.placeID = packet->key;
        translated.worldID = packet->worldId;
        translated.stage = packet->auxiliary;
        updateCoinCollects(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::PurpleCoinSpend: {
        CoinCollectUse translated;
        translated.mUserID = packet->mUserID;
        translated.worldID = packet->worldId;
        translated.count = packet->value;
        updateCoinCollectUse(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::Checkpoint: {
        CheckpointGet translated;
        translated.mUserID = packet->mUserID;
        translated.objId = packet->key;
        updateCheckpoints(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::MoonRock: {
        if (packet->worldId < 0 || packet->worldId >= SNumMoonRocks)
            break;
        MoonRockHit translated;
        translated.mUserID = packet->mUserID;
        translated.worldId = packet->worldId;
        updateMoonRocks(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::AbilityUnlock: {
        AbilityUnlock translated;
        translated.mUserID = packet->mUserID;
        translated.abilityId = packet->value;
        updateAbilityUnlock(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::CaptureUnlock: {
        CaptureUnlock translated;
        translated.mUserID = packet->mUserID;
        translated.captureIndex = packet->value;
        updateCaptureUnlock(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::KingdomVisit:
        if (packet->worldId >= 0 && packet->worldId < 32) {
            const u32 bit = 1u << packet->worldId;
            mPublishedKingdoms.fetchOr(bit);
            mPendingKingdoms.fetchOr(bit);
        }
        break;
    case MultiplayerProtocol::SharedEventKind::HintUnlock: {
        PendingHintUnlock pending = {};
        pending.unlockKind = packet->worldId;
        pending.shineIndex = packet->value;
        if (!parseHintUnlockAuxiliary(packet->auxiliary.cstr(), &pending.category,
                &pending.amount)) {
            MpDebugLog::write("Client::updateSharedEvent: malformed hint event auxiliary=%s",
                packet->auxiliary.cstr());
            break;
        }
        if (!encodeHintFlagIndex(pending.unlockKind, pending.category, packet->key.cstr(),
                &pending.flagIndexPlusOne)) {
            MpDebugLog::write("Client::updateSharedEvent: malformed hint event key=%s",
                packet->key.cstr());
            break;
        }
        enqueueHintUnlock(pending);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::YellowCoinAdd: {
        if (packet->value <= 0)
            break;
        AddCoin translated;
        translated.mUserID = packet->mUserID;
        translated.count = packet->value;
        updateAddCoin(&translated);
        break;
    }
    case MultiplayerProtocol::SharedEventKind::YellowCoinSpend: {
        if (packet->value <= 0)
            break;
        SubCoin translated;
        translated.mUserID = packet->mUserID;
        translated.count = packet->value;
        updateSubCoin(&translated);
        break;
    }
    // ===== protocol 6 =====
    //
    // All four write into the persisted mask rather than a session-only RAM
    // overlay, which is what makes them survive a restart and what lets the
    // state vector republish them. The masks are read back through
    // hasRemoteMoonRockKeyUnlock/hasRemotePaintingUnlock, so the game-side
    // queries did not have to change shape - only their storage did.
    case MultiplayerProtocol::SharedEventKind::MoonRockKey:
        if (packet->worldId >= 0 && packet->worldId < SNumMoonRocks) {
            DurableSync::setMoonRockKey(packet->worldId);
            MpDebugLog::write("updateSharedEvent: moon rock key unlocked for world %d",
                packet->worldId);
        }
        break;
    case MultiplayerProtocol::SharedEventKind::PaintingUnlock:
        if (packet->value >= 0 && packet->value < SNumPaintingLinks) {
            DurableSync::setPainting(packet->value);
            MpDebugLog::write("updateSharedEvent: painting route %d unlocked", packet->value);
        }
        break;
    case MultiplayerProtocol::SharedEventKind::ScenarioAdvance:
        if (packet->worldId >= 0 && packet->worldId < SNumMoonRocks && packet->value > 0)
            queueScenarioAdvance(packet->worldId, packet->value);
        break;
    // Kingdom progression. All three park in mPendingKingdom* and are merged on
    // the game thread by drainKingdomProgress - they mutate GameDataFile, and
    // this is the read thread.
    case MultiplayerProtocol::SharedEventKind::KingdomComplete:
        if (packet->value > 0 && packet->value <= KingdomProgress::cKingdomCount
            && packet->value > mPendingCompletedCount)
            mPendingCompletedCount = (u8)packet->value;
        break;
    case MultiplayerProtocol::SharedEventKind::KingdomBranchOrder:
        if (packet->worldId >= 0 && packet->worldId < KingdomProgress::cBranchCount
            && packet->value >= 0 && packet->value < KingdomProgress::cKingdomCount
            && mPendingBranchChoice[packet->worldId] < 0)
            mPendingBranchChoice[packet->worldId] = (s8)packet->value;
        break;
    case MultiplayerProtocol::SharedEventKind::KingdomForcedPeace:
        if (packet->worldId >= 0 && packet->worldId < 32)
            mPendingForcedPeace.fetchOr(1u << packet->worldId);
        break;

    case MultiplayerProtocol::SharedEventKind::TravelingPeachStart:
        mTravelingPeachPublished.store(true, std::memory_order_release);
        mPendingTravelingPeachStart.store(true, std::memory_order_release);
        PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::PEACH_QUEST, "");
        break;
    }
}

void Client::updateCoinReserveResult(CoinReserveResult* packet) {
    if (!packet || !sInstance)
        return;
    mCoinReserveAvailable.store(packet->available);
    mCoinReserveDecision.store(packet->accepted ? 1 : -1);
    mCoinReserveResultId.store(packet->requestId);
}

void Client::updateDeathLink(DeathLink* packet) {
    if (!packet || !sInstance || !isDeathLinkEnabled() || packet->eventId == 0)
        return;
    if (mPendingDeathLinkId.load() == 0)
        mPendingDeathLinkId.store(packet->eventId);
}

/**
 * @brief Sends the local seed's multiplayer settings as the room's flags.
 * Death link and cross-team sync are both seed-owned (stored in rando.bin, see
 * RandoSaveFile's MultiplayerSettingsRecord), so what gets published is
 * whatever the host's seed carries. The server accepts this only from the
 * room's setup owner.
 */
void Client::sendRoomSettings() {
    if (!sInstance) {
        hk::diag::logLine("Static Instance is Null!");
        return;
    }

    u8 flags = 0;
    if (Randomizer::isDeathLinkEnabled())
        flags |= static_cast<u8>(MultiplayerProtocol::FlagDeathLink);
    if (Randomizer::isSyncOtherTeamsEnabled())
        flags |= static_cast<u8>(MultiplayerProtocol::FlagSyncOtherTeams);

    auto settings = new (gHeap) RoomSettings;
    settings->mUserID = sInstance->mUserID;
    settings->flags = flags;

    // Read nothing off `settings` past this point - the send thread owns it and
    // deletes it once written.
    const bool queued = sInstance->mSocket->queuePacket(settings);
    MpDebugLog::write("Client::sendRoomSettings: flags=0x%02x queued=%d", (int)flags, queued ? 1 : 0);
}

void Client::onPlayerDeathDetected() {
    if (!sInstance)
        return;
    if (sInstance->mRemoteDeathArmed) {
        sInstance->mRemoteDeathArmed = false;
        sInstance->mRemoteDeathArmFrames = 0;
        return;
    }
    if (!isSocketActive() || !isRoomReady() || !isDeathLinkEnabled())
        return;
    auto packet = new (gHeap) DeathLink;
    packet->mUserID = sInstance->mUserID;
    packet->eventId = sInstance->mUserID.m_Storage[0]
        ^ (sInstance->mNextEventId++ * 0xD6E8FEB86659FD93ULL);
    packet->generation = sInstance->mNextEventId;
    sInstance->mSocket->queuePacket(packet);
}

void Client::updateAddCoin(AddCoin* packet) {
    if (!packet || !sInstance)
        return;

    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::AddCoin;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueB = packet->count;
    sInstance->enqueueRemoteAction(pending);
}

void Client::updateSubCoin(SubCoin* packet) {
    if (!packet || !sInstance)
        return;

    PendingRemoteAction pending = {};
    pending.kind = PendingRemoteAction::SubCoin;
    pending.senderId[0] = packet->mUserID.m_Storage[0];
    pending.senderId[1] = packet->mUserID.m_Storage[1];
    pending.valueB = packet->count;
    sInstance->enqueueRemoteAction(pending);
}

void Client::updatePlayerDeath(PlayerDeath* packet) {
    if (!packet)
        return;

    PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::DEATH, "");
}

// The bare MOONROCKKEYUNLOCK/PAINTINGUNLOCK packets are no longer sent by this
// build - both facts travel as durable shared events now - but the server still
// understands them, so these handlers stay and route into the same persisted
// masks. Anything that does send one behaves identically to the event path.
void Client::updateMoonRockKeyUnlock(MoonRockKeyUnlock* packet) {
    if (!packet || !sInstance)
        return;

    if (packet->kingdom < 0 || packet->kingdom >= SNumMoonRocks)
        return;

    DurableSync::setMoonRockKey(packet->kingdom);
}

void Client::updatePaintingUnlock(PaintingUnlock* packet) {
    if (!packet || !sInstance)
        return;

    if (packet->paintingIndex < 0 || packet->paintingIndex >= SNumPaintingLinks)
        return;

    DurableSync::setPainting(packet->paintingIndex);
}

// These used to read session-only RAM arrays that were never saved, never
// replayed on join and gone on every restart - which is exactly why a late
// joiner arrived to a Moon Rock they could not open and a painting route they
// could not use, and why the same player lost both again after a reboot. The
// answer now comes out of the persisted mask, so it survives everything the
// arrays did not.
bool Client::hasRemoteMoonRockKeyUnlock(s32 kingdom) {
    return DurableSync::hasMoonRockKey(kingdom);
}

bool Client::hasRemotePaintingUnlock(s32 paintingIndex) {
    return DurableSync::hasPainting(paintingIndex);
}

// Scenario advancement arrives on the read thread but mutates GameDataFile, so
// it waits here for the game thread exactly like coin collects and checkpoints
// already do. Applied as max(), so replay order does not matter and a duplicate
// costs nothing.
void Client::queueScenarioAdvance(s32 worldId, s32 scenarioNo) {
    if (!sInstance || worldId < 0 || worldId >= SNumMoonRocks)
        return;
    if (scenarioNo > sInstance->mPendingScenarioNo[worldId])
        sInstance->mPendingScenarioNo[worldId] = (u8)(scenarioNo < 255 ? scenarioNo : 255);
}

// Game thread. Merges whatever kingdom progression the team has reported into
// this save.
//
// One monotone merge rather than a per-frame retry loop. The version this
// replaced re-called KingdomProgress::commitBranchChoice every second hoping
// this save would eventually reach the frontier that choice belonged to - which
// could not work for a player who was BEHIND the team, because the frontier
// they needed to reach was itself gated on the completion this had no way to
// deliver. applySyncState writes the ordering positionally instead, so a player
// adopts the team's branch choice regardless of where they are.
void Client::drainKingdomProgress() {
    const u32 peace = mPendingForcedPeace.exchange(0);
    bool haveProgress = mPendingCompletedCount > 0;
    for (s32 branch = 0; branch < KingdomProgress::cBranchCount && !haveProgress; branch++)
        haveProgress = mPendingBranchChoice[branch] >= 0;

    if (peace != 0) {
        for (s32 world = 0; world < 32; world++) {
            if ((peace & (1u << world)) != 0)
                Randomizer::setKingdomForcedPeace(world, true);
        }
        // Republishing our own adoption back at the team is pointless - it is
        // where this came from.
        mPublishedForcedPeace |= peace;
        MpDebugLog::write("drainKingdomProgress: adopted forced-peace mask %08x", peace);
    }

    if (!haveProgress)
        return;

    GameDataFile* gdf = mHolder.mData ? mHolder->getGameDataFile() : nullptr;
    if (!gdf)
        return;

    KingdomProgress::SyncState remote;
    remote.completedCount = mPendingCompletedCount;
    for (s32 branch = 0; branch < KingdomProgress::cBranchCount; branch++)
        remote.branchChoice[branch] = mPendingBranchChoice[branch];

    mPendingCompletedCount = 0;
    for (s32 branch = 0; branch < KingdomProgress::cBranchCount; branch++)
        mPendingBranchChoice[branch] = -1;

    if (!KingdomProgress::applySyncState(gdf, remote))
        return;

    // Adopt into the published baseline so the outbound diff does not propose
    // back what the team just told us.
    KingdomProgress::SyncState merged;
    if (KingdomProgress::captureSyncState(gdf, &merged)) {
        if (merged.completedCount > mPublishedCompletedCount)
            mPublishedCompletedCount = merged.completedCount;
        for (s32 branch = 0; branch < KingdomProgress::cBranchCount; branch++) {
            if (mPublishedBranchChoice[branch] < 0)
                mPublishedBranchChoice[branch] = merged.branchChoice[branch];
        }
    }
}

// Publishes what this save actually contains. See StateVector in
// RoomProtocol.h for why this is the only mechanism that can heal progress lost
// before it ever reached the server.
//
// Deliberately cheap: the moon set is read straight off the hint list's isGet
// flags (1024 field reads, no lookups and no remap), and everything else is
// either a bitmask already held in memory or a 14-entry array.
bool Client::sendStateVector() {
    if (!sInstance || !isSocketActive() || !isRoomReady() || !DurableSync::isReady())
        return false;

    // Only once the room save is the one actually loaded and running. A vector
    // published from a half-initialised save would describe nothing, and while
    // that direction is harmless (the server would just replay everything back)
    // it is pure noise on every join.
    if (!(sInstance->mCurStageScene && gIsSceneAlive))
        return false;

    GameDataFile* gdf = sInstance->mHolder.mData ? sInstance->mHolder->getGameDataFile() : nullptr;
    if (!gdf)
        return false;

    auto packet = new (gHeap) StateVector;
    packet->mUserID = sInstance->mUserID;

    // Whether the server may ADOPT from this vector, decided per publication
    // rather than by withholding the whole packet.
    //
    // This used to be an early return: a save that had applied nothing from
    // this team's log published no vector at all. The intent was right - a save
    // built from one team's progress must not be injected wholesale into
    // another team's log - but the remedy also switched off the direction that
    // is always safe, the server replaying the team's own events back to us.
    // Worse, the same condition gated the catch-up sweep, so a client that
    // reached it had no recovery mechanism left at all and no way back: the
    // watermark only advances contiguously, so it could never climb off zero on
    // its own. That is the state a permanently one-sided moon lives in.
    //
    // Both halves are now expressed separately. Direction 1 always runs; the
    // NoAdopt flag turns off direction 2 for the two situations that genuinely
    // cannot vouch for the save - a team change with no replay behind it
    // (DurableSync::isSaveProvenForTeam) and a save that has applied nothing
    // from a log that already has history.
    const bool unprovenAgainstThisLog
        = DurableSync::getAppliedRevision() == 0 && getRoomHeadRevision() != 0;
    if (unprovenAgainstThisLog || !DurableSync::isSaveProvenForTeam())
        packet->flags |= MultiplayerProtocol::cStateVectorFlagNoAdopt;

    auto hintList = gdf->getHintList();
    s32 moonCount = 0;
    for (s32 i = 0; i < 0x400; i++) {
        const GameDataFile::HintInfo& info = hintList[i];
        // isGet on the slot IS the got-flag, so reading it directly is both
        // cheaper than and equivalent to isGotShineRaw - and it sidesteps the
        // credit remap entirely rather than having to suppress it.
        if (!info.isGet || info.uniqueId < 0)
            continue;
        packet->setMoon(info.uniqueId);
        moonCount++;
    }

    // Achievement/Toadette moons are not hint-list entries; they live in the
    // 2000-2060 range the shine sync already reserves for them.
    for (s32 i = 0; i < (s32)hk::util::arraySize(toadetteMoons); i++) {
        if (rs::checkGetAchievement(sInstance->mCurStageScene, toadetteMoons[i])) {
            packet->setMoon(2000 + i);
            moonCount++;
        }
    }

    packet->kingdomVisited = Randomizer::getVisitedKingdoms();
    packet->moonRocksHit = DurableSync::getMoonRockMask();
    packet->moonRockKeys = DurableSync::getMoonRockKeyMask();

    // Kingdom progression and forced peace are read straight out of the save
    // that owns them - the progression record and the rando save meta - rather
    // than from a shadow mask in DurableSync. One source of truth, and it is
    // the one the game itself reads.
    packet->forcedPeace = Randomizer::getForcedPeaceKingdoms();
    KingdomProgress::SyncState progress;
    if (KingdomProgress::captureSyncState(gdf, &progress)) {
        packet->kingdomCompletedCount = progress.completedCount;
        for (s32 branch = 0; branch < KingdomProgress::cBranchCount
             && branch < MultiplayerProtocol::cStateVectorBranches;
             branch++)
            packet->branchChoice[branch] = progress.branchChoice[branch];
    }

    // Persisted mask OR what this save independently derives. The two can
    // legitimately differ: a painting unlocked by our own Cloud moons was never
    // an event at all, and publishing it is how the rest of the team gets it.
    packet->paintingUnlocks = DurableSync::getPaintingMask();
    for (s32 i = 0; i < SNumPaintingLinks; i++) {
        if (PaintingRando::isPaintingUnlocked(gdf, i))
            packet->paintingUnlocks |= (1u << i);
    }

    auto scenarios = gdf->getScenarioNumArr();
    for (s32 world = 0; world < MultiplayerProtocol::cStateVectorKingdoms; world++) {
        const s32 scenario = scenarios[world];
        packet->scenarioNo[world] = (u8)(scenario > 0 ? (scenario < 255 ? scenario : 255) : 0);
    }

    MpDebugLog::write(
        "sendStateVector: moons=%d visited=%x rocks=%x keys=%x paintings=%x peace=%x kpDone=%d "
        "flags=%x",
        moonCount, packet->kingdomVisited, packet->moonRocksHit, packet->moonRockKeys,
        packet->paintingUnlocks, packet->forcedPeace, (int)packet->kingdomCompletedCount,
        (int)packet->flags);

    queueRoomPacket(packet);
    return true;
}

// Game thread. Raises this save's scenario number for any kingdom a team-mate
// has taken further than we have.
void Client::drainScenarioAdvances() {
    GameDataFile* gdf = mHolder.mData ? mHolder->getGameDataFile() : nullptr;
    if (!gdf)
        return;

    for (s32 world = 0; world < SNumMoonRocks; world++) {
        const s32 pending = mPendingScenarioNo[world];
        if (pending <= 0)
            continue;
        mPendingScenarioNo[world] = 0;

        s32& scenarioNum = gdf->getScenarioNumArr()[world];
        if (scenarioNum >= pending)
            continue;

        MpDebugLog::write("drainScenarioAdvances: world=%d %d -> %d", world, scenarioNum, pending);
        scenarioNum = pending;
        // Record it as published too. Without this, syncScenarioNumbers' diff
        // would see the raise we just applied from a team-mate and propose it
        // straight back - harmless (the server dedups it) but one pointless
        // round trip per advance.
        if (pending > mPublishedScenarioNo[world])
            mPublishedScenarioNo[world] = (u8)pending;
    }
}

void Client::saveMoonRocks(al::ByamlWriter* writer) {
    writer->pushHash("ScenarioSyncMoonRockData");

    for (s32 i = 0; i < SNumMoonRocks; i++) {
        writer->addBool(mPendingMoonRocks[i]);
    }

    writer->pop();
}

void Client::readMoonRocks(const al::ByamlIter& save) {
    al::ByamlIter moonRockIter;
    if (al::tryGetByamlIterByKey(&moonRockIter, save, "ScenarioSyncMoonRockData")) {
        for (s32 i = 0; i < SNumMoonRocks; i++) {
            mPendingMoonRocks[i] = false;
            moonRockIter.tryGetBoolByIndex(&mPendingMoonRocks[i], i);
        }
    }
}

/**
 * @brief Processes all pending collected shines on the game thread.
 *        Must only be called when mCurStageScene is valid.
 */
void Client::updateShines() {
    if (!sInstance) {
        hk::diag::logLine("Client Null!");
        return;
    }

    if (!(sInstance->mCurStageScene && gIsSceneAlive)) {
        hk::diag::logLine("updateShines: scene not ready, skipping");
        return;
    }

    // Not while a cutscene owns the player. applyRemoteShine runs the real
    // setGotShine path, which fires unlock feedback and can put an overlay or a
    // scenario change on screen - landing that inside somebody else's demo is
    // the same collision PendingRemoteAction exists to avoid. The table is not
    // cleared on this path, so nothing is lost: the moons apply on the first
    // frame after the demo ends.
    if (isDemoActiveNow())
        return;

    // skip shine sync if player is in cap kingdom scenario zero (very start of the game)
    if (sInstance->mStageName == "CapWorldHomeStage" && (sInstance->mScenario == 0 || sInstance->mScenario == 1)) {
        return;
    }

    GameDataHolderAccessor accessor = sInstance->mHolder;

    // DRAIN FIRST, APPLY SECOND, and never wipe the table wholesale.
    //
    // This used to walk the live table in place and finish with
    // resetCollectedShines(), which clears all 1024 slots and zeroes the count.
    // updateShineInfo() writes that same table from the READ thread, so a moon
    // that landed in a slot this walk had already passed was erased by that
    // reset without ever being applied - the sender had it, the receiver never
    // got it, and nothing said so. The wholesale `= 0` racing the read thread's
    // collectedShineCount++ could also strand a live entry behind a zero count,
    // which isNeedUpdateShines() reads as "nothing to do".
    //
    // Taking the entries OUT under the lock and applying them from a private
    // copy removes both. Anything the read thread stores while the applies run
    // lands in a slot this pass has already emptied and is picked up next
    // frame, because nothing clears a slot any more except the drain that
    // consumed it.
    //
    // Module storage rather than a 4 KB stack array, and safe as a single
    // static because only the game thread ever reaches here.
    static int sDraining[1024];
    s32 drained = 0;
    {
        ShineTableLock lock;
        if (!lock.isAcquired()) {
            // Leave the table alone and try again next frame. Unlike the read
            // thread's store, there is nothing to lose by waiting - these moons
            // stay recorded and this runs every frame.
            MpDebugLog::write("updateShines: shine table lock timed out, retrying next frame");
            return;
        }
        // The whole table, not collectedShineCount entries of it: the live
        // entries are not a dense prefix, so bounding on the count would skip
        // everything past it.
        for (s32 i = 0; i < static_cast<s32>(sInstance->curCollectedShines.size()); i++) {
            const int shineID = sInstance->curCollectedShines[i];
            if (shineID < 0)
                continue;
            sInstance->curCollectedShines[i] = -1;
            if (drained < static_cast<s32>(hk::util::arraySize(sDraining)))
                sDraining[drained++] = shineID;
        }
        sInstance->collectedShineCount = 0;
    }

    if (drained == 0)
        return;

    for (s32 i = 0; i < drained; i++) {
        int shineID = sDraining[i];

        for (const storyShine& shine : scenarioSyncList) {
            if (shine.uid == shineID) {
                s32& scenarioNum = Client::instance()->getHolder()->getGameDataFile()->getScenarioNumArr()[shine.worldId];

                if (scenarioNum < shine.scenario) {
                    bool isSubScenario = scenarioNum != accessor->getGameDataFile()->getScenarioNo();

                    scenarioNum = shine.scenario;

                    if (!isSubScenario && al::isEqualString(shine.homeStage, GameDataFunction::getCurrentStageName(accessor))) {
                        ChangeStageInfo info(accessor, "start",
                            GameDataFunction::getCurrentStageName(accessor));
                        info.mWipeType = "FadeWhite";
                        // avoid setting mIsStageChanging because we want to allow checkpoint warps to
                        // override the scenario sync warp. this is why we aren't using
                        // GameDataFunction::tryChangeNextStage()
                        // if (!accessor->mIsStageChanging) {
                        //     accessor->getGameDataFile()->changeNextStage(&info, 0);
                        //     accessor->resetLocationName();
                        // }
                    }
                }

                break;
            }
        }

        if (shineID >= 2000 && shineID <= 2060) {
            if (!rs::checkGetAchievement(sInstance->mCurStageScene, toadetteMoons[shineID - 2000])) {
                sInstance->getHolder()->getGameDataFile()->getAchievement(toadetteMoons[shineID - 2000]);
            }
            continue;
        }

        GameDataFile::HintInfo* shineInfo = CustomGameDataFunction::getHintInfoByUniqueID(accessor, shineID);

        if (shineInfo) {
            // shineInfo->stageName/objId is already the CREDITED/destination
            // slot (packet->shineId is the credited moon - see the NOTE in
            // Multiplayer/hooks/syncHooks.cpp's mpOnSetGotShine), not a raw
            // physical location. GameDataFunction::isGotShine(accessor,
            // stageName, objId) flows into MoonHooks::isGotShineHook, which
            // re-runs MoonRando::getCreditMoon() on whatever it's given - a
            // SECOND remap on an already-credited item. Where a credited
            // moon's coordinates happen to also key a different real
            // location (documented in syncHooks.cpp as "observed with Sand
            // Kingdom moons"), that second remap reads a DIFFERENT slot's
            // got-flag, so both checks below could each wrongly report
            // "not collected" even after the first one just set it -
            // double-crediting this player's own save for one packet.
            // MoonHooks::isGotShineRaw reads the flag for the exact
            // stage/objId given, with the remap disabled - the same fix
            // already applied in mpOnSetGotShine for this identical
            // ambiguity, just missing here on the receive side.
            ShineInfo destInfo;
            destInfo.mStageName.copy(shineInfo->stageName);
            destInfo.mObjId.copy(shineInfo->objId);
            destInfo.setType(shineInfo->isGrand ? ShineInfo::Type_GrandShine : ShineInfo::Type_Shine);
            destInfo.mIsShop = shineInfo->isShop;

            if (!MoonHooks::isGotShineRaw(accessor->getGameDataFile(), &destInfo)) {
                Shine* stageShine = findStageShine(shineID);
                MpDebugLog::write(
                    "updateShines: shineID=%d dest=%s/%s liveStageShine=%d",
                    shineID, destInfo.mStageName.cstr(), destInfo.mObjId.cstr(), stageShine ? 1 : 0);

                if (stageShine) {
                    MoonHooks::RemoteShineGrantScope remoteGrantScope;
                    // Retire the actor silently. This used to force the actor
                    // alive and call Shine::getDirect() + onSwitchGet(), i.e.
                    // the *local player pickup* pipeline: getDirect resets the
                    // shine onto the player and requests the moon-get demo, so
                    // a peer buying a moon in a shop (or a peer's backlog being
                    // replayed as this client joins) took this player's camera
                    // away for a cutscene they had no part in. Reviving a dead
                    // actor first made it worse - an untouched Scarecrow /
                    // P-Switch / NPC moon was dragged through its appear demo
                    // too. Nothing a peer does should start a cutscene here.
                    MoonHooks::collectStageShineSilently(stageShine);
                }

                // The silent retire above deliberately does NOT credit the
                // moon (unlike the old getDirect() path, which ran the native
                // pickup pipeline and called GameDataFunction::setGotShine
                // itself). The re-check below therefore always falls through
                // to applyRemoteShine now, which is the same path the far more
                // common "no live stage actor" case already took - one credit,
                // one HUD bump, no double count.
                //
                // applyRemoteShine rather than the bare
                // GameDataFile::setGotShine(HintInfo*) flag write:
                // The bare write only sets the got-flag; it skips
                // GameDataFunction::setGotShine entirely, so setGotShineHook
                // never runs applyUnlockFeedback for this player, and any
                // capture/ability that moon carries silently never grants
                // (confirmed via crash-free but reproducible reports of
                // captures/abilities not applying for the non-collecting
                // player). applyRemoteShine goes through the real hooked
                // path with the credit remap bypassed, matching
                // grantMoonDirect's pattern for the same "already a credited
                // destination" situation.
                if (!MoonHooks::isGotShineRaw(accessor->getGameDataFile(), &destInfo)) {
                    GameDataHolderWriter writer(accessor);
                    MpDebugLog::write("updateShines: shineID=%d -> applyRemoteShine (no live actor took it)", shineID);
                    MoonHooks::applyRemoteShine(writer, &destInfo);
                } else if (stageShine) {
                    MpDebugLog::write(
                        "updateShines: shineID=%d -> already credited while retiring the live stage actor",
                        shineID);
                }
            } else {
                MpDebugLog::write("updateShines: shineID=%d already credited locally, skipped", shineID);
            }
        }
    }

    // No resetCollectedShines() here. The drain at the top already took these
    // entries out; wiping the table again would throw away anything the read
    // thread has stored since, which is precisely the moon-losing race this
    // function used to have.
    sInstance->mCurStageScene->stageSceneLayout->startShineCountAnim(false);
    sInstance->mCurStageScene->stageSceneLayout->updateCounterParts();
}

/**
 * @brief Core logic for applying a received coin collect to game state and killing the actor.
 *        Called from updateCoinCollects() when the scene is ready, or from update() when
 *        draining the pending queue.
 *
 * @param placeID placement ID string of the coin collect
 * @param worldID world ID
 * @param stage   stage name string
 */
void Client::applyOneCoinCollect(const char* placeID, int worldID, const char* stage) {
    al::PlacementId pid(placeID, nullptr, nullptr);
    GameDataFile* gdf = sInstance->getHolder()->getGameDataFile();
    if (!gdf) {
        hk::diag::logLine("applyOneCoinCollect: GameDataFile null, dropping");
        return;
    }

    gdf->customAddCoinCollect(&pid, worldID, stage);

    if (gdf->isGotCoinCollect(&pid)) {
        for (s32 i = 0; i < sInstance->mCoinCollectArray.size(); i++) {
            const al::PlacementId* actorPlacement = sInstance->mCoinCollectArray[i]
                ? CoinRando::translatePurplePlacement(
                      sInstance->mCoinCollectArray[i]->mPlacementId)
                : nullptr;
            if (actorPlacement && actorPlacement->isEqual(pid)) {
                sInstance->mCoinCollectArray[i]->makeActorDead();
                return;
            }
        }
        for (s32 i = 0; i < sInstance->mCoinCollect2DArray.size(); i++) {
            const al::PlacementId* actorPlacement = sInstance->mCoinCollect2DArray[i]
                ? CoinRando::translatePurplePlacement(
                      sInstance->mCoinCollect2DArray[i]->mPlacementId)
                : nullptr;
            if (actorPlacement && actorPlacement->isEqual(pid)) {
                sInstance->mCoinCollect2DArray[i]->makeActorDead();
                return;
            }
        }
    }
}

/**
 * @brief Receives a coin collect packet from the read thread.
 *        If the scene is ready, applies immediately. Otherwise queues it for update().
 *
 * @param packet
 */
void Client::updateCoinCollects(CoinCollectCollect* packet) {
    if (!packet)
        return;

    if (!sInstance) {
        return;
    }

    // Never log a purple coin back to the player who collected it - the same
    // own-echo rule updateShineInfo follows, and for the same reason: the local
    // pickup is already logged by mpOnCoinCollect (syncHooks.cpp:217) when it
    // happens, so anything arriving here with OUR uid is the server's echo of
    // what we just sent, or a replay of it during a reconcile.
    //
    // This function has TWO callers - the shared-event apply, which
    // updateSharedEvent's own-echo scope covers, and the direct
    // PacketType::COINCOLLECTCOLLECT case, which it does not. Guarding here
    // covers both. Only the duplicate log line is dropped; the queueing and
    // application below run exactly as before.
    if (packet->mUserID != Client::getClientId()) {
        PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::PURPLE,
            worldNames[packet->worldID]);
    }

    if (!(sInstance->mCurStageScene && gIsSceneAlive)) {
        // Scene not ready — queue for later processing in update()
        if (sInstance->mPendingCoinCollectCount < sMaxPendingCoinCollects) {
            PendingCoinCollect& pending = sPendingCoinCollects[sInstance->mPendingCoinCollectCount++];
            pending.placeID = packet->placeID;
            pending.worldID = packet->worldID;
            pending.stage = packet->stage;
            hk::diag::logLine("updateCoinCollects: scene not ready, queued (total pending: %d)",
                sInstance->mPendingCoinCollectCount);
        } else {
            hk::diag::logLine("updateCoinCollects: pending queue full, dropping packet");
        }
        return;
    }

    applyOneCoinCollect(packet->placeID.cstr(), packet->worldID, packet->stage.cstr());
}

/**
 * @brief Core logic for marking a checkpoint as collected and warpable when a checkpoint get packet is
 * received from the read thread.
 *
 * @param objId
 */
void Client::getOneCheckpoint(const char* objId) {
    GameDataFile* gdf = sInstance->getHolder()->getGameDataFile();

    if (!gdf) {
        hk::diag::logLine("updateCheckpoints: GameDataFile null, dropping");
        return;
    }
    if (!sInstance->mCurStageScene || !gIsSceneAlive) {
        hk::diag::logLine("updateCheckpoints: no scene or scene not alive, dropping");
    }

    al::PlacementId placeId(objId, nullptr, nullptr);
    UniqObjInfo* info = gdf->customSetCheckpointId(&placeId);
    if (info && al::isEqualString(info->getStageName(), sInstance->mStageName.cstr())) {
        CheckpointFlag* checkpoint = rs::tryFindCheckpointFlag(sInstance->mCurStageScene, objId);
        if (checkpoint) {
            al::startHitReaction(checkpoint, "取得");
            al::startAction(checkpoint, "Get");
            rs::requestHideCheckpointFlagBalloon(checkpoint);
            checkpoint->getCheckpoint();
            checkpoint->setAfter();
        }
    }
}

/**
 * @brief Receives a checkpoint get packet from the read thread.
 *
 * @param packet
 */
void Client::updateCheckpoints(CheckpointGet* packet) {
    if (!packet)
        return;

    if (!sInstance)
        return;

    // Own-echo guard - see updateShineInfo. The local checkpoint is logged
    // by syncHooks.cpp:231, and this function is reached both directly
    // (PacketType::CHECKPOINTGET) and from the shared-event apply.
    if (packet->mUserID != Client::getClientId())
        PlayerEventLog::addEvent(packet->mUserID, PlayerEventLog::CHECKPOINT,
            PlayerEventLog::getCheckpointMessage(packet->objId));

    if (!(sInstance->mCurStageScene && gIsSceneAlive)) {
        if (sInstance->mPendingCheckpointCount < sMaxPendingCheckpoints) {
            PendingCheckpoint& pending = sPendingCheckpoints[sInstance->mPendingCheckpointCount++];
            pending.objId = packet->objId;
            hk::diag::logLine("updateCheckpoints: scene not ready, queued (total pending: %d)",
                sInstance->mPendingCoinCollectCount);
        } else {
            hk::diag::logLine("updateCheckpoints: pending queue full, dropping packet");
        }
        return;
    }

    getOneCheckpoint(packet->objId.cstr());
}

/**
 * @brief Main per-frame update. Runs on the game thread.
 *        Drains any coin collects that arrived before the scene was ready.
 */
void Client::update() {
    if (sInstance) {
        // Heartbeat while connected - if a future hang happens after
        // connecting, the last heartbeat line marks roughly when the main
        // thread stopped reaching this point at all, separating "froze
        // before this frame ran again" from "froze inside something this
        // frame called." Was ~1/sec; a real ~1hr co-op session filled
        // MpDebugLog's whole buffer with these well before anything else
        // interesting happened (confirmed: 200+ of 242 lines in one real
        // session were this line, unchanged, crowding out the moon-credit
        // events actually being debugged). ~30s granularity is still fine
        // for hang detection and lasts a co-op session's worth of buffer.
        if (sInstance->mSocket->isConnected()) {
            static s32 sHeartbeatFrame = 0;
            if (++sHeartbeatFrame >= 1800) {
                sHeartbeatFrame = 0;
                // The outbox numbers ride the existing heartbeat rather than
                // getting their own line: a non-zero unacked count that does
                // not fall between heartbeats is the signal that something is
                // genuinely wrong with delivery, and it is only readable
                // against the queue depths beside it. `rejected` counts server
                // refusals over the whole session - a steadily climbing one
                // points at the room never reaching Ready, or at the server
                // failing its durable appends.
                MpDebugLog::write(
                    "Client::update heartbeat: sendQueue=%u/%u recvQueue=%u/%u eventQueue=%u/%u "
                    "outbox=%d rejected=%d applied=%llu head=%llu lastReject=%s",
                    sInstance->mSocket->getSendCount(), sInstance->mSocket->getSendMaxCount(),
                    sInstance->mSocket->getRecvCount(), sInstance->mSocket->getRecvMaxCount(),
                    sInstance->mSocket->getEventRecvCount(),
                    sInstance->mSocket->getEventRecvMaxCount(), DurableSync::getPendingCount(),
                    DurableSync::getRejectedCount(),
                    (unsigned long long)DurableSync::getAppliedRevision(),
                    (unsigned long long)sInstance->mRoomHeadRevision,
                    DurableSync::getLastRejectionText());
            }
        }

        // The durable outbox: performs its deferred load, persists dirty state
        // and runs the retransmit tick. Unconditional, because an outbox with
        // entries in it is at its MOST important while disconnected - that
        // backlog is exactly the progress the old fire-and-forget path threw
        // away on every reconnect.
        DurableSync::update();

        // Periodic catch-up sweep.
        //
        // Gap detection alone is not enough: it can only notice a loss when a
        // LATER event arrives to reveal it. If the last event of a burst is
        // the one that was dropped - which is exactly what a full receive
        // queue during a stage load produces - nothing ever follows it and the
        // gap is invisible. This sweep closes that hole. It costs one small
        // packet a minute and the server answers with nothing at all when
        // there is no gap.
        if (sInstance->mEventCatchUpCooldown > 0)
            sInstance->mEventCatchUpCooldown--;

        // No longer gated on a NONZERO applied revision. It used to be, because
        // afterRevision == 0 is the server's "send me the whole room save"
        // request and there was no other way to say "from the beginning" - the
        // reasoning being that a client sitting at zero was covered by the
        // state vector instead. It was not: the vector was gated on the same
        // condition, so such a client did nothing at all, forever, and could
        // not climb off zero because the watermark only advances contiguously.
        // requestEventCatchUp now sends cReplayFromStart in that case.
        if (sInstance->mSocket->isConnected()) {
            if (++sInstance->mEventCatchUpFrames >= 3600) {
                sInstance->mEventCatchUpFrames = 0;
                requestEventCatchUp("sweep");
            }
        } else {
            sInstance->mEventCatchUpFrames = 0;
        }

        // Periodic state-vector publication - the belt to catch-up's braces.
        //
        // Catch-up can only ever return events the server already holds, so it
        // is structurally incapable of recovering a fact that was lost on the
        // way IN. This publishes what the save actually contains and lets the
        // server reconcile both ways, which is what heals that case. ~30s.
        if (sInstance->mSocket->isConnected() && isRoomReady()) {
            if (++sInstance->mStateVectorFrames >= 1800) {
                // Only spend the counter on a publication that actually
                // happened. sendStateVector refuses while there is no live
                // scene, which is exactly the state a guest is in for the whole
                // file select and stage load after joining - and the minutes
                // right after a join are when a late joiner is most desynced.
                // Holding the counter at the threshold means the first vector
                // goes out on the first frame it can, not 30 seconds later.
                if (sendStateVector())
                    sInstance->mStateVectorFrames = 0;
                else
                    sInstance->mStateVectorFrames = 1800;
            }
        } else {
            sInstance->mStateVectorFrames = 0;
        }

        // A capture notification can arrive during the placement window. The
        // pool builds its stand-in on this main-thread tick; run that before
        // puppets consume the capture state so a first request is not memoized
        // as a permanent pool miss for the rest of the stage.
        PuppetCapturePool::update();

        // The reference design steps this from a StageScene::control hook; here
        // it rides the bridge that already exists, at the same point in the
        // frame. Main thread, once per gameplay frame - which is what lets a
        // subclass move an actor safely, unlike the packet-drain context.
        SyncedActor::calcAll(sInstance->mSequence ? sInstance->mSequence->mCurrentScene : nullptr);

        sInstance->mPuppetHolder->update();

        if (sInstance->mRemoteDeathArmed && --sInstance->mRemoteDeathArmFrames <= 0)
            sInstance->mRemoteDeathArmed = false;

        const u64 pendingDeath = sInstance->mPendingDeathLinkId.load();
        if (pendingDeath && sInstance->mCurStageScene && gIsSceneAlive) {
            PlayerActorHakoniwa* player = static_cast<PlayerActorHakoniwa*>(
                rs::getPlayerActor(sInstance->mCurStageScene));
            if (player) {
                sInstance->mPendingDeathLinkId.store(0);
                if (!al::isDead(player)) {
                    sInstance->mRemoteDeathArmed = true;
                    sInstance->mRemoteDeathArmFrames = 600;
                    killMainPlayer(player);
                }
            }
        }

        if (isNeedUpdateShines()) {
            updateShines();
        }

        if (sInstance->mCurStageScene && gIsSceneAlive) {
            const u32 remoteKingdoms = sInstance->mPendingKingdoms.exchange(0);
            for (s32 worldId = 0; worldId < 32; worldId++) {
                if ((remoteKingdoms & (1u << worldId)) != 0)
                    Randomizer::recordKingdomVisitById(worldId);
            }

            // GameDataFile/hack-dictionary and native coin mutations must stay
            // on the game thread. Snapshot-replayed purchases wait here until
            // the downloaded multiplayer save is the one actually loaded.
            sInstance->drainHintUnlocks();
            // Ability/capture unlocks, moon rocks, coin deltas and regional
            // spends - everything that used to write the save straight from the
            // read thread, now applied here and only outside a cutscene.
            sInstance->drainRemoteActions();
            sInstance->drainScenarioAdvances();
            sInstance->drainKingdomProgress();

            GameDataFile* file = sInstance->mHolder.mData
                ? sInstance->mHolder->getGameDataFile()
                : nullptr;
            if (file && sInstance->mTravelingPeachRetryFrames <= 0
                && sInstance->mPendingTravelingPeachStart.exchange(
                    false, std::memory_order_acq_rel)) {
                file->customStartWorldTravelingPeach();
                sInstance->mTravelingPeachPublished.store(true, std::memory_order_release);
            }

            // syncKingdomVisits() appears once: both branches added it here.
            syncKingdomVisits();
            syncScenarioNumbers();
            syncKingdomProgress();
            syncTravelingPeachStart();

            // Drain coin collects that arrived while the scene was loading
            if (sInstance->mPendingCoinCollectCount > 0) {
                hk::diag::logLine("update: draining %d pending coin collect(s)",
                    sInstance->mPendingCoinCollectCount);
                for (s32 i = 0; i < sInstance->mPendingCoinCollectCount; i++) {
                    PendingCoinCollect& p = sPendingCoinCollects[i];
                    applyOneCoinCollect(p.placeID.cstr(), p.worldID, p.stage.cstr());
                }
                sInstance->mPendingCoinCollectCount = 0;
            }

            // Drain checkpoints that arrived while the scene was loading
            if (sInstance->mPendingCheckpointCount > 0) {
                hk::diag::logLine("update: draining %d pending checkpoint(s)",
                    sInstance->mPendingCheckpointCount);
                for (s32 i = 0; i < sInstance->mPendingCheckpointCount; i++) {
                    PendingCheckpoint& c = sPendingCheckpoints[i];
                    getOneCheckpoint(c.objId.cstr());
                }
                sInstance->mPendingCheckpointCount = 0;
            }
        }

        if (GameDataFunction::isGameClear(Client::instance()->getHolder())) {
            // Set moon rock scenarios for rocks that were hit prior to the player beating the game
            for (s32 i = 0; i < SNumMoonRocks; i++) {
                if (Client::instance()->mPendingMoonRocks[i]) {
                    Client::instance()->getHolder()->getGameDataFile()->getScenarioNumArr()[i] = moonRockScenarios[i];
                }
            }
        }
    }
}

/**
 * @brief Clears puppet, shine, and coin collect arrays. Called on scene exit.
 */
void Client::clearArrays() {
    if (sInstance) {
        sInstance->mPuppetHolder->clearPuppets();
        sInstance->mShineArray.clear();
        sInstance->mCoinCollectArray.clear();
        sInstance->mCoinCollect2DArray.clear();
    }
}

/*void Client::setNeedUpdateHealthCoins(bool value) {
    if (!sInstance) {
        return;
    }

    sInstance->mNeedsUpdateHealthCoins = value;
}

void Client::updateHealthCoins(HealthCoins* packet) {
    if (!sInstance) {
        return;
    }

    sInstance->mHealth = packet->health;
    sInstance->mCoins = packet->coins;
    sInstance->isKids = packet->isKids;
    sInstance->mNeedsUpdateHealthCoins = true;
}*/

Keyboard* Client::getKeyboard() {
    if (sInstance) {
        return sInstance->mKeyboard;
    }
    return nullptr;
}

const char* Client::getCurrentIP() {
    if (sInstance) {
        return sInstance->mServerIP.cstr();
    }
    return nullptr;
}

const int Client::getCurrentPort() {
    if (sInstance) {
        return sInstance->mServerPort;
    }
    return -1;
}

bool Client::hasServerChanged() {
    if (!sInstance) {
        return false;
    }
    hk::diag::logLine("client port: %d\n socket port: %d\n client ip: %s\n socket ip: %s", getCurrentPort(),
        sInstance->mSocket->getPort(), getCurrentIP(), sInstance->mSocket->getIP());
    return (getCurrentPort() != sInstance->mSocket->getPort() || !al::isEqualString(getCurrentIP(), sInstance->mSocket->getIP()));
}

void Client::setLastUsedIP(const char* ip) {
    if (sInstance) {
        sInstance->mServerIP = ip;
    }
}

void Client::setLastUsedPort(const int port) {
    if (sInstance) {
        sInstance->mServerPort = port;
    }
}

bool Client::tryRegisterShine(Shine* shine) {
    if (sInstance) {
        if (!sInstance->mShineArray.isFull()) {
            if (!shine->isGot()) {
                sInstance->mShineArray.pushBack(shine);
                return true;
            }
        }
    }
    return false;
}

void Client::tryRegisterCoinCollect(CoinCollect* coin) {
    if (sInstance) {
        if (!sInstance->mCoinCollectArray.isFull()) {
            sInstance->mCoinCollectArray.pushBack(coin);
        }
    }
}

void Client::tryRegisterCoinCollect2D(CoinCollect2D* coin) {
    if (sInstance) {
        if (!sInstance->mCoinCollect2DArray.isFull()) {
            sInstance->mCoinCollect2DArray.pushBack(coin);
        }
    }
}

Shine* Client::findStageShine(int shineID) {
    if (sInstance) {
        // shineID is the *credited/destination* uid (see mpOnSetGotShine in
        // syncHooks.cpp -- it sends curInfo->uniqueId from the HintInfo slot
        // matching the credited ShineInfo, not the physically picked-up one).
        //
        // We deliberately do NOT index the hint list with mShineIdx
        // (via getHintInfoByIndex) here. mShineIdx is a placement index,
        // and only happens to line up with an entry's own uniqueId in a
        // vanilla, unshuffled layout. Under moon rando that 1:1
        // relationship no longer holds, so indexing by mShineIdx can
        // return a *different* HintInfo than the one this shine actually
        // represents.
        //
        // We also can't match a registered shine's own curShineInfo
        // (its physical placement stageName/objId) directly against the
        // credited HintInfo's stageName/objId -- those two only coincide
        // when a moon *isn't* remapped. Under moon rando, the physical
        // shine actor standing in the world and the moon it's credited as
        // are different entries, so a direct compare silently fails,
        // findStageShine returns nullptr, and the caller falls back to
        // setGotShine() without ever touching the live actor -- the shine
        // isn't killed and never plays its collected animation on this
        // client.
        //
        // Instead, for each physically registered shine we run its own
        // curShineInfo through the same physical -> credited remap
        // (MoonRando::getCreditMoon) that mpOnSetGotShine's source path
        // uses, and compare the resulting credited uid against shineID.
        // When moon rando isn't active/initialized, getCreditMoon has
        // nothing to remap and every shine is its own credited moon, so
        // this still matches shines up by their physical identity.
        for (int i = 0; i < sInstance->mShineArray.size(); i++) {
            Shine* curShine = sInstance->mShineArray[i];

            if (!curShine || !curShine->curShineInfo)
                continue;

            const ShineDB::ShineEntry* credited = MoonRando::isInitialized()
                ? MoonRando::getCreditMoon(curShine->curShineInfo->getObjId(), curShine->curShineInfo->getStageName())
                : nullptr;

            if (credited) {
                if (credited->uid == shineID) {
                    return curShine;
                }
            } else {
                // Not remapped (or rando inactive) -- this shine's own
                // physical identity is its credited identity, so fall
                // back to the direct HintInfo compare.
                GameDataFile::HintInfo* hintInfo = CustomGameDataFunction::getHintInfoByUniqueID(sInstance->mHolder, shineID);

                if (hintInfo && al::isEqualString(curShine->curShineInfo->getStageName(), hintInfo->stageName.cstr()) && al::isEqualString(curShine->curShineInfo->getObjId(), hintInfo->objId.cstr())) {
                    return curShine;
                }
            }
        }
    }
    return nullptr;
}
