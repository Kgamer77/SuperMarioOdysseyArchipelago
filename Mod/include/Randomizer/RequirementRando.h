#pragma once

#include <basis/seadTypes.h>

namespace Randomizer {
    // Opaque declaration rather than the whole header: this one is included by
    // the seed builder and the menu alike.
    enum MoonDifficulty : u8;
}

namespace RequirementRando {

    void rebuild(u32 seed, u32 attempt = 0);
    void clear();
    s32 getRequirementKingdomCount();
    s32 getRequirementKingdomWorldId(s32 index);
    // Does this kingdom gate the route? True exactly when its moons are set
    // to Progress and it sits somewhere a requirement can bite. The globe
    // deposit, the tracker readout and the requirement distribution all ask
    // this, so none of them can disagree about which kingdoms have one.
    bool isRequirementKingdom(s32 worldId);
    s32 getRequirement(s32 worldId);

    // Per-kingdom base requirements, as edited on the Moon Requirement advanced
    // page. Untouched, the distribution decides everything and getRequirementBase
    // reports what it would decide; typing any number authors the whole table
    // (see setRequirementBase) and the distribution then only adds variance.
    s32 getRequirementBase(s32 worldId);
    s32 getRequirementBaseTotal();
    void setRequirementBase(s32 worldId, s32 value);
    // Pin every kingdom at what getRequirementBase() reports for it right now.
    // Call before adding or removing a kingdom so the others keep their numbers
    // and the total moves by that kingdom's share.
    void authorRequirementBases();
    bool isRequirementBaseAuthored();

    // The per-seed spread, per kingdom. Untouched, every kingdom uses the one
    // Req Deviation number; a kingdom the player edited uses its own instead.
    s32 getRequirementDeviation(s32 worldId);
    void setRequirementDeviation(s32 worldId, s32 value);
    bool isRequirementDeviationAuthored();
    void clearRequirementDeviations();
    s32 getAuthoredRequirementDeviationRaw(s32 worldId);
    void setAuthoredRequirementDeviationRaw(s32 worldId, s32 value);
    // The marker the two raw accessors use for "this kingdom follows the one
    // global number". Zero is a real deviation, so it cannot serve.
    s32 getUnsetRequirementDeviation();
    // Refill every kingdom from the vanilla proportional split of `total`.
    void redistributeRequirementBases(s32 total);
    void clearRequirementBases();
    // The stored value, -1 where the kingdom is unauthored. For the save file,
    // which has to round-trip "not set" as distinct from zero.
    s32 getAuthoredRequirementBaseRaw(s32 worldId);
    void setAuthoredRequirementBaseRaw(s32 worldId, s32 value);
    // The +/- spread a named difficulty describes. The Req Deviation row shows
    // this until the player types a different number, which is what makes the
    // difficulty select read Custom.
    s32 getDefaultDeviation(Randomizer::MoonDifficulty difficulty);
    s32 getDefaultMediumMaximum(s32 worldId);
    bool isInitialized();

    // Extra Moon Kingdom moons the low-gravity ability asks for on top of that
    // kingdom's own requirement, under Randomizer::MoonGravityMode's
    // ExtraMoonRequirements. Always 5-10, always rolled (on its own Rng, so it
    // cannot perturb the requirement distribution of an existing seed) whether
    // or not the mode is selected - the mode only decides whether anyone asks.
    // Callers outside this module should use Randomizer::getMoonGravityExtraRequirement,
    // which returns 0 in every other mode.
    s32 getMoonGravityExtra();

    // Widest requirement this kingdom could have ended up with under the current
    // settings: its settings-derived base value -/+ the variance the generator is
    // allowed to apply (or the distribution's own bounds on the difficulties that
    // roll each kingdom outright, which have no base to offset from). Deliberately
    // never looks at getRequirement(), so a caller can show how a moon count
    // relates to the possible range without revealing the seed's actual value.
    // Returns false (leaving the outputs untouched) for kingdoms that carry no
    // randomized requirement, or before a seed has been built.
    bool getTheoreticalRange(s32 worldId, s32* outMin, s32* outMax);

} // namespace RequirementRando
