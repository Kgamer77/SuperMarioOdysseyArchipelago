#pragma once

#include <basis/seadTypes.h>

#include "SD/SDHooks.h"

namespace TrackerBridge {

    // Kept well under sRoomCode's 64-byte buffer and MAX_HOSTNAME_LENGTH (that
    // constant is shared with Multiplayer's IP/hostname entry, unrelated to
    // this) - a generated web-tracker room code is 12 characters, and this
    // value is also displayed appended to the Current Seed row on the
    // Randomizer Overview page, which has no room for a long string before
    // it overlaps the next column.
    constexpr s32 kRoomCodeMaxLength = 16;

    // Creates TrackerBridge's own small dedicated heap (see TrackerBridge.cpp
    // for why), as a child of al::getStationedHeap(). Must be called exactly
    // once, early, from a context that's actually guaranteed to have a valid
    // heap - ridden along on Multiplayer's existing al::SystemKit::
    // createMemorySystem hook in Multiplayer/hooks/initHooks.cpp rather than
    // installing a second independent hook on that same symbol (see the
    // one-trampoline-per-function invariant documented in Multiplayer/main.cpp).
    // Safe to call again (no-op) if it already succeeded.
    void initHeap();

    // Sends a live "this loading zone was just used, and led here" event
    // straight to Firestore over HTTPS - Phase 1 of a live auto-tracking
    // feature for the companion tracker web app. No local relay process
    // needed; resolved browser-side by firebase-live-sync.js against the
    // room code below. Runs on a small background upload thread pool (see
    // TrackerBridge.cpp) so a slow/real-internet TLS handshake never blocks
    // this hook's caller; if every slot is already busy, the event is
    // silently dropped rather than blocking or queuing.
    //
    // exitId/fromStage/toStage should be the resolved values *after* entrance-rando remapping
    // (i.e. what the transition actually ended up being), not the vanilla/pre-remap ones.
    //
    // debugArrivalDoorId is a temporary diagnostic field (raw patched.mChangeStageId, before
    // any resolution) - lets the bridge log show what the game actually computed, to debug
    // why arrival-side granularity resolution isn't matching expectations. Remove once fixed.
    void notifyTransition(
        const char* exitId, const char* fromStage, const char* toStage,
        const char* debugArrivalDoorId = nullptr);

    // Call once per frame (see DebugMenuHooks.cpp's drawMain hook). Reads the
    // current save's moon counts (per kingdom), capture unlocks, and ability
    // unlocks, and - only if any of that changed since the last call - sends
    // a snapshot straight to Firestore, same transport as notifyTransition.
    // Unlike a transition, a dropped/late snapshot is harmless: the next
    // frame that actually changes something sends a fresh one, so there's no
    // queue-and-replay concern here the way there is for door events.
    void updateProgress();

    // The last progress snapshot strings updateProgress() actually sent, for
    // TrackerBridgeDebugOverlay - see the format comments in TrackerBridge.cpp
    // (checkAndSendProgress(), CAPTURE_ORDER/ABILITY_ORDER on the web side)
    // for what each character position means. Empty until the first send.
    const char* getLastMoonsDebugText();
    const char* getLastMoonReqsDebugText();
    const char* getLastCapturesDebugText();
    const char* getLastAbilitiesDebugText();
    const char* getLastKingdomStatusDebugText();

    // Configured from the "Tracker Live Testing" Mod Config menu entry (or the
    // Web Tracker row on the save-file creation wizard). Falls back to a
    // hardcoded default (see TrackerBridge.cpp) until set.
    void setRoomCode(const char* roomCode);
    const char* getRoomCode();
    // getRoomCode(), but "(not set)" instead of an empty string - for menu
    // rows that show it as "Room Code: %s" (an empty %s just leaves a bare
    // trailing label, which reads as a rendering bug rather than "unset").
    const char* getRoomCodeDisplayText();

    // Persists the current room code to a small per-slot file on the SD card
    // (sd:/SMO-Randomizer/Saves/Slot<N>/tracker_room_code.bin), same pattern
    // BingoBoard.cpp already uses for its own per-slot data - NOT the packed
    // RandoSaveFile::Header, which has no spare bytes left (see the call site
    // comments in SeedGenerator.cpp/StageSceneStateFileSelect.cpp for why).
    // Call once, right after a new save's RandoSaveFile::write() succeeds.
    void saveRoomCodeToSlot(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    // Restores the room code from that same file into the in-memory buffer
    // getRoomCode() reads. Call right after RandoSaveFile::loadAndApply()
    // succeeds for an existing save. A missing file (old save, or one that
    // never had a room code set) leaves the current in-memory value alone.
    void loadRoomCodeFromSlot(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());

    // Human-readable outcome of the most recent (or in-progress) cloud
    // upload attempt, for display in the Tracker Live Testing menu - "Not
    // attempted yet" before any event has ever fired, then whatever the last
    // attempt's result was: an HTTP status, a transport failure reason, or
    // "upload thread alloc failed".
    const char* getCloudStatusText();

    // Live free/max-contiguous-block/total size of TrackerBridge's own
    // dedicated heap, for TrackerBridgeDebugOverlay - "No dedicated heap
    // (initHeap() never ran or failed)" if initHeap() hasn't succeeded.
    // Unlike querying sead::HeapMgr's "current heap" directly, this always
    // reflects real state: it's a heap we created and own outright, not one
    // dependent on whatever ambient scope happens to be active at the
    // caller's context (see sendCloud()'s comment on why that mattered).
    const char* getHeapStatusText();

} // namespace TrackerBridge
