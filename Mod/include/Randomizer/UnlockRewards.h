#pragma once

#include <basis/seadTypes.h>

// Tunable coin rewards + message text for moon-collect unlock feedback.
// Kept in one header so the values/strings are easy to change.
namespace Randomizer::UnlockRewards {

    // Filler: a kingdom's moons that are not its lock reward look like Cap moons and pay this.
    static constexpr s32 kFillerCoins = 100;

    // First-time locked unlocks (capture/ability/painting link) award no coins.
    static constexpr s32 kFirstTimeUnlockCoins = 0;

    // Collecting a moon whose reward is already owned pays a duplicate bonus and shows "(again)".
    static constexpr s32 kCaptureDuplicateCoins = 1000;
    static constexpr s32 kAbilityDuplicateCoins = 1500;
    static constexpr s32 kPaintingDuplicateCoins = 1000;

    // Suffix appended to an unlock message when the reward was already owned.
    static constexpr const char* kAgainSuffix = " (again)";

} // namespace Randomizer::UnlockRewards
