#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FeatureFlags.h"

namespace al {
    class LiveActor;
}

#if SMOR_FEATURE_CHAOS_ANY
namespace EnemySwapDebug {

    // findRuntimeOverride() writes this placement index when the live
    // identity matches more than one committed pair at the same priority.
    // The caller must reject the replacement rather than fall back to the
    // generated row or the first object-ID match.
    static constexpr s32 kAmbiguousRuntimeOverride = -2;

    // Session-only state. Overrides are indexed by the generated placement
    // table and are intentionally never serialized into save data.
    void clearLiveActors();
    void registerActor(al::LiveActor* actor, s32 placementIndex);

    bool areObjectIdsVisible();
    void setObjectIdsVisible(bool visible);
    void drawObjectIds();

    // The inspector is intentionally backed by the same session table that
    // the placement hook reads.  This overlay makes it possible to tell
    // whether a pair was not committed, was committed to a different
    // placement row, or was later rejected by runtime identity matching.
    void setInspectorOverlayContext(
        bool visible, const char* physicalStage, const char* logicStage);
    void drawInspectorOverlay();

    // Effective runtime override. Object-specific overrides take precedence
    // over the bulk override for the same placement.
    s16 getOverrideEntity(s32 placementIndex);
    s16 getBulkOverrideEntity(s32 placementIndex);
    s16 getObjectOverrideEntity(s32 placementIndex);
    // Resolve a session override directly from the identity exposed by the
    // live placement factory. This is intentionally independent of the
    // generated placement index: embedded-zone loaders can copy a
    // PlacementInfo while changing its physical stage key, and duplicate
    // object IDs can otherwise make a bulk override look as if it vanished.
    bool findRuntimeOverride(
        const char* physicalStage, const char* logicStage,
        const char* objId, const char* unitConfigName,
        s16* entityIndex, s32* placementIndex);
    // Shared object-pair policy used by both the inspector UI and runtime
    // override lookup. NPC sources may use vetted NPCs or whitelisted
    // enemy/capture targets; enemy/capture sources remain enemy/capture-only.
    bool isObjectTargetSupported(s32 placementIndex, s16 entityIndex);
    // Set an object-specific override. A negative entity selects Default and
    // clears both visible/session override layers for that placement.
    bool setOverrideEntity(s32 placementIndex, s16 entityIndex);
    // Clear every session-local inspector override. This does not alter the
    // generated randomizer assignment.
    s32 clearAllOverrides();

    // Bulk overrides are scoped to a source category. "enemy" includes both
    // ordinary enemies and capture actors; "npc" affects residents only.
    // entityIndex -1 clears the selected category. Returns placements affected.
    s32 clearOverridesForLogicStageCategory(
        const char* logicStage, const char* category);
    s32 setOverrideAllForLogicStageCategory(
        const char* logicStage, const char* category, s16 entityIndex);
    bool hasAnyOverrideForLogicStageCategory(
        const char* logicStage, const char* category);

    // The memory audit must not inherit an inspector experiment from the
    // current session.  This checks both bulk and object-specific controls.
    bool hasAnyOverride();

    // The inspector's current stage can contain several extracted placement
    // files (embedded zones) and can be exposed by the game with a StageMap
    // suffix. These variants apply the bulk setting to every matching
    // physical-stage row, with the logical stage as a fallback.
    s32 clearOverridesForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category);
    s32 setOverrideAllForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category, s16 entityIndex);
    bool hasAnyOverrideForRuntimeStageCategory(
        const char* physicalStage, const char* logicStage,
        const char* category);

    // Used by the inspector to turn the current physical stage into the
    // placement inventory's logical-map key.
    const char* resolveLogicStage(const char* physicalStage);

} // namespace EnemySwapDebug
#else
namespace EnemySwapDebug {
    static constexpr s32 kAmbiguousRuntimeOverride = -2;
    inline void clearLiveActors() { }
    inline void registerActor(al::LiveActor*, s32) { }
    inline bool areObjectIdsVisible() { return false; }
    inline void setObjectIdsVisible(bool) { }
    inline void drawObjectIds() { }
    inline void setInspectorOverlayContext(
        bool, const char*, const char*) { }
    inline void drawInspectorOverlay() { }
    inline s16 getOverrideEntity(s32) { return -1; }
    inline s16 getBulkOverrideEntity(s32) { return -1; }
    inline s16 getObjectOverrideEntity(s32) { return -1; }
    inline bool findRuntimeOverride(
        const char*, const char*, const char*, const char*, s16*, s32*) {
        return false;
    }
    inline bool isObjectTargetSupported(s32, s16) { return false; }
    inline bool setOverrideEntity(s32, s16) { return false; }
    inline s32 clearAllOverrides() { return 0; }
    inline s32 clearOverridesForLogicStageCategory(const char*, const char*) { return 0; }
    inline s32 setOverrideAllForLogicStageCategory(const char*, const char*, s16) { return 0; }
    inline bool hasAnyOverrideForLogicStageCategory(const char*, const char*) { return false; }
    inline bool hasAnyOverride() { return false; }
    inline s32 clearOverridesForRuntimeStageCategory(const char*, const char*, const char*) { return 0; }
    inline s32 setOverrideAllForRuntimeStageCategory(const char*, const char*, const char*, s16) { return 0; }
    inline bool hasAnyOverrideForRuntimeStageCategory(const char*, const char*, const char*) { return false; }
    inline const char* resolveLogicStage(const char* stage) { return stage; }
} // namespace EnemySwapDebug
#endif
