#include "Randomizer/ScenarioPatcher.h"

#include "EntranceDB.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MoonRequirement.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/SubAreaResourceLoad.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "al/Library/Nerve/NerveUtil.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/WorldList.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include <cstdint>
#include <cstring>

namespace al {
    class Scene;
} // namespace al

class StageSceneStateRaceManRace;
class StageSceneStateYukimaruRace;
class StageSceneStateYukimaruRaceTutorial;

namespace ScenarioPatcher {
    namespace {
        namespace LD = Randomizer::LogicData;

        static s32 sLastNormalScenario[WorldMeta::sWorldCount] = {};
        static bool sScenarioCacheValid = false;
        static GameDataHolder* sScenarioCacheHolder = nullptr;
        static GameDataFile* sScenarioCacheFile = nullptr;
        static s64 sScenarioCacheSaveDataId = 0;

        static bool sPendingRestartValid = false;
        static GameDataHolder* sPendingRestartHolder = nullptr;
        static GameDataFile* sPendingRestartFile = nullptr;
        static s64 sPendingRestartSaveDataId = 0;
        static s32 sPendingRestartScenario = -1;

        static constexpr uintptr_t cStageSceneStateYukimaruRaceOffset = 0x1c0;
        static constexpr uintptr_t cStageSceneStateYukimaruRaceTutorialOffset = 0x1c8;
        static constexpr uintptr_t cStageSceneStateRaceManRaceOffset = 0x1d0;
        static constexpr uintptr_t cStageSceneGameDataHolderOffset = 0x2d0;
        static constexpr uintptr_t cStageSceneNrvRaceYukimaruOffset = 0x1d8cbf8;
        static constexpr uintptr_t cStageSceneNrvRaceManRaceOffset = 0x1d8cc08;
        static constexpr const char* kAuditProfileMarker = "SMOR_AUDIT_PROFILE:1";

        // WorldList::getAfterEndingScenarioNo is not exported by every retail
        // build.  The native after-ending value is stored directly on the
        // corresponding WorldListEntry.
        s32 getAfterEndingScenarioNo(const WorldList* worldList, s32 worldId) {
            if (!worldList || worldId < 0 || worldId >= sNumWorlds)
                return -1;
            const WorldListEntry* entry = worldList->mWorldList[worldId];
            return entry ? entry->endingScenario : -1;
        }

        void clearScenarioCache() {
            for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
                sLastNormalScenario[i] = 0;
        }

        GameDataFile* getActiveFile(GameDataHolderAccessor accessor) {
            if (!accessor.mData)
                return nullptr;
            return accessor.mData->getGameDataFile();
        }

        // syncScenarioCache tracks which GameDataFile/save the cache belongs to, clearing
        // sLastNormalScenario on a genuine save-file change (file == nullptr). Scenario
        // resolution itself no longer reads this cache: calcActualScenario recomputes the
        // scenario live from moon-collection state on every call, and rememberScenario /
        // rememberCurrentFileScenario are no-op stubs kept only for call-site compatibility.
        // sLastNormalScenario is now write-only, populated solely by healAllScenarios.
        void syncScenarioCache(GameDataFile* file) {
            if (!file) {
                if (sScenarioCacheValid) {
                    clearScenarioCache();
                    sScenarioCacheValid = false;
                    sScenarioCacheHolder = nullptr;
                    sScenarioCacheFile = nullptr;
                    sScenarioCacheSaveDataId = 0;
                }
                return;
            }

            GameDataHolder* holder = file->getGameDataHolder();
            const s64 saveDataId = file->getSaveDataIdForPrepo();

            if (sScenarioCacheValid && sScenarioCacheHolder == holder && sScenarioCacheFile == file && sScenarioCacheSaveDataId == saveDataId)
                return;

            sScenarioCacheValid = true;
            sScenarioCacheHolder = holder;
            sScenarioCacheFile = file;
            sScenarioCacheSaveDataId = saveDataId;
        }

        bool isRaceManRace(GameDataFile* file, s32 raceType) {
            if (raceType == GameDataFile::RaceType_Flag)
                return true;
            return file && file->getRaceType() == GameDataFile::RaceType_Flag;
        }

        bool isSameString(const char* left, const char* right) {
            if (!left || !right)
                return false;
            return std::strcmp(left, right) == 0;
        }

        const WorldMeta::Entry* findEntryForStageName(const char* stageName) {
            const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
            if (entry)
                return entry;

            if (!stageName)
                return nullptr;

            for (s32 i = 0; i < LD::sStageCount; i++) {
                if (isSameString(LD::sStages[i].stage, stageName))
                    return WorldMeta::getByWorldId(LD::sStages[i].kingdom);
            }

            return nullptr;
        }

        // Only a kingdom's main/home stage represents that kingdom's progression. Sub-areas
        // (story rooms, and rooms entrance rando dropped here from another kingdom) must read
        // the kingdom scenario to load correctly but must never advance or cache it.
        bool isMainKingdomStage(const char* stageName) {
            return WorldMeta::findByMainStage(stageName) != nullptr;
        }

        // Randomizable sub-areas (those listed in EntranceDB) must never have their
        // scenario forced to the host kingdom's progression scenario: entrance rando can
        // drop them under a kingdom sitting at a scenario their stage data does not define,
        // which loads an invalid/empty layout. Only main kingdom stages and non-randomizable
        // story-mission sub-areas get patched.
        bool isUnpatchedSubArea(const char* stageName) {
            return EntranceDB::isSubStage(stageName);
        }

        bool isWorldIdForEntry(s32 worldId, const WorldMeta::Entry& entry) {
            if (worldId < 0)
                return false;
            if (worldId == entry.worldId || worldId == entry.scenarioWorldId)
                return true;

            const WorldMeta::Entry* byWorldId = WorldMeta::getByWorldId(worldId);
            if (byWorldId && byWorldId->worldId == entry.worldId)
                return true;

            const WorldMeta::Entry* byScenarioWorldId = WorldMeta::getByScenarioWorldId(worldId);
            return byScenarioWorldId && byScenarioWorldId->worldId == entry.worldId;
        }

        bool isCurrentWorldDestination(GameDataFile* file, const WorldMeta::Entry& entry) {
            if (!file)
                return false;

            const char* currentStage = file->getStageNameCurrent();

            if (isSameString(currentStage, entry.mainStage))
                return true;

            const WorldMeta::Entry* currentEntry = findEntryForStageName(currentStage);
            if (currentEntry && currentEntry->worldId == entry.worldId)
                return true;

            if (isWorldIdForEntry(file->getCurrentWorldId(), entry))
                return true;

            return isWorldIdForEntry(file->getCurrentWorldIdForDisp(), entry);
        }

        bool isPendingRestartForFile(GameDataFile* file) {
            if (!sPendingRestartValid || !file)
                return false;

            return sPendingRestartHolder == file->getGameDataHolder() && sPendingRestartFile == file && sPendingRestartSaveDataId == file->getSaveDataIdForPrepo();
        }

        void clearPendingRestartScenario() {
            sPendingRestartValid = false;
            sPendingRestartHolder = nullptr;
            sPendingRestartFile = nullptr;
            sPendingRestartSaveDataId = 0;
            sPendingRestartScenario = -1;
        }

        void rememberPendingRestartScenario(GameDataFile* file) {
            clearPendingRestartScenario();
            if (!file)
                return;

            const s32 scenario = file->getScenarioNo();
            if (scenario <= 0)
                return;

            sPendingRestartValid = true;
            sPendingRestartHolder = file->getGameDataHolder();
            sPendingRestartFile = file;
            sPendingRestartSaveDataId = file->getSaveDataIdForPrepo();
            sPendingRestartScenario = scenario;
        }

        s32 getPendingRestartScenario(GameDataFile* file, const WorldMeta::Entry& entry) {
            if (!isPendingRestartForFile(file) || sPendingRestartScenario <= 0)
                return -1;
            if (!isCurrentWorldDestination(file, entry))
                return -1;
            return sPendingRestartScenario;
        }

        bool tryApplyPendingRestartScenario(GameDataFile* file, ChangeStageInfo* info) {
            if (!info || info->isEmpty())
                return false;

            const WorldMeta::Entry* entry = findEntryForStageName(info->mChangeStageName.cstr());
            if (!entry)
                return false;

            const s32 scenario = getPendingRestartScenario(file, *entry);
            if (scenario <= 0)
                return false;

            if (info->mScenarioNo != scenario) {
                Randomizer::logPush("Scenario restart patch: %s %d -> %d",
                    info->mChangeStageName.cstr(), info->mScenarioNo, scenario);
                info->mScenarioNo = scenario;
            }

            return true;
        }

        s32 normalizeScenario(GameDataHolderAccessor accessor, const WorldMeta::Entry& entry, s32 requested, bool isKfr, bool authoritative);

        s32 normalizePendingRestartCalc(GameDataHolderAccessor accessor, const char* stageName, s32 scenario) {
            const WorldMeta::Entry* entry = findEntryForStageName(stageName);
            if (!entry)
                return scenario;

            GameDataFile* file = getActiveFile(accessor);
            const s32 restartScenario = getPendingRestartScenario(file, *entry);
            return restartScenario > 0 ? restartScenario : scenario;
        }

        s32 normalizeCalculatedScenario(GameDataHolderAccessor accessor, const char* stageName, s32 scenario) {
            GameDataFile* file = getActiveFile(accessor);
            syncScenarioCache(file);

            if (MoonRequirement::isMoonChapelStage(stageName))
                return 1;

            if (isUnpatchedSubArea(stageName))
                return scenario;

            const WorldMeta::Entry* entry = findEntryForStageName(stageName);
            if (!entry)
                return scenario;

            const s32 restartScenario = normalizePendingRestartCalc(accessor, stageName, scenario);
            if (restartScenario != scenario)
                return restartScenario;

            // calcNextScenarioNo runs every frame to compute the load scenario; it is a pure
            // read and must never promote or store (entering a foreign hosted sub-area must not
            // touch its kingdom's scenario).
            return normalizeScenario(accessor, *entry, scenario, isRaceManRace(file, 0), /*authoritative=*/false);
        }

        s32 getKfrScenario(s32 worldId) {
            switch (worldId) {
            case WorldMeta::WorldId_Cap:
                return 6;
            case WorldMeta::WorldId_Cascade:
                return 5;
            case WorldMeta::WorldId_Sand:
                return 7;
            case WorldMeta::WorldId_Wooded:
                return 7;
            case WorldMeta::WorldId_Lake:
                return 5;
            case WorldMeta::WorldId_Lost:
                return 6;
            case WorldMeta::WorldId_Metro:
                return 9;
            case WorldMeta::WorldId_Seaside:
                return 5;
            case WorldMeta::WorldId_Snow:
                return 6;
            case WorldMeta::WorldId_Luncheon:
                return 4;
            case WorldMeta::WorldId_Bowsers:
                return 6;
            case WorldMeta::WorldId_Moon:
                return 5;
            case WorldMeta::WorldId_Mushroom:
                return 4;
            default:
                return -1;
            }
        }

        s32 getPostGameForPeaceScenario(s32 worldId, s32 scenario) {
            switch (worldId) {
            case WorldMeta::WorldId_Cap:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Cascade:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Sand:
                return scenario == 3 ? 4 : scenario;
            case WorldMeta::WorldId_Wooded:
                return scenario == 3 ? 4 : scenario;
            case WorldMeta::WorldId_Lake:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Cloud:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Lost:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Metro:
                return scenario == 4 ? 5 : scenario;
            case WorldMeta::WorldId_Seaside:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Snow:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Luncheon:
                return scenario == 3 ? 8 : scenario;
            case WorldMeta::WorldId_Ruined:
                return scenario == 2 ? 3 : scenario;
            case WorldMeta::WorldId_Bowsers:
                return scenario == 2 ? 3 : scenario;
            default:
                return scenario;
            }
        }

        bool isFestivalRevisit(s32 worldId, s32 scenario) {
            return worldId == WorldMeta::WorldId_Metro && scenario == 7;
        }

        bool isMorningMetro(s32 worldId, s32 scenario) {
            return worldId == WorldMeta::WorldId_Metro && scenario == 10;
        }

        bool isBruncheon(s32 worldId, s32 scenario) {
            return worldId == WorldMeta::WorldId_Luncheon && scenario == 10;
        }

        bool isScenarioBlockedByLogic(s32 worldId, s32 scenario) {
            if (worldId < 0 || worldId >= WorldMeta::sWorldCount || scenario < 0)
                return false;

            const s32 maxScenario = LD::sMaxLogicScenarioByKingdom[worldId];
            if (maxScenario >= 0 && scenario > maxScenario)
                return true;

            if (scenario >= 32)
                return true;

            return (LD::sBlockedScenarioMaskByKingdom[worldId] & (1u << scenario)) != 0;
        }

        s32 getFallbackScenario(s32 worldId) {
            if (worldId == WorldMeta::WorldId_Cap || worldId == WorldMeta::WorldId_Lost)
                return 3;
            if (worldId == WorldMeta::WorldId_Moon || worldId == WorldMeta::WorldId_Mushroom)
                return 2;
            return 1;
        }

        // Mirror of MoonRando::isScriptedScenarioAdvance: a single story beat advances
        // ScenarioNo without a triple moon (Metro festival).
        bool isScriptedScenarioAdvance(s32 worldId, s32 target) {
            return worldId == WorldMeta::WorldId_Metro && target == 3;
        }

        bool isSnowGrandPrixLocation(const LD::MoonAccess& moon) {
            return moon.kingdom == WorldMeta::WorldId_Snow && moon.uid == 1020;
        }

        s32 calcActualScenario(GameDataFile* file, s32 worldId) {

            if (!file)
                return getFallbackScenario(worldId);

            // Reject a contaminated saved scenario: the vanilla world-travel sequence can
            // stamp a *departing* kingdom's slot with the *arriving* kingdom's KFR scenario
            // (e.g. leaving Cascade for Sand wrote Sand's KFR 7 into Cascade's slot). A
            // legitimate save never exceeds the kingdom's max logic scenario, so a value
            // above it is bogus and must not be trusted — otherwise it trips the moon-rock
            // short-circuit below and forces the kingdom to World Peace with no story done.
            const s32 maxLogicScenario = LD::sMaxLogicScenarioByKingdom[worldId];
            s32 savedScenario = file->getScenarioNumArr()[worldId];
            if (maxLogicScenario >= 0 && savedScenario > maxLogicScenario)
                savedScenario = 0;

            s32 maxScenario = Randomizer::getInitialKingdomScenario(worldId);
            if (maxScenario <= 0)
                maxScenario = 1;

            if (maxScenario < getFallbackScenario(worldId))
                maxScenario = getFallbackScenario(worldId);

            // Scenario progression is physical and follows the furthest completed story
            // beat. Only a *multi/triple* moon location advances the scenario (single story
            // moons never move ScenarioNo, even though they count as story beats). A later
            // story location can advance its own scenario transition even when earlier
            // story locations remain uncollected; this marks their story progress complete
            // without granting those missing moons. World Peace is gated only on the kingdom's
            // story multi-moons.
            // After the remap the save flag lives under the *item* placed at the location,
            // so collection is read through the placement map.
            auto isLocationCollected = [&](const LD::MoonAccess& m) -> bool {
                const ShineDB::ShineEntry* placedItem = MoonRando::getItemForLocation(m.uid, m.stage);
                return placedItem
                    ? file->customIsGotShine(placedItem->uid, placedItem->stage)
                    : file->customIsGotShine(m.uid, m.stage);
            };

            static constexpr s32 kMaxStoryOrder = 16;
            bool storyPresent[kMaxStoryOrder] = {};
            bool storyCollected[kMaxStoryOrder] = {};
            bool storyMulti[kMaxStoryOrder] = {};
            s16 storyMoveTo[kMaxStoryOrder];
            for (s32 i = 0; i < kMaxStoryOrder; i++)
                storyMoveTo[i] = -1;
            s32 maxStoryOrder = -1;
            s32 storyTripleCount = 0;
            s32 collectedStoryTripleCount = 0;
            bool snowGrandPrixCollected = false;

            for (s32 i = 0; i < LD::sMoonCount; i++) {
                const LD::MoonAccess& moon = LD::sMoons[i];
                if (moon.kingdom != worldId)
                    continue;

                const bool collected = isLocationCollected(moon);
                const bool multi = MoonRando::isMultiMoonLocation(moon.uid, moon.stage);

                if (collected && multi && isSnowGrandPrixLocation(moon))
                    snowGrandPrixCollected = true;

                // Non-story scenario advances (e.g. triple moons that bump a sub-scenario)
                // are physical and only fire on multi moons.
                if (collected && multi && moon.scenarioMoveTo > maxScenario)
                    maxScenario = moon.scenarioMoveTo;

                if (moon.storyOrder >= 0 && moon.storyOrder < kMaxStoryOrder) {
                    storyPresent[moon.storyOrder] = true;
                    storyCollected[moon.storyOrder] = collected;
                    storyMulti[moon.storyOrder] = multi;
                    storyMoveTo[moon.storyOrder] = moon.storyMoveTo;
                    if (multi) {
                        storyTripleCount++;
                        if (collected)
                            collectedStoryTripleCount++;
                    }
                    if (moon.storyOrder > maxStoryOrder)
                        maxStoryOrder = moon.storyOrder;
                }
            }

            // The Bound Bowl Grand Prix is physically collected while Snow is
            // still in scenario 1, but it is story order 4 because the town's
            // barriers can be done in any order.  Do not make the race wait for
            // the generic story-order gate: winning it advances Snow to scenario
            // 3.  The Moon Rock scan below remains authoritative for scenario 4.
            if (worldId == WorldMeta::WorldId_Snow
                && maxScenario == 1
                && snowGrandPrixCollected) {
                maxScenario = 3;
            }

            for (s32 order = 0; order <= maxStoryOrder; order++) {
                if (!storyPresent[order] || !storyCollected[order])
                    continue;

                if (storyMoveTo[order] >= 0
                    && (storyMulti[order] || isScriptedScenarioAdvance(worldId, storyMoveTo[order]))
                    && storyMoveTo[order] > maxScenario)
                    maxScenario = storyMoveTo[order];
            }

            if (storyTripleCount > 0 && collectedStoryTripleCount >= storyTripleCount) {
                if (LD::sPostGameScenarioByKingdom[worldId] > maxScenario) {
                    maxScenario = LD::sPostGameScenarioByKingdom[worldId];
                }
            }

            // Cloud Kingdom has no moons and no required boss fight: it always opens
            // straight into its post-game scenario, no scripted event needed.
            if (worldId == WorldMeta::WorldId_Cloud && LD::sPostGameScenarioByKingdom[worldId] > maxScenario) {
                maxScenario = LD::sPostGameScenarioByKingdom[worldId];
            }

            // World Peace Requirement: On Kingdom Complete - once the player has
            // left this kingdom for a different one with the mode active, float it
            // up to its post-game scenario floor going forward, mirroring the
            // Cloud Kingdom always-postgame special case above but keyed off the
            // persisted forced-peace bitmask instead of a fixed kingdom id.
            if (Randomizer::getWorldPeaceRequirementMode() == Randomizer::WorldPeaceRequirementMode_OnKingdomComplete
                && Randomizer::isKingdomForcedPeace(worldId)
                && LD::sPostGameScenarioByKingdom[worldId] > maxScenario) {
                Randomizer::logPush("WORLDPEACE: flooring world=%d %d -> %d (mask=%08x)",
                    worldId, maxScenario, LD::sPostGameScenarioByKingdom[worldId],
                    Randomizer::getForcedPeaceKingdoms());
                maxScenario = LD::sPostGameScenarioByKingdom[worldId];
            }

            // Honor the saved Moon Rock scenario ONLY if the physical moon scan reached post-game.
            // This prevents a contaminated saved scenario (e.g. from an out-of-order KFR warp)
            // from falsely bumping the kingdom to World Peace on arrival.
            if (LD::sMoonRockScenarioByKingdom[worldId] > 0
                && savedScenario >= LD::sMoonRockScenarioByKingdom[worldId]) {
                const s32 postGameScenario = LD::sPostGameScenarioByKingdom[worldId];
                if (postGameScenario > 0 && maxScenario >= postGameScenario) {
                    maxScenario = LD::sMoonRockScenarioByKingdom[worldId];
                }
            }

            return maxScenario;
        }

        s32 getRememberedScenario(GameDataHolderAccessor accessor, const WorldMeta::Entry& entry) {
            GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
            return calcActualScenario(file, entry.worldId);
        }

        void rememberScenario(s32 worldId, s32 scenario) {
        }

        void rememberCurrentFileScenario(GameDataFile* file) {
        }

        void writeRaceType(GameDataFile* file, GameDataFile::RaceType raceType) {
            *reinterpret_cast<GameDataFile::RaceType*>(reinterpret_cast<char*>(file) + 0xA44) = raceType;
        }

        StageSceneStateYukimaruRace* getStageSceneYukimaruRaceState(al::Scene* scene) {
            if (!scene)
                return nullptr;
            return *reinterpret_cast<StageSceneStateYukimaruRace**>(
                reinterpret_cast<char*>(scene) + cStageSceneStateYukimaruRaceOffset);
        }

        StageSceneStateYukimaruRaceTutorial* getStageSceneYukimaruRaceTutorialState(al::Scene* scene) {
            if (!scene)
                return nullptr;
            return *reinterpret_cast<StageSceneStateYukimaruRaceTutorial**>(
                reinterpret_cast<char*>(scene) + cStageSceneStateYukimaruRaceTutorialOffset);
        }

        StageSceneStateRaceManRace* getStageSceneRaceManRaceState(al::Scene* scene) {
            if (!scene)
                return nullptr;
            return *reinterpret_cast<StageSceneStateRaceManRace**>(
                reinterpret_cast<char*>(scene) + cStageSceneStateRaceManRaceOffset);
        }

        GameDataHolder* getStageSceneGameDataHolder(al::Scene* scene) {
            if (!scene)
                return nullptr;
            return *reinterpret_cast<GameDataHolder**>(
                reinterpret_cast<char*>(scene) + cStageSceneGameDataHolderOffset);
        }

        al::IUseNerve* asUseNerve(al::Scene* scene) {
            return reinterpret_cast<al::IUseNerve*>(scene);
        }

        const al::Nerve* mainOffsetNerve(uintptr_t offset) {
            return reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + offset);
        }

        bool isKfrStageScenario(const char* stageName, s32 scenario) {
            const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
            return entry && scenario == getKfrScenario(entry->worldId);
        }

        // Diagnostic: snapshot the raw per-world scenario + main-scenario arrays (the same
        // mScenarioNo that calcActualScenario reads). Change-gated across all call sites, so
        // the FIRST hook tag that prints a changed slot is the one whose orig() wrote it -
        // this is how we find what stamps a departing kingdom's slot (e.g. Cascade slot 1
        // jumping to 7 on the Cascade->Sand travel).
        void debugScenarioArray(GameDataFile* file, const char* tag) {
            if (!file)
                return;
            auto sc = file->getScenarioNumArr();
            auto mn = file->getMainScenarioNumArr();
            if (!sc.begin() || !mn.begin())
                return;

            static s32 sLast[7] = {};
            static bool sHas = false;
            bool changed = !sHas;
            for (s32 i = 0; i < 7 && !changed; i++)
                if (sLast[i] != sc[i])
                    changed = true;
            if (!changed)
                return;

            Randomizer::logPush("scn[%s] sc=%d,%d,%d,%d,%d,%d,%d main=%d,%d,%d,%d,%d,%d,%d cur=%s",
                tag ? tag : "?",
                sc[0], sc[1], sc[2], sc[3], sc[4], sc[5], sc[6],
                mn[0], mn[1], mn[2], mn[3], mn[4], mn[5], mn[6],
                file->getStageNameCurrent() ? file->getStageNameCurrent() : "?");

            for (s32 i = 0; i < 7; i++)
                sLast[i] = sc[i];
            sHas = true;
        }

        static HkTrampoline<void, GameDataHolderWriter>
            restartStageHook = hk::hook::trampoline([](GameDataHolderWriter writer) -> void {
                GameDataFile* file = writer->getGameDataFile();
                debugScenarioArray(file, "restartStage-pre");
                syncScenarioCache(file);
                rememberCurrentFileScenario(file);
                rememberPendingRestartScenario(file);

                restartStageHook.orig(writer);
                debugScenarioArray(file, "restartStage-post");

                if (file)
                    tryApplyPendingRestartScenario(file, file->getChangeStageInfo());
            });

        static HkTrampoline<s32, GameDataHolderAccessor>
            calcNextScenarioNoHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> s32 {
                const s32 scenario = calcNextScenarioNoHook.orig(accessor);

                const char* stageName = GameDataFunction::getNextStageName(accessor);
                if (!stageName || !stageName[0])
                    stageName = GameDataFunction::getCurrentStageName(accessor);

                const s32 normalized = normalizeCalculatedScenario(accessor, stageName, scenario);
                if (normalized > 0 && normalized != scenario)
                    Randomizer::logPush("Scenario calc patch: %s %d -> %d",
                        stageName ? stageName : "???", scenario, normalized);

                debugScenarioArray(getActiveFile(accessor), "calcNextScenario");
                return normalized;
            });

        // Loading the high scenario materializes both linked moons in these
        // shuffled rooms, but the NPC/event flow performs a separate
        // GameDataFunction::isOpenMoonRock check before offering prize two.
        // That native check reads the host kingdom after entrance
        // randomization, so it can incorrectly take the "no more prizes"
        // branch. These are precisely the rooms promoted by
        // getRequiredScenarioForRandomizedMultiPrizeSubarea; make their
        // runtime prize selection agree with the promoted map scenario.
        static HkTrampoline<bool, GameDataHolderAccessor>
            isOpenMoonRockForMultiPrizeSubareaHook = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> bool {
                const char* stageName = accessor.mData
                    ? GameDataFunction::getCurrentStageName(accessor)
                    : nullptr;
                if (ScenarioPatcher::getRequiredScenarioForRandomizedMultiPrizeSubarea(
                        stageName)
                    > 0)
                    return true;
                return isOpenMoonRockForMultiPrizeSubareaHook.orig(accessor);
            });

        static HkTrampoline<void, GameDataHolder*, const ChangeStageInfo*, s32>
            gameDataHolderChangeNextStageHook = hk::hook::trampoline([](GameDataHolder* holder, const ChangeStageInfo* info, s32 raceType) -> void {
                if (!holder || !info || info->isEmpty()) {
                    gameDataHolderChangeNextStageHook.orig(holder, info, raceType);
                    return;
                }

                GameDataFile* file = holder->getGameDataFile();
                debugScenarioArray(file, "changeNextStage-pre");
                if (raceType == GameDataFile::RaceType_Flag) {
                    syncScenarioCache(file);
                    rememberCurrentFileScenario(file);
                    gameDataHolderChangeNextStageHook.orig(holder, info, raceType);
                    debugScenarioArray(file, "changeNextStage-post-flag");
                    return;
                }

                ChangeStageInfo patched(holder, info->mChangeStageId.cstr(), info->mChangeStageName.cstr(),
                    info->mIsReturn, info->mScenarioNo, info->mSubScenarioType);
                patched.mPlacementString.copy(info->mPlacementString.cstr());
                patched.mWipeType.copy(info->mWipeType.cstr());
                patched.mHintPriority = info->mHintPriority;

                ScenarioPatcher::normalizeChangeStageInfo(file, &patched, raceType);

                // Prevent the game from stamping the arriving kingdom's scenario into the
                // departing kingdom's save slot during the transition.
                const WorldMeta::Entry* destEntry = findEntryForStageName(patched.mChangeStageName.cstr());
                s32 destWorldId = destEntry ? destEntry->worldId : -1;
                s32 currentWorldId = file->getCurrentWorldId();
                s32 savedScenario = currentWorldId >= 0 && currentWorldId < 0x10 ? file->getScenarioNumArr()[currentWorldId] : -1;

                gameDataHolderChangeNextStageHook.orig(holder, &patched, raceType);

                if (currentWorldId >= 0 && savedScenario >= 0 && currentWorldId != destWorldId) {
                    file->getScenarioNumArr()[currentWorldId] = savedScenario;
                }

                debugScenarioArray(file, "changeNextStage-post");
            });

        static HkTrampoline<bool, WorldResourceLoader*, s32, s32>
            requestLoadWorldHomeStageResourceHook = hk::hook::trampoline([](WorldResourceLoader* loader, s32 loadWorldId, s32 scenario) -> bool {
                bool isStartRedirect = false;
                if (loader && loader->mDataHolder && loader->mDataHolder->mWorldList && loadWorldId >= 0) {
                    const s32 worldCount = loader->mDataHolder->mWorldList->mWorldList.size();
                    if (loadWorldId < worldCount) {
                        const WorldListEntry* world = loader->mDataHolder->mWorldList->mWorldList[loadWorldId];
                        const char* stageName = world ? world->mainStageName : nullptr;

                        const char* forcedStageName = nullptr;
                        s32 forcedScenario = -1;
                        if (stageName && std::strcmp(stageName, WorldMeta::getByWorldId(WorldMeta::WorldId_Cap)->mainStage) == 0 && ProgressionHooks::getForcedStartStage(GameDataHolderAccessor(loader->mDataHolder), &forcedStageName, &forcedScenario, nullptr) && forcedStageName && std::strcmp(forcedStageName, stageName) != 0) {
                            const s32 forcedWorldId = loader->mDataHolder->mWorldList->tryFindWorldIndexByStageName(forcedStageName);
                            if (forcedWorldId >= 0 && forcedWorldId < worldCount) {
                                Randomizer::logPush("Start resource redirect: %s -> %s",
                                    stageName ? stageName : "???", forcedStageName);
                                loadWorldId = forcedWorldId;
                                stageName = forcedStageName;
                                isStartRedirect = true;
                                if (forcedScenario > 0)
                                    scenario = forcedScenario;
                            }
                        }

                        const s32 normalized = normalizeCalculatedScenario(GameDataHolderAccessor(loader->mDataHolder), stageName, scenario);
                        if (normalized > 0 && normalized != scenario) {
                            Randomizer::logPush("Scenario resource patch: %s %d -> %d",
                                stageName ? stageName : "???", scenario, normalized);
                            scenario = normalized;
                        }
                    }
                }

                GameDataFile* resFile = (loader && loader->mDataHolder) ? loader->mDataHolder->getGameDataFile() : nullptr;

                // Entrance rando routes into sub-areas that WorldList still
                // attributes to their vanilla home kingdom, which makes
                // exeLoadStage swap the whole world resource set out and back
                // for a stage that needs a fraction of it. When the resident set
                // can host the sub-area instead, answer "no load needed" so the
                // sequence never enters the exeLoadWorldResource wait at all.
                // getNextStageName dereferences the playing file unguarded, so
                // it is only safe once resFile has proven to be there.
                const char* destinationStage = resFile
                    ? GameDataFunction::getNextStageName(GameDataHolderAccessor(loader->mDataHolder))
                    : nullptr;
                s32 destinationWorldId = -1;
                if (destinationStage && destinationStage[0] && loader->mDataHolder->mWorldList)
                    destinationWorldId = loader->mDataHolder->mWorldList->tryFindWorldIndexByStageName(destinationStage);
                if (SubAreaResourceLoad::shouldKeepResidentWorld(loader, loadWorldId, destinationStage, destinationWorldId, isStartRedirect))
                    return false;

                debugScenarioArray(resFile, "loadWorldRes-pre");
                const bool result = requestLoadWorldHomeStageResourceHook.orig(loader, loadWorldId, scenario);
                debugScenarioArray(resFile, "loadWorldRes-post");
                return result;
            });

        s32 normalizeScenario(GameDataHolderAccessor accessor, const WorldMeta::Entry& entry, s32 requested, bool isKfr, bool authoritative) {
            const s32 worldId = entry.worldId;

            // Debug "Scenario Override" menu: force this kingdom's scenario outright,
            // ahead of every other rule below. Every scenario decision in the game
            // (transitions, per-frame calc, resource preloads) funnels through here,
            // so this one check covers all of them uniformly.
            if (worldId == Randomizer::getScenarioOverrideWorldId()) {
                const s32 overrideScenario = Randomizer::getScenarioOverrideValue();
                if (overrideScenario > 0)
                    return overrideScenario;
            }

            // The audit profile is synthetic: it deliberately has no collected
            // moons, so the ordinary physical-progress resolver would collapse
            // every kingdom back to its opening story scenario on the next
            // boot. Preserve the native after-ending scenario table for this
            // marker; audit-owned case overrides above still win for a timed
            // case reload.
            GameDataFile* auditFile = accessor.mData
                ? accessor.mData->getGameDataFile()
                : nullptr;
            GameDataHolder* auditHolder = auditFile
                ? auditFile->getGameDataHolder()
                : nullptr;
            WorldList* auditWorldList = auditHolder
                ? auditHolder->getWorldList()
                : nullptr;
            if (auditFile && auditWorldList
                && auditFile->customIsExistInHackDictionary(kAuditProfileMarker)) {
                const s32 afterEnding = getAfterEndingScenarioNo(auditWorldList, worldId);
                if (afterEnding > 0)
                    return afterEnding;
            }

            // Dark Side has no story progression in rando — always load scenario 2.
            if (worldId == WorldMeta::WorldId_Dark)
                return 2;

            const s32 kfrScenario = getKfrScenario(worldId);
            const s32 actualScenario = getRememberedScenario(accessor, entry);

            if (requested <= 0)
                requested = actualScenario;

            if (requested == LD::sMoonRockScenarioByKingdom[worldId])
                return requested;

            if (requested == kfrScenario) {
                if (isKfr)
                    return requested;
                return actualScenario;
            }

            if (isMorningMetro(worldId, requested) || isBruncheon(worldId, requested))
                return actualScenario;

            if (worldId == WorldMeta::WorldId_Mushroom)
                return 2;

            if (isFestivalRevisit(worldId, requested))
                return requested;

            // Metro festival exception: scenario 3 is the scripted festival that
            // begins after the Power Plant beat ("Powering Up the Station") + Pauline.
            // The game only requests scenario 3 once that story state is reached, so
            // honor the saved-2 -> 3 transition instead of clobbering it back to the
            // remembered 2 (which would lock the player out of the festival and the
            // rest of Metro's progression). Mirrors the logic-side scenario exception
            // in randomizer_logic.py / MoonRando (SCRIPTED_SCENARIO_ADVANCE).
            if (worldId == WorldMeta::WorldId_Metro && requested == 3 && actualScenario == 2)
                return requested;

            return actualScenario;
        }

        bool isYukimaruRaceCircuitStage(const char* stageName) {
            if (!stageName)
                return false;
            return isSameString(stageName, "SnowWorldRace000Stage")
                || isSameString(stageName, "SnowWorldRace001Stage")
                || isSameString(stageName, "SnowWorldRaceExStage")
                || isSameString(stageName, "SnowWorldRaceHardExStage");
        }

    } // namespace

    s32 getRequiredScenarioForRandomizedMultiPrizeSubarea(const char* stageName) {
        if (!stageName)
            return -1;
        if (std::strcmp(stageName, "IceWalkerExStage") == 0)
            return 4; // Koopa Walking in Snow
        if (std::strcmp(stageName, "FukuwaraiKuriboStage") == 0)
            return 4; // Picture Match: Goomba
        if (std::strcmp(stageName, "FukuwaraiMarioStage") == 0)
            return 3; // Picture Match: Mario
        if (std::strcmp(stageName, "CityPeopleRoadStage") == 0)
            return 10; // Crowded Alleyway
        return -1;
    }

    s32 normalizeScenarioForStage(GameDataHolderAccessor accessor, const char* stageName, s32 requestedScenario, bool isKfr) {
        if (MoonRequirement::isMoonChapelStage(stageName))
            return 1;

        const s32 requiredSubareaScenario = getRequiredScenarioForRandomizedMultiPrizeSubarea(stageName);
        if (requiredSubareaScenario > 0)
            return requiredSubareaScenario;

        if (isUnpatchedSubArea(stageName))
            return requestedScenario;

        const WorldMeta::Entry* entry = findEntryForStageName(stageName);
        if (!entry)
            return requestedScenario;

        syncScenarioCache(getActiveFile(accessor));
        // World-resource loading is incidental — entering a sub-area loads that stage's whole
        // world, including its home (main) stage. That load must not promote or store the
        // world's scenario, or a hosted foreign sub-area silently advances its real kingdom.
        return normalizeScenario(accessor, *entry, requestedScenario, isKfr, /*authoritative=*/false);
    }

    void normalizeChangeStageInfo(GameDataFile* file, ChangeStageInfo* info, s32 raceType) {
        if (!info || info->isEmpty())
            return;

        syncScenarioCache(file);
        rememberCurrentFileScenario(file);

        if (MoonRequirement::isMoonChapelStage(info->mChangeStageName.cstr())) {
            const s32 original = info->mScenarioNo;
            info->mScenarioNo = 1;
            if (original != 1)
                Randomizer::logPush("Scenario patch: %s %d -> 1",
                    info->mChangeStageName.cstr(), original);
            return;
        }

        const s32 requiredSubareaScenario = getRequiredScenarioForRandomizedMultiPrizeSubarea(info->mChangeStageName.cstr());
        if (requiredSubareaScenario > 0) {
            const s32 original = info->mScenarioNo;
            info->mScenarioNo = requiredSubareaScenario;
            if (original != requiredSubareaScenario)
                Randomizer::logPush("Scenario patch multi-prize: %s %d -> %d",
                    info->mChangeStageName.cstr(), original, requiredSubareaScenario);
            return;
        }

        // Leave randomizable sub-areas on whatever scenario the game requested.
        if (isUnpatchedSubArea(info->mChangeStageName.cstr()))
            return;

        const WorldMeta::Entry* entry = findEntryForStageName(info->mChangeStageName.cstr());
        if (!entry)
            return;

        const s32 original = info->mScenarioNo;
        if (original <= 0 && tryApplyPendingRestartScenario(file, info))
            return;

        GameDataHolderAccessor accessor(file ? file->getGameDataHolder() : nullptr);
        const s32 normalized = normalizeScenario(accessor, *entry, original, isRaceManRace(file, raceType),
            isMainKingdomStage(info->mChangeStageName.cstr()));
        if (normalized <= 0 || normalized == original)
            return;

        info->mScenarioNo = normalized;
        Randomizer::logPush("Scenario patch: %s %d -> %d",
            info->mChangeStageName.cstr(), original, normalized);
    }

    void clearPendingRestart() {
        clearPendingRestartScenario();
    }

    void applyInitialKingdomStates(GameDataFile* file) {
        if (!file)
            return;

        auto& scenarioNumbers = file->getScenarioNumArr();
        auto& mainScenarioNumbers = file->getMainScenarioNumArr();
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const s32 scenario = Randomizer::getInitialKingdomScenario(worldId);
            if (scenario <= 0)
                continue;
            scenarioNumbers[worldId] = scenario;
            mainScenarioNumbers[worldId] = scenario;
            Randomizer::logPush("Initial kingdom state: world=%d scenario=%d",
                worldId, scenario);
        }
    }

    void beforeStageSceneInit(const char* stageName, GameDataHolder* holder) {
        ProgressionHooks::clearGravestoneState();

        GameDataFile* file = holder ? holder->getGameDataFile() : nullptr;
        if (file) {
            const char* cur = file->getStageNameCurrent();
            Randomizer::logPush("beforeStageSceneInit: loading=%s cur=%s raceType=%d",
                stageName ? stageName : "NULL",
                cur ? cur : "NULL",
                (int)file->getRaceType());
        }

        if (!isYukimaruRaceCircuitStage(stageName))
            return;
        if (!file)
            return;
        // Race circuit stages have RaceWatcher in ObjectList, so tryStartRaceStage will
        // find it and set mRaceType=Yukimaru. But if the player arrived without going
        // through the NPC race trigger (entrance rando routing), mRaceType may still be
        // None at that point. Forcing it here ensures StageSceneStateYukimaruRace::tryCreate
        // sees isRaceStartYukimaru=true and creates the race state.
        if (file->getRaceType() == GameDataFile::RaceType_None) {
            Randomizer::logPush("beforeStageSceneInit: forcing mRaceType=Yukimaru for %s", stageName);
            writeRaceType(file, GameDataFile::RaceType_Yukimaru);
        }
    }

    void afterStageSceneInit(al::Scene* scene) {
        GameDataHolder* holder = getStageSceneGameDataHolder(scene);
        GameDataFile* file = holder ? holder->getGameDataFile() : nullptr;
        const char* stageName = file ? file->getStageNameCurrent() : "NULL";
        const s32 scenario = file ? file->getScenarioNo() : -1;
        const s32 raceType = file ? (s32)file->getRaceType() : -1;

        StageSceneStateRaceManRace* kfrState = getStageSceneRaceManRaceState(scene);
        StageSceneStateYukimaruRace* yukiState = getStageSceneYukimaruRaceState(scene);
        StageSceneStateYukimaruRaceTutorial* tutState = getStageSceneYukimaruRaceTutorialState(scene);

        Randomizer::logPush("afterStageSceneInit: stage=%s sc=%d raceType=%d kfr=%d yuki=%d tut=%d",
            stageName, scenario, raceType,
            kfrState ? 1 : 0,
            yukiState ? 1 : 0,
            tutState ? 1 : 0);

        if (kfrState) {
            if (!file)
                return;

            if (!isKfrStageScenario(stageName, scenario))
                return;

            if (file->getRaceType() != GameDataFile::RaceType_Flag)
                writeRaceType(file, GameDataFile::RaceType_Flag);

            const al::Nerve* raceNerve = mainOffsetNerve(cStageSceneNrvRaceManRaceOffset);
            if (!al::isNerve(asUseNerve(scene), raceNerve)) {
                Randomizer::logPush("afterStageSceneInit: forcing KFR nerve for %s/%d", stageName, scenario);
                al::setNerve(asUseNerve(scene), raceNerve);
            }
            return;
        }

        if (!yukiState) {
            if (tutState)
                Randomizer::logPush("afterStageSceneInit: tutorial state active, no normal yuki state");
            return;
        }

        if (!file)
            return;

        if (file->getRaceType() != GameDataFile::RaceType_Yukimaru)
            writeRaceType(file, GameDataFile::RaceType_Yukimaru);

        const al::Nerve* raceNerve = mainOffsetNerve(cStageSceneNrvRaceYukimaruOffset);
        if (!al::isNerve(asUseNerve(scene), raceNerve)) {
            Randomizer::logPush("afterStageSceneInit: forcing Yukimaru race nerve for %s", stageName);
            al::setNerve(asUseNerve(scene), raceNerve);
        }
    }

    void healAllScenarios(GameDataFile* file) {
        if (!file)
            return;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++) {
            const WorldMeta::Entry* entry = WorldMeta::getByWorldId(i);
            if (!entry)
                continue;
            // Use the native/logical worldId here, matching getRememberedScenario
            // and every LD::s*ByKingdom table - not scenarioWorldId, which swaps
            // Wooded/Lake (and Snow/Seaside) and would heal the wrong kingdom's
            // slot from the wrong kingdom's data for that one pair.
            const s32 gameWorldId = entry->worldId;
            if (gameWorldId >= 0 && gameWorldId < 16) {
                const s32 scenario = calcActualScenario(file, gameWorldId);
                file->getScenarioNumArr()[gameWorldId] = scenario;
                sLastNormalScenario[gameWorldId] = scenario;
            }
        }
    }

    void installHooks() {
        restartStageHook.installAtSym<"_ZN16GameDataFunction12restartStageE20GameDataHolderWriter">();
        calcNextScenarioNoHook.installAtSym<"_ZN16GameDataFunction18calcNextScenarioNoE22GameDataHolderAccessor">();
        isOpenMoonRockForMultiPrizeSubareaHook.installAtMainOffset(0x00529210);
        gameDataHolderChangeNextStageHook.installAtSym<"_ZN14GameDataHolder15changeNextStageEPK15ChangeStageInfoi">();
        requestLoadWorldHomeStageResourceHook.installAtSym<"_ZN19WorldResourceLoader33requestLoadWorldHomeStageResourceEii">();
    }

} // namespace ScenarioPatcher
