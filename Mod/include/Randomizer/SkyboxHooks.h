#pragma once

namespace sead {
    class Heap;
}

namespace SkyboxHooks {

    void setHeap(sead::Heap* heap);
    void drawDebugWindow();
    void installHooks();

} // namespace SkyboxHooks
