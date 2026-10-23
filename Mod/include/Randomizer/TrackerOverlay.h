#pragma once

#include <basis/seadTypes.h>

namespace TrackerOverlay {

    // The kingdom icon glyph / label / accent colour the tracker rows use.
    // Exposed so other HUD surfaces (the Nuzlocke death screen) can present a
    // kingdom the same way the tracker does instead of keeping a second table.
    // The colour is an ImU32 kept untyped so this header stays imgui-free.
    struct WorldStyle {
        const char* icon;
        const char* name;
        u32 color;
    };

    // A null icon means the tracker does not list that world id.
    WorldStyle getWorldStyle(s32 logicalWorldId);

    // Shared layout values used by board-mode overlays so switching between the
    // tracker and board does not make the HUD jump vertically.
    float getHudScale();
    float getPanelTop();
    float getLastPanelBottom();
    bool isHudConditionActive();

    // Draws the progress-tracker HUD below the moon counter, on the configured
    // screen side: per-kingdom moon count / requirement (or "?" until visited),
    // total moons, world-peace + moon-cube state, and endgame checklists.
    //
    // No-op unless enabled and the current scene is a StageScene with valid save
    // data. Call every frame so disabling the tracker also clears fade state.
    void draw();

} // namespace TrackerOverlay
