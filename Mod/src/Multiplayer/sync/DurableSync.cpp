#include "Multiplayer/sync/DurableSync.h"

#include "hk/diag/diag.h"

#include "nn/fs.h"
#include "nn/os.h"

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstring>

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/algorithms/crc32.h"
#include "Multiplayer/packets/RoomProtocol.h"
#include "Multiplayer/server/Client.hpp"
#include "SDRedirect/SDPaths.h"

namespace DurableSync {

    namespace {

        // ===== on-disk format =====
        //
        // Header, then exactly `entryCount` live entries. The ring is NOT
        // written in full: a healthy outbox holds zero or one entries, so a
        // save is a few hundred bytes rather than the ring's 37 KB. That
        // matters because a save happens on every submit, which is the game
        // thread, in the middle of gameplay.
        //
        // A torn write is detected by the CRC and the whole file discarded.
        // That is a safe failure: the server's own team log is the authority,
        // so a lost outbox costs at most a re-send that comes back Duplicate,
        // and a lost watermark costs one full catch-up sweep.
        constexpr u32 kMagic = 0x5953504D; // 'MPSY'
        // 2: the kingdom-unlock mask is gone. Kingdom progression lives in
        // the save's own progression record and forced peace in the rando save
        // meta, so mirroring either here was a second source of truth for state
        // that already had one. A version-1 file is rejected by the check in
        // loadLocked and the outbox simply starts clean.
        constexpr u16 kFormatVersion = 2;
        constexpr s32 kRoomCodeSize = 24;

        struct FileHeader {
            u32 magic;
            u16 version;
            u16 entryCount;
            u32 crc; // over everything after this field, header tail included
            u32 reserved0;
            char roomCode[kRoomCodeSize];
            u64 sessionId[2];
            u64 appliedRevision;
            u64 nextSequence;
            u32 moonRockMask;
            u32 moonRockKeyMask;
            u32 paintingMask;
            u8 team;
            // Non-zero once this save is known to carry progress that did NOT
            // come from this team's log - see isSaveProvenForTeam. Taken from
            // the reserved tail, so the layout and the format version are
            // unchanged and a file written before this existed reads as zero,
            // which is the permissive value it always effectively had.
            u8 saveUnproven;
            u8 reserved1[6];
        };

        struct FileEntry {
            u64 eventId;
            u8 kind;
            u8 reserved[3];
            s32 worldId;
            s32 value;
            char key[cKeySize];
            char auxiliary[cKeySize];
        };

        // ===== in-memory entry =====

        struct Entry {
            u64 eventId = 0;
            MultiplayerProtocol::SharedEventKind kind = MultiplayerProtocol::SharedEventKind::Moon;
            s32 worldId = -1;
            s32 value = 0;
            char key[cKeySize] = {};
            char auxiliary[cKeySize] = {};
            // Frame this entry may next be transmitted on. Set into the future
            // by every attempt, so the tick does not have to scan or sort.
            u32 nextAttemptFrame = 0;
            u16 attempts = 0;
            bool live = false;
        };

        Entry sEntries[cCapacity];
        s32 sLiveCount = 0;

        // ===== room identity =====

        char sRoomCode[kRoomCodeSize] = {};
        u8 sTeam = 0;
        u64 sSessionId[2] = {};
        bool sAttached = false;

        // noteRoom runs on the read thread; the load it implies is SD I/O and
        // belongs on the game thread with every other save in this file.
        std::atomic<bool> sLoadPending { false };
        char sPendingRoomCode[kRoomCodeSize] = {};
        u8 sPendingTeam = 0;
        u64 sPendingSessionId[2] = {};

        // ===== persisted state =====

        u64 sAppliedRevision = 0;
        u64 sNextSequence = 1;
        u64 sNonce = 0;
        u32 sMoonRockMask = 0;
        u32 sMoonRockKeyMask = 0;
        u32 sPaintingMask = 0;
        // See isSaveProvenForTeam. Set by a team change on the game thread,
        // cleared by the join replay's commit on the READ thread, and read by
        // sendStateVector on the game thread - so atomic, for the same reason
        // sDirty is and with the same explicit ordering.
        std::atomic<bool> sSaveUnproven { false };

        std::atomic<bool> sDirty { false };

        // ===== diagnostics =====

        s32 sRejectedCount = 0;
        char sLastRejection[96] = "none";

        // ===== timing =====

        u32 sFrame = 0;
        // ~2s at 60fps between retransmit passes.
        constexpr u32 kTickIntervalFrames = 120;
        // Never more than this many re-sends in one pass. The send queue holds
        // 100 slots shared with position updates; a backlog must not be able to
        // fill it and start dropping the very traffic it is trying to protect.
        constexpr s32 kMaxSendsPerTick = 8;
        // Exponential, capped. A long-offline client retries every ~30s rather
        // than every 2s, which is plenty given nothing can succeed until the
        // socket comes back.
        constexpr u32 kBaseBackoffFrames = 120;
        constexpr u32 kMaxBackoffFrames = 1800;

        // ===== lock =====
        //
        // submit() and the tick run on the game thread; onEcho() runs on the
        // read thread. Raw std::atomic with explicit acquire/release rather
        // than sead::Atomic, which hardcodes relaxed ordering and therefore
        // establishes no happens-before at all on ARM - the same trap
        // MpDebugLog documents having actually been bitten by. Bounded acquire
        // for the same reason: this must never be able to wedge a caller.
        std::atomic<bool> sLockHeld { false };
        constexpr s32 kAcquireMaxAttempts = 200;

        class ScopedLock {
        public:
            ScopedLock() {
                bool expected = false;
                for (s32 attempts = 0; !sLockHeld.compare_exchange_weak(
                         expected, true, std::memory_order_acquire, std::memory_order_relaxed);
                     attempts++) {
                    expected = false;
                    if (attempts >= kAcquireMaxAttempts)
                        return;
                    nn::os::YieldThread();
                }
                mAcquired = true;
            }
            ~ScopedLock() {
                if (mAcquired)
                    sLockHeld.store(false, std::memory_order_release);
            }
            bool isAcquired() const { return mAcquired; }

            ScopedLock(const ScopedLock&) = delete;
            ScopedLock& operator=(const ScopedLock&) = delete;

        private:
            bool mAcquired = false;
        };

        // ===== paths =====

        void ensureSyncDir() {
            SDPaths::ensureRandomizerRoot();
            // CreateDirectory is mkdir, not mkdir -p (SDPaths.h), so each level
            // has to exist before the next one can be made.
            nn::fs::CreateDirectory("sd:/SMO-Randomizer/MPSaves");
            nn::fs::CreateDirectory("sd:/SMO-Randomizer/MPSaves/_sync");
        }

        void makePath(char* out, size_t size, const char* roomCode, u8 team) {
            std::snprintf(out, size, "sd:/SMO-Randomizer/MPSaves/_sync/%s_T%d.bin",
                roomCode && roomCode[0] ? roomCode : "NOROOM", (int)team);
        }

        u32 computeCrc(const u8* data, size_t size) {
            u32 crc = 0xFFFFFFFFu;
            for (size_t i = 0; i < size; i++)
                crc = (crc >> 8) ^ crc32::s_Table[(crc ^ data[i]) & 0xFF];
            return ~crc;
        }

        void copyFixed(char* dst, size_t size, const char* src) {
            if (!dst || size == 0)
                return;
            if (!src) {
                dst[0] = '\0';
                return;
            }
            std::strncpy(dst, src, size - 1);
            dst[size - 1] = '\0';
        }

        // ===== persistence =====
        //
        // Caller must hold the lock. Buffer sized for the header plus the whole
        // ring so a pathological backlog still writes in one call; only the
        // live prefix is ever actually written.
        void saveLocked() {
            if (!sAttached)
                return;

            static u8 sBuffer[sizeof(FileHeader) + sizeof(FileEntry) * cCapacity];

            FileHeader* header = reinterpret_cast<FileHeader*>(sBuffer);
            std::memset(header, 0, sizeof(FileHeader));
            header->magic = kMagic;
            header->version = kFormatVersion;
            header->crc = 0;
            copyFixed(header->roomCode, sizeof(header->roomCode), sRoomCode);
            header->sessionId[0] = sSessionId[0];
            header->sessionId[1] = sSessionId[1];
            header->appliedRevision = sAppliedRevision;
            header->nextSequence = sNextSequence;
            header->moonRockMask = sMoonRockMask;
            header->moonRockKeyMask = sMoonRockKeyMask;
            header->paintingMask = sPaintingMask;
            header->team = sTeam;
            header->saveUnproven = sSaveUnproven.load(std::memory_order_acquire) ? 1 : 0;

            u16 written = 0;
            FileEntry* out = reinterpret_cast<FileEntry*>(sBuffer + sizeof(FileHeader));
            for (s32 i = 0; i < cCapacity; i++) {
                if (!sEntries[i].live)
                    continue;
                FileEntry& entry = out[written++];
                std::memset(&entry, 0, sizeof(FileEntry));
                entry.eventId = sEntries[i].eventId;
                entry.kind = (u8)sEntries[i].kind;
                entry.worldId = sEntries[i].worldId;
                entry.value = sEntries[i].value;
                copyFixed(entry.key, sizeof(entry.key), sEntries[i].key);
                copyFixed(entry.auxiliary, sizeof(entry.auxiliary), sEntries[i].auxiliary);
            }
            header->entryCount = written;

            const size_t total = sizeof(FileHeader) + sizeof(FileEntry) * written;
            constexpr size_t kCrcOffset = offsetof(FileHeader, crc) + sizeof(u32);
            header->crc = computeCrc(sBuffer + kCrcOffset, total - kCrcOffset);

            char path[128];
            makePath(path, sizeof(path), sRoomCode, sTeam);

            ensureSyncDir();
            // nn::fs::SetFileSize does not link in this build, so the file has
            // to be recreated at its exact size every time - the same
            // constraint MpDebugLog documents.
            nn::fs::DeleteFile(path);
            if (nn::fs::CreateFile(path, total).IsFailure())
                return;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsFailure())
                return;
            const bool ok = nn::fs::WriteFile(handle, 0, sBuffer, total,
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);

            if (!ok)
                MpDebugLog::write("DurableSync: failed to persist outbox (%d entries)", (int)written);
        }

        // Caller must hold the lock. Merges rather than replaces: a submit can
        // land between noteRoom() flagging the load and update() performing it,
        // and that entry is every bit as real as a persisted one.
        void loadLocked() {
            char path[128];
            makePath(path, sizeof(path), sRoomCode, sTeam);

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return;

            s64 size = 0;
            if (nn::fs::GetFileSize(&size, handle).IsFailure() || size < (s64)sizeof(FileHeader)
                || size > (s64)(sizeof(FileHeader) + sizeof(FileEntry) * cCapacity)) {
                nn::fs::CloseFile(handle);
                return;
            }

            static u8 sReadBuffer[sizeof(FileHeader) + sizeof(FileEntry) * cCapacity];
            const bool read = nn::fs::ReadFile(handle, 0, sReadBuffer, size).IsSuccess();
            nn::fs::CloseFile(handle);
            if (!read)
                return;

            const FileHeader* header = reinterpret_cast<const FileHeader*>(sReadBuffer);
            if (header->magic != kMagic || header->version != kFormatVersion)
                return;
            if (header->entryCount > cCapacity)
                return;

            const size_t expected
                = sizeof(FileHeader) + sizeof(FileEntry) * (size_t)header->entryCount;
            if ((size_t)size != expected)
                return;

            constexpr size_t kCrcOffset = offsetof(FileHeader, crc) + sizeof(u32);
            if (computeCrc(sReadBuffer + kCrcOffset, expected - kCrcOffset) != header->crc) {
                MpDebugLog::write("DurableSync: discarding torn outbox file for room %s team %d",
                    sRoomCode, (int)sTeam);
                return;
            }

            // A file written for a different room, team or server session
            // describes somebody else's run. Resuming from it would replay the
            // wrong team's outbox and, worse, adopt a watermark that has no
            // meaning in this log.
            if (std::strncmp(header->roomCode, sRoomCode, kRoomCodeSize) != 0
                || header->team != sTeam || header->sessionId[0] != sSessionId[0]
                || header->sessionId[1] != sSessionId[1]) {
                MpDebugLog::write(
                    "DurableSync: stored state is for a different room/team/session - starting clean");
                return;
            }

            sAppliedRevision = header->appliedRevision;
            if (header->nextSequence > sNextSequence)
                sNextSequence = header->nextSequence;
            sMoonRockMask |= header->moonRockMask;
            sMoonRockKeyMask |= header->moonRockKeyMask;
            sPaintingMask |= header->paintingMask;
            // Sticky in the unsafe direction: a stint on this team that ended
            // unproven is still unproven when it resumes.
            if (header->saveUnproven)
                sSaveUnproven.store(true, std::memory_order_release);

            const FileEntry* entries
                = reinterpret_cast<const FileEntry*>(sReadBuffer + sizeof(FileHeader));
            s32 restored = 0;
            for (u16 i = 0; i < header->entryCount; i++) {
                const FileEntry& source = entries[i];

                bool duplicate = false;
                for (s32 slot = 0; slot < cCapacity && !duplicate; slot++)
                    duplicate = sEntries[slot].live && sEntries[slot].eventId == source.eventId;
                if (duplicate)
                    continue;

                s32 free = -1;
                for (s32 slot = 0; slot < cCapacity && free < 0; slot++)
                    if (!sEntries[slot].live)
                        free = slot;
                if (free < 0)
                    break;

                Entry& entry = sEntries[free];
                entry.eventId = source.eventId;
                entry.kind = (MultiplayerProtocol::SharedEventKind)source.kind;
                entry.worldId = source.worldId;
                entry.value = source.value;
                copyFixed(entry.key, sizeof(entry.key), source.key);
                copyFixed(entry.auxiliary, sizeof(entry.auxiliary), source.auxiliary);
                entry.attempts = 0;
                entry.nextAttemptFrame = sFrame;
                entry.live = true;
                sLiveCount++;
                restored++;
            }

            MpDebugLog::write(
                "DurableSync: loaded room=%s team=%d applied=%llu restored=%d masks=%x/%x/%x",
                sRoomCode, (int)sTeam, (unsigned long long)sAppliedRevision, restored,
                sMoonRockMask, sMoonRockKeyMask, sPaintingMask);
        }

        // Caller must hold the lock. Returns whether the packet reached the
        // send queue - which is only a hint: a queued packet can still be lost
        // downstream, which is precisely why the entry stays live until an echo
        // retires it.
        bool transmitLocked(Entry& entry) {
            entry.attempts++;
            const u32 shift = entry.attempts < 5 ? entry.attempts : 5;
            u32 backoff = kBaseBackoffFrames << shift;
            if (backoff > kMaxBackoffFrames)
                backoff = kMaxBackoffFrames;
            entry.nextAttemptFrame = sFrame + backoff;

            if (!Client::isSocketActive() || !Client::isRoomAssigned())
                return false;

            auto packet = new (gHeap) SharedEvent;
            packet->mUserID = Client::getClientId();
            packet->eventId = entry.eventId;
            packet->kind = entry.kind;
            packet->status = MultiplayerProtocol::SharedEventStatus::Proposed;
            packet->worldId = entry.worldId;
            packet->value = entry.value;
            packet->key = entry.key;
            packet->auxiliary = entry.auxiliary;
            return Client::queueRoomPacket(packet);
        }

    } // namespace

    void noteRoom(const char* roomCode, u8 team, const u64 sessionId[2]) {
        if (!roomCode || !roomCode[0])
            return;
        copyFixed(sPendingRoomCode, sizeof(sPendingRoomCode), roomCode);
        sPendingTeam = team;
        sPendingSessionId[0] = sessionId ? sessionId[0] : 0;
        sPendingSessionId[1] = sessionId ? sessionId[1] : 0;
        sLoadPending.store(true, std::memory_order_release);
    }

    bool isReady() { return sAttached; }

    u64 submit(MultiplayerProtocol::SharedEventKind kind, s32 worldId, s32 value, const char* key,
        const char* auxiliary) {
        ScopedLock lock;
        if (!lock.isAcquired())
            return 0;

        // Not in a room. Solo play must not accumulate an outbox that would
        // then be flushed at whichever room is joined next.
        if (!sAttached)
            return 0;

        // Same fact already in flight. A hook that fires twice, or a retry the
        // caller made itself, must not burn a second slot.
        for (s32 i = 0; i < cCapacity; i++) {
            const Entry& entry = sEntries[i];
            if (!entry.live || entry.kind != kind || entry.worldId != worldId
                || entry.value != value)
                continue;
            if (std::strncmp(entry.key, key ? key : "", cKeySize) != 0)
                continue;
            if (std::strncmp(entry.auxiliary, auxiliary ? auxiliary : "", cKeySize) != 0)
                continue;
            return entry.eventId;
        }

        s32 free = -1;
        for (s32 i = 0; i < cCapacity && free < 0; i++)
            if (!sEntries[i].live)
                free = i;

        if (free < 0) {
            // 256 unacknowledged facts means the server has been unreachable
            // for a very long time. Nothing useful is left to do but say so
            // loudly - and note that the fact is not silently lost the way it
            // used to be: the periodic StateVector reconciliation will still
            // find it in the save and adopt it once contact resumes.
            MpDebugLog::write("DurableSync: outbox FULL, kind=%d key=%s not tracked", (int)kind,
                key ? key : "");
            return 0;
        }

        // The same nonce-mixed scheme sendHintUnlockPacket already used. The
        // nonce differs per game process, so an id can never collide with one
        // from an earlier boot that the server may still hold in its dedup set.
        const u64 sequence = sNextSequence++;
        u64 eventId = sNonce ^ (sequence * 0xE7037ED1A0B428DBULL);
        if (eventId == 0)
            eventId = sequence ? sequence : 1;

        Entry& entry = sEntries[free];
        entry.eventId = eventId;
        entry.kind = kind;
        entry.worldId = worldId;
        entry.value = value;
        copyFixed(entry.key, sizeof(entry.key), key);
        copyFixed(entry.auxiliary, sizeof(entry.auxiliary), auxiliary);
        entry.attempts = 0;
        entry.live = true;
        sLiveCount++;

        // Persisted by update() on the next frame rather than synchronously
        // here. Every save is a delete+create+write+close, and submits arrive
        // in bursts - a handful of purple coins in one grab is a handful of
        // submits - so writing per submit would put several blocking SD writes
        // in a single gameplay frame. Deferring coalesces a burst into one
        // write and bounds the exposure to one frame, and the state vector
        // heals even that: a fact lost to a crash inside that window is still
        // in the save, and reconciliation adopts it within 30 seconds.
        sDirty.store(true, std::memory_order_release);
        transmitLocked(entry);
        return eventId;
    }

    void onEcho(u64 eventId, MultiplayerProtocol::SharedEventStatus status) {
        if (eventId == 0)
            return;

        ScopedLock lock;
        if (!lock.isAcquired())
            return;

        for (s32 i = 0; i < cCapacity; i++) {
            Entry& entry = sEntries[i];
            if (!entry.live || entry.eventId != eventId)
                continue;

            if (status == MultiplayerProtocol::SharedEventStatus::Accepted
                || status == MultiplayerProtocol::SharedEventStatus::Duplicate) {
                entry.live = false;
                sLiveCount--;
                // The SD write is deferred to update(). Retiring late is
                // harmless - the worst case is one extra retransmit that comes
                // back Duplicate - whereas doing blocking I/O here would put it
                // on the read thread, in a packet handler.
                sDirty.store(true, std::memory_order_release);
                return;
            }

            // Rejected. NOT retired: the commonest cause is the room not being
            // Ready yet (the host is still uploading the seed), which resolves
            // on its own, and the next commonest is the server failing its
            // durable append, which is exactly the case where giving up would
            // lose the fact for good. Back off hard and keep trying.
            sRejectedCount++;
            std::snprintf(sLastRejection, sizeof(sLastRejection), "kind=%d key=%s attempts=%d",
                (int)entry.kind, entry.key, (int)entry.attempts);
            entry.nextAttemptFrame = sFrame + kMaxBackoffFrames;
            MpDebugLog::write("DurableSync: event REJECTED by server, will retry - %s",
                sLastRejection);
            return;
        }
    }

    u64 getAppliedRevision() { return sAppliedRevision; }

    bool tryAdvanceAppliedRevision(u64 revision) {
        ScopedLock lock;
        if (!lock.isAcquired())
            return false;
        // Contiguous only. An event that arrives past a hole is still applied
        // by the caller (every apply is idempotent), but moving the watermark
        // over the hole would tell the catch-up sweep the missing event was
        // already seen and it would never be asked for again.
        if (revision != sAppliedRevision + 1)
            return false;
        sAppliedRevision = revision;
        sDirty.store(true, std::memory_order_release);
        return true;
    }

    void setAppliedRevision(u64 revision) {
        ScopedLock lock;
        if (!lock.isAcquired())
            return;
        // The replay just rebuilt this save from THIS team's log, which is
        // exactly the proof isSaveProvenForTeam is asking for - clear the mark
        // even when the revision itself does not move.
        if (sSaveUnproven.exchange(false, std::memory_order_acq_rel))
            sDirty.store(true, std::memory_order_release);
        if (revision <= sAppliedRevision)
            return;
        sAppliedRevision = revision;
        sDirty.store(true, std::memory_order_release);
    }

    bool isSaveProvenForTeam() { return !sSaveUnproven.load(std::memory_order_acquire); }

    // ===== state masks =====

    namespace {
        bool hasBit(u32 mask, s32 index) {
            return index >= 0 && index < 32 && (mask & (1u << index)) != 0;
        }
        void setBit(u32& mask, s32 index) {
            if (index < 0 || index >= 32)
                return;
            const u32 bit = 1u << index;
            if ((mask & bit) != 0)
                return;
            mask |= bit;
            sDirty.store(true, std::memory_order_release);
        }
    } // namespace

    bool hasMoonRock(s32 worldId) { return hasBit(sMoonRockMask, worldId); }
    void setMoonRock(s32 worldId) { setBit(sMoonRockMask, worldId); }
    bool hasMoonRockKey(s32 worldId) { return hasBit(sMoonRockKeyMask, worldId); }
    void setMoonRockKey(s32 worldId) { setBit(sMoonRockKeyMask, worldId); }
    bool hasPainting(s32 paintingIndex) { return hasBit(sPaintingMask, paintingIndex); }
    void setPainting(s32 paintingIndex) { setBit(sPaintingMask, paintingIndex); }

    u32 getMoonRockMask() { return sMoonRockMask; }
    u32 getMoonRockKeyMask() { return sMoonRockKeyMask; }
    u32 getPaintingMask() { return sPaintingMask; }

    // ===== driving =====

    void update() {
        sFrame++;

        if (sLoadPending.load(std::memory_order_acquire)) {
            ScopedLock lock;
            if (!lock.isAcquired())
                return;
            sLoadPending.store(false, std::memory_order_release);

            const bool sameRoom = sAttached
                && std::strncmp(sRoomCode, sPendingRoomCode, kRoomCodeSize) == 0
                && sTeam == sPendingTeam && sSessionId[0] == sPendingSessionId[0]
                && sSessionId[1] == sPendingSessionId[1];

            if (!sameRoom) {
                // Same room and same server session, different team: the player
                // moved teams while carrying a save built from the OLD team's
                // log. That save now describes progress this team never made,
                // and letting the server adopt from its state vector would
                // inject the whole of it into this team's log. Recorded here
                // because it is the only moment the transition is visible - a
                // frame later the old team number is gone.
                //
                // A different room or a different session is not this case:
                // there is no previous log for the save to have come from.
                const bool teamChange = sAttached
                    && std::strncmp(sRoomCode, sPendingRoomCode, kRoomCodeSize) == 0
                    && sSessionId[0] == sPendingSessionId[0]
                    && sSessionId[1] == sPendingSessionId[1] && sTeam != sPendingTeam;

                // A different room, a different team, or a server that restarted
                // this room under a new session id. None of the previous state
                // means anything against the new log.
                for (s32 i = 0; i < cCapacity; i++)
                    sEntries[i].live = false;
                sLiveCount = 0;
                sAppliedRevision = 0;
                sMoonRockMask = 0;
                sMoonRockKeyMask = 0;
                sPaintingMask = 0;
                sSaveUnproven.store(false, std::memory_order_release);

                copyFixed(sRoomCode, sizeof(sRoomCode), sPendingRoomCode);
                sTeam = sPendingTeam;
                sSessionId[0] = sPendingSessionId[0];
                sSessionId[1] = sPendingSessionId[1];
                sAttached = true;

                if (sNonce == 0) {
                    sNonce = nn::os::GetSystemTick().value * 0x9E3779B97F4A7C15ULL
                        + 0x632BE59BD9B4E019ULL;
                    if (sNonce == 0)
                        sNonce = 0xA0761D6478BD642FULL;
                }

                loadLocked();
                // After the load, so a team change always wins over whatever a
                // previous stint on this team happened to record.
                if (teamChange)
                    sSaveUnproven.store(true, std::memory_order_release);
                saveLocked();
            }
            return;
        }

        if (!sAttached)
            return;

        if (sDirty.exchange(false, std::memory_order_acq_rel)) {
            ScopedLock lock;
            if (lock.isAcquired())
                saveLocked();
            else
                sDirty.store(true, std::memory_order_release);
        }

        if (sLiveCount == 0 || (sFrame % kTickIntervalFrames) != 0)
            return;
        if (!Client::isSocketActive() || !Client::isRoomAssigned())
            return;

        ScopedLock lock;
        if (!lock.isAcquired())
            return;

        s32 sent = 0;
        for (s32 i = 0; i < cCapacity && sent < kMaxSendsPerTick; i++) {
            Entry& entry = sEntries[i];
            if (!entry.live || sFrame < entry.nextAttemptFrame)
                continue;
            transmitLocked(entry);
            sent++;
        }

        if (sent > 0)
            MpDebugLog::write("DurableSync: retransmitted %d of %d pending event(s)", sent,
                sLiveCount);
    }

    void flushNow() {
        if (!sAttached)
            return;

        ScopedLock lock;
        if (!lock.isAcquired())
            return;

        s32 sent = 0;
        for (s32 i = 0; i < cCapacity && sent < kMaxSendsPerTick; i++) {
            Entry& entry = sEntries[i];
            if (!entry.live)
                continue;
            // Reset the ladder: whatever made the previous attempts fail is
            // exactly what has just changed.
            entry.attempts = 0;
            transmitLocked(entry);
            sent++;
        }

        if (sent > 0)
            MpDebugLog::write("DurableSync: flushed %d pending event(s) on room ready", sent);
    }

    void reset() {
        ScopedLock lock;
        if (!lock.isAcquired())
            return;
        // Flush before detaching. Submits only mark the state dirty and leave
        // the write to update(), so leaving a room in the same frame as a
        // pickup would otherwise drop that pickup from the file - the one
        // record that is meant to survive exactly this.
        if (sAttached && sDirty.exchange(false, std::memory_order_acq_rel))
            saveLocked();
        for (s32 i = 0; i < cCapacity; i++)
            sEntries[i].live = false;
        sLiveCount = 0;
        sAttached = false;
        sAppliedRevision = 0;
        sMoonRockMask = 0;
        sMoonRockKeyMask = 0;
        sPaintingMask = 0;
        sSaveUnproven.store(false, std::memory_order_release);
        sRoomCode[0] = '\0';
        sLoadPending.store(false, std::memory_order_release);
        sDirty.store(false, std::memory_order_release);
    }

    s32 getPendingCount() { return sLiveCount; }
    s32 getRejectedCount() { return sRejectedCount; }
    const char* getLastRejectionText() { return sLastRejection; }

} // namespace DurableSync
