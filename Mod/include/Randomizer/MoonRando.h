#pragma once

#include <basis/seadTypes.h>

class GameDataHolderAccessor;
class GameDataFile;

namespace ShineDB {
    struct ShineEntry;
}

namespace MoonRando {

    static constexpr s32 kMaxPathTravelSteps = 6;
    static constexpr s32 kPathTravelStepTextSize = 144;
    // Upper bound for the fixed-size generation-safety-net block in rando.bin.
    // The current logic table is smaller, but spare records avoid another format
    // change if a future pool can leave more items without physical locations.
    static constexpr s32 kMaxAutoGrants = 1024;
    // ShineListLayout constructs fixed storage for exactly 110 rows.  Every
    // generated kingdom allocation must remain within this native capacity.
    static constexpr s32 kMaxShineListRows = 110;

    enum class FailureCode : u8
    {
        None = 0,
        DataError,
        CapacityFailure,
        StrandedUnlocks,
        UnreachableObjective,
        WorldPeaceFailure,
        SearchBudgetExhausted,
        ValidationFailure,
    };

    enum class AllocationConstraintKind : u8
    {
        None,
        CaptureSlots,
        PaintingEntries,
        AbilitySlotsCombined,
    };

    struct KingdomAllocationSummary {
        s32 assignedEntries;
        s32 assignedValue;
        s32 baseRequirement;
        s32 excessValue;
        s32 minimumValue;
        s32 specialCapacity;
        s32 specialMinimum;
        s32 targetValue;
        AllocationConstraintKind specialKind;
        bool targetTight;
        bool concentration;
        // Held at its vanilla credited value instead of drawing from the
        // shuffled excess. Mushroom, Cloud, and Dark plus Darker as a pair.
        bool pinned;
    };

    struct Placement {
        const ShineDB::ShineEntry* location;
        const ShineDB::ShineEntry* item;
    };

    struct SerializedPlacement {
        s32 locShineIdx;
        s32 itemShineIdx;
    };

    // One deterministic dependency/layout attempt made while building a seed.
    // These records are diagnostic-only and are emitted into the spoiler log for
    // the generation that produced it.
    struct GenerationAttemptInfo {
        u32 attempt;
        u32 dependencyMillis;
        u32 placementMillis;
        s32 searchNodes;
        s32 searchBundles;
        FailureCode result;
        bool succeeded;
        char message[160];
    };

    struct TravelStep {
        char text[kPathTravelStepTextSize];
    };

    struct PathStep {
        const ShineDB::ShineEntry* location;
        const ShineDB::ShineEntry* item;
        const char* reason;
        s32 sphere;
        s32 unlockedTokenCount;
        const char* unlockedTokens[8];
        s32 travelStepCount;
        TravelStep travelSteps[kMaxPathTravelSteps];
        s16 locationStageIdx;
        s16 locationKingdomIdx;
        s16 hostKingdomIdx;
        s16 scenarioAdvanceKingdom;
        s16 scenarioAdvanceTo;
        // True when this step's scenario advance was caused by hitting the Moon Rock
        // during travel (not by collecting the step's own moon). Lets the spoiler
        // attribute it to the Moon Rock instead of mislabelling it "(from <moon>)".
        bool scenarioAdvanceIsMoonRock;
        bool isStoryStep;
        bool grantedWorldPeace;
    };

    struct PaintingLinkInfo {
        const char* sourceNode;
        const char* sourceKingdom;
        const char* destinationNode;
        const char* destinationKingdom;
        bool bidirectional;
    };

    // Spheres for the suggested playthrough: each sphere is a wave of accessibility.
    // Everything in a sphere is reachable with only what earlier spheres provided;
    // collecting a sphere's captures/abilities/moons opens the next one.
    static constexpr s32 kMaxSpheres = 64;
    static constexpr s32 kMaxSphereKingdoms = 24;
    static constexpr s32 kMaxSphereAreas = 160;

    enum class KingdomAccessKind : u8
    {
        Start = 0, // the starting kingdom
        Odyssey, // a normal Odyssey flight destination
        Entrance, // reached through a (shuffled) loading zone from another stage
        Painting, // reached through a painting from another kingdom
        Inferred, // access proven by the graph without recorded provenance
    };

    enum class SphereAreaKind : u8
    {
        Kingdom = 0, // a newly accessible kingdom (overworld)
        SubArea, // a newly accessible sub-area stage
        ScenarioBump, // an accessible kingdom's scenario increased
    };

    struct SphereArea {
        SphereAreaKind kind;
        KingdomAccessKind access; // how the kingdom/sub-area was reached
        s16 kingdom;
        s16 scenario; // current scenario (Kingdom / ScenarioBump)
        s16 stageIdx; // the sub-area stage (SubArea); -1 otherwise
        // For Entrance/Painting access, the source stage the player came from
        // (its kingdom names the "through X" label). -1 otherwise.
        s16 viaStageIdx;
        // For Entrance access, the overworld loading-zone link used to arrive (an
        // index into EntranceDB::sLinks); its vanilla sub-area names the specific
        // entrance. -1 otherwise.
        s16 viaEntranceIdx;
        // First shuffled connection followed from viaEntranceIdx. Detailed travel
        // logs can use this to render the routed destination; sphere summaries may
        // omit the chain so each layer stays readable.
        s16 entranceChainFirstLink;
        u8 entranceChainFirstKind;
    };

    struct SphereInfo {
        s32 index;
        // New areas opened this sphere (kingdoms, sub-areas, scenario advances).
        s32 areaCount;
        SphereArea areas[kMaxSphereAreas];
        // Kingdoms opened by a route deposit this sphere ("Kingdom Unlocked: X").
        s32 routeUnlockCount;
        s16 routeUnlocked[kMaxSphereKingdoms];
        // Range of optimized-path steps (moons/captures/abilities) collected in this
        // sphere: [firstStepIndex, firstStepIndex + stepCount).
        s32 firstStepIndex;
        s32 stepCount;
    };

    // Live "what's worth doing in this kingdom right now" signal for the tracker's
    // Hints mode. Computed from the current save (owned abilities/captures/moons),
    // not from the generator's one-time fill order.
    enum class KingdomHintStatus : u8
    {
        Unavailable = 0, // not computable right now (moon rando inactive, etc.)
        Red, // unreachable, or reachable with nothing currently collectible
        Orange, // something collectible now, but not progression-relevant
        Green, // something progression-relevant is collectible now: it unlocks an
               // ability/capture, it's credited to Mushroom/Dark Side/Darker Side
               // (always worth having - see getKingdomHintCount), or it still
               // counts toward its credited kingdom's (unmet) moon/story requirement
    };

    bool buildRemapTable(u32 seed);
    void clear();
    // Compute the per-seed moon->kingdom redistribution (legacy Triple Moons or
    // the full Moon Allocation engine)
    // in isolation, without running the moon placement. Role assignments (capture,
    // ability, rock keys, Toadette) read the assigned kingdom, so this must run
    // BEFORE them. buildRemapTable does this internally; the save-restore path
    // (reinitDeterministic) must call it explicitly so restored captures/abilities
    // ride the reshuffled pool instead of the home kingdoms. Identity is used when
    // neither full allocation nor legacy Triple redistribution is active.
    bool prepareKingdomRedistribution(u32 seed);
    bool restorePlacements(const s32* itemForLocation, s32 itemCount, const SerializedPlacement* placements, s32 placementCount, u32 seed, u32 entranceAttempt, bool logicSucceeded, const s16* cloudPaintingLinkOrder = nullptr, s32 cloudPaintingLinkOrderCount = 0, const s32* autoGrantItems = nullptr, s32 autoGrantCount = 0);
    void rebuildPlaythrough();

    // Whether a player entering the link's sub-area at the given side can reach
    // the opposite side, with every capture/ability token granted (those are
    // gated by the placement logic later) but impossible edges, scenario
    // windows and the selected logic difficulty honored. Used by the entrance
    // shuffle so chains never require crossing an untraversable room.
    bool canEntranceSubBeTraversed(s32 linkIndex, bool enteredFromFarSide);
    // Whether the link sub-area holds any moon location at all. Lets the
    // entrance fill tell a pocket worth exploring from a bare dead end, which
    // are two very different things to hand a player under low Seed Openness.
    bool linkSubAreaHasLocations(s32 linkIndex);

    // One door the fill wired, in the order it chose them. Under Entrance Rando
    // the door graph is built during placement, so this is the record of which
    // doors were deliberate and what each one opened; whatever is not listed
    // was paired blind by the leftover pass at finalize.
    struct EntranceWireStep {
        s16 sourceLink;
        s16 targetLink;
        u8 sourceSlot; // EntranceRando::DoorSlot
        u8 targetSlot;
        s16 sphere; // construction sphere, not the spoiler playthrough numbering
        u8 doorClass; // EntranceRando::DoorClass
        u8 intent; // EntranceRando::WireIntent
    };
    s32 getEntranceWireStepCount();
    bool getEntranceWireStep(s32 index, EntranceWireStep* out);
    // Randomized Starting Position: whether a player dropped at LogicData node
    // `node` with only his starting equipment can prove his way back to
    // `requiredStage` (the starting kingdom's overworld). Driven by
    // StartPositionRando::rebuild to validate each shuffled candidate; running
    // the ordinary reachability fixed point is what makes it honor the entrance
    // shuffle, one-way pockets, dead-end subs and scenario gates at once.
    bool canStartAtNode(s32 node, s32 requiredStage);
    // Randomized Starting Position: fills outStages (LogicData::sStageCount bools)
    // with the stages reachable FROM `kingdom`'s overworld using only the starting
    // equipment - the sub-area chains that actually hang off that kingdom's doors
    // under this seed's shuffle. Bounds the Max start pool to "linked from that
    // overworld", which is not the same as "a path back to it exists": with
    // entrances shuffled, a sub-area physically inside the starting kingdom can
    // belong to a chain rooted in a completely different one.
    void computeStagesLinkedFromKingdom(s32 kingdom, bool* outStages);
    // True when linkIndex's home side sits on a kingdom's own main overworld
    // stage (mirrors the unconditional kingdom unlocks the entrance
    // reachability proof grants). This alone does NOT mean the door itself is
    // freely reachable - see isEntranceHomeSideUnconditionallyReachable for
    // that. Used by the entrance shuffle to classify a link's home side as
    // kingdom-owned versus "nested" (physically inside another sub-area).
    bool isEntranceHomeStageAlwaysAccessible(s32 linkIndex);
    // True when linkIndex's home-side node (home_in, or home_out if outSide)
    // can actually be reached from its kingdom's own default start using only
    // that kingdom's own internal route graph, under maximal state. Being on
    // the kingdom's main stage isn't sufficient by itself: some doors are
    // gated behind other in-kingdom progression (a one-way drop, a section
    // only opened by walking out of a different sub-area) and aren't
    // free-standing roots the way a true, unconditional overworld pipe is.
    // Used by the entrance shuffle to decide which home doors are safe to
    // seed a chain from.
    bool isEntranceHomeSideUnconditionallyReachable(s32 linkIndex, bool outSide);
    // Fills outLinks[0 .. min(return value, maxOut)) with the indices of
    // randomizable entrance links whose sub-area cannot be proven reachable
    // from an overworld entrance under maximal state (every token, widest
    // per-kingdom scenario). The return value is the true count even if it
    // exceeds maxOut. Used by the entrance shuffle to repair connectivity
    // gaps after chain-fill; does not log or warn by itself.
    s32 findUnreachableEntranceLinks(s16* outLinks, s32 maxOut);

    // Equipment the player already owns going into this generation, on top of
    // whatever opener the seed rolls. Only a Soft Nuzlocke reroll sets this: the
    // run keeps a share of its captures and abilities across a death, and
    // without this the logic solves for someone who owns nothing and spends
    // opener slots re-supplying what is already in hand.
    //
    // Captures are keyed by CaptureRando index, abilities by AbilityEntry::id -
    // the same bit layout SoftSeedRequest uses. Set before generation, clear
    // after; a new-game generation must see an empty carry.
    void setCarriedStartingEquipment(u64 captures, u64 abilities);

    bool isInitialized();
    bool usedLogicPlacement();
    bool logicSucceeded();
    FailureCode getFailureCode();
    const char* getFailureMessage();
    const char* getFailureCodeName(FailureCode code);
    s32 getSearchNodeCount();
    s32 getSearchBundleCount();
    s32 getGenerationAttemptCount();
    const GenerationAttemptInfo* getGenerationAttempt(s32 index);
    u32 getGenerationDurationMillis();
    // Writes the persisted free-link followed by Cloud-moon link order.  Returns
    // the full count even if maxCount is too small.
    s32 getCloudPaintingLinkOrder(s16* outOrder, s32 maxCount);
    bool isRemapDest(const char* objectId, const char* stage);
    const ShineDB::ShineEntry* getCreditMoon(const char* objectId, const char* stage);
    const Placement* getPlacementForItem(s32 uid, const char* stage);
    const ShineDB::ShineEntry* getItemForLocation(s32 uid, const char* stage);
    // Count physically completed Figure Walking stages. Vanilla derives this
    // from the source ShineInfo records, while moon randomization commits the
    // destination record instead. Returns -1 when the query is not the
    // Figure Walking object/optional-id query.
    s32 getFigureWalkingCollectedCount(const GameDataFile* file, const char* objectNameOrOptionalId);
    // Return the collected state for a Picture Match ShineActor placement.
    // Returns -1 when the placement is not one of the four Picture Match
    // source moons.
    s32 getPictureMatchCollectedState(const GameDataFile* file, const char* objectId, const char* stage);
    bool kingdomHasMoonRockKey(s32 kingdom);
    const ShineDB::ShineEntry* getMoonRockKeyForKingdom(s32 kingdom);
    s32 getMoonRockKeyKingdomForItem(s32 uid, const char* stage);
    bool isMoonRockKeyItem(s32 uid, const char* stage);

    // Subarea Keys. Slots come from SubareaKeyDB::keySlotAt, so the mode decides
    // how coarsely one key covers (kingdom, door kind) pairs and callers do not
    // have to care which mode is active.
    s32 getSubareaKeySlotCount();
    const ShineDB::ShineEntry* getSubareaKeyForSlot(s32 slot);
    s32 getSubareaKeySlotForItem(s32 uid, const char* stage);
    bool isSubareaKeyItem(s32 uid, const char* stage);
    // Assignment queries: does a key covering this exist in this seed at all?
    // The generation-side guarantee is all-or-nothing, so an incomplete layout
    // reports nothing assigned rather than a usable subset. UI must ask these
    // before asking hasSubareaKey, which deliberately falls open when it cannot
    // tell and would otherwise paint an unassigned pair as permanently owned.
    bool isSubareaKeyLayoutComplete();
    bool isSubareaKeySlotAssigned(s32 slot);
    bool hasSubareaKeyAssignment(s32 kingdom, s32 subareaType);
    // Live gameplay queries: has the player collected the key covering this
    // door? These read the running save, not the generation-time simulation.
    bool hasSubareaKey(GameDataFile* file, s32 kingdom, s32 subareaType);
    // Whether entering this entrance link is currently allowed. Keys on the
    // link's home side, so entrance rando never changes which key it asks for.
    bool canEnterSubareaLink(GameDataFile* file, s32 linkIndex);
    // Toadette Medals: a per-seed subset of moons (excluding Mushroom/Dark/Darker
    // Side) designated as the alternate win-condition goal. A medal still
    // counts toward its credited kingdom's unlock requirement as normal -
    // isToadetteMedalItem is purely additional bookkeeping.
    //
    // A medal that is a triple pays out three moons and therefore three medals,
    // so entry counts and medal counts differ: getToadetteMedalTotalCount is the
    // number of pool ENTRIES (the moon-list row count and the range of
    // getToadetteMedalAt), while getToadetteMedalTotalValue and
    // getToadetteMedalCollectedCount count MEDALS and are what the required
    // medal setting is measured against.
    bool isToadetteMedalItem(s32 uid, const char* stage);
    s32 getToadetteMedalTotalCount();
    s32 getToadetteMedalTotalValue();
    s32 getToadetteMedalCollectedCount(GameDataFile* file);
    const ShineDB::ShineEntry* getToadetteMedalAt(s32 index);
    // Medals the index-th pool entry is worth (3 for a triple, otherwise 1).
    s32 getToadetteMedalValueAt(s32 index);
    bool hasTriggeredToadetteMedalWin();
    void setToadetteMedalWinTriggered(bool triggered);
    // True when the moon at this location is a multi/triple moon. Scenario progression
    // is only driven by multi moons (single story moons never advance ScenarioNo).
    // This is the VANILLA physical flag and is NOT affected by "Randomize Triple Moons".
    bool isMultiMoonLocation(s32 uid, const char* stage);
    // True when collecting this moon/item pays out as a triple (gives 3, grand
    // visual), honoring the "Randomize Triple Moons" reward override; falls back
    // to the vanilla ShineFlag_Multi when that option is off. Runtime reward/count/
    // visual hooks use these instead of reading ShineFlag_Multi directly.
    bool isRewardMultiMoon(s32 uid, const char* stage);
    bool isRewardMultiEntry(const ShineDB::ShineEntry* entry);
    // Kingdom a moon is assigned to after the redistribution pre-processing (moon
    // lists/counts/requirements and the role assigners read this, not the home
    // kingdom). Returns the home kingdom when redistribution is inactive, or -1 if
    // the moon is unknown.
    s32 getAssignedKingdom(s32 uid, const char* stage);
    s32 getAssignedKingdomForEntry(const ShineDB::ShineEntry* entry);
    bool isKingdomRedistributionActive();
    u32 getMoonRoleLayoutHash();
    bool getKingdomAllocationSummary(s32 worldId,
        KingdomAllocationSummary* outSummary);
    s32 getPlacementCount();

    // Auto-grants are item moon-table indices that the solver could not place
    // at a physical location. They remain separate from Placement so consumers
    // can distinguish a real pickup from a start-of-run grant.
    s32 getAutoGrantCount();
    s32 getAutoGrantItemIndex(s32 index);
    const ShineDB::ShineEntry* getMoonEntry(s32 index);
    const Placement* getPlacement(s32 index);
    // True when getPlacement(index) was placed by the buffer engine (route capacity,
    // story, capture or ability work) rather than the even leftover fill.
    bool isCriticalPathPlacement(s32 index);
    s32 getPlacementItemForLocation(s32 index);
    u32 getLastEntranceAttempt();
    s32 getPathStepCount();
    const PathStep* getPathStep(s32 index);
    s32 getOptimizedPathStepCount();
    const PathStep* getOptimizedPathStep(s32 index);
    s32 getSphereCount();
    const SphereInfo* getSphere(s32 index);
    bool isPathMoon(s32 ldMoonIndex);
    s32 getMoonCount();
    bool finalObjectiveReached();
    const char* getFinalObjectiveNote();
    s32 getWarningCount();
    const char* getWarning(s32 index);
    s32 getPaintingLinkCount();
    PaintingLinkInfo getPaintingLinkInfo(s32 index);

    // Recomputes per-kingdom hint status from the current save if the owned-moon
    // count or physical kingdom visits have changed; cheap to call every frame
    // otherwise. Unvisited kingdoms report Unavailable.
    void refreshKingdomHintStatus(GameDataHolderAccessor accessor);
    KingdomHintStatus getKingdomHintStatus(s32 kingdom);
    // Number of currently reachable, not-yet-collected, CRITICAL placements in a
    // physically visited kingdom. A purely-optional reachable pickup does not
    // add to this count even though it still turns the kingdom's dot Orange.
    // Zero whenever the dot is Red or Orange. A placement is attributed to a
    // kingdom purely physically: it counts when the player can walk to it from
    // that kingdom's own front door OR from a loading-zone/painting arrival the
    // save has actually discovered there, following every loading zone their
    // current movement already opens and stopping at a dead end or the next
    // kingdom's front door. The location's static/vanilla home kingdom is
    // deliberately NOT used - a shuffled loading zone routinely moves a check
    // to a different kingdom entirely - and the same placement can legitimately
    // count for two kingdoms when it is reachable from both.
    s32 getKingdomHintCount(s32 kingdom);
    // Number of currently reachable, not-yet-collected placements in a physically
    // visited kingdom (same attribution rule as getKingdomHintCount), regardless
    // of whether they're progression-critical. For the tracker's plain "Hints"
    // mode, which reports reachability only and doesn't judge importance.
    s32 getKingdomHintTotalCount(s32 kingdom);

    // Same attribution/reachability rule as getKingdomHintCount, but returns the
    // actual placements instead of just a count. Treats the selected kingdom's
    // front door plus its recorded entrance/painting arrivals as available
    // starts, writes up to maxCount entries into outEntries, and returns how many
    // were written. Used by the "Kingdom Checks" mod-config menu, which applies
    // each kingdom's physical/story-order rules.
    s32 getKingdomReachableUncollectedMoons(GameDataHolderAccessor accessor, s32 kingdom, const ShineDB::ShineEntry** outEntries, s32 maxCount);
    // Kingdoms that can be reached directly from `kingdom` with the current
    // save state. Direct means the route starts in the selected kingdom and
    // does not pass through another kingdom's overworld first. Loading zones
    // and unlocked paintings are considered; Odyssey travel is not a
    // walkable route for this list.
    s32 getKingdomDirectAccesses(GameDataHolderAccessor accessor, s32 kingdom,
        s16* outKingdoms, s32 maxCount);

    // ── Sub-area routes (Extended Hints / "Kingdom Checks" path view) ────────
    //
    // A human-readable walkthrough of the loading zones that lead from a
    // kingdom's overworld to the sub-area a placement actually sits in this
    // seed, e.g.
    //
    //     Sand Kingdom
    //      > Pipe to Slots
    //      > Progress through Invisible Road
    //      > Reach Deep Woods (exit side)
    //
    // Loading zones are named by the area they lead to in the VANILLA game,
    // because that is how a player recognizes them on the ground; every other
    // line describes where the shuffle actually drops you. Seed routes start
    // from one kingdom's overworld and follow loading zones only - paintings
    // are not movement options there.
    static constexpr s32 kMaxAreaRoutes = 4;
    static constexpr s32 kMaxAreaRouteSteps = 10;
    static constexpr s32 kAreaRouteStepTextSize = 112;

    struct AreaRoute {
        char steps[kMaxAreaRouteSteps][kAreaRouteStepTextSize];
        s32 stepCount;
    };

    struct AreaRouteSet {
        AreaRoute routes[kMaxAreaRoutes];
        s32 routeCount;
        // More distinct routes exist than the ones reported here.
        bool truncated;
    };

    // True when this placement sits behind at least one loading zone rather than
    // out in a kingdom's own overworld - the case where "which kingdom" is not
    // enough to find it and a route may be worth describing. A route is still
    // rejected when the location is only reachable through a painting.
    bool isLocationInSubArea(const ShineDB::ShineEntry* location);

    // How this seed is WIRED, independent of the save: every ability and
    // capture assumed, widest scenario, starting separately in each accessible
    // kingdom and following loading zones only. This is the answer a hint should
    // give - it describes the shuffle, not the player's progress, so it reads
    // the same whenever it is bought and wherever Mario happens to be standing.
    // Shortest route first.
    bool buildSeedRoutesToLocation(const ShineDB::ShineEntry* location, AreaRouteSet* out);

    // What the player can walk RIGHT NOW, starting from `startKingdom`'s own
    // overworld and using only the movement they already have. For the
    // "Kingdom Checks" list, whose whole premise is present-tense reachability;
    // unlike seed routes, this view includes unlocked paintings.
    bool buildReachableRoutesToLocation(GameDataHolderAccessor accessor,
        const ShineDB::ShineEntry* location, s32 startKingdom, AreaRouteSet* out);

    // Current-save route view for a direct kingdom access entry. Routes start
    // in `startKingdom`, include loading zones and unlocked paintings, and stop
    // as soon as they reach `targetKingdom` (or any other kingdom first).
    bool buildReachableRoutesToKingdom(GameDataHolderAccessor accessor,
        s32 startKingdom, s32 targetKingdom, AreaRouteSet* out);

} // namespace MoonRando
