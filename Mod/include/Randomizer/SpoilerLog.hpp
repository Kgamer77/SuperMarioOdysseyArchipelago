#pragma once

namespace SpoilerLog {

    /// Creates sd:/SMO-Randomizer/ if it doesn't exist.
    /// Call this once from hkMain or gameSystemInit so the folder is ready early.
    void ensureDirectory();

    /// Writes sd:/SMO-Randomizer/SpoilerLog.txt with the full moon mapping.
    /// Call after Randomizer::reinit() when moon rando is enabled.
    void generate();

    /// Writes sd:/SMO-Randomizer/Spoiler-Logs/GenerationFailure - <seed>.txt:
    /// the settings, the per-attempt diagnostics table, the warnings and the
    /// failure message, and nothing that needs a finished placement.
    ///
    /// generate() cannot serve this case - it bails on !MoonRando::isInitialized(),
    /// which is exactly the state a rejected seed leaves behind - so before this
    /// existed a seed that failed to generate produced NO artifact at all. The
    /// one case anybody actually needs to debug was the one case with no log.
    void generateFailureReport();

} // namespace SpoilerLog