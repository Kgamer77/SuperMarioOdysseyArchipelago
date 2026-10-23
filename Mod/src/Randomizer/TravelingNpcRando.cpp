#include "Randomizer/TravelingNpcRando.h"

#include "al/Library/LiveActor/LiveActor.h"
#include "game/Npc/WorldTravelingNpc.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderAccessor.h"

#include "hk/hook/Trampoline.h"

#include <cstring>

namespace TravelingNpcRando {
    namespace {

        struct TravelMoonEntry {
            const char* stage;
            const char* objId;
            const char* activeEntry;
        };

        constexpr const char* kDoneEntry = "RandoTravelingNpcDone";

        constexpr TravelMoonEntry sTravelMoonEntries[] = {
            { "CityWorldHomeStage", "obj12578", "CityWorld0" },
            { "WaterfallWorldHomeStage", "obj2423", "WaterfallWorld0" },
            { "LavaWorldHomeStage", "obj5666", "LavaWorld0" },
            { "MoonWorldHomeStage", "obj944", "MoonWorld0" },
            { "PeachWorldHomeStage", "obj734", "PeachCastleWorld0" },
            { "SandWorldHomeStage", "obj4277", "Last" },
        };

        const TravelMoonEntry* findCurrentEntry(const WorldTravelingNpc* actor) {
            if (!actor)
                return nullptr;

            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return nullptr;

            const char* stageName = GameDataFunction::getCurrentStageName(accessor);
            if (!stageName)
                return nullptr;

            for (const TravelMoonEntry& entry : sTravelMoonEntries) {
                if (std::strcmp(entry.stage, stageName) == 0)
                    return &entry;
            }
            return nullptr;
        }

        bool isTravelMoonCollected(const WorldTravelingNpc* actor, const TravelMoonEntry& entry) {
            GameDataHolderAccessor accessor(actor);
            if (!accessor.mData)
                return false;
            return GameDataFunction::isGotShine(accessor, entry.stage, entry.objId);
        }

        static HkTrampoline<bool, const WorldTravelingNpc*>
            isFirstWorldTravelingStatusHook = hk::hook::trampoline([](const WorldTravelingNpc* actor) -> bool {
                if (findCurrentEntry(actor))
                    return false;
                return isFirstWorldTravelingStatusHook.orig(actor);
            });

        static HkTrampoline<const char*, const WorldTravelingNpc*>
            getWorldTravelingStatusHook = hk::hook::trampoline([](const WorldTravelingNpc* actor) -> const char* {
                if (const TravelMoonEntry* entry = findCurrentEntry(actor))
                    return isTravelMoonCollected(actor, *entry) ? kDoneEntry : entry->activeEntry;
                return getWorldTravelingStatusHook.orig(actor);
            });

        static HkTrampoline<void, const WorldTravelingNpc*, const char*>
            saveWorldTravelingStatusHook = hk::hook::trampoline([](const WorldTravelingNpc* actor, const char* status) -> void {
                if (findCurrentEntry(actor))
                    return;
                saveWorldTravelingStatusHook.orig(actor, status);
            });

    } // namespace

    void installHooks() {
        isFirstWorldTravelingStatusHook.installAtMainOffset(0x00529814);
        getWorldTravelingStatusHook.installAtMainOffset(0x00529890);
        saveWorldTravelingStatusHook.installAtMainOffset(0x00529850);
    }

} // namespace TravelingNpcRando
