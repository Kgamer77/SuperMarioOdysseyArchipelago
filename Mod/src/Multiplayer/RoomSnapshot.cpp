#include "Multiplayer/RoomSnapshot.h"

#include "Library/Base/StringUtil.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/algorithms/crc32.h"
#include "Multiplayer/packets/Packet.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/sync/DurableSync.h"

#include "Randomizer/Randomizer.h"
#include "SDRedirect/SDPaths.h"

#include "hk/diag/diag.h"

#include "nn/fs.h"
#include "nn/os.h"
#include "nn/util.h"

#include "sead/thread/seadAtomic.h"

#include <cstring>

namespace RoomSnapshot {
    namespace {
        using MultiplayerProtocol::SnapshotArtifactType;

        constexpr s32 cChunkSize = MultiplayerProtocol::cSnapshotChunkSize;
        // The server's own ceiling (Settings.Server.MaxRoomSnapshotBytes) is
        // 4 MB; refusing oversized input here means a doomed transfer is
        // reported on the row that started it instead of dying mid-stream.
        constexpr u32 cMaxTotalBytes = 4u * 1024u * 1024u;
        // RoomManager.BeginUpload rejects a file count outside 1..5.
        constexpr s32 cMaxArtifacts = 5;
        // The send queue holds 100 packets and DROPS (does not block) when
        // full, so a 4 KB-at-a-time upload has to pace itself against it -
        // see waitForSendQueue(). Kept far below that 100 because the queue
        // holds the packets themselves, and gHeap is only 512 KB in total: a
        // dozen queued SnapshotChunks is already ~50 KB of it.
        constexpr u32 cSendQueueHighWater = 12;
        constexpr u32 cSendQueueLowWater = 4;
        // ONE budget for every millisecond upload() is allowed to stall the
        // caller, shared between draining the send queue and waiting for the
        // commit reply. It has to be a single total, not a per-wait timeout:
        // upload() runs on the main thread, and a per-chunk timeout of even a
        // second would let a stalled socket freeze the game for a minute
        // across a hundred chunks - which is indistinguishable from a hang.
        // Blowing the budget just abandons the upload; the host plays on and
        // the room stays joinable-later rather than the game dying.
        constexpr s32 cUploadBudgetMs = 8000;
        constexpr s32 cUploadSliceMs = 4;
        s32 sUploadBudgetLeftMs = 0;

        // Sleeps against the shared budget. False once it is spent.
        bool spendUploadBudget() {
            if (sUploadBudgetLeftMs <= 0)
                return false;
            sUploadBudgetLeftMs -= cUploadSliceMs;
            nn::os::YieldThread();
            nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(cUploadSliceMs));
            return true;
        }

        enum class Mode : u8
        {
            None,
            Upload,
            Download,
            Replay,
        };

        // Written by whichever side is running, read by the UI thread.
        sead::Atomic<s32> sState { (s32)State::Idle };
        sead::Atomic<s32> sProgress { 0 };

        // volatile because both threads touch it: upload() arms it from the
        // caller's thread and the receive thread clears it when the commit
        // lands, and the handlers gate on it. Not a lock - the two sides never
        // run a transfer at once - just a guarantee the compiler keeps
        // re-reading it.
        volatile Mode sMode = Mode::None;
        s32 sSlot = -1;
        u64 sTransferId[2] = {};
        // Upload only: set by onCommit() on the receive thread, polled by
        // upload() on the caller's thread. 0 pending, 1 accepted, -1 rejected.
        sead::Atomic<s32> sCommitResult { 0 };
        char sMessage[MultiplayerProtocol::cMessageSize] = "Not connected.";

        // -- incoming transfer -----------------------------------------------
        u32 sExpectedTotal = 0;
        u32 sReceivedTotal = 0;
        s32 sExpectedFiles = 0;
        s32 sFinishedFiles = 0;
        bool sFileOpen = false;
        bool sFileDiscarded = false; // artifact type this build has no reader for
        nn::fs::FileHandle sFileHandle = {};
        s32 sFileIndex = -1;
        u32 sFileSize = 0;
        u32 sFileWritten = 0;
        u32 sFileCrcExpected = 0;
        u32 sFileCrcRunning = 0;
        al::StringTmp<160> sFilePath { "" };

        void setState(State state) { sState.store((s32)state); }

        void setMessage(const char* text) {
            if (!text)
                text = "";
            std::strncpy(sMessage, text, sizeof(sMessage) - 1);
            sMessage[sizeof(sMessage) - 1] = '\0';
        }

        const char* categoryRoot(SaveRedirect::Category category) {
            switch (category) {
            case SaveRedirect::Category::Multiplayer:
                return "MPSaves";
            case SaveRedirect::Category::MultiplayerGuest:
                return "MPSaves/_guest";
            default:
                return "Saves";
            }
        }

        al::StringTmp<160> makeSlotPath(
            s32 slot, SaveRedirect::Category category, const char* name) {
            return al::StringTmp<160>(
                "sd:/SMO-Randomizer/%s/Slot%d/%s", categoryRoot(category), slot, name);
        }

        void ensureSlotDir(s32 slot, SaveRedirect::Category category) {
            SDPaths::ensureRandomizerRoot();
            // CreateDirectory is mkdir, not mkdir -p (see SDPaths.h), and the
            // guest root is two levels deep - MPSaves has to exist before
            // MPSaves/_guest can.
            nn::fs::CreateDirectory("sd:/SMO-Randomizer/MPSaves");
            al::StringTmp<128> root = { "sd:/SMO-Randomizer/%s", categoryRoot(category) };
            nn::fs::CreateDirectory(root.cstr());
            al::StringTmp<160> dir
                = { "sd:/SMO-Randomizer/%s/Slot%d", categoryRoot(category), slot };
            nn::fs::CreateDirectory(dir.cstr());
        }

        u32 crcUpdate(u32 crc, const u8* data, size_t size) {
            for (size_t i = 0; i < size; i++)
                crc = (crc >> 8) ^ crc32::s_Table[(crc ^ data[i]) & 0xFF];
            return crc;
        }

        bool getFileSize(const char* path, u32* out) {
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;
            s64 size = 0;
            const bool ok = nn::fs::GetFileSize(&size, handle).IsSuccess() && size > 0;
            nn::fs::CloseFile(handle);
            if (!ok)
                return false;
            *out = (u32)size;
            return true;
        }

        void makeTransferId(u64 out[2]) {
            // The server only ever echoes this back, so it just has to be
            // distinct between transfers rather than a real GUID.
            const u64 tick = nn::os::GetSystemTick().value;
            out[0] = tick;
            out[1] = tick * 0x9E3779B97F4A7C15ull + 0x632BE59BD9B4E019ull;
        }

        bool isOurTransfer(const u64 id[2]) {
            return id[0] == sTransferId[0] && id[1] == sTransferId[1];
        }

        // Filename each artifact type lands on inside the guest's slot
        // directory. Anything this build has no reader for is consumed and
        // dropped rather than refused - the transfer as a whole still has to
        // finish for the artifacts that do matter.
        const char* artifactFileName(SnapshotArtifactType type) {
            switch (type) {
            case SnapshotArtifactType::RandoSave:
                return "rando.bin";
            // Deliberately dropped rather than written. A guest builds its save
            // from the seed (see exeJoinRoom), so the host's File1.bin has no
            // reader on this side - writing 2 MB to the SD card for something
            // nothing opens is pure cost. A current host does not send one at
            // all; this is what keeps an older host, which still does, working:
            // the bytes are consumed and CRC-checked like any other artifact,
            // just never landed on disk.
            case SnapshotArtifactType::NativeSave:
                return nullptr;
            case SnapshotArtifactType::BingoBoard:
                return "board.bin";
            case SnapshotArtifactType::ArrivalLog:
                return "arrivals.bin";
            default:
                return nullptr;
            }
        }

        void closeIncomingFile() {
            if (sFileOpen) {
                nn::fs::CloseFile(sFileHandle);
                sFileOpen = false;
            }
        }

        void failDownload(const char* reason) {
            closeIncomingFile();
            sMode = Mode::None;
            setMessage(reason);
            setState(State::DownloadFailed);
            MpDebugLog::write("RoomSnapshot: download failed - %s", reason);
        }

        // -- upload ----------------------------------------------------------

        struct OutgoingArtifact {
            SnapshotArtifactType type = SnapshotArtifactType::RandoSave;
            al::StringTmp<160> path { "" };
            u32 size = 0;
            u32 crc = 0;
        };

        // Keeps the 4 KB-per-packet upload from overrunning the 100-slot send
        // queue, which drops rather than blocks once it is full.
        bool waitForSendQueue() {
            while (Client::getSendQueueDepth() >= cSendQueueHighWater) {
                if (!Client::isSocketActive())
                    return false;
                if (!spendUploadBudget())
                    return false;
                if (Client::getSendQueueDepth() <= cSendQueueLowWater)
                    return true;
            }
            return true;
        }

        bool hashArtifact(OutgoingArtifact& artifact) {
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, artifact.path.cstr(), nn::fs::OpenMode_Read).IsFailure())
                return false;

            static u8 buffer[cChunkSize];
            u32 crc = 0xFFFFFFFF;
            bool ok = true;
            for (u32 offset = 0; offset < artifact.size; offset += cChunkSize) {
                const u32 length
                    = artifact.size - offset < cChunkSize ? artifact.size - offset : cChunkSize;
                if (nn::fs::ReadFile(handle, offset, buffer, length).IsFailure()) {
                    ok = false;
                    break;
                }
                crc = crcUpdate(crc, buffer, length);
            }
            nn::fs::CloseFile(handle);
            if (!ok)
                return false;
            artifact.crc = crc ^ 0xFFFFFFFF;
            return true;
        }

        bool sendArtifact(const OutgoingArtifact& artifact, u8 index, u32 sentBefore, u32 total) {
            auto declare = new (gHeap) SnapshotFile;
            declare->mUserID = Client::getClientId();
            declare->transferId[0] = sTransferId[0];
            declare->transferId[1] = sTransferId[1];
            declare->fileIndex = index;
            declare->artifactType = artifact.type;
            declare->fileSize = artifact.size;
            declare->crc32 = artifact.crc;
            if (!Client::queueRoomPacket(declare))
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, artifact.path.cstr(), nn::fs::OpenMode_Read).IsFailure())
                return false;

            bool ok = true;
            for (u32 offset = 0; offset < artifact.size; offset += cChunkSize) {
                const u32 length
                    = artifact.size - offset < cChunkSize ? artifact.size - offset : cChunkSize;

                if (!waitForSendQueue()) {
                    ok = false;
                    break;
                }

                auto chunk = new (gHeap) SnapshotChunk;
                chunk->mUserID = Client::getClientId();
                chunk->transferId[0] = sTransferId[0];
                chunk->transferId[1] = sTransferId[1];
                chunk->fileIndex = index;
                chunk->offset = offset;
                chunk->dataLength = (u16)length;
                if (nn::fs::ReadFile(handle, offset, chunk->bytes, length).IsFailure()) {
                    delete chunk;
                    ok = false;
                    break;
                }
                if (!Client::queueRoomPacket(chunk)) {
                    ok = false;
                    break;
                }

                sProgress.store((s32)(((u64)(sentBefore + offset + length) * 100) / total));
            }

            nn::fs::CloseFile(handle);
            return ok;
        }
    } // namespace

    State getState() {
        return (State)sState.load();
    }

    const char* getMessage() {
        return sMessage;
    }

    s32 getProgressPercent() {
        const s32 progress = sProgress.load();
        return progress < 0 ? 0 : (progress > 100 ? 100 : progress);
    }

    s32 getDownloadSlot() {
        return sSlot;
    }

    // ========================================================================
    // Setup owner
    // ========================================================================

    bool upload(s32 slot, SaveRedirect::Category category) {
        if (!Client::isSocketActive() || !Client::isSetupOwner()) {
            // Not an error: a solo run, or a room whose seed is already up.
            MpDebugLog::write("RoomSnapshot::upload: skipped (active=%d owner=%d)",
                Client::isSocketActive() ? 1 : 0, Client::isSetupOwner() ? 1 : 0);
            return false;
        }

        // The host picks a team like everyone else, and its own progress has to
        // land in that team's bucket rather than the default one. Sent before
        // the upload so it is settled by the time the room goes Ready.
        Client::sendTeamSelect();

        OutgoingArtifact artifacts[cMaxArtifacts];
        s32 count = 0;

        // rando.bin is the whole room save. The native save (File1.bin) used to
        // go with it, for a guest to load directly - but the game will not
        // populate a GameDataFile from a save file it did not write itself, so
        // a guest builds its own from the seed instead (see exeJoinRoom). That
        // made File1.bin ~2 MB of dead weight on every join: read off the SD
        // card here, streamed 4 KB at a time through a main-thread upload
        // budget, written back out on the far side, and then never read.
        //
        // board.bin only exists for a board run.
        artifacts[count].type = SnapshotArtifactType::RandoSave;
        artifacts[count].path = makeSlotPath(slot, category, "rando.bin");
        if (!getFileSize(artifacts[count].path.cstr(), &artifacts[count].size)) {
            setMessage("Room save is missing rando.bin.");
            setState(State::UploadFailed);
            return false;
        }
        count++;

        artifacts[count].type = SnapshotArtifactType::BingoBoard;
        artifacts[count].path = makeSlotPath(slot, category, "board.bin");
        if (getFileSize(artifacts[count].path.cstr(), &artifacts[count].size))
            count++;

        u32 total = 0;
        for (s32 i = 0; i < count; i++) {
            if (!hashArtifact(artifacts[i])) {
                setMessage("Could not read the room save.");
                setState(State::UploadFailed);
                return false;
            }
            total += artifacts[i].size;
        }

        if (total == 0 || total > cMaxTotalBytes) {
            setMessage("Room save is too large to share.");
            setState(State::UploadFailed);
            return false;
        }

        sMode = Mode::Upload;
        sSlot = slot;
        makeTransferId(sTransferId);
        sCommitResult.store(0);
        sProgress.store(0);
        sUploadBudgetLeftMs = cUploadBudgetMs;
        setMessage("Uploading room save...");
        setState(State::Uploading);
        // Logged before the first blocking call, so a log that ends here says
        // "froze inside the upload" rather than leaving it ambiguous.
        MpDebugLog::write("RoomSnapshot::upload: begin files=%d bytes=%u budget=%dms",
            count, total, cUploadBudgetMs);

        auto begin = new (gHeap) SnapshotBegin;
        begin->mUserID = Client::getClientId();
        begin->transferId[0] = sTransferId[0];
        begin->transferId[1] = sTransferId[1];
        begin->seedFingerprint = ((u64)Randomizer::getSeed() << 32) | (u64)total;
        begin->totalSize = total;
        begin->fileCount = (u8)count;
        // CommitUpload assigns room.Flags straight from this, so sending 0
        // would silently clear the death link or cross-team sync that the
        // Multiplayer page had already turned on through RoomSettings.
        begin->flags = 0;
        if (Randomizer::isDeathLinkEnabled())
            begin->flags |= (u8)MultiplayerProtocol::FlagDeathLink;
        if (Randomizer::isSyncOtherTeamsEnabled())
            begin->flags |= (u8)MultiplayerProtocol::FlagSyncOtherTeams;
        begin->baseRevision = 0;
        if (!Client::queueRoomPacket(begin)) {
            setMessage("Could not start the upload.");
            setState(State::UploadFailed);
            sMode = Mode::None;
            return false;
        }

        u32 sent = 0;
        for (s32 i = 0; i < count; i++) {
            if (!sendArtifact(artifacts[i], (u8)i, sent, total)) {
                setMessage("Upload was interrupted.");
                setState(State::UploadFailed);
                sMode = Mode::None;
                return false;
            }
            sent += artifacts[i].size;
        }

        auto commit = new (gHeap) SnapshotCommit;
        commit->mUserID = Client::getClientId();
        commit->transferId[0] = sTransferId[0];
        commit->transferId[1] = sTransferId[1];
        commit->accepted = 1;
        commit->revision = 0;
        if (!Client::queueRoomPacket(commit)) {
            setMessage("Could not finish the upload.");
            setState(State::UploadFailed);
            sMode = Mode::None;
            return false;
        }

        // The server answers every commit, accepted or not, so this waits for
        // a real reply rather than assuming success - a rejected snapshot
        // leaves the room in Configuring, which a guest would otherwise sit in
        // forever with no idea why. Off the same budget as the chunk pacing
        // above, so the two together cannot exceed cUploadBudgetMs.
        while (sCommitResult.load() == 0 && Client::isSocketActive()) {
            if (!spendUploadBudget())
                break;
        }

        sMode = Mode::None;
        const s32 result = sCommitResult.load();
        if (result != 1) {
            if (result == 0)
                setMessage("Timed out uploading the room save.");
            setState(State::UploadFailed);
            MpDebugLog::write("RoomSnapshot::upload: failed result=%d msg=%s", result, sMessage);
            return false;
        }

        sProgress.store(100);
        setState(State::UploadDone);
        MpDebugLog::write("RoomSnapshot::upload: accepted files=%d bytes=%u", count, total);
        return true;
    }

    // ========================================================================
    // Guest
    // ========================================================================

    bool beginDownload(s32 slot) {
        if (!Client::isSocketActive() || !Client::isRoomAssigned()) {
            setMessage("Not connected to a room.");
            setState(State::DownloadFailed);
            return false;
        }

        closeIncomingFile();

        // Whatever a previous join left behind is not this room's seed.
        SaveRedirect::deleteGuestTemp();
        ensureSlotDir(slot, SaveRedirect::Category::MultiplayerGuest);

        sMode = Mode::Download;
        sSlot = slot;
        sExpectedTotal = 0;
        sReceivedTotal = 0;
        sExpectedFiles = 0;
        sFinishedFiles = 0;
        sFileIndex = -1;
        sProgress.store(0);
        setMessage("Asking the host for the seed...");
        setState(State::Waiting);
        retryDownload();
        return true;
    }

    void retryDownload() {
        if (sMode != Mode::Download || !Client::isSocketActive())
            return;

        // Team first, every time, and not just on the first attempt: the
        // server picks the event log to send by the requester's team, so a
        // request that overtakes the team selection would replay whatever team
        // this player was last on. Both go through the same send queue, so
        // queueing the team here guarantees the ordering.
        Client::sendTeamSelect();

        auto request = new (gHeap) SnapshotRequest;
        request->mUserID = Client::getClientId();
        request->afterRevision = 0;
        Client::queueRoomPacket(request);
    }

    bool beginReplay() {
        if (!Client::isSocketActive() || getState() != State::DownloadDone)
            return false;

        auto request = new (gHeap) SnapshotRequest;
        request->mUserID = Client::getClientId();
        // Reserved apply-ready acknowledgement: ordinary snapshot requests
        // always use a real revision (currently zero for a freshly generated
        // guest save), so UINT64_MAX cannot collide with one.
        request->afterRevision = ~0ULL;
        // Arm the receive-side routing before handing the packet to the send
        // thread. A local server can answer quickly enough for the completion
        // marker to arrive before queueRoomPacket returns.
        sMode = Mode::Replay;
        setMessage("Applying room progress...");
        setState(State::Replaying);
        if (!Client::queueRoomPacket(request)) {
            sMode = Mode::None;
            setMessage("Could not request room progress.");
            setState(State::ReplayFailed);
            return false;
        }
        return true;
    }

    void cancel() {
        closeIncomingFile();
        sMode = Mode::None;
        sProgress.store(0);
        setState(State::Idle);
    }

    // ========================================================================
    // Packet handlers - SocketClient receive thread (see the header)
    // ========================================================================

    static void onBegin(SnapshotBegin* packet) {
        if (!packet || sMode != Mode::Download)
            return;

        closeIncomingFile();
        sTransferId[0] = packet->transferId[0];
        sTransferId[1] = packet->transferId[1];
        sExpectedTotal = packet->totalSize;
        sReceivedTotal = 0;
        sExpectedFiles = packet->fileCount;
        sFinishedFiles = 0;
        sFileIndex = -1;
        sProgress.store(0);

        if (sExpectedTotal == 0 || sExpectedTotal > cMaxTotalBytes) {
            failDownload("The room save is not a valid size.");
            return;
        }

        setMessage("Downloading the room save...");
        setState(State::Downloading);
        MpDebugLog::write(
            "RoomSnapshot: download begin files=%d bytes=%u", sExpectedFiles, sExpectedTotal);
    }

    static void onFile(SnapshotFile* packet) {
        if (!packet || sMode != Mode::Download || getState() != State::Downloading)
            return;
        if (!isOurTransfer(packet->transferId))
            return;

        // Each artifact arrives complete before the next is declared, so a
        // still-open handle here means the previous one was short.
        if (sFileOpen && sFileWritten != sFileSize) {
            failDownload("The room save arrived incomplete.");
            return;
        }
        closeIncomingFile();

        sFileIndex = packet->fileIndex;
        sFileSize = packet->fileSize;
        sFileWritten = 0;
        sFileCrcExpected = packet->crc32;
        sFileCrcRunning = 0xFFFFFFFF;

        const char* name = artifactFileName(packet->artifactType);
        sFileDiscarded = name == nullptr;
        if (sFileDiscarded)
            return;

        sFilePath = makeSlotPath(sSlot, SaveRedirect::Category::MultiplayerGuest, name);
        // CreateFile fails on an existing path, and there is no append: the
        // file is created at its final size up front and every chunk is
        // written at its own offset. Same constraint MpDebugLog documents -
        // nn::fs::SetFileSize is not linkable in this build.
        nn::fs::DeleteFile(sFilePath.cstr());
        if (nn::fs::CreateFile(sFilePath.cstr(), sFileSize).IsFailure()) {
            failDownload("Could not write to the SD card.");
            return;
        }
        if (nn::fs::OpenFile(&sFileHandle, sFilePath.cstr(), nn::fs::OpenMode_Write).IsFailure()) {
            failDownload("Could not write to the SD card.");
            return;
        }
        sFileOpen = true;
    }

    static void onChunk(SnapshotChunk* packet) {
        if (!packet || sMode != Mode::Download || getState() != State::Downloading)
            return;
        if (!isOurTransfer(packet->transferId) || packet->fileIndex != sFileIndex)
            return;

        // The server streams strictly sequentially. A gap means a packet was
        // dropped (the recv queue drops rather than blocks when full), and a
        // silently short save file is far worse than a reported failure.
        if (packet->offset != sFileWritten
            || (u64)packet->offset + packet->dataLength > sFileSize) {
            failDownload("The room save arrived out of order.");
            return;
        }

        if (!sFileDiscarded) {
            const auto flush = nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush);
            if (!sFileOpen
                || nn::fs::WriteFile(
                    sFileHandle, packet->offset, packet->bytes, packet->dataLength, flush)
                       .IsFailure()) {
                failDownload("Could not write to the SD card.");
                return;
            }
        }

        sFileCrcRunning = crcUpdate(sFileCrcRunning, packet->bytes, packet->dataLength);
        sFileWritten += packet->dataLength;
        sReceivedTotal += packet->dataLength;
        if (sExpectedTotal)
            sProgress.store((s32)(((u64)sReceivedTotal * 100) / sExpectedTotal));

        if (sFileWritten == sFileSize) {
            closeIncomingFile();
            if ((sFileCrcRunning ^ 0xFFFFFFFF) != sFileCrcExpected) {
                failDownload("The room save failed its integrity check.");
                return;
            }
            sFinishedFiles++;
        }
    }

    static void onCommit(SnapshotCommit* packet) {
        if (!packet)
            return;

        if (sMode == Mode::Upload) {
            if (!isOurTransfer(packet->transferId))
                return;
            setMessage(packet->message.cstr());
            sCommitResult.store(packet->accepted ? 1 : -1);
            return;
        }

        if (sMode == Mode::Replay) {
            const bool emptyTransfer = packet->transferId[0] == 0 && packet->transferId[1] == 0;
            if (!isOurTransfer(packet->transferId) && (packet->accepted || !emptyTransfer))
                return;
            // The replay just delivered this team's whole log in revision order
            // and named the revision it ended at, so the applied watermark can
            // safely jump straight to it. This is the ONE place adopting a
            // revision wholesale is correct: everywhere else the watermark has
            // to advance one event at a time, or a client that has applied
            // nothing would claim to have seen everything - the reconnect bug
            // documented in Client::updateRoomStatus.
            if (packet->accepted && packet->revision > 0)
                DurableSync::setAppliedRevision(packet->revision);
            sMode = Mode::None;
            setMessage(packet->message.isEmpty()
                    ? (packet->accepted ? "Room progress applied."
                                        : "Could not apply room progress.")
                    : packet->message.cstr());
            setState(packet->accepted ? State::ReplayDone : State::ReplayFailed);
            return;
        }

        if (sMode != Mode::Download)
            return;

        if (!packet->accepted) {
            // A rejection with no transfer behind it is the server saying the
            // owner has not uploaded a seed yet (DownloadSnapshot's
            // Guid.Empty rejection). That is a wait, not a failure.
            if (getState() != State::Downloading) {
                setMessage(packet->message.isEmpty()
                        ? "Waiting for the host to finish setting up..."
                        : packet->message.cstr());
                setState(State::Waiting);
                return;
            }
            failDownload(packet->message.isEmpty() ? "The host rejected the transfer."
                                                   : packet->message.cstr());
            return;
        }

        if (getState() != State::Downloading || !isOurTransfer(packet->transferId))
            return;

        closeIncomingFile();
        if (sFinishedFiles != sExpectedFiles || sReceivedTotal != sExpectedTotal) {
            failDownload("The room save arrived incomplete.");
            return;
        }

        sMode = Mode::None;
        sProgress.store(100);
        setMessage("Room save downloaded.");
        setState(State::DownloadDone);
        MpDebugLog::write(
            "RoomSnapshot: download complete files=%d bytes=%u", sFinishedFiles, sReceivedTotal);
    }

    bool tryHandlePacket(Packet* packet) {
        if (!packet)
            return false;

        switch (packet->mType) {
        case PacketType::SNAPSHOTBEGIN:
            onBegin(static_cast<SnapshotBegin*>(packet));
            return true;
        case PacketType::SNAPSHOTFILE:
            onFile(static_cast<SnapshotFile*>(packet));
            return true;
        case PacketType::SNAPSHOTCHUNK:
            onChunk(static_cast<SnapshotChunk*>(packet));
            return true;
        case PacketType::SNAPSHOTCOMMIT:
            // Replay completion deliberately goes through Client::readFunc's
            // normal queue. The SharedEvents sent before it are in that same
            // FIFO, so ReplayDone cannot become visible to the file select
            // until every event has actually been applied to GameDataFile.
            if (sMode == Mode::Replay)
                return false;
            onCommit(static_cast<SnapshotCommit*>(packet));
            return true;
        case PacketType::SNAPSHOTREQUEST:
            // Client -> server only. Swallowed rather than queued so it cannot
            // reach readFunc()'s "Discarding Unknown Packet Type" line if a
            // server ever echoes one back.
            return true;
        default:
            return false;
        }
    }

    void handleQueuedReplayCommit(Packet* packet) {
        if (packet && packet->mType == PacketType::SNAPSHOTCOMMIT && sMode == Mode::Replay)
            onCommit(static_cast<SnapshotCommit*>(packet));
    }

} // namespace RoomSnapshot
