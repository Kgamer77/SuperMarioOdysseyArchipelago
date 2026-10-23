#pragma once

// A tiny, deliberately separate logger from Randomizer::logPush(): that one
// is a RAM-only ring buffer, lost the moment the process hangs or is force-
// closed with no crash report generated (exactly the "frozen but doesn't
// crash" case this exists for). Every call here opens a file, writes one
// flushed line, and closes again - slower than the RAM logger, so only use
// it on milestones that matter for diagnosing a silent hang or a real
// multiplayer session, never in a true per-frame hot path (confirmed this
// session: logging on every isOpenShineName call, which fires per row per
// frame while a list is visible, changed timing enough to make an unrelated
// bug reproduce more reliably - throttle any call site anywhere near that
// frequency the same way Client::update()'s heartbeat does).
//
// Output: sd:/SMO-Randomizer/mp_debug_NNNN.log, one chunk per ~16KB of log
// text - a long session (a multi-hour co-op playthrough) rotates into a new
// chunk instead of silently going quiet once one fills, and each write()
// only ever rewrites its own current chunk, not the whole session, so cost
// stays bounded no matter how long the session runs. All chunks from a
// previous boot are wiped at the first write() of a new one, so pull every
// mp_debug_*.log off the SD card after a session and read them in filename
// order (the [#NNNNNN] sequence number inside each line is never reset
// between chunks, so ordering/continuity is unambiguous even if only one
// chunk gets pulled out of context).
//
// write() is called from multiple threads (main thread hook call sites, and
// ClientReadThread via updateShineInfo) and is internally serialized with a
// spinlock, so it's safe to call from anywhere without external locking.
//
// That lock's acquire has a bounded ~500ms timeout - write() silently no-ops
// (skips that one line) rather than blocking forever if it can't get in.
// This is load-bearing, not just defensive: confirmed on real hardware that
// a single call stuck inside a slow/hung nn::fs::WriteFile (root cause of
// the slowness itself unconfirmed) held an earlier, unbounded version of
// this lock indefinitely - every OTHER thread's write() call for the rest
// of the session hung right along with it, and the game itself froze solid
// the moment the MAIN thread's own next write() blocked on it too. A
// logger's own failure mode must never be able to freeze the game it exists
// to diagnose - losing a line is an acceptable cost the game itself never
// notices; hanging every caller forever is not.
// ===== Log-off builds =====
//
// SMOR_MP_DEBUG_LOG=0 makes write() an empty inline, so all ~215 call sites
// compile away and the SD writes, the ~16KB chunk buffer and the format
// strings all go with them. Built with `make logoff`, which is what a build
// handed to players or streamers should be: the logger opens, writes and
// closes a file per line, and the header above documents it holding a lock
// with a 500ms timeout - fine while diagnosing, not something to inflict on
// someone just playing.
//
// An empty INLINE FUNCTION rather than a macro, deliberately. A macro that
// swallowed its arguments would leave any variable used only for logging
// looking unused, and this file has plenty of those - the arguments here are
// still named and still type-checked, and anything with a side effect is still
// evaluated, so a log-off build cannot diverge in behaviour from a log-on one.
#ifndef SMOR_MP_DEBUG_LOG
#define SMOR_MP_DEBUG_LOG 1
#endif

namespace MpDebugLog {
#if SMOR_MP_DEBUG_LOG
    void write(const char* fmt, ...);
#else
    inline void write(const char*, ...) { }
#endif
} // namespace MpDebugLog
