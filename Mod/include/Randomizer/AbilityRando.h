#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/FoolPunishment.h"

class GameDataHolderAccessor;
class GameDataHolderWriter;

namespace ShineDB {
    struct ShineEntry;
}

namespace AbilityRando {

    enum AbilityCategory : u8
    {
        AbilityCategory_Movement,
        AbilityCategory_CapThrow,
    };

    enum AbilityId : s32
    {
        AbilityId_SingleJump = 0,
        AbilityId_DoubleJump,
        AbilityId_TripleJump,
        AbilityId_CapReturnJump,
        AbilityId_Backflip,
        AbilityId_LongJump,
        AbilityId_Vault,
        AbilityId_SideFlip,
        AbilityId_GroundPoundJump,
        AbilityId_Roll,
        AbilityId_RollBoost,
        AbilityId_Crouch,
        AbilityId_GroundPound,
        AbilityId_Dive,
        AbilityId_Spin,
        AbilityId_WallJump,
        AbilityId_LedgeGrab,
        AbilityId_Climb,
        AbilityId_Swing,
        AbilityId_NeutralThrow,
        AbilityId_UpThrow,
        AbilityId_DownThrow,
        AbilityId_SpinThrow,
        // Movement Abilities page. These gate a traversal verb rather than a move,
        // and each is governed by its own setting instead of the master Locked
        // Abilities toggle alone - see isAbilityInUnlockPool. Appended, never
        // renumbered: these values are written into rando.bin's logic-directed
        // start and Nuzlocke cap-throw records.
        AbilityId_MoonLowGravity,
        AbilityId_Swim,
        AbilityId_Grab,
        AbilityId_JaxiRide,
        AbilityId_ScooterRide,
        AbilityId_RocketFlower,
        AbilityId_NpcTalk,
        AbilityId_Pipe2D,
        AbilityId_TimerChallenge,
        AbilityId_Count,
    };

    struct AbilityEntry {
        AbilityId id;
        const char* key;
        const char* displayName;
        AbilityCategory category;
        s32 prerequisite;
        bool alwaysUnlocked;
        bool requiresAnyCapThrow;
        bool canStartWithSeed;
    };

    // Result of processing a collected moon's ability unlock (distinguishes a genuine
    // already-owned duplicate from a re-processed/non-ability "skip").
    enum class UnlockResult
    {
        Skipped, // not an ability moon, disabled, or this moon was already processed this event
        Unlocked, // granted at least one new ability
        AlreadyOwned // ability moon whose abilities were all already owned (eligible for "(again)")
    };

    void buildRemapTable(u32 seed, u32 attempt = 0);
    void clear();
    bool isInitialized();

    s32 getAbilityCount();
    s32 getRequiredProgressionSlotCount();
    const AbilityEntry* getAbility(s32 index);
    s32 getUnlockableAbilityCount();
    const AbilityEntry* getUnlockableAbility(s32 index);
    // Player-facing ability list: every acquirable ability including the starting cap
    // throw (rendered as already-acquired). Distinct from the unlock/reward pool.
    s32 getDisplayAbilityCount();
    const AbilityEntry* getDisplayAbility(s32 index);
    // List/shop row name: chain members read as "Progressive ..." so the player
    // knows acquiring them advances the prerequisite chain (e.g. Backflip is
    // granted after Crouch). Standalone abilities keep their plain name.
    const char* getProgressiveDisplayName(const AbilityEntry& ability);
    s32 getStartingAbilityCount();
    const AbilityEntry* getStartingAbility(s32 index);
    s32 getStartingCapThrowCount();
    const AbilityEntry* getStartingCapThrow(s32 index);
    // Optional solver-selected starts.  Entries supplied here take the earliest
    // starting slots; any configured slots left over retain their normal seeded
    // random selection.
    void clearLogicStartingOverride();
    bool setLogicStartingOverride(const AbilityId* abilities, s32 count);
    s32 getLogicStartingOverride(AbilityId* outAbilities, s32 maxCount);
    // Save-restore only: replays the cap throw a stored slot was generated with.
    // A Soft Nuzlocke reroll deliberately does NOT use this - it re-rolls the
    // opener along with everything else (see Nuzlocke::prepareSoftPenalty).
    void setLogicStartingCapThrowOverride(AbilityId ability);
    AbilityId getLogicStartingCapThrowOverride();
    s32 getDebugAbilityCount();
    const AbilityEntry* getDebugAbility(s32 index);
    const AbilityEntry* findAbilityByKey(const char* key);
    // True when this ability is represented by a seeded ability reward. It
    // excludes always-unlocked abilities and the cap throws granted at start.
    bool isAbilityInUnlockPool(AbilityId ability);
    const AbilityEntry* getStartingCapThrow();

    bool isAbilityMoon(const ShineDB::ShineEntry& moon);
    s32 getAbilityCountForMoon(const ShineDB::ShineEntry& moon);
    s32 getProgressionAbilityCountForMoon(const ShineDB::ShineEntry& moon);
    const AbilityEntry* getAbilityForMoon(const ShineDB::ShineEntry& moon);
    const AbilityEntry* getAbilityForMoon(const ShineDB::ShineEntry& moon, s32 index);
    // False marks a repeated/non-progression assignment in the seeded item pool.
    bool isProgressionAbilitySlotForMoon(const ShineDB::ShineEntry& moon, s32 index);
    const AbilityEntry* getProgressionAbilityForMoon(const ShineDB::ShineEntry& moon, s32 index);
    const AbilityEntry* getAbilityForMoon(s32 uid, const char* stage);
    const AbilityEntry* getAbilityForMoon(s32 uid, const char* stage, s32 index);
    const ShineDB::ShineEntry* getMoonForAbility(AbilityId ability);
    const ShineDB::ShineEntry* getMoonForAbility(const AbilityEntry& ability);
    const ShineDB::ShineEntry* getMoonForAbility(GameDataHolderAccessor accessor, AbilityId ability);
    const ShineDB::ShineEntry* getMoonForAbility(GameDataHolderAccessor accessor, const AbilityEntry& ability);
    const AbilityEntry* getRevealedAbilityForListIndex(GameDataHolderAccessor accessor, s32 index);
    void recordAbilityReveal(GameDataHolderWriter writer, s32 index, const AbilityEntry& ability);
    void recordAbilityReveal(GameDataHolderWriter writer, const AbilityEntry& ability);
    s32 getLastUnlockedAbilityCount();
    const AbilityEntry* getLastUnlockedAbility(s32 index);
    // Slots from the most recent unlockForMoon call whose target ability was already owned
    // (a Multi/triple moon can mix these with genuinely new slots in one collection).
    s32 getLastDuplicateAbilityCount();
    const AbilityEntry* getLastDuplicateAbility(s32 index);

    bool isAbilityInSave(GameDataHolderAccessor accessor, AbilityId ability);
    bool isAbilityInSave(GameDataHolderAccessor accessor, const AbilityEntry& ability);
    bool isAbilityUnlockedByMoon(GameDataHolderAccessor accessor, AbilityId ability);
    bool isAbilityUnlockedByMoon(GameDataHolderAccessor accessor, const AbilityEntry& ability);
    bool isAbilityUnlocked(GameDataHolderAccessor accessor, AbilityId ability);
    bool isAbilityUnlocked(GameDataHolderAccessor accessor, const AbilityEntry& ability);

    // The Movement Abilities (AbilityId_MoonLowGravity and up). They are always
    // in the player-facing Ability List, but only join the reward pool when their own
    // setting asks for it; otherwise they read as granted from the start.
    bool isMovementAbility(AbilityId ability);
    // True when this ability's setting hands it over outright, so nothing has to be
    // collected for it. False for a pool member and for Moon low gravity under either
    // moon-count mode, which is earned rather than granted.
    bool isMovementAbilityGranted(AbilityId ability);
    // Moon Kingdom moons collected towards low gravity under a moon-count mode, and
    // how many are needed. Both are 0 outside those modes. The count comes from the
    // same per-kingdom total the chapel door reads.
    s32 getMoonGravityCollected(GameDataHolderAccessor accessor);
    s32 getMoonGravityRequirement();

    // True when clearing this ability's save flag actually takes it away. False for
    // an always-unlocked move, for a Movement Ability whose dial grants it, and for
    // Moon low gravity under a moon-count mode - that one is derived from the moon
    // total, so surrendering it would report a loss and change nothing. Callers that
    // take abilities away (Nuzlocke) must filter on this.
    bool isAbilityRevocable(AbilityId ability);

    // Name of this ability's icon inside the TextureSouvenir archive, or nullptr
    // to keep the native one. Only the Movement Abilities need it: the Ability
    // List and the purple shop both take an ability's icon from the vanilla gift
    // record sitting at its display index, and the five appended rows land on
    // souvenirs that have nothing to do with them. Presentation-only - nothing in
    // logic or the save reads it. Names are the archive's own, which all carry the
    // "Souvenir" prefix (SouvenirSky2, not Sky2) - a name that does not resolve
    // silently leaves the row showing whatever native souvenir sits at its index.
    const char* getSouvenirTextureName(AbilityId ability);

    bool isAbilityDebugUnlocked(AbilityId ability);
    bool isAbilityDebugUnlocked(const AbilityEntry& ability);
    bool isAbilityDebugOverrideActive(AbilityId ability);
    bool isAbilityDebugOverrideActive(const AbilityEntry& ability);
    void setAbilityDebugUnlocked(AbilityId ability, bool unlocked);
    void setAbilityDebugUnlocked(const AbilityEntry& ability, bool unlocked);

    void setAbilityUnlocked(GameDataHolderWriter writer, AbilityId ability, bool unlocked);
    void setAbilityUnlocked(GameDataHolderWriter writer, const AbilityEntry& ability, bool unlocked);
    void applyStartingEquipment(GameDataHolderWriter writer);
    void unlockAll(GameDataHolderWriter writer);
    void clearAll(GameDataHolderWriter writer);
    // Pass alreadyCollected = true when re-collecting a moon that was already got: a moon only
    // awards its ability the first time it is collected, so a re-collect must never advance a
    // progressive ability. It still claims the per-event dedup slot (suppressing the redundant
    // GameDataFile::setGotShine award path) and reports AlreadyOwned for "(again)" feedback.
    UnlockResult unlockForMoon(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon,
        bool alreadyCollected = false);
    bool isFoolMoon(const ShineDB::ShineEntry& moon);
    Randomizer::FoolPunishment getFoolPunishment(const ShineDB::ShineEntry& moon);

} // namespace AbilityRando
