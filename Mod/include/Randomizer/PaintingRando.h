#pragma once

#include <basis/seadTypes.h>

class ChangeStageInfo;
class GameDataFile;
class WorldWarpHole;

namespace ShineDB {
    struct ShineEntry;
}

namespace PaintingRando {

    void buildRemapTable(u32 seed, u32 attempt = 0);
    void clear();
    bool isInitialized();
    void installHooks();

    s32 getPaintingCount();
    s32 getSourceNode(s32 index);
    s32 getDestinationNode(s32 index);
    s32 getSourceWorldId(s32 index);
    s32 getDestinationWorldId(s32 index);
    s32 getMappedDestinationIndex(s32 sourceIndex);
    s32 getMappedSourceIndexForDestination(s32 destinationIndex);
    s32 getMappedDestinationNode(s32 sourceIndex);
    s32 getMappedReturnSourceNode(s32 destinationIndex);
    s32 getMappedDestinationWorldIdForSourceWorld(s32 sourceWorldId);
    s32 getMappedSourceWorldIdForDestinationWorld(s32 destinationWorldId);

    bool cloudMoonUnlocksPainting(const ShineDB::ShineEntry& moon, s32 paintingIndex);
    // Ordinary Cloud rewards unlock one painting; triple rewards unlock three.
    s32 getCloudMoonPaintingUnlockCount(const ShineDB::ShineEntry& moon);
    s32 getCloudMoonPaintingUnlockIndex(const ShineDB::ShineEntry& moon, s32 unlockIndex);
    // Stable first index among the Cloud Kingdom moon-credit slots, or -1 for a
    // non-Cloud reward. Triple rewards occupy this slot and the following two.
    s32 getCloudMoonSlot(const ShineDB::ShineEntry& moon);
    bool isPaintingLinkFree(s32 paintingIndex);
    // The solver can choose a link order that gives its opening frontier useful
    // painting access.  The order is persisted with the seed and restored before
    // any live painting checks run.
    s32 getCloudLinkOrder(s16* outOrder, s32 maxCount);
    bool setCloudLinkOrder(const s16* order, s32 count);
    void requestFreeLinkAnnouncement();
    // startDelay chains the announcement after the starting abilities/captures
    // overlay sequence (see AbilityOverlay::tryQueueStartingEquipment); pass 0
    // to use the overlay's default delay.
    void tryQueueFreeLinkAnnouncement(s32 startDelay = 0);
    bool getPaintingUnlockKingdomNames(s32 paintingIndex, const char** sourceKingdom, const char** destinationKingdom);
    bool isPaintingUnlocked(const GameDataFile* file, s32 paintingIndex);
    // Cloud-moon painting unlocks are cached because the painting label hooks ask
    // for them every frame. Collection is monotonic within a file, so the cache
    // only has to be dropped when a shine is committed to the save.
    void invalidateUnlockCache();
    bool isWorldWarpHoleUnlocked(const WorldWarpHole* worldWarpHole);
    bool isAnyPaintingUnlockedForWorld(const GameDataFile* file, s32 worldId);
    s32 calcUnlockedPaintingCount(const GameDataFile* file);

    bool tryApplyPaintingChange(GameDataFile* file, const char* currentStage, ChangeStageInfo* info);

} // namespace PaintingRando
