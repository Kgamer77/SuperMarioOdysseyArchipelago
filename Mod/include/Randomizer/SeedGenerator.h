#pragma once

#include <basis/seadTypes.h>

#include "SD/SDHooks.h"

namespace SeedGenerator {
    // A Soft respawn deliberately keeps generation data independent from the
    // live GameDataFile.  The file is only rebuilt on the sequence thread
    // after this worker has finished, so the worker never races FileLoadThread.
    // No carried-moon set travels with a soft respawn request. Collection flags
    // are keyed by *item* and the new seed re-places every item, so an item set
    // means nothing on the far side of generation - the moons that carry the
    // run's counts are picked afterwards, against the new placement, by
    // Nuzlocke::commitSoftPenalty. Generation is gated on solvability alone.

    struct SoftSeedRequest {
        s32 slot = -1;
        u32 oldSeed = 0;
        u32 requestedSeed = 0;
        u64 retainedCaptures = 0;
        u64 retainedAbilities = 0;
        u64 retainedPaintings = 0;
    };

    // Carves the dedicated heap for the generator worker thread's large stack.
    // Must be called early (GameSystem::init) while the stationed heap is fresh;
    // by title/Start-Game time it no longer has room for the 0x80000 stack.
    void createHeap();
    bool prepareThread();
    bool isPrepared();

    // The new-save generation path, triggered from a file-select screen
    // browsing before anything is "active" - category has to be passed
    // explicitly (SaveRedirect::getActiveCategory() would just be whatever
    // was last activated, not necessarily what's being browsed/generated).
    bool start(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool startSoft(const SoftSeedRequest& request);
    bool isRunning();
    bool isDone();
    bool succeeded();
    s32 progressPercent();
    const char* status();
    s32 getSlot();
    SaveRedirect::Category getCategory();
    void reportProgress(s32 percent, const char* status);
} // namespace SeedGenerator
