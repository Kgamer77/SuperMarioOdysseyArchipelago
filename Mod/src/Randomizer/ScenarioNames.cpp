#include "Randomizer/ScenarioNames.h"

#include "Randomizer/WorldMeta.h"

namespace Randomizer::ScenarioNames {

    namespace {

        // One row per kingdom (WorldMeta::WorldId_* order), one column per scenario
        // number (index 0 = scenario 1). nullptr = no named scenario at that slot.
        constexpr const char* sNames[WorldMeta::sWorldCount][kMaxScenariosPerKingdom] = {
            // Cap
            { "First Visit", "Revisit/peace", "Post-game", "Moon Rock", "Balloon World", "KFR", "Trailer" },
            // Cascade
            { "First Visit", "Revisit/peace", "Post-game", "Moon Rock", "KFR", "Balloon World", nullptr, "E3/Trailer" },
            // Sand
            { "First Visit", "Night", "Peace", "Post-game", "Moon Rock", "Balloon World", "KFR", "Kiosk Demo" },
            // Wooded
            { "First Visit", "Post-Spewart", "Peace", "Post-game", "Moon Rock", "Balloon World", "KFR" },
            // Lake
            { "First Visit", "Peace", "Post-game", "Moon Rock", "KFR", "Balloon World" },
            // Cloud
            { "First Visit", "Revisit/peace", "Post-game", "Moon Rock" },
            // Lost
            { "First Visit", "Revisit/peace", "Post-game", "Moon Rock", "Balloon World", "KFR" },
            // Metro
            { "Night", "Day", "Festival", "Peace", "Post-game", "Balloon World", "Festival Revisit?",
                "Moon Rock", "KFR", "Morning Metro", "8-Bit Festival", nullptr, "E3 Day Metro",
                "E3 Festival", "Normal Night Metro" },
            // Seaside
            { "First Visit", "Peace", "Post-game", "Moon Rock", "KFR", "Balloon World" },
            // Snow
            { "First Visit", "Peace", "Post-game", "Moon Rock", "Balloon World", "KFR" },
            // Luncheon
            { "First Visit", "Post-Meat", "Post-game", "KFR", "Volcano-less", "Volcano-less", "Peace",
                "Moon Rock", "Balloon World", "Bruncheon" },
            // Ruined
            { "First Visit", "Peace", "Post-game", "Moon Rock" },
            // Bowser's
            { "First Visit", "Peace", "Post-game", "Moon Rock", "Balloon World", "KFR" },
            // Moon
            { "First Visit", "Peace/Postgame", "Moon Rock", "Balloon World", "KFR" },
            // Mushroom
            { "Rushroom", "Post-game", "World Peace?", "KFR", "Balloon World" },
            // Dark
            { "First Visit", "Peace" },
            // Darker
            { "First Visit", "Peace" },
        };

        bool isValidWorldId(s32 worldId) {
            return worldId >= 0 && worldId < WorldMeta::sWorldCount;
        }

    } // namespace

    s32 getCount(s32 worldId) {
        if (!isValidWorldId(worldId))
            return 0;

        s32 count = 0;
        for (s32 i = 0; i < kMaxScenariosPerKingdom; i++)
            if (sNames[worldId][i])
                count++;
        return count;
    }

    bool getByIndex(s32 worldId, s32 index, s32* outScenarioNo, const char** outName) {
        if (!isValidWorldId(worldId) || index < 0)
            return false;

        s32 seen = 0;
        for (s32 i = 0; i < kMaxScenariosPerKingdom; i++) {
            if (!sNames[worldId][i])
                continue;
            if (seen == index) {
                if (outScenarioNo)
                    *outScenarioNo = i + 1;
                if (outName)
                    *outName = sNames[worldId][i];
                return true;
            }
            seen++;
        }
        return false;
    }

    const char* find(s32 worldId, s32 scenarioNo) {
        if (!isValidWorldId(worldId) || scenarioNo < 1 || scenarioNo > kMaxScenariosPerKingdom)
            return nullptr;
        return sNames[worldId][scenarioNo - 1];
    }

} // namespace Randomizer::ScenarioNames
