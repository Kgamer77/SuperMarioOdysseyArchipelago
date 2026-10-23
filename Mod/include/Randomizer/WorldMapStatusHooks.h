#pragma once

namespace WorldMapStatusHooks {

    // QoL: color-codes each kingdom's name on the World Map screen (white =
    // previously completed, green = current kingdom, orange = visited but not
    // yet progression-unlocked) in both the Visited and Unlocked
    // VisitedKingdomsMapMode settings.
    void installHooks();

} // namespace WorldMapStatusHooks
