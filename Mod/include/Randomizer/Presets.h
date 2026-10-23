#pragma once

#include <basis/seadTypes.h>

// Hardcoded "preset" definitions for the new-file Main Settings page. A preset is
// a named bundle of randomizer settings plus a one-line description shown in the
// menu's info box. Kept as a C++ table (there is no JSON parser available this
// early in the mod); the layout mirrors a small JSON object so it can later be
// loaded from romfs if a parser is added.
namespace Presets {

    struct Preset {
        const char16_t* name; // shown in the roll
        const char* description; // shown in the info box (multibyte)
        void (*apply)(); // sets the underlying Randomizer toggles
    };

    s32 count();
    const Preset& get(s32 index);

    // Apply preset `index`'s settings to the live Randomizer state.
    void apply(s32 index);

} // namespace Presets
