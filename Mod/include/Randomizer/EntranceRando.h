#pragma once
#include "EntranceDB.h"

namespace EntranceRando {

    // A door's outward connection. The shuffle pairs doors symmetrically, so
    // walking back through the door you arrived from always returns you to
    // where you came from (two-way doors). Chains are emergent: an overworld
    // pipe leads into a sub-area, whose other door leads into the next sub or
    // surfaces at another (possibly different-kingdom) overworld pipe.
    enum ConnKind : u8
    {
        ConnKind_None, // door not connected by the shuffle (vanilla fallback)
        ConnKind_SubNear, // leads into link's sub-area, spawning at its near side
        ConnKind_SubFar, // leads into link's sub-area, spawning at its far side
        ConnKind_HomeOut, // surfaces at link's overworld exit side (homeEntranceOut)
        ConnKind_Home = ConnKind_HomeOut, // legacy alias for serialized v1 saves
        ConnKind_HomeIn, // surfaces at link's overworld entry side (homeEntranceIn)
    };

    struct Conn {
        s16 link;
        ConnKind kind;
    };

    // A door handle is link * DoorSlot_Count + slot. The placement engine
    // builds and reads these too, so the encoding lives here rather than
    // privately in the builder.
    enum DoorSlot : s32
    {
        DoorSlot_HomeIn = 0,
        DoorSlot_HomeOut = 1,
        DoorSlot_SubNear = 2,
        DoorSlot_SubFar = 3,
        DoorSlot_Count = 4,
    };

    inline s32 makeDoor(s32 link, DoorSlot slot) {
        return link * DoorSlot_Count + static_cast<s32>(slot);
    }
    inline s32 doorLink(s32 door) { return door / DoorSlot_Count; }
    inline DoorSlot doorSlot(s32 door) {
        return static_cast<DoorSlot>(door % DoorSlot_Count);
    }

    // What wiring a given target would give the player. Derived from static
    // door data (does this sub have a usable far door? is this a home surface
    // in another kingdom?), so classifying is free and needs no reachability
    // simulation - only an Expand move has to actually prove anything.
    enum DoorClass : u8
    {
        // A sub-area with no onward door, or whose far door its near side
        // cannot reach. Walking in means walking back out.
        DoorClass_DeadEnd,
        // A sub-area that holds locations but hands back no new door.
        DoorClass_Pocket,
        // A sub-area whose far door continues the chain.
        DoorClass_PassThrough,
        // A home-side surface: you come out in some kingdom's overworld.
        DoorClass_CrossKingdom,
        DoorClass_Count,
    };

    // Why the fill wants a door wired right now.
    enum WireIntent : u8
    {
        // The frontier is stuck; only a pairing that actually opens something
        // new is acceptable. Ignores the open-door reserve.
        WireIntent_Expand,
        // Ordinary progress: draw by the Seed Openness door-class weights.
        WireIntent_Fill,
        // Deliberately spend a door on something that opens little or nothing.
        // This is what makes a low-openness seed feel closed.
        WireIntent_Compact,
    };

    // ── one-shot build (Moon Rando off) ──────────────────────────────
    // Builds the entire table in one call, exactly as the standalone shuffle
    // always did. Used when there is no placement engine to interleave with.
    void buildRemapTable(u32 seed, u32 attempt = 0);

    // ── incremental build (Moon Rando on) ────────────────────────────
    // The placement engine decides the wiring one door at a time, so it can
    // pick doors knowing what they open. Sequence per generation attempt:
    //
    //   beginIncrementalBuild(seed, attempt)   - classify pools, reserve the
    //                                            Darker Side warp and the
    //                                            non-root gateways, wire nothing
    //   ... listUnwiredSources / listUnwiredTargets / commitWiring ...
    //   finalizeIncrementalBuild()             - pair everything left over
    //
    // Between begin and finalize the table is PARTIAL: unwired doors carry
    // ConnKind_None and the reachability model treats them as leading nowhere,
    // which is what lets the fill reason about a door before it exists.
    void beginIncrementalBuild(u32 seed, u32 attempt);

    // Doors that may seed a pairing: provably reachable within their own
    // kingdom's route graph, or handed back by an already-wired sub-area.
    // Returns the true count even if it exceeds maxOut. outIsChainExit, when
    // given, is filled in parallel: true for a door an already-wired sub-area
    // handed back, false for a free-standing kingdom entrance. The caller needs
    // that split to apply the Seed Openness chain bias.
    s32 listUnwiredSources(s32* out, u8* outIsChainExit, s32 maxOut);

    // The chain-exit : kingdom-entrance draw weights for the current openness.
    void getChainBias(s32* outChainExitWeight, s32* outKingdomWeight);

    // Doors that may receive a pairing from sourceDoor, filtered by the active
    // mode (Limited keeps both ends inside one kingdom) and by the structural
    // exclusions the one-shot builder applies. Returns the true count.
    s32 listUnwiredTargets(s32 sourceDoor, s32* out, s32 maxOut);

    // What wiring targetDoor would hand the player. Cached per attempt.
    DoorClass classifyTarget(s32 targetDoor);

    // The Conn a door hands out once paired - i.e. where you end up if you walk
    // through whatever is wired to it. Lets a caller simulate a pairing before
    // committing to it.
    Conn connForDoor(s32 door);

    // Relative weight of each door class under the current Seed Openness.
    s32 doorClassWeight(DoorClass doorClass);

    // How many reachable unwired sources a discretionary wiring must leave
    // standing, so the player keeps that many unexplored loading zones.
    s32 openDoorReserve();

    // Pairs the two doors and updates the pools. Returns false if either door
    // is not currently pairable (already wired, or not in the pools).
    bool commitWiring(s32 sourceDoor, s32 targetDoor);

    // Pairs every door the fill did not get to, preferring to give a still
    // unreachable sub-area a reachable partner before pairing two reachable
    // leftovers with each other. Also runs the shop swap. Idempotent.
    void finalizeIncrementalBuild();

    // Whether an incremental build is open (begun and not yet finalized).
    bool isIncrementalBuildActive();

    // While set, a door the fill has not wired yet answers with its VANILLA
    // connection instead of "leads nowhere". Exists for the Randomized Starting
    // Position walk-back proof, which runs before the fill has chosen any doors
    // and would otherwise see a world with no loading zones in it at all and
    // reject almost every candidate. The chosen start is re-validated against
    // the real wiring once the build is finalized. Nothing else may use this:
    // the fill itself MUST see unwired doors as leading nowhere, or it would
    // reason about connectivity that does not exist.
    void setAssumeVanillaForUnwiredDoors(bool enabled);

    // Whether every randomizable door now carries an outward connection.
    bool isFullyWired();

    // Last-resort connectivity repair, run after finalize when some sub-area
    // still cannot be reached. Breaks a pairing the leftover pass made between
    // two already-reachable doors and hands one half to the orphan, which
    // cannot strand anything. Returns how many links it re-paired. Only pairs
    // the leftover pass made are eligible; anything the fill chose is
    // load-bearing and is never touched.
    s32 repairUnreachableSubAreas(const s16* links, s32 linkCount);

    // How many sub-area items are still waiting for a partner. Zero means the
    // remaining work is only leftover pairing.
    s32 getUnwiredTargetCount();
    bool isInitialized();
    void reset();
    void restore(const Conn* pipe, const Conn* homeOut, const Conn* subNear, const Conn* subFar, s32 count, u32 seed, u32 attempt, s32 logicDifficulty);

    // Whether the current game mode pins a normally randomizable link to its
    // vanilla connection. Placement logic uses this too, so its reachability
    // model matches the runtime shuffle pool.
    bool isLinkPinnedToVanilla(s32 linkIndex);

    // Static connection lookups. With the feature off (or for non-randomizable
    // links) these return the vanilla wiring for each physical side.
    Conn getPipeConn(s32 linkIndex); // entering the overworld entry side of link
    Conn getHomeDoorConn(s32 linkIndex, bool outSide); // entering a specific overworld side
    Conn getSubDoorConn(s32 linkIndex, bool farDoor); // leaving the sub via a door

    // Whether the link's sub-area has a distinct far door (subEntranceOut).
    bool linkHasFarDoor(s32 linkIndex);

    // The Darker Side story link stays out of the shuffle pool, but one
    // randomized home-side door may target it as a one-way home warp.
    bool isDarkerSideLink(s32 linkIndex);

    // Tracks the sub-stage the player is currently inside. Only used to fix up
    // getStageNameCurrent() for in-sub menus (shops/closet) — never routing,
    // which is fully static now that connections are symmetric.
    void noteEnteredSub(const char* subStage);
    void clearSubStack();
    const char* peekTopSubStage();

    // Reconcile the cached "current sub" with a stage the player reached by a
    // non-door teleport (a warp), which bypasses the routing that normally keeps
    // the cache current. Sets the cache if stageName is a known sub-stage,
    // otherwise clears it. Without this, a warp out of a sub-area leaves the
    // cache stale and the next door transition is misclassified.
    void syncSubTrackingToStage(const char* stageName);

} // namespace EntranceRando
