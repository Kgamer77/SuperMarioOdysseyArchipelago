#pragma once

namespace ChestOrderRando {

    // Shuffles the required opening order of the sequential-chest puzzle rooms
    // (TreasureBoxSequentialDirector + its linked TreasureBoxSequential boxes).
    // No-op unless Randomizer::isChestOrderRandoEnabled().
    void installHooks();

} // namespace ChestOrderRando
