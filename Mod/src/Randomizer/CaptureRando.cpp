#include "Randomizer/CaptureRando.h"

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

namespace CaptureRando {

    static constexpr CaptureEntry sCaptures[] = {
        { "Frog", "Frog" },
        { "ElectricWire", "Spark Pylon" },
        { "KuriboWing", "Paragoomba" },
        { "Wanwan", "Chain Chomp" },
        { "WanwanBig", "Big Chain Chomp" },
        { "BreedaWanwan", "Broode's Chain Chomp" },
        { "TRex", "T-Rex" },
        { "Fukankun", "Binoculars" },
        { "Killer", "Bullet Bill" },
        { "Megane", "Moe-Eye" },
        { "Cactus", "Cactus" },
        { "Kuribo", "Goomba" },
        { "BossKnuckleHand", "Knucklotec's Fist" },
        { "BazookaElectric", "Mini Rocket" },
        { "Kakku", "Glydon" },
        { "JugemFishing", "Lakitu" },
        { "Fastener", "Zipper" },
        { "Pukupuku", "Cheep Cheep" },
        { "GotogotonLake", "Puzzle Part (Lake Kingdom)" },
        { "PackunPoison", "Poison Piranha Plant" },
        { "Senobi", "Uproot" },
        { "FireBros", "Fire Bro" },
        { "Tank", "Sherm" },
        { "Gamane", "Coin Coffer" },
        { "Tree", "Tree" },
        { "RockForest", "Boulder" },
        { "FukuwaraiFacePartsKuribo", "Picture Match Part (Goomba)" },
        { "Imomu", "Tropical Wiggler" },
        { "Guidepost", "Pole" },
        { "Manhole", "Manhole" },
        { "Car", "Taxi" },
        { "Radicon", "RC Car" },
        { "Byugo", "Ty-foo" },
        { "Yukimaru", "Shiverian Racer" },
        { "PukupukuSnow", "Cheep Cheep (Snow Kingdom)" },
        { "Hosui", "Gushen" },
        { "Bubble", "Lava Bubble" },
        { "HackFork", "Volbonan" },
        { "HammerBros", "Hammer Bro" },
        { "CarryMeat", "Meat" },
        { "PackunFire", "Fire Piranha Plant" },
        { "Tsukkun", "Pokio" },
        { "Statue", "Jizo" },
        { "StatueKoopa", "Bowser Statue" },
        { "KaronWing", "Parabones" },
        { "KillerMagnum", "Banzai Bill" },
        { "Bull", "Chargin' Chuck" },
        { "Koopa", "Bowser" },
        { "AnagramAlphabetCharacter", "Letter" },
        { "GotogotonCity", "Puzzle Part (Metro Kingdom)" },
        { "FukuwaraiFacePartsMario", "Picture Match Part (Mario)" },
        { "Yoshi", "Yoshi" },
    };

    static constexpr s32 kCaptureCount = sizeof(sCaptures) / sizeof(sCaptures[0]);
    // Sized for Moon Allocation's worst case, not the vanilla Mushroom list:
    // every Mushroom host carries a capture slot, a triple host carries three,
    // and the shuffled allocation profile can hand Mushroom the largest excess
    // in the set. Also bounds the Mushroom host scan below.
    static constexpr s32 kMaxMoonCaptures = 512;

    struct CaptureAlias {
        const char* alias;
        const char* hackName;
    };

    static constexpr CaptureAlias sCaptureAliases[] = {
        { "ElectricWireMover", "ElectricWire" },
        { "Jugem", "JugemFishing" },
        { "LavaBubble", "Bubble" },
        { "KoopaHack", "Koopa" },
        { "RadiconNpc", "Radicon" },
        { "StatueJizo", "Statue" },
        { "Yoshi", "Yoshi" },
    };

    struct MoonCaptureEntry {
        s32 uid;
        const char* stage;
        const CaptureEntry* capture;
        bool progression;
    };

    struct MoonKey {
        s32 uid;
        const char* stage;
    };

    static MoonCaptureEntry sMoonCaptures[kMaxMoonCaptures];
    static s32 sMoonCaptureCount = 0;
    static MoonKey sFoolMoons[kMaxMoonCaptures];
    static Randomizer::FoolPunishment sFoolPunishments[kMaxMoonCaptures];
    static s32 sFoolMoonCount = 0;
    static const CaptureEntry* sStartingCaptures[4];
    static s32 sStartingCaptureCount = 0;
    static s16 sLogicStartingCaptureIndexes[4] = {};
    static s32 sLogicStartingCaptureCount = 0;
    static bool sInitialized = false;
    static u32 sLastSeed = 0xFFFFFFFF;
    static u32 sLastAttempt = 0xFFFFFFFF;
    static Randomizer::StartingEquipment sLastStartingEquipment = Randomizer::StartingEquipment_Normal;
    static s32 sLastStartingCaptureTarget = 2;
    static Randomizer::CaptureAbilityDifficulty sLastDifficulty = Randomizer::CaptureAbilityDifficulty_Normal;
    static u32 sLastMoonRoleLayoutHash = 0;
    static u64 sLastCaptureLockPoolMaskLo = ~0ull;
    static u64 sLastCaptureLockPoolMaskHi = ~0ull;
    static bool sDebugOverrideActive[kCaptureCount] = {};
    static bool sDebugUnlocked[kCaptureCount] = {};

    static void shuffleIndices(s32* indices, s32 count, Randomizer::Rng& rng) {
        for (s32 i = count - 1; i > 0; i--) {
            s32 j = (s32)rng.nextModulo((u32)(i + 1));
            s32 tmp = indices[i];
            indices[i] = indices[j];
            indices[j] = tmp;
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

    static bool specialMoonAwardsCapture(u32 seed, const ShineDB::ShineEntry& moon) {
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
        if (!capturePossible)
            return false;
        if (abilityPossible)
            // This is the inverse of AbilityRando's bit above.
            return (moonChoiceHash(seed, moon) & 1u) == 0;
        return true;
    }

    static s32 startingCaptureTargetCount() {
        return Randomizer::getStartingCaptureCount();
    }

    static bool isStartingCapture(const CaptureEntry& capture) {
        for (s32 i = 0; i < sStartingCaptureCount; i++)
            if (sStartingCaptures[i] && std::strcmp(sStartingCaptures[i]->hackName, capture.hackName) == 0)
                return true;
        return false;
    }

    static void makeStartingCaptureFlag(char* out, size_t outSize, const CaptureEntry& capture) {
        nn::util::SNPrintf(out, outSize, "StartCapture_%s", capture.hackName ? capture.hackName : "");
    }

    static bool isStartingCaptureUnlocked(GameDataFile* file, const CaptureEntry& capture) {
        if (!file)
            return false;
        char flag[96];
        makeStartingCaptureFlag(flag, sizeof(flag), capture);
        return file->customIsExistInHackDictionary(flag);
    }

    static void setStartingCaptureUnlocked(GameDataFile* file, const CaptureEntry& capture, bool unlocked) {
        if (!file)
            return;
        char flag[96];
        makeStartingCaptureFlag(flag, sizeof(flag), capture);
        if (unlocked)
            file->customAddHackDictionary(flag);
        else
            file->customRemoveHackDictionary(flag);
    }

    static bool sameMoon(const MoonKey& key, const ShineDB::ShineEntry& moon) {
        return key.uid == moon.uid && key.stage && std::strcmp(key.stage, moon.stage) == 0;
    }

    static void addFoolMoon(const ShineDB::ShineEntry& moon, u32 seed) {
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return;
        if (sFoolMoonCount < kMaxMoonCaptures) {
            // Keep the result tied to the seed and moon rather than collection order.
            sFoolMoons[sFoolMoonCount++] = { moon.uid, moon.stage };
            sFoolPunishments[sFoolMoonCount - 1] = Randomizer::rollFoolPunishment(
                moonChoiceHash(seed ^ 0xF001B00Bu, moon), Randomizer::isNuzlockeRunMode());
        }
    }

    static GameDataFile* getDataFile(GameDataHolderAccessor accessor) {
        if (!accessor.mData)
            return nullptr;
        return accessor->getGameDataFile();
    }

    static const char* resolveCaptureHackName(const char* hackName) {
        if (!hackName || !hackName[0])
            return nullptr;
        for (s32 i = 0; i < (s32)(sizeof(sCaptureAliases) / sizeof(sCaptureAliases[0])); i++)
            if (std::strcmp(sCaptureAliases[i].alias, hackName) == 0)
                return sCaptureAliases[i].hackName;
        return hackName;
    }

    static s32 findCaptureIndexByHackName(const char* hackName) {
        const char* resolvedHackName = resolveCaptureHackName(hackName);
        if (!resolvedHackName)
            return -1;
        for (s32 i = 0; i < kCaptureCount; i++)
            if (std::strcmp(sCaptures[i].hackName, resolvedHackName) == 0)
                return i;
        return -1;
    }

    static void removeCaptureAliases(GameDataFile* file, const CaptureEntry& capture) {
        if (!file)
            return;
        for (s32 i = 0; i < (s32)(sizeof(sCaptureAliases) / sizeof(sCaptureAliases[0])); i++) {
            if (std::strcmp(sCaptureAliases[i].hackName, capture.hackName) == 0)
                file->customRemoveHackDictionary(sCaptureAliases[i].alias);
        }
    }

    static bool shouldWriteUnlockToHackDictionary(const CaptureEntry& capture) {
        return std::strcmp(capture.hackName, "Tree") != 0 && std::strcmp(capture.hackName, "RockForest") != 0;
    }

    static void removeUnsafeDictionaryUnlock(GameDataFile* file, const CaptureEntry& capture) {
        if (!file || shouldWriteUnlockToHackDictionary(capture))
            return;
        file->customRemoveHackDictionary(capture.hackName);
        removeCaptureAliases(file, capture);
    }

    bool isMushroomMoon(const ShineDB::ShineEntry& moon) {
        if (moon.uid == 0 || moon.objId < 0)
            return false;
        // Read the redistributed kingdom, not the vanilla home: the moon->kingdom
        // reshuffle runs before capture assignment, so the capture roster is
        // whatever pool ended up assigned to a capture kingdom (falls back to
        // home when redistribution is off).
        //
        // Which kingdoms those are is the Kingdom Moons page's answer, not
        // Mushroom alone - MoonRando routes capture-carrying moon value to the
        // same set, and if the two disagree the captures have nowhere to land.
        if (!Randomizer::isCaptureRewardKingdom(
                MoonRando::getAssignedKingdomForEntry(&moon)))
            return false;
        if (moon.flags & ShineDB::ShineFlag_Toadette)
            return false;
        return true;
    }

    static u32 seedForAttempt(u32 seed, u32 attempt) {
        return seed ^ (attempt * 0x165667B1u);
    }

    void buildRemapTable(u32 seed, u32 attempt) {
        const Randomizer::StartingEquipment startingEquipment = Randomizer::getStartingEquipment();
        const Randomizer::CaptureAbilityDifficulty difficulty = Randomizer::getCaptureAbilityDifficulty();
        const u32 moonRoleLayoutHash = MoonRando::getMoonRoleLayoutHash();
        const s32 startingCaptureTarget = startingCaptureTargetCount();
        const u64 captureLockPoolMaskLo = Randomizer::getCaptureLockPoolMask(0);
        const u64 captureLockPoolMaskHi = Randomizer::getCaptureLockPoolMask(1);
        if (sInitialized && sLastSeed == seed && sLastAttempt == attempt
            && sLastStartingEquipment == startingEquipment
            && sLastStartingCaptureTarget == startingCaptureTarget
            && sLastDifficulty == difficulty
            && sLastMoonRoleLayoutHash == moonRoleLayoutHash
            && sLastCaptureLockPoolMaskLo == captureLockPoolMaskLo
            && sLastCaptureLockPoolMaskHi == captureLockPoolMaskHi)
            return;

        sMoonCaptureCount = 0;
        sFoolMoonCount = 0;
        sStartingCaptureCount = 0;

        s32 mushroomEntries[kMaxMoonCaptures];
        s32 mushroomCount = 0;
        for (s32 i = 0; i < ShineDB::sShineTableSize && mushroomCount < kMaxMoonCaptures; i++) {
            if (isMushroomMoon(ShineDB::sShineTable[i]))
                mushroomEntries[mushroomCount++] = i;
        }

        s32 captureOrder[kCaptureCount];
        for (s32 i = 0; i < kCaptureCount; i++)
            captureOrder[i] = i;

        const u32 attemptSeed = seedForAttempt(seed, attempt);
        // Shared with AbilityRando's specialMoonSeed: the AbilitiesOrCaptures coin flip must
        // hash off the same input in both files (see specialMoonAwardsCapture), not each
        // domain's own obfuscated attemptSeed, or the two choices stop being true opposites.
        const u32 specialMoonSeed = seed ^ attempt;
        Randomizer::Rng rng = Randomizer::makeRng(attemptSeed, Randomizer::RngStream::Capture);
        shuffleIndices(captureOrder, kCaptureCount, rng);

        const s32 startTarget = startingCaptureTarget < 0 ? 0
                                                          : (startingCaptureTarget > (s32)(sizeof(sStartingCaptures) / sizeof(sStartingCaptures[0]))
                                                                    ? (s32)(sizeof(sStartingCaptures) / sizeof(sStartingCaptures[0]))
                                                                    : startingCaptureTarget);
        for (s32 i = 0; i < sLogicStartingCaptureCount
             && sStartingCaptureCount < startTarget;
             i++) {
            const s32 index = sLogicStartingCaptureIndexes[i];
            if (index < 0 || index >= kCaptureCount
                || !Randomizer::isCaptureInLockPool(index))
                continue;
            bool duplicate = false;
            for (s32 j = 0; j < sStartingCaptureCount; j++)
                duplicate |= sStartingCaptures[j] == &sCaptures[index];
            if (!duplicate)
                sStartingCaptures[sStartingCaptureCount++] = &sCaptures[index];
        }
        for (s32 i = 0; i < kCaptureCount && sStartingCaptureCount < startTarget; i++)
            if (Randomizer::isCaptureInLockPool(captureOrder[i])
                && !isStartingCapture(sCaptures[captureOrder[i]]))
                sStartingCaptures[sStartingCaptureCount++] = &sCaptures[captureOrder[i]];

        s32 rewardOrder[kCaptureCount];
        s32 rewardCount = 0;
        for (s32 i = 0; i < kCaptureCount; i++) {
            const CaptureEntry& capture = sCaptures[captureOrder[i]];
            if (!Randomizer::isCaptureInLockPool(captureOrder[i])
                || isStartingCapture(capture))
                continue;
            rewardOrder[rewardCount++] = captureOrder[i];
        }

        // Mushroom holds more hosts than there are captures to hand out, and Moon
        // Allocation can make it hold several times more. Every host still gets a
        // slot; the ones past the reward pool wrap back around it and are marked
        // non-progression, so the route proof and the logic tokens still see
        // exactly one host per capture. A surplus host that used to be slotless
        // paid nothing at all - no capture, no duplicate bonus, and no filler
        // coins, since Mushroom has no moon requirement to count toward either.
        s32 captureIndex = 0;
        for (s32 i = 0; i < mushroomCount && rewardCount > 0; i++) {
            const ShineDB::ShineEntry& moon = ShineDB::sShineTable[mushroomEntries[i]];
            const s32 unlockCount = MoonRando::isRewardMultiEntry(&moon) ? 3 : 1;

            // Scarce-mode fool conversion is a whole-moon decision. If the
            // progression pool runs out halfway through a triple, keep filling
            // its remaining slots with wrapped duplicates; otherwise a grand
            // reward can incorrectly expose only one or two captures. Only a
            // host reached after the pool is already exhausted becomes a fool.
            if (captureIndex >= rewardCount
                && !MoonRando::isKingdomRedistributionActive()
                && difficulty == Randomizer::CaptureAbilityDifficulty_Scarce) {
                addFoolMoon(moon, specialMoonSeed);
                continue;
            }

            for (s32 j = 0; j < unlockCount && sMoonCaptureCount < kMaxMoonCaptures; j++) {
                const bool progression = captureIndex < rewardCount;
                const CaptureEntry* capture = &sCaptures[rewardOrder[captureIndex % rewardCount]];
                sMoonCaptures[sMoonCaptureCount++] = { moon.uid, moon.stage, capture, progression };
                captureIndex++;
            }
        }

        if (rewardCount > 0) {
            for (s32 i = 0; i < ShineDB::sShineTableSize && sMoonCaptureCount < kMaxMoonCaptures; i++) {
                const ShineDB::ShineEntry& moon = ShineDB::sShineTable[i];
                if (!specialMoonAwardsCapture(specialMoonSeed, moon))
                    continue;

                const s32 pick = (s32)(moonChoiceHash(attemptSeed ^ 0xCA97A11u, moon) % (u32)rewardCount);
                const CaptureEntry* capture = &sCaptures[rewardOrder[pick]];
                sMoonCaptures[sMoonCaptureCount++] = { moon.uid, moon.stage, capture, false };
            }
        }

        // Randomizer::reinit() re-runs the whole dependency chain on every
        // stage-scene init, so this line was repeating verbatim on every stage
        // load (twice, once per rebuilt attempt). Only report a table that
        // actually differs from the one already reported.
        static const char sCaptureTableLogTag = 0;
        if (Randomizer::shouldNarrateBuild(&sCaptureTableLogTag, seed, attempt)) {
            Randomizer::logPush(
                "capture table: seed=%u attempt=%u %d Mushroom moons, %d rewards, %d starting, scarcity=%s",
                seed, attempt, mushroomCount, sMoonCaptureCount, sStartingCaptureCount,
                Randomizer::getCaptureAbilityDifficultyName(difficulty));
        }

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastStartingEquipment = startingEquipment;
        sLastStartingCaptureTarget = startingCaptureTarget;
        sLastDifficulty = difficulty;
        sLastMoonRoleLayoutHash = moonRoleLayoutHash;
        sLastCaptureLockPoolMaskLo = captureLockPoolMaskLo;
        sLastCaptureLockPoolMaskHi = captureLockPoolMaskHi;
        sInitialized = true;
    }

    void clear() {
        sMoonCaptureCount = 0;
        sFoolMoonCount = 0;
        sStartingCaptureCount = 0;
        sInitialized = false;
        sLastSeed = 0xFFFFFFFF;
        sLastAttempt = 0xFFFFFFFF;
        sLastStartingEquipment = Randomizer::StartingEquipment_Normal;
        sLastStartingCaptureTarget = 2;
        sLastDifficulty = Randomizer::CaptureAbilityDifficulty_Normal;
        sLastMoonRoleLayoutHash = 0;
        sLastCaptureLockPoolMaskLo = ~0ull;
        sLastCaptureLockPoolMaskHi = ~0ull;
    }

    bool isInitialized() { return sInitialized; }

    s32 getCaptureCount() { return kCaptureCount; }

    s32 getRequiredProgressionSlotCount() {
        s32 poolCount = 0;
        for (s32 i = 0; i < kCaptureCount; i++)
            if (Randomizer::isCaptureInLockPool(i))
                poolCount++;
        const s32 startTarget = startingCaptureTargetCount() < poolCount
            ? startingCaptureTargetCount()
            : poolCount;
        const s32 required = poolCount - startTarget;
        return required > 0 ? required : 0;
    }

    const CaptureEntry* getCapture(s32 index) {
        if (index < 0 || index >= kCaptureCount)
            return nullptr;
        return &sCaptures[index];
    }

    const CaptureEntry* findCaptureByHackName(const char* hackName) {
        const s32 index = findCaptureIndexByHackName(hackName);
        if (index < 0)
            return nullptr;
        return &sCaptures[index];
    }

    bool canGrantCaptureDirectly(const CaptureEntry& capture) {
        return shouldWriteUnlockToHackDictionary(capture);
    }

    s32 getStartingCaptureCount() {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        return sStartingCaptureCount;
    }

    const CaptureEntry* getStartingCapture(s32 index) {
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());
        if (index < 0 || index >= sStartingCaptureCount)
            return nullptr;
        return sStartingCaptures[index];
    }

    void clearLogicStartingOverride() {
        sLogicStartingCaptureCount = 0;
        sInitialized = false;
    }

    bool setLogicStartingOverride(const s16* captureIndexes, s32 count) {
        if (count < 0 || count > (s32)(sizeof(sLogicStartingCaptureIndexes) / sizeof(sLogicStartingCaptureIndexes[0])))
            return false;
        for (s32 i = 0; i < count; i++) {
            const s32 index = captureIndexes ? captureIndexes[i] : -1;
            if (index < 0 || index >= kCaptureCount)
                return false;
            for (s32 j = 0; j < i; j++)
                if (captureIndexes[j] == captureIndexes[i])
                    return false;
        }
        for (s32 i = 0; i < count; i++)
            sLogicStartingCaptureIndexes[i] = captureIndexes[i];
        sLogicStartingCaptureCount = count;
        sInitialized = false;
        return true;
    }

    s32 getLogicStartingOverride(s16* outCaptureIndexes, s32 maxCount) {
        if (outCaptureIndexes && maxCount > 0) {
            const s32 writeCount = maxCount < sLogicStartingCaptureCount
                ? maxCount
                : sLogicStartingCaptureCount;
            for (s32 i = 0; i < writeCount; i++)
                outCaptureIndexes[i] = sLogicStartingCaptureIndexes[i];
        }
        return sLogicStartingCaptureCount;
    }

    s32 getCaptureCountForMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized)
            return 0;

        s32 count = 0;
        for (s32 i = 0; i < sMoonCaptureCount; i++)
            if (sMoonCaptures[i].uid == moon.uid && std::strcmp(sMoonCaptures[i].stage, moon.stage) == 0)
                count++;
        return count;
    }

    s32 getProgressionCaptureCountForMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized)
            return 0;

        s32 count = 0;
        for (s32 i = 0; i < sMoonCaptureCount; i++)
            if (sMoonCaptures[i].progression && sMoonCaptures[i].uid == moon.uid && std::strcmp(sMoonCaptures[i].stage, moon.stage) == 0)
                count++;
        return count;
    }

    const CaptureEntry* getCaptureForMoon(s32 uid, const char* stage, s32 index) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized || !stage)
            return nullptr;
        if (index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            if (sMoonCaptures[i].uid != uid || std::strcmp(sMoonCaptures[i].stage, stage) != 0)
                continue;
            if (found == index)
                return sMoonCaptures[i].capture;
            found++;
        }
        return nullptr;
    }

    const CaptureEntry* getCaptureForMoon(s32 uid, const char* stage) {
        return getCaptureForMoon(uid, stage, 0);
    }

    const CaptureEntry* getCaptureForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        return getCaptureForMoon(moon.uid, moon.stage, index);
    }

    const CaptureEntry* getCaptureForMoon(const ShineDB::ShineEntry& moon) {
        return getCaptureForMoon(moon, 0);
    }

    bool isProgressionCaptureSlotForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized || index < 0)
            return false;

        s32 found = 0;
        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            if (sMoonCaptures[i].uid != moon.uid || std::strcmp(sMoonCaptures[i].stage, moon.stage) != 0)
                continue;
            if (found == index)
                return sMoonCaptures[i].progression;
            found++;
        }
        return false;
    }

    const CaptureEntry* getProgressionCaptureForMoon(const ShineDB::ShineEntry& moon, s32 index) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized || index < 0)
            return nullptr;

        s32 found = 0;
        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            if (!sMoonCaptures[i].progression || sMoonCaptures[i].uid != moon.uid || std::strcmp(sMoonCaptures[i].stage, moon.stage) != 0)
                continue;
            if (found == index)
                return sMoonCaptures[i].capture;
            found++;
        }
        return nullptr;
    }

    const ShineDB::ShineEntry* getMoonForCapture(const char* hackName) {
        const CaptureEntry* capture = findCaptureByHackName(hackName);
        if (!Randomizer::isCaptureRandoEnabled() || !capture)
            return nullptr;
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            const MoonCaptureEntry& moonCapture = sMoonCaptures[i];
            if (std::strcmp(moonCapture.capture->hackName, capture->hackName) == 0)
                return ShineDB::findEntry(moonCapture.uid, moonCapture.stage);
        }
        return nullptr;
    }

    const ShineDB::ShineEntry* getMoonForCapture(const CaptureEntry& capture) {
        return getMoonForCapture(capture.hackName);
    }

    bool isFrogMoon(const ShineDB::ShineEntry& moon) {
        const CaptureEntry* capture = getCaptureForMoon(moon);
        return capture && std::strcmp(capture->hackName, "Frog") == 0;
    }

    bool isCaptureInGallery(GameDataHolderAccessor accessor, const char* hackName) {
        const CaptureEntry* capture = findCaptureByHackName(hackName);
        if (!capture)
            return false;

        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return false;
        if (!shouldWriteUnlockToHackDictionary(*capture))
            return false;
        if (file->customIsExistInHackDictionary(capture->hackName))
            return true;
        for (s32 i = 0; i < (s32)(sizeof(sCaptureAliases) / sizeof(sCaptureAliases[0])); i++) {
            if (std::strcmp(sCaptureAliases[i].hackName, capture->hackName) == 0 && file->customIsExistInHackDictionary(sCaptureAliases[i].alias))
                return true;
        }
        return false;
    }

    bool isCaptureInGallery(GameDataHolderAccessor accessor, const CaptureEntry& capture) {
        return isCaptureInGallery(accessor, capture.hackName);
    }

    bool isCaptureDebugUnlocked(const char* hackName) {
        const s32 index = findCaptureIndexByHackName(hackName);
        return index >= 0 && sDebugOverrideActive[index] && sDebugUnlocked[index];
    }

    bool isCaptureDebugUnlocked(const CaptureEntry& capture) {
        return isCaptureDebugUnlocked(capture.hackName);
    }

    bool isCaptureDebugOverrideActive(const char* hackName) {
        const s32 index = findCaptureIndexByHackName(hackName);
        return index >= 0 && sDebugOverrideActive[index];
    }

    bool isCaptureDebugOverrideActive(const CaptureEntry& capture) {
        return isCaptureDebugOverrideActive(capture.hackName);
    }

    void setCaptureDebugUnlocked(const char* hackName, bool unlocked) {
        const s32 index = findCaptureIndexByHackName(hackName);
        if (index >= 0) {
            sDebugOverrideActive[index] = true;
            sDebugUnlocked[index] = unlocked;
        }
    }

    void setCaptureDebugUnlocked(const CaptureEntry& capture, bool unlocked) {
        setCaptureDebugUnlocked(capture.hackName, unlocked);
    }

    static bool isCaptureUnlockedByMoonImpl(
        GameDataHolderAccessor accessor, const CaptureEntry& capture) {
        if (!Randomizer::isCaptureRandoEnabled())
            return false;
        GameDataFile* file = getDataFile(accessor);
        if (!file)
            return false;
        removeUnsafeDictionaryUnlock(file, capture);

        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            const MoonCaptureEntry& moonCapture = sMoonCaptures[i];
            if (std::strcmp(moonCapture.capture->hackName, capture.hackName) != 0)
                continue;
            if (file->customIsGotShine(moonCapture.uid, moonCapture.stage))
                return true;
        }
        return false;
    }

    bool isCaptureUnlockedByMoon(GameDataHolderAccessor accessor, const char* hackName) {
        const CaptureEntry* capture = findCaptureByHackName(hackName);
        return capture && isCaptureUnlockedByMoonImpl(accessor, *capture);
    }

    bool isCaptureUnlockedByMoon(GameDataHolderAccessor accessor, const CaptureEntry& capture) {
        return isCaptureUnlockedByMoonImpl(accessor, capture);
    }

    bool isCaptureUnlocked(GameDataHolderAccessor accessor, const char* hackName) {
        if (!Randomizer::isCaptureRandoEnabled())
            return true;
        const CaptureEntry* capture = findCaptureByHackName(hackName);
        if (!capture)
            return true;

        const s32 captureIndex = findCaptureIndexByHackName(capture->hackName);
        if (captureIndex >= 0 && sDebugOverrideActive[captureIndex])
            return sDebugUnlocked[captureIndex];

        GameDataFile* file = getDataFile(accessor);
        removeUnsafeDictionaryUnlock(file, *capture);

        if (Nuzlocke::isCaptureTemporarilyLocked(file, *capture))
            return false;
        if (Nuzlocke::isCaptureExplicitlyRetained(file, *capture))
            return true;

        // The pool page defines which captures need progression slots. Captures
        // outside that pool are available from the beginning and must not wait
        // for a moon assignment that will never be generated for them.
        if (captureIndex >= 0 && !Randomizer::isCaptureInLockPool(captureIndex))
            return true;

        if (isStartingCaptureUnlocked(file, *capture))
            return true;

        if (isCaptureInGallery(accessor, *capture))
            return true;

        return isCaptureUnlockedByMoonImpl(accessor, *capture);
    }

    bool isCaptureUnlocked(GameDataHolderAccessor accessor, const CaptureEntry& capture) {
        return isCaptureUnlocked(accessor, capture.hackName);
    }

    void setCaptureUnlocked(GameDataHolderWriter writer, const char* hackName, bool unlocked) {
        const CaptureEntry* capture = findCaptureByHackName(hackName);
        if (!capture || !writer.mData)
            return;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return;

        if (unlocked && shouldWriteUnlockToHackDictionary(*capture))
            file->addHackDictionary(capture->hackName);
        else {
            file->customRemoveHackDictionary(capture->hackName);
            removeCaptureAliases(file, *capture);
            setStartingCaptureUnlocked(file, *capture, false);
        }
        GameDataFunction::setRequireSave(writer);
    }

    void setCaptureUnlocked(GameDataHolderWriter writer, const CaptureEntry& capture, bool unlocked) {
        setCaptureUnlocked(writer, capture.hackName, unlocked);
    }

    void applyStartingEquipment(GameDataHolderWriter writer) {
        if (!Randomizer::isCaptureRandoEnabled() || !writer.mData)
            return;
        if (!sInitialized)
            buildRemapTable(Randomizer::getSeed());

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return;

        for (s32 i = 0; i < sStartingCaptureCount; i++) {
            if (!sStartingCaptures[i])
                continue;
            setStartingCaptureUnlocked(file, *sStartingCaptures[i], true);
            if (shouldWriteUnlockToHackDictionary(*sStartingCaptures[i]))
                file->addHackDictionary(sStartingCaptures[i]->hackName);
            Randomizer::logPush("starting capture: %s", sStartingCaptures[i]->displayName);
        }

        // A capture omitted from the lock pool is an explicit freebie. Register
        // it in the gallery as part of new-save setup so the list and its count
        // agree with the unlock predicate from the first frame.
        for (s32 i = 0; i < kCaptureCount; i++) {
            if (Randomizer::isCaptureInLockPool(i))
                continue;
            const CaptureEntry& capture = sCaptures[i];
            setStartingCaptureUnlocked(file, capture, true);
            if (shouldWriteUnlockToHackDictionary(capture)
                && !file->customIsExistInHackDictionary(capture.hackName))
                file->addHackDictionary(capture.hackName);
        }
        GameDataFunction::setRequireSave(writer);
    }

    void unlockAll(GameDataHolderWriter writer) {
        for (s32 i = 0; i < kCaptureCount; i++) {
            sDebugOverrideActive[i] = true;
            sDebugUnlocked[i] = true;
            setCaptureUnlocked(writer, sCaptures[i], true);
        }
    }

    void clearAll(GameDataHolderWriter writer) {
        for (s32 i = 0; i < kCaptureCount; i++) {
            sDebugOverrideActive[i] = true;
            sDebugUnlocked[i] = false;
            setCaptureUnlocked(writer, sCaptures[i], false);
        }
    }

    void unlockForMoon(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon) {
        const CaptureEntry* capture = getCaptureForMoon(moon);
        if (!capture || !writer.mData)
            return;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return;

        Nuzlocke::unlockCaptureForMoon(file, moon);

        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            const MoonCaptureEntry& moonCapture = sMoonCaptures[i];
            if (moonCapture.uid != moon.uid || std::strcmp(moonCapture.stage, moon.stage) != 0)
                continue;

            // Tree / RockForest can't be safely written to the hack dictionary; they stay
            // unlocked purely via the dynamic moon check (isCaptureUnlockedByMoon).
            removeUnsafeDictionaryUnlock(file, *moonCapture.capture);
            if (!shouldWriteUnlockToHackDictionary(*moonCapture.capture))
                continue;

            // Already registered (e.g. re-collect, or the other setGotShine award path ran
            // first this event): nothing to do.
            if (file->customIsExistInHackDictionary(moonCapture.capture->hackName))
                continue;

            // Register the capture in the game's gallery so the Capture List reveals its name
            // and counts it, matching how starting captures are unlocked. Without this the entry
            // reads as unlocked (via isCaptureUnlockedByMoon) but shows "???" with a stale count.
            file->addHackDictionary(moonCapture.capture->hackName);
            GameDataFunction::setRequireSave(writer);
            Randomizer::logPush("capture unlock: %s via %s",
                moonCapture.capture->displayName, moon.name);
        }
    }

    static bool isCaptureOwnedExcludingMoon(GameDataFile* file, const CaptureEntry& capture,
        const ShineDB::ShineEntry& excludeMoon) {
        if (!file)
            return false;

        if (isStartingCaptureUnlocked(file, capture))
            return true;

        if (shouldWriteUnlockToHackDictionary(capture) && file->customIsExistInHackDictionary(capture.hackName))
            return true;
        for (s32 i = 0; i < (s32)(sizeof(sCaptureAliases) / sizeof(sCaptureAliases[0])); i++)
            if (std::strcmp(sCaptureAliases[i].hackName, capture.hackName) == 0 && file->customIsExistInHackDictionary(sCaptureAliases[i].alias))
                return true;

        for (s32 i = 0; i < sMoonCaptureCount; i++) {
            const MoonCaptureEntry& moonCapture = sMoonCaptures[i];
            if (std::strcmp(moonCapture.capture->hackName, capture.hackName) != 0)
                continue;
            if (moonCapture.uid == excludeMoon.uid && std::strcmp(moonCapture.stage, excludeMoon.stage) == 0)
                continue;
            if (file->customIsGotShine(moonCapture.uid, moonCapture.stage))
                return true;
        }
        return false;
    }

    bool grantsNewCapture(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized || !writer.mData)
            return false;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return false;

        const s32 count = getCaptureCountForMoon(moon);
        for (s32 i = 0; i < count; i++) {
            const CaptureEntry* capture = getCaptureForMoon(moon, i);
            if (capture && !isCaptureOwnedExcludingMoon(file, *capture, moon))
                return true;
        }
        return false;
    }

    bool isFoolMoon(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized)
            return false;
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return true;
        return false;
    }

    Randomizer::FoolPunishment getFoolPunishment(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized)
            return Randomizer::FoolPunishment::None;
        for (s32 i = 0; i < sFoolMoonCount; i++)
            if (sameMoon(sFoolMoons[i], moon))
                return sFoolPunishments[i];
        return Randomizer::FoolPunishment::None;
    }

    u32 getDuplicateCaptureSlotMask(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isCaptureRandoEnabled() || !sInitialized || !writer.mData)
            return 0;

        GameDataFile* file = writer->getGameDataFile();
        if (!file)
            return 0;

        u32 mask = 0;
        const s32 count = getCaptureCountForMoon(moon);
        for (s32 i = 0; i < count && i < 32; i++) {
            const CaptureEntry* capture = getCaptureForMoon(moon, i);
            if (capture && isCaptureOwnedExcludingMoon(file, *capture, moon))
                mask |= (1u << i);
        }
        return mask;
    }

} // namespace CaptureRando
