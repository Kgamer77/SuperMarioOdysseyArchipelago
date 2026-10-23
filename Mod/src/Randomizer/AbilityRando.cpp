#include "Randomizer/AbilityRando.h"

#include "Randomizer/MoonRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"

#include "nn/util.h"

#include <cstring>

namespace AbilityRando {

    static constexpr s32 kNoPrerequisite = -1;
    // Sized for Moon Allocation's worst case rather than the vanilla Dark plus
    // Darker list. Every host in those two kingdoms carries a reward slot, and
    // the shuffled allocation profile can hand them the two largest excesses in
    // the set at once. Abilities additionally hold the Cap/Cloud/Moon selector
    // slots, which ride on kingdoms that can grow the same way.
    static constexpr s32 kMaxMoonAbilities = 512;
    static constexpr s32 kMaxMoonSlots = 256;

    static constexpr AbilityEntry sAbilities[] = {
        { AbilityId_SingleJump, "SingleJump", "Single Jump", AbilityCategory_Movement, kNoPrerequisite, true, false, false },
        { AbilityId_DoubleJump, "DoubleJump", "Double Jump", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_TripleJump, "TripleJump", "Triple Jump", AbilityCategory_Movement, AbilityId_DoubleJump, false, false, false },
        { AbilityId_CapReturnJump, "CapReturnJump", "Cap Return Jump", AbilityCategory_Movement, kNoPrerequisite, true, false, false },
        { AbilityId_Backflip, "Backflip", "Backflip", AbilityCategory_Movement, AbilityId_Crouch, false, false, false },
        { AbilityId_LongJump, "LongJump", "Long Jump", AbilityCategory_Movement, AbilityId_Crouch, false, false, false },
        { AbilityId_Vault, "Vault", "Vault", AbilityCategory_Movement, kNoPrerequisite, false, true, false },
        { AbilityId_SideFlip, "SideFlip", "Side Flip", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_GroundPoundJump, "GroundPoundJump", "Ground Pound Jump", AbilityCategory_Movement, AbilityId_GroundPound, false, false, false },
        { AbilityId_Roll, "Roll", "Roll", AbilityCategory_Movement, AbilityId_Crouch, false, false, false },
        { AbilityId_RollBoost, "RollBoost", "Roll Boost", AbilityCategory_Movement, AbilityId_Roll, false, false, false },
        { AbilityId_Crouch, "Crouch", "Crouch", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_GroundPound, "GroundPound", "Ground Pound", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Dive, "Dive", "Dive", AbilityCategory_Movement, AbilityId_GroundPound, false, false, false },
        { AbilityId_Spin, "Spin", "Spin", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_WallJump, "WallJump", "Wall Jump", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_LedgeGrab, "LedgeGrab", "Ledge Grab", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Climb, "Climb", "Climb", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Swing, "Swing", "Swing", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_NeutralThrow, "NeutralThrow", "Neutral Throw", AbilityCategory_CapThrow, kNoPrerequisite, false, false, true },
        { AbilityId_UpThrow, "UpThrow", "Up Throw", AbilityCategory_CapThrow, kNoPrerequisite, false, false, true },
        { AbilityId_DownThrow, "DownThrow", "Down Throw", AbilityCategory_CapThrow, kNoPrerequisite, false, false, true },
        { AbilityId_SpinThrow, "SpinThrow", "Spin Throw", AbilityCategory_CapThrow, kNoPrerequisite, false, false, true },
        { AbilityId_MoonLowGravity, "MoonLowGravity", "Moon Kingdom Low Gravity", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Swim, "Swim", "Swim", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Grab, "Grab", "Grab", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_JaxiRide, "Jaxi", "Jaxi Riding", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_ScooterRide, "Bike", "Scooter Riding", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_RocketFlower, "RocketFlower", "Rocket Flower", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_NpcTalk, "NpcTalk", "Talk to NPCs", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_Pipe2D, "Pipe2D", "Enter 2D Pipes", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
        { AbilityId_TimerChallenge, "TimerChallenge", "Timer Challenges", AbilityCategory_Movement, kNoPrerequisite, false, false, false },
    };

    static_assert(sizeof(sAbilities) / sizeof(sAbilities[0]) == AbilityId_Count);

    struct MoonAbilityEntry {
        s32 uid;
        const char* stage;
        const AbilityEntry* ability;
        bool progression;
    };

    struct MoonSlot {
        s32 uid;
        const char* stage;
    };

    struct MoonKey {
        s32 uid;
        const char* stage;
    };

    static MoonAbilityEntry sMoonAbilities[kMaxMoonAbilities];
    static s32 sMoonAbilityCount = 0;
    static MoonKey sFoolMoons[kMaxMoonAbilities];
    static Randomizer::FoolPunishment sFoolPunishments[kMaxMoonAbilities];
    static s32 sFoolMoonCount = 0;
    static AbilityId sStartingCapThrow = AbilityId_NeutralThrow;
    static AbilityId sStartingCapThrows[4] = {
        AbilityId_NeutralThrow,
        AbilityId_NeutralThrow,
        AbilityId_NeutralThrow,
        AbilityId_NeutralThrow,
    };
    static s32 sStartingCapThrowCount = 1;
    static const AbilityEntry* sStartingAbilities[2];
    static s32 sStartingAbilityCount = 0;
    static AbilityId sLogicStartingAbilities[2] = {};
    static s32 sLogicStartingAbilityCount = 0;
    static s32 sLogicStartingCapThrowOverride = -1;
    static bool sInitialized = false;
    static u32 sLastSeed = 0xFFFFFFFF;
    static u32 sLastAttempt = 0xFFFFFFFF;
    static Randomizer::StartingEquipment sLastStartingEquipment = Randomizer::StartingEquipment_Normal;
    static Randomizer::CaptureAbilityDifficulty sLastDifficulty = Randomizer::CaptureAbilityDifficulty_Normal;
    static u32 sLastMoonRoleLayoutHash = 0;
    static s32 sLastStartingAbilityTarget = 1;
    static s32 sLastStartingCapThrowTarget = 1;
    static u64 sLastAbilityLockPoolMask = ~0ull;
    static bool sDebugOverrideActive[AbilityId_Count] = {};
    static bool sDebugUnlocked[AbilityId_Count] = {};
    static const AbilityEntry* sLastUnlockedAbilities[AbilityId_Count];
    static s32 sLastUnlockedAbilityCount = 0;
    static const AbilityEntry* sLastDuplicateAbilities[AbilityId_Count];
    static s32 sLastDuplicateAbilityCount = 0;
    static bool sLastProcessedMoonValid = false;
    static s32 sLastProcessedMoonUid = -1;
    static const char* sLastProcessedMoonStage = nullptr;

    static const AbilityEntry* getAbilityById(AbilityId ability);

    static bool isStartingCapThrow(AbilityId id) {
        for (s32 i = 0; i < sStartingCapThrowCount; i++)
            if (sStartingCapThrows[i] == id)
                return true;
        return false;
    }

    // Which of the five Movement Abilities dials, if any, governs this ability.
    // Everything else in the table is governed by Locked Abilities alone.
    static bool isMovementAbilityId(AbilityId id) {
        return id >= AbilityId_MoonLowGravity && id < AbilityId_Count;
    }

    // True when this ability's own setting puts it in the ability reward pool, so a
    // Dark/Darker Side moon hosts it like any other locked ability. Moon low gravity
    // says yes only in Ability Lock mode; its two moon-count modes withhold it
    // without making it an item.
    static bool isMovementAbilityInPool(AbilityId id) {
        bool inPool = false;
        switch (id) {
        case AbilityId_MoonLowGravity:
            inPool = Randomizer::isMoonGravityAbilityLocked();
            break;
        case AbilityId_Swim:
            inPool = Randomizer::isSwimLockEnabled();
            break;
        case AbilityId_Grab:
            inPool = Randomizer::isGrabLockEnabled();
            break;
        case AbilityId_JaxiRide:
            inPool = Randomizer::isJaxiLockEnabled();
            break;
        case AbilityId_ScooterRide:
            inPool = Randomizer::isScooterLockEnabled();
            break;
        case AbilityId_RocketFlower:
            inPool = Randomizer::isRocketFlowerLockEnabled();
            break;
        case AbilityId_NpcTalk:
            inPool = Randomizer::isNpcTalkLockEnabled();
            break;
        case AbilityId_Pipe2D:
            inPool = Randomizer::isPipe2DLockEnabled();
            break;
        case AbilityId_TimerChallenge:
            inPool = Randomizer::isTimerChallengeLockEnabled();
            break;
        default:
            return false;
        }
        return inPool && Randomizer::isAbilityInLockPool(static_cast<s32>(id));
    }

    // A Movement Ability nobody is withholding. Deliberately not the same as
    // "not in the pool": low gravity under a moon-count mode is in neither set,
    // because it is earned from a moon total rather than granted or placed.
    static bool isMovementAbilityGrantedById(AbilityId id) {
        if (!isMovementAbilityId(id))
            return false;
        if (id == AbilityId_MoonLowGravity)
            return !Randomizer::isMoonGravityLocked();
        return !isMovementAbilityInPool(id);
    }

    // Moon Kingdom moons on the save, which is what both moon-count gravity modes
    // count. Same source as MoonRequirement::getCollected - the persistent
    // per-kingdom total, so depositing moons at a shop cannot take the ability back.
    static s32 moonKingdomShineNum(GameDataHolderAccessor accessor) {
        if (!accessor.mData)
            return 0;
        GameDataFile* file = accessor->getGameDataFile();
        if (!file)
            return 0;
        const s32 collected = file->getShineNum(WorldMeta::WorldId_Moon);
        return collected > 0 ? collected : 0;
    }

    // Low gravity earned by moon count. Derived rather than written to the save's
    // hack dictionary: the moon total already persists, so a derived answer cannot
    // drift out of step with it.
    static bool isMoonGravityEarned(GameDataHolderAccessor accessor) {
        if (!Randomizer::isMoonGravityCountLocked())
            return false;
        const s32 requirement = Randomizer::getMoonGravityMoonRequirement();
        return requirement > 0 && moonKingdomShineNum(accessor) >= requirement;
    }

    static bool isAbilityInUnlockPool(const AbilityEntry& ability) {
        if (ability.alwaysUnlocked || isStartingCapThrow(ability.id))
            return false;
        if (isMovementAbilityId(ability.id))
            return isMovementAbilityInPool(ability.id);
        return Randomizer::isAbilityInLockPool(static_cast<s32>(ability.id));
    }

    // The player-facing list shows every acquirable ability, including the ones granted
    // at the start (they render as already-acquired). It is the unlock pool plus the
    // starting cap throw, which the unlock pool excludes because it is not a reward.
    // Starting movement abilities are already in the unlock pool, so they appear too.
    static bool isAbilityInDisplayList(const AbilityEntry& ability) {
        // The Movement Abilities are listed unconditionally, rendering as already
        // acquired when their setting grants them. Keeping the list length fixed is
        // deliberate: the Sphinx quiz asks how many rows it has.
        return !ability.alwaysUnlocked || isMovementAbilityId(ability.id);
    }

    static bool isAbilityInDebugMenu(const AbilityEntry& ability) {
        return !ability.alwaysUnlocked;
    }

    // This is deliberately separate from sAbilities: the enum order is persistent data used by
    // saves, debug overrides, and logic, while the Ability List should read like a progression
    // guide. Keep each prerequisite before its dependent move, with cap throws first.
    static constexpr AbilityId sDisplayAbilityOrder[] = {
        AbilityId_NeutralThrow,
        AbilityId_UpThrow,
        AbilityId_DownThrow,
        AbilityId_SpinThrow,
        AbilityId_Vault,
        AbilityId_Crouch,
        AbilityId_Backflip,
        AbilityId_LongJump,
        AbilityId_Roll,
        AbilityId_RollBoost,
        AbilityId_GroundPound,
        AbilityId_GroundPoundJump,
        AbilityId_Dive,
        AbilityId_SingleJump,
        AbilityId_DoubleJump,
        AbilityId_TripleJump,
        AbilityId_CapReturnJump,
        AbilityId_SideFlip,
        AbilityId_Spin,
        AbilityId_WallJump,
        AbilityId_LedgeGrab,
        AbilityId_Climb,
        AbilityId_Swing,
        // Movement Abilities, in the order the setup page lists them.
        AbilityId_MoonLowGravity,
        AbilityId_Swim,
        AbilityId_Grab,
        AbilityId_JaxiRide,
        AbilityId_ScooterRide,
        AbilityId_RocketFlower,
        AbilityId_NpcTalk,
        AbilityId_Pipe2D,
        AbilityId_TimerChallenge,
    };

    static_assert(sizeof(sDisplayAbilityOrder) / sizeof(sDisplayAbilityOrder[0]) == AbilityId_Count);

    static const AbilityEntry* getDisplayAbilityByListIndex(s32 index) {
        if (index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < AbilityId_Count; i++) {
            const AbilityEntry& ability = sAbilities[sDisplayAbilityOrder[i]];
            if (!isAbilityInDisplayList(ability))
                continue;
            if (found == index)
                return &ability;
            found++;
        }
        return nullptr;
    }

    // Position of an ability in the player-facing display list. Reveal flags are keyed by
    // this position, so they MUST use the same list the UI renders (display list), not the
    // narrower unlock pool, otherwise reveals land on the wrong row.
    static s32 findDisplayAbilityIndex(AbilityId ability) {
        for (s32 index = 0;; index++) {
            const AbilityEntry* entry = getDisplayAbilityByListIndex(index);
            if (!entry)
                break;
            if (entry->id == ability)
                return index;
        }
        return -1;
    }

    static void shuffleIndices(s32* indices, s32 count, Randomizer::Rng& rng) {
        for (s32 i = count - 1; i > 0; i--) {
            s32 j = (s32)rng.nextModulo((u32)(i + 1));
            s32 tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
        }
    }

    static void shuffleSlots(MoonSlot* slots, s32 count, Randomizer::Rng& rng) {
        for (s32 i = count - 1; i > 0; i--) {
            s32 j = (s32)rng.nextModulo((u32)(i + 1));
            MoonSlot tmp = slots[i];
            slots[i] = slots[j];
            slots[j] = tmp;
        }
    }

    static u32 hashString(const char* value) {
        u32 hash = 2166136261u;
        if (!value)
            return hash;
        while (*value) {
            hash ^= (u8)*value++;
            hash *= 16777619u;
        }
        return hash;
    }

    static u32 moonChoiceHash(u32 seed, const ShineDB::ShineEntry& moon) {
        u32 hash = seed ^ 0x9E3779B9u;
        hash ^= (u32)moon.uid * 2246822519u;
        hash ^= (u32)moon.objId * 3266489917u;
        hash ^= (u32)moon.worldId * 668265263u;
        hash ^= hashString(moon.stage);
        hash ^= hash >> 16;
        hash *= 2246822519u;
        hash ^= hash >> 13;
        return hash;
    }

    static u8 getSpecialMoonUseMask(const ShineDB::ShineEntry& moon) {
        switch (MoonRando::getAssignedKingdomForEntry(&moon)) {
        case WorldMeta::WorldId_Cap:
            return Randomizer::getCapKingdomMoonUseMask();
        case WorldMeta::WorldId_Cloud:
            return Randomizer::getCloudKingdomMoonUseMask();
        case WorldMeta::WorldId_Moon:
            return Randomizer::getMoonKingdomMoonUseMask();
        default:
            return static_cast<u8>(1u << Randomizer::SpecialMoonUse_Coins);
        }
    }

    static bool specialMoonAwardsAbility(u32 seed, const ShineDB::ShineEntry& moon) {
        const u8 mask = getSpecialMoonUseMask(moon);
        const bool canAbility = (mask
                                    & (1u << Randomizer::SpecialMoonUse_Abilities))
            != 0;
        const bool canCapture = (mask
                                    & (1u << Randomizer::SpecialMoonUse_Captures))
            != 0;
        const bool canEither = (mask
                                   & (1u << Randomizer::SpecialMoonUse_AbilitiesOrCaptures))
            != 0;
        const bool abilityPossible = canAbility || canEither;
        const bool capturePossible = canCapture || canEither;
        if (!abilityPossible)
            return false;
        if (capturePossible)
            // The capture and ability passes share this exact bit, so a
            // multi-select pool never awards both item types for one moon.
            return (moonChoiceHash(seed, moon) & 1u) != 0;
        return true;
    }

    static s32 startingAbilityTargetCount() {
        return Randomizer::getStartingAbilityCount();
    }

    static bool canStartWithExtraAbility(const AbilityEntry& ability) {
        // Movement Abilities share the Movement category but are never a starting
        // freebie. Each one is an explicit choice on its own dial, and handing one
        // out at random undoes that choice for the whole run - a free Swim in
        // particular means the lock the player asked for simply never happens.
        if (isMovementAbilityId(ability.id))
            return false;
        return ability.category == AbilityCategory_Movement && isAbilityInUnlockPool(ability);
    }

    static const AbilityEntry* resolveStartingAbilityAwardFromSnapshot(
        const bool* unlockedAtStart, const AbilityEntry& ability) {
        if (ability.prerequisite >= 0) {
            const AbilityEntry* prerequisite = getAbilityById((AbilityId)ability.prerequisite);
            if (prerequisite && !unlockedAtStart[prerequisite->id])
                return resolveStartingAbilityAwardFromSnapshot(unlockedAtStart, *prerequisite);
        }

        if (unlockedAtStart[ability.id])
            return nullptr;
        return &ability;
    }

    static void addPendingStartingTarget(const AbilityEntry** pendingTargets,
        s32& pendingTargetCount, const AbilityEntry& ability) {
        for (s32 i = 0; i < pendingTargetCount; i++)
            if (pendingTargets[i] && pendingTargets[i]->id == ability.id)
                return;
        if (pendingTargetCount < AbilityId_Count)
            pendingTargets[pendingTargetCount++] = &ability;
    }

    static const AbilityEntry* findPendingStartingAward(
        const AbilityEntry* const* pendingTargets, s32 pendingTargetCount,
        const bool* unlockedAtStart) {
        for (s32 i = 0; i < pendingTargetCount; i++) {
            if (!pendingTargets[i])
                continue;
            if (const AbilityEntry* award = resolveStartingAbilityAwardFromSnapshot(unlockedAtStart, *pendingTargets[i]))
                return award;
        }
        return nullptr;
    }

    static bool addStartingAbilityAward(const AbilityEntry& award, bool* unlockedAtStart) {
        if (sStartingAbilityCount >= startingAbilityTargetCount())
            return false;

        for (s32 i = 0; i < sStartingAbilityCount; i++)
            if (sStartingAbilities[i] && sStartingAbilities[i]->id == award.id)
                return false;

        sStartingAbilities[sStartingAbilityCount++] = &award;
        unlockedAtStart[award.id] = true;
        return true;
    }

    static bool addStartingAbility(const AbilityEntry& ability,
        const AbilityEntry** pendingTargets, s32& pendingTargetCount,
        bool* unlockedAtStart) {
        const AbilityEntry* award = resolveStartingAbilityAwardFromSnapshot(unlockedAtStart, ability);
        if (!award)
            award = findPendingStartingAward(pendingTargets, pendingTargetCount, unlockedAtStart);
        if (!award)
            return false;

        const bool added = addStartingAbilityAward(*award, unlockedAtStart);
        if (added && award->id != ability.id)
            addPendingStartingTarget(pendingTargets, pendingTargetCount, ability);
        return added;
    }

    static bool isStartingAbility(const AbilityEntry& ability) {
        for (s32 i = 0; i < sStartingAbilityCount; i++)
            if (sStartingAbilities[i] && sStartingAbilities[i]->id == ability.id)
                return true;
        return false;
    }

    static bool sameMoon(const MoonKey& key, const ShineDB::ShineEntry& moon) {
        return key.uid == moon.uid && key.stage && std::strcmp(key.stage, moon.stage) == 0;
    }

    static void addFoolMoon(const ShineDB::ShineEntry& moon, u32 seed) {
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return;
        if (sFoolMoonCount < kMaxMoonAbilities) {
            // Keep the result tied to the seed and moon rather than collection order.
            sFoolMoons[sFoolMoonCount++] = { moon.uid, moon.stage };
            sFoolPunishments[sFoolMoonCount - 1] = Randomizer::rollFoolPunishment(
                moonChoiceHash(seed ^ 0xF001B00Bu, moon), Randomizer::isNuzlockeRunMode());
        }
    }

    static bool isValidAbilityId(AbilityId ability) {
        return ability >= 0 && ability < AbilityId_Count;
    }

    static GameDataFile* getDataFile(GameDataHolderAccessor accessor) {
        if (!accessor.mData)
            return nullptr;
        return accessor->getGameDataFile();
    }

    static void makeAbilityFlag(char* out, size_t outSize, const AbilityEntry& ability) {
        nn::util::SNPrintf(out, outSize, "Ability_%s", ability.key);
    }

    static void makeAbilityRevealFlag(char* out, size_t outSize, s32 index,
        const AbilityEntry& ability) {
        nn::util::SNPrintf(out, outSize, "AbilityReveal_%02d_%s", index,
            ability.key ? ability.key : "");
    }

    static const AbilityEntry* getAbilityById(AbilityId ability) {
        if (!isValidAbilityId(ability))
            return nullptr;
        return &sAbilities[ability];
    }

    static void buildAwardSnapshot(GameDataHolderAccessor accessor, bool* unlockedAtStart);
    static const AbilityEntry* resolveAwardAbilityFromSnapshot(
        const bool* unlockedAtStart, const bool* awardedThisMoon, const AbilityEntry& ability);
    static const AbilityEntry* resolveAwardForSlot(
        const bool* unlockedAtStart, const bool* awardedThisMoon,
        const AbilityEntry& ability, const AbilityEntry** pendingTargets,
        s32& pendingTargetCount);

    static bool isAnyPrimaryCapThrowUnlocked(GameDataHolderAccessor accessor) {
        for (s32 i = 0; i < AbilityId_Count; i++) {
            if (!sAbilities[i].canStartWithSeed)
                continue;
            if (isAbilityUnlocked(accessor, sAbilities[i]))
                return true;
        }
        return false;
    }

    bool isAbilityMoon(const ShineDB::ShineEntry& moon) {
        if (moon.uid == 0 || moon.objId < 0)
            return false;
        // Redistributed kingdom, not vanilla home (see CaptureRando::isMushroomMoon):
        // abilities ride on whatever pool ended up assigned to a kingdom the
        // Kingdom Moons page put them in - the two Side kingdoms by default.
        if (!Randomizer::isAbilityRewardKingdom(
                MoonRando::getAssignedKingdomForEntry(&moon)))
            return false;
        if (moon.flags & ShineDB::ShineFlag_Toadette)
            return false;
        return true;
    }

    s32 getAbilityCountForMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized)
            return 0;

        s32 count = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            if (sMoonAbilities[i].uid == moon.uid && std::strcmp(sMoonAbilities[i].stage, moon.stage) == 0)
                count++;
        }
        return count;
    }

    s32 getProgressionAbilityCountForMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized)
            return 0;

        s32 count = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            if (sMoonAbilities[i].progression && sMoonAbilities[i].uid == moon.uid && std::strcmp(sMoonAbilities[i].stage, moon.stage) == 0)
                count++;
        }
        return count;
    }

    const AbilityEntry* getAbilityForMoon(s32 uid, const char* stage, s32 index) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized || !stage)
            return nullptr;
        if (index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            if (sMoonAbilities[i].uid != uid || std::strcmp(sMoonAbilities[i].stage, stage) != 0)
                continue;
            if (found == index)
                return sMoonAbilities[i].ability;
            found++;
        }
        return nullptr;
    }

    const AbilityEntry* getAbilityForMoon(s32 uid, const char* stage) {
        return getAbilityForMoon(uid, stage, 0);
    }

    const AbilityEntry* getAbilityForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        return getAbilityForMoon(moon.uid, moon.stage, index);
    }

    const AbilityEntry* getAbilityForMoon(const ShineDB::ShineEntry& moon) {
        return getAbilityForMoon(moon, 0);
    }

    bool isProgressionAbilitySlotForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized || index < 0)
            return false;

        s32 found = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            if (sMoonAbilities[i].uid != moon.uid || std::strcmp(sMoonAbilities[i].stage, moon.stage) != 0)
                continue;
            if (found == index)
                return sMoonAbilities[i].progression;
            found++;
        }
        return false;
    }

    const AbilityEntry* getProgressionAbilityForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized || index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            if (!sMoonAbilities[i].progression || sMoonAbilities[i].uid != moon.uid || std::strcmp(sMoonAbilities[i].stage, moon.stage) != 0)
                continue;
            if (found == index)
                return sMoonAbilities[i].ability;
            found++;
        }
        return nullptr;
    }

    static bool isSameMoonAbilitySlot(const MoonAbilityEntry& lhs,
        const MoonAbilityEntry& rhs) {
        return lhs.uid == rhs.uid && lhs.stage && rhs.stage && std::strcmp(lhs.stage, rhs.stage) == 0;
    }

    const ShineDB::ShineEntry* getMoonForAbility(AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry || !Randomizer::isLockedAbilitiesEnabled())
            return nullptr;
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            const MoonAbilityEntry& moonAbility = sMoonAbilities[i];
            if (moonAbility.progression && moonAbility.ability->id == entry->id)
                return ShineDB::findEntry(moonAbility.uid, moonAbility.stage);
        }
        return nullptr;
    }

    const ShineDB::ShineEntry* getMoonForAbility(const AbilityEntry& ability) {
        return getMoonForAbility(ability.id);
    }

    const ShineDB::ShineEntry* getMoonForAbility(GameDataHolderAccessor accessor, AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry || !Randomizer::isLockedAbilitiesEnabled())
            return nullptr;
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return getMoonForAbility(ability);

        bool unlockedAtStart[AbilityId_Count];
        buildAwardSnapshot(accessor, unlockedAtStart);

        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            const MoonAbilityEntry& candidate = sMoonAbilities[i];
            if (!candidate.stage || file->customIsGotShine(candidate.uid, candidate.stage))
                continue;

            bool alreadyCheckedMoon = false;
            for (s32 checked = 0; checked < i; checked++) {
                if (isSameMoonAbilitySlot(sMoonAbilities[checked], candidate)) {
                    alreadyCheckedMoon = true;
                    break;
                }
            }
            if (alreadyCheckedMoon)
                continue;

            const ShineDB::ShineEntry* moon = ShineDB::findEntry(candidate.uid, candidate.stage);
            if (!moon)
                continue;

            bool awardedThisMoon[AbilityId_Count] = {};
            const AbilityEntry* pendingTargets[AbilityId_Count] = {};
            s32 pendingTargetCount = 0;
            for (s32 j = 0; j < sMoonAbilityCount; j++) {
                const MoonAbilityEntry& moonAbility = sMoonAbilities[j];
                if (!isSameMoonAbilitySlot(moonAbility, candidate))
                    continue;

                const AbilityEntry* awarded = resolveAwardForSlot(
                    unlockedAtStart, awardedThisMoon, *moonAbility.ability,
                    pendingTargets, pendingTargetCount);
                if (!awarded)
                    continue;

                awardedThisMoon[awarded->id] = true;
                if (awarded->id == entry->id)
                    return moon;
            }
        }

        return getMoonForAbility(ability);
    }

    const ShineDB::ShineEntry* getMoonForAbility(
        GameDataHolderAccessor accessor, const AbilityEntry& ability) {
        return getMoonForAbility(accessor, ability.id);
    }

    const AbilityEntry* getRevealedAbilityForListIndex(
        GameDataHolderAccessor accessor, s32 index) {
        if (index < 0)
            return nullptr;

        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return nullptr;

        const AbilityEntry* fallback = getDisplayAbility(index);
        // Reveal flags from previous versions used the old display indices. Only honour a
        // flag for the ability that actually owns this row; an old flag at the same numerical
        // index must not rename an unrelated ability after the list is reordered.
        if (fallback) {
            char flag[96];
            makeAbilityRevealFlag(flag, sizeof(flag), index, *fallback);
            if (file->customIsExistInHackDictionary(flag))
                return fallback;
        }

        return fallback && isAbilityInSave(accessor, *fallback) ? fallback : nullptr;
    }

    void recordAbilityReveal(GameDataHolderWriter writer, s32 index,
        const AbilityEntry& ability) {
        if (index < 0 || !writer.mData)
            return;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return;

        for (s32 i = 0; i < AbilityId_Count; i++) {
            if (!isAbilityInDisplayList(sAbilities[i]))
                continue;

            char existingFlag[96];
            makeAbilityRevealFlag(existingFlag, sizeof(existingFlag), index, sAbilities[i]);
            file->customRemoveHackDictionary(existingFlag);
        }

        char flag[96];
        makeAbilityRevealFlag(flag, sizeof(flag), index, ability);
        file->customAddHackDictionary(flag);
        GameDataFunction::setRequireSave(writer);
    }

    void recordAbilityReveal(GameDataHolderWriter writer, const AbilityEntry& ability) {
        recordAbilityReveal(writer, findDisplayAbilityIndex(ability.id), ability);
    }

    static u32 seedForAttempt(u32 seed, u32 attempt) {
        return seed ^ (attempt * 0x85EBCA6Bu);
    }

    void buildRemapTable(u32 seed, u32 attempt) {
        const Randomizer::StartingEquipment startingEquipment = Randomizer::getStartingEquipment();
        const Randomizer::CaptureAbilityDifficulty difficulty = Randomizer::getCaptureAbilityDifficulty();
        const u32 moonRoleLayoutHash = MoonRando::getMoonRoleLayoutHash();
        const s32 startingAbilityTarget = Randomizer::getStartingAbilityCount();
        const s32 startingCapThrowTarget = Randomizer::getStartingThrowCount();
        const u64 abilityLockPoolMask = Randomizer::getAbilityLockPoolMask();
        if (sInitialized && sLastSeed == seed && sLastAttempt == attempt
            && sLastStartingEquipment == startingEquipment
            && sLastDifficulty == difficulty
            && sLastMoonRoleLayoutHash == moonRoleLayoutHash
            && sLastStartingAbilityTarget == startingAbilityTarget
            && sLastStartingCapThrowTarget == startingCapThrowTarget
            && sLastAbilityLockPoolMask == abilityLockPoolMask)
            return;

        sMoonAbilityCount = 0;
        sFoolMoonCount = 0;
        sStartingAbilityCount = 0;
        sStartingCapThrowCount = 0;
        sLastUnlockedAbilityCount = 0;
        sLastDuplicateAbilityCount = 0;
        sLastProcessedMoonValid = false;
        sLastProcessedMoonUid = -1;
        sLastProcessedMoonStage = nullptr;

        const u32 attemptSeed = seedForAttempt(seed, attempt);
        // Shared with CaptureRando's specialMoonSeed: the AbilitiesOrCaptures coin flip must
        // hash off the same input in both files (see specialMoonAwardsAbility), not each
        // domain's own obfuscated attemptSeed, or the two choices stop being true opposites.
        const u32 specialMoonSeed = seed ^ attempt;
        Randomizer::Rng rng = Randomizer::makeRng(attemptSeed, Randomizer::RngStream::Ability);

        AbilityId startChoices[AbilityId_Count];
        s32 startChoiceCount = 0;
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (sAbilities[i].canStartWithSeed)
                startChoices[startChoiceCount++] = sAbilities[i].id;
        const AbilityEntry* capThrowOverride = sLogicStartingCapThrowOverride >= 0
            ? getAbilityById(static_cast<AbilityId>(sLogicStartingCapThrowOverride))
            : nullptr;
        // The replayed override still wins, so loading a slot generated before
        // the toggle was flipped keeps the cap throw it was actually built with.
        // Like that override, the pin skips the draw rather than discarding it:
        // it is a different seed layout, not a reroll of the same one.
        AbilityId firstCapThrow = capThrowOverride && capThrowOverride->canStartWithSeed
            ? capThrowOverride->id
            : (Randomizer::isAlwaysNeutralCapThrowEnabled()
                      ? AbilityId_NeutralThrow
                      : (startChoiceCount > 0
                                ? startChoices[rng.nextModulo((u32)startChoiceCount)]
                                : AbilityId_NeutralThrow));
        sStartingCapThrows[sStartingCapThrowCount++] = firstCapThrow;
        for (s32 extra = 1; extra < startingCapThrowTarget
             && sStartingCapThrowCount < (s32)(sizeof(sStartingCapThrows)
                    / sizeof(sStartingCapThrows[0]));
             extra++) {
            AbilityId candidate = AbilityId_NeutralThrow;
            bool found = false;
            for (s32 tries = 0; tries < AbilityId_Count * 2 && !found; tries++) {
                if (startChoiceCount <= 0)
                    break;
                candidate = startChoices[rng.nextModulo((u32)startChoiceCount)];
                if (!isStartingCapThrow(candidate))
                    found = true;
            }
            if (!found) {
                for (s32 i = 0; i < startChoiceCount; i++) {
                    if (isStartingCapThrow(startChoices[i]))
                        continue;
                    candidate = startChoices[i];
                    found = true;
                    break;
                }
            }
            if (!found)
                break;
            sStartingCapThrows[sStartingCapThrowCount++] = candidate;
        }
        sStartingCapThrow = sStartingCapThrows[0];

        bool startingUnlocked[AbilityId_Count] = {};
        for (s32 i = 0; i < AbilityId_Count; i++)
            startingUnlocked[i] = sAbilities[i].alwaysUnlocked
                || isStartingCapThrow(sAbilities[i].id)
                || isMovementAbilityGrantedById(sAbilities[i].id);
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (!isMovementAbilityId(sAbilities[i].id)
                && !Randomizer::isAbilityInLockPool(i))
                startingUnlocked[i] = true;

        MoonSlot moonSlots[kMaxMoonSlots];
        s32 moonSlotCount = 0;
        s32 moonCount = 0;
        for (s32 i = 0; i < ShineDB::sShineTableSize && moonSlotCount < kMaxMoonSlots; i++) {
            const ShineDB::ShineEntry& moon = ShineDB::sShineTable[i];
            if (!isAbilityMoon(moon))
                continue;

            moonCount++;
            const s32 unlockCount = MoonRando::isRewardMultiEntry(&moon) ? 3 : 1;
            for (s32 j = 0; j < unlockCount && moonSlotCount < kMaxMoonSlots; j++)
                moonSlots[moonSlotCount++] = { moon.uid, moon.stage };
        }
        shuffleSlots(moonSlots, moonSlotCount, rng);

        s32 abilityOrder[AbilityId_Count];
        s32 unlockableAbilityCount = 0;
        for (s32 i = 0; i < AbilityId_Count; i++) {
            if (!isAbilityInUnlockPool(sAbilities[i]))
                continue;
            abilityOrder[unlockableAbilityCount++] = i;
        }
        shuffleIndices(abilityOrder, unlockableAbilityCount, rng);

        const s32 startAbilityTarget = startingAbilityTargetCount();
        const AbilityEntry* pendingStartingTargets[AbilityId_Count] = {};
        s32 pendingStartingTargetCount = 0;
        for (s32 i = 0; i < sLogicStartingAbilityCount
             && sStartingAbilityCount < startAbilityTarget;
             i++) {
            const AbilityEntry* ability = getAbilityById(sLogicStartingAbilities[i]);
            if (ability && canStartWithExtraAbility(*ability))
                addStartingAbility(*ability, pendingStartingTargets,
                    pendingStartingTargetCount, startingUnlocked);
        }
        for (s32 i = 0; i < unlockableAbilityCount && sStartingAbilityCount < startAbilityTarget; i++) {
            const AbilityEntry& ability = sAbilities[abilityOrder[i]];
            if (!canStartWithExtraAbility(ability))
                continue;
            addStartingAbility(ability, pendingStartingTargets, pendingStartingTargetCount, startingUnlocked);
        }

        s32 rewardOrder[AbilityId_Count];
        s32 rewardCount = 0;
        for (s32 i = 0; i < unlockableAbilityCount; i++) {
            const AbilityEntry& ability = sAbilities[abilityOrder[i]];
            if (isStartingAbility(ability))
                continue;
            rewardOrder[rewardCount++] = abilityOrder[i];
        }

        // Dark and Darker hold more hosts than there are abilities to hand out -
        // by roughly nine even at their vanilla size, and by several times that
        // once Moon Allocation shuffles a large excess onto them. Every host
        // still gets a slot; the ones past the reward pool wrap back around it
        // and are marked non-progression, exactly like the Cap/Cloud/Moon
        // selector slots below.
        //
        // Leaving the surplus slotless was the bug: unlockForMoon takes any
        // ability moon down its award path, found no slot to resolve and
        // reported AlreadyOwned, so the moon paid the 1500-coin duplicate bonus
        // and showed the unlock overlay while granting nothing. Now it resolves
        // a real ability - the assigned one, or the first unowned link in its
        // prerequisite chain - and only reports a duplicate once that ability is
        // genuinely owned.
        //
        // Only the first rewardCount slots are progression, so the route proof
        // and the logic tokens still see exactly one host per ability.
        if (moonSlotCount > 0 && rewardCount > 0) {
            for (s32 i = 0; i < moonSlotCount && sMoonAbilityCount < kMaxMoonAbilities; i++) {
                const MoonSlot& slot = moonSlots[i];
                const bool progression = i < rewardCount;
                // Scarce turned the surplus into fool moons before redistribution
                // existed. Unredistributed seeds keep that; a moon cannot be both
                // a fool and a reward host.
                if (!progression && !MoonRando::isKingdomRedistributionActive()
                    && difficulty == Randomizer::CaptureAbilityDifficulty_Scarce) {
                    if (const ShineDB::ShineEntry* moon = ShineDB::findEntry(slot.uid, slot.stage))
                        addFoolMoon(*moon, specialMoonSeed);
                    continue;
                }

                sMoonAbilities[sMoonAbilityCount++] = {
                    slot.uid,
                    slot.stage,
                    &sAbilities[rewardOrder[i % rewardCount]],
                    progression,
                };
            }
        }

        if (rewardCount > 0) {
            for (s32 i = 0; i < ShineDB::sShineTableSize && sMoonAbilityCount < kMaxMoonAbilities; i++) {
                const ShineDB::ShineEntry& moon = ShineDB::sShineTable[i];
                if (!specialMoonAwardsAbility(specialMoonSeed, moon))
                    continue;

                const s32 pick = (s32)(moonChoiceHash(attemptSeed ^ 0xAB1117EDu, moon) % (u32)rewardCount);
                sMoonAbilities[sMoonAbilityCount++] = {
                    moon.uid,
                    moon.stage,
                    &sAbilities[rewardOrder[pick]],
                    false,
                };
            }
        }

        // Randomizer::reinit() re-runs the whole dependency chain on every
        // stage-scene init; only report a table that differs from the last one.
        static const char sAbilityTableLogTag = 0;
        if (Randomizer::shouldNarrateBuild(&sAbilityTableLogTag, seed, attempt)) {
            const AbilityEntry* start = getAbilityById(sStartingCapThrow);
            s32 progressionSlots = 0;
            for (s32 i = 0; i < sMoonAbilityCount; i++)
                if (sMoonAbilities[i].progression)
                    progressionSlots++;
            Randomizer::logPush(
                "ability table: seed=%u attempt=%u %d Dark moons, %d slots (%d progression), start=%s, extra=%d, scarcity=%s",
                seed, attempt, moonCount, sMoonAbilityCount, progressionSlots,
                start ? start->displayName : "None",
                sStartingAbilityCount, Randomizer::getCaptureAbilityDifficultyName(difficulty));
        }

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastStartingEquipment = startingEquipment;
        sLastDifficulty = difficulty;
        sLastMoonRoleLayoutHash = moonRoleLayoutHash;
        sLastStartingAbilityTarget = startingAbilityTarget;
        sLastStartingCapThrowTarget = startingCapThrowTarget;
        sLastAbilityLockPoolMask = abilityLockPoolMask;
        sInitialized = true;
    }

    void clear() {
        sMoonAbilityCount = 0;
        sFoolMoonCount = 0;
        sStartingAbilityCount = 0;
        sLastUnlockedAbilityCount = 0;
        sLastDuplicateAbilityCount = 0;
        sLastProcessedMoonValid = false;
        sLastProcessedMoonUid = -1;
        sLastProcessedMoonStage = nullptr;
        sInitialized = false;
        sLastSeed = 0xFFFFFFFF;
        sLastAttempt = 0xFFFFFFFF;
        sLastStartingEquipment = Randomizer::StartingEquipment_Normal;
        sLastDifficulty = Randomizer::CaptureAbilityDifficulty_Normal;
        sLastMoonRoleLayoutHash = 0;
        sLastStartingAbilityTarget = 1;
        sLastStartingCapThrowTarget = 1;
        sLastAbilityLockPoolMask = ~0ull;
        sStartingCapThrow = AbilityId_NeutralThrow;
        sStartingCapThrowCount = 1;
        for (s32 i = 0; i < (s32)(sizeof(sStartingCapThrows)
                            / sizeof(sStartingCapThrows[0]));
             i++)
            sStartingCapThrows[i] = AbilityId_NeutralThrow;
    }

    bool isInitialized() { return sInitialized; }

    s32 getAbilityCount() { return AbilityId_Count; }

    s32 getRequiredProgressionSlotCount() {
        s32 required = 0;
        for (s32 i = 0; i < AbilityId_Count; i++)
            // A Movement Ability whose dial is off never needs a host, so it must not
            // be counted against the Dark/Darker Side slot budget either.
            if (!sAbilities[i].alwaysUnlocked
                && Randomizer::isAbilityInLockPool(i)
                && (!isMovementAbilityId(sAbilities[i].id)
                    || isMovementAbilityInPool(sAbilities[i].id)))
                required++;

        // The starting cap throw is selected from the same non-always-unlocked
        // pool but is never a reward. Starting movement abilities are removed
        // from that reward pool as well.
        if (sInitialized) {
            for (s32 i = 0; i < sStartingCapThrowCount; i++) {
                const s32 abilityId = static_cast<s32>(sStartingCapThrows[i]);
                if (Randomizer::isAbilityInLockPool(abilityId)
                    && required > 0)
                    required--;
            }
        } else if (required > 0) {
            const s32 startingThrowTarget = Randomizer::getStartingThrowCount();
            required -= startingThrowTarget < required ? startingThrowTarget : required;
        }
        required -= startingAbilityTargetCount();
        return required > 0 ? required : 0;
    }

    const AbilityEntry* getAbility(s32 index) {
        if (index < 0 || index >= AbilityId_Count)
            return nullptr;
        return &sAbilities[index];
    }

    s32 getUnlockableAbilityCount() {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        s32 count = 0;
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (isAbilityInUnlockPool(sAbilities[i]))
                count++;
        return count;
    }

    const AbilityEntry* getUnlockableAbility(s32 index) {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        if (index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < AbilityId_Count; i++) {
            if (!isAbilityInUnlockPool(sAbilities[i]))
                continue;
            if (found == index)
                return &sAbilities[i];
            found++;
        }
        return nullptr;
    }

    s32 getDisplayAbilityCount() {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        s32 count = 0;
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (isAbilityInDisplayList(sAbilities[i]))
                count++;
        return count;
    }

    const AbilityEntry* getDisplayAbility(s32 index) {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        return getDisplayAbilityByListIndex(index);
    }

    const char* getProgressiveDisplayName(const AbilityEntry& ability) {
        switch (ability.id) {
        case AbilityId_DoubleJump:
            return "Progressive Jump (1)";
        case AbilityId_TripleJump:
            return "Progressive Jump (2)";
        case AbilityId_GroundPound:
            return "Progressive Ground Pound";
        case AbilityId_GroundPoundJump:
            return "Progressive Ground Pound Jump";
        case AbilityId_Dive:
            return "Progressive Dive";
        case AbilityId_Roll:
            return "Progressive Roll (1)";
        case AbilityId_RollBoost:
            return "Progressive Roll (2)";
        case AbilityId_Backflip:
            return "Progressive Backflip";
        case AbilityId_LongJump:
            return "Progressive Long Jump";
        default:
            return ability.displayName ? ability.displayName : ability.key;
        }
    }

    s32 getStartingAbilityCount() {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        return sStartingAbilityCount;
    }

    const AbilityEntry* getStartingAbility(s32 index) {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        if (index < 0 || index >= sStartingAbilityCount)
            return nullptr;
        return sStartingAbilities[index];
    }

    s32 getStartingCapThrowCount() {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        return sStartingCapThrowCount;
    }

    const AbilityEntry* getStartingCapThrow(s32 index) {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        if (index < 0 || index >= sStartingCapThrowCount)
            return nullptr;
        return getAbilityById(sStartingCapThrows[index]);
    }

    void clearLogicStartingOverride() {
        sLogicStartingAbilityCount = 0;
        sLogicStartingCapThrowOverride = -1;
        sInitialized = false;
    }

    bool setLogicStartingOverride(const AbilityId* abilities, s32 count) {
        if (count < 0 || count > (s32)(sizeof(sLogicStartingAbilities) / sizeof(sLogicStartingAbilities[0])))
            return false;

        for (s32 i = 0; i < count; i++) {
            const AbilityEntry* ability = abilities ? getAbilityById(abilities[i]) : nullptr;
            if (!ability || !canStartWithExtraAbility(*ability))
                return false;
            for (s32 j = 0; j < i; j++)
                if (abilities[j] == abilities[i])
                    return false;
        }
        for (s32 i = 0; i < count; i++)
            sLogicStartingAbilities[i] = abilities[i];
        sLogicStartingAbilityCount = count;
        sInitialized = false;
        return true;
    }

    s32 getLogicStartingOverride(AbilityId* outAbilities, s32 maxCount) {
        if (outAbilities && maxCount > 0) {
            const s32 writeCount = maxCount < sLogicStartingAbilityCount
                ? maxCount
                : sLogicStartingAbilityCount;
            for (s32 i = 0; i < writeCount; i++)
                outAbilities[i] = sLogicStartingAbilities[i];
        }
        return sLogicStartingAbilityCount;
    }

    void setLogicStartingCapThrowOverride(AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        sLogicStartingCapThrowOverride = entry && entry->canStartWithSeed
            ? static_cast<s32>(ability)
            : -1;
        sInitialized = false;
    }

    AbilityId getLogicStartingCapThrowOverride() {
        return sLogicStartingCapThrowOverride >= 0
            ? static_cast<AbilityId>(sLogicStartingCapThrowOverride)
            : AbilityId_Count;
    }

    s32 getDebugAbilityCount() {
        s32 count = 0;
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (isAbilityInDebugMenu(sAbilities[i]))
                count++;
        return count;
    }

    const AbilityEntry* getDebugAbility(s32 index) {
        if (index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < AbilityId_Count; i++) {
            if (!isAbilityInDebugMenu(sAbilities[i]))
                continue;
            if (found == index)
                return &sAbilities[i];
            found++;
        }
        return nullptr;
    }

    const AbilityEntry* findAbilityByKey(const char* key) {
        if (!key || !key[0])
            return nullptr;
        for (s32 i = 0; i < AbilityId_Count; i++)
            if (std::strcmp(sAbilities[i].key, key) == 0)
                return &sAbilities[i];
        return nullptr;
    }

    const AbilityEntry* getStartingCapThrow() {
        return getStartingCapThrow(0);
    }

    s32 getLastUnlockedAbilityCount() { return sLastUnlockedAbilityCount; }

    const AbilityEntry* getLastUnlockedAbility(s32 index) {
        if (index < 0 || index >= sLastUnlockedAbilityCount)
            return nullptr;
        return sLastUnlockedAbilities[index];
    }

    s32 getLastDuplicateAbilityCount() { return sLastDuplicateAbilityCount; }

    const AbilityEntry* getLastDuplicateAbility(s32 index) {
        if (index < 0 || index >= sLastDuplicateAbilityCount)
            return nullptr;
        return sLastDuplicateAbilities[index];
    }

    bool isAbilityInSave(GameDataHolderAccessor accessor, AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry)
            return false;

        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return false;

        char flag[64];
        makeAbilityFlag(flag, sizeof(flag), *entry);
        return file->customIsExistInHackDictionary(flag);
    }

    bool isAbilityInSave(GameDataHolderAccessor accessor, const AbilityEntry& ability) {
        return isAbilityInSave(accessor, ability.id);
    }

    bool isAbilityUnlockedByMoon(GameDataHolderAccessor accessor, AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry || !Randomizer::isLockedAbilitiesEnabled())
            return false;

        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return false;

        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            const MoonAbilityEntry& moonAbility = sMoonAbilities[i];
            if (moonAbility.ability->id != entry->id)
                continue;
            if (file->customIsGotShine(moonAbility.uid, moonAbility.stage))
                return true;
        }
        return false;
    }

    bool isAbilityUnlockedByMoon(GameDataHolderAccessor accessor, const AbilityEntry& ability) {
        return isAbilityUnlockedByMoon(accessor, ability.id);
    }

    static bool isAbilityUnlockedForAward(GameDataHolderAccessor accessor, const AbilityEntry& ability) {
        if (ability.alwaysUnlocked)
            return true;
        if (isMovementAbilityGrantedById(ability.id))
            return true;
        if (ability.id == AbilityId_MoonLowGravity && isMoonGravityEarned(accessor))
            return true;
        if (sDebugOverrideActive[ability.id])
            return sDebugUnlocked[ability.id];
        if (ability.id == sStartingCapThrow)
            return true;
        if (isStartingAbility(ability))
            return true;
        return isAbilityInSave(accessor, ability);
    }

    static void buildAwardSnapshot(GameDataHolderAccessor accessor, bool* unlockedAtStart) {
        for (s32 i = 0; i < AbilityId_Count; i++)
            unlockedAtStart[i] = isAbilityUnlockedForAward(accessor, sAbilities[i]);
    }

    static const AbilityEntry* resolveAwardAbilityFromSnapshot(
        const bool* unlockedAtStart, const bool* awardedThisMoon, const AbilityEntry& ability) {
        // Progressive off: the slot hands over its own ability whole instead of
        // walking down to the first missing prerequisite, so the triple jump
        // arrives as the triple jump. setAbilityUnlocked() writes the rest of
        // the chain alongside it so the move actually works.
        if (Randomizer::isProgressiveAbilitiesEnabled() && ability.prerequisite >= 0) {
            const AbilityEntry* prerequisite = getAbilityById((AbilityId)ability.prerequisite);
            if (prerequisite && !unlockedAtStart[prerequisite->id] && !awardedThisMoon[prerequisite->id])
                return resolveAwardAbilityFromSnapshot(unlockedAtStart, awardedThisMoon, *prerequisite);
        }

        if (unlockedAtStart[ability.id] || awardedThisMoon[ability.id])
            return nullptr;
        return &ability;
    }

    // Whether `candidate` sits above `root` in the prerequisite tree - a direct
    // dependent, or a dependent of one. Crouch's descendants are Backflip, Long
    // Jump, Roll and (through Roll) Roll Boost.
    static bool isDescendantAbility(const AbilityEntry& candidate, const AbilityEntry& root) {
        const AbilityEntry* walk = &candidate;
        for (s32 depth = 0; walk && depth < AbilityId_Count; depth++) {
            if (walk->prerequisite < 0)
                return false;
            const AbilityEntry* prerequisite = getAbilityById((AbilityId)walk->prerequisite);
            if (!prerequisite)
                return false;
            if (prerequisite->id == root.id)
                return true;
            walk = prerequisite;
        }
        return false;
    }

    // The other half of resolveAwardAbilityFromSnapshot. That one walks DOWN a
    // progress path, handing over a missing prerequisite when the slot's own
    // ability is not yet reachable; this walks UP it, handing over the next
    // upgrade when the slot's ability is already owned. A slot is only a real
    // duplicate once the whole path is owned - "(again)" for a Crouch slot while
    // Roll or Long Jump are still outstanding is the bug this closes, and it
    // shows up whenever Crouch arrived from somewhere else (a Progressive Roll
    // moon downgrading to it, or a purple-coin shop purchase).
    //
    // Candidates are walked in sDisplayAbilityOrder, the progression-guide order
    // the Ability List renders, so the upgrade a player receives matches the
    // order they read the family in. Each candidate still goes back through
    // resolveAwardAbilityFromSnapshot, so a deeper descendant can never be
    // handed over before its own prerequisite.
    static const AbilityEntry* resolveUpgradeAwardFromSnapshot(
        const bool* unlockedAtStart, const bool* awardedThisMoon, const AbilityEntry& ability) {
        for (s32 i = 0; i < AbilityId_Count; i++) {
            const AbilityEntry& candidate = sAbilities[sDisplayAbilityOrder[i]];
            if (!isDescendantAbility(candidate, ability))
                continue;
            if (const AbilityEntry* awarded = resolveAwardAbilityFromSnapshot(
                    unlockedAtStart, awardedThisMoon, candidate))
                return awarded;
        }
        return nullptr;
    }

    static bool isMoonAbilityForMoon(const MoonAbilityEntry& moonAbility,
        const ShineDB::ShineEntry& moon) {
        return moonAbility.uid == moon.uid && std::strcmp(moonAbility.stage, moon.stage) == 0;
    }

    static bool isMoonAbilityCollected(GameDataFile* file, const ShineDB::ShineEntry& currentMoon,
        const MoonAbilityEntry& moonAbility) {
        if (isMoonAbilityForMoon(moonAbility, currentMoon))
            return true;
        return file && file->customIsGotShine(moonAbility.uid, moonAbility.stage);
    }

    static const AbilityEntry* findPendingAwardAbility(const bool* unlockedAtStart,
        const bool* awardedThisMoon, GameDataFile* file, const ShineDB::ShineEntry& currentMoon) {
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            const MoonAbilityEntry& moonAbility = sMoonAbilities[i];
            if (!isMoonAbilityCollected(file, currentMoon, moonAbility))
                continue;
            if (const AbilityEntry* awarded = resolveAwardAbilityFromSnapshot(unlockedAtStart, awardedThisMoon, *moonAbility.ability))
                return awarded;
        }
        return nullptr;
    }

    static void addPendingAwardTarget(const AbilityEntry** pendingTargets,
        s32& pendingTargetCount, const AbilityEntry& ability) {
        for (s32 i = 0; i < pendingTargetCount; i++)
            if (pendingTargets[i] && pendingTargets[i]->id == ability.id)
                return;
        if (pendingTargetCount < AbilityId_Count)
            pendingTargets[pendingTargetCount++] = &ability;
    }

    static const AbilityEntry* findPendingAwardTarget(
        const AbilityEntry* const* pendingTargets, s32 pendingTargetCount,
        const bool* unlockedAtStart, const bool* awardedThisMoon) {
        for (s32 i = 0; i < pendingTargetCount; i++) {
            if (!pendingTargets[i])
                continue;
            if (const AbilityEntry* awarded = resolveAwardAbilityFromSnapshot(
                    unlockedAtStart, awardedThisMoon, *pendingTargets[i]))
                return awarded;
        }
        return nullptr;
    }

    static const AbilityEntry* resolveAwardForSlot(
        const bool* unlockedAtStart, const bool* awardedThisMoon,
        const AbilityEntry& ability, const AbilityEntry** pendingTargets,
        s32& pendingTargetCount) {
        const AbilityEntry* awarded = resolveAwardAbilityFromSnapshot(
            unlockedAtStart, awardedThisMoon, ability);
        if (awarded) {
            if (awarded->id != ability.id)
                addPendingAwardTarget(pendingTargets, pendingTargetCount, ability);
            return awarded;
        }

        awarded = findPendingAwardTarget(
            pendingTargets, pendingTargetCount, unlockedAtStart, awardedThisMoon);
        if (awarded)
            return awarded;

        // Everything at or below this slot's ability is owned. Advance up its
        // progress path before giving up on the slot; only a fully owned path
        // is a duplicate.
        return resolveUpgradeAwardFromSnapshot(unlockedAtStart, awardedThisMoon, ability);
    }

    static s32 countDirectAbilitySlotsForMoon(const ShineDB::ShineEntry& moon) {
        s32 count = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++)
            if (isMoonAbilityForMoon(sMoonAbilities[i], moon))
                count++;
        return count;
    }

    static void awardAbility(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon,
        const AbilityEntry& ability, bool* awardedThisMoon) {
        setAbilityUnlocked(writer, ability, true);
        recordAbilityReveal(writer, ability);
        awardedThisMoon[ability.id] = true;
        if (sLastUnlockedAbilityCount < AbilityId_Count)
            sLastUnlockedAbilities[sLastUnlockedAbilityCount++] = &ability;

        Randomizer::logPush("ability unlock: %s via %s",
            ability.displayName, moon.name);
    }

    bool isAbilityDebugUnlocked(AbilityId ability) {
        return isValidAbilityId(ability) && sDebugOverrideActive[ability] && sDebugUnlocked[ability];
    }

    bool isAbilityDebugUnlocked(const AbilityEntry& ability) {
        return isAbilityDebugUnlocked(ability.id);
    }

    bool isAbilityDebugOverrideActive(AbilityId ability) {
        return isValidAbilityId(ability) && sDebugOverrideActive[ability];
    }

    bool isAbilityDebugOverrideActive(const AbilityEntry& ability) {
        return isAbilityDebugOverrideActive(ability.id);
    }

    void setAbilityDebugUnlocked(AbilityId ability, bool unlocked) {
        if (!isValidAbilityId(ability))
            return;
        sDebugOverrideActive[ability] = true;
        sDebugUnlocked[ability] = unlocked;
    }

    void setAbilityDebugUnlocked(const AbilityEntry& ability, bool unlocked) {
        setAbilityDebugUnlocked(ability.id, unlocked);
    }

    bool isAbilityUnlocked(GameDataHolderAccessor accessor, AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry)
            return true;

        if (!Randomizer::isLockedAbilitiesEnabled())
            return true;

        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        if (accessor.mData && Nuzlocke::isAbilityTemporarilyLocked(accessor.mData->getGameDataFile(), *entry))
            return false;

        bool unlocked = false;
        if (entry->alwaysUnlocked)
            unlocked = true;
        else if (accessor.mData && Nuzlocke::isAbilityExplicitlyRetained(accessor.mData->getGameDataFile(), *entry))
            unlocked = true;
        else if (sDebugOverrideActive[ability])
            unlocked = sDebugUnlocked[ability];
        // Below the debug override on purpose: the debug menu is how this page
        // gets tested, so it has to be able to force a granted Movement Ability
        // locked and a withheld one owned.
        //
        // A Movement Ability nobody is withholding is simply owned - there is no
        // save flag for it, because it was never taken away. Low gravity under a
        // moon-count mode is derived from the kingdom's moon total for the same
        // reason: the count already persists, so a second copy could only drift.
        else if (isMovementAbilityGrantedById(ability))
            unlocked = true;
        else if (ability == AbilityId_MoonLowGravity && isMoonGravityEarned(accessor))
            unlocked = true;
        else if (ability == sStartingCapThrow)
            unlocked = true;
        else if (isStartingAbility(*entry))
            unlocked = true;
        else if (!accessor.mData)
            unlocked = false;
        else
            unlocked = isAbilityInSave(accessor, *entry);

        if (!unlocked)
            return false;

        if (entry->requiresAnyCapThrow && !isAnyPrimaryCapThrowUnlocked(accessor))
            return false;

        if (entry->prerequisite >= 0)
            return isAbilityUnlocked(accessor, (AbilityId)entry->prerequisite);

        return true;
    }

    bool isAbilityUnlocked(GameDataHolderAccessor accessor, const AbilityEntry& ability) {
        return isAbilityUnlocked(accessor, ability.id);
    }

    bool isMovementAbility(AbilityId ability) { return isMovementAbilityId(ability); }

    bool isMovementAbilityGranted(AbilityId ability) {
        return isMovementAbilityGrantedById(ability);
    }

    s32 getMoonGravityCollected(GameDataHolderAccessor accessor) {
        if (!Randomizer::isMoonGravityCountLocked())
            return 0;
        return moonKingdomShineNum(accessor);
    }

    s32 getMoonGravityRequirement() {
        return Randomizer::getMoonGravityMoonRequirement();
    }

    const char* getSouvenirTextureName(AbilityId ability) {
        switch (ability) {
        case AbilityId_MoonLowGravity:
            return "SouvenirSky2";
        case AbilityId_Grab:
            return "SouvenirPeach1";
        case AbilityId_Swim:
            return "SouvenirPeach2";
        case AbilityId_JaxiRide:
            return "SouvenirSpecial1";
        case AbilityId_ScooterRide:
            return "SouvenirSpecial2";
        case AbilityId_RocketFlower:
            return "SouvenirDolphin1";
        case AbilityId_NpcTalk:
            return "SouvenirDolphin2";
        case AbilityId_Pipe2D:
            return "SouvenirSpace1";
        case AbilityId_TimerChallenge:
            return "SouvenirSpace2";
        default:
            return nullptr;
        }
    }

    bool isAbilityRevocable(AbilityId ability) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry || entry->alwaysUnlocked)
            return false;
        if (isMovementAbilityGrantedById(ability))
            return false;
        return !(ability == AbilityId_MoonLowGravity
            && Randomizer::isMoonGravityCountLocked());
    }

    void setAbilityUnlocked(GameDataHolderWriter writer, AbilityId ability, bool unlocked) {
        const AbilityEntry* entry = getAbilityById(ability);
        if (!entry || !writer.mData)
            return;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return;

        char flag[64];
        makeAbilityFlag(flag, sizeof(flag), *entry);
        if (unlocked)
            file->customAddHackDictionary(flag);
        else
            file->customRemoveHackDictionary(flag);

        // Progressive off: a move is only usable once everything below it in
        // the prerequisite tree is owned, so granting the final form grants the
        // whole chain. Bounded by AbilityId_Count so a malformed table cannot
        // spin here. Revoking is left alone - a prerequisite may still be
        // holding up a sibling branch (Crouch feeds Backflip, Long Jump, Roll).
        if (unlocked && !Randomizer::isProgressiveAbilitiesEnabled()) {
            const AbilityEntry* walk = entry;
            for (s32 depth = 0; walk && walk->prerequisite >= 0
                 && depth < AbilityId_Count;
                 depth++) {
                walk = getAbilityById((AbilityId)walk->prerequisite);
                if (!walk)
                    break;
                makeAbilityFlag(flag, sizeof(flag), *walk);
                file->customAddHackDictionary(flag);
            }
        }
        GameDataFunction::setRequireSave(writer);
    }

    void setAbilityUnlocked(GameDataHolderWriter writer, const AbilityEntry& ability, bool unlocked) {
        setAbilityUnlocked(writer, ability.id, unlocked);
    }

    void applyStartingEquipment(GameDataHolderWriter writer) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !writer.mData)
            return;
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        for (s32 i = 0; i < sStartingCapThrowCount; i++) {
            const AbilityEntry* capThrow = getAbilityById(sStartingCapThrows[i]);
            if (!capThrow)
                continue;
            setAbilityUnlocked(writer, *capThrow, true);
            Randomizer::logPush("starting cap throw: %s", capThrow->displayName);
        }

        for (s32 i = 0; i < sStartingAbilityCount; i++) {
            if (!sStartingAbilities[i])
                continue;
            setAbilityUnlocked(writer, *sStartingAbilities[i], true);
            Randomizer::logPush("starting ability: %s", sStartingAbilities[i]->displayName);
        }

        // Abilities omitted from the explicit pool are free at the start. Keep
        // their save state in sync with the pool-aware reward builder; otherwise
        // they would be excluded from placement but still appear locked to the
        // runtime unlock check.
        for (s32 i = 0; i < AbilityId_Count; i++) {
            const AbilityEntry& ability = sAbilities[i];
            if (ability.alwaysUnlocked || isMovementAbilityId(ability.id)
                || Randomizer::isAbilityInLockPool(i))
                continue;
            setAbilityUnlocked(writer, ability, true);
        }
    }

    void unlockAll(GameDataHolderWriter writer) {
        for (s32 i = 0; i < AbilityId_Count; i++) {
            sDebugOverrideActive[i] = true;
            sDebugUnlocked[i] = true;
            setAbilityUnlocked(writer, sAbilities[i], true);
        }
    }

    void clearAll(GameDataHolderWriter writer) {
        GameDataFile* file = writer.mData ? writer->getGameDataFile() : nullptr;
        if (file) {
            for (s32 index = 0; index < AbilityId_Count; index++) {
                for (s32 i = 0; i < AbilityId_Count; i++) {
                    char flag[96];
                    makeAbilityRevealFlag(flag, sizeof(flag), index, sAbilities[i]);
                    file->customRemoveHackDictionary(flag);
                }
            }
        }

        for (s32 i = 0; i < AbilityId_Count; i++) {
            sDebugOverrideActive[i] = true;
            sDebugUnlocked[i] = false;
            setAbilityUnlocked(writer, sAbilities[i], false);
        }
    }

    UnlockResult unlockForMoon(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon,
        bool alreadyCollected) {
        sLastUnlockedAbilityCount = 0;
        sLastDuplicateAbilityCount = 0;

        if (!Randomizer::isLockedAbilitiesEnabled() || !writer.mData)
            return UnlockResult::Skipped;

        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        const s32 directSlotTotal = countDirectAbilitySlotsForMoon(moon);
        if (!isAbilityMoon(moon) && directSlotTotal <= 0)
            return UnlockResult::Skipped;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return UnlockResult::Skipped;

        if (sLastProcessedMoonValid && sLastProcessedMoonUid == moon.uid && sLastProcessedMoonStage && std::strcmp(sLastProcessedMoonStage, moon.stage) == 0) {
            Randomizer::logPush("ability moon skipped duplicate: %s [%d %s]",
                moon.name, moon.uid, moon.stage);
            return UnlockResult::Skipped;
        }
        sLastProcessedMoonValid = true;
        sLastProcessedMoonUid = moon.uid;
        sLastProcessedMoonStage = moon.stage;

        // Re-collecting a moon must not advance a progressive ability: only the first collection
        // awards. We still claimed the dedup slot above so the GameDataFile::setGotShine award path
        // is suppressed; report AlreadyOwned so the caller can show "(again)" without granting.
        if (alreadyCollected) {
            Randomizer::logPush("ability moon re-collected, no advance: %s [%d %s]",
                moon.name, moon.uid, moon.stage);
            return UnlockResult::AlreadyOwned;
        }

        Nuzlocke::unlockAbilityForMoon(file, moon);

        bool unlockedAtStart[AbilityId_Count];
        bool awardedThisMoon[AbilityId_Count] = {};
        const AbilityEntry* pendingTargets[AbilityId_Count] = {};
        s32 pendingTargetCount = 0;
        buildAwardSnapshot(writer, unlockedAtStart);

        s32 directSlotCount = 0;
        for (s32 i = 0; i < sMoonAbilityCount; i++) {
            const MoonAbilityEntry& moonAbility = sMoonAbilities[i];
            if (!isMoonAbilityForMoon(moonAbility, moon))
                continue;
            directSlotCount++;

            const AbilityEntry* awarded = resolveAwardForSlot(
                unlockedAtStart, awardedThisMoon, *moonAbility.ability,
                pendingTargets, pendingTargetCount);
            if (!awarded) {
                // This slot's assigned ability (and any prerequisite chain) is already fully
                // owned. Report it instead of dropping it so the caller can show "(again)"
                // for this specific slot even when other slots of the same Multi moon are new.
                if (sLastDuplicateAbilityCount < AbilityId_Count)
                    sLastDuplicateAbilities[sLastDuplicateAbilityCount++] = moonAbility.ability;
                continue;
            }

            awardAbility(writer, moon, *awarded, awardedThisMoon);
        }

        if (sLastUnlockedAbilityCount == 0 && isAbilityMoon(moon)) {
            if (const AbilityEntry* pending = findPendingAwardAbility(unlockedAtStart, awardedThisMoon, file, moon)) {
                awardAbility(writer, moon, *pending, awardedThisMoon);

                // The fallback promotes this collection from a duplicate into the next
                // outstanding reward in a prerequisite chain.  Consume one direct
                // duplicate slot: it is the slot that advanced the chain, not a second
                // reward that should be shown as "(again)" beside the new ability.
                if (sLastDuplicateAbilityCount > 0)
                    sLastDuplicateAbilityCount--;
            }
        }

        if (sLastUnlockedAbilityCount == 0) {
            Randomizer::logPush("ability moon no unlock: %s [%d %s] direct=%d totalSlots=%d",
                moon.name, moon.uid, moon.stage, directSlotCount,
                directSlotTotal);
        }

        return sLastUnlockedAbilityCount > 0 ? UnlockResult::Unlocked : UnlockResult::AlreadyOwned;
    }

    bool isFoolMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized)
            return false;
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return true;
        return false;
    }

    Randomizer::FoolPunishment getFoolPunishment(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isLockedAbilitiesEnabled() || !sInitialized)
            return Randomizer::FoolPunishment::None;
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return sFoolPunishments[i];
        return Randomizer::FoolPunishment::None;
    }

} // namespace AbilityRando
