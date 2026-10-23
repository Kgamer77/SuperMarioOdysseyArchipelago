#pragma once

#include <basis/seadTypes.h>
#include <math/seadVector.h>

#include "Randomizer/FeatureFlags.h"

#if SMOR_FEATURE_CHAOS_ANY

// Per-entity spawn tuning for chaos swaps, and the experimental target list
// that feeds the crash hunt.
//
// A swap reuses the source placement's transform verbatim, which is usually
// right and occasionally very wrong: a Komboo authored to stand on a seabed
// floats when it lands on a rooftop, and an actor whose model origin sits at
// its feet buries itself in a placement authored around a centre. Rather than
// scatter special cases through EnemySwapHooks, every such adjustment lives in
// the one table below.
//
// Everything here is data. Adding a case should mean adding a row, not editing
// the hook, and the row format has room for the adjustments we already know we
// will want -- translate, rotate, and a deterministic jitter on either.
//
// DETERMINISM. Jitter is derived from (seed, placement index), never from a
// live RNG. Two runs of the same seed must place the same actor in the same
// spot, or a crash found by rerolling could not be reproduced by returning to
// that roll -- which is the entire point of the reroll counter.
namespace SpawnTuning {

    // How a row is matched against the entity being placed. Entity rows win
    // over category rows, so a category can carry the general case and a
    // single awkward actor can override it without disturbing the rest.
    enum class MatchKind : u8
    {
        Entity = 0,
        Category = 1,
    };

    // A plain triple, not sead::Vector3f. Vector3's default constructor is
    // user-provided and not constexpr, which makes it a non-literal type and
    // the table below impossible to declare constexpr. The runtime API still
    // speaks sead::Vector3f; only the stored data uses this.
    struct Offset {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;
    };

    // A deterministic range. `magnitude` is the half-width applied per axis:
    // an axis with magnitude 100 lands somewhere in [-100, +100]. Zero
    // disables that axis, which is the default and means "use the placement's
    // own value untouched".
    struct Jitter {
        f32 x = 0.0f;
        f32 y = 0.0f;
        f32 z = 0.0f;

        constexpr bool isActive() const {
            return x != 0.0f || y != 0.0f || z != 0.0f;
        }
    };

    // Flags for adjustments that are not simply additive. None are implemented
    // yet -- they are named here so the table format does not have to change
    // when they are, and so a row can already record the intent.
    enum SpawnFlag : u8
    {
        SpawnFlag_None = 0,
        // Discard the source's authored rotation and face a deterministic
        // direction instead. For actors whose authored rotation only makes
        // sense against their original geometry.
        SpawnFlag_ReplaceRotation = 1 << 0,
        // Keep the actor upright, applying only the yaw component. Useful for
        // ground enemies inheriting a placement on a slope or a wall.
        SpawnFlag_YawOnly = 1 << 1,
        // The offset is along the source's local axes rather than world axes.
        SpawnFlag_LocalSpace = 1 << 2,
    };

    struct Adjust {
        const char* match;
        MatchKind kind;
        // Added to the placement's translate, in world units unless
        // SpawnFlag_LocalSpace is set. Y is the one that usually matters:
        // positive lifts the actor out of the floor.
        Offset translate;
        // Added to the placement's rotate, in degrees.
        Offset rotate;
        Jitter translateJitter;
        Jitter rotateJitter;
        u8 flags;
        // Why this row exists. Kept in the table because a bare offset with no
        // explanation is unmaintainable: the next person cannot tell a
        // measured correction from a guess someone never removed.
        const char* reason;

        // Whether applying this row would change anything. A row can exist as
        // documentation with every field zero -- the two below do -- and the
        // call site is expected to skip those rather than repose the actor
        // with the values it already has.
        constexpr bool hasEffect() const {
            return flags != SpawnFlag_None
                || translate.x != 0.0f || translate.y != 0.0f
                || translate.z != 0.0f
                || rotate.x != 0.0f || rotate.y != 0.0f || rotate.z != 0.0f
                || translateJitter.isActive() || rotateJitter.isActive();
        }
    };

    // ---------------------------------------------------------------------
    // Tuning table. Empty of real corrections today: no swap has yet been
    // observed landing wrongly, and inventing offsets before seeing the
    // failure would be guesswork baked into data. The two rows below are
    // inert (all-zero) and exist to document the shape.
    // ---------------------------------------------------------------------
    inline constexpr Adjust sAdjustments[] = {
        { "prop", MatchKind::Category, {}, {}, {}, {}, SpawnFlag_None,
            "placeholder: props inherit scenery transforms authored flush to "
            "geometry, so this is where a lift would go if one proves needed" },
        { "Komboo", MatchKind::Entity, {}, {}, {}, {}, SpawnFlag_None,
            "placeholder: seabed-authored actor, first candidate for a lift if "
            "it turns up floating or buried away from water" },
    };
    inline constexpr u32 sAdjustmentCount = static_cast<u32>(sizeof(sAdjustments) / sizeof(sAdjustments[0]));

    // ---------------------------------------------------------------------
    // Experimental targets: entities that are not in the swap pool and could
    // be, listed so the crash hunt has somewhere to go after the current pool
    // is exhausted.
    //
    // Being here is a proposal, not a decision. The pool itself is built from
    // `any_pool` in config/enemy_swap_entities.csv; promoting one of these
    // means flipping that column and regenerating, which is deliberately a
    // separate act from writing it down here.
    // ---------------------------------------------------------------------
    enum class Risk : u8
    {
        // An ordinary standalone actor. Nothing about its authored placement
        // suggests it needs anything the source will not provide.
        Standalone = 0,
        // A capture. Constructs like an enemy, but a capture that misbehaves
        // takes the player's control state with it, so it fails louder.
        Capture = 1,
        // Known or strongly suspected to need something a foreign placement
        // cannot give it. Listed for completeness; do not promote casually.
        Bound = 2,
    };

    struct ExperimentalTarget {
        const char* entity;
        const char* category;
        Risk risk;
        const char* note;
    };

    // Everything with swap_out set that is currently outside the pool and is
    // not already disproven. The nine entities the audit measured at zero
    // successes are deliberately absent -- they were removed from the pool on
    // evidence and should not be proposed back into it.
    inline constexpr ExperimentalTarget sExperimentalTargets[] = {
        { "Burrbo", "enemy", Risk::Standalone,
            "Snow Kingdom burrower; never yet a target" },
        { "Trapeetle", "enemy", Risk::Standalone,
            "Wooded Kingdom; charges and explodes, so a bad landing is visible" },
        { "Komboo", "enemy", Risk::Standalone,
            "seabed kelp; likely needs a translate row once placed on land" },
        { "Mummy", "enemy", Risk::Standalone,
            "the non-generator form of MummyGenerator, which never constructs -- "
            "worth knowing whether the defect is the generator or the actor" },
        { "Jango", "enemy", Risk::Standalone,
            "already appears as a swap source; untested as a target" },
        { "Gunetter", "enemy", Risk::Standalone,
            "the non-Move form of GunetterMove, which is clean in the audit" },
        { "Joku", "enemy", Risk::Standalone, "cloud platform enemy" },
        { "Chorobon2D3D", "enemy", Risk::Standalone,
            "flagged 2D/3D capable; may still need a plane section in 2D" },
        { "Chair", "prop", Risk::Bound,
            "generic identity has no standalone Chair archive; model-specific "
            "chair and bench identities are already in the pool" },
        { "CrystalBreakable", "prop", Risk::Standalone,
            "uses placement-selected crystal models; needs an explicit target "
            "model mapping before promotion" },
        { "DigPoint", "prop", Risk::Standalone,
            "standalone archive exists; keep source-only until interaction and "
            "visibility are checked on a foreign placement" },
        { "ReactionObject", "prop", Risk::Bound,
            "generic placements select many unrelated models; promote only "
            "named standalone variants" },
        { "Pen", "prop", Risk::Bound,
            "paint controller has no standalone Pen archive" },
        { "Souvenir", "prop", Risk::Bound,
            "generic identity selects one of many souvenir models and has no "
            "standalone Souvenir archive" },
        { "TrampleBush", "prop", Risk::Bound,
            "placement selects CapWorldBush; no standalone TrampleBush archive" },
        { "Senobi", "capture", Risk::Capture,
            "the non-generator form of SenobiGeneratePoint" },
        { "Yoshi", "capture", Risk::Capture,
            "eats and flutter-jumps; heavy, and its own moon logic may object" },
        { "RockForest", "capture", Risk::Capture, "Wooded Kingdom rock" },
        { "CarryMeat", "capture", Risk::Capture,
            "Luncheon meat; a carried object, so it may expect a carrier" },
        { "Guidepost", "capture", Risk::Capture, "signpost capture" },
        { "AnagramAlphabetCharacter", "capture", Risk::Capture,
            "letter block; belongs to a puzzle that will not be present" },
    };
    inline constexpr u32 sExperimentalTargetCount = static_cast<u32>(
        sizeof(sExperimentalTargets) / sizeof(sExperimentalTargets[0]));

    // ---------------------------------------------------------------------
    // Lookup and application.
    // ---------------------------------------------------------------------

    // Row governing this entity, or nullptr when the placement is used as
    // authored. `category` may be null.
    const Adjust* findAdjust(const char* entityName, const char* category);

    // Applies a row in place. `trans` and `rotate` start as the source
    // placement's values. Safe to call with a null adjust, in which case
    // nothing changes -- callers do not need to branch.
    //
    // `seed` and `placementIndex` seed the jitter and nothing else; the same
    // pair always produces the same result.
    void applyAdjust(
        const Adjust* adjust, u32 seed, s32 placementIndex,
        sead::Vector3f* trans, sead::Vector3f* rotate);

    // Convenience: look up and apply in one call.
    void applyForEntity(
        const char* entityName, const char* category, u32 seed,
        s32 placementIndex, sead::Vector3f* trans, sead::Vector3f* rotate);

    // ---------------------------------------------------------------------
    // Not yet called from anywhere.
    //
    // The intended call site is EnemySwapHooks::createPlacementActorHook,
    // immediately after a replacement actor is created, along these lines:
    //
    //     const Adjust* tune = SpawnTuning::findAdjust(
    //         replacement->name, replacement->category);
    //     if (tune && tune->hasEffect()) {
    //         sead::Vector3f trans, rotate;
    //         al::getTrans(&trans, *placementInfo);
    //         al::getRotate(&rotate, *placementInfo);
    //         SpawnTuning::applyAdjust(
    //             tune, Randomizer::getSeed(), placementIndex,
    //             &trans, &rotate);
    //         al::setTrans(actor, trans);
    //         al::setRotate(actor, rotate);
    //     }
    //
    // It is deliberately not wired yet: every row is inert, so wiring it now
    // would add a reposition to the actor-creation path while that path is
    // still being debugged, and buy nothing until a row has real values in it.
    // ---------------------------------------------------------------------

}

#else

namespace SpawnTuning {
    struct Adjust;
    inline const Adjust* findAdjust(const char*, const char*) { return nullptr; }
}

#endif
