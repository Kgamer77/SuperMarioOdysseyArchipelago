#include "Randomizer/SkyboxHooks.h"

#include "Randomizer/Randomizer.h"
#include "Randomizer/SkyboxRando.h"
#include "Randomizer/WorldMeta.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/Draw/GraphicsSystemInfo.h"
#include "al/Library/Resource/Resource.h"
#include "al/Library/Scene/Scene.h"
#include "al/Library/Stage/StageResourceKeeper.h"
#include "al/Library/Stage/StageResourceList.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameSystem.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"
#include "imgui.h"
#include "sead/heap/seadHeap.h"

namespace SkyboxHooks {

    static constexpr s32 kSkyboxCount = WorldMeta::sWorldCount;

    static sead::Heap* sHeap = nullptr;
    static s32 sOverrideIndex = -1;
    static bool sOverrideEnabled = false;
    // "Stabilize Shaders" / "Sky atmosphere only" (default ON, persisted via
    // Randomizer::isStabilizeShadersEnabled). Instead of swapping the whole graphics
    // preset (which re-lights the kingdom — Seaside water/lava blowing out), the
    // native preset loads in full (lighting/HDR/bloom/fog/color-correction stay
    // native) and afterwards ONLY the sky-rendering directors are re-initialised
    // from the borrowed kingdom's resource: the AtmosScatter (the gradient + sun
    // that actually IS the visible sky dome) plus the swapped SkyList scenery.
    // Turning this off restores the old full-preset swap for comparison.
    static bool keepNativeLighting() { return Randomizer::isStabilizeShadersEnabled(); }

    static const WorldMeta::Entry* getOverrideEntry() {
        if (sOverrideIndex < 0 || sOverrideIndex >= kSkyboxCount)
            return nullptr;
        return WorldMeta::getBySkyboxIndex(sOverrideIndex);
    }

    static bool canRemapStage(const char* stageName) {
        return WorldMeta::findByMainStage(stageName) != nullptr;
    }

    // Resolve the skybox stage/scenario for a stage being loaded. Returns true and
    // fills outStage/outScenario when a remap applies; returns false (leaving the
    // caller to use the original stage) otherwise. Centralizes the debug override,
    // the per-kingdom main-stage rando, and the per-sub-area rando so all three
    // hooks stay consistent.
    //
    // affectsLighting marks callers that swap the graphics preset (lighting/HDR/
    // fog), as opposed to the SkyList dome. In "keep native lighting" mode those
    // callers are short-circuited so only the dome is swapped.
    static bool resolveSkybox(const char* stageName, s32 inScenario,
        const char** outStage, s32* outScenario,
        bool affectsLighting) {
        if (affectsLighting && keepNativeLighting())
            return false;

        const bool isMain = canRemapStage(stageName);

        if (sOverrideEnabled) {
            const WorldMeta::Entry* override = getOverrideEntry();
            if (override && isMain) {
                *outStage = override->mainStage;
                *outScenario = override->skyboxScenario;
                return true;
            }
            return false;
        }

        // The remap functions self-gate on the skybox mode, returning the stage
        // unchanged when the current mode doesn't randomize this area type.
        s32 scenario = inScenario;
        const char* remapped = isMain
            ? SkyboxRando::remapWithScenario(stageName, &scenario)
            : SkyboxRando::remapSubAreaWithScenario(stageName, &scenario);

        if (!remapped || al::isEqualString(remapped, stageName))
            return false;

        *outStage = remapped;
        *outScenario = scenario;
        return true;
    }

    void setHeap(sead::Heap* heap) {
        sHeap = heap;
    }

    void drawDebugWindow() {
        ImGui::SetNextWindowSize(ImVec2(340, 360), ImGuiCond_Once);
        ImGui::SetNextWindowPos(ImVec2(20, 540), ImGuiCond_Once);
        ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
        ImGui::Begin("Skybox Test");

        bool keepLighting = Randomizer::isStabilizeShadersEnabled();
        if (ImGui::Checkbox("Stabilize shaders (keep native lighting)", &keepLighting))
            Randomizer::setStabilizeShadersEnabled(keepLighting);
        ImGui::TextDisabled("Re-inits the borrowed AtmosScatter sky on top of");
        ImGui::TextDisabled("native lighting/HDR/fog (no water blowouts).");
        ImGui::Separator();

        ImGui::Checkbox("Override skybox", &sOverrideEnabled);
        if (!sOverrideEnabled) {
            ImGui::TextDisabled("Using randomizer mapping");
        } else {
            ImGui::Separator();
            ImGui::Text("Select kingdom skybox:");
            ImGui::BeginChild("SkyboxList", ImVec2(0, 220), true);
            for (s32 i = 0; i < kSkyboxCount; i++) {
                const WorldMeta::Entry* entry = WorldMeta::getBySkyboxIndex(i);
                if (!entry)
                    continue;
                bool selected = (sOverrideIndex == i);
                if (ImGui::Selectable(entry->skyboxLabel, selected))
                    sOverrideIndex = i;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndChild();

            if (const WorldMeta::Entry* entry = getOverrideEntry()) {
                ImGui::Separator();
                ImGui::Text("Active: %s", entry->skyboxLabel);
                ImGui::Text("Stage:  %s", entry->mainStage);
                ImGui::Text("Scenario: %d", entry->skyboxScenario);
            } else {
                ImGui::TextDisabled("(none selected)");
            }
        }
        ImGui::End();
    }

    static HkTrampoline<void, al::Scene*, const char*, int>
        initGraphicsSystemInfoHook = hk::hook::trampoline([](al::Scene* scene, const char* stageName, int maybeScenario) -> void {
            const char* remappedStage = stageName;
            s32 remappedScenario = maybeScenario;
            if (resolveSkybox(stageName, maybeScenario, &remappedStage, &remappedScenario, /*affectsLighting=*/true)) {
                initGraphicsSystemInfoHook.orig(scene, remappedStage, remappedScenario);
            } else {
                initGraphicsSystemInfoHook.orig(scene, stageName, maybeScenario);
            }
        });

    static HkTrampoline<void, void*, const char*, int, const char*>
        stageResourceListCtorHook = hk::hook::trampoline([](void* thisPtr, const char* stageName, int maybeScenario, const char* levelFile) -> void {
            if (al::isEqualString(levelFile, "Design") || al::isEqualString(levelFile, "StageMap")) {
                const char* remappedStage = stageName;
                s32 remappedScenario = maybeScenario;
                if (resolveSkybox(stageName, maybeScenario, &remappedStage, &remappedScenario, /*affectsLighting=*/true)) {
                    stageResourceListCtorHook.orig(thisPtr, remappedStage, remappedScenario, levelFile);
                    return;
                }
            }
            stageResourceListCtorHook.orig(thisPtr, stageName, maybeScenario, levelFile);
        });

    static HkTrampoline<void, const al::StageInfo*, const char*, const al::ActorInitInfo*>
        initPlacementByStageInfoHook = hk::hook::trampoline(
            [](const al::StageInfo* stageInfo, const char* listName, const al::ActorInitInfo* actorInfo) -> void {
                if (!al::isEqualString(listName, "SkyList")) {
                    initPlacementByStageInfoHook.orig(stageInfo, listName, actorInfo);
                    return;
                }

                const char* curStage = GameDataFunction::getCurrentStageName(
                    ((HakoniwaSequence*)GameSystemFunction::getGameSystem()->mSequence)->mGameDataHolderAccessor);

                const char* skyStage = curStage;
                s32 skyScenario = -1;
                // affectsLighting=false: the dome is always swapped, even in
                // "keep native lighting" mode (that is the whole point of option 1).
                if (!resolveSkybox(curStage, -1, &skyStage, &skyScenario, /*affectsLighting=*/false))
                    return;

                if (!skyStage || al::isEqualString(skyStage, curStage) || !sHeap)
                    return;

                al::StageResourceKeeper* keeper = new (sHeap) al::StageResourceKeeper();
                keeper->initAndLoadResource(skyStage, skyScenario);
                al::StageResourceList* list = nullptr;

                uintptr_t keeperAddr = reinterpret_cast<uintptr_t>(keeper);
                uintptr_t firstPtr = *reinterpret_cast<uintptr_t*>(keeperAddr);

                if (firstPtr)
                    list = *reinterpret_cast<al::StageResourceList**>(firstPtr);

                if (list) {
                    const al::StageInfo* remappedInfo = list->getStageInfo(0);

                    if (remappedInfo) {
                        initPlacementByStageInfoHook.orig(remappedInfo, listName, actorInfo);
                        sHeap->free(keeper);
                        return;
                    }
                }
                sHeap->free(keeper);
            });

    // ── "Sky atmosphere only" path ───────────────────────────────────────────────
    // al::AtmosScatter::initStageResource is not in syms/main.sym, so call it by
    // main-module offset (see ida_context/.../AtmosScatter.md @ 0x9f3c48). It copies
    // the params out of the resource into its own fields, so the resource it reads
    // from can be freed immediately afterwards.
    static void reinitAtmosScatterFromResource(al::GraphicsSystemInfo* gsi,
        const al::Resource* resource,
        const char* stageName, const char* preset) {
        if (!gsi || !gsi->mAtmosScatter || !resource)
            return;
        using Fn = float (*)(void*, const al::Resource*, const char*, const char*);
        auto fn = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + 0x9f3c48);
        fn(gsi->mAtmosScatter, resource, stageName, preset);
    }

    // Load a borrowed kingdom's Design archive (where the graphics/atmos params live)
    // and return its al::Resource. *keeperOut owns the load; the caller frees it once
    // the params have been copied out. Returns nullptr (and frees) on any failure.
    static const al::Resource* loadDesignResource(const char* stage, s32 scenario,
        al::StageResourceKeeper** keeperOut) {
        *keeperOut = nullptr;
        if (!stage || !sHeap)
            return nullptr;

        al::StageResourceKeeper* keeper = new (sHeap) al::StageResourceKeeper();
        keeper->initAndLoadResource(stage, scenario);

        al::StageResourceList* list = keeper->getDesignStageInfo();
        const al::StageInfo* info = list ? list->getStageInfo(0) : nullptr;
        // The al::StageInfo's first member is its const al::Resource* (see how
        // al::initGraphicsSystemInfo pulls the resource out of the stage info).
        const al::Resource* resource = info ? *reinterpret_cast<const al::Resource* const*>(info) : nullptr;

        if (!resource) {
            sHeap->free(keeper);
            return nullptr;
        }
        *keeperOut = keeper;
        return resource;
    }

    // al::GraphicsSystemInfo::initStageResource(resource, name, preset) is the call
    // that distributes the stage resource to every graphics director (AtmosScatter,
    // CubeMap, GraphicsArea=lighting, …). Not in the sym file -> install by offset.
    // We let it run natively (everything native) and then re-init only the sky's
    // AtmosScatter from the borrowed kingdom, so lighting stays native.
    static HkTrampoline<void*, al::GraphicsSystemInfo*, const al::Resource*, const char*, const char*>
        gsiInitStageResourceHook = hk::hook::trampoline(
            [](al::GraphicsSystemInfo* gsi, const al::Resource* resource, const char* name,
                const char* preset) -> void* {
                void* result = gsiInitStageResourceHook.orig(gsi, resource, name, preset);

                // Only relevant in "sky atmosphere only" mode; the full-preset swap is
                // handled by the other two hooks when this is off. Guard re-entrancy in
                // case loading the borrowed resource triggers another graphics init.
                static bool sReentry = false;
                if (!keepNativeLighting() || sReentry || !name)
                    return result;

                const char* foreignStage = name;
                s32 foreignScenario = -1;
                if (!resolveSkybox(name, -1, &foreignStage, &foreignScenario, /*affectsLighting=*/false))
                    return result;
                if (!foreignStage || al::isEqualString(foreignStage, name))
                    return result;

                sReentry = true;
                al::StageResourceKeeper* keeper = nullptr;
                const al::Resource* foreignRes = loadDesignResource(foreignStage, foreignScenario, &keeper);
                if (foreignRes) {
                    reinitAtmosScatterFromResource(gsi, foreignRes, foreignStage, preset);
                    Randomizer::logPush("skybox atmos swap %s -> %s (scenario %d) atmos=%p",
                        name, foreignStage, foreignScenario, gsi->mAtmosScatter);
                } else {
                    Randomizer::logPush("skybox atmos swap %s -> %s: design resource load failed",
                        name, foreignStage);
                }
                if (keeper && sHeap)
                    sHeap->free(keeper);
                sReentry = false;
                return result;
            });

    void installHooks() {
        initPlacementByStageInfoHook.installAtSym<"_ZN2al24initPlacementByStageInfoEPKNS_9StageInfoEPKcRKNS_13ActorInitInfoE">();
        initGraphicsSystemInfoHook.installAtSym<"_ZN2al22initGraphicsSystemInfoEPNS_5SceneEPKci">();
        stageResourceListCtorHook.installAtSym<"_ZN2al17StageResourceListC2EPKciS2_">();
        // al::GraphicsSystemInfo::initStageResource is not in syms/main.sym.
        gsiInitStageResourceHook.installAtMainOffset(0x879100);
    }

} // namespace SkyboxHooks
