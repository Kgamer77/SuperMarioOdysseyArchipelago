#include "Randomizer/WorldMapStatusHooks.h"

#include "Randomizer/KingdomProgress.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/WorldMeta.h"

#include "Sequence/HakoniwaSequence.h"
#include "System/GameSystem.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "gfx/seadColor.h"
#include "hk/hook/Trampoline.h"

#include <cstring>

namespace WorldMapStatusHooks {

    namespace {

        const sead::Color4u8 kColorComplete(255, 255, 255, 255); // white — previously completed
        const sead::Color4u8 kColorFrontier(90, 210, 110, 255); // green — current kingdom
        const sead::Color4u8 kColorVisitedUnlocked(255, 150, 40, 255); // orange — visited, not yet unlocked
        const sead::Color4u8 kColorDefault(255, 255, 255, 255); // untinted text

        GameDataFile* getCurrentGameDataFile() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;

            HakoniwaSequence* sequence = static_cast<HakoniwaSequence*>(system->mSequence);
            GameDataHolderAccessor accessor = sequence->mGameDataHolderAccessor;
            if (!accessor.mData)
                return nullptr;

            return accessor->getGameDataFile();
        }

        // worldId is expected to be the logical WorldMeta::WorldId this Randomizer
        // build treats WorldList indices as (see WorldId logical-vs-game swap notes
        // elsewhere in this codebase). If World Map name text is observed tinting
        // the wrong kingdom in-game, this is the first place to re-check.
        sead::Color4u8 resolveColor(GameDataFile* file, s32 worldId) {
            if (!file || !Randomizer::isVisitedKingdomsMapEnabled())
                return kColorDefault;

            const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, worldId);
            if (entry && entry->completeStatus)
                return kColorComplete;

            const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
            const s32 frontier = KingdomProgress::getFirstIncompleteIndex(file);
            if (list && frontier >= 0 && frontier < KingdomProgress::cKingdomCount
                && list->entries[frontier].kingdomId == worldId) {
                return kColorFrontier;
            }

            // Mushroom Kingdom, Dark Side, and Darker Side are not entries in
            // KingdomProgress, but appear as visited-map extras. Treat their
            // persisted visit bits the same as early-visited route kingdoms.

            if (worldId >= 0 && worldId < 32
                && (Randomizer::getVisitedKingdoms() & (1u << worldId)) != 0) {
                return kColorVisitedUnlocked;
            }

            return kColorDefault;
        }

        void applyColor(al::LayoutActor* actor, const char* paneName, s32 worldId) {
            if (!actor || !paneName)
                return;
            al::setPaneVtxColor(actor, paneName, resolveColor(getCurrentGameDataFile(), worldId));
        }

        // rs::setPaneStringWorldNameForWorldMap / ...WithoutRuby are not present in
        // syms/main.sym; hooked at fixed offsets (ida_context/Scene/
        // StageSceneStateWorldMap.md, IDA 0x7100577f94 / 0x7100577fd4). Both set the
        // "TxtContent" pane on a World Map kingdom-icon layout to that world's name.
        static HkTrampoline<void, al::LayoutActor*, const char*, int> setWorldNameHook
            = hk::hook::trampoline([](al::LayoutActor* actor, const char* paneName, int worldId) -> void {
                  setWorldNameHook.orig(actor, paneName, worldId);
                  applyColor(actor, paneName, worldId);
              });

        static HkTrampoline<void, al::LayoutActor*, const char*, int> setWorldNameWithoutRubyHook
            = hk::hook::trampoline([](al::LayoutActor* actor, const char* paneName, int worldId) -> void {
                  setWorldNameWithoutRubyHook.orig(actor, paneName, worldId);
                  applyColor(actor, paneName, worldId);
              });

        // StageSceneStateWorldMap::appear reads GameDataFunction::getCurrentWorldId
        // to decide which kingdom icon to scroll/highlight as "current" when the
        // world map opens (ida_context/Scene/StageSceneStateWorldMap.md, IDA
        // 0x7100501270). That native value is the game's own mCurrentWorldId,
        // which is only updated by explicit "go to world" travel - it is not
        // re-derived from the stage the player is physically standing in. Shiveria
        // Town is the only Snow Kingdom sub-area whose randomizable entrances
        // (SnowCostumeEx, ShopDoor) can drop the player there directly from an
        // unrelated kingdom, bypassing SnowWorldHomeStage (and the normal world-id
        // update) entirely, so the world map would otherwise keep highlighting
        // whichever kingdom the player last departed from.
        static HkTrampoline<s32, GameDataHolderAccessor> getCurrentWorldIdHook
            = hk::hook::trampoline([](GameDataHolderAccessor accessor) -> s32 {
                  const s32 orig = getCurrentWorldIdHook.orig(accessor);
                  if (!accessor.mData)
                      return orig;
                  const char* stageName = GameDataFunction::getCurrentStageName(accessor);
                  if (stageName && std::strcmp(stageName, "SnowWorldTownStage") == 0)
                      return WorldMeta::WorldId_Snow;
                  return orig;
              });

    } // namespace

    void installHooks() {
        // IDA addresses 0x7100577f94 / 0x7100577fd4 minus the 0x7100000000 main
        // module base (see ida_context/Scene/StageSceneStateWorldMap.md).
        setWorldNameHook.installAtMainOffset(0x577f94);
        setWorldNameWithoutRubyHook.installAtMainOffset(0x577fd4);
        getCurrentWorldIdHook.installAtSym<"_ZN16GameDataFunction17getCurrentWorldIdE22GameDataHolderAccessor">();
    }

} // namespace WorldMapStatusHooks
