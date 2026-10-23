#pragma once

namespace NuzlockeOverlay {

    // Draws the Nuzlocke death screen's moon-loss ledger and the seed-reroll
    // progress panel, centered over whatever the sequence is showing.
    //
    // These run after the StageScene is destroyed, so unlike TrackerOverlay this
    // cannot key off a live scene: it renders purely from the coordinator's
    // published Nuzlocke::OverlayState. No-op while that state is Hidden. Call
    // every frame from the ImGui draw hook.
    void draw();

} // namespace NuzlockeOverlay
