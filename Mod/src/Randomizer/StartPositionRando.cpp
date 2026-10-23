#include "Randomizer/StartPositionRando.h"

#include "EntranceDB.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/WorldMeta.h"

#include "Library/LiveActor/ActorPoseUtil.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/DemoUtil.h"

namespace StartPositionRando {
    namespace {
        namespace LD = Randomizer::LogicData;

        // Candidates are drawn from the coordinate-bearing nodes (1332 of 1617
        // at the time of writing), plus one sentinel for the Odyssey drop-off.
        constexpr s32 kMaxCandidates = LD::sNodeCount + 1;

        // Probing a candidate runs a full reachability fixed point. Accepting the
        // first candidate that validates normally settles in one or two probes,
        // but a badly gated seed could walk the whole shuffled pool; cap the work
        // so generation cannot stall on a hopeless configuration.
        constexpr s32 kMaxProbes = 48;

        s32 sStartNode = -1;
        s32 sStartStage = -1;
        s32 sStartKingdom = -1;
        // Overworld/Max may deliberately roll the normal Odyssey drop-off as
        // their start.  Keep that result distinct from the Vanilla setting:
        // there is no logic node to seed, but the pause menu can still offer a
        // meaningful Return to Start warp.
        bool sVanillaDropoffStart = false;

        // Deferred reposition state (see updatePendingReposition).
        bool sRepositionPending = false;
        s32 sRepositionFrames = 0;
        constexpr s32 kRepositionSettleFrames = 2;
        // How far the player may drift from the armed spawn before it counts as
        // "the cutscene moved him". Stage coordinates run to five digits and the
        // Odyssey is a long way from any sub-area door, so this only has to be
        // wider than the small settle the arrival itself does.
        constexpr float kRepositionToleranceSq = 1000.0f * 1000.0f;

        bool isValidNode(s32 node) { return node >= 0 && node < LD::sNodeCount; }
        bool isValidStage(s32 stage) { return stage >= 0 && stage < LD::sStageCount; }
        bool isValidKingdom(s32 kingdom) { return kingdom >= 0 && kingdom < LD::sKingdomCount; }

        s32 mainStageOfKingdom(s32 kingdom) {
            return isValidKingdom(kingdom) ? LD::sMainStageByKingdom[kingdom] : -1;
        }

        // The kingdom a brand new save physically opens in. KingdomRando::clear()
        // restores the vanilla tiers (tier 0 = Cap) when Kingdom Order is off, so
        // this is correct either way. Mirrors
        // ProgressionHooks::getStartingProgressionWorldId().
        s32 calcStartKingdom() {
            const s32 kingdom = KingdomRando::getStartingWorldId();
            return isValidKingdom(kingdom) ? kingdom : WorldMeta::WorldId_Cap;
        }

        bool nodeHasAnchor(s32 node) {
            return isValidNode(node) && LD::sNodeAnchors[node].hasCoordinates;
        }

        // A node that IS a loading zone. Its recorded coordinates are the trigger
        // volume rather than a place to stand, so placement must go through the
        // zone's authored PlayerStart instead (see Spawn::preferEntranceId).
        bool nodeIsLoadingZone(s32 node) {
            if (!isValidNode(node))
                return false;
            const LD::AnchorKind kind = LD::sNodeAnchors[node].kind;
            return kind == LD::AnchorKind_EntranceSide || kind == LD::AnchorKind_ChangeStageId;
        }

        // Overworld pool: only the home-out endpoints of the starting kingdom's
        // sub-area doors, i.e. the places a player surfaces when he leaves a
        // sub-area. The home-in endpoint is the entrance trigger on the way into
        // the sub-area; using it as a start sends the arrival code through a
        // trigger (or its raw coordinates) instead of the authored return
        // PlayerStart. Vanilla-status links count too - they are still real
        // doors, just not in the shuffle pool.
        bool isOverworldCandidate(s32 node, s32 mainStage) {
            if (!nodeHasAnchor(node) || LD::sNodes[node].excluded)
                return false;
            const LD::NodeAnchor& anchor = LD::sNodeAnchors[node];
            if (anchor.kind != LD::AnchorKind_EntranceSide
                || !anchor.changeStageId || !anchor.changeStageId[0])
                return false;
            if (LD::sNodes[node].stage != mainStage)
                return false;

            for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
                if (!EntranceDB::isRuntimeUsableLink(i))
                    continue;
                const LD::EntranceAccess& access = LD::sEntrances[i];
                if (access.homeOutNode == node)
                    return true;
            }
            return false;
        }

        // Max pool: the Overworld pool plus coordinate-bearing nodes inside the
        // sub-area chains LINKED FROM the starting kingdom's overworld. Not "any
        // sub-area anywhere": with entrances shuffled a sub-area sitting
        // physically inside the starting kingdom can belong to a chain rooted in
        // another one, and starting there strands the player in kingdoms his
        // route has not reached yet. `linkedStages` comes from
        // MoonRando::computeStagesLinkedFromKingdom. Whether he can also get back
        // out is proven per candidate by MoonRando::canStartAtNode.
        bool isSubAreaCandidate(s32 node, const bool* linkedStages) {
            if (!nodeHasAnchor(node) || LD::sNodes[node].excluded)
                return false;

            const s32 stage = LD::sNodes[node].stage;
            if (!isValidStage(stage) || !linkedStages[stage])
                return false;

            for (s32 kingdom = 0; kingdom < LD::sKingdomCount; kingdom++) {
                if (LD::sMainStageByKingdom[kingdom] == stage)
                    return false;
            }
            return true;
        }

        // Sentinel candidate: "start where the Odyssey drops you off", which stays
        // one of the possible outcomes in both non-vanilla modes.
        constexpr s32 kOdysseyCandidate = -1;

        // Inside-the-Odyssey mode. The ship interior is a stage of its own with a
        // single exit - a ChangeStageArea aimed at the magic name
        // "CurrentWorldHome" - so walking out lands the player exactly where a
        // vanilla file starts. It carries no logic node and no entrance link,
        // which is why this mode needs no walk-back proof and leaves the reachable
        // set untouched.
        //
        // GameDataFile::changeNextStage resolves that name through
        // WorldList::getMainStageName(mCurrentWorldId), clamping a negative id to
        // 0. So the sub-area world-id pin is not merely a crash guard here: an
        // unpinned start would silently walk out into Cap Kingdom instead of the
        // kingdom the run actually opens in. isStartInSubArea() reports true for
        // this start, which is what puts the pin on the path.
        //
        // kOdysseyStartId is the start id vanilla itself uses to enter the ship
        // (ShineTowerBackDoor / ShineTowerRocket both build their ChangeStageInfo
        // with "HomeBackDoor"), so the game resolves its authored PlayerStart and
        // no synthetic coordinate spawn is involved.
        constexpr const char* kOdysseyStage = "HomeShipInsideStage";
        constexpr const char* kOdysseyStartId = "HomeBackDoor";
        constexpr const char* kOdysseyNodeLabel = "Inside the Odyssey";

        s32 findStageIndex(const char* stage) {
            for (s32 i = 0; i < LD::sStageCount; i++) {
                if (std::strcmp(LD::sStages[i].stage, stage) == 0)
                    return i;
            }
            return -1;
        }

        // True once rebuild has committed the Inside-the-Odyssey start. Kept
        // separate from sStartNode: this start is a stage, not a node.
        bool sOdysseyStart = false;
        bool sOdysseyOpeningHudPending = false;

        s32 collectCandidates(
            s32* out, Randomizer::StartPositionMode mode, s32 mainStage, const bool* linkedStages) {
            s32 count = 0;
            out[count++] = kOdysseyCandidate;
            for (s32 node = 0; node < LD::sNodeCount && count < kMaxCandidates; node++) {
                const bool eligible = isOverworldCandidate(node, mainStage)
                    || (mode == Randomizer::StartPositionMode_Max
                        && isSubAreaCandidate(node, linkedStages));
                if (eligible)
                    out[count++] = node;
            }
            return count;
        }

        // Re-derivation guard. Randomizer::reinit() runs on every stage-scene init
        // and MoonRando's already-generated path calls rebuild again right after,
        // so without this the full candidate probe (an initState plus a
        // reachability fixed point) ran twice per stage load for an answer that
        // cannot have changed.
        bool sCacheValid = false;
        u32 sCachedSeed = 0;
        u32 sCachedAttempt = 0;
        Randomizer::StartPositionMode sCachedMode = Randomizer::StartPositionMode_Vanilla;
        s32 sCachedKingdom = -1;
    } // namespace

    void clear() {
        sStartNode = -1;
        sStartStage = -1;
        sStartKingdom = -1;
        sVanillaDropoffStart = false;
        sOdysseyStart = false;
        sOdysseyOpeningHudPending = false;
        // A pending reposition targets a start that no longer exists.
        clearPendingReposition();
    }

    bool demoteToVanillaDropoff() {
        if (!isActive() || sStartNode < 0)
            return false;
        const s32 kingdom = sStartKingdom;
        Randomizer::logPush(
            "start position: %s stranded by the final wiring; using the vanilla drop-off",
            LD::sNodes[sStartNode].nodeId);
        clear();
        sVanillaDropoffStart = true;
        sStartKingdom = kingdom;
        return true;
    }

    void rebuild(u32 seed, u32 attempt) {
        const Randomizer::StartPositionMode mode = Randomizer::getStartPositionMode();
        const s32 kingdom = calcStartKingdom();
        if (sCacheValid && sCachedSeed == seed && sCachedAttempt == attempt
            && sCachedMode == mode && sCachedKingdom == kingdom)
            return;

        // Probing a candidate calls back into MoonRando::initState, which asks
        // this module for the chosen node. Clearing first keeps every probe
        // measured against the plain opening state.
        clear();
        sCacheValid = true;
        sCachedSeed = seed;
        sCachedAttempt = attempt;
        sCachedMode = mode;
        sCachedKingdom = kingdom;

        if (mode == Randomizer::StartPositionMode_Vanilla)
            return;

        // Inside the Odyssey is a fixed start, not a roll: no candidate pool, no
        // walk-back proof (the ship's only exit is the unshuffled CurrentWorldHome
        // door), and no logic node to seed.
        if (mode == Randomizer::StartPositionMode_Odyssey) {
            const s32 stage = findStageIndex(kOdysseyStage);
            if (!isValidStage(stage)) {
                Randomizer::logPush(
                    "start position: %s missing from the logic stage table; using vanilla",
                    kOdysseyStage);
                return;
            }
            sOdysseyStart = true;
            sStartStage = stage;
            sStartKingdom = kingdom;
            Randomizer::logPush("start position: inside the Odyssey (kingdom %d)", kingdom);
            return;
        }

        const s32 mainStage = mainStageOfKingdom(kingdom);
        if (!isValidStage(mainStage))
            return;

        static bool sLinkedStages[LD::sStageCount];
        MoonRando::computeStagesLinkedFromKingdom(kingdom, sLinkedStages);

        static s32 sCandidates[kMaxCandidates];
        s32 count = collectCandidates(sCandidates, mode, mainStage, sLinkedStages);
        if (count <= 0) {
            Randomizer::logPush("start position: no candidate nodes for kingdom %d", kingdom);
            sVanillaDropoffStart = true;
            sStartKingdom = kingdom;
            return;
        }

        // Fisher-Yates over the whole pool, then accept the first candidate whose
        // opening walk provably reaches the starting kingdom's overworld.
        Randomizer::Rng rng = Randomizer::makeRng(
            seed ^ (attempt * 0x9E3779B9u), Randomizer::RngStream::StartPosition);
        for (s32 i = count - 1; i > 0; i--) {
            const s32 j = (s32)rng.nextModulo((u32)(i + 1));
            const s32 tmp = sCandidates[i];
            sCandidates[i] = sCandidates[j];
            sCandidates[j] = tmp;
        }

        const s32 probeLimit = count < kMaxProbes ? count : kMaxProbes;
        for (s32 i = 0; i < probeLimit; i++) {
            const s32 node = sCandidates[i];
            if (node == kOdysseyCandidate) {
                sVanillaDropoffStart = true;
                sStartKingdom = kingdom;
                Randomizer::logPush(
                    "start position: Odyssey drop-off (kingdom %d, probe %d/%d)",
                    kingdom, i + 1, probeLimit);
                return; // already cleared: vanilla placement
            }
            if (!MoonRando::canStartAtNode(node, mainStage))
                continue;

            sStartNode = node;
            sStartStage = LD::sNodes[node].stage;
            sStartKingdom = kingdom;
            Randomizer::logPush("start position: %s (kingdom %d, probe %d/%d)",
                LD::sNodes[node].nodeId, kingdom, i + 1, probeLimit);
            return;
        }

        Randomizer::logPush(
            "start position: no candidate reached the %s overworld in %d probes; using vanilla",
            LD::sStages[mainStage].stage, probeLimit);
        clear();
        // A failed probe falls back to the same normal drop-off as the explicit
        // sentinel.  It remains node-less for logic and stage redirection, but
        // it is still a valid Return to Start destination.
        sVanillaDropoffStart = true;
        sStartKingdom = kingdom;
    }

    // "A non-vanilla spawn is committed", which is a stage question rather than a
    // node one: the Inside-the-Odyssey start has no node. Every caller that needs
    // the node itself (MoonRando's seeding) already tests getStartNode() >= 0.
    bool isActive() { return isValidStage(sStartStage); }

    bool canReturnToStart() {
        return isActive() || (sVanillaDropoffStart && isValidKingdom(sStartKingdom));
    }

    s32 getStartNode() { return sStartNode; }
    s32 getStartStageIndex() { return sStartStage; }
    s32 getStartKingdom() { return sStartKingdom; }

    bool isStartInSubArea() {
        return isActive() && sStartStage != mainStageOfKingdom(sStartKingdom);
    }

    void armOpeningHudHide() {
        // rebuild() also runs when an existing save is loaded. Arming only from
        // fresh-save setup prevents a later visit to the Odyssey from being
        // mistaken for the opening after a reboot.
        sOdysseyOpeningHudPending = sOdysseyStart;
    }

    bool shouldHideOpeningHud(const char* currentStage) {
        if (!sOdysseyStart || !sOdysseyOpeningHudPending)
            return false;
        if (!currentStage || std::strcmp(currentStage, kOdysseyStage) != 0) {
            sOdysseyOpeningHudPending = false;
            return false;
        }
        return true;
    }

    bool getSpawn(Spawn* out) {
        if (!out || !isActive())
            return false;

        // Inside the Odyssey travels under the ship's own authored PlayerStart,
        // the same id vanilla uses to walk in. No coordinates: there is nothing to
        // correct, and handing coordinates over would arm a synthetic-spawn
        // fallback (and a pending reposition) for a stage with no arrival demo.
        if (sOdysseyStart) {
            out->stage = kOdysseyStage;
            out->entranceId = kOdysseyStartId;
            out->preferEntranceId = true;
            out->hasCoordinates = false;
            out->x = 0.0f;
            out->y = 0.0f;
            out->z = 0.0f;
            return true;
        }

        if (!isValidNode(sStartNode))
            return false;

        const LD::NodeAnchor& anchor = LD::sNodeAnchors[sStartNode];
        out->stage = LD::sStages[sStartStage].stage;
        out->entranceId = anchor.changeStageId ? anchor.changeStageId : "";
        out->preferEntranceId = nodeIsLoadingZone(sStartNode) && out->entranceId[0] != '\0';
        out->hasCoordinates = anchor.hasCoordinates;
        out->x = anchor.x;
        out->y = anchor.y;
        out->z = anchor.z;
        return true;
    }

    const char* getStartNodeId() {
        if (sOdysseyStart)
            return kOdysseyNodeLabel;
        return isValidNode(sStartNode) ? LD::sNodes[sStartNode].nodeId : nullptr;
    }

    bool warpToStart(GameDataHolder* holder) {
        if (!holder)
            return false;

        // The normal drop-off has no randomized node or synthetic arrival to
        // arm. Re-enter the starting kingdom's main stage with an empty start
        // id and let the game's authored default PlayerStart reproduce the
        // vanilla Odyssey arrival.
        if (sVanillaDropoffStart) {
            const s32 mainStage = mainStageOfKingdom(sStartKingdom);
            if (!isValidStage(mainStage))
                return false;

            const char* stage = LD::sStages[mainStage].stage;
            EntranceHooks::bypassNextTransition();

            GameDataHolderWriter writer(holder);
            ChangeStageInfo info(writer, "", stage, false, -1,
                ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
            info.mPlacementString.copy("");
            info.setWipeType("FadeBlack");
            ScenarioPatcher::normalizeChangeStageInfo(writer->getGameDataFile(), &info, 0);
            GameDataFunction::tryChangeNextStage(writer, &info);
            ProgressionHooks::repairStartPositionWorldId(writer);

            Randomizer::logPush("return to start: vanilla drop-off -> %s", stage);
            return true;
        }

        Spawn spawn = {};
        if (!getSpawn(&spawn) || !spawn.stage || !spawn.stage[0])
            return false;

        const char* entranceId = spawn.preferEntranceId ? spawn.entranceId : "";
        const char* startId = EntranceHooks::getStartPositionStartId(entranceId);

        // This is a warp, not a door: without the bypass the entrance hook would
        // treat the start node's loading-zone id as a transition to re-route and
        // send the player wherever that door leads this seed instead.
        EntranceHooks::bypassNextTransition();

        GameDataHolderWriter writer(holder);
        ChangeStageInfo info(writer, startId, spawn.stage, false, -1,
            ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
        info.mPlacementString.copy(startId);
        info.setWipeType("FadeBlack");
        ScenarioPatcher::normalizeChangeStageInfo(writer->getGameDataFile(), &info, 0);
        GameDataFunction::tryChangeNextStage(writer, &info);

        // After the stage change, never before: changeNextStage's hook clears any
        // pending arrival, so arming first would be thrown away. This is what
        // lands the player on the node itself rather than the stage's default
        // spawn - through the entrance's own PlayerStart when the node is a
        // loading zone, or a synthetic one at its coordinates otherwise.
        EntranceHooks::armStartPositionArrival(entranceId, spawn.x, spawn.y, spawn.z,
            spawn.hasCoordinates);
        // Deliberately does NOT arm a pending reposition: this warp IS the
        // correction updatePendingReposition performs, so re-arming here would
        // make the reload trigger another warp on its first controllable frame,
        // and so on forever.
        // No-op unless the world id is somehow invalid; the guard against the
        // sub-area -1 crash is the same one the new-save path relies on.
        ProgressionHooks::repairStartPositionWorldId(writer);

        Randomizer::logPush("return to start: %s -> %s/%s",
            getStartNodeId(), spawn.stage, startId);
        return true;
    }

    void armPendingReposition() {
        sRepositionPending = isActive();
        sRepositionFrames = 0;
    }

    void clearPendingReposition() { sRepositionPending = false; }

    void updatePendingReposition(
        PlayerActorHakoniwa* player, GameDataHolder* holder, const char* currentStage) {
        if (!sRepositionPending)
            return;

        Spawn spawn = {};
        if (!player || !holder || !getSpawn(&spawn) || !spawn.hasCoordinates || !spawn.stage) {
            sRepositionPending = false;
            return;
        }
        // A stage change means the player has already moved on under his own
        // steam - there is nothing left to correct.
        if (!currentStage || !currentStage[0] || std::strcmp(currentStage, spawn.stage) != 0) {
            sRepositionPending = false;
            return;
        }

        // Wait out the arrival cutscene. rs::isActiveDemo covers the scripted
        // camera; isActiveDemoPlayerPuppetable covers the stretch where the demo
        // is driving Mario's body. Only once both are clear does the game
        // consider him under player control - which is the moment the vanilla
        // demo has finished dropping him at the Odyssey.
        if (rs::isActiveDemo(player) || rs::isActiveDemoPlayerPuppetable(player))
            return;
        if (++sRepositionFrames < kRepositionSettleFrames)
            return;

        // Did the cutscene actually move him? Most kingdoms/scenarios have no
        // opening demo at all, and warping when the spawn already survived would
        // be a pointless extra stage reload.
        const sead::Vector3f* trans = al::getTransPtr(player);
        if (trans) {
            const float dx = trans->x - spawn.x;
            const float dy = trans->y - spawn.y;
            const float dz = trans->z - spawn.z;
            if (dx * dx + dy * dy + dz * dz <= kRepositionToleranceSq) {
                Randomizer::logPush("start position: spawn survived the opening, no warp needed");
                sRepositionPending = false;
                return;
            }
        }

        // Re-run the real entrance warp rather than writing the transform.
        // Moving the actor alone leaves his velocity and collision in whatever
        // state the demo left them, which drops him out of the sky next to the
        // Odyssey instead of standing him at the entrance.
        //
        // Clear BEFORE warping: warpToStart reloads the stage, and a flag left
        // set would re-enter this on the very next controllable frame and warp
        // forever.
        sRepositionPending = false;
        Randomizer::logPush("start position: opening demo displaced Mario, warping to start");
        warpToStart(holder);
    }

} // namespace StartPositionRando
