#include "Randomizer/EntranceRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace EntranceRando {

    namespace LD = Randomizer::LogicData;

    static_assert(EntranceDB::sLinkCount == LD::sEntranceCount,
        "EntranceDB and LogicData entrance tables must remain index-aligned");

    // ─────────────────────────────────────────────────────────────
    // Logging
    // ─────────────────────────────────────────────────────────────

    // Randomizer::reinit() re-runs the whole dependency chain on every
    // stage-scene init, and this builder narrates ~10 lines per run (door pools,
    // gateway reservations, pipe assignments), so the same block repeated on
    // every stage load. Every one of those goes through logf, so one gate here
    // silences the repeats: buildRemapTable clears it for a shuffle that
    // actually differs from the last one narrated. Diagnostics outside a build
    // are unaffected - the flag is only false while re-narrating a known result.
    static bool sSuppressBuildLog = false;

    static void logf(const char* fmt, ...) {
        if (sSuppressBuildLog)
            return;
        char buf[128];
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        Randomizer::logPush(buf);
    }

    // ─────────────────────────────────────────────────────────────
    // Connection tables
    //
    // One outward connection per door. Doors come in four flavors per link:
    // the overworld entry side (homeEntranceIn), the distinct overworld exit
    // side (homeEntranceOut, when distinct), the sub-area's near side
    // (subEntranceIn), and its far side (subEntranceOut, when distinct). The
    // shuffle pairs doors, writing both directions, so backtracking always
    // works on the exact side that was used.
    // ─────────────────────────────────────────────────────────────

    static Conn sPipeConn[EntranceDB::sLinkCount];
    static Conn sHomeOutConn[EntranceDB::sLinkCount];
    static Conn sSubNearConn[EntranceDB::sLinkCount];
    static Conn sSubFarConn[EntranceDB::sLinkCount];

    static bool sInitialized = false;
    static u32 sLastSeed = 0xFFFFFFFF;
    static u32 sLastAttempt = 0xFFFFFFFF;
    static s32 sLastDifficulty = -1;
    static Randomizer::EntranceRandoMode sLastMode = Randomizer::EntranceRandoMode_Off;
    static Randomizer::ShopLocationMode sLastShopMode = Randomizer::ShopLocationMode_Randomized;
    // Seed Openness is a real build input (chainBiasForOpenness, doorClassWeight,
    // openDoorReserve), so it belongs in the cache key exactly like the logic
    // difficulty does. Without it, changing Openness on the Moon-Rando-off path
    // - the only one that calls buildRemapTable - silently reuses the previous
    // mapping.
    static s32 sLastOpenness = -1;

    bool linkHasFarDoor(s32 linkIndex) {
        return EntranceDB::hasSubOutDoor(linkIndex);
    }

    static bool linkHasHomeOutDoor(s32 linkIndex) {
        return EntranceDB::hasHomeOutDoor(linkIndex);
    }

    // True whenever the home side has ANY distinct out surface - a real walk-in
    // Door, or a ReturnSpawn (landing-only, not enterable). Route-target
    // decisions (does the sub's far door surface at the home's out spot, or
    // fall back to sharing the home's in spot?) must use this broader check;
    // linkHasHomeOutDoor/isHomeOutEnterable stay Door-only for "can the player
    // walk into this from the overworld" checks.
    static bool linkHasHomeOutSurface(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        return link && link->homeOutBehavior != EntranceDB2::DoorSideBehavior_SameSide;
    }

    bool isDarkerSideLink(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        return link
            && std::strcmp(link->homeStage, "Special2WorldHomeStage") == 0
            && std::strcmp(link->subStage, "Special2WorldLavaStage") == 0;
    }

    static s32 findDarkerSideLink() {
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (isDarkerSideLink(i))
                return i;
        }
        return -1;
    }

    // ─────────────────────────────────────────────────────────────
    // Non-root kingdoms
    //
    // The Odyssey route unlocks Cap..Moon on its own, so any home door in
    // those kingdoms is walkable from a fresh file and can seed an entrance
    // chain unconditionally. Mushroom Kingdom, Dark Side and Darker Side are
    // NOT on that route: under entrance rando the only way into them is a
    // door chain arriving from somewhere else. Treating their home doors as
    // free-standing chain roots is what lets them wire into a closed
    // component - Mushroom's doors pair with Mushroom's sub-areas, whose
    // exits pair onward, and nothing outside ever points back in.
    //
    // Darker Side is not shuffled at all (isRandomizableLink rejects it) and
    // instead gets one reserved one-way home warp, so only Mushroom and Dark
    // Side need a gateway reserved out of the pools below. What they do share
    // is the requirement that whatever leads into them must itself be
    // provably reachable, which is exactly what keeping their doors out of
    // kingdomEntrances buys.
    // ─────────────────────────────────────────────────────────────

    enum NonRootKingdom : s32
    {
        NonRootKingdom_Mushroom = 0,
        NonRootKingdom_DarkSide = 1,
        NonRootKingdom_Count = 2,
    };

    static constexpr const char* sNonRootKingdomHomeStages[NonRootKingdom_Count] = {
        "PeachWorldHomeStage",
        "Special1WorldHomeStage",
    };

    // Which non-root kingdom this link's HOME side sits in, or -1 when the
    // home side is on the Odyssey route (the overwhelmingly common case).
    static s32 nonRootKingdomForLinkHome(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        if (!link || !link->homeStage)
            return -1;
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            if (std::strcmp(link->homeStage, sNonRootKingdomHomeStages[k]) == 0)
                return k;
        }
        return -1;
    }

    // Mushroom has 12 shuffled links and Dark Side 6, so two home doors each
    // caps out well under this; the bound only exists so the pools are fixed
    // size like every other array in the builder.
    static constexpr s32 kMaxPendingHomeDoors = 64;

    // ─────────────────────────────────────────────────────────────
    // Shop doors
    //
    // Randomizer::ShopLocationMode can pull the Crazy Cap doors out of the
    // ordinary shuffle. Every shop link is single-door - both of its out sides
    // are SameSide - so a shop is exactly one home-in door paired with one
    // sub-near door, and swapping them is a plain permutation of those pairs.
    //
    // Sand's shop also has a back way in over the roof ("bar2"). That is
    // deliberately NOT a shop entrance for this setting: it stays an ordinary
    // loading zone in every mode, so it keeps shuffling with everything else.
    // ─────────────────────────────────────────────────────────────

    static bool isShopSubStage(const char* subStage) {
        if (!subStage)
            return false;
        static constexpr const char* sShopSubStages[] = {
            "SandWorldShopStage",
            "LakeWorldShopStage",
            "ClashWorldShopStage",
            "CityWorldShop01Stage",
            "SnowWorldShopStage",
            "LavaWorldShopStage",
            "SkyWorldShopStage",
            "MoonWorldShopRoom",
            "PeachWorldShopStage",
        };
        for (const char* stage : sShopSubStages) {
            if (std::strcmp(subStage, stage) == 0)
                return true;
        }
        return false;
    }

    static bool isSandShopBackEntrance(const EntranceDB::EntranceLink& link) {
        return std::strcmp(link.subStage, "SandWorldShopStage") == 0
            && std::strcmp(link.homeEntranceIn, "bar2") == 0;
    }

    static bool isShopLink(s32 index) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(index);
        return link
            && EntranceDB::isRuntimeUsableLink(*link)
            && isShopSubStage(link->subStage)
            && !isSandShopBackEntrance(*link);
    }

    // The shop link that owns index's swap slot: the first shop link sharing
    // its sub-stage. Only Metro has two - its two street doors open into one
    // interior and are one player-facing interface - so for every other shop
    // this is the link itself. The loser of that tie is collapsed onto the
    // owner's connections after the swap, which is what makes both Metro
    // street doors lead to the same shop and both doors inside the Metro shop
    // lead back to the same place.
    static s32 shopSlotOwner(s32 index) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(index);
        if (!link)
            return index;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (isShopLink(i)
                && std::strcmp(EntranceDB::sLinks[i].subStage, link->subStage) == 0)
                return i;
        }
        return index;
    }

    // Whether the Shop Locations setting - not the ordinary shuffle - decides
    // where a shop door goes.
    static bool shopDoorsLeaveMainPool() {
        return Randomizer::getShopLocationMode() != Randomizer::ShopLocationMode_Randomized;
    }

    bool isLinkPinnedToVanilla(s32 linkIndex) {
        // Shop Locations: Vanilla is a real pin, so placement logic sees these
        // doors as fixed two-way links to their own shop rather than following
        // a shuffled connection. Swap is not pinned here - those doors still
        // move, just only among each other (see isRandomizableLink).
        if (Randomizer::getShopLocationMode() == Randomizer::ShopLocationMode_Vanilla
            && isShopLink(linkIndex))
            return true;

        if (!Randomizer::isToadetteMedalModeEnabled())
            return false;

        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        return link
            && std::strcmp(link->homeStage, "PeachWorldHomeStage") == 0
            && std::strcmp(link->subStage, "PeachWorldCastleStage") == 0;
    }

    static bool isRandomizableLink(s32 index) {
        if (index < 0 || index >= EntranceDB::sLinkCount || index >= LD::sEntranceCount)
            return false;
        if (isDarkerSideLink(index))
            return false;
        if (isLinkPinnedToVanilla(index))
            return false;
        if (!LD::sEntrances[index].randomizable)
            return false;

        // Shop Locations: Swap keeps these doors out of the general pool and
        // wires them itself once the builder has finished (wireShopSwap).
        if (shopDoorsLeaveMainPool() && isShopLink(index))
            return false;

        // Deep Woods has two physical ceiling drops, but they are one
        // player-facing entrance interface.  The primary row owns the shuffle
        // slot; the secondary drop remains a source alias and is collapsed onto
        // that slot after the table is built.
        const s32 deepWoodsPrimary = EntranceDB::findDeepWoodsPrimaryLink();
        if (deepWoodsPrimary >= 0
            && EntranceDB::isDeepWoodsArea(index)
            && index != deepWoodsPrimary)
            return false;

        return true;
    }

    // Deep Woods is directional rather than four independently pairable doors:
    // its ceiling drops are entrance-only and its vines are exit-only.  All
    // drops share one shuffled source, all vines share one shuffled source, and
    // the reverse trip must arrive at the primary safe endpoint -- never on a
    // vine trigger or a ceiling-drop ChangeStageArea in Wooded Kingdom.
    static Conn normalizeDeepWoodsArrival(Conn conn) {
        if (conn.link < 0 || conn.link >= EntranceDB::sLinkCount
            || !EntranceDB::isDeepWoodsArea(conn.link))
            return conn;

        const s32 primary = EntranceDB::findDeepWoodsPrimaryLink();
        if (primary < 0)
            return conn;

        switch (conn.kind) {
        case ConnKind_SubNear:
        case ConnKind_SubFar:
            // There is exactly one allowed arrival inside Deep Woods: its
            // primary ceiling-drop spawn.
            return { (s16)primary, ConnKind_SubNear };
        case ConnKind_HomeIn:
        case ConnKind_HomeOut:
            // There is exactly one allowed return arrival in Wooded Kingdom:
            // the primary vine-return spawn.
            return { (s16)primary, ConnKind_HomeOut };
        default:
            return conn;
        }
    }

    static bool doorExists(s32 link, s32 slot) {
        switch (slot) {
        case DoorSlot_HomeIn:
        case DoorSlot_SubNear:
            return true;
        case DoorSlot_HomeOut:
            return linkHasHomeOutDoor(link);
        case DoorSlot_SubFar:
            return linkHasFarDoor(link);
        default:
            return false;
        }
    }

    static Conn vanillaConnForDoor(s32 door) {
        const s16 link = (s16)(door / DoorSlot_Count);
        switch (door % DoorSlot_Count) {
        case DoorSlot_HomeIn:
            return { link, ConnKind_SubNear };
        case DoorSlot_HomeOut:
            return { link, linkHasFarDoor(link) ? ConnKind_SubFar : ConnKind_SubNear };
        case DoorSlot_SubNear:
            if (linkHasHomeOutSurface(link) && !linkHasFarDoor(link))
                return { link, ConnKind_HomeOut };
            return { link, ConnKind_HomeIn };
        case DoorSlot_SubFar:
            return { link, linkHasHomeOutSurface(link) ? ConnKind_HomeOut : ConnKind_HomeIn };
        default:
            return { (s16)-1, ConnKind_None };
        }
    }

    static void setVanillaConns() {
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            sPipeConn[i] = vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeIn);
            sHomeOutConn[i] = linkHasHomeOutDoor(i)
                ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeOut)
                : Conn { (s16)-1, ConnKind_None };
            sSubNearConn[i] = vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubNear);
            sSubFarConn[i] = linkHasFarDoor(i)
                ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubFar)
                : Conn { (s16)-1, ConnKind_None };
        }
    }

    static void collapseDeepWoodsSourceConns() {
        const s32 primary = EntranceDB::findDeepWoodsPrimaryLink();
        if (primary < 0)
            return;

        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (i == primary || !EntranceDB::isDeepWoodsArea(i))
                continue;

            // The two Wooded falling zones are one player-facing exit, and all
            // Deep Woods vines are one player-facing exit. Keep the distinct
            // arrival slots, but make every physical source use the primary
            // randomized connection.
            sPipeConn[i] = sPipeConn[primary];
            if (linkHasFarDoor(i) && linkHasFarDoor(primary))
                sSubFarConn[i] = sSubFarConn[primary];
        }
    }

    static void collapseIceCaveEntrancePipeAlias() {
        const s32 primary = EntranceDB::findIceCavePrimaryLink();
        const s32 alias = EntranceDB::findIceCaveEntrancePipeAliasLink();
        if (primary < 0 || alias < 0)
            return;

        // The entrance pipe is not a second randomized home-side entry. It is
        // the return half of the ceiling-drop interface, so it must use the
        // exact connection that brought the player into the primary near side.
        const Conn coupled = sSubNearConn[primary];
        sSubFarConn[alias] = coupled.kind != ConnKind_None
            ? coupled
            : Conn { (s16)primary, ConnKind_HomeIn };
    }

    static void collapseSpecialSourceConns() {
        collapseDeepWoodsSourceConns();
        collapseIceCaveEntrancePipeAlias();
    }

    // The Jaxi ride (SandWorldHomeStage <-> SandWorldSphinxExStage) is spread
    // over two legacy rows: the primary one owns the drive-in ChangeStageArea
    // ("aaa") plus the interior pipe near the start ("run00"), and a second row
    // owns only the interior pipe at the far end of the ride ("run00return").
    // That second row has no home-in endpoint at all - vanilla sends both
    // interior pipes back to the same overworld pipe - so it is an exit-only
    // alias of the ride, not a loading zone the player can enter from the
    // overworld.
    static bool isJaxiRideLink(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        return link
            && std::strcmp(link->homeStage, "SandWorldHomeStage") == 0
            && std::strcmp(link->subStage, "SandWorldSphinxExStage") == 0;
    }

    static s32 findJaxiRideFarPipeLink() {
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (isJaxiRideLink(i) && !EntranceDB::isHomeInEnterable(i))
                return i;
        }
        return -1;
    }

    static s32 findJaxiRidePrimaryLink() {
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (isJaxiRideLink(i) && EntranceDB::isHomeInEnterable(i))
                return i;
        }
        return -1;
    }

    // ─────────────────────────────────────────────────────────────
    // Builder
    //
    // Door handle: link*4 + 0 (home in), +1 (home out), +2 (sub near),
    // +3 (sub far).
    // ─────────────────────────────────────────────────────────────

    static Conn connToDoor(s32 door) {
        const s16 link = (s16)(door / DoorSlot_Count);
        switch (door % DoorSlot_Count) {
        case DoorSlot_HomeIn:
            return { link, ConnKind_HomeIn };
        case DoorSlot_HomeOut:
            return { link, ConnKind_HomeOut };
        case DoorSlot_SubNear:
            return { link, ConnKind_SubNear };
        default:
            return { link, ConnKind_SubFar };
        }
    }

    static DoorSlot connKindToSlot(ConnKind kind) {
        switch (kind) {
        case ConnKind_SubNear:
            return DoorSlot_SubNear;
        case ConnKind_SubFar:
            return DoorSlot_SubFar;
        case ConnKind_HomeOut:
            return DoorSlot_HomeOut;
        default:
            return DoorSlot_HomeIn;
        }
    }

    static void setDoorOutward(s32 door, Conn conn) {
        conn = normalizeDeepWoodsArrival(conn);
        const s32 link = door / DoorSlot_Count;
        switch (door % DoorSlot_Count) {
        case DoorSlot_HomeIn:
            sPipeConn[link] = conn;
            break;
        case DoorSlot_HomeOut:
            sHomeOutConn[link] = conn;
            break;
        case DoorSlot_SubNear:
            sSubNearConn[link] = conn;
            break;
        default:
            sSubFarConn[link] = conn;
            break;
        }
    }

    static void pairDoors(s32 a, s32 b) {
        setDoorOutward(a, connToDoor(b));
        setDoorOutward(b, connToDoor(a));
    }

    static s32 takeRandomHomeInDoor(s32* doors, s32* count, Randomizer::Rng& rng) {
        s32 candidates[EntranceDB::sLinkCount * 2];
        s32 candidateCount = 0;
        for (s32 i = 0; i < *count; i++) {
            if (doors[i] % DoorSlot_Count == DoorSlot_HomeIn)
                candidates[candidateCount++] = i;
        }
        if (candidateCount == 0)
            return -1;

        const s32 candidateIndex = (s32)rng.nextModulo((u32)candidateCount);
        const s32 arrayIndex = candidates[candidateIndex];
        const s32 door = doors[arrayIndex];
        doors[arrayIndex] = doors[--(*count)];
        return door;
    }

    // A home door the player can walk to on a fresh file: on the Odyssey
    // route, and not gated behind other progression inside its own kingdom.
    static bool isRootReachableHomeDoor(s32 door) {
        const s32 link = door / DoorSlot_Count;
        const s32 slot = door % DoorSlot_Count;
        if (slot != DoorSlot_HomeIn && slot != DoorSlot_HomeOut)
            return false;
        if (nonRootKingdomForLinkHome(link) >= 0)
            return false;
        return MoonRando::isEntranceHomeSideUnconditionallyReachable(link, slot == DoorSlot_HomeOut);
    }

    // homeInOnly restricts the draw to walk-in entry surfaces, which is what
    // the Darker Side warp wants; a plain route source is happy with either side.
    static s32 takeRandomRootReachableHomeDoor(s32* doors, s32* count, Randomizer::Rng& rng,
        bool homeInOnly = false) {
        s32 candidates[EntranceDB::sLinkCount * 2];
        s32 candidateCount = 0;
        for (s32 i = 0; i < *count; i++) {
            if (homeInOnly && doors[i] % DoorSlot_Count != DoorSlot_HomeIn)
                continue;
            if (isRootReachableHomeDoor(doors[i]))
                candidates[candidateCount++] = i;
        }
        if (candidateCount == 0)
            return -1;

        const s32 arrayIndex = candidates[(s32)rng.nextModulo((u32)candidateCount)];
        const s32 door = doors[arrayIndex];
        doors[arrayIndex] = doors[--(*count)];
        return door;
    }

    // A home door belonging to one specific non-root kingdom, used to pick
    // which of that kingdom's overworld surfaces an outside route lands on.
    static s32 takeRandomHomeDoorInNonRootKingdom(s32* doors, s32* count, Randomizer::Rng& rng,
        s32 nonRootKingdom) {
        s32 candidates[EntranceDB::sLinkCount * 2];
        s32 candidateCount = 0;
        for (s32 i = 0; i < *count; i++) {
            const s32 slot = doors[i] % DoorSlot_Count;
            if (slot != DoorSlot_HomeIn && slot != DoorSlot_HomeOut)
                continue;
            if (nonRootKingdomForLinkHome(doors[i] / DoorSlot_Count) == nonRootKingdom)
                candidates[candidateCount++] = i;
        }
        if (candidateCount == 0)
            return -1;

        const s32 arrayIndex = candidates[(s32)rng.nextModulo((u32)candidateCount)];
        const s32 door = doors[arrayIndex];
        doors[arrayIndex] = doors[--(*count)];
        return door;
    }

    static bool containsDoorValue(const s32* doors, s32 count, s32 door) {
        for (s32 i = 0; i < count; i++)
            if (doors[i] == door)
                return true;
        return false;
    }

    // A sub-area near surface usable as BOTH an arrival and a source, so a
    // forced decoupled route can land on it and immediately leave through it.
    // Deep Woods near doors are arrival-only aliases and never appear in the
    // source pool, so the membership check filters them out on its own.
    static s32 takeRandomRelayNearDoor(s32* arrivals, s32* arrivalCount,
        const s32* sources, s32 sourceCount, Randomizer::Rng& rng) {
        s32 candidates[EntranceDB::sLinkCount * 2];
        s32 candidateCount = 0;
        for (s32 i = 0; i < *arrivalCount; i++) {
            if (arrivals[i] % DoorSlot_Count != DoorSlot_SubNear)
                continue;
            if (containsDoorValue(sources, sourceCount, arrivals[i]))
                candidates[candidateCount++] = i;
        }
        if (candidateCount == 0)
            return -1;

        const s32 arrayIndex = candidates[(s32)rng.nextModulo((u32)candidateCount)];
        const s32 door = arrivals[arrayIndex];
        arrivals[arrayIndex] = arrivals[--(*arrivalCount)];
        return door;
    }

    static bool removeDoorValue(s32* doors, s32* count, s32 door) {
        for (s32 i = 0; i < *count; i++) {
            if (doors[i] == door) {
                doors[i] = doors[--(*count)];
                return true;
            }
        }
        return false;
    }

    static s32 takeRandomEntry(s32* entries, s32* count, Randomizer::Rng& rng) {
        if (*count <= 0)
            return -1;
        const s32 index = (s32)rng.nextModulo((u32)*count);
        const s32 entry = entries[index];
        entries[index] = entries[--(*count)];
        return entry;
    }

    static bool setDarkerSideHomeWarp(s32 sourceDoor) {
        const s32 darkerLink = findDarkerSideLink();
        if (sourceDoor < 0 || darkerLink < 0)
            return false;

        setDoorOutward(sourceDoor, Conn { (s16)darkerLink, ConnKind_HomeIn });
        logf("ER Darker Side home warp: door %d -> link %d", sourceDoor, darkerLink);
        return true;
    }

    static void shuffleValues(s32* arr, s32 count, Randomizer::Rng& rng) {
        for (s32 i = count - 1; i > 0; i--) {
            s32 j = (s32)rng.nextModulo((u32)(i + 1));
            s32 tmp = arr[i];
            arr[i] = arr[j];
            arr[j] = tmp;
        }
    }

    // A sub-area's own two doors, reduced to which one is used to enter and
    // which one (if any) becomes newly available once that entry is used.
    // otherDoor is -1 for a dead end (no far door, or a far door that can't
    // be reached from either side - two independent dead ends in that case).
    struct SubareaItem {
        s32 entryDoor;
        s32 otherDoor;
    };

    // ─────────────────────────────────────────────────────────────
    // Seed Openness door policy
    //
    // Openness owns every "shape of the door graph" decision. It replaced the
    // retired Small Chains / Long Chains entrance modes, which only ever moved
    // the chain bias below. Every table is indexed by the resolved openness
    // level 0..4, and index 2 is the historical Standard behaviour.
    // ─────────────────────────────────────────────────────────────

    static s32 opennessLevel() {
        s32 level = static_cast<s32>(Randomizer::getResolvedSeedOpenness());
        if (level < 0)
            return 0;
        if (level >= Randomizer::cSeedAxisLevelCount)
            return Randomizer::cSeedAxisLevelCount - 1;
        return level;
    }

    // How readily a chain keeps extending versus handing back to a fresh
    // kingdom entrance: the weight applied to the chain-exit pool relative to
    // the kingdom-entrance pool when choosing which one to draw from next.
    // Equal weights (Standard) reduce to a uniform draw over the combined pool
    // - the algorithm's natural, unweighted behavior. The other levels bias
    // away from that baseline without changing what's actually safe to draw
    // from: both pools stay provably reachable no matter how they're weighted.
    //
    // Long chains are the LESS open shape: with a fixed door count, short
    // chains mean MORE chains, so more independent overworld entry points and
    // more direct kingdom-to-kingdom doors. Compact therefore reproduces the
    // retired Long Chains mode and Open the retired Small Chains mode.
    static void chainBiasForOpenness(s32* biasNum, s32* biasDen) {
        static constexpr s32 kNum[Randomizer::cSeedAxisLevelCount] = { 4, 3, 1, 1, 1 };
        static constexpr s32 kDen[Randomizer::cSeedAxisLevelCount] = { 1, 1, 1, 3, 4 };
        const s32 level = opennessLevel();
        *biasNum = kNum[level];
        *biasDen = kDen[level];
    }

    s32 doorClassWeight(DoorClass doorClass) {
        // Rows are door classes, columns openness levels. A claustrophobic seed
        // buries the player in dead ends and pockets; a wide-open one keeps
        // handing out doors to other kingdoms. Pass-through is deliberately
        // flat: it is the neutral "the chain continues" outcome, and the chain
        // bias above already decides how often chains keep going.
        static constexpr s32 kWeights[DoorClass_Count][Randomizer::cSeedAxisLevelCount] = {
            { 8, 5, 3, 2, 1 }, // DeadEnd
            { 6, 4, 3, 2, 1 }, // Pocket
            { 3, 3, 3, 3, 3 }, // PassThrough
            { 1, 2, 3, 5, 8 }, // CrossKingdom
        };
        if (doorClass >= DoorClass_Count)
            return 1;
        return kWeights[doorClass][opennessLevel()];
    }

    s32 openDoorReserve() {
        static constexpr s32 kByOpenness[Randomizer::cSeedAxisLevelCount] = { 0, 1, 2, 4, 6 };
        return kByOpenness[opennessLevel()];
    }

    // Whether a door's own outward connection is one the pool-construction
    // loops below feed into the random shuffle (kingdomEntrances or
    // subareas) - i.e. whether some unrelated door can end up reciprocally
    // paired with it. Must mirror those loops exactly.
    static bool isDoorIndependentlyPoolable(s32 link, ConnKind kind) {
        switch (kind) {
        case ConnKind_SubNear:
            return true;
        case ConnKind_SubFar:
            return linkHasFarDoor(link);
        case ConnKind_HomeIn:
            return EntranceDB::isHomeInEnterable(link);
        case ConnKind_HomeOut:
            return EntranceDB::isHomeOutEnterable(link);
        default:
            return false;
        }
    }

    // Whether door can be safely sealed to its vanilla connection with no
    // reciprocal partner. An odd number of doors in the shuffle means one
    // door can never get a distinct partner (pigeonhole), so the leftover
    // fallback below has to give it *some* outward connection - but by the
    // time that fallback runs, its natural vanilla partner has almost
    // always already been reciprocally paired with something else entirely
    // (every door that's independently poolable gets consumed by the main
    // loop or the pairwise leftover loop before any single door is left
    // over). Sealing door -> partner one-way in that case creates a
    // many-to-one warp: two different doors both leading into partner, but
    // partner's own return only leads back to whichever one it was
    // actually paired with. It's only safe when partner was never fed into
    // a pool to begin with (e.g. a return-spawn-only home surface that's
    // never a walk-in door), so nothing else could have claimed it.
    static bool isSafeVanillaSeal(s32 door) {
        const Conn target = vanillaConnForDoor(door);
        return target.link >= 0
            && !isDoorIndependentlyPoolable(target.link, target.kind);
    }

    // -------------------------------------------------------------
    // Incremental builder
    //
    // The pools below used to be locals inside one buildConnections() call
    // that ran to exhaustion. They are persistent now so the placement engine
    // can take one pairing at a time, look at what it opened, and come back
    // for the next one. Nothing about WHAT is safe to draw changed - both
    // source pools are still provably reachable by construction - only who
    // decides the order.
    // -------------------------------------------------------------

    struct BuilderState {
        // Sub-area items still waiting for a partner. This is the "targets"
        // side: draining it to empty is what wires every sub-area.
        SubareaItem subareas[EntranceDB::sLinkCount * 4 + NonRootKingdom_Count];
        s32 subareaCount;
        // Home doors walkable from a fresh file. Only ever shrinks, except
        // when a non-root gateway is wired and promotes its held-back doors.
        s32 kingdomEntrances[EntranceDB::sLinkCount * 2];
        s32 kingdomCount;
        // Grows as sub-areas are connected: each wired sub hands back its far
        // door, which is reachable precisely because the near side now is.
        s32 chainExits[EntranceDB::sLinkCount * 2];
        s32 chainExitCount;
        s32 pendingHomeDoors[NonRootKingdom_Count][kMaxPendingHomeDoors];
        s32 pendingHomeDoorCount[NonRootKingdom_Count];
        s32 gatewayDoor[NonRootKingdom_Count];
        s32 pool[EntranceDB::sLinkCount];
        s32 randomizableCount;
        s32 biasNum;
        s32 biasDen;
        // Open between beginIncrementalBuild and finalizeIncrementalBuild.
        bool active;
    };

    // Every door paired during the finalize leftover pass, in the order it was
    // paired. Only these are safe to break for a repair: they joined two doors
    // that were ALREADY reachable, so re-pointing one half cannot strand the
    // other. Pairings the fill made are load-bearing and are never touched.
    static s32 sLeftoverPairA[EntranceDB::sLinkCount * DoorSlot_Count];
    static s32 sLeftoverPairB[EntranceDB::sLinkCount * DoorSlot_Count];
    static s32 sLeftoverPairCount = 0;

    static BuilderState sBuild = {};
    // The build RNG has to survive across the phases, because the draws the
    // placement engine makes sit in the middle of the same sequence the pool
    // construction started and the leftover pairing finishes.
    static Randomizer::Rng sBuildRng(0);

    static void resetBuilderState() {
        // The leftover pairs describe the table the LAST build produced. They
        // are indices into a wiring that is about to be thrown away, so they
        // have to die with it - repairUnreachableSubAreas would otherwise break
        // "pairs" that the new table never made.
        sLeftoverPairCount = 0;
        sBuild.subareaCount = 0;
        sBuild.kingdomCount = 0;
        sBuild.chainExitCount = 0;
        sBuild.randomizableCount = 0;
        sBuild.biasNum = 1;
        sBuild.biasDen = 1;
        sBuild.active = false;
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            sBuild.pendingHomeDoorCount[k] = 0;
            sBuild.gatewayDoor[k] = -1;
        }
    }

    // Phase 1: classify every door into the pools, reserve the Darker Side
    // warp and the non-root gateways. Wires nothing else. Returns the number
    // of randomizable links, or 0 when there is nothing to shuffle.
    static s32 buildPools(Randomizer::Rng& rng) {
        resetBuilderState();
        setVanillaConns();

        for (s32 i = 0; i < EntranceDB::sLinkCount; i++)
            if (isRandomizableLink(i))
                sBuild.pool[sBuild.randomizableCount++] = i;
        if (sBuild.randomizableCount == 0)
            return 0;

        logf("Building door pairing with %d randomizable links (chain-fill v5, incremental)", sBuild.randomizableCount);

        // Clear the randomizable doors so leftovers are visible below.
        for (s32 p = 0; p < sBuild.randomizableCount; p++) {
            const s32 i = sBuild.pool[p];
            sPipeConn[i] = { (s16)-1, ConnKind_None };
            sHomeOutConn[i] = { (s16)-1, ConnKind_None };
            sSubNearConn[i] = { (s16)-1, ConnKind_None };
            sSubFarConn[i] = { (s16)-1, ConnKind_None };
        }

        // Sub-area pool: every randomizable link's own sub-side, classified
        // by how its near/far doors relate to each other (a link with no far
        // door is a one-door dead end). Which side ends up as the "entry"
        // matters only for a one-way sub-area: MoonRando::
        // canEntranceSubBeTraversed pins it to whichever side can actually
        // reach the other (same guarantee chain-fill v3 made for its
        // continuations), so a far side that's not reachable from the near
        // side (or vice versa) never gets treated as a walk-through. Home
        // doors that can't unconditionally seed a chain (below) are folded in
        // here too, each as its own one-door dead end, as is one reserved
        // gateway door per non-root kingdom (see the gateway block after the
        // Darker Side warp below) - hence the extra slots on the bound.

        // Kingdom-entrance pool: every home door whose specific node is
        // unconditionally reachable within its own kingdom's route graph
        // under maximal state (MoonRando::
        // isEntranceHomeSideUnconditionallyReachable), so it's walkable from
        // the start no matter how anything else is wired. Being on the
        // kingdom's own main stage isn't sufficient by itself - a "nested"
        // link's home side physically sits inside another link's sub-area
        // (e.g. a second doorway found deep in Deep Woods), and even a door
        // that IS on the main stage can still be gated behind other
        // in-kingdom progression (a one-way drop, a section only opened by
        // walking out of a different sub-area). Doors that don't qualify
        // still need to be wired, so they're added to the sub-area pool
        // above as their own one-door dead ends instead - something else's
        // chain-exit/kingdom-entrance can target them as an arrival, but
        // they can never seed a chain unconditionally themselves. This pool
        // only shrinks: nothing is ever added back to it.
        s32 gatedHomeDoorCount = 0; // diagnostic only

        // Home doors in a kingdom the Odyssey route never unlocks are held
        // here rather than seeding chains. isEntranceHomeSideUnconditionally-
        // Reachable only proves a door is walkable once you are standing in
        // its kingdom, which says nothing about whether that kingdom can be
        // entered in the first place - see the NonRootKingdom notes above.

        for (s32 p = 0; p < sBuild.randomizableCount; p++) {
            const s32 link = sBuild.pool[p];
            const s32 nonRoot = nonRootKingdomForLinkHome(link);
            for (s32 slot = 0; slot < 2; slot++) {
                const bool outSide = slot != 0;
                const bool enterable = outSide ? EntranceDB::isHomeOutEnterable(link)
                                               : EntranceDB::isHomeInEnterable(link);
                if (!enterable)
                    continue;

                const s32 door = link * DoorSlot_Count + (outSide ? DoorSlot_HomeOut : DoorSlot_HomeIn);
                if (!MoonRando::isEntranceHomeSideUnconditionallyReachable(link, outSide)) {
                    sBuild.subareas[sBuild.subareaCount++] = { door, -1 };
                    gatedHomeDoorCount++;
                    continue;
                }
                if (nonRoot >= 0 && sBuild.pendingHomeDoorCount[nonRoot] < kMaxPendingHomeDoors) {
                    sBuild.pendingHomeDoors[nonRoot][sBuild.pendingHomeDoorCount[nonRoot]++] = door;
                    continue;
                }
                if (nonRoot >= 0) {
                    // Bound blown (cannot happen with real data): fall back to
                    // a dead end rather than a root, which is conservative.
                    sBuild.subareas[sBuild.subareaCount++] = { door, -1 };
                    continue;
                }
                sBuild.kingdomEntrances[sBuild.kingdomCount++] = door;
            }
        }
        logf("ER build: %d/%d home doors failed the unconditional-reachability check",
            gatedHomeDoorCount, gatedHomeDoorCount + sBuild.kingdomCount);
        logf("ER build: held back %d Mushroom + %d Dark Side home door(s) as non-root",
            sBuild.pendingHomeDoorCount[NonRootKingdom_Mushroom], sBuild.pendingHomeDoorCount[NonRootKingdom_DarkSide]);

        // Darker Side is a fixed story area internally. Reserve exactly one
        // reachable home-in door as its external one-way entry and keep that
        // door out of the ordinary symmetric pairing pool. kingdomEntrances
        // now holds root-kingdom doors only, so the door this draws is one the
        // player can actually walk to - previously it could land on a Mushroom
        // or Dark Side door and make Darker Side unreachable in turn.
        if (findDarkerSideLink() >= 0) {
            const s32 darkerWarpDoor = takeRandomHomeInDoor(sBuild.kingdomEntrances, &sBuild.kingdomCount, rng);
            if (darkerWarpDoor >= 0)
                setDarkerSideHomeWarp(darkerWarpDoor);
            else
                logf("ER Darker Side home warp: no reachable home-in door");
        }

        // Reserve one gateway door per non-root kingdom. Pushing it into the
        // sub-area pool as a dead end is what guarantees the kingdom becomes
        // enterable: the main loop below pairs every sub-area item with a door
        // drawn from a provably reachable pool, and pairDoors writes both
        // directions, so that partner door leads into this kingdom's overworld.
        // The kingdom's remaining home doors stay parked until the gateway is
        // actually wired (see the promotion step in the main loop).
        // A home-in door is preferred - it is the walk-in surface - but a
        // home-out door works just as well as an arrival if that is all there is.
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            sBuild.gatewayDoor[k] = takeRandomHomeInDoor(sBuild.pendingHomeDoors[k], &sBuild.pendingHomeDoorCount[k], rng);
            if (sBuild.gatewayDoor[k] < 0)
                sBuild.gatewayDoor[k] = takeRandomEntry(sBuild.pendingHomeDoors[k], &sBuild.pendingHomeDoorCount[k], rng);
            if (sBuild.gatewayDoor[k] >= 0) {
                sBuild.subareas[sBuild.subareaCount++] = { sBuild.gatewayDoor[k], -1 };
                logf("ER build: reserved gateway door %d for non-root kingdom %d", sBuild.gatewayDoor[k], k);
            } else {
                logf("ER build: no gateway door available for non-root kingdom %d", k);
            }
        }

        // Chain-exit pool: starts empty, grows as sub-areas are connected
        // (see the main loop). Every door in either pool is provably
        // reachable, and each step below consumes exactly one door total and
        // produces at most one, so their combined size only shrinks or holds
        // steady - unlike a "single current chain" design, which throws a
        // stalled chain's dangling exit straight into unchecked leftover
        // pairing the moment it stops extending. Drawing every connection
        // from these two pools keeps everything in the main loop provably
        // reachable by construction, regardless of how they're weighted
        // against each other.

        // Sub-area pool (continued): the sub-side half described above.
        for (s32 p = 0; p < sBuild.randomizableCount; p++) {
            const s32 link = sBuild.pool[p];
            const s32 nearDoor = link * DoorSlot_Count + DoorSlot_SubNear;
            if (!linkHasFarDoor(link)) {
                sBuild.subareas[sBuild.subareaCount++] = { nearDoor, -1 };
                continue;
            }
            const s32 farDoor = link * DoorSlot_Count + DoorSlot_SubFar;

            // The two Deep Woods drops are entry-only aliases and the vines
            // are exit-only aliases.  Always enter via the primary near side;
            // the vine side alone continues the chain outward.
            if (EntranceDB::isDeepWoodsArea(link)) {
                sBuild.subareas[sBuild.subareaCount++] = { nearDoor, farDoor };
                continue;
            }

            const bool nearToFar = MoonRando::canEntranceSubBeTraversed(link, false);
            const bool farToNear = MoonRando::canEntranceSubBeTraversed(link, true);
            if (nearToFar && farToNear)
                sBuild.subareas[sBuild.subareaCount++] = (rng.nextModulo(2) == 0)
                    ? SubareaItem { nearDoor, farDoor }
                    : SubareaItem { farDoor, nearDoor };
            else if (nearToFar)
                sBuild.subareas[sBuild.subareaCount++] = { nearDoor, farDoor };
            else if (farToNear)
                sBuild.subareas[sBuild.subareaCount++] = { farDoor, nearDoor };
            else {
                // Neither side reaches the other: two independent dead ends.
                sBuild.subareas[sBuild.subareaCount++] = { nearDoor, -1 };
                sBuild.subareas[sBuild.subareaCount++] = { farDoor, -1 };
            }
        }

        chainBiasForOpenness(&sBuild.biasNum, &sBuild.biasDen);

        // Diagnostic only: how the doors split across the two pools before
        // the main loop consumes them. kingdomCount should comfortably
        // outnumber subareaCount's dead-end share for the "ran out" fallback
        // below to stay unreachable in practice.
        logf("ER build: pools before main loop: kingdomEntrances=%d subareas=%d",
            sBuild.kingdomCount, sBuild.subareaCount);

        sBuild.active = true;
        return sBuild.randomizableCount;
    }

    // Wiring a non-root kingdom gateway is the moment that kingdom becomes
    // provably enterable, so its held-back home doors are now ordinary chain
    // seeds and rejoin the kingdom-entrance pool. Shared by the drain loop and
    // by commitWiring, because either one can be the call that wires a gateway.
    static void promoteNonRootHomeDoors(s32 wiredEntryDoor) {
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            if (sBuild.gatewayDoor[k] < 0 || wiredEntryDoor != sBuild.gatewayDoor[k])
                continue;
            for (s32 q = 0; q < sBuild.pendingHomeDoorCount[k]; q++)
                sBuild.kingdomEntrances[sBuild.kingdomCount++] = sBuild.pendingHomeDoors[k][q];
            logf("ER build: non-root kingdom %d reached; promoted %d home door(s)",
                k, sBuild.pendingHomeDoorCount[k]);
            sBuild.pendingHomeDoorCount[k] = 0;
            sBuild.gatewayDoor[k] = -1;
        }
    }

    // Phase 2, one step: draw one source door - weighted between the
    // kingdom-entrance and chain-exit pools - and one sub-area, and connect
    // them. Returns false when nothing is left to pair, which leaves any
    // remaining sub-areas to the leftover pairing in phase 3.
    static bool drainOneSubarea(Randomizer::Rng& rng) {
        // Draw one random door - weighted between the kingdom-entrance and
        // chain-exit pools by biasNum/biasDen - and one random sub-area, and
        // connect them. A sub-area's "other" door (if any) joins the
        // chain-exit pool once it's used, so it can seed further connections
        // just like a kingdom entrance.
        if (!sBuild.active || sBuild.subareaCount <= 0)
            return false;

        const s32 weightedChainExit = sBuild.chainExitCount * sBuild.biasNum;
        const s32 weightedKingdom = sBuild.kingdomCount * sBuild.biasDen;
        const s32 totalWeight = weightedChainExit + weightedKingdom;
        if (totalWeight == 0) {
            // Cannot happen with real data (kingdom entrances comfortably
            // outnumber dead ends), but never leave a sub-area unwired:
            // dump whatever's left into the leftover pairing below.
            logf("ER build: ran out of open entrances with %d sub-area(s) left",
                sBuild.subareaCount);
            return false;
        }

        const bool useChainExit = (s32)rng.nextModulo((u32)totalWeight) < weightedChainExit;
        s32 doorFromPool;
        if (useChainExit) {
            const s32 idx = (s32)rng.nextModulo((u32)sBuild.chainExitCount);
            doorFromPool = sBuild.chainExits[idx];
            sBuild.chainExits[idx] = sBuild.chainExits[--sBuild.chainExitCount];
        } else {
            const s32 idx = (s32)rng.nextModulo((u32)sBuild.kingdomCount);
            doorFromPool = sBuild.kingdomEntrances[idx];
            sBuild.kingdomEntrances[idx] = sBuild.kingdomEntrances[--sBuild.kingdomCount];
        }

        const s32 subIndex = (s32)rng.nextModulo((u32)sBuild.subareaCount);
        const SubareaItem sub = sBuild.subareas[subIndex];
        sBuild.subareas[subIndex] = sBuild.subareas[--sBuild.subareaCount];

        pairDoors(doorFromPool, sub.entryDoor);
        if (sub.otherDoor >= 0)
            sBuild.chainExits[sBuild.chainExitCount++] = sub.otherDoor;

        promoteNonRootHomeDoors(sub.entryDoor);
        return true;
    }

    // Phase 3: pair everything the fill did not get to.
    static void wireLeftovers(Randomizer::Rng& rng) {
        // Leftover cleanup: whatever's left in either pool (unused kingdom
        // entrances and chain exits nobody happened to draw before the
        // sub-area pool ran dry) is, by construction, already provably
        // reachable, so pairing it randomly - including direct
        // kingdom-to-kingdom doors - can't strand anything. Anything dumped
        // by the "ran out" fallback above has no such guarantee, but that
        // path is not expected to run.
        s32 open[EntranceDB::sLinkCount * DoorSlot_Count];
        s32 leftoverCount = 0;
        for (s32 p = 0; p < sBuild.kingdomCount; p++)
            open[leftoverCount++] = sBuild.kingdomEntrances[p];
        for (s32 p = 0; p < sBuild.chainExitCount; p++)
            open[leftoverCount++] = sBuild.chainExits[p];
        for (s32 p = 0; p < sBuild.subareaCount; p++) {
            open[leftoverCount++] = sBuild.subareas[p].entryDoor;
            if (sBuild.subareas[p].otherDoor >= 0)
                open[leftoverCount++] = sBuild.subareas[p].otherDoor;
        }
        // Held-back home doors whose gateway was never drawn. The main loop
        // runs until the sub-area pool is empty and every gateway lives in
        // that pool, so this only has anything in it if the loop hit its
        // "ran out of open entrances" break - but an unwired door would be a
        // hard defect, so never leave one behind.
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            for (s32 q = 0; q < sBuild.pendingHomeDoorCount[k]; q++)
                open[leftoverCount++] = sBuild.pendingHomeDoors[k][q];
            if (sBuild.pendingHomeDoorCount[k] > 0)
                logf("ER build: %d unpromoted home door(s) for non-root kingdom %d",
                    sBuild.pendingHomeDoorCount[k], k);
            sBuild.pendingHomeDoorCount[k] = 0;
        }
        shuffleValues(open, leftoverCount, rng);
        if (leftoverCount & 1) {
            // Odd door out: find one whose vanilla partner is never
            // independently pooled (see isSafeVanillaSeal), so sealing it
            // below can't collide with a partner that's already been
            // reciprocally paired with someone else. Search instead of
            // just taking whatever the shuffle happened to leave last.
            s32 safeIdx = -1;
            for (s32 p = 0; p < leftoverCount; p++) {
                if (isSafeVanillaSeal(open[p])) {
                    safeIdx = p;
                    break;
                }
            }
            if (safeIdx < 0) {
                // Every leftover door's vanilla partner is itself
                // independently poolable, so it's almost certainly already
                // spoken for. Not expected to happen in practice - fall
                // back to the last shuffled door rather than leaving
                // anything unset.
                safeIdx = leftoverCount - 1;
                logf("ER build: no safe odd-door candidate among %d leftovers", leftoverCount);
            }

            // Pull the chosen door out of the pairing pool entirely (swap
            // with the last slot, shrink the count) before the pairwise
            // loop below runs, so it's excluded rather than double-paired.
            const s32 door = open[safeIdx];
            open[safeIdx] = open[leftoverCount - 1];
            leftoverCount--;

            setDoorOutward(door, vanillaConnForDoor(door));
            logf("ER build: odd door %d sealed to vanilla", door);
        }
        sLeftoverPairCount = 0;
        for (s32 p = 0; p + 1 < leftoverCount; p += 2) {
            pairDoors(open[p], open[p + 1]);
            // Remember it: these are the only pairings a later repair may break,
            // because both halves were already reachable when they were joined.
            if (sLeftoverPairCount < (s32)(sizeof(sLeftoverPairA) / sizeof(sLeftoverPairA[0]))) {
                sLeftoverPairA[sLeftoverPairCount] = open[p];
                sLeftoverPairB[sLeftoverPairCount] = open[p + 1];
                sLeftoverPairCount++;
            }
        }

        s32 logged = 0;
        for (s32 p = 0; p < sBuild.randomizableCount && logged < 5; p++) {
            const s32 i = sBuild.pool[p];
            logf("Pipe %d -> link %d kind %d", i, (s32)sPipeConn[i].link, (s32)sPipeConn[i].kind);
            logged++;
        }

        collapseSpecialSourceConns();
    }

    // The one-shot path (Moon Rando off): run all three phases back to back.
    // This is exactly what buildConnections always did and produces the same
    // table for a given seed/attempt.
    static s32 buildConnections(Randomizer::Rng& rng) {
        const s32 randomizableCount = buildPools(rng);
        if (randomizableCount == 0)
            return 0;
        while (drainOneSubarea(rng)) { }
        wireLeftovers(rng);
        sBuild.active = false;
        return randomizableCount;
    }

    static s32 buildDecoupledConnections(Randomizer::Rng& rng) {
        setVanillaConns();

        s32 pool[EntranceDB::sLinkCount];
        s32 randomizableCount = 0;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++)
            if (isRandomizableLink(i))
                pool[randomizableCount++] = i;
        if (randomizableCount == 0)
            return 0;

        logf("Building decoupled entrance map with %d randomizable links", randomizableCount);

        for (s32 p = 0; p < randomizableCount; p++) {
            const s32 i = pool[p];
            sPipeConn[i] = { (s16)-1, ConnKind_None };
            sHomeOutConn[i] = { (s16)-1, ConnKind_None };
            sSubNearConn[i] = { (s16)-1, ConnKind_None };
            sSubFarConn[i] = { (s16)-1, ConnKind_None };
        }

        s32 homeSourceDoors[EntranceDB::sLinkCount * 2];
        s32 homeArrivalDoors[EntranceDB::sLinkCount * 2];
        s32 subArrivalDoors[EntranceDB::sLinkCount * 2];
        s32 subSourceDoors[EntranceDB::sLinkCount * 2];
        s32 homeSourceDoorCount = 0;
        s32 homeArrivalDoorCount = 0;
        s32 subArrivalDoorCount = 0;
        s32 subSourceDoorCount = 0;
        for (s32 p = 0; p < randomizableCount; p++) {
            const s32 link = pool[p];
            if (EntranceDB::isHomeInEnterable(link)) {
                homeArrivalDoors[homeArrivalDoorCount++] = link * DoorSlot_Count + DoorSlot_HomeIn;
                homeSourceDoors[homeSourceDoorCount++] = link * DoorSlot_Count + DoorSlot_HomeIn;
            }
            if (EntranceDB::isHomeOutEnterable(link)) {
                homeArrivalDoors[homeArrivalDoorCount++] = link * DoorSlot_Count + DoorSlot_HomeOut;
                homeSourceDoors[homeSourceDoorCount++] = link * DoorSlot_Count + DoorSlot_HomeOut;
            }

            const s32 nearDoor = link * DoorSlot_Count + DoorSlot_SubNear;
            subArrivalDoors[subArrivalDoorCount++] = nearDoor;
            if (EntranceDB::isDeepWoodsArea(link)) {
                // Drops are valid arrivals only; vines are valid sources only.
                if (linkHasFarDoor(link))
                    subSourceDoors[subSourceDoorCount++] = link * DoorSlot_Count + DoorSlot_SubFar;
            } else {
                subSourceDoors[subSourceDoorCount++] = nearDoor;
                if (linkHasFarDoor(link)) {
                    const s32 farDoor = link * DoorSlot_Count + DoorSlot_SubFar;
                    subArrivalDoors[subArrivalDoorCount++] = farDoor;
                    subSourceDoors[subSourceDoorCount++] = farDoor;
                }
            }
        }

        if (findDarkerSideLink() >= 0) {
            // Root-reachable only: a warp hanging off a Mushroom or Dark Side
            // door would make Darker Side depend on reaching those first.
            s32 darkerWarpDoor
                = takeRandomRootReachableHomeDoor(homeSourceDoors, &homeSourceDoorCount, rng, true);
            if (darkerWarpDoor < 0)
                darkerWarpDoor = takeRandomHomeInDoor(homeSourceDoors, &homeSourceDoorCount, rng);
            if (darkerWarpDoor >= 0)
                setDarkerSideHomeWarp(darkerWarpDoor);
            else
                logf("ER Darker Side home warp: no home-in source door");
        }

        // Decoupled mode wires sources and arrivals from independent shuffles,
        // so nothing is reachable by construction the way the coupled builder
        // guarantees. Force a two-hop route into each non-root kingdom before
        // the general shuffle runs: a root-kingdom home door leads into some
        // sub-area's near surface, and that same surface - used as a source -
        // surfaces in the non-root kingdom's overworld. Walking straight back
        // out of the surface you arrived on needs no traversal at all, so the
        // route holds regardless of what that sub-area's interior demands.
        for (s32 k = 0; k < NonRootKingdom_Count; k++) {
            const s32 relayDoor = takeRandomRelayNearDoor(
                subArrivalDoors, &subArrivalDoorCount, subSourceDoors, subSourceDoorCount, rng);
            if (relayDoor < 0) {
                logf("ER decoupled: no relay sub-area available for non-root kingdom %d", k);
                continue;
            }
            const s32 sourceDoor = takeRandomRootReachableHomeDoor(
                homeSourceDoors, &homeSourceDoorCount, rng);
            const s32 arrivalDoor = takeRandomHomeDoorInNonRootKingdom(
                homeArrivalDoors, &homeArrivalDoorCount, rng, k);
            if (sourceDoor < 0 || arrivalDoor < 0) {
                logf("ER decoupled: could not force a route into non-root kingdom %d", k);
                // Put the relay back rather than dropping it from the shuffle.
                subArrivalDoors[subArrivalDoorCount++] = relayDoor;
                if (sourceDoor >= 0)
                    homeSourceDoors[homeSourceDoorCount++] = sourceDoor;
                if (arrivalDoor >= 0)
                    homeArrivalDoors[homeArrivalDoorCount++] = arrivalDoor;
                continue;
            }

            removeDoorValue(subSourceDoors, &subSourceDoorCount, relayDoor);
            setDoorOutward(sourceDoor, connToDoor(relayDoor));
            setDoorOutward(relayDoor, connToDoor(arrivalDoor));
            logf("ER decoupled: forced route %d -> %d -> %d into non-root kingdom %d",
                sourceDoor, relayDoor, arrivalDoor, k);
        }

        shuffleValues(homeSourceDoors, homeSourceDoorCount, rng);
        shuffleValues(subArrivalDoors, subArrivalDoorCount, rng);

        for (s32 p = 0; p < homeSourceDoorCount && p < subArrivalDoorCount; p++)
            setDoorOutward(homeSourceDoors[p], connToDoor(subArrivalDoors[p]));

        shuffleValues(subSourceDoors, subSourceDoorCount, rng);
        shuffleValues(homeArrivalDoors, homeArrivalDoorCount, rng);
        for (s32 p = 0; p < subSourceDoorCount && p < homeArrivalDoorCount; p++)
            setDoorOutward(subSourceDoors[p], connToDoor(homeArrivalDoors[p]));

        s32 logged = 0;
        for (s32 p = 0; p < randomizableCount && logged < 5; p++) {
            const s32 i = pool[p];
            logf("Decoupled pipe %d -> link %d kind %d", i, (s32)sPipeConn[i].link, (s32)sPipeConn[i].kind);
            logged++;
        }

        collapseSpecialSourceConns();
        return randomizableCount;
    }

    // ─────────────────────────────────────────────────────────────
    // Limited builder
    //
    // Swap whole loading zones without ever chaining them. Each overworld
    // link keeps its pipes where they are but is wired to a *different* link's
    // sub-area; both sides of that sub-area route straight back to the
    // overworld link that owns it. So a door entered in one kingdom always
    // surfaces again in that same kingdom — never another sub-area, and never
    // a different kingdom's overworld. With the identity permutation this is
    // exactly vanilla. The pairing stays symmetric (two-way), so backtracking
    // works and MoonRando treats it like coupled "On" reachability.
    //
    // Door shapes differ across links, so the permutation is built in two
    // passes: every home that owns a real exit-side door (homeEntranceOut) is
    // matched to a sub that owns a real far door, so the secondary pair is
    // clean (such subs always outnumber such homes). The rest fall back to
    // single-door (near only) or exit-only far-door coupling, mirroring the
    // vanilla wiring.
    // ─────────────────────────────────────────────────────────────

    // In Limited mode, a few loading zones are pinned to their vanilla wiring
    // instead of being swapped. Wooded Kingdom <-> Deep Woods
    // (ForestWorldHomeStage <-> ForestWorldWoodsStage, the Jyukai woods doors)
    // keeps its vanilla bundle here, then the Deep Woods source collapse below
    // makes the two falls and all vines share the same player-facing mappings.
    static bool isLimitedVanillaLink(s32 index) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(index);
        if (!link)
            return false;
        return std::strcmp(link->homeStage, "ForestWorldHomeStage") == 0
            && std::strcmp(link->subStage, "ForestWorldWoodsStage") == 0;
    }

    static void wireLimitedPair(s32 homeLink, s32 subLink) {
        // Primary: overworld entry pipe <-> sub near side (two-way).
        pairDoors(homeLink * DoorSlot_Count + DoorSlot_HomeIn,
            subLink * DoorSlot_Count + DoorSlot_SubNear);

        const bool homeHasOut = linkHasHomeOutSurface(homeLink);
        const bool homeOutEnterable = EntranceDB::isHomeOutEnterable(homeLink);
        const bool subHasFar = linkHasFarDoor(subLink);

        if (homeOutEnterable && subHasFar) {
            // Secondary: overworld exit door <-> sub far door (two-way).
            pairDoors(homeLink * DoorSlot_Count + DoorSlot_HomeOut,
                subLink * DoorSlot_Count + DoorSlot_SubFar);
        } else if (homeHasOut && subHasFar) {
            // Exit-only home surfaces (Door the sub can't pair back to, or a
            // ReturnSpawn landing spot) are valid arrivals, but not route
            // sources.
            setDoorOutward(subLink * DoorSlot_Count + DoorSlot_SubFar,
                Conn { (s16)homeLink, ConnKind_HomeOut });
        } else if (!homeHasOut && subHasFar) {
            // The home has no distinct exit surface at all (SameSide), so the
            // sub's far door is exit-only and surfaces at the home's entry
            // pipe, keeping the player in the same kingdom.
            setDoorOutward(subLink * DoorSlot_Count + DoorSlot_SubFar,
                Conn { (s16)homeLink, ConnKind_HomeIn });
        } else if (homeOutEnterable && !subHasFar) {
            // The home owns an exit door but the assigned sub has only one door.
            // The pre-pass avoids this, but defensively keep the exit door inside
            // the same loading zone: it drops into the sub's near side, whose
            // return still leads back to this home's entry pipe.
            setDoorOutward(homeLink * DoorSlot_Count + DoorSlot_HomeOut,
                Conn { (s16)subLink, ConnKind_SubNear });
        }
    }

    static s32 buildLimitedConnections(Randomizer::Rng& rng) {
        setVanillaConns();

        s32 pool[EntranceDB::sLinkCount];
        s32 randomizableCount = 0;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++)
            if (isRandomizableLink(i) && !isLimitedVanillaLink(i))
                pool[randomizableCount++] = i;
        if (randomizableCount == 0)
            return 0;

        logf("Building limited entrance swap with %d randomizable links", randomizableCount);

        for (s32 p = 0; p < randomizableCount; p++) {
            const s32 i = pool[p];
            sPipeConn[i] = { (s16)-1, ConnKind_None };
            sHomeOutConn[i] = { (s16)-1, ConnKind_None };
            sSubNearConn[i] = { (s16)-1, ConnKind_None };
            sSubFarConn[i] = { (s16)-1, ConnKind_None };
        }

        // Split home interfaces by whether they own a real exit-side door, and
        // sub interfaces by whether they own a real far door. A home with an
        // exit door must receive a sub with a far door so the secondary pair is
        // clean; those subs always outnumber those homes, so the match below
        // never starves.
        s32 homesWithOut[EntranceDB::sLinkCount];
        s32 homesNoOut[EntranceDB::sLinkCount];
        s32 subsWithFar[EntranceDB::sLinkCount];
        s32 subsNoFar[EntranceDB::sLinkCount];
        bool participates[EntranceDB::sLinkCount] = {};
        s32 hOut = 0, hNo = 0, sFar = 0, sNo = 0;
        for (s32 p = 0; p < randomizableCount; p++) {
            const s32 i = pool[p];
            // The pairing is keyed on the home side, so a row with no enterable
            // home entrance cannot take part at all. Its doors were cleared
            // above, so they are put back below rather than left dangling.
            if (!EntranceDB::isHomeInEnterable(i))
                continue;
            participates[i] = true;
            if (EntranceDB::isHomeOutEnterable(i))
                homesWithOut[hOut++] = i;
            else
                homesNoOut[hNo++] = i;
            if (linkHasFarDoor(i))
                subsWithFar[sFar++] = i;
            else
                subsNoFar[sNo++] = i;
        }

        for (s32 p = 0; p < randomizableCount; p++) {
            const s32 i = pool[p];
            if (participates[i])
                continue;
            sPipeConn[i] = vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeIn);
            sHomeOutConn[i] = linkHasHomeOutDoor(i)
                ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeOut)
                : Conn { (s16)-1, ConnKind_None };
            sSubNearConn[i] = vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubNear);
            sSubFarConn[i] = linkHasFarDoor(i)
                ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubFar)
                : Conn { (s16)-1, ConnKind_None };
            logf("Limited: link %d has no enterable home side; kept vanilla", i);
        }

        if (findDarkerSideLink() >= 0) {
            s32 darkerWarpHome = takeRandomEntry(homesNoOut, &hNo, rng);
            if (darkerWarpHome < 0) {
                // Keep the feature available even if a future data set has no
                // home-without-out link. The unused secondary side remains
                // explicitly vanilla in that fallback case.
                darkerWarpHome = takeRandomEntry(homesWithOut, &hOut, rng);
                if (darkerWarpHome >= 0)
                    setDoorOutward(darkerWarpHome * DoorSlot_Count + DoorSlot_HomeOut,
                        vanillaConnForDoor(darkerWarpHome * DoorSlot_Count + DoorSlot_HomeOut));
            }
            if (darkerWarpHome >= 0)
                setDarkerSideHomeWarp(darkerWarpHome * DoorSlot_Count + DoorSlot_HomeIn);
            else
                logf("ER Darker Side home warp: no home-in source link");
        }

        shuffleValues(homesWithOut, hOut, rng);
        shuffleValues(homesNoOut, hNo, rng);
        shuffleValues(subsWithFar, sFar, rng);
        shuffleValues(subsNoFar, sNo, rng);

        // Pass 1: pair every home-with-out to a sub-with-far (clean two-way
        // secondary). Pass 2: pool the leftover subs and hand them to the
        // homes-without-out, regardless of whether each leftover still carries a
        // far door (those far doors become exit-only return spawns).
        s32 farUsed = 0;
        for (s32 p = 0; p < hOut; p++) {
            const s32 sub = (farUsed < sFar) ? subsWithFar[farUsed++] : homesWithOut[p];
            wireLimitedPair(homesWithOut[p], sub);
        }

        s32 subRest[EntranceDB::sLinkCount];
        s32 subRestCount = 0;
        for (; farUsed < sFar; farUsed++)
            subRest[subRestCount++] = subsWithFar[farUsed];
        for (s32 p = 0; p < sNo; p++)
            subRest[subRestCount++] = subsNoFar[p];
        shuffleValues(subRest, subRestCount, rng);

        for (s32 p = 0; p < hNo && p < subRestCount; p++)
            wireLimitedPair(homesNoOut[p], subRest[p]);

        s32 logged = 0;
        for (s32 p = 0; p < randomizableCount && logged < 5; p++) {
            const s32 i = pool[p];
            logf("Limited pipe %d -> link %d kind %d", i, (s32)sPipeConn[i].link, (s32)sPipeConn[i].kind);
            logged++;
        }

        collapseSpecialSourceConns();

        // The far-end pipe of the Jaxi ride is the one row the pairing above
        // has to skip, and plain vanilla is not good enough for it here: the
        // ride itself is wired to some other kingdom's overworld link, so
        // surfacing at Sand Kingdom's own pipe would be the one thing Limited
        // promises never happens. Send it wherever the ride's other interior
        // pipe now goes - vanilla already sends both to the same place.
        const s32 jaxiPrimary = findJaxiRidePrimaryLink();
        const s32 jaxiFarPipe = findJaxiRideFarPipeLink();
        if (jaxiPrimary >= 0 && jaxiFarPipe >= 0
            && isRandomizableLink(jaxiFarPipe) && !isLimitedVanillaLink(jaxiFarPipe)) {
            const Conn rideExit = linkHasFarDoor(jaxiPrimary) ? sSubFarConn[jaxiPrimary]
                                                              : sSubNearConn[jaxiPrimary];
            if (rideExit.kind != ConnKind_None) {
                sSubNearConn[jaxiFarPipe] = rideExit;
                if (linkHasFarDoor(jaxiFarPipe))
                    sSubFarConn[jaxiFarPipe] = rideExit;
                logf("Limited: Jaxi far pipe (link %d) follows link %d -> link %d kind %d",
                    jaxiFarPipe, jaxiPrimary, (s32)rideExit.link, (s32)rideExit.kind);
            }
        }

        return randomizableCount;
    }

    // ─────────────────────────────────────────────────────────────
    // Shop swap
    //
    // Runs after the main builder, over doors it was told to leave alone.
    // Every shop is one home-in door and one sub-near door, so this is a
    // straight permutation: entering shop A's door lands you in shop pi(A),
    // and walking back out returns you to A's door. Both sides are written,
    // so the shops stay a closed set of two-way dead ends exactly as they are
    // in vanilla - nothing else's reachability moves.
    // ─────────────────────────────────────────────────────────────

    static void wireShopSwap(Randomizer::Rng& rng) {
        s32 owners[EntranceDB::sLinkCount];
        s32 ownerCount = 0;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++)
            if (isShopLink(i) && shopSlotOwner(i) == i)
                owners[ownerCount++] = i;
        if (ownerCount < 2) {
            logf("ER shops: %d shop door(s); nothing to swap", ownerCount);
            return;
        }

        s32 targets[EntranceDB::sLinkCount];
        for (s32 p = 0; p < ownerCount; p++)
            targets[p] = owners[p];
        shuffleValues(targets, ownerCount, rng);

        for (s32 p = 0; p < ownerCount; p++)
            pairDoors(owners[p] * DoorSlot_Count + DoorSlot_HomeIn,
                targets[p] * DoorSlot_Count + DoorSlot_SubNear);

        // Metro's second street door is an alias of the first on both sides.
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isShopLink(i))
                continue;
            const s32 owner = shopSlotOwner(i);
            if (owner == i)
                continue;
            sPipeConn[i] = sPipeConn[owner];
            sSubNearConn[i] = sSubNearConn[owner];
            logf("ER shops: link %d follows shop slot owner %d", i, owner);
        }

        logf("ER shops: swapped %d shop door(s)", ownerCount);
    }

    // ─────────────────────────────────────────────────────────────
    // Build / lifecycle
    // ─────────────────────────────────────────────────────────────

    void buildRemapTable(u32 seed, u32 attempt) {
        // Traversability depends on the logic difficulty, so the pairing must
        // be rebuilt when it changes.
        const s32 difficulty = (s32)Randomizer::getLogicDifficulty();
        const Randomizer::EntranceRandoMode mode = Randomizer::getEntranceRandoMode();
        const Randomizer::ShopLocationMode shopMode = Randomizer::getShopLocationMode();
        const s32 openness = opennessLevel();
        if (sInitialized && seed == sLastSeed && attempt == sLastAttempt && difficulty == sLastDifficulty
            && mode == sLastMode && shopMode == sLastShopMode && openness == sLastOpenness)
            return;

        // Narrate a shuffle only the first time this (seed, attempt) is built;
        // see sSuppressBuildLog and Randomizer::shouldNarrateBuild. Without this
        // the full ~20-line build narration landed in the log twice per stage
        // load, for every stage load.
        static const char sEntranceBuildLogTag = 0;
        sSuppressBuildLog = !Randomizer::shouldNarrateBuild(&sEntranceBuildLogTag, seed, attempt);

        const u32 attemptSeed = seed ^ (attempt * 0x9E3779B9u);
        Randomizer::Rng rng = Randomizer::makeRng(attemptSeed, Randomizer::RngStream::Entrance);
        s32 randomizableCount;
        // Every mode starts from a clean builder. buildConnections gets this
        // through buildPools, but the Limited and Decoupled builders never
        // touch BuilderState, so without this they would inherit sBuild.active
        // and the leftover pairs of whatever ran before them.
        resetBuilderState();
        if (mode == Randomizer::EntranceRandoMode_Decoupled)
            randomizableCount = buildDecoupledConnections(rng);
        else if (mode == Randomizer::EntranceRandoMode_Limited)
            randomizableCount = buildLimitedConnections(rng);
        else
            // The retired SmallChains/LongChains modes routed here too. Chain
            // length is Seed Openness now, so every coupled seed is this one
            // builder, and they differ only in the chainBiasForOpenness weights.
            randomizableCount = buildConnections(rng);

        // Shop doors sat out the builder above under Swap and Vanilla, so they
        // still carry the vanilla wiring setVanillaConns gave them. Vanilla is
        // done at that point; Swap permutes them among themselves here.
        if (shopMode == Randomizer::ShopLocationMode_Swap)
            wireShopSwap(rng);

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastDifficulty = difficulty;
        sLastMode = mode;
        sLastShopMode = shopMode;
        sLastOpenness = openness;
        sInitialized = true;

        logf("ER built: seed=%u attempt=%u mode=%d shops=%d openness=%d links=%d", seed, attempt,
            (s32)mode, (s32)shopMode, openness, randomizableCount);

        // MUST clear before returning: logf is shared with the runtime entrance
        // diagnostics (door routing, fallbacks, death checkpoints), and leaving
        // this set would silence all of them for the rest of the session.
        // buildRemapTable has no early return past the point it is set.
        sSuppressBuildLog = false;
    }

    // -------------------------------------------------------------
    // Public incremental API
    //
    // Same three phases as buildConnections above, but the placement engine
    // owns the middle one. See EntranceRando.h for the call sequence.
    // -------------------------------------------------------------

    void beginIncrementalBuild(u32 seed, u32 attempt) {
        const s32 difficulty = (s32)Randomizer::getLogicDifficulty();
        const Randomizer::EntranceRandoMode mode = Randomizer::getEntranceRandoMode();
        const Randomizer::ShopLocationMode shopMode = Randomizer::getShopLocationMode();

        static const char sEntranceBuildLogTag = 0;
        sSuppressBuildLog = !Randomizer::shouldNarrateBuild(&sEntranceBuildLogTag, seed, attempt);

        const u32 attemptSeed = seed ^ (attempt * 0x9E3779B9u);
        sBuildRng = Randomizer::makeRng(attemptSeed, Randomizer::RngStream::Entrance);

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastDifficulty = difficulty;
        sLastMode = mode;
        sLastShopMode = shopMode;
        sLastOpenness = opennessLevel();

        // Limited and Decoupled have no incremental form: Limited is a
        // permutation of whole loading zones and Decoupled is two independent
        // permutations, so neither has a "one more door" step for the fill to
        // take. Build them outright and hand the fill a finished table, which
        // it then simply never wires anything into.
        if (mode == Randomizer::EntranceRandoMode_Decoupled
            || mode == Randomizer::EntranceRandoMode_Limited) {
            resetBuilderState();
            sBuild.randomizableCount = mode == Randomizer::EntranceRandoMode_Decoupled
                ? buildDecoupledConnections(sBuildRng)
                : buildLimitedConnections(sBuildRng);
            if (shopMode == Randomizer::ShopLocationMode_Swap)
                wireShopSwap(sBuildRng);
            sInitialized = true;
            logf("ER incremental begin (one-shot mode %d): links=%d", (s32)mode,
                sBuild.randomizableCount);
            sSuppressBuildLog = false;
            return;
        }

        const s32 randomizableCount = buildPools(sBuildRng);
        // The table is PARTIAL from here until finalizeIncrementalBuild:
        // unwired doors carry ConnKind_None, and the reachability model reads
        // that as "leads nowhere", which is exactly what lets the fill reason
        // about a door before deciding where it goes.
        sInitialized = true;
        logf("ER incremental begin: seed=%u attempt=%u mode=%d links=%d subareas=%d",
            seed, attempt, (s32)mode, randomizableCount, sBuild.subareaCount);
        sSuppressBuildLog = false;
    }

    bool isIncrementalBuildActive() {
        return sBuild.active;
    }

    s32 getUnwiredTargetCount() {
        return sBuild.active ? sBuild.subareaCount : 0;
    }

    s32 listUnwiredSources(s32* out, u8* outIsChainExit, s32 maxOut) {
        if (!sBuild.active)
            return 0;
        s32 count = 0;
        for (s32 i = 0; i < sBuild.kingdomCount; i++) {
            if (count < maxOut) {
                out[count] = sBuild.kingdomEntrances[i];
                if (outIsChainExit)
                    outIsChainExit[count] = 0;
            }
            count++;
        }
        for (s32 i = 0; i < sBuild.chainExitCount; i++) {
            if (count < maxOut) {
                out[count] = sBuild.chainExits[i];
                if (outIsChainExit)
                    outIsChainExit[count] = 1;
            }
            count++;
        }
        return count;
    }

    void getChainBias(s32* outChainExitWeight, s32* outKingdomWeight) {
        chainBiasForOpenness(outChainExitWeight, outKingdomWeight);
    }

    s32 listUnwiredTargets(s32 sourceDoor, s32* out, s32 maxOut) {
        if (!sBuild.active)
            return 0;
        const s32 sourceLink = sourceDoor / DoorSlot_Count;
        const bool limited = sLastMode == Randomizer::EntranceRandoMode_Limited;
        s32 count = 0;
        for (s32 i = 0; i < sBuild.subareaCount; i++) {
            const s32 entry = sBuild.subareas[i].entryDoor;
            if (entry == sourceDoor)
                continue;
            if (limited) {
                // Limited keeps a door and its destination inside one kingdom.
                const EntranceDB::EntranceLink* a = EntranceDB::entryAt(sourceLink);
                const EntranceDB::EntranceLink* b
                    = EntranceDB::entryAt(entry / DoorSlot_Count);
                if (!a || !b || !a->homeStage || !b->homeStage
                    || std::strcmp(a->homeStage, b->homeStage) != 0)
                    continue;
            }
            if (count < maxOut)
                out[count] = entry;
            count++;
        }
        return count;
    }

    DoorClass classifyTarget(s32 targetDoor) {
        const s32 link = targetDoor / DoorSlot_Count;
        const s32 slot = targetDoor % DoorSlot_Count;
        if (slot == DoorSlot_HomeIn || slot == DoorSlot_HomeOut)
            return DoorClass_CrossKingdom;

        // A sub side: whether it continues the chain is decided by the pool
        // item, because that is where the traversability answer already lives.
        if (sBuild.active) {
            for (s32 i = 0; i < sBuild.subareaCount; i++) {
                if (sBuild.subareas[i].entryDoor != targetDoor)
                    continue;
                if (sBuild.subareas[i].otherDoor >= 0)
                    return DoorClass_PassThrough;
                // No onward door. A sub holding nothing at all is a plain dead
                // end; one the player can actually spend time in is a pocket,
                // which is a different kind of closed.
                return MoonRando::linkSubAreaHasLocations(link) ? DoorClass_Pocket
                                                                : DoorClass_DeadEnd;
            }
        }
        return linkHasFarDoor(link) ? DoorClass_PassThrough : DoorClass_DeadEnd;
    }

    Conn connForDoor(s32 door) {
        return connToDoor(door);
    }

    bool commitWiring(s32 sourceDoor, s32 targetDoor) {
        if (!sBuild.active || sourceDoor < 0 || targetDoor < 0 || sourceDoor == targetDoor)
            return false;

        // The source must come out of one of the two provably reachable pools,
        // and the target must still be an unpaired sub-area item. Anything else
        // would break the by-construction reachability the pools guarantee.
        s32 subIndex = -1;
        for (s32 i = 0; i < sBuild.subareaCount; i++) {
            if (sBuild.subareas[i].entryDoor == targetDoor) {
                subIndex = i;
                break;
            }
        }
        if (subIndex < 0)
            return false;

        bool tookSource
            = removeDoorValue(sBuild.kingdomEntrances, &sBuild.kingdomCount, sourceDoor);
        if (!tookSource)
            tookSource = removeDoorValue(sBuild.chainExits, &sBuild.chainExitCount, sourceDoor);
        if (!tookSource)
            return false;

        const SubareaItem sub = sBuild.subareas[subIndex];
        sBuild.subareas[subIndex] = sBuild.subareas[--sBuild.subareaCount];

        pairDoors(sourceDoor, sub.entryDoor);
        if (sub.otherDoor >= 0)
            sBuild.chainExits[sBuild.chainExitCount++] = sub.otherDoor;

        promoteNonRootHomeDoors(sub.entryDoor);
        return true;
    }

    void finalizeIncrementalBuild() {
        if (!sBuild.active)
            return;

        static const char sEntranceFinalizeLogTag = 0;
        sSuppressBuildLog
            = !Randomizer::shouldNarrateBuild(&sEntranceFinalizeLogTag, sLastSeed, sLastAttempt);

        // Anything the fill left untouched still has to be wired, and the
        // leftover pairing is exactly the tail the one-shot builder always ran.
        while (drainOneSubarea(sBuildRng)) { }
        wireLeftovers(sBuildRng);
        sBuild.active = false;

        if (sLastShopMode == Randomizer::ShopLocationMode_Swap)
            wireShopSwap(sBuildRng);

        sInitialized = true;
        logf("ER incremental finalize: seed=%u attempt=%u links=%d", sLastSeed, sLastAttempt,
            sBuild.randomizableCount);
        sSuppressBuildLog = false;
    }

    s32 repairUnreachableSubAreas(const s16* links, s32 linkCount) {
        if (!links || linkCount <= 0 || sLeftoverPairCount <= 0)
            return 0;

        s32 repaired = 0;
        for (s32 i = 0; i < linkCount; i++) {
            const s32 link = links[i];
            if (link < 0 || link >= EntranceDB::sLinkCount || !isRandomizableLink(link))
                continue;

            // The orphan we want reachable is this sub-area near side.
            const s32 orphan = makeDoor(link, DoorSlot_SubNear);

            // Find a leftover pair we can break. Skip any pair that involves
            // the orphan itself, and any whose halves are both sub sides -
            // re-pointing one of those just moves the problem.
            s32 pairIndex = -1;
            for (s32 p = 0; p < sLeftoverPairCount; p++) {
                const s32 a = sLeftoverPairA[p];
                const s32 b = sLeftoverPairB[p];
                if (a == orphan || b == orphan)
                    continue;
                const DoorSlot slotA = doorSlot(a);
                const DoorSlot slotB = doorSlot(b);
                const bool aIsHome = slotA == DoorSlot_HomeIn || slotA == DoorSlot_HomeOut;
                const bool bIsHome = slotB == DoorSlot_HomeIn || slotB == DoorSlot_HomeOut;
                if (!aIsHome && !bIsHome)
                    continue;
                pairIndex = p;
                break;
            }
            if (pairIndex < 0)
                break;

            // Break it: the home half takes the orphan, and the half it left
            // behind takes the orphan partner it displaced. Both directions are
            // written by pairDoors, so nothing is left one-way.
            s32 keep = sLeftoverPairA[pairIndex];
            s32 displaced = sLeftoverPairB[pairIndex];
            const DoorSlot keepSlot = doorSlot(keep);
            if (keepSlot != DoorSlot_HomeIn && keepSlot != DoorSlot_HomeOut) {
                const s32 swap = keep;
                keep = displaced;
                displaced = swap;
            }

            const Conn orphanPartner = sSubNearConn[link];
            if (orphanPartner.kind == ConnKind_None || orphanPartner.link < 0)
                continue;
            const s32 orphanPartnerDoor
                = makeDoor(orphanPartner.link, connKindToSlot(orphanPartner.kind));
            // The swap only works on four DISTINCT doors. If the orphan was
            // already pointing at one half of this pair, re-pairing would leave
            // a one-way connection behind.
            if (orphanPartnerDoor == keep || orphanPartnerDoor == displaced
                || orphanPartnerDoor == orphan)
                continue;

            pairDoors(keep, orphan);
            pairDoors(displaced, orphanPartnerDoor);

            // Any remaining entry naming one of these four is now stale: it
            // records a pairing that no longer exists, and breaking it later
            // would re-point a door whose partner has already moved. Drop them
            // all rather than let a later repair act on the old picture.
            s32 kept = 0;
            for (s32 p = 0; p < sLeftoverPairCount; p++) {
                const s32 a = sLeftoverPairA[p];
                const s32 b = sLeftoverPairB[p];
                if (a == keep || a == displaced || a == orphan || a == orphanPartnerDoor
                    || b == keep || b == displaced || b == orphan || b == orphanPartnerDoor)
                    continue;
                sLeftoverPairA[kept] = a;
                sLeftoverPairB[kept] = b;
                kept++;
            }
            sLeftoverPairCount = kept;
            repaired++;
        }

        if (repaired > 0)
            collapseSpecialSourceConns();
        return repaired;
    }

    bool isFullyWired() {
        if (!sInitialized || sBuild.active)
            return false;
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            if (!isRandomizableLink(i))
                continue;
            if (sSubNearConn[i].kind == ConnKind_None)
                return false;
        }
        return true;
    }

    bool isInitialized() { return sInitialized; }

    void reset() {
        resetBuilderState();
        sInitialized = false;
        sLastAttempt = 0xFFFFFFFF;
        sLastDifficulty = -1;
        sLastMode = Randomizer::EntranceRandoMode_Off;
        sLastShopMode = Randomizer::ShopLocationMode_Randomized;
        sLastOpenness = -1;
        clearSubStack();
    }

    void restore(const Conn* pipe, const Conn* homeOut, const Conn* subNear, const Conn* subFar, s32 count, u32 seed, u32 attempt, s32 logicDifficulty) {
        // A restored table did not come from this session's builder: there is
        // no in-flight build to resume and no leftover pair that means anything
        // against it.
        resetBuilderState();
        setVanillaConns();

        const s32 copyCount = count < EntranceDB::sLinkCount ? count : EntranceDB::sLinkCount;
        for (s32 i = 0; i < copyCount; i++) {
            sPipeConn[i] = pipe ? pipe[i] : vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeIn);
            sHomeOutConn[i] = homeOut ? homeOut[i] : (linkHasHomeOutDoor(i) ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_HomeOut) : Conn { (s16)-1, ConnKind_None });
            sSubNearConn[i] = subNear ? subNear[i] : vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubNear);
            sSubFarConn[i] = subFar ? subFar[i] : (linkHasFarDoor(i) ? vanillaConnForDoor(i * DoorSlot_Count + DoorSlot_SubFar) : Conn { (s16)-1, ConnKind_None });
        }
        collapseSpecialSourceConns();

        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastDifficulty = logicDifficulty;
        sLastMode = Randomizer::getEntranceRandoMode();
        sLastShopMode = Randomizer::getShopLocationMode();
        sLastOpenness = opennessLevel();
        sInitialized = true;
        clearSubStack();
    }

    // ─────────────────────────────────────────────────────────────
    // Connection lookups
    // ─────────────────────────────────────────────────────────────

    // See setAssumeVanillaForUnwiredDoors.
    static bool sAssumeVanillaForUnwired = false;

    void setAssumeVanillaForUnwiredDoors(bool enabled) { sAssumeVanillaForUnwired = enabled; }

    static Conn resolveStoredConn(Conn stored, s32 door) {
        if (stored.kind == ConnKind_None && sAssumeVanillaForUnwired)
            return vanillaConnForDoor(door);
        return stored;
    }

    Conn getPipeConn(s32 linkIndex) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount)
            return { (s16)-1, ConnKind_None };
        const s32 door = makeDoor(linkIndex, DoorSlot_HomeIn);
        if (!sInitialized)
            return normalizeDeepWoodsArrival(vanillaConnForDoor(door));
        return normalizeDeepWoodsArrival(resolveStoredConn(sPipeConn[linkIndex], door));
    }

    Conn getHomeDoorConn(s32 linkIndex, bool outSide) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount)
            return { (s16)-1, ConnKind_None };
        if (!outSide)
            return getPipeConn(linkIndex);
        if (!linkHasHomeOutDoor(linkIndex))
            return getPipeConn(linkIndex);
        const s32 door = makeDoor(linkIndex, DoorSlot_HomeOut);
        if (!sInitialized)
            return normalizeDeepWoodsArrival(vanillaConnForDoor(door));
        return normalizeDeepWoodsArrival(resolveStoredConn(sHomeOutConn[linkIndex], door));
    }

    Conn getSubDoorConn(s32 linkIndex, bool farDoor) {
        if (linkIndex < 0 || linkIndex >= EntranceDB::sLinkCount)
            return { (s16)-1, ConnKind_None };
        const s32 door = makeDoor(linkIndex, farDoor ? DoorSlot_SubFar : DoorSlot_SubNear);
        if (!sInitialized)
            return normalizeDeepWoodsArrival(vanillaConnForDoor(door));
        return normalizeDeepWoodsArrival(
            resolveStoredConn(farDoor ? sSubFarConn[linkIndex] : sSubNearConn[linkIndex], door));
    }

    // ─────────────────────────────────────────────────────────────
    // Current-sub tracking (shop/closet stage-name fixup only)
    // ─────────────────────────────────────────────────────────────

    static const char* sCurrentSub = nullptr;

    void noteEnteredSub(const char* subStage) {
        sCurrentSub = subStage;
    }

    void clearSubStack() {
        sCurrentSub = nullptr;
    }

    const char* peekTopSubStage() {
        return sCurrentSub;
    }

    void syncSubTrackingToStage(const char* stageName) {
        // Store the canonical EntranceDB pointer so the cache stays comparable to the
        // subStage strings the changeNextStage hook matches against; clear if the stage
        // is not a tracked runtime sub-area (e.g. a warp back to an overworld/Odyssey
        // stage, or an excluded observation row that should never route).
        sCurrentSub = EntranceDB::findTrackedSubStageName(stageName);
    }

} // namespace EntranceRando
