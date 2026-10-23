#pragma once

#include "Randomizer/Randomizer.h"

#include "imgui.h"

namespace HudTheme {

    // Panel chrome (outline, header divider) and heading text are tinted per run
    // mode so a glance at the HUD says which mode is running: Standard keeps the
    // original yellow, Toadette Medal Hunt is pink, Bingo blue, Ascend red,
    // Nuzlocke orange, Timer purple.
    // Only the hue changes -- the alphas are the ones the yellow palette was
    // tuned with, so panels keep the same weight against the game behind them.
    struct Palette {
        ImU32 border; // panel outline
        ImU32 divider; // hairline under a panel header
        ImU32 heading; // section titles and headline stats
    };

    inline Palette get() {
        const Randomizer::RunMode mode = Randomizer::getRunMode();
        if (mode == Randomizer::RunMode::Bingo) {
            return { IM_COL32(86, 150, 255, 175), IM_COL32(86, 150, 255, 115),
                IM_COL32(140, 185, 255, 255) };
        }
        if (mode == Randomizer::RunMode::Ascend) {
            return { IM_COL32(255, 88, 88, 175), IM_COL32(255, 88, 88, 115),
                IM_COL32(255, 145, 140, 255) };
        }
        // Both Nuzlocke variants share one hue: they are one mode in the menu
        // (Soft is a checkbox), so they read as one mode on the HUD too. Kept
        // clear of Ascend's red, which the death screen already uses for losses.
        if (mode == Randomizer::RunMode::Nuzlocke
            || mode == Randomizer::RunMode::SoftNuzlocke) {
            return { IM_COL32(255, 150, 60, 175), IM_COL32(255, 150, 60, 115),
                IM_COL32(255, 182, 112, 255) };
        }
        if (mode == Randomizer::RunMode::Timer) {
            return { IM_COL32(178, 118, 255, 175), IM_COL32(178, 118, 255, 115),
                IM_COL32(202, 160, 255, 255) };
        }
        // Last, because its second condition is a win-condition fallback rather
        // than a run mode: checking it earlier would repaint a mode that already
        // has its own hue pink.
        //
        // "Medal Hunt + Defeat Bowser" leaves the run mode Standard -- only the
        // pure medal win condition claims RunMode::ToadetteMedals -- but it is
        // still a medal hunt as far as the HUD is concerned.
        if (mode == Randomizer::RunMode::ToadetteMedals
            || Randomizer::isToadetteMedalModeEnabled()) {
            return { IM_COL32(255, 128, 196, 175), IM_COL32(255, 128, 196, 115),
                IM_COL32(255, 170, 214, 255) };
        }
        return { IM_COL32(255, 218, 88, 175), IM_COL32(255, 218, 88, 115),
            IM_COL32(255, 230, 130, 255) };
    }

} // namespace HudTheme
