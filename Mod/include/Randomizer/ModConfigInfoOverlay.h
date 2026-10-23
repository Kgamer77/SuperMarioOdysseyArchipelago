#pragma once

namespace ModConfigInfoOverlay {

    // Draws the currently-selected item's description text for the in-game
    // ModConfig Misc/Quality of Life/Scenario Override menu (StageSceneStateModConfig
    // populates the text; this only draws it). Pure ImGui draw-list overlay -
    // deliberately not a native layout part, since the title scene's
    // layout-executor table is already at capacity.
    //
    // No-op whenever there is no description text to show (menu closed, or the
    // highlighted row has none). Safe to call every frame.
    void draw();

} // namespace ModConfigInfoOverlay
