#pragma once

namespace ShineDB {
    struct ShineEntry;
}

namespace MoonPurchaseOverlay {

    // `moon` is the original purchased moon (its kingdom is shown in parentheses);
    // `location` is where that moon was randomized to (its name is shown as the title,
    // matching the hint text). `location` may be null, in which case the moon's own
    // name is used.
    void queue(const ShineDB::ShineEntry& moon, const ShineDB::ShineEntry* location,
        bool hasCompanionOverlay);
    // Shows the "YOU BOUGHT" banner with a caller-provided line. Used by Purple
    // Shop moons, which have no ShineEntry to point at.
    void queueText(const char* text);
    void clear();
    void draw();

} // namespace MoonPurchaseOverlay
