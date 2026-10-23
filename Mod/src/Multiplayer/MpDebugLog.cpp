#include "Multiplayer/MpDebugLog.h"

// The whole implementation is compiled only for a logging build. With
// SMOR_MP_DEBUG_LOG=0 the header supplies an empty inline write() instead, and
// defining the real one here as well would be a duplicate definition - so this
// guard is what makes the two halves mutually exclusive rather than optional.
#if SMOR_MP_DEBUG_LOG

#include "hk/diag/diag.h"

#include "nn/fs.h"
#include "nn/os.h"
#include "nn/util.h"

#include "SDRedirect/SDPaths.h"
#include <atomic>
#include <cstdio>
#include <cstring>

namespace MpDebugLog {

    namespace {
        constexpr const char* kDir = "sd:/SMO-Randomizer";
        // Legacy single-file path from before chunking - only touched to
        // delete a stale copy left by an older build, never written to.
        constexpr const char* kLegacyPath = "sd:/SMO-Randomizer/mp_debug.log";

        // nn::fs::SetFileSize isn't linkable in this build, so a real
        // "extend the file and write the new tail" append isn't available -
        // every write() has to rewrite the whole file it's writing to from
        // scratch (delete+create-at-exact-size+write+close), since
        // nn::fs::CreateFile doesn't zero-fill the space it reserves and
        // leaves garbage past whatever's actually written otherwise.
        //
        // The original version kept the ENTIRE session in one growing RAM
        // buffer and rewrote all of it on every single call - fine for a
        // handful of connect-milestone lines, but for a full co-op session
        // (confirmed: a real ~1hr session filled a 16KB buffer well before
        // anything else interesting happened) this is O(total session size)
        // per call, getting slower and slower as the session goes on, with
        // no way to log a full multi-hour session at all once the buffer's
        // full - it just silently stops.
        //
        // Fix: split the session into fixed-size CHUNK files
        // (mp_debug_NNNN.log). Each write() only ever rewrites the CURRENT
        // chunk (bounded cost, same as before), and once a chunk fills, it's
        // finalized (already written in full - nothing more to do) and a
        // fresh chunk starts. Total cost across an arbitrarily long session
        // stays O(total lines), not O(total lines squared) - this scales to
        // a 10+ hour session the same way it scales to 10 minutes. Chunks
        // are numbered so pulling every mp_debug_*.log off the SD card and
        // reading them in order reconstructs the full session; the
        // sequence number in each line (unlike sLength/sChunkIndex) is
        // NEVER reset between chunks, so ordering/continuity is unambiguous
        // even out of context.
        constexpr s32 kChunkCapacity = 16384;
        // Generous upper bound for how many old chunk files a previous
        // session could have left behind, purely for the fresh-boot cleanup
        // sweep below - deleting a nonexistent file is a harmless no-op, so
        // this only costs time proportional to however many really exist,
        // not this bound itself being reached.
        constexpr s32 kMaxChunkCleanup = 4096;

        bool sInitialized = false;
        char sBuffer[kChunkCapacity];
        s32 sLength = 0;
        u32 sSequence = 0;
        s32 sChunkIndex = 1;

        // write() is called from more than one thread (e.g. the main thread
        // via hook call sites, and ClientReadThread via updateShineInfo) -
        // confirmed the hard way: two threads racing CreateFile/DeleteFile/
        // WriteFile against the same chunk path aborted inside
        // nn::fs::WriteFile's internal FlushFile on ClientReadThread. All of
        // write()'s state above is shared and unsynchronized otherwise, so
        // every call needs to be serialized. sead::CriticalSection doesn't
        // link on this target (see TrackerBridge.cpp), so this is a plain
        // spinlock instead of an OS mutex.
        //
        // FIRST ATTEMPT at this used sead::Atomic<bool>, whose
        // compareExchange/store/load are hardcoded to
        // std::memory_order_relaxed - that gives atomicity to the lock flag
        // itself (only one thread can ever win the CAS), but relaxed
        // ordering does NOT establish a happens-before relationship, so the
        // next thread to acquire the lock is not guaranteed to see the
        // previous holder's writes to sBuffer/sLength/sChunkIndex in order -
        // on ARM's weak memory model that's a real gap, not a theoretical
        // one. Confirmed insufficient: the exact same abort reproduced on a
        // build containing that lock (verified via addr2line against that
        // build's own .nss - same file/line every time, not a stale-binary
        // fluke). Fixed by using a raw std::atomic<bool> directly with
        // explicit acquire (lock)/release (unlock) ordering, so every write
        // made inside the critical section is guaranteed visible to
        // whichever thread acquires the lock next. Also yields between spin
        // attempts so a losing thread doesn't burn its whole timeslice
        // busy-polling while the winner is off doing blocking SD I/O.
        std::atomic<bool> sLockHeld { false };

        // Bounded wait, ~500ms worst case (kAcquireMaxAttempts *
        // kAcquireRetryMs). Confirmed on real hardware this needed a hard
        // limit: a single call that got stuck inside flushCurrentChunk()'s
        // blocking nn::fs::WriteFile - genuinely slow SD I/O, cause
        // unconfirmed - held this lock indefinitely, and because the old
        // unbounded spin-wait had no way out, EVERY other thread that ever
        // called write() again for the rest of the session silently hung
        // forever waiting for a lock that was never coming back - a whole
        // session produced nothing past the very first boot-time line, and
        // the game itself froze solid the moment the main thread's own next
        // write() call (existing moon-credit logging) blocked on it too. A
        // logger's failure mode must never be able to freeze the game it's
        // diagnosing - give up and skip this one log line instead.
        constexpr s32 kAcquireMaxAttempts = 100;
        constexpr s32 kAcquireRetryMs = 5;

        class ScopedLock {
        public:
            ScopedLock() {
                bool expected = false;
                for (s32 attempts = 0; !sLockHeld.compare_exchange_weak(expected, true,
                         std::memory_order_acquire, std::memory_order_relaxed);
                     attempts++) {
                    expected = false;
                    if (attempts >= kAcquireMaxAttempts)
                        return; // mAcquired stays false - caller must check isAcquired()
                    nn::os::YieldThread();
                    nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(kAcquireRetryMs));
                }
                mAcquired = true;
            }
            ~ScopedLock() {
                if (mAcquired)
                    sLockHeld.store(false, std::memory_order_release);
            }
            bool isAcquired() const { return mAcquired; }

        private:
            bool mAcquired = false;
        };

        void formatChunkPath(char* out, size_t outSize, s32 chunkIndex) {
            nn::util::SNPrintf(out, outSize, "sd:/SMO-Randomizer/mp_debug_%04d.log", chunkIndex);
        }

        // Fresh chunks each boot - a stale log from a prior session would
        // just be noise when trying to read the tail after a fresh hang.
        void ensureFresh() {
            if (sInitialized)
                return;
            sInitialized = true;

            SDPaths::ensureRandomizerRoot();
            SDPaths::ensureArchipelagoRoot();
            nn::fs::DeleteFile(kLegacyPath);

            char path[64];
            for (s32 i = 1; i <= kMaxChunkCleanup; i++) {
                formatChunkPath(path, sizeof(path), i);
                nn::fs::DeleteFile(path);
            }

            sLength = 0;
            sChunkIndex = 1;
        }

        // Bounded retry count for the WriteFile step below - a real crash
        // report came back with a chunk file CreateFile'd at exactly the
        // right size but full of null bytes: the write silently failed for
        // an entire session and nothing noticed, since the Result was never
        // checked. Kept small (2, not the original 5) - retries only help a
        // FAST-failing WriteFile; they do nothing for a genuinely stuck/slow
        // one (which is what ScopedLock's own acquire timeout above exists
        // to contain), so there's no benefit to holding the lock longer here
        // chasing a case retries can't actually help with.
        constexpr s32 kWriteRetries = 2;

        // Rewrites the current chunk in full (delete+create-at-exact-size+
        // write+close) - the only I/O write() ever does. Bounded by
        // kChunkCapacity regardless of how long the session has run.
        void flushCurrentChunk() {
            char path[64];
            formatChunkPath(path, sizeof(path), sChunkIndex);

            nn::fs::DeleteFile(path);
            if (nn::fs::CreateFile(path, sLength).IsFailure())
                return;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsFailure())
                return;

            bool wroteOk = false;
            for (s32 attempt = 0; attempt < kWriteRetries; attempt++) {
                if (nn::fs::WriteFile(handle, 0, sBuffer, sLength,
                        nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                        .IsSuccess()) {
                    wroteOk = true;
                    break;
                }
                nn::os::YieldThread();
                nn::os::SleepThread(nn::TimeSpan::FromMilliSeconds(10));
            }
            // Can't persist this (the write that would carry it is exactly
            // what's failing), but it at least reaches a live debug console -
            // a real crash report once came back as a chunk file the right
            // size but full of null bytes, with nothing anywhere hinting the
            // write had silently failed all session.
            if (!wroteOk)
                hk::diag::logLine("MpDebugLog: WriteFile failed after %d retries, chunk=%d len=%d",
                    kWriteRetries, sChunkIndex, sLength);

            nn::fs::CloseFile(handle);
        }
    } // namespace

    void write(const char* fmt, ...) {
        ScopedLock lock;
        if (!lock.isAcquired())
            return; // another call is stuck holding this - skip rather than pile up behind it

        ensureFresh();

        char msg[192];
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(msg, sizeof(msg), fmt, args);
        va_end(args);

        char line[224];
        const s32 lineLen = nn::util::SNPrintf(line, sizeof(line), "[#%06u] %s\n", ++sSequence, msg);
        if (lineLen <= 0)
            return;

        if (sLength + lineLen >= kChunkCapacity) {
            // Current chunk is full and already fully written (every call
            // flushes) - it needs no more work. Start the next one instead
            // of dropping this line, so an arbitrarily long session just
            // keeps producing chunks rather than silently going quiet once
            // one buffer fills.
            sChunkIndex++;
            sLength = 0;
        }

        std::memcpy(sBuffer + sLength, line, lineLen);
        sLength += lineLen;

        flushCurrentChunk();
    }

} // namespace MpDebugLog

#endif // SMOR_MP_DEBUG_LOG
