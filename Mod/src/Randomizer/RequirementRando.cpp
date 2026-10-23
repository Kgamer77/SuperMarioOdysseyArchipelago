#include "Randomizer/RequirementRando.h"

#include "Randomizer/KingdomRando.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/WorldMeta.h"

namespace RequirementRando {

    static constexpr s32 kRegularModifiable[] = { 1, 2, 3, 4, 6, 7, 8, 9, 10, 11, 12 };
    static constexpr s32 kRegularModifiableCount = sizeof(kRegularModifiable) / sizeof(kRegularModifiable[0]);
    static constexpr s32 kVanillaRequirementTotal = 124;
    static constexpr s32 kVanillaAverageRequirement = (kVanillaRequirementTotal + kRegularModifiableCount / 2) / kRegularModifiableCount;
    static constexpr s32 kMoonNormalRequirement = 7;
    // Inclusive bounds of the Extra Moon Requirements surcharge.
    static constexpr s32 kMoonGravityExtraMin = 5;
    static constexpr s32 kMoonGravityExtraMax = 10;
    // Distinct stream salt: the surcharge must not consume draws from the
    // requirement distribution, or adding it would change every existing seed.
    static constexpr u32 kMoonGravitySeedSalt = 0x47726176u; // "Grav"
    static constexpr s32 kMoonTotalsByKingdom[WorldMeta::sWorldCount] = {
        31,
        42,
        93,
        80,
        44,
        9,
        35,
        85,
        73,
        57,
        72,
        12,
        64,
        38,
        55,
        26,
        3,
    };

    static s32 sRequirements[WorldMeta::sWorldCount];
    static s32 sBaseRequirements[WorldMeta::sWorldCount];
    // What the player typed on the Moon Requirement advanced page, per kingdom,
    // or -1 where they have not. Untouched, the distribution below decides
    // everything as it always did; an authored value replaces the distribution's
    // answer for that kingdom and then still takes the per-seed variance, so it
    // is "about this many", not exactly.
    static s16 sAuthoredBase[WorldMeta::sWorldCount] = {};
    static bool sAuthoredBaseReady = false;
    // Same idea for the spread. A deviation is signed and zero is meaningful
    // ("this kingdom never moves"), so the "not set" marker cannot be -1 or 0.
    constexpr s16 cDeviationUnset = 0x7FFF;
    static s16 sAuthoredDeviation[WorldMeta::sWorldCount] = {};
    static bool sAuthoredDeviationReady = false;
    static bool sInitialized = false;
    // Low-gravity surcharge, in Moon Kingdom moons. Rolled every rebuild so the
    // value is a pure function of (seed, attempt) rather than of the mode that
    // reads it - flipping Moon Gravity between modes never re-rolls it.
    static s32 sMoonGravityExtra = kMoonGravityExtraMin;

    // Which kingdoms this route can put a moon requirement on: every kingdom
    // whose moons are set to Progress (Kingdom Moons page).
    //
    // Mushroom, Dark Side and Darker Side are not among them. They sit off the
    // frontier route entirely - KingdomProgress tracks fourteen kingdoms and
    // none of these is one - so there is no globe to deposit at and nothing a
    // requirement there would gate. The Kingdom Moons page does not offer them
    // Progress either; the two have to agree.
    static bool includesRequirement(s32 worldId) {
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return false;
        if (!Randomizer::isKingdomMoonUseSelected(
                worldId, Randomizer::SpecialMoonUse_KingdomUnlock))
            return false;
        switch (worldId) {
        case WorldMeta::WorldId_Mushroom:
        case WorldMeta::WorldId_Dark:
        case WorldMeta::WorldId_Darker:
            return false;
        default:
            return true;
        }
    }

    static void resetToVanilla() {
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++) {
            // Dark and Darker Side keep their vanilla numbers whatever their
            // moons hand out: those are the gates for entering them, and
            // clearing them is what includesRequirement() decides.
            const bool keepVanilla = i == WorldMeta::WorldId_Dark
                || i == WorldMeta::WorldId_Darker
                || includesRequirement(i);
            sRequirements[i] = keepVanilla
                ? WorldMeta::getVanillaMoonRequirement(i)
                : 0;
        }
    }

    static s32 modifiableCount() {
        s32 count = 0;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            count += includesRequirement(worldId) ? 1 : 0;
        return count;
    }

    static s32 modifiableWorldId(s32 index) {
        if (index < 0)
            return -1;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            if (includesRequirement(worldId) && index-- == 0)
                return worldId;
        }
        return -1;
    }

    static s32 moonTotal(s32 worldId) {
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return 0;

        // Full moon allocation changes the actual amount of local moon value a
        // kingdom can ever collect.  Requirement generation runs after that
        // allocation, so use its credited value as the hard local ceiling.  The
        // native table remains the ceiling when allocation is disabled (and as a
        // lifecycle fallback before an allocation summary exists).
        if (Randomizer::isMoonAllocationActive()) {
            MoonRando::KingdomAllocationSummary summary = {};
            if (MoonRando::getKingdomAllocationSummary(worldId, &summary))
                return summary.assignedValue;
        }
        return kMoonTotalsByKingdom[worldId];
    }

    // Per-kingdom soft cap on the base distribution, as a percentage of the
    // kingdom's total moon count. The caps only bound the base assignment: the
    // selected total is always honoured, spilling past the caps when it cannot
    // fit under them (e.g. Hard's 30% capacity is below a 256 requirement).
    static s32 basePercentCap(Randomizer::MoonDifficulty difficulty) {
        switch (difficulty) {
        case Randomizer::MoonDifficulty_Hard:
            return 30;
        case Randomizer::MoonDifficulty_Harder:
            return 40;
        case Randomizer::MoonDifficulty_Hardest:
            return 60;
        case Randomizer::MoonDifficulty_Insane:
            return 80;
        default:
            return 100;
        }
    }

    static s32 cappedMoonTotal(s32 worldId, s32 percentCap) {
        const s32 total = moonTotal(worldId);
        if (total <= 0 || percentCap >= 100)
            return total;
        s32 cap = (total * percentCap + 50) / 100;
        if (cap < 1)
            cap = 1;
        if (cap > total)
            cap = total;
        return cap;
    }

    static s32 clampRequirement(s32 worldId, s32 value) {
        const s32 total = moonTotal(worldId);
        if (total <= 0)
            return 0;
        if (value < 1)
            return 1;
        if (value > total)
            return total;
        return value;
    }

    static s32 clampRequirementZeroMin(s32 worldId, s32 value) {
        const s32 total = moonTotal(worldId);
        if (total <= 0)
            return 0;
        if (value < 0)
            return 0;
        if (value > total)
            return total;
        return value;
    }

    static s32 vanillaRequirement(s32 worldId) {
        if (!includesRequirement(worldId))
            return WorldMeta::getVanillaMoonRequirement(worldId);
        if (worldId == WorldMeta::WorldId_Cap || worldId == WorldMeta::WorldId_Cloud)
            return kVanillaAverageRequirement;
        if (worldId == WorldMeta::WorldId_Moon)
            return kMoonNormalRequirement;
        return WorldMeta::getVanillaMoonRequirement(worldId);
    }

    static s32 scaledBaseRequirementForTotal(s32 worldId, s32 total) {
        const s32 vanilla = vanillaRequirement(worldId);
        if (vanilla <= 0)
            return 0;

        const s64 scaled = (s64)vanilla * total;
        return clampRequirement(worldId,
            static_cast<s32>((scaled + kVanillaRequirementTotal / 2) / kVanillaRequirementTotal));
    }

    static s32 scaledBaseRequirement(s32 worldId) {
        return scaledBaseRequirementForTotal(worldId,
            static_cast<s32>(Randomizer::getMoonRequirement()));
    }

    static s32 routeIndex(s32 worldId) {
        const s32 routeCount = KingdomRando::getRouteWorldCount();
        for (s32 i = 0; i < routeCount; i++) {
            if (KingdomRando::getRouteWorldId(i) == worldId)
                return i;
        }
        return -1;
    }

    // Power-curve distribution. Fixed-point integer math (no libm): exponent 2,
    // eps = 0.1 in /1000 units.
    //   Easy (harder later):   available * (pos/(N-1) + eps)^2
    //   Hard (harder earlier): (((N-1-pos)/(N-1) + eps)^2) / available
    static s64 powerCurveWeight(s32 routePos, s32 routeCount, s32 available,
        Randomizer::MoonDifficulty difficulty) {
        const s32 denom = routeCount > 1 ? routeCount - 1 : 1;
        const s32 numerator = difficulty == Randomizer::MoonDifficulty_Easy
            ? routePos
            : routeCount - 1 - routePos;
        const s64 pos = ((s64)numerator * 1000) / denom + 100; // (frac + 0.1) * 1000
        const s64 term2 = pos * pos; // scaled by 1e6
        if (difficulty == Randomizer::MoonDifficulty_Easy)
            return (s64)available * term2;
        return (term2 * 1000) / (available > 0 ? available : 1);
    }

    // Distribute targetTotal across modifiable kingdoms by power-curve weight, then
    // hand the rounding remainder to the largest weights (round-robin) so the sum is
    // close to target before variance/balancing.
    static void buildPowerCurveBase(s32 targetTotal, Randomizer::MoonDifficulty difficulty) {
        const s32 count = modifiableCount();
        const s32 routeCount = KingdomRando::getRouteWorldCount();
        const s32 effectiveCount = routeCount > 1 ? routeCount : count;
        const s32 percentCap = basePercentCap(difficulty);

        if (count <= 0)
            return;

        s64 weights[WorldMeta::sWorldCount] = {};
        s32 order[WorldMeta::sWorldCount];
        s64 totalWeight = 0;
        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            const s32 routePos = routeIndex(worldId);
            weights[i] = powerCurveWeight(routePos >= 0 ? routePos : i, effectiveCount,
                moonTotal(worldId), difficulty);
            if (weights[i] < 0)
                weights[i] = 0;
            totalWeight += weights[i];
            order[i] = i;
        }
        if (totalWeight <= 0)
            totalWeight = 1;

        s32 assigned = 0;
        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            s32 value = (s32)((weights[i] * targetTotal) / totalWeight);
            const s32 cap = cappedMoonTotal(worldId, percentCap);
            if (value > cap)
                value = cap;
            sRequirements[worldId] = clampRequirement(worldId, value);
            assigned += sRequirements[worldId];
        }

        // order[] = modifiable indices sorted by weight desc (selection sort, count <= 12).
        for (s32 a = 0; a < count; a++) {
            s32 best = a;
            for (s32 b = a + 1; b < count; b++)
                if (weights[order[b]] > weights[order[best]])
                    best = b;
            const s32 tmp = order[a];
            order[a] = order[best];
            order[best] = tmp;
        }

        // Remainder that cannot fit under the percent caps is left for
        // balanceBaseToTotal, which spills past them.
        s32 remainder = targetTotal - assigned;
        s32 cursor = 0;
        s32 stalled = 0;
        while (remainder > 0 && stalled < count) {
            const s32 worldId = modifiableWorldId(order[cursor % count]);
            cursor++;
            if (sRequirements[worldId] >= cappedMoonTotal(worldId, percentCap)) {
                stalled++;
                continue;
            }
            stalled = 0;
            sRequirements[worldId]++;
            remainder--;
        }
    }

    static s32 randomSpread(Randomizer::MoonDifficulty difficulty) {
        switch (difficulty) {
        case Randomizer::MoonDifficulty_Easy:
            return 3;
        case Randomizer::MoonDifficulty_Hard:
        case Randomizer::MoonDifficulty_Medium:
        default:
            return 5;
        }
    }

    s32 getDefaultDeviation(Randomizer::MoonDifficulty difficulty) {
        return randomSpread(difficulty);
    }

    static s32 requirementTotal() {
        s32 total = 0;
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++)
            total += sRequirements[modifiableWorldId(i)];
        return total;
    }

    // Normalize the base before adding variance. Medium's scaled vanilla values can
    // miss the target after rounding (and special Cap/Cloud/Moon requirements add
    // extra bases), while Easy/Hard can miss it after min/max clamping. Kingdoms
    // still under their percent cap are preferred; the caps are only exceeded when
    // the target cannot otherwise be reached.
    static void balanceBaseToTotal(s32 targetTotal, s32 percentCap) {
        const s32 count = modifiableCount();
        if (count <= 0)
            return;
        s32 total = requirementTotal();
        s32 guard = 0;

        while (total < targetTotal && guard++ < 100000) {
            s32 best = -1;
            s32 bestSpare = 0;
            bool bestUnderCap = false;
            for (s32 i = 0; i < count; i++) {
                const s32 worldId = modifiableWorldId(i);
                const bool underCap = sRequirements[worldId] < cappedMoonTotal(worldId, percentCap);
                const s32 bound = underCap ? cappedMoonTotal(worldId, percentCap) : moonTotal(worldId);
                const s32 spare = bound - sRequirements[worldId];
                if (spare <= 0)
                    continue;
                if (best >= 0 && bestUnderCap && !underCap)
                    continue;
                if (best >= 0 && bestUnderCap == underCap && spare <= bestSpare)
                    continue;
                bestSpare = spare;
                bestUnderCap = underCap;
                best = worldId;
            }
            if (best < 0)
                break;
            sRequirements[best]++;
            total++;
        }

        while (total > targetTotal && guard++ < 100000) {
            s32 best = -1;
            s32 bestReducible = 0;
            for (s32 i = 0; i < count; i++) {
                const s32 worldId = modifiableWorldId(i);
                const s32 reducible = sRequirements[worldId] - 1;
                if (reducible > bestReducible) {
                    bestReducible = reducible;
                    best = worldId;
                }
            }
            if (best < 0)
                break;
            sRequirements[best]--;
            total--;
        }
    }

    // Zero-sum delta variance applied on top of an exact-total base. Both the initial
    // roll and the correction stay inside [-spread, +spread]. The old correction
    // could repeatedly decrement the same kingdom past -spread (Lake 8 -> 1 on
    // Medium), and the later debt pass could escape the range again.
    static void applyZeroSumVariance(Randomizer::Rng& rng, s32 spread) {
        if (spread <= 0)
            return;

        const s32 count = modifiableCount();
        // The correction loop takes a modulo by this.
        if (count <= 0)
            return;
        s32 delta[WorldMeta::sWorldCount] = {};
        s32 lower[WorldMeta::sWorldCount] = {};
        s32 upper[WorldMeta::sWorldCount] = {};
        s32 sum = 0;

        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            const s32 base = sBaseRequirements[worldId];
            lower[i] = 1 - base;
            if (lower[i] < -spread)
                lower[i] = -spread;
            upper[i] = moonTotal(worldId) - base;
            if (upper[i] > spread)
                upper[i] = spread;

            const u32 range = static_cast<u32>(upper[i] - lower[i] + 1);
            delta[i] = lower[i] + static_cast<s32>(rng.nextModulo(range));
            sum += delta[i];
        }

        // Zero is inside every [lower, upper] interval, so a bounded correction
        // always exists. Pick a random starting point for each unit, then scan to
        // avoid relying on an unbounded sequence of random retries.
        while (sum != 0) {
            const s32 start = static_cast<s32>(rng.nextModulo(static_cast<u32>(count)));
            bool adjusted = false;
            for (s32 offset = 0; offset < count; offset++) {
                const s32 i = (start + offset) % count;
                if (sum > 0 && delta[i] > lower[i]) {
                    delta[i]--;
                    sum--;
                    adjusted = true;
                    break;
                }
                if (sum < 0 && delta[i] < upper[i]) {
                    delta[i]++;
                    sum++;
                    adjusted = true;
                    break;
                }
            }
            if (!adjusted)
                break;
        }

        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            sRequirements[worldId] = sBaseRequirements[worldId] + delta[i];
        }
    }

    static s32 requirementCapacity() {
        s32 capacity = 0;
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++)
            capacity += moonTotal(modifiableWorldId(i));
        return capacity;
    }

    // Random distribution (Harder/Hardest/Insane/Max): each eligible kingdom gets
    // 0 to its (percent-capped) max, preserving the total. When the target exceeds
    // the capped capacity, every kingdom lands on its cap and the overflow is
    // spread past the caps round-robin so the total is still respected.
    static void buildRandomRequirements(Randomizer::Rng& rng, s32 targetTotal, s32 percentCap) {
        const s32 count = modifiableCount();
        // The spill loop takes a modulo by this.
        if (count <= 0)
            return;
        s32 order[WorldMeta::sWorldCount];
        s32 cappedCapacity = 0;
        s32 fullCapacity = 0;

        for (s32 i = 0; i < count; i++) {
            order[i] = i;
            const s32 worldId = modifiableWorldId(i);
            sRequirements[worldId] = 0;
            cappedCapacity += cappedMoonTotal(worldId, percentCap);
            fullCapacity += moonTotal(worldId);
        }

        if (targetTotal < 0)
            targetTotal = 0;
        if (targetTotal > fullCapacity)
            targetTotal = fullCapacity;

        for (s32 i = count - 1; i > 0; i--) {
            const s32 other = static_cast<s32>(rng.nextModulo(static_cast<u32>(i + 1)));
            const s32 tmp = order[i];
            order[i] = order[other];
            order[other] = tmp;
        }

        s32 overflow = targetTotal - cappedCapacity;
        if (overflow < 0)
            overflow = 0;

        s32 remaining = targetTotal - overflow;
        s32 remainingCapacity = cappedCapacity;
        for (s32 pos = 0; pos < count; pos++) {
            const s32 worldId = modifiableWorldId(order[pos]);
            const s32 capacity = cappedMoonTotal(worldId, percentCap);
            remainingCapacity -= capacity;

            s32 value = remaining;
            if (pos + 1 < count) {
                s32 minPick = remaining - remainingCapacity;
                if (minPick < 0)
                    minPick = 0;
                s32 maxPick = capacity < remaining ? capacity : remaining;
                if (maxPick < minPick)
                    maxPick = minPick;
                value = minPick + static_cast<s32>(rng.nextModulo(static_cast<u32>(maxPick - minPick + 1)));
            } else if (value > capacity) {
                value = capacity;
            }

            sRequirements[worldId] = clampRequirementZeroMin(worldId, value);
            remaining -= sRequirements[worldId];
        }

        remaining += overflow;
        s32 cursor = 0;
        s32 stalled = 0;
        while (remaining > 0 && stalled < count) {
            const s32 worldId = modifiableWorldId(order[cursor % count]);
            cursor++;
            if (sRequirements[worldId] >= moonTotal(worldId)) {
                stalled++;
                continue;
            }
            stalled = 0;
            sRequirements[worldId]++;
            remaining--;
        }
    }

    // Harder and up drop the base + variance model entirely: every eligible
    // kingdom is rolled straight out of the distribution instead.
    // The player's own numbers replace whatever the distribution produced. The
    // variance below still moves them, which is what keeps Req Difficulty
    // meaningful on a hand-authored spread.
    static void applyAuthoredBases() {
        if (!isRequirementBaseAuthored())
            return;
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            const s32 authored = getAuthoredRequirementBaseRaw(worldId);
            if (authored >= 0)
                sRequirements[worldId] = clampRequirement(worldId, authored);
        }
    }

    static bool usesRandomDistribution(Randomizer::MoonDifficulty difficulty) {
        return difficulty == Randomizer::MoonDifficulty_Harder
            || difficulty == Randomizer::MoonDifficulty_Hardest
            || difficulty == Randomizer::MoonDifficulty_Insane
            || difficulty == Randomizer::MoonDifficulty_Max;
    }

    static bool isModifiable(s32 worldId) {
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++) {
            if (modifiableWorldId(i) == worldId)
                return true;
        }
        return false;
    }

    static void applyKingdomDeviations() {
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const s32 deviation = getRequirementDeviation(worldId);
            if (deviation == 0)
                continue;
            sRequirements[worldId] = clampRequirementZeroMin(
                worldId, sRequirements[worldId] + deviation);
        }
    }

    static u32 seedForAttempt(u32 seed, u32 attempt) {
        return seed ^ (attempt * 0xC2B2AE35u);
    }

    void rebuild(u32 seed, u32 attempt) {
        resetToVanilla();

        {
            Randomizer::Rng gravityRng = Randomizer::makeRng(
                seedForAttempt(seed, attempt) ^ kMoonGravitySeedSalt,
                Randomizer::RngStream::Requirement);
            sMoonGravityExtra = kMoonGravityExtraMin
                + (s32)gravityRng.nextModulo(
                    (u32)(kMoonGravityExtraMax - kMoonGravityExtraMin + 1));
        }

        const Randomizer::MoonDifficulty difficulty = Randomizer::getMoonDifficulty();
        s32 targetTotal = (s32)Randomizer::getMoonRequirement();
        Randomizer::Rng rng = Randomizer::makeRng(seedForAttempt(seed, attempt), Randomizer::RngStream::Requirement);

        // Every kingdom can be taken off the progress route now (Kingdom Moons
        // page), so "no kingdom gates anything" is a legal configuration.
        // resetToVanilla() has already put every requirement where it belongs.
        if (modifiableCount() <= 0) {
            for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
                sBaseRequirements[i] = sRequirements[i];
            sInitialized = true;
            return;
        }

        if (usesRandomDistribution(difficulty)) {
            buildRandomRequirements(rng, targetTotal, basePercentCap(difficulty));
            applyAuthoredBases();
            const s32 count = modifiableCount();
            for (s32 i = 0; i < count; i++) {
                const s32 worldId = modifiableWorldId(i);
                sBaseRequirements[worldId] = sRequirements[worldId];
            }
            applyKingdomDeviations();
            sInitialized = true;
            return;
        }

        // Base distribution (D2): Medium uses vanilla values scaled by level; Easy/Hard
        // use the power curve (Hard capped per kingdom). The base is normalized to the
        // exact target below.
        const s32 count = modifiableCount();
        if (targetTotal < count)
            targetTotal = count;
        const s32 capacity = requirementCapacity();
        if (targetTotal > capacity)
            targetTotal = capacity;
        if (difficulty == Randomizer::MoonDifficulty_Medium) {
            for (s32 i = 0; i < count; i++) {
                const s32 worldId = modifiableWorldId(i);
                sRequirements[worldId] = clampRequirement(worldId, scaledBaseRequirement(worldId));
            }
        } else {
            buildPowerCurveBase(targetTotal, difficulty);
        }

        // An authored table already sums to the total the menu showed, so
        // balancing to a separate target would just undo the editing.
        if (isRequirementBaseAuthored())
            applyAuthoredBases();
        else
            balanceBaseToTotal(targetTotal, basePercentCap(difficulty));

        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            sBaseRequirements[worldId] = sRequirements[worldId];
        }

        applyZeroSumVariance(rng, randomSpread(difficulty)); // D3 + D4
        applyKingdomDeviations();
        sInitialized = true;
    }

    void clear() {
        resetToVanilla();
        sMoonGravityExtra = kMoonGravityExtraMin;
        sInitialized = false;
    }

    bool isInitialized() { return sInitialized; }

    s32 getMoonGravityExtra() { return sMoonGravityExtra; }

    bool isRequirementKingdom(s32 worldId) { return includesRequirement(worldId); }

    static void ensureAuthoredDeviation() {
        if (sAuthoredDeviationReady)
            return;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            sAuthoredDeviation[i] = cDeviationUnset;
        sAuthoredDeviationReady = true;
    }

    s32 getRequirementDeviation(s32 worldId) {
        ensureAuthoredDeviation();
        if (worldId >= 0 && worldId < WorldMeta::sWorldCount
            && sAuthoredDeviation[worldId] != cDeviationUnset)
            return sAuthoredDeviation[worldId];
        return Randomizer::getMoonRequirementDeviation();
    }

    void setRequirementDeviation(s32 worldId, s32 value) {
        ensureAuthoredDeviation();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        if (value < -20)
            value = -20;
        if (value > 20)
            value = 20;
        sAuthoredDeviation[worldId] = static_cast<s16>(value);
    }

    bool isRequirementDeviationAuthored() {
        ensureAuthoredDeviation();
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            if (sAuthoredDeviation[i] != cDeviationUnset)
                return true;
        return false;
    }

    void clearRequirementDeviations() {
        sAuthoredDeviationReady = false;
        ensureAuthoredDeviation();
    }

    s32 getAuthoredRequirementDeviationRaw(s32 worldId) {
        ensureAuthoredDeviation();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return cDeviationUnset;
        return sAuthoredDeviation[worldId];
    }

    void setAuthoredRequirementDeviationRaw(s32 worldId, s32 value) {
        ensureAuthoredDeviation();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        sAuthoredDeviation[worldId] = static_cast<s16>(value);
    }

    s32 getUnsetRequirementDeviation() { return cDeviationUnset; }

    static void ensureAuthoredBase() {
        if (sAuthoredBaseReady)
            return;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            sAuthoredBase[i] = -1;
        sAuthoredBaseReady = true;
    }

    // The vanilla proportional split of `total` across the kingdoms that gate,
    // rounded so the parts sum to it exactly. This is what the advanced page
    // fills itself in with, and what typing a new total redistributes to.
    void previewRequirementBases(s32 total, s32* out) {
        if (!out)
            return;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            out[i] = 0;

        const s32 count = modifiableCount();
        if (count <= 0)
            return;
        if (total < count)
            total = count;
        const s32 capacity = requirementCapacity();
        if (total > capacity)
            total = capacity;

        s32 assigned = 0;
        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            s32 value = scaledBaseRequirementForTotal(worldId, total);
            if (value < 1)
                value = 1;
            out[worldId] = value;
            assigned += value;
        }

        // Rounding leaves the parts a little off the total. Walk them until it
        // closes, skipping any kingdom already at its own ceiling or floor.
        s32 cursor = 0;
        s32 stalled = 0;
        while (assigned != total && stalled < count) {
            const s32 worldId = modifiableWorldId(cursor % count);
            cursor++;
            if (assigned < total) {
                if (out[worldId] >= moonTotal(worldId)) {
                    stalled++;
                    continue;
                }
                out[worldId]++;
                assigned++;
            } else {
                if (out[worldId] <= 1) {
                    stalled++;
                    continue;
                }
                out[worldId]--;
                assigned--;
            }
            stalled = 0;
        }
    }

    bool isRequirementBaseAuthored() {
        ensureAuthoredBase();
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            if (sAuthoredBase[i] >= 0)
                return true;
        return false;
    }

    // What the advanced page shows for a kingdom: the typed value where there
    // is one, otherwise the split it would get from the current total.
    s32 getRequirementBase(s32 worldId) {
        ensureAuthoredBase();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return 0;
        // Zero is how the menu spells "this kingdom does not gate", and it has
        // to survive a kingdom being switched off with an old authored value
        // still sitting in the table.
        if (!includesRequirement(worldId))
            return 0;
        if (sAuthoredBase[worldId] >= 0)
            return sAuthoredBase[worldId];
        s32 preview[WorldMeta::sWorldCount];
        previewRequirementBases(
            static_cast<s32>(Randomizer::getMoonRequirement()), preview);
        return preview[worldId];
    }

    s32 getRequirementBaseTotal() {
        s32 total = 0;
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++)
            total += getRequirementBase(modifiableWorldId(i));
        return total;
    }

    // Pin every kingdom at the number the page is currently showing. Until this
    // happens the table is a view of the distribution, and the distribution
    // always re-spreads the whole total over whatever kingdoms remain - so
    // dropping one would leave the total unchanged and quietly enlarge the
    // others. Freezing first is what makes the total move by the share the
    // player actually removed.
    void authorRequirementBases() {
        ensureAuthoredBase();
        if (isRequirementBaseAuthored())
            return;
        s32 preview[WorldMeta::sWorldCount];
        previewRequirementBases(
            static_cast<s32>(Randomizer::getMoonRequirement()), preview);
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++) {
            const s32 kingdom = modifiableWorldId(i);
            sAuthoredBase[kingdom] = static_cast<s16>(preview[kingdom]);
        }
    }

    void setRequirementBase(s32 worldId, s32 value) {
        ensureAuthoredBase();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        // Typing one number authors the whole table: the others keep the values
        // the page was already showing, so the total the player reads is the
        // total they get.
        authorRequirementBases();
        if (value < 0)
            value = 0;
        sAuthoredBase[worldId] = static_cast<s16>(clampRequirement(worldId, value));
    }

    void redistributeRequirementBases(s32 total) {
        ensureAuthoredBase();
        s32 preview[WorldMeta::sWorldCount];
        previewRequirementBases(total, preview);
        const s32 count = modifiableCount();
        for (s32 i = 0; i < count; i++) {
            const s32 worldId = modifiableWorldId(i);
            sAuthoredBase[worldId] = static_cast<s16>(preview[worldId]);
        }
    }

    void clearRequirementBases() {
        sAuthoredBaseReady = false;
        ensureAuthoredBase();
    }

    s32 getAuthoredRequirementBaseRaw(s32 worldId) {
        ensureAuthoredBase();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return -1;
        return sAuthoredBase[worldId];
    }

    void setAuthoredRequirementBaseRaw(s32 worldId, s32 value) {
        ensureAuthoredBase();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;
        sAuthoredBase[worldId] = static_cast<s16>(value < 0 ? -1 : value);
    }

    s32 getRequirementKingdomCount() { return modifiableCount(); }

    s32 getRequirementKingdomWorldId(s32 index) {
        return modifiableWorldId(index);
    }

    s32 getRequirement(s32 worldId) {
        if (!sInitialized)
            resetToVanilla();
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            worldId = 0;
        return sRequirements[worldId];
    }

    s32 getDefaultMediumMaximum(s32 worldId) {
        for (s32 i = 0; i < kRegularModifiableCount; i++) {
            if (kRegularModifiable[i] != worldId)
                continue;
            // Medium's fixed variance is five. This helper intentionally uses
            // the vanilla route requirement rather than generated requirements.
            return WorldMeta::getVanillaMoonRequirement(worldId) + 5;
        }
        return 0;
    }

    bool getTheoreticalRange(s32 worldId, s32* outMin, s32* outMax) {
        if (!sInitialized || worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return false;
        if (!isModifiable(worldId))
            return false;

        const s32 total = moonTotal(worldId);
        if (total <= 0)
            return false;

        const Randomizer::MoonDifficulty difficulty = Randomizer::getMoonDifficulty();
        s32 low = 0;
        s32 high = 0;
        if (usesRandomDistribution(difficulty)) {
            // buildRandomRequirements hands each kingdom anything from 0 up to its
            // percent cap (only spilling past the cap when the target total cannot
            // fit under it), so the cap is the requirement to beat here.
            low = 0;
            high = cappedMoonTotal(worldId, basePercentCap(difficulty));
        } else {
            // applyZeroSumVariance moves the base by at most +/- the spread, and
            // clamps the result to [1, total] on the way (see its lower/upper).
            const s32 base = sBaseRequirements[worldId];
            const s32 spread = randomSpread(difficulty);
            low = base - spread;
            if (low < 1)
                low = 1;
            high = base + spread;
            if (high > total)
                high = total;
        }

        const s32 deviation = Randomizer::getMoonRequirementDeviation();
        low = clampRequirementZeroMin(worldId, low + deviation);
        high = clampRequirementZeroMin(worldId, high + deviation);

        if (high < low)
            high = low;
        if (outMin)
            *outMin = low;
        if (outMax)
            *outMax = high;
        return true;
    }

} // namespace RequirementRando
