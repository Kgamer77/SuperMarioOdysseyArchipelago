#pragma once

namespace AreaNameOverlay {

    // Called once per stage load with the stage being entered (overworld home
    // stage or a subarea). No-ops if the name is unchanged from last time.
    void show(const char* stageName);
    void draw();

} // namespace AreaNameOverlay
