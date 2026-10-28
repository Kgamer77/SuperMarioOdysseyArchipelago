#pragma once
#include "types.h"

namespace SDRedirect {
    void installHooks();
}

namespace SaveRedirect {
    // SinglePlayer is the normal solo-play save tree (sd:/SMO-Randomizer/
    // Saves/Slot<N>/), unchanged from before this existed. Multiplayer is a
    // fully separate tree (sd:/SMO-Randomizer/MPSaves/Slot<N>/) entered via
    // the title screen's Multiplayer button, so playing MP can never read or
    // write a single-player save.
    enum class Category : s32
    {
        Randomizer,
        Archipealgo,
        SinglePlayer,
        Multiplayer,
        MultiplayerGuest,
        Archipelago_MultiData,
        Archipelago_Multiplayer,
    };

    void installHooks();
    void setActiveSlot(s32 slot, Category category = Category::SinglePlayer);
    Category getActiveMode();
    s32 getActiveSlot();
    Category getActiveCategory();
    // Deletes every per-slot artifact.  Common.bin is deliberately shared and
    // is never touched.  Missing files are treated as already deleted.
    // Defaults to whatever category is currently active (matches every
    // in-gameplay caller, e.g. a Nuzlocke death mid-session) rather than
    // always assuming SinglePlayer - a file-select browsing screen should
    // still pass its own category explicitly, since it isn't necessarily
    // browsing the currently-active one.
    bool deleteSlot(s32 slot, u32 seed, Category category = getActiveCategory());
    bool deleteGuestTemp();
}
