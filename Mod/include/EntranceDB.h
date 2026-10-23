#pragma once
#include "EntranceDB2.h"
#include "sead/prim/seadSafeString.h"

#include <cstring>
#include <stdalign.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// Runtime view of the entrance database.
//
// EntranceDB2 is the generated source of truth and contains every curated legacy
// row, including active shuffle links, vanilla-only story links, and excluded
// observations kept for reference. EntranceDB exposes that table in the shape the
// randomizer code expects, plus helpers for the clearer semantics we actually
// want:
//
// - status answers whether a row is shuffled, fixed-vanilla, or excluded
// - home/sub "out" behavior answers whether that side is a real door, the same
//   surface as the entry side, or only a return-spawn surface
//
// Callers should use the helpers below instead of inferring behavior from
// string equality or the old exit-only flag arrays.

namespace EntranceDB {

    using EntranceLink = EntranceDB2::LegacyLink;
    using RandoStatus = EntranceDB2::RandoStatus;
    using DoorSideBehavior = EntranceDB2::DoorSideBehavior;

    static constexpr const EntranceLink* sLinks = EntranceDB2::sLegacyLinks;
    static constexpr s32 sLinkCount = EntranceDB2::sLegacyLinkCount;

    inline bool isValidIndex(s32 i) {
        return i >= 0 && i < sLinkCount;
    }

    inline const EntranceLink* entryAt(s32 i) {
        return isValidIndex(i) ? &sLinks[i] : nullptr;
    }

    inline bool isActiveLink(const EntranceLink& link) {
        return link.status == EntranceDB2::RandoStatus_Active;
    }

    inline bool isVanillaLink(const EntranceLink& link) {
        return link.status == EntranceDB2::RandoStatus_Vanilla;
    }

    inline bool isExcludedLink(const EntranceLink& link) {
        return link.status == EntranceDB2::RandoStatus_Excluded;
    }

    inline bool isRuntimeUsableLink(const EntranceLink& link) {
        return isActiveLink(link) || isVanillaLink(link);
    }

    inline bool isActiveLink(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isActiveLink(*link);
    }

    inline bool isVanillaLink(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isVanillaLink(*link);
    }

    inline bool isExcludedLink(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isExcludedLink(*link);
    }

    inline bool isRuntimeUsableLink(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isRuntimeUsableLink(*link);
    }

    inline bool hasHomeOutDoor(const EntranceLink& link) {
        return EntranceDB2::legacyLinkHasHomeOutDoor(link);
    }

    inline bool hasSubOutDoor(const EntranceLink& link) {
        return EntranceDB2::legacyLinkHasSubOutDoor(link);
    }

    inline bool hasHomeOutDoor(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && hasHomeOutDoor(*link);
    }

    inline bool hasSubOutDoor(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && hasSubOutDoor(*link);
    }

    inline bool isEndpointEnterable(s16 endpoint) {
        return endpoint >= 0
            && EntranceDB2::isOutgoing(endpoint)
            && !EntranceDB2::isExitOnly(endpoint);
    }

    inline bool isHomeInEnterable(const EntranceLink& link) {
        if (!isRuntimeUsableLink(link)
            || link.homeInEndpoint < 0
            || !EntranceDB2::isOutgoing(link.homeInEndpoint))
            return false;

        // Some usable overworld entrances are authored with IsExitOnly even
        // though the player can enter them (notably the Mushroom Kingdom well).
        // An active legacy row is the curated assertion that its primary home
        // side belongs in the shuffle, so do not let that unreliable actor flag
        // silently remove it from both the connection pool and runtime lookup.
        return !EntranceDB2::isExitOnly(link.homeInEndpoint) || isActiveLink(link);
    }

    inline bool isHomeInEnterable(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isHomeInEnterable(*link);
    }

    inline bool isHomeOutEnterable(const EntranceLink& link) {
        return isRuntimeUsableLink(link)
            && hasHomeOutDoor(link)
            && isEndpointEnterable(link.homeOutEndpoint);
    }

    inline bool isHomeOutEnterable(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isHomeOutEnterable(*link);
    }

    inline const EntranceLink* findLegacyLinkInfo(s32 i) {
        return entryAt(i);
    }

    inline bool isDeepWoodsArea(const EntranceLink& link) {
        return std::strcmp(link.homeStage, "ForestWorldHomeStage") == 0
            && std::strcmp(link.subStage, "ForestWorldWoodsStage") == 0;
    }

    inline bool isDeepWoodsArea(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isDeepWoodsArea(*link);
    }

    // Every loading zone the Deep Woods owns a side of: the two drops down from
    // the Wooded overworld, and the doors nested inside the Woods itself (the
    // Costume Room and the Treasure Tree). Unlike isDeepWoodsArea - the narrower
    // "this row is one of the overworld drops" test entrance routing collapses
    // onto a primary link - this is the semantic question Subarea Keys ask, and
    // the answer for all of them is the same: never gate them. The Woods is
    // Wooded's connective tissue rather than an optional pocket, and its vine
    // exits mean a locked drop would strand the far side of it.
    inline bool isDeepWoodsLoadingZone(const EntranceLink& link) {
        return std::strcmp(link.homeStage, "ForestWorldWoodsStage") == 0
            || std::strcmp(link.subStage, "ForestWorldWoodsStage") == 0;
    }

    inline bool isDeepWoodsLoadingZone(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isDeepWoodsLoadingZone(*link);
    }

    // The Sphinx's treasure vault. Its door is SeaWorldSphinxQuizZone, an
    // embedded zone of the Seaside overworld rather than a stage of its own, so
    // a denied entry cannot be stood back on its own trigger the way every other
    // sub-area door can - see the fixed checkpoint respawn in EntranceHooks'
    // subarea-denial branch.
    inline bool isSeasideSphinxTreasureLink(const EntranceLink& link) {
        return std::strcmp(link.homeStage, "SeaWorldHomeStage") == 0
            && std::strcmp(link.subStage, "SeaWorldSecretStage") == 0
            && std::strcmp(link.homeEntranceIn, "TreasureEventWorldSea") == 0;
    }

    inline bool isSeasideSphinxTreasureLink(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isSeasideSphinxTreasureLink(*link);
    }

    // Ice Cave has two physical return pipes inside the sub-area. The ceiling
    // drop and the entrance pipe are one coupled interface; the Jaxi-ruins
    // pipe is the separate far-side exit used by the shuffle.
    inline bool isIceCaveArea(const EntranceLink& link) {
        return std::strcmp(link.homeStage, "SandWorldHomeStage") == 0
            && std::strcmp(link.subStage, "SandWorldPressExStage") == 0;
    }

    inline bool isIceCaveArea(s32 i) {
        const EntranceLink* link = entryAt(i);
        return link && isIceCaveArea(*link);
    }

    inline s32 findIceCavePrimaryLink() {
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isActiveLink(link) || !isIceCaveArea(link))
                continue;
            if (std::strcmp(link.homeEntranceIn, "arijigoku") == 0
                && std::strcmp(link.subEntranceOut, "arijigoku2") == 0)
                return i;
        }
        return -1;
    }

    inline s32 findIceCaveEntrancePipeAliasLink() {
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link) || !isIceCaveArea(link))
                continue;
            if (std::strcmp(link.subEntranceOut, "arijigoku1") == 0)
                return i;
        }
        return -1;
    }

    inline bool isDeepWoodsFallEntrance(const char* entrance) {
        return entrance
            && (std::strcmp(entrance, "Jyukai001") == 0
                || std::strcmp(entrance, "Jyukai003") == 0);
    }

    inline bool isDeepWoodsVineExit(const char* entrance) {
        return entrance
            && (std::strcmp(entrance, "Jyukai001v") == 0
                || std::strcmp(entrance, "Jyukai002") == 0
                || std::strcmp(entrance, "Jyukai003v") == 0
                || std::strcmp(entrance, "Jyukai004") == 0);
    }

    inline s32 findDeepWoodsPrimaryLink() {
        s32 fallback = -1;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link) || !isDeepWoodsArea(link))
                continue;
            if (fallback < 0)
                fallback = i;
            if (std::strcmp(link.homeEntranceIn, "Jyukai001") == 0)
                return i;
        }
        return fallback;
    }

    inline const EntranceLink* findBySubEntry(const char* subStage, const char* subEntrance) {
        if (!subStage || !subEntrance)
            return nullptr;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link))
                continue;
            if (std::strcmp(link.subStage, subStage) == 0
                && std::strcmp(link.subEntranceIn, subEntrance) == 0)
                return &link;
        }
        return nullptr;
    }

    inline const EntranceLink* findByHomeEntry(const char* homeStage, const char* homeEntrance) {
        if (!homeStage || !homeEntrance)
            return nullptr;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isHomeInEnterable(link))
                continue;
            if (std::strcmp(link.homeStage, homeStage) == 0
                && std::strcmp(link.homeEntranceIn, homeEntrance) == 0)
                return &link;
        }
        return nullptr;
    }

    inline s32 findIndexByHomeEntry(const char* homeStage, const char* homeEntrance) {
        if (!homeStage || !homeEntrance)
            return -1;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isHomeInEnterable(link))
                continue;
            if (std::strcmp(link.homeStage, homeStage) == 0
                && std::strcmp(link.homeEntranceIn, homeEntrance) == 0)
                return i;
        }
        return -1;
    }

    // Matches a usable home-side loading zone. homeEntranceOut only counts when that
    // side is a real door; return-spawn surfaces stay non-enterable.
    inline s32 findIndexByHomeEntrance(const char* homeStage, const char* entrance,
        bool& out_isOutSide) {
        out_isOutSide = false;
        if (!homeStage || !entrance)
            return -1;

        if (std::strcmp(homeStage, "ForestWorldHomeStage") == 0
            && isDeepWoodsFallEntrance(entrance)) {
            const s32 primary = findDeepWoodsPrimaryLink();
            if (primary >= 0)
                return primary;
        }

        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isHomeInEnterable(link) || std::strcmp(link.homeStage, homeStage) != 0)
                continue;
            if (std::strcmp(link.homeEntranceIn, entrance) == 0) {
                out_isOutSide = false;
                return i;
            }
        }

        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isHomeOutEnterable(link)
                || std::strcmp(link.homeStage, homeStage) != 0)
                continue;
            if (std::strcmp(link.homeEntranceOut, entrance) == 0) {
                out_isOutSide = true;
                return i;
            }
        }

        return -1;
    }

    // Whether `index` is the link the runtime hands the player when they use that
    // overworld door. findIndexByHomeEntrance() scans in link order, so when two
    // rows share a home entrance id only the first can ever receive the player: the
    // Ice Cave's ceiling drop ("arijigoku") is claimed by both the shuffled row and
    // its vanilla return alias, and the loser is a return spawn, not a second way
    // in. Callers use this to avoid crediting a shadowed row with a door it will
    // never own -- in logic reachability, and when naming a loading zone.
    //
    // Only a literal duplicate of the id shadows a row. Deep Woods is deliberately
    // different: findIndexByHomeEntrance() folds both fall entrances onto one
    // primary link, but Jyukai001 and Jyukai003 are two distinct holes in the
    // overworld rather than one door written down twice, so each keeps its own.
    //
    // A door no earlier row claims answers true on purpose. isHomeInEnterable()
    // rejects endpoints flagged IsExitOnly, and that actor flag is unreliable (see
    // above), so this must not silently delete a vanilla door that is the only way
    // into its sub-area.
    inline bool ownsHomeEntranceSide(s32 index, bool outSide) {
        const EntranceLink* link = entryAt(index);
        if (!link)
            return false;

        // A return spawn is where the player surfaces, not a door they can enter.
        if (outSide && !hasHomeOutDoor(*link))
            return false;

        const char* entrance = outSide ? link->homeEntranceOut : link->homeEntranceIn;
        if (!entrance || !entrance[0])
            return false;

        // Mirrors findIndexByHomeEntrance()'s scan order: the first enterable row
        // naming this id wins, so anything after it never receives the player.
        for (s32 i = 0; i < index; i++) {
            const EntranceLink& other = sLinks[i];
            if (std::strcmp(other.homeStage, link->homeStage) != 0)
                continue;
            if (isHomeInEnterable(other) && std::strcmp(other.homeEntranceIn, entrance) == 0)
                return false;
            if (isHomeOutEnterable(other) && std::strcmp(other.homeEntranceOut, entrance) == 0)
                return false;
        }
        return true;
    }

    // Matches a usable sub-stage door. Near sides win over far sides so same-id
    // single-door rows stay classified as their primary entrance instead of a fake far
    // door. Excluded observations never consume runtime exits.
    inline s32 findIndexBySubDoor(const char* subStage, const char* entrance,
        bool& out_isFarDoor) {
        out_isFarDoor = false;
        if (!subStage || !entrance)
            return -1;

        if (std::strcmp(subStage, "ForestWorldWoodsStage") == 0
            && isDeepWoodsVineExit(entrance)) {
            const s32 primary = findDeepWoodsPrimaryLink();
            if (primary >= 0) {
                out_isFarDoor = true;
                return primary;
            }
        }

        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link) || std::strcmp(link.subStage, subStage) != 0)
                continue;
            if (std::strcmp(link.subEntranceIn, entrance) == 0) {
                out_isFarDoor = false;
                return i;
            }
        }

        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link) || !hasSubOutDoor(link)
                || std::strcmp(link.subStage, subStage) != 0)
                continue;
            if (std::strcmp(link.subEntranceOut, entrance) == 0) {
                out_isFarDoor = true;
                return i;
            }
        }

        return -1;
    }

    // True when stageName is a shuffled sub-area. Scenario patching must leave these
    // alone because entrance randomizer can drop them under a kingdom scenario their
    // own stage data does not define. Vanilla-only story rows are intentionally NOT
    // treated as unpatched here.
    inline bool isSubStage(const char* stageName) {
        if (!stageName)
            return false;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isActiveLink(link))
                continue;
            if (std::strcmp(link.subStage, stageName) == 0)
                return true;
        }
        return false;
    }

    // True when stageName is any non-excluded sub-area tracked by the database. Used by
    // runtime exit routing and the current-sub cache, where vanilla identity-wired rows
    // still matter.
    inline bool isTrackedSubStage(const char* stageName) {
        if (!stageName)
            return false;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link))
                continue;
            if (std::strcmp(link.subStage, stageName) == 0)
                return true;
        }
        return false;
    }

    inline const char* findTrackedSubStageName(const char* stageName) {
        if (!stageName)
            return nullptr;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link))
                continue;
            if (std::strcmp(link.subStage, stageName) == 0)
                return link.subStage;
        }
        return nullptr;
    }

    inline s32 findIndexBySubStage(const char* homeStage, const char* subStage) {
        if (!homeStage || !subStage)
            return -1;
        for (s32 i = 0; i < sLinkCount; i++) {
            const EntranceLink& link = sLinks[i];
            if (!isRuntimeUsableLink(link))
                continue;
            if (std::strcmp(link.homeStage, homeStage) == 0
                && std::strcmp(link.subStage, subStage) == 0)
                return i;
        }
        return -1;
    }

} // namespace EntranceDB
