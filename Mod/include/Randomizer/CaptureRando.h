#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FoolPunishment.h"

class GameDataHolderAccessor;
class GameDataHolderWriter;

namespace ShineDB {
    struct ShineEntry;
}

namespace CaptureRando {

    struct CaptureEntry {
        const char* hackName;
        const char* displayName;
    };

    void buildRemapTable(u32 seed, u32 attempt = 0);
    void clear();
    bool isInitialized();

    s32 getCaptureCount();
    s32 getRequiredProgressionSlotCount();
    const CaptureEntry* getCapture(s32 index);
    const CaptureEntry* findCaptureByHackName(const char* hackName);
    // Tree and RockForest cannot safely be written to the hack dictionary and
    // therefore cannot be granted directly by a shop reward.
    bool canGrantCaptureDirectly(const CaptureEntry& capture);
    s32 getStartingCaptureCount();
    const CaptureEntry* getStartingCapture(s32 index);
    // See AbilityRando's matching API.  Values are getCapture() indexes.
    void clearLogicStartingOverride();
    bool setLogicStartingOverride(const s16* captureIndexes, s32 count);
    s32 getLogicStartingOverride(s16* outCaptureIndexes, s32 maxCount);

    bool isMushroomMoon(const ShineDB::ShineEntry& moon);
    bool isFrogMoon(const ShineDB::ShineEntry& moon);
    s32 getCaptureCountForMoon(const ShineDB::ShineEntry& moon);
    s32 getProgressionCaptureCountForMoon(const ShineDB::ShineEntry& moon);
    const CaptureEntry* getCaptureForMoon(const ShineDB::ShineEntry& moon);
    const CaptureEntry* getCaptureForMoon(const ShineDB::ShineEntry& moon, s32 index);
    // False marks a repeated/non-progression assignment in the seeded item pool.
    bool isProgressionCaptureSlotForMoon(const ShineDB::ShineEntry& moon, s32 index);
    const CaptureEntry* getProgressionCaptureForMoon(const ShineDB::ShineEntry& moon, s32 index);
    const CaptureEntry* getCaptureForMoon(s32 uid, const char* stage);
    const CaptureEntry* getCaptureForMoon(s32 uid, const char* stage, s32 index);
    const ShineDB::ShineEntry* getMoonForCapture(const char* hackName);
    const ShineDB::ShineEntry* getMoonForCapture(const CaptureEntry& capture);

    bool isCaptureInGallery(GameDataHolderAccessor accessor, const char* hackName);
    bool isCaptureInGallery(GameDataHolderAccessor accessor, const CaptureEntry& capture);
    bool isCaptureDebugUnlocked(const char* hackName);
    bool isCaptureDebugUnlocked(const CaptureEntry& capture);
    bool isCaptureDebugOverrideActive(const char* hackName);
    bool isCaptureDebugOverrideActive(const CaptureEntry& capture);
    void setCaptureDebugUnlocked(const char* hackName, bool unlocked);
    void setCaptureDebugUnlocked(const CaptureEntry& capture, bool unlocked);
    bool isCaptureUnlockedByMoon(GameDataHolderAccessor accessor, const char* hackName);
    bool isCaptureUnlockedByMoon(GameDataHolderAccessor accessor, const CaptureEntry& capture);
    bool isCaptureUnlocked(GameDataHolderAccessor accessor, const char* hackName);
    bool isCaptureUnlocked(GameDataHolderAccessor accessor, const CaptureEntry& capture);
    void setCaptureUnlocked(GameDataHolderWriter writer, const char* hackName, bool unlocked);
    void setCaptureUnlocked(GameDataHolderWriter writer, const CaptureEntry& capture, bool unlocked);
    void applyStartingEquipment(GameDataHolderWriter writer);
    void unlockAll(GameDataHolderWriter writer);
    void clearAll(GameDataHolderWriter writer);
    void unlockForMoon(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon);
    bool isFoolMoon(const ShineDB::ShineEntry& moon);
    Randomizer::FoolPunishment getFoolPunishment(const ShineDB::ShineEntry& moon);

    // True if collecting `moon` would grant a capture the player does not already own
    // (from the gallery or another already-collected assigned moon). Call before the moon
    // is committed; false => this collection is a duplicate (eligible for the "(again)" bonus).
    bool grantsNewCapture(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon);

    // Bit i set => the capture assigned to slot i of `moon` (see getCaptureForMoon(moon, i))
    // is already owned elsewhere. A Multi (triple) moon can mix new and duplicate slots in
    // one collection; call before the moon is committed so each bit reflects prior ownership.
    u32 getDuplicateCaptureSlotMask(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon);

} // namespace CaptureRando
