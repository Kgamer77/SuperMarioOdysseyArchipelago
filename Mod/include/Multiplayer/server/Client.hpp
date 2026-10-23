/**
 * @file server/Client.hpp
 * @author CraftyBoss (https://github.com/CraftyBoss)
 * @brief main class responsible for handing all client-server related communications, as well as
 * any gamemodes.
 *
 * @copyright Copyright (c) 2022
 *
 */
#pragma once

// ===== SYSTEM INCLUDES =====
#include <atomic>
#include <cstddef>
#include <stdlib.h>

// ===== AL/GAME ENGINE INCLUDES =====

#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"
#include "al/Library/Sequence/Sequence.h"
#include "al/Library/Thread/AsyncFunctorThread.h"
#include "al/Library/Yaml/Writer/ByamlWriter.h"

// ===== GAME INCLUDES =====
#include "game/Item/CoinCollect.h"
#include "game/Item/CoinCollect2D.h"
#include "game/Item/Shine.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataHolderAccessor.h"

// ===== NINTENDO SDK INCLUDES =====
#include "nn/account.h"

// ===== SEAD INCLUDES =====
#include "sead/container/seadSafeArray.h"
#include "sead/heap/seadDisposer.h"
#include "sead/prim/seadSafeString.h"
#include "sead/thread/seadAtomic.h"

#include "Library/Yaml/ByamlIter.h"
#include "Sequence/HakoniwaSequence.h"
#include "container/seadPtrArray.h"

// ===== PROJECT INCLUDES =====
#include "Keyboard.hpp"
#include "Multiplayer/puppets/PuppetHolder.hpp"
#include "Multiplayer/puppets/PuppetInfo.h"
// SocketClient.hpp pulls in Packet.h, whose own bottom block aggregates
// every packet type including RoomProtocol.h - deliberately not included
// directly here. RoomProtocol.h's own top-of-file #include "Packet.h"
// means a translation unit that reached RoomProtocol.h FIRST (before
// Packet.h was independently parsed) would hit Packet.h's #pragma once
// guard while still mid-parse of RoomProtocol.h itself, leaving its types
// (ProtocolHello, RoomStatus, etc.) undefined by the time PacketFactory.h -
// also pulled in from Packet.h's bottom block - tries to reference them.
#include "Multiplayer/server/SocketClient.hpp"

// ===== CONSTANTS =====
#define MAXPUPINDEX 32

// ===== FORWARD DECLARATIONS =====
class HideAndSeekIcon;
struct HealthCoins;

// ===== STRUCTURES =====
struct UIDIndexNode {
    nn::account::Uid uid;
    int puppetIndex;
};

/**
 * @brief Holds coin collect data that arrived before the scene was ready.
 *        Drained each frame in Client::update() once mCurStageScene is valid.
 */
struct PendingCoinCollect {
    sead::FixedSafeString<0x40> placeID;
    int worldID;
    sead::FixedSafeString<0x40> stage;
};

struct PendingCheckpoint {
    sead::FixedSafeString<0x40> objId;
};

// Remote progress that mutates GameDataFile, or that can start a cutscene or a
// stage change.
//
// Two separate problems, one queue.
//
// THREAD SAFETY. These arrive on ClientReadThread but write the save. Coin
// collects, checkpoints and hint unlocks were already deferred to the game
// thread for exactly that reason; ability unlocks, capture unlocks, moon rocks,
// regional coin spends and yellow-coin deltas were not, and wrote GameDataFile
// straight from the read thread while the game thread was free to be reading or
// writing the same fields. That was survivable while events trickled in one at
// a time during play. It stopped being survivable once catch-up replay and
// state-vector reconciliation could deliver a burst of dozens at once, during a
// stage load, which is exactly when a join happens.
//
// CUTSCENE COLLISION. updateMoonRocks calls tryChangeNextStage, and a moon
// credit can start a get-demo. Firing either while a demo is already running is
// the reported soft-lock: GameDataHolder latches one stage-change request per
// scene and silently drops a second (see changeNextStage's own notes), so a
// remote request landing mid-demo can leave the player in a cutscene whose exit
// transition has already been consumed. Draining only while no demo is active
// costs a few frames of latency and removes the collision entirely.
struct PendingRemoteAction {
    enum Kind : u8
    {
        AbilityUnlock,
        CaptureUnlock,
        MoonRock,
        CoinCollectUse,
        AddCoin,
        SubCoin,
    };

    u64 senderId[2];
    s32 valueA; // abilityId / captureIndex / worldId
    s32 valueB; // count, where the kind carries one
    u8 kind;
    u8 reserved[3];
};

// Hint events are received on ClientReadThread but mutate GameDataFile and the
// native coin balance. They are copied into an SPSC queue and drained by
// Client::update() on the game thread once the downloaded/selected save is
// actually live.
struct PendingHintUnlock {
    s32 unlockKind;
    s32 shineIndex;
    s32 amount;
    // Capture/ability catalog index plus one; zero means this event has no
    // flag key. Keeping only the index makes a full late-join replay compact.
    u16 flagIndexPlusOne;
    u8 category;
    u8 reserved;
};

// ===== MAIN CLASS =====
class Client {
    SEAD_SINGLETON_DISPOSER(Client)

public:
    // ===== CONSTRUCTOR/DESTRUCTOR =====
    Client();

    // ===== INITIALIZATION =====
    static Client* get();
    GameDataHolderAccessor& getHolder() { return mHolder; }
    void init(const al::LayoutInitInfo& initInfo, GameDataHolderAccessor holder);
    bool startThread();

    // ===== STATIC CONNECTION METHODS =====
    static void restartConnection();
    // Connects to the dedicated server with a room code entered from the
    // title screen's Multiplayer button (PauseMenuHooks.cpp) - replaces the
    // old boot-time keyboard prompt that used to live in init(). Safe to call
    // any time after boot: startThread() is already designed to be
    // re-callable.
    static void connectWithRoomCode(const char* roomCode);
    // Opens the room-code keyboard and calls connectWithRoomCode() if a code
    // was entered. This is what the title screen's Multiplayer button
    // actually calls - see the .cpp for why the blocking keyboard call is
    // safe from a per-frame nerve executor.
    static bool promptRoomCodeAndConnect();
    // connectWithRoomCode() plus the bounded wait for the server's room
    // assignment - i.e. promptRoomCodeAndConnect() with the keyboard step
    // removed. The title Multiplayer menu's Connect row already has the room
    // name typed, so it calls this directly; promptRoomCodeAndConnect() calls
    // it too, so both entry points share one definition of "connected" and
    // cannot drift apart.
    static bool connectAndAwaitRoom(const char* roomCode);
    // Cleanly drops an active connection and stays disconnected - for
    // backing out of Multiplayer to play single-player instead (the title's
    // Randomize! row calls this). Deliberately NOT signalReset()/
    // restartConnection(): those close the socket and then auto-reconnect
    // (SocketClient::exeReset() ends in RECONNECT), which is exactly the
    // "still connected to MP after going solo" bug this exists to fix.
    // Clearing mServerIP/mRoomCode first means that auto-reconnect still
    // fires internally but harmlessly no-ops, same as "never entered a room
    // code" - see the .cpp.
    static void disconnect();
    static bool isSocketActive() { return sInstance ? sInstance->mSocket->isConnected() : false; }
    static bool isFirstConnect() { return sInstance ? sInstance->mIsFirstConnect : false; }

    // Whether a room code has ever been entered this session (via the title
    // screen's Multiplayer button) - distinct from isSocketActive(), which is
    // also false for a player who never touched multiplayer at all. Used to
    // gate the on-screen connection status indicator so solo players never
    // see it.
    static bool hasAttemptedConnection() { return sInstance && !sInstance->mRoomCode.isEmpty(); }
    static bool isRoomAssigned() { return sInstance && sInstance->mRoomAssigned; }
    static bool hasRoomError() { return sInstance && sInstance->mRoomError != MultiplayerProtocol::RoomError::None; }
    static bool isSetupOwner() {
        return sInstance && sInstance->mRoomAssigned
            && sInstance->mRoomRole == MultiplayerProtocol::RoomRole::SetupOwner;
    }
    static bool isGuest() {
        return sInstance && sInstance->mRoomAssigned
            && sInstance->mRoomRole == MultiplayerProtocol::RoomRole::Guest;
    }
    // Hands an ActorSync packet to the send queue. A thin wrapper so
    // SyncedActor does not need to know about the socket or its queueing
    // rules; it also takes ownership, so a queue refusal cannot leak.
    static void queueActorSync(struct ActorSync* packet);

    static bool isRoomReady() {
        return sInstance && sInstance->mRoomState == MultiplayerProtocol::RoomState::Ready;
    }
    static bool isDeathLinkEnabled() {
        return sInstance && (sInstance->mRoomFlags & MultiplayerProtocol::FlagDeathLink) != 0;
    }
    // Whether opposing teams are visible to each other. Enforced by the SERVER
    // (it simply stops relaying their player packets), so this is only used for
    // display and for the belt-and-braces puppet gate in PuppetHolder.
    static bool isSyncOtherTeamsEnabled() {
        return sInstance && (sInstance->mRoomFlags & MultiplayerProtocol::FlagSyncOtherTeams) != 0;
    }

    // ===== TEAMS =====
    //
    // Two values, deliberately: mLocalTeam is what this player picked on the
    // file select, mRoomTeam is what the server confirmed in RoomStatus. They
    // differ for the moment between sending TeamSelect and the reply landing,
    // and permanently for a player who never connected at all - in which case
    // the local pick is still the right thing to show on the settings page.

    // 0-based, so 0 is "Team 1" everywhere except the UI strings.
    static u8 getLocalTeam() { return sInstance ? sInstance->mLocalTeam : 0; }
    // The confirmed team when there is one, otherwise the local pick.
    static u8 getTeam() {
        if (!sInstance)
            return 0;
        return sInstance->mRoomAssigned ? sInstance->mRoomTeam : sInstance->mLocalTeam;
    }
    // Records the pick and, when connected, tells the server. Safe to call
    // while disconnected: the value sticks and is re-sent on the next join.
    static void setLocalTeam(u8 team);
    static void sendTeamSelect();

    // Which team a given player is on, from the roster the server maintains.
    // Answers the local team for our own id, and 0 for anyone unknown.
    static u8 getTeamOf(const nn::account::Uid& id);
    // Roster access for the in-game team panel. Entries are ordered by team
    // then join time, which is the order the panel draws them in.
    static s32 getRosterCount() { return sInstance ? sInstance->mRosterCount : 0; }
    static const TeamRosterEntry* getRosterEntry(s32 index) {
        if (!sInstance || index < 0 || index >= sInstance->mRosterCount)
            return nullptr;
        return &sInstance->mRoster[index];
    }
    // Team label for UI. 1-based, since "Team 0" means nothing to a player.
    static const char* getTeamName(u8 team) {
        static const char* names[MultiplayerProtocol::cTeamCount] = { "Team 1", "Team 2", "Team 3",
            "Team 4" };
        return names[team < MultiplayerProtocol::cTeamCount ? team : 0];
    }
    static const char* getRoomStatusMessage() {
        return sInstance ? sInstance->mRoomStatusMessage.cstr() : "Not connected.";
    }
    // The server's HEAD revision for our team, purely for display. What this
    // client has actually APPLIED is DurableSync::getAppliedRevision(), and the
    // two must not be conflated - see updateRoomStatus for the reconnect bug
    // that conflating them caused.
    static u64 getRoomHeadRevision() { return sInstance ? sInstance->mRoomHeadRevision : 0; }

    // ===== PLAYER CONNECTION METHODS =====
    bool isPlayerConnected(int index) { return mPuppetInfoArr[index]->isConnected; }
    static int getConnectCount() {
        if (sInstance) {
            int conn = 0;
            for (PuppetInfo* pup : sInstance->mPuppetInfoArr) {
                if (pup->isConnected)
                    conn++;
            }
            return conn;
        }
        return 0;
    }
    static int getMaxPlayerCount() { return sInstance ? sInstance->maxPuppets + 1 : 8; }

    // ===== SHINE MANAGEMENT =====
    static bool isNeedUpdateShines();
    bool isShineCollected(int shineId);
    // Deliberately no raw index accessors any more. getCollectedShinesCount()
    // returned the table's CAPACITY rather than its live count - a name that
    // read as the opposite of what it did - and pairing it with a bare
    // getShineID(i) handed out unsynchronised reads of a table the read thread
    // writes. updateShines() takes the entries out under the lock instead; see
    // ShineTableLock in Client.cpp.
    void resetCollectedShines();
    void removeShine(int shineId);
    static bool tryRegisterShine(Shine* shine);
    static Shine* findStageShine(int shineID);
    static void updateShines();

    // ==== COINCOLLECT MANAGEMENT ====
    static void tryRegisterCoinCollect(CoinCollect* coin);
    static void tryRegisterCoinCollect2D(CoinCollect2D* coin);

    // ==== MOON ROCK MANAGEMENT ====
    void saveMoonRocks(al::ByamlWriter* writer);
    void readMoonRocks(const al::ByamlIter& save);

    // ===== PACKET SENDING METHODS =====
    static void sendHackCapInfPacket(const HackCap* hackCap);
    static void sendPlayerInfPacket(const PlayerActorBase* player, bool isYukimaru);
    static void sendGameInfPacket(const PlayerActorHakoniwa* player, GameDataHolderAccessor holder);
    static void sendGameInfPacket(GameDataHolderAccessor holder);
    static void sendCaptureInfPacket(const PlayerActorHakoniwa* player);
    static void sendCostumeInfPacket(const char* body, const char* cap);
    static void sendShineCollectPacket(int shineId, int abilityId = -1);
    static void sendCoinCollectCollectPacket(const char* placeID, int worldID, const char* stage);
    static void sendCheckpointGetPacket(const char* objId);
    static void sendMoonRockHitPacket(int worldId);
    static void sendGameStartPacket();
    static void sendAbilityUnlockPacket(int abilityId);
    static void sendCaptureUnlockPacket(int captureIndex);
    static void sendCoinCollectUsePacket(int worldID, int count);
    // A kingdom reached for the first time. Sent as a shared event rather than
    // left to GameInf alone so it lands in the team's log and replays on join -
    // otherwise a player who was offline keeps seeing "?" moon counts for a
    // kingdom their team has already been to.
    static bool sendKingdomVisitPacket(int worldId);
    // A completed hint purchase: the debit, the price-ladder step and (for the
    // hint kinds) the unlock, so a team shares one wallet instead of one each.
    // See HintSystem::SharedUnlock for what the first argument means.
    static bool sendHintUnlockPacket(int unlockKind, int shineIndex, const char* flagKey,
        int category, int amount, u32 buyerPrePurchaseCount);
    static bool reserveCoinSpend(int worldID, int count, u64* requestId);
    static void commitCoinSpend(u64 requestId);
    static void releaseCoinSpend(u64 requestId);
    static void onPlayerDeathDetected();

    // Pushes the local seed's multiplayer settings (currently just death link)
    // to the server as this room's flags. Only meaningful from the setup owner
    // - the server ignores it from anyone else - and sent once the room role is
    // known, so a guest's own local setting can never overwrite the host's.
    static void sendRoomSettings();

    static void sendAddCoinPacket(int count);
    static void sendSubCoinPacket(int count);
    // Purely a Player Event Log signal, distinct from DEATHLINK/
    // onPlayerDeathDetected above (which kills the OTHER player when
    // death-link mode is enabled) - this always fires regardless of that
    // setting.
    static void sendPlayerDeathPacket();
    static void sendMoonRockKeyUnlockPacket(int kingdom);
    static void sendPaintingUnlockPacket(int paintingIndex);
    // A kingdom's scenario number advancing. Previously derivable only inside
    // updateShines()'s scenarioSyncList walk, which needs a live StageScene and
    // so could not run during a join replay at the file select.
    static void sendScenarioAdvancePacket(int worldId, int scenarioNo);
    // Kingdom progression as STATE - the completed prefix, the resolved branch
    // choices and forced peace - diffed from update() rather than hooked at
    // each mutation site. See the .cpp for why syncing the mutations was
    // partial by construction.
    static void syncKingdomProgress();
    // "Here is everything my save actually has." See StateVector in
    // RoomProtocol.h: the only mechanism that can recover progress lost before
    // it ever reached the server, which revision catch-up cannot.
    //
    // Returns whether a vector actually went out. It refuses while there is no
    // live scene, or while this save has not applied anything from the current
    // team's log, and the caller holds its timer at the threshold in that case
    // so the first publication lands on the first frame it can.
    static bool sendStateVector();

    // Whether a team-mate's moon-rock key / painting unlock has been applied
    // here. Backed by DurableSync's persisted masks rather than the session-only
    // RAM arrays these used to read, so the answer survives a restart.
    static bool hasRemoteMoonRockKeyUnlock(s32 kingdom);
    static bool hasRemotePaintingUnlock(s32 paintingIndex);

    // Read thread -> game thread. Both mutate GameDataFile, so they park here
    // and are drained from update() like coin collects and checkpoints.
    static void queueScenarioAdvance(s32 worldId, s32 scenarioNo);

    // Whether a cutscene owns the player right now. Nothing that can start a
    // demo, request a stage change, or move progression underneath one may run
    // while this is true - see PendingRemoteAction for the soft-lock it exists
    // to prevent.
    static bool isDemoActiveNow();

    // ===== PUPPET MANAGEMENT =====
    static bool tryAddPuppet(PuppetActor* puppet);

    // PuppetInfo is written by ClientReadThread and consumed by the game
    // thread. It is a native aggregate rather than a collection of atomics,
    // so readers must hold this short process-wide lock while copying or
    // acting on it. The lock is deliberately owned by Client: every path
    // that can touch a PuppetInfo already has access to the singleton.
    static void lockPuppetState();
    static void unlockPuppetState();

    // Self-healing shared-event sync. Every moon, coin, checkpoint, ability
    // and capture unlock reaches the other player as ONE edge-triggered
    // SharedEvent and nothing re-sends it, so anything that loses one - a full
    // receive queue, a reconnect, a stall - silently costs that player the
    // item forever. The server already keeps a persisted, per-team,
    // revision-ordered log of every accepted event; this asks it for whatever
    // this client has not seen.
    static void requestEventCatchUp(const char* reason);

    static PuppetActor* getPuppet(int idx);
    static PuppetInfo* getPuppetInfo(int idx);
    static PuppetInfo* getLatestInfo();
    static PuppetHolder* getPuppetHolder() {
        if (sInstance)
            return sInstance->mPuppetHolder;
        return nullptr;
    }

    // ===== CLIENT INFO GETTERS =====
    static const char* getClientName() { return sInstance ? sInstance->mUsername.cstr() : "Player"; }
    static nn::account::Uid getClientId() { return sInstance ? sInstance->mUserID : nn::account::Uid(); }
    static sead::FixedSafeString<0x20> getUsername() {
        return sInstance ? sInstance->mUsername : sead::FixedSafeString<0x20>::cEmptyString;
    }
    // static bool shouldKids() { return sInstance ? sInstance->isKids : false; }
    // static u8 getHealth() { return sInstance ? sInstance->mHealth : 3; }
    // static int getCoins() { return sInstance ? sInstance->mCoins : 0; }
    // static bool isNeedUpdateHealthCoins() { return sInstance ? sInstance->mNeedsUpdateHealthCoins : false;
    // } static void setNeedUpdateHealthCoins(bool value);

    static bool isThreadDone() { return sInstance ? sInstance->mReadThread->isDone() : true; }

    // Hands an already-built packet to the send thread, for a sender that
    // lives outside this class - the room snapshot transfer
    // (Multiplayer/RoomSnapshot.cpp), which is the only thing that streams
    // thousands of packets in a row and therefore needs the queue depth below
    // to pace itself. Every other sender is a send*Packet() member above.
    //
    // Takes ownership either way: SocketClient::queuePacket() deletes the
    // packet on every path that does not hand it to the send thread.
    static bool queueRoomPacket(Packet* packet) {
        if (!sInstance || !packet) {
            delete packet;
            return false;
        }
        return sInstance->mSocket->queuePacket(packet);
    }
    // How many packets are still waiting to go out. The queue holds 100 and
    // DROPS silently once full, so a bulk sender has to watch this.
    static u32 getSendQueueDepth() { return sInstance ? sInstance->mSocket->getSendCount() : 0; }

    // ===== SERVER CONFIGURATION =====
    static const int getCurrentPort();
    static const char* getCurrentIP();
    static bool hasServerChanged();
    static void setLastUsedIP(const char* ip);
    static void setLastUsedPort(const int port);
    static void setServerIP(const char* ip);
    static void setServerPort(int port);

    // ===== SERVER VISIBILITY =====
    static bool isServerHidden() { return sInstance ? sInstance->mServerHidden : true; }
    static void setServerHidden(bool hide) {
        if (sInstance) {
            sInstance->mServerHidden = hide;
        }
    }
    static void toggleServerHidden() {
        if (sInstance) {
            sInstance->mServerHidden = !sInstance->mServerHidden;
        }
    }

    // ===== MUSIC SETTINGS =====
    static bool isMusicDisabled() { return sInstance->mIsDisableMusic; }
    static void toggleMusicDisabled() {
        if (sInstance) {
            sInstance->mIsDisableMusic = !sInstance->mIsDisableMusic;
        }
    }

    // ===== RUMBLE =====
    static bool shouldStopRumble() { return sInstance ? sInstance->mShouldStopRumble : false; }
    static void clearStopRumble() {
        if (sInstance)
            sInstance->mShouldStopRumble = false;
    }
    static void setStopRumble() {
        if (sInstance)
            sInstance->mShouldStopRumble = true;
    }
    // ===== UTILITY METHODS =====
    static void update();
    // Compares the visited-kingdom bitmask against what this client has already
    // published and sends an event for anything new. Driven from update()
    // rather than from the one call site that records a visit, so every path
    // that can set a bit is covered without each having to remember to.
    static void syncKingdomVisits();
    static void syncTravelingPeachStart();
    // Same diff-from-update() pattern as syncKingdomVisits, covering every path
    // that can raise a kingdom's scenario number rather than asking each of
    // them to remember to publish.
    static void syncScenarioNumbers();
    static void clearArrays();
    static Keyboard* getKeyboard();
    static PuppetInfo* findPuppetInfo(const nn::account::Uid& id, bool isFindAvailable);

    // ===== STAGE MANAGEMENT =====
    static void setStageInfo(HakoniwaSequence* sequence);
    static void setTagState(bool state);

    // ===== UI METHODS =====
    static bool openKeyboardIP();
    static bool openKeyboardPort();

    // ===== PUBLIC MEMBERS (for debug purposes) =====
    SocketClient* mSocket;

    PlayerInf* getLastPlayerInfPacket() { return &this->lastPlayerInfPacket; }
    GameInf* getLastGameInfPacket() { return &this->lastGameInfPacket; }
    CostumeInf* getLastCostumeInfPacket() { return &this->lastCostumeInfPacket; }
    CaptureInf* getLastCaptureInfPacket() { return &this->lastCaptureInfPacket; }

    static al::Sequence* getSequence() { return sInstance ? sInstance->mSequence : nullptr; }

    static void setSequence(al::Sequence* sequence) {
        if (sInstance) {
            sInstance->mSequence = sequence;
        }
    }

private:
    // ===== CORE FUNCTIONALITY =====
    void readFunc();
    bool startConnection();

    // ===== PACKET HANDLERS =====
    void updatePlayerInfo(PlayerInf* packet);
    void updateHackCapInfo(HackCapInf* packet);
    void updateGameInfo(GameInf* packet);
    void updateCostumeInfo(CostumeInf* packet);
    void updateShineInfo(ShineCollect* packet);
    void updatePlayerConnect(PlayerConnect* packet);
    void updateCaptureInfo(CaptureInf* packet);
    void sendToStage(ChangeStagePacket* packet);
    void disconnectPlayer(PlayerDC* packet);
    void disconnectAll();
    void updateCoinCollects(CoinCollectCollect* packet);
    void updateCheckpoints(CheckpointGet* packet);
    void updateMoonRocks(MoonRockHit* packet);
    void updateAbilityUnlock(AbilityUnlock* packet);
    void updateCaptureUnlock(CaptureUnlock* packet);
    void updateCoinCollectUse(CoinCollectUse* packet);
    void updateRoomStatus(RoomStatus* packet);
    void updateTeamRoster(TeamRoster* packet);
    void updateSharedEvent(SharedEvent* packet);
    void updateCoinReserveResult(CoinReserveResult* packet);
    void updateDeathLink(DeathLink* packet);
    bool enqueueHintUnlock(const PendingHintUnlock& pending);
    void drainHintUnlocks();
    // See PendingRemoteAction. enqueue runs on the read thread, apply and drain
    // on the game thread, and drain refuses to run while a demo is active.
    bool enqueueRemoteAction(const PendingRemoteAction& pending);
    void drainRemoteActions();
    void applyRemoteAction(const PendingRemoteAction& pending);
    void applyMoonRock(s32 worldId);
    void updateAddCoin(AddCoin* packet);
    void updateSubCoin(SubCoin* packet);
    void updatePlayerDeath(PlayerDeath* packet);
    void updateMoonRockKeyUnlock(MoonRockKeyUnlock* packet);
    void updatePaintingUnlock(PaintingUnlock* packet);
    void drainScenarioAdvances();
    // Merges whatever kingdom progression the team has reported into this save,
    // in one monotone pass. Replaces a per-frame retry loop that could not work
    // for a player who was behind the team - see the .cpp.
    void drainKingdomProgress();

    // ===== UTILITY METHODS =====

    /**
     * @brief Core logic for applying a coin collect to game state and killing the actor.
     *        Called both from updateCoinCollects (when scene is ready) and from update()
     *        when draining mPendingCoinCollects.
     */
    static void applyOneCoinCollect(const char* placeID, int worldID, const char* stage);
    static void getOneCheckpoint(const char* objId);

    // ===== CONNECTION MEMBERS =====
    al::AsyncFunctorThread* mReadThread = nullptr;

    // See lockPuppetState()/PuppetStateGuard. This is not used as a general
    // Client mutex: packet queues and the room UI have their own ownership;
    // this only protects the non-atomic PuppetInfo records shared with the
    // game thread. lockPuppetState()/unlockPuppetState() are re-entrant on the
    // owning thread because model construction can synchronously invoke a
    // packet-sending hook that takes the guard again. The owner token uses the
    // Horizon TLS-region address instead of C++ thread_local storage because
    // this code also runs on the NSS module's custom worker thread.
    std::atomic_flag mPuppetStateLock = ATOMIC_FLAG_INIT;
    std::atomic<u64> mPuppetStateLockOwner = 0;
    std::atomic<s32> mPuppetStateLockDepth = 0;

    bool mShouldStopRumble = false;
    nn::account::Uid mUserID;
    sead::FixedSafeString<0x20> mUsername;
    bool mIsConnectionActive = false;
    bool mIsFirstConnect = true;

    // ===== SERVER CONFIGURATION MEMBERS =====
    hostname mServerIP;
    int mServerPort = 0;
    sead::FixedSafeString<64> mServerVersion;
    bool mServerHidden = true;
    bool mIsDisableMusic = false;

    // Dedicated-server room code, set from connectWithRoomCode() (called from
    // the title screen's Multiplayer button, PauseMenuHooks.cpp) and sent as
    // a RoomJoin packet once startConnection()'s TCP handshake completes -
    // see both for why this replaced the old Firestore/STUN/hole-punch room
    // flow. Stays empty for a player who never opens the Multiplayer menu -
    // mServerIP stays empty too in that case, so startConnection() doesn't
    // even attempt to connect.
    sead::FixedSafeString<0x20> mRoomCode;
    bool mProtocolAccepted = false;
    bool mRoomAssigned = false;
    MultiplayerProtocol::RoomRole mRoomRole = MultiplayerProtocol::RoomRole::Guest;
    MultiplayerProtocol::RoomState mRoomState = MultiplayerProtocol::RoomState::Closing;
    MultiplayerProtocol::RoomError mRoomError = MultiplayerProtocol::RoomError::None;
    u8 mRoomFlags = 0;
    // See getLocalTeam/getTeam for why both exist. mLocalTeam survives a
    // disconnect on purpose - it is the player's setting, not the room's.
    u8 mLocalTeam = 0;
    u8 mRoomTeam = 0;
    TeamRosterEntry mRoster[MultiplayerProtocol::cMaxRosterEntries] = {};
    s32 mRosterCount = 0;
    // Visited-kingdom bits this client has already published, so update()'s
    // diff never re-sends one. Atomic because the read thread sets bits when a
    // team-mate's visit arrives, while update() reads them on the main thread.
    sead::Atomic<u32> mPublishedKingdoms = 0;
    sead::Atomic<u32> mPendingKingdoms = 0;
    std::atomic<bool> mPendingTravelingPeachStart { false };
    std::atomic<bool> mTravelingPeachPublished { false };
    s32 mTravelingPeachRetryFrames = 0;
    // mRoomRevision is deliberately gone: the desync work replaced that single
    // number with the head/watermark split below, and keeping it would leave a
    // second, stale idea of where the log is.
    //
    // The server's head, from RoomStatus and from every accepted event. NOT the
    // number catch-up asks from - see updateRoomStatus.
    u64 mRoomHeadRevision = 0;
    // Frames until the next unconditional catch-up sweep, and a small
    // cooldown so a burst of gaps cannot spam the server.
    s32 mEventCatchUpFrames = 0;
    s32 mEventCatchUpCooldown = 0;
    // Frames until the next state-vector publication.
    s32 mStateVectorFrames = 0;
    u64 mServerInstance[2] = {};
    u64 mRoomSessionId[2] = {};
    sead::FixedSafeString<MultiplayerProtocol::cMessageSize> mRoomStatusMessage;
    // Coin-reservation request ids only. Durable event ids are assigned by
    // DurableSync now, which mixes a per-process nonce in the same way this
    // used to for hint purchases alone - so that an id can never collide with
    // one from an earlier boot that the server still holds in its dedup set.
    u64 mNextEventId = 1;
    sead::Atomic<u64> mCoinReserveResultId = 0;
    sead::Atomic<s32> mCoinReserveDecision = 0;
    sead::Atomic<s32> mCoinReserveAvailable = 0;
    sead::Atomic<u64> mPendingDeathLinkId = 0;
    bool mRemoteDeathArmed = false;
    s32 mRemoteDeathArmFrames = 0;

    // ===== SHINE SYNCHRONIZATION MEMBERS =====
    // Moons received from other players and not yet applied to this save.
    //
    // Written by updateShineInfo() on the READ thread and drained by
    // updateShines() on the GAME thread, both under ShineTableLock (Client.cpp)
    // - the two used to touch it with no synchronisation at all, and the
    // wholesale wipe that ended updateShines() erased whatever the read thread
    // had stored during the walk. That is a moon the sender had and the
    // receiver silently never got.
    //
    // Not a short queue: it drains ONLY on a frame with a live scene and no
    // active demo, so it has to hold everything that arrives in between. At 128
    // it did not - a join replays a whole co-op run's moons with no scene at
    // all, and once the table filled updateShineInfo() discarded every further
    // moon.
    sead::SafeArray<int, 1024> curCollectedShines;
    int collectedShineCount = 0;

    // ===== COIN COLLECT PENDING QUEUE =====
    // Coin collect packets that arrived while mCurStageScene was null are stored here
    // and applied in update() once the scene becomes available.
    // Capacity and storage both live in Client.cpp now. 50 entries, held
    // inline here, was sized for the trickle of a live session where the scene
    // is only briefly unavailable - far too shallow for the case that actually
    // matters, a join replaying the team's ENTIRE purple-coin history with no
    // scene to apply it to. A run with a few hundred coins collected silently
    // lost everything past the fiftieth, which is real progression and the shop
    // currency besides.
    s32 mPendingCoinCollectCount = 0;

    // ===== CHECKPOINT PENDING QUEUE =====
    // Checkpoint get packets that arrived while mCurStageScene was null are stored here
    // and applied in update() once the scene becomes available.
    // Same reasoning and the same move as the coin queue above - a join
    // replays every checkpoint the team has ever touched, with no scene.
    s32 mPendingCheckpointCount = 0;

    // One producer (ClientReadThread), one consumer (the game thread). Use
    // std::atomic rather than sead::Atomic here because sead hardcodes relaxed
    // ordering; acquire/release is required before the consumer reads a slot.
    // The slots themselves live in Client.cpp's zero-initialized module
    // storage. Putting the replay buffer in this heap-allocated singleton can
    // starve the 16 KiB client read thread stack during startup.
    std::atomic<s32> mPendingHintUnlockHead { 0 };
    std::atomic<s32> mPendingHintUnlockTail { 0 };

    // Same SPSC shape and the same storage rule as the hint queue above: the
    // slots live in Client.cpp's module storage rather than in this
    // heap-allocated singleton, which the 16 KiB read thread stack cannot
    // afford to have grow.
    std::atomic<s32> mPendingRemoteActionHead { 0 };
    std::atomic<s32> mPendingRemoteActionTail { 0 };

    // ===== MOON ROCK PENDING QUEUE =====
    // Moon Rocks when players haven't beaten the game yet get stored here.
    // The moon rock scenario is applied when the game is beaten.
    static constexpr s32 SNumMoonRocks = 14;
    sead::SafeArray<bool, SNumMoonRocks> mPendingMoonRocks;

    // Mirrors Randomizer::LogicData::sPaintingLinkCount (LogicData.h) -
    // declared locally rather than including that header here.
    static constexpr s32 SNumPaintingLinks = 10;

    // ===== SCENARIO ADVANCE PENDING QUEUE =====
    // Highest scenario number a team-mate has reported for each kingdom that
    // this save has not applied yet. Written on the read thread, drained on the
    // game thread. Zero means nothing pending; the apply is a max(), so a
    // duplicate or an out-of-order arrival costs nothing.
    //
    // The moon-rock-key and painting-unlock flags that used to live here as
    // session-only RAM arrays are gone: both are persisted masks in DurableSync
    // now, which is what makes them survive a restart and lets the state vector
    // republish them.
    u8 mPendingScenarioNo[SNumMoonRocks] = {};
    // The mirror of the above for the OUTBOUND direction: the highest scenario
    // number this client has already published per kingdom, so update()'s diff
    // never re-sends one. Seeded from the save on first observation rather than
    // published, because the state vector covers the initial state in one pass.
    u8 mPublishedScenarioNo[SNumMoonRocks] = {};
    bool mScenarioBaselineSeeded = false;

    // ===== KINGDOM PROGRESSION =====
    //
    // Mirrors KingdomProgress::SyncState in both directions. There is
    // deliberately no persisted shadow copy of any of this: the completed
    // prefix and branch choices live in the save's own progression record, and
    // forced peace lives in the rando save meta, so those remain the single
    // source of truth and are simply read back on demand. The fields here are
    // only "what have we already told the team" and "what has the team told us
    // that the game thread has not merged yet".
    //
    // mPendingForcedPeace is atomic because the read thread sets bits in it
    // while the game thread drains it; the other two are only touched by the
    // read thread before the game thread clears them, in the same
    // producer/consumer shape as mPendingScenarioNo above.
    static constexpr s32 SNumProgressBranches = 2;
    u8 mPendingCompletedCount = 0;
    s8 mPendingBranchChoice[SNumProgressBranches] = { -1, -1 };
    sead::Atomic<u32> mPendingForcedPeace = 0;
    u8 mPublishedCompletedCount = 0;
    s8 mPublishedBranchChoice[SNumProgressBranches] = { -1, -1 };
    u32 mPublishedForcedPeace = 0;
    bool mKingdomBaselineSeeded = false;

    // ===== PACKET BACKUPS =====
    PlayerInf lastPlayerInfPacket = PlayerInf();
    // PlayerInf's default quaternion is all zeroes, so the object above is not
    // a usable pose until sendPlayerInfPacket() has sampled the game at least
    // once. PLAYERCON must not advertise that sentinel during a join.
    bool mHasLastPlayerInfPacket = false;
    // A peer announcement requests one current pose even when the sampled
    // values happen to equal the cache. This closes the race where the peer
    // misses an earlier pose sent before it processed our PLAYERCON.
    bool mForcePlayerInfSend = false;
    GameInf lastGameInfPacket = GameInf();
    GameInf emptyGameInfPacket = GameInf();
    CostumeInf lastCostumeInfPacket = CostumeInf();
    CaptureInf lastCaptureInfPacket = CaptureInf();

    // ===== UI COMPONENTS =====
    Keyboard* mKeyboard = nullptr;
    al::SimpleLayoutAppearWaitEnd* mConnectStatus = nullptr;

    // ===== GAME STATE MEMBERS =====
    bool isClientCaptured = false;
    bool isSentCaptureInf = false;
    // PlayerHackKeeper::getCurrentHackName() returns null on scattered frames
    // in the middle of a capture the player is plainly still riding - measured
    // on both clients, same keeper pointer, name alternating 'Frog'/(null).
    // isHack() is the reliable state (a plain bool field), but it carries no
    // name, so the last name actually seen is latched here and sent with it.
    sead::FixedSafeString<0x20> lastHackName;

    // Which member of lastHackName's archive family the local player is on -
    // see PuppetInfo::captureVariant. Latched next to the name in
    // sendPlayerInfPacket, which is the only place with the hack actor in hand,
    // and read one call later by sendCaptureInfPacket.
    s32 lastHackVariant = -1;
    // Consecutive frames getCurrentHackName() has come back null. Used to ride
    // out the scattered nulls it returns mid-capture without trusting any one
    // signal on its own.
    s32 hackNullFrames = 0;
    bool isSentHackInf = false;

    // ===== SCENE AND STAGE MEMBERS =====
    const StageScene* mCurStageScene = nullptr;
    sead::PtrArray<Shine> mShineArray;
    sead::PtrArray<CoinCollect> mCoinCollectArray;
    sead::PtrArray<CoinCollect2D> mCoinCollect2DArray;
    sead::FixedSafeString<0x40> mStageName;
    GameDataHolderAccessor mHolder;
    u8 mScenario = 0;

    // ===== PUPPET MANAGEMENT MEMBERS =====
    int maxPuppets = 9;
    PuppetInfo* mPuppetInfoArr[MAXPUPINDEX] = {};
    PuppetHolder* mPuppetHolder = nullptr;

    // ===== SEQUENCE =====
    al::Sequence* mSequence = nullptr; // current sequence, used for debug menu
};

// RAII wrapper for the native packet/game-thread hand-off. It is intentionally
// defined after Client so the inline constructor can call the public static
// lock methods without exposing Client's implementation details to every
// puppet source file.
class PuppetStateGuard {
public:
    PuppetStateGuard() {
        if (Client::get()) {
            Client::lockPuppetState();
            mLocked = true;
        }
    }

    ~PuppetStateGuard() {
        if (mLocked)
            Client::unlockPuppetState();
    }

    PuppetStateGuard(const PuppetStateGuard&) = delete;
    PuppetStateGuard& operator=(const PuppetStateGuard&) = delete;

private:
    bool mLocked = false;
};
