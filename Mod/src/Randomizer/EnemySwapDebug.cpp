#include "Randomizer/EnemySwapDebug.h"

#if SMOR_FEATURE_CHAOS_ANY

#include "Randomizer/EnemySwapDB.h"
#include "Randomizer/EnemySwapRando.h"
#include "al/Library/Camera/CameraUtil.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Screen/ScreenFunction.h"
#include "imgui.h"

#include <cstring>

namespace EnemySwapDebug {

    namespace DB = Randomizer::EnemySwapDB;

    static al::LiveActor* sActors[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1] = {};
    // Keep the two inspector controls independent. The effective runtime
    // override is object-specific first, then bulk, but changing one control
    // must not overwrite the other control's setting.
    static s16 sBulkOverrides[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1] = {};
    static s16 sObjectOverrides[DB::sPlacementCount > 0 ? DB::sPlacementCount : 1] = {};
    static bool sOverridesInitialized = false;
    static s32 sActiveOverridePlacementCount = 0;
    static bool sObjectIdsVisible = false;
    static bool sInspectorOverlayVisible = false;
    static char sInspectorOverlayPhysicalStage[128] = {};
    static char sInspectorOverlayLogicStage[128] = {};

    static bool strEq(const char* left, const char* right) {
        return left && right && std::strcmp(left, right) == 0;
    }

    static bool stageMatchesRuntimeKey(
        const char* tableStage, const char* runtimeStage) {
        if (!tableStage || !runtimeStage)
            return false;
        if (strEq(tableStage, runtimeStage))
            return true;
        const size_t tableLength = std::strlen(tableStage);
        const size_t runtimeLength = std::strlen(runtimeStage);
        if (tableLength + 3 == runtimeLength
            && std::strncmp(runtimeStage, tableStage, tableLength) == 0
            && std::strcmp(runtimeStage + tableLength, "Map") == 0)
            return true;
        if (runtimeLength + 3 == tableLength
            && std::strncmp(tableStage, runtimeStage, runtimeLength) == 0
            && std::strcmp(tableStage + runtimeLength, "Map") == 0)
            return true;
        return false;
    }

    static bool placementMatchesRuntimeStage(
        const DB::Placement& placement,
        const char* physicalStage, const char* logicStage) {
        // A physical-stage match deliberately wins over the logical
        // fallback: one loaded stage can contain several embedded-zone
        // logical maps.
        return (physicalStage
                   && stageMatchesRuntimeKey(placement.stage, physicalStage))
            || (logicStage && strEq(placement.logicStage, logicStage));
    }

    static void initializeOverrides() {
        if (sOverridesInitialized)
            return;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            sBulkOverrides[i] = -1;
            sObjectOverrides[i] = -1;
        }
        sOverridesInitialized = true;
    }

    static bool placementHasOverride(s32 placementIndex) {
        return sBulkOverrides[placementIndex] >= 0
            || sObjectOverrides[placementIndex] >= 0;
    }

    static void setBulkOverride(s32 placementIndex, s16 entityIndex) {
        const bool wasActive = placementHasOverride(placementIndex);
        sBulkOverrides[placementIndex] = entityIndex;
        const bool isActive = placementHasOverride(placementIndex);
        sActiveOverridePlacementCount += (s32)isActive - (s32)wasActive;
    }

    static void setObjectOverride(s32 placementIndex, s16 entityIndex) {
        const bool wasActive = placementHasOverride(placementIndex);
        sObjectOverrides[placementIndex] = entityIndex;
        const bool isActive = placementHasOverride(placementIndex);
        sActiveOverridePlacementCount += (s32)isActive - (s32)wasActive;
    }

    static bool isInspectorSource(
        const DB::Placement& placement, const char* category = nullptr) {
        if (placement.entity < 0 || placement.entity >= DB::sEntityCount)
            return false;
        const char* placementCategory = DB::sEntities[placement.entity].category;
        // The original Enemy bulk action covered both ordinary enemies and
        // capture actors. Keep that scope intact while adding a separate NPC
        // action.
        if (category) {
            if (strEq(category, "enemy"))
                return strEq(placementCategory, "enemy")
                    || strEq(placementCategory, "capture");
            return strEq(placementCategory, category);
        }
        return strEq(placementCategory, "enemy")
            || strEq(placementCategory, "capture")
            || strEq(placementCategory, "npc");
    }

    static bool isSafeObjectTarget(
        const DB::Placement& placement, s16 entityIndex) {
        if (entityIndex < 0 || entityIndex >= DB::sEntityCount
            || placement.entity < 0 || placement.entity >= DB::sEntityCount)
            return false;

        const DB::Entity& source = DB::sEntities[placement.entity];
        const DB::Entity& target = DB::sEntities[entityIndex];
        if (!target.fullActorTarget)
            return false;

        const bool sourceNpc = strEq(source.category, "npc");
        const bool sourceEnemyOrCapture = strEq(source.category, "enemy")
            || strEq(source.category, "capture");
        const bool targetNpc = strEq(target.category, "npc");
        const bool targetEnemyOrCapture = strEq(target.category, "enemy")
            || strEq(target.category, "capture");

        // NPC sources may use any authored full-actor NPC and the explicitly
        // whitelisted enemy/capture pool. Enemy/capture sources stay limited
        // to that same vetted enemy/capture pool and can never become NPCs.
        return (sourceNpc && (targetNpc || (target.menuTarget && targetEnemyOrCapture)))
            || (sourceEnemyOrCapture && target.menuTarget
                && targetEnemyOrCapture);
    }

    bool isObjectTargetSupported(s32 placementIndex, s16 entityIndex) {
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return false;
        return isSafeObjectTarget(DB::sPlacements[placementIndex], entityIndex);
    }

    void clearLiveActors() {
        for (s32 i = 0; i < DB::sPlacementCount; i++)
            sActors[i] = nullptr;
    }

    void registerActor(al::LiveActor* actor, s32 placementIndex) {
        if (!actor || placementIndex < 0
            || placementIndex >= DB::sPlacementCount
            || !isInspectorSource(DB::sPlacements[placementIndex]))
            return;
        sActors[placementIndex] = actor;
    }

    bool areObjectIdsVisible() {
        return sObjectIdsVisible;
    }

    void setObjectIdsVisible(bool visible) {
        sObjectIdsVisible = visible;
    }

    void setInspectorOverlayContext(
        bool visible, const char* physicalStage, const char* logicStage) {
        sInspectorOverlayVisible = visible;
        std::strncpy(
            sInspectorOverlayPhysicalStage, physicalStage ? physicalStage : "",
            sizeof(sInspectorOverlayPhysicalStage) - 1);
        sInspectorOverlayPhysicalStage[sizeof(sInspectorOverlayPhysicalStage) - 1] = '\0';
        std::strncpy(
            sInspectorOverlayLogicStage, logicStage ? logicStage : "",
            sizeof(sInspectorOverlayLogicStage) - 1);
        sInspectorOverlayLogicStage[sizeof(sInspectorOverlayLogicStage) - 1] = '\0';
    }

    s16 getOverrideEntity(s32 placementIndex) {
        initializeOverrides();
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return -1;
        const s16 objectOverride = sObjectOverrides[placementIndex];
        if (objectOverride >= 0)
            return isObjectTargetSupported(placementIndex, objectOverride)
                ? objectOverride
                : -1;
        const s16 bulkOverride = sBulkOverrides[placementIndex];
        return bulkOverride >= 0
                && isObjectTargetSupported(placementIndex, bulkOverride)
            ? bulkOverride
            : -1;
    }

    s16 getBulkOverrideEntity(s32 placementIndex) {
        initializeOverrides();
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return -1;
        return sBulkOverrides[placementIndex];
    }

    s16 getObjectOverrideEntity(s32 placementIndex) {
        initializeOverrides();
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return -1;
        return sObjectOverrides[placementIndex];
    }

    bool findRuntimeOverride(
        const char* physicalStage, const char* logicStage,
        const char* objId, const char* unitConfigName,
        s16* entityIndex, s32* placementIndex) {
        initializeOverrides();
        // The normal game path has no session inspector overrides. Avoid a
        // full placement-table scan for every actor created during stage
        // loading; this became prohibitive once broad props expanded the DB.
        if (sActiveOverridePlacementCount <= 0
            || (!physicalStage && !logicStage) || !objId || !unitConfigName)
            return false;

        s32 bestIndex = -1;
        s16 bestEntity = -1;
        // Object-specific overrides always outrank bulk overrides. Within
        // either class, an extracted physical-stage match outranks a logical
        // alias match. This gives copied embedded-zone PlacementInfo objects
        // the same precedence as the normal object lookup.
        s32 bestPriority = 0x7FFFFFFF;
        bool ambiguousBestMatch = false;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            const DB::Placement& placement = DB::sPlacements[i];
            if (!strEq(placement.objId, objId)
                || !EnemySwapRando::sourceEntityMatchesUnitConfig(
                    placement.entity, unitConfigName)
                || !isInspectorSource(placement))
                continue;

            const s16 objectOverride = sObjectOverrides[i];
            const s16 bulkOverride = sBulkOverrides[i];
            const s16 activeOverride = objectOverride >= 0
                ? objectOverride
                : bulkOverride;
            if (activeOverride < 0
                || !isObjectTargetSupported(i, activeOverride))
                continue;

            const bool physicalMatch = physicalStage
                && stageMatchesRuntimeKey(placement.stage, physicalStage);
            const bool logicalMatch = logicStage
                && strEq(placement.logicStage, logicStage);
            if (!physicalMatch && !logicalMatch)
                continue;

            const s32 priority = (objectOverride >= 0 ? 0 : 2)
                + (physicalMatch ? 0 : 1);
            if (priority < bestPriority) {
                bestPriority = priority;
                bestIndex = i;
                bestEntity = activeOverride;
                ambiguousBestMatch = false;
            } else if (priority == bestPriority
                && activeOverride != bestEntity) {
                // The runtime only gives us an object ID and source name. If
                // the generated inventory contains two equally specific rows
                // with different committed targets, picking the first row
                // makes one user pair appear to overwrite another. Fail
                // closed and let the overlay/log identify the ambiguity.
                ambiguousBestMatch = true;
            }
        }
        if (ambiguousBestMatch) {
            if (entityIndex)
                *entityIndex = -1;
            if (placementIndex)
                *placementIndex = kAmbiguousRuntimeOverride;
            return false;
        }
        if (bestIndex < 0) {
            // A handful of game-generated PlacementInfo records use a
            // factory alias for UnitConfigName even though their Id and
            // PlacementFileName still identify the authored row. An
            // explicit inspector override is allowed to use that stable
            // object identity as a final fallback, but only when the best
            // matching override is unambiguous. This keeps NPC/enemy rows
            // with reused object IDs fail-closed instead of swapping the
            // wrong actor.
            bool ambiguous = false;
            bestPriority = 0x7FFFFFFF;
            for (s32 i = 0; i < DB::sPlacementCount; i++) {
                const DB::Placement& placement = DB::sPlacements[i];
                if (!strEq(placement.objId, objId)
                    || !isInspectorSource(placement))
                    continue;
                const s16 objectOverride = sObjectOverrides[i];
                const s16 bulkOverride = sBulkOverrides[i];
                const s16 activeOverride = objectOverride >= 0
                    ? objectOverride
                    : bulkOverride;
                if (activeOverride < 0
                    || !isObjectTargetSupported(i, activeOverride))
                    continue;

                const bool physicalMatch = physicalStage
                    && stageMatchesRuntimeKey(placement.stage, physicalStage);
                const bool logicalMatch = logicStage
                    && strEq(placement.logicStage, logicStage);
                if (!physicalMatch && !logicalMatch)
                    continue;

                const s32 priority = (objectOverride >= 0 ? 0 : 2)
                    + (physicalMatch ? 0 : 1);
                if (priority < bestPriority) {
                    bestPriority = priority;
                    bestIndex = i;
                    bestEntity = activeOverride;
                    ambiguous = false;
                } else if (priority == bestPriority
                    && activeOverride != bestEntity) {
                    ambiguous = true;
                }
            }
            if (ambiguous) {
                if (entityIndex)
                    *entityIndex = -1;
                if (placementIndex)
                    *placementIndex = kAmbiguousRuntimeOverride;
                bestIndex = -1;
            }
        }
        if (bestIndex < 0)
            return false;
        if (entityIndex)
            *entityIndex = bestEntity;
        if (placementIndex)
            *placementIndex = bestIndex;
        return true;
    }

    bool setOverrideEntity(s32 placementIndex, s16 entityIndex) {
        initializeOverrides();
        if (placementIndex < 0 || placementIndex >= DB::sPlacementCount)
            return false;
        const DB::Placement& placement = DB::sPlacements[placementIndex];
        if (!isInspectorSource(placement))
            return false;
        if (entityIndex < 0) {
            // The visible inspector is now the only UI for session swaps and
            // its Default choice means vanilla for this exact placement. Do
            // not leave an older hidden bulk value active behind that choice.
            setObjectOverride(placementIndex, -1);
            setBulkOverride(placementIndex, -1);
            return true;
        }
        if (!isObjectTargetSupported(placementIndex, entityIndex))
            return false;
        setObjectOverride(placementIndex, entityIndex);
        return true;
    }

    s32 clearAllOverrides() {
        initializeOverrides();
        s32 cleared = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (sBulkOverrides[i] >= 0 || sObjectOverrides[i] >= 0)
                cleared++;
            sBulkOverrides[i] = -1;
            sObjectOverrides[i] = -1;
        }
        sActiveOverridePlacementCount = 0;
        return cleared;
    }

    s32 clearOverridesForLogicStageCategory(
        const char* logicStage, const char* category) {
        initializeOverrides();
        if (!logicStage || !category)
            return 0;
        s32 cleared = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (sBulkOverrides[i] >= 0
                && strEq(DB::sPlacements[i].logicStage, logicStage)
                && isInspectorSource(DB::sPlacements[i], category)) {
                setBulkOverride(i, -1);
                cleared++;
            }
        }
        return cleared;
    }

    s32 clearOverridesForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category) {
        initializeOverrides();
        if ((!physicalStage && !logicStage) || !category)
            return 0;
        s32 cleared = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (sBulkOverrides[i] >= 0
                && placementMatchesRuntimeStage(
                    DB::sPlacements[i], physicalStage, logicStage)
                && isInspectorSource(DB::sPlacements[i], category)) {
                setBulkOverride(i, -1);
                cleared++;
            }
        }
        return cleared;
    }

    s32 setOverrideAllForLogicStageCategory(
        const char* logicStage, const char* category, s16 entityIndex) {
        initializeOverrides();
        if (!logicStage || !category)
            return 0;
        s32 affected = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (!strEq(DB::sPlacements[i].logicStage, logicStage))
                continue;
            if (!isInspectorSource(DB::sPlacements[i], category))
                continue;
            if (entityIndex < 0) {
                setBulkOverride(i, -1);
                affected++;
            } else if (isObjectTargetSupported(i, entityIndex)) {
                setBulkOverride(i, entityIndex);
                affected++;
            }
        }
        return affected;
    }

    s32 setOverrideAllForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category, s16 entityIndex) {
        initializeOverrides();
        if ((!physicalStage && !logicStage) || !category)
            return 0;
        s32 affected = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (!placementMatchesRuntimeStage(
                    DB::sPlacements[i], physicalStage, logicStage)
                || !isInspectorSource(DB::sPlacements[i], category))
                continue;
            if (entityIndex < 0) {
                setBulkOverride(i, -1);
                affected++;
            } else if (isObjectTargetSupported(i, entityIndex)) {
                setBulkOverride(i, entityIndex);
                affected++;
            }
        }
        return affected;
    }

    bool hasAnyOverrideForLogicStageCategory(
        const char* logicStage, const char* category) {
        initializeOverrides();
        if (!logicStage || !category)
            return false;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (sBulkOverrides[i] >= 0
                && strEq(DB::sPlacements[i].logicStage, logicStage)
                && isInspectorSource(DB::sPlacements[i], category))
                return true;
        }
        return false;
    }

    bool hasAnyOverride() {
        initializeOverrides();
        return sActiveOverridePlacementCount > 0;
    }

    bool hasAnyOverrideForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category) {
        initializeOverrides();
        if ((!physicalStage && !logicStage) || !category)
            return false;
        for (s32 i = 0; i < DB::sPlacementCount; i++)
            if (sBulkOverrides[i] >= 0
                && placementMatchesRuntimeStage(
                    DB::sPlacements[i], physicalStage, logicStage)
                && isInspectorSource(DB::sPlacements[i], category))
                return true;
        return false;
    }

    const char* resolveLogicStage(const char* physicalStage) {
        if (!physicalStage)
            return nullptr;
        for (s32 i = 0; i < DB::sPlacementCount; i++)
            if (stageMatchesRuntimeKey(
                    DB::sPlacements[i].stage, physicalStage))
                return DB::sPlacements[i].logicStage;
        return physicalStage;
    }

    static bool isInFrontOfCamera(
        const al::LiveActor* actor, const sead::Vector3f& worldPos) {
        const sead::Vector3f& cameraPos = al::getCameraPos(actor, 0);
        const sead::Vector3f& cameraAt = al::getCameraAt(actor, 0);
        const sead::Vector3f view(
            cameraAt.x - cameraPos.x,
            cameraAt.y - cameraPos.y,
            cameraAt.z - cameraPos.z);
        const sead::Vector3f target(
            worldPos.x - cameraPos.x,
            worldPos.y - cameraPos.y,
            worldPos.z - cameraPos.z);
        return view.dot(target) > 0.0f;
    }

    void drawObjectIds() {
        if (!sObjectIdsVisible)
            return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        ImDrawList* drawList = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        constexpr f32 fontSize = 20.0f;
        constexpr f32 heightOffset = 140.0f;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            al::LiveActor* actor = sActors[i];
            if (!actor || al::isDead(actor) || al::isClipped(actor))
                continue;
            sead::Vector3f worldPos = al::getTrans(actor);
            worldPos.y += heightOffset;
            if (!isInFrontOfCamera(actor, worldPos))
                continue;

            sead::Vector2f screenPos;
            al::calcScreenPosFromWorldPos(&screenPos, actor, worldPos);
            if (screenPos.x < 0.0f || screenPos.y < 0.0f
                || screenPos.x > io.DisplaySize.x
                || screenPos.y > io.DisplaySize.y)
                continue;

            const char* text = DB::sPlacements[i].objId;
            const ImVec2 size = font->CalcTextSizeA(fontSize, 3.402823e38F, 0.0f, text);
            const ImVec2 pos(screenPos.x - size.x * 0.5f,
                screenPos.y - size.y * 0.5f);
            drawList->AddText(font, fontSize,
                ImVec2(pos.x + 2.0f, pos.y + 2.0f),
                IM_COL32(0, 0, 0, 230), text);
            drawList->AddText(font, fontSize, pos,
                IM_COL32(255, 240, 110, 255), text);
        }
    }

    void drawInspectorOverlay() {
        if (!sInspectorOverlayVisible)
            return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        initializeOverrides();
        constexpr f32 windowWidth = 560.0f;
        constexpr f32 windowHeight = 420.0f;
        constexpr f32 windowMargin = 20.0f;
        const f32 windowX = io.DisplaySize.x > windowWidth + windowMargin
            ? io.DisplaySize.x - windowWidth - windowMargin
            : 0.0f;
        const f32 windowY = io.DisplaySize.y > windowHeight + windowMargin
            ? io.DisplaySize.y - windowHeight - windowMargin
            : 0.0f;
        ImGui::SetNextWindowPos(
            ImVec2(windowX, windowY), ImGuiCond_Always);
        ImGui::SetNextWindowSize(
            ImVec2(windowWidth, windowHeight), ImGuiCond_Always);
        ImGui::Begin(
            "Enemy Swap Active Pairs", nullptr,
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize
                | ImGuiWindowFlags_NoSavedSettings);
        ImGui::Text(
            "Stage: %s",
            sInspectorOverlayPhysicalStage[0]
                ? sInspectorOverlayPhysicalStage
                : "?");
        ImGui::Text(
            "Logic map: %s",
            sInspectorOverlayLogicStage[0]
                ? sInspectorOverlayLogicStage
                : "?");
        ImGui::Separator();

        s32 storedCount = 0;
        s32 activeCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            if (!isInspectorSource(DB::sPlacements[i]))
                continue;
            if (sObjectOverrides[i] >= 0 || sBulkOverrides[i] >= 0)
                storedCount++;
        }
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            const DB::Placement& placement = DB::sPlacements[i];
            if (!placementMatchesRuntimeStage(
                    placement,
                    sInspectorOverlayPhysicalStage[0]
                        ? sInspectorOverlayPhysicalStage
                        : nullptr,
                    sInspectorOverlayLogicStage[0]
                        ? sInspectorOverlayLogicStage
                        : nullptr))
                continue;
            if (!isInspectorSource(placement))
                continue;
            if (sObjectOverrides[i] >= 0 || sBulkOverrides[i] >= 0)
                activeCount++;
        }
        ImGui::Text("Stored pairs: %d", storedCount);
        ImGui::Text("Pairs on this stage: %d", activeCount);

        ImGui::BeginChild("EnemySwapActivePairList", ImVec2(0.0f, 0.0f), true);
        s32 shownCount = 0;
        for (s32 i = 0; i < DB::sPlacementCount; i++) {
            const DB::Placement& placement = DB::sPlacements[i];
            if (!isInspectorSource(placement))
                continue;

            const s16 objectOverride = sObjectOverrides[i];
            const s16 bulkOverride = sBulkOverrides[i];
            const s16 targetIndex = objectOverride >= 0
                ? objectOverride
                : bulkOverride;
            if (targetIndex < 0 || targetIndex >= DB::sEntityCount)
                continue;

            const DB::Entity& source = DB::sEntities[placement.entity];
            const DB::Entity& target = DB::sEntities[targetIndex];
            const char* sourceName = source.friendlyName
                ? source.friendlyName
                : source.name;
            const char* targetName = target.friendlyName
                ? target.friendlyName
                : target.name;
            const bool onStage = placementMatchesRuntimeStage(
                placement,
                sInspectorOverlayPhysicalStage[0]
                    ? sInspectorOverlayPhysicalStage
                    : nullptr,
                sInspectorOverlayLogicStage[0]
                    ? sInspectorOverlayLogicStage
                    : nullptr);
            ImGui::Text(
                "%s -> %s  [%s/%s]  (%s -> %s)",
                placement.objId ? placement.objId : "?",
                targetName ? targetName : "?",
                objectOverride >= 0 ? "object" : "bulk",
                onStage ? "current" : "stored",
                source.name ? source.name : "?",
                target.name ? target.name : "?");
            ImGui::TextDisabled(
                "  source: %s  placement: %d",
                sourceName ? sourceName : "?", i);
            shownCount++;
        }
        if (shownCount == 0)
            ImGui::TextDisabled("(no committed inspector swaps)");
        ImGui::EndChild();
        ImGui::End();
    }

} // namespace EnemySwapDebug

#endif
