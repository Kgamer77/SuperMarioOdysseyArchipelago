#pragma once

#include <basis/seadTypes.h>

namespace MoonRando {
    struct AreaRouteSet;
}

// The route panel shared by Extended Hints (collection list) and the read-only
// "Kingdom Checks" menu. A route is several short lines per way in, and there
// can be more than one way in, so this is drawn as its own sized panel rather
// than squeezed into the game's fixed-size confirm-window message pane.
//
// The panel is passive: it draws while visible and never reads input. Whoever
// opened it owns closing it, because the two callers sit behind completely
// different input paths (the collection list's gated pad hooks vs. the pause
// menu's own list input).
namespace PathHintOverlay {

    void show(const char* title, const MoonRando::AreaRouteSet& routes);
    // Panel with a single explanatory line, for "no route could be worked out".
    void showMessage(const char* title, const char* message);
    void hide();
    bool isVisible();
    void draw();

} // namespace PathHintOverlay
