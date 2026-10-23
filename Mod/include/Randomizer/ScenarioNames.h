#pragma once

#include <basis/seadTypes.h>

// Human-readable names for each kingdom's scenario numbers, sourced from the
// project's scenario reference sheet. Used by the debug "Scenario Override"
// menu to label what each scenario number actually means for the kingdom the
// player is currently in.
namespace Randomizer::ScenarioNames {

    constexpr s32 kMaxScenariosPerKingdom = 15;

    // Number of named scenarios for worldId (WorldMeta::WorldId_*), 0 if unknown.
    s32 getCount(s32 worldId);

    // Fills outScenarioNo/outName for the index'th named scenario (0-based, in
    // ascending scenario-number order) of worldId. Returns false if out of range.
    bool getByIndex(s32 worldId, s32 index, s32* outScenarioNo, const char** outName);

    // Name for a specific (worldId, scenarioNo) pair, or nullptr if this kingdom
    // has no name on record for that scenario number.
    const char* find(s32 worldId, s32 scenarioNo);

} // namespace Randomizer::ScenarioNames
