#include "Randomizer/PaintingRando.h"

#include "Randomizer/ArrivalLog.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingUnlockOverlay.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "game/Item/ShineInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/MapObj/WorldWarpHole.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameSystem.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "Multiplayer/server/Client.hpp"

#include <cstring>

namespace PaintingRando {
    namespace {

        namespace LD = Randomizer::LogicData;

        static s32 sDestinationBySource[LD::sPaintingLinkCount];
        static s32 sSourceByDestination[LD::sPaintingLinkCount];
        // Seed-shuffled permutation of link indices so which painting link a given
        // Cloud moon-credit slot unlocks is tied to the seed (coverage of all links
        // preserved). When links outnumber Cloud credits, the leading entries (see
        // getFreePaintingLinkCount) are surplus links with no credit slot and are
        // unlocked from the start.
        static s32 sCloudLinkOrder[LD::sPaintingLinkCount];
        static bool sInitialized = false;
        static u32 sLastSeed = 0xFFFFFFFF;
        static u32 sLastAttempt = 0xFFFFFFFF;
        static bool sLastPaintingRandoEnabled = false;
        static const char* sPendingLandingStage = nullptr;
        static s32 sPendingLandingWorldId = -1;
        static bool sFreeLinkAnnouncementPending = false;

        static bool isSameString(const char* a, const char* b) {
            if (!a || !b)
                return a == b;
            return std::strcmp(a, b) == 0;
        }

        // Active whenever the table is built, even with Painting Rando off: the QoL
        // patches force warp paintings open pre-endgame, and the vanilla game can
        // only resolve their world ids from post-game save data. With the feature
        // off the table is an identity map, so the hooks reproduce the vanilla
        // routes while keeping the paintings functional (Cascade's painting used to
        // show closed/linkless because of this, and forcing it open crashed).
        static bool isActive() {
            return sInitialized;
        }

        static bool isComeChangeStageId(const char* changeStageId) {
            return changeStageId && std::strstr(changeStageId, "Come");
        }

        static bool isGoChangeStageId(const char* changeStageId) {
            return changeStageId && std::strstr(changeStageId, "Go");
        }

        static bool isLakeOdysseyPaintingChangeStageId(s32 currentWorldId, const char* changeStageId) {
            return currentWorldId == WorldMeta::WorldId_Lake && isSameString(changeStageId, "Odyssey");
        }

        static bool isGoChangeStageIdForWorld(s32 currentWorldId, const char* changeStageId) {
            return isGoChangeStageId(changeStageId)
                || isLakeOdysseyPaintingChangeStageId(currentWorldId, changeStageId);
        }

        static bool isPaintingChangeStageId(const char* changeStageId) {
            return isComeChangeStageId(changeStageId) || isGoChangeStageId(changeStageId);
        }

        static bool isPaintingChangeStageIdForWorld(s32 currentWorldId, const char* changeStageId) {
            return isComeChangeStageId(changeStageId)
                || isGoChangeStageIdForWorld(currentWorldId, changeStageId);
        }

        static bool isForwardPaintingChange(const ChangeStageInfo* info) {
            return info && (isComeChangeStageId(info->mChangeStageId.cstr()) || isComeChangeStageId(info->mPlacementString.cstr()));
        }

        static bool isReturnPaintingChange(const ChangeStageInfo* info) {
            return info && (isGoChangeStageId(info->mChangeStageId.cstr()) || isGoChangeStageId(info->mPlacementString.cstr()));
        }

        static void appendUnsigned(char* out, s32 max, s32& pos, u32 value) {
            char digits[16];
            s32 digitCount = 0;
            do {
                digits[digitCount++] = (char)('0' + value % 10);
                value /= 10;
            } while (value && digitCount < (s32)(sizeof(digits) / sizeof(digits[0])));

            while (digitCount > 0 && pos + 1 < max)
                out[pos++] = digits[--digitCount];
        }

        static void makeFromName(char* out, s32 max, const char* baseName, s32 fromId) {
            if (!out || max <= 0)
                return;

            s32 pos = 0;
            if (baseName) {
                for (const char* c = baseName; *c && pos + 1 < max; c++)
                    out[pos++] = *c;
            }
            const char* from = "From";
            for (const char* c = from; *c && pos + 1 < max; c++)
                out[pos++] = *c;

            if (fromId < 0) {
                if (pos + 1 < max)
                    out[pos++] = '-';
                fromId = -fromId;
            }
            appendUnsigned(out, max, pos, (u32)fromId);
            out[pos] = '\0';
        }

        static bool isValidPaintingIndex(s32 index) {
            return index >= 0 && index < LD::sPaintingLinkCount;
        }

        static bool isValidNode(s32 node) {
            return node >= 0 && node < LD::sNodeCount;
        }

        static const LD::Node* getNode(s32 node) {
            if (!isValidNode(node))
                return nullptr;
            return &LD::sNodes[node];
        }

        static const char* getNodeStage(s32 node) {
            const LD::Node* logicNode = getNode(node);
            if (!logicNode || logicNode->stage < 0 || logicNode->stage >= LD::sStageCount)
                return nullptr;
            return LD::sStages[logicNode->stage].stage;
        }

        static s32 getNodeWorldId(s32 node) {
            const LD::Node* logicNode = getNode(node);
            if (!logicNode || logicNode->stage < 0 || logicNode->stage >= LD::sStageCount)
                return -1;
            return LD::sStages[logicNode->stage].kingdom;
        }

        static s32 findWorldIdByStageName(const char* stageName) {
            if (!stageName)
                return -1;

            for (s32 i = 0; i < LD::sStageCount; i++)
                if (isSameString(LD::sStages[i].stage, stageName))
                    return LD::sStages[i].kingdom;

            const WorldMeta::Entry* world = WorldMeta::findByMainStage(stageName);
            return world ? world->worldId : -1;
        }

        static const char* getSourceStage(s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return nullptr;
            return getNodeStage(LD::sPaintingLinks[paintingIndex].sourceNode);
        }

        static const char* getDestinationStage(s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return nullptr;
            return getNodeStage(LD::sPaintingLinks[paintingIndex].destinationNode);
        }

        static s32 getPaintingSourceWorldId(s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return -1;
            return getNodeWorldId(LD::sPaintingLinks[paintingIndex].sourceNode);
        }

        static s32 getPaintingDestinationWorldId(s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return -1;
            return getNodeWorldId(LD::sPaintingLinks[paintingIndex].destinationNode);
        }

        // Cloud painting unlocks are a *logical reward* role.  Triple-moon
        // redistribution can move that role to an item whose static/home world
        // is not Cloud, so all consumers must use the assigned kingdom rather
        // than ShineDB's immutable worldId.  Without this, a Cloud-looking
        // reward grants nothing while a different-looking (Cloud-home) reward
        // silently opens the painting instead.
        // The moons that unlock paintings. Cloud Kingdom by default, but the
        // Kingdom Moons page can put Painting Unlock on any kingdom, and
        // MoonRando reserves the carrier moons off the same answer.
        static bool isCloudMoonEntry(const ShineDB::ShineEntry& moon) {
            if (moon.uid == 0 || moon.objId < 0)
                return false;
            if (!Randomizer::isPaintingRewardKingdom(
                    MoonRando::getAssignedKingdomForEntry(&moon)))
                return false;
            if (moon.flags & ShineDB::ShineFlag_Toadette)
                return false;
            return true;
        }

        static bool isSameMoonEntry(const ShineDB::ShineEntry& left, const ShineDB::ShineEntry& right) {
            return left.uid == right.uid && isSameString(left.stage, right.stage);
        }

        static s32 getCloudMoonPaintingCredit(const ShineDB::ShineEntry& moon) {
            if (!isCloudMoonEntry(moon))
                return 0;
            return MoonRando::isRewardMultiEntry(&moon) ? 3 : 1;
        }

        // Painting availability follows Cloud's total moon value, not the number
        // of reward entries. Randomize Triple Moons preserves that value while it
        // can replace three singles with one triple; counting entries would make
        // the two missing credits look like surplus links and open them at start.
        static s32 getCloudPaintingCreditCount() {
            s32 count = 0;
            for (s32 i = 0; i < ShineDB::sShineTableSize; i++)
                count += getCloudMoonPaintingCredit(ShineDB::sShineTable[i]);
            return count;
        }

        static s32 findCloudMoonSlot(const ShineDB::ShineEntry& moon) {
            s32 slot = 0;
            for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
                const ShineDB::ShineEntry& candidate = ShineDB::sShineTable[i];
                if (!isCloudMoonEntry(candidate))
                    continue;
                if (isSameMoonEntry(candidate, moon))
                    return slot;
                slot += getCloudMoonPaintingCredit(candidate);
            }
            return -1;
        }

        // When there are more painting links than cloud moons, the surplus links
        // have no moon slot to assign to. Rather than doubling a single moon up to
        // unlock two links, those surplus links are set aside as "free": unlocked
        // (and displayed as open) from the very start of the game.
        static s32 getFreePaintingLinkCount() {
            const s32 cloudPaintingCredits = getCloudPaintingCreditCount();
            return LD::sPaintingLinkCount > cloudPaintingCredits
                ? LD::sPaintingLinkCount - cloudPaintingCredits
                : 0;
        }

        static bool isFreePaintingLinkIndex(s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return false;
            const s32 freeLinkCount = getFreePaintingLinkCount();
            for (s32 i = 0; i < freeLinkCount; i++)
                if (sCloudLinkOrder[i] == paintingIndex)
                    return true;
            return false;
        }

        // Maps a cloud-moon credit slot to the painting link it unlocks. The first
        // getFreePaintingLinkCount() entries of sCloudLinkOrder are the free links
        // set aside above; the rest are assigned one-to-one to credit slots, wrapping
        // (multiple rewards sharing a link) only when there are more credits than
        // assignable links.
        static s32 getCloudMoonPaintingUnlockIndexBySlot(s32 cloudMoonSlot) {
            const s32 cloudPaintingCredits = getCloudPaintingCreditCount();
            if (cloudMoonSlot < 0 || cloudPaintingCredits <= 0 || LD::sPaintingLinkCount <= 0)
                return -1;

            const s32 freeLinkCount = getFreePaintingLinkCount();
            const s32 assignableLinkCount = LD::sPaintingLinkCount - freeLinkCount;
            if (assignableLinkCount <= 0)
                return -1;

            return sCloudLinkOrder[freeLinkCount + (cloudMoonSlot % assignableLinkCount)]; // seed-tied link
        }

        static bool cloudMoonUnlocksPaintingIndex(const ShineDB::ShineEntry& moon, s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return false;

            const s32 cloudMoonSlot = findCloudMoonSlot(moon);
            if (cloudMoonSlot < 0)
                return false;

            const s32 unlockCount = getCloudMoonPaintingCredit(moon);
            for (s32 i = 0; i < unlockCount; i++)
                if (getCloudMoonPaintingUnlockIndexBySlot(cloudMoonSlot + i) == paintingIndex)
                    return true;
            return false;
        }

        static bool initShineInfoFromEntry(ShineInfo* shineInfo, const ShineDB::ShineEntry& entry) {
            if (!shineInfo || entry.objId < 0 || !entry.stage)
                return false;

            shineInfo->mStageName.copy(entry.stage);
            shineInfo->mObjId.format("obj%d", entry.objId);
            shineInfo->mLabel.format("ScenarioName_obj%d", entry.objId);
            shineInfo->setType((entry.flags & ShineDB::ShineFlag_Multi)
                    ? ShineInfo::Type_GrandShine
                    : ShineInfo::Type_Shine);
            return true;
        }

        static bool isCloudMoonCollected(const GameDataFile* file, const ShineDB::ShineEntry& moon) {
            if (!file || !isCloudMoonEntry(moon))
                return false;

            ShineInfo info;
            if (!initShineInfoFromEntry(&info, moon))
                return false;
            return MoonHooks::isGotShineRaw(file, &info);
        }

        // This runs from the painting label/world-id hooks, which fire every frame
        // while a painting is on screen, and its *failure* path is the expensive
        // one: a locked painting sweeps the whole ShineDB and then asks
        // isCloudMoonCollected -- an SNPrintf plus isGotShineRaw's 1024-entry
        // hint-list scan -- about every cloud moon before it can answer false.
        // An unlocked painting returns early on its first collected moon, which
        // is why this only bit in kingdoms whose visible painting was still
        // locked.
        //
        // Collection is monotonic inside a save file, so the answer can only
        // change when a shine is actually committed. Cache it per painting index
        // and let the commit path bump the revision (invalidateUnlockCache).
        static const GameDataFile* sUnlockCacheFile = nullptr;
        static u32 sUnlockCacheRoleLayoutHash = 0;
        static u32 sUnlockCacheRevision = 1;
        static u32 sUnlockCacheStamp[LD::sPaintingLinkCount] = {};
        static bool sUnlockCacheValue[LD::sPaintingLinkCount] = {};

        static bool computePaintingUnlocked(const GameDataFile* file, s32 paintingIndex) {
            for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
                const ShineDB::ShineEntry& moon = ShineDB::sShineTable[i];
                if (!cloudMoonUnlocksPaintingIndex(moon, paintingIndex))
                    continue;
                if (isCloudMoonCollected(file, moon))
                    return true;
            }
            return false;
        }

        static bool isPaintingUnlockedInternal(const GameDataFile* file, s32 paintingIndex) {
            if (!isValidPaintingIndex(paintingIndex))
                return false;
            if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
                return true;
            if (isFreePaintingLinkIndex(paintingIndex))
                return true;
            if (Nuzlocke::isPaintingTemporarilyLocked(file, paintingIndex))
                return false;
            if (Nuzlocke::isPaintingExplicitlyRetained(file, paintingIndex))
                return true;
            // A team-mate has taken the Cloud moon that opens this route.
            // Additive to the general moon-sync pipeline, which is
            // scene-readiness-gated and can miss a credit - and this one gates
            // a real soft-lock.
            //
            // Backed by a durable shared event and a persisted mask
            // (DurableSync) rather than the session-only RAM array this used to
            // read, which was never saved and never replayed on join - so a
            // late joiner could not use a route the team had already opened,
            // and nobody kept it across a restart.
            if (Client::hasRemotePaintingUnlock(paintingIndex))
                return true;

            // A different file or redistributed role layout changes the set of
            // moons credited to Cloud, so drop every stamp rather than reusing
            // an unlock answer from the previous layout.
            const u32 roleLayoutHash = MoonRando::getMoonRoleLayoutHash();
            if (sUnlockCacheFile != file || sUnlockCacheRoleLayoutHash != roleLayoutHash) {
                sUnlockCacheFile = file;
                sUnlockCacheRoleLayoutHash = roleLayoutHash;
                for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                    sUnlockCacheStamp[i] = 0;
            }

            if (sUnlockCacheStamp[paintingIndex] == sUnlockCacheRevision)
                return sUnlockCacheValue[paintingIndex];

            const bool unlocked = computePaintingUnlocked(file, paintingIndex);
            sUnlockCacheStamp[paintingIndex] = sUnlockCacheRevision;
            sUnlockCacheValue[paintingIndex] = unlocked;
            return unlocked;
        }

        static GameDataFile* tryGetCurrentGameDataFile() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;

            HakoniwaSequence* sequence = static_cast<HakoniwaSequence*>(system->mSequence);
            if (!sequence->mGameDataHolderAccessor.mData)
                return nullptr;
            return sequence->mGameDataHolderAccessor.mData->getGameDataFile();
        }

        static GameDataFile* tryGetGameDataFile(const GameDataHolder* holder) {
            if (!holder)
                return tryGetCurrentGameDataFile();
            return holder->getGameDataFile();
        }

        static GameDataFile* tryGetGameDataFile(const WorldWarpHole* worldWarpHole) {
            if (!worldWarpHole)
                return tryGetCurrentGameDataFile();

            GameDataHolderAccessor accessor(worldWarpHole);
            if (!accessor.mData)
                return tryGetCurrentGameDataFile();
            return accessor.mData->getGameDataFile();
        }

        static void rebuildInverse() {
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                sSourceByDestination[i] = i;
            for (s32 source = 0; source < LD::sPaintingLinkCount; source++) {
                const s32 destination = sDestinationBySource[source];
                if (isValidPaintingIndex(destination))
                    sSourceByDestination[destination] = source;
            }
        }

        // A painting must lead to a different kingdom. Reject both same-stage
        // and same-kingdom mappings (the former is implied by the latter for the
        // home-stage link set but kept for robustness).
        static bool hasSelfMapping() {
            for (s32 source = 0; source < LD::sPaintingLinkCount; source++) {
                const s32 destination = sDestinationBySource[source];
                const char* sourceStage = getSourceStage(source);
                const char* destinationStage = getDestinationStage(destination);
                if (sourceStage && destinationStage && isSameString(sourceStage, destinationStage))
                    return true;

                const s32 sourceWorld = getPaintingSourceWorldId(source);
                const s32 destinationWorld = getPaintingDestinationWorldId(destination);
                if (sourceWorld >= 0 && destinationWorld >= 0 && sourceWorld == destinationWorld)
                    return true;
            }
            return false;
        }

        static s32 findSourceIndexByWorldId(s32 worldId) {
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                if (getPaintingSourceWorldId(i) == worldId)
                    return i;
            return -1;
        }

        static s32 findDestinationIndexByWorldId(s32 worldId) {
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                if (getPaintingDestinationWorldId(i) == worldId)
                    return i;
            return -1;
        }

        static bool isPaintingWorldId(s32 worldId) {
            return findSourceIndexByWorldId(worldId) >= 0 || findDestinationIndexByWorldId(worldId) >= 0;
        }

        static s32 findSourceIndexByWarpId(const GameDataHolder* holder, s32 warpId) {
            if (!holder || warpId < 0)
                return -1;
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
                const s32 sourceWorld = getPaintingSourceWorldId(i);
                if (holder->calcWorldWarpHoleIdFromWorldId(sourceWorld) == warpId)
                    return i;
            }
            return -1;
        }

        static const GameDataHolder::WorldWarpHoleInfo* findWarpInfo(
            const GameDataHolder* holder, s32 worldId, const char* baseName, s32 fromId) {
            if (!holder || !holder->mWorldWarpHoleInfos)
                return nullptr;

            if (fromId >= 0) {
                char fromName[32];
                makeFromName(fromName, sizeof(fromName), baseName, fromId);
                for (s32 i = 0; i < holder->mWorldWarpHoleInfoNum; i++) {
                    const GameDataHolder::WorldWarpHoleInfo& info = holder->mWorldWarpHoleInfos[i];
                    if (info.worldId == worldId && isSameString(info.name.cstr(), fromName))
                        return &info;
                }
            }

            for (s32 i = 0; i < holder->mWorldWarpHoleInfoNum; i++) {
                const GameDataHolder::WorldWarpHoleInfo& info = holder->mWorldWarpHoleInfos[i];
                if (info.worldId == worldId && isSameString(info.name.cstr(), baseName))
                    return &info;
            }

            return nullptr;
        }

        // A locked painting is a steady state, not an event: this resolve runs
        // from the label/world-id hooks for as long as the painting is on screen,
        // so logging every failure emitted ~900 identical lines in one minute of
        // Sand Kingdom. Each one is an svcOutputDebugString the host has to
        // format and write. Log a transition instead: remember the last reported
        // failure per direction and stay quiet until it actually changes.
        static s32 sLastComeFailWorld = -2;
        static s32 sLastComeFailDetail = -2;
        static s32 sLastGoFailWorld = -2;

        static bool shouldLogComeFailure(s32 worldId, s32 detail) {
            if (sLastComeFailWorld == worldId && sLastComeFailDetail == detail)
                return false;
            sLastComeFailWorld = worldId;
            sLastComeFailDetail = detail;
            return true;
        }

        static bool shouldLogGoFailure(s32 worldId) {
            if (sLastGoFailWorld == worldId)
                return false;
            sLastGoFailWorld = worldId;
            return true;
        }

        static s32 getMappedWorldForChangeStageId(const GameDataFile* file, s32 currentWorldId, const char* changeStageId) {
            if (!isActive())
                return -1;

            if (isComeChangeStageId(changeStageId)) {
                const s32 destinationIndex = findDestinationIndexByWorldId(currentWorldId);
                if (!isValidPaintingIndex(destinationIndex)) {
                    if (shouldLogComeFailure(currentWorldId, -1))
                        Randomizer::logPush("painting getMapped Come: world=%d -> no destIdx", currentWorldId);
                    return -1;
                }
                const s32 sourceIndex = getMappedSourceIndexForDestination(destinationIndex);
                if (!isPaintingUnlockedInternal(file, sourceIndex)) {
                    if (shouldLogComeFailure(currentWorldId, destinationIndex))
                        Randomizer::logPush("painting getMapped Come: world=%d destIdx=%d srcIdx=%d LOCKED",
                            currentWorldId, destinationIndex, sourceIndex);
                    return -1;
                }
                // Resolving again is a real state change, so let the next failure
                // report itself rather than being swallowed as a duplicate.
                sLastComeFailWorld = -2;
                sLastComeFailDetail = -2;
                const s32 result = getSourceWorldId(sourceIndex);
                return result;
            }

            if (isGoChangeStageIdForWorld(currentWorldId, changeStageId)) {
                const s32 sourceIndex = findSourceIndexByWorldId(currentWorldId);
                if (!isValidPaintingIndex(sourceIndex) || !isPaintingUnlockedInternal(file, sourceIndex)) {
                    if (shouldLogGoFailure(currentWorldId))
                        Randomizer::logPush("painting getMapped Go: world=%d -> no srcIdx or LOCKED", currentWorldId);
                    return -1;
                }
                sLastGoFailWorld = -2;
                const s32 destinationIndex = getMappedDestinationIndex(sourceIndex);
                const s32 result = getDestinationWorldId(destinationIndex);
                return result;
            }

            return -1;
        }

        static bool canResolvePaintingWorld(const GameDataFile* file, s32 currentWorldId, const char* changeStageId) {
            return getMappedWorldForChangeStageId(file, currentWorldId, changeStageId) >= 0;
        }

        static const char* tryGetCurrentStageNameFromSequence() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;

            HakoniwaSequence* sequence = static_cast<HakoniwaSequence*>(system->mSequence);
            const char* stageName = sequence->mStageName.cstr();
            if (stageName && stageName[0])
                return stageName;

            if (!sequence->mGameDataHolderAccessor.mData)
                return nullptr;
            return GameDataFunction::getCurrentStageName(sequence->mGameDataHolderAccessor);
        }

        static const char* tryGetCurrentStageNameForActor(const WorldWarpHole* worldWarpHole) {
            const char* stageName = tryGetCurrentStageNameFromSequence();
            if (stageName && stageName[0])
                return stageName;

            if (!worldWarpHole)
                return nullptr;
            GameDataHolderAccessor accessor(worldWarpHole);
            return GameDataFunction::getCurrentStageName(accessor);
        }

        static s32 getCurrentWorldIdForPaintingActor(
            const WorldWarpHole* worldWarpHole, const char* changeStageId, const char** outStageName) {
            const char* stageName = tryGetCurrentStageNameForActor(worldWarpHole);
            const GameDataFile* file = tryGetGameDataFile(worldWarpHole);
            if (outStageName)
                *outStageName = stageName;

            // No log on this fast path: it runs every frame via the WorldWarpHole
            // control hook and floods the emulator log otherwise.
            const s32 stageWorldId = findWorldIdByStageName(stageName);
            if (stageWorldId >= 0)
                return stageWorldId;

            const s32 actorWorldId = GameDataFunction::getCurrentWorldId(worldWarpHole);
            if (canResolvePaintingWorld(file, actorWorldId, changeStageId)) {
                Randomizer::logPush("painting getCurWorld id=%s: stage=%s stageId=-1 -> actorWorldId=%d",
                    changeStageId ? changeStageId : "?", stageName ? stageName : "?", actorWorldId);
                return actorWorldId;
            }

            if (canResolvePaintingWorld(file, sPendingLandingWorldId, changeStageId)) {
                Randomizer::logPush("painting getCurWorld id=%s: stage=%s stageId=-1 actorId=%d -> pendingWorldId=%d",
                    changeStageId ? changeStageId : "?", stageName ? stageName : "?", actorWorldId, sPendingLandingWorldId);
                return sPendingLandingWorldId;
            }

            Randomizer::logPush("painting getCurWorld id=%s: stage=%s stageId=-1 -> fallback actorWorldId=%d (pending=%d)",
                changeStageId ? changeStageId : "?", stageName ? stageName : "?", actorWorldId, sPendingLandingWorldId);
            return actorWorldId;
        }

        static void rememberPendingLanding(const char* stageName) {
            sPendingLandingStage = stageName;
            sPendingLandingWorldId = findWorldIdByStageName(stageName);
        }

        static void applyPendingLandingWorld(GameDataFile* file) {
            if (!file || sPendingLandingWorldId < 0)
                return;

            file->customSetWorldWarpHoleDisplayWorldId(sPendingLandingWorldId);
            Randomizer::logPush("painting landing stamp: stage=%s world=%d cur=%d disp=%d warp=%d",
                sPendingLandingStage ? sPendingLandingStage : "???", sPendingLandingWorldId,
                file->getCurrentWorldId(), file->getCurrentWorldIdForDisp(),
                file->customGetWorldWarpHoleDisplayWorldId());
        }

        static bool tryGetLabelWorldId(const GameDataFile* file, s32 currentWorldId, const char* changeStageId, s32* outWorldId) {
            if (canResolvePaintingWorld(file, currentWorldId, changeStageId)) {
                if (outWorldId)
                    *outWorldId = currentWorldId;
                return true;
            }
            if (canResolvePaintingWorld(file, sPendingLandingWorldId, changeStageId)) {
                if (outWorldId)
                    *outWorldId = sPendingLandingWorldId;
                return true;
            }
            return false;
        }

        static bool tryCalcPaintingWarpInfo(
            const GameDataHolder* holder, s32 currentWorldId, const char* changeStageId,
            const GameDataHolder::WorldWarpHoleInfo** outInfo, s32* outMappedWorld) {
            if (outInfo)
                *outInfo = nullptr;
            if (outMappedWorld)
                *outMappedWorld = -1;
            if (!holder || !isActive())
                return false;

            const bool isReturn = isComeChangeStageId(changeStageId);
            const s32 mappedWorld = getMappedWorldForChangeStageId(
                tryGetGameDataFile(holder), currentWorldId, changeStageId);
            if (mappedWorld < 0)
                return false;

            const s32 currentWarpId = holder->calcWorldWarpHoleIdFromWorldId(currentWorldId);
            const char* baseName = isReturn ? "Go" : "Come";
            const GameDataHolder::WorldWarpHoleInfo* info = findWarpInfo(holder, mappedWorld, baseName, currentWarpId);
            if (!info)
                info = findWarpInfo(holder, mappedWorld, baseName, currentWorldId);
            if (!info)
                return false;

            if (outInfo)
                *outInfo = info;
            if (outMappedWorld)
                *outMappedWorld = mappedWorld;
            return true;
        }

        static bool isPaintingWarpRequest(const GameDataFile* file, s32 currentWorldId, const char* changeStageId) {
            return getMappedWorldForChangeStageId(file, currentWorldId, changeStageId) >= 0;
        }

        static void setDefaultDestinationSlots() {
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
                sDestinationBySource[i] = i;
        }

        static void shuffleDestinationSlots(Randomizer::Rng& rng) {
            setDefaultDestinationSlots();
            for (s32 i = LD::sPaintingLinkCount - 1; i > 0; i--) {
                const s32 j = (s32)rng.nextModulo((u32)(i + 1));
                const s32 tmp = sDestinationBySource[i];
                sDestinationBySource[i] = sDestinationBySource[j];
                sDestinationBySource[j] = tmp;
            }
        }

        static HkTrampoline<s32, const GameDataHolder*, s32>
            calcWorldWarpHoleDestIdHook = hk::hook::trampoline([](const GameDataHolder* holder, s32 sourceWarpId) -> s32 {
                if (isActive()) {
                    const s32 sourceIndex = findSourceIndexByWarpId(holder, sourceWarpId);
                    if (isValidPaintingIndex(sourceIndex)) {
                        if (!isPaintingUnlockedInternal(tryGetGameDataFile(holder), sourceIndex))
                            return -1;
                        const s32 destinationIndex = getMappedDestinationIndex(sourceIndex);
                        const s32 destinationWorld = getPaintingDestinationWorldId(destinationIndex);
                        const s32 destinationWarpId = holder->calcWorldWarpHoleIdFromWorldId(destinationWorld);
                        if (destinationWarpId >= 0)
                            return destinationWarpId;
                    }
                }
                return calcWorldWarpHoleDestIdHook.orig(holder, sourceWarpId);
            });

        static HkTrampoline<void, const GameDataHolder*, sead::BufferedSafeString*,
            sead::BufferedSafeString*, const char*, s32>
            calcWorldWarpHoleLabelAndStageNameHook = hk::hook::trampoline(
                [](const GameDataHolder* holder, sead::BufferedSafeString* label,
                    sead::BufferedSafeString* stageName, const char* changeStageId,
                    s32 currentWorldId) -> void {
                    const GameDataFile* file = tryGetGameDataFile(holder);
                    // Kingdom progression mutates the world-map/warp mirrors. The
                    // physical stage is authoritative for a painting actor, so prefer
                    // it when it can resolve this side of a painting link.
                    const s32 stageWorldId = findWorldIdByStageName(tryGetCurrentStageNameFromSequence());
                    const s32 lookupWorldId = canResolvePaintingWorld(file, stageWorldId, changeStageId)
                        ? stageWorldId
                        : currentWorldId;
                    s32 labelWorldId = lookupWorldId;
                    const bool resolved = tryGetLabelWorldId(
                        file, lookupWorldId, changeStageId, &labelWorldId);
                    Randomizer::logPush("painting label hook: id=%s rawWorld=%d stageWorld=%d resolvedLabel=%d pending=%d (resolved=%d)",
                        changeStageId ? changeStageId : "?",
                        currentWorldId, stageWorldId, labelWorldId,
                        sPendingLandingWorldId, (int)resolved);

                    const GameDataHolder::WorldWarpHoleInfo* info = nullptr;
                    s32 mappedWorld = -1;
                    if (label && stageName && tryCalcPaintingWarpInfo(holder, labelWorldId, changeStageId, &info, &mappedWorld)) {
                        Randomizer::logPush("painting label: id=%s labelWorld=%d -> mappedWorld=%d name=%s stage=%s",
                            changeStageId ? changeStageId : "?", labelWorldId, mappedWorld,
                            info->name.cstr(), info->stageName.cstr());
                        label->format("%s", info->name.cstr());
                        stageName->format("%s", info->stageName.cstr());
                        // Vanilla ends this function by storing the resolved destination
                        // world into GameDataFile (the slot consumed as the new current
                        // world after a warp-hole transition). Returning early skips that
                        // store, leaving the post-warp current world stale - later
                        // warp-hole lookups then run with a bogus world id and crash in
                        // vanilla code. Replicate the store.
                        if (GameDataFile* dataFile = tryGetGameDataFile(holder))
                            dataFile->customSetWorldWarpHoleDisplayWorldId(mappedWorld);
                        return;
                    }
                    if (label && stageName && isPaintingWarpRequest(file, labelWorldId, changeStageId)) {
                        label->clear();
                        stageName->clear();
                        Randomizer::logPush("painting label: missing info world=%d raw=%d pending=%d id=%s",
                            labelWorldId, currentWorldId, sPendingLandingWorldId,
                            changeStageId ? changeStageId : "???");
                        return;
                    }
                    if (label && stageName && isActive()
                        && isPaintingChangeStageIdForWorld(labelWorldId, changeStageId)) {
                        label->clear();
                        stageName->clear();
                        Randomizer::logPush("painting label: blocked vanilla world=%d id=%s",
                            currentWorldId, changeStageId ? changeStageId : "???");
                        return;
                    }

                    calcWorldWarpHoleLabelAndStageNameHook.orig(
                        holder, label, stageName, changeStageId, currentWorldId);
                });

        static HkTrampoline<s32, const WorldWarpHole*, const char*>
            calcWorldIdByWorldWarpHoleHook = hk::hook::trampoline(
                [](const WorldWarpHole* worldWarpHole, const char* changeStageId) -> s32 {
                    if (!worldWarpHole)
                        return calcWorldIdByWorldWarpHoleHook.orig(worldWarpHole, changeStageId);

                    const char* stageName = nullptr;
                    const s32 currentWorldId = getCurrentWorldIdForPaintingActor(
                        worldWarpHole, changeStageId, &stageName);
                    const s32 mappedWorld = getMappedWorldForChangeStageId(
                        tryGetGameDataFile(worldWarpHole), currentWorldId, changeStageId);
                    if (mappedWorld >= 0) {
                        Randomizer::logPush("painting worldid: stage=%s curWorld=%d pending=%d id=%s -> mappedWorld=%d mWorldId=%d",
                            stageName ? stageName : "???", currentWorldId, sPendingLandingWorldId,
                            changeStageId ? changeStageId : "???", mappedWorld, worldWarpHole->mWorldId);
                        return mappedWorld;
                    }

                    if (isActive() && isPaintingChangeStageIdForWorld(currentWorldId, changeStageId)) {
                        // Fall back to vanilla for unresolved paintings. Returning -1 here
                        // used to send the actor to its Close nerve in WorldWarpHole::init
                        // (init closes any warp hole whose world id is negative), which left
                        // the painting permanently locked despite the QoL force-unlock
                        // patches. An unresolved painting has no remap, so vanilla routing
                        // is correct for it.
                        Randomizer::logPush("painting worldid: UNRESOLVED stage=%s curWorld=%d pending=%d id=%s mWorldId=%d (vanilla fallback)",
                            stageName ? stageName : "???", currentWorldId, sPendingLandingWorldId,
                            changeStageId ? changeStageId : "???", worldWarpHole->mWorldId);
                    }

                    return calcWorldIdByWorldWarpHoleHook.orig(worldWarpHole, changeStageId);
                });

        static void startWorldIdMtp(WorldWarpHole* worldWarpHole, s32 worldId) {
            using Fn = void (*)(al::LiveActor*, const char*, f32);
            Fn startMtp = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + 0x8d44a8);
            startMtp(worldWarpHole, "WorldId", (f32)worldId);
        }

        static void applyWorldWarpHoleDisplay(WorldWarpHole* worldWarpHole) {
            if (!worldWarpHole)
                return;

            const s32 currentWorldId = getCurrentWorldIdForPaintingActor(
                worldWarpHole, worldWarpHole->mChangeStageId, nullptr);
            const s32 mappedWorld = getMappedWorldForChangeStageId(
                tryGetGameDataFile(worldWarpHole), currentWorldId, worldWarpHole->mChangeStageId);
            if (mappedWorld < 0 || worldWarpHole->mWorldId == mappedWorld)
                return;

            Randomizer::logPush("painting applyDisplay: id=%s curWorld=%d mappedWorld=%d (was mWorldId=%d)",
                worldWarpHole->mChangeStageId ? worldWarpHole->mChangeStageId : "?",
                currentWorldId, mappedWorld, worldWarpHole->mWorldId);
            worldWarpHole->mWorldId = mappedWorld;
            startWorldIdMtp(worldWarpHole, mappedWorld);
        }

        static HkTrampoline<void, WorldWarpHole*, const al::ActorInitInfo&>
            worldWarpHoleInitHook = hk::hook::trampoline(
                [](WorldWarpHole* worldWarpHole, const al::ActorInitInfo& info) -> void {
                    worldWarpHoleInitHook.orig(worldWarpHole, info);
                    applyWorldWarpHoleDisplay(worldWarpHole);
                });

        static HkTrampoline<void, WorldWarpHole*>
            worldWarpHoleControlHook = hk::hook::trampoline([](WorldWarpHole* worldWarpHole) -> void {
                worldWarpHoleControlHook.orig(worldWarpHole);
                applyWorldWarpHoleDisplay(worldWarpHole);
            });

        // True when stageName lies in worldId (by logic-stage kingdom or main
        // stage). The game's warp infos use home stages while logic nodes can
        // live in zones (e.g. LakeWorldTownZone), so exact stage comparison is
        // not enough.
        static bool stageMatchesWorld(const char* stageName, s32 worldId) {
            return worldId >= 0 && findWorldIdByStageName(stageName) == worldId;
        }

        static const char* getFallbackPaintingWarpStage(s32 worldId, const char* nodeStage) {
            if (nodeStage && nodeStage[0] && WorldMeta::findByMainStage(nodeStage))
                return nodeStage;
            const WorldMeta::Entry* world = WorldMeta::getByWorldId(worldId);
            return world ? world->mainStage : nodeStage;
        }

        static bool applyWarpInfoDestination(
            GameDataFile* file, ChangeStageInfo* changeInfo, s32 targetWorldId,
            const char* targetBaseName, s32 fromWorldId) {
            if (!file || !changeInfo || !targetBaseName)
                return false;

            const GameDataHolder* holder = file->getGameDataHolder();
            if (!holder)
                return false;

            const s32 fromWarpId = holder->calcWorldWarpHoleIdFromWorldId(fromWorldId);
            const GameDataHolder::WorldWarpHoleInfo* warpInfo = findWarpInfo(holder, targetWorldId, targetBaseName, fromWarpId);
            if (!warpInfo)
                warpInfo = findWarpInfo(holder, targetWorldId, targetBaseName, fromWorldId);
            if (!warpInfo)
                return false;

            changeInfo->mChangeStageName.copy(warpInfo->stageName.cstr());
            changeInfo->mChangeStageId.copy(warpInfo->name.cstr());
            changeInfo->mPlacementString.copy(warpInfo->name.cstr());
            rememberPendingLanding(warpInfo->stageName.cstr());
            file->customSetWorldWarpHoleDisplayWorldId(targetWorldId);
            Randomizer::logPush("painting apply warp info: fromWorld=%d targetWorld=%d name=%s stage=%s",
                fromWorldId, targetWorldId, warpInfo->name.cstr(), warpInfo->stageName.cstr());
            return true;
        }

        static bool matchesForwardTransition(s32 sourceIndex, const char* currentStage,
            const ChangeStageInfo* info) {
            if (!isForwardPaintingChange(info))
                return false;

            const char* sourceStage = getSourceStage(sourceIndex);
            if (!isSameString(currentStage, sourceStage)
                && !stageMatchesWorld(currentStage, getPaintingSourceWorldId(sourceIndex)))
                return false;

            const s32 mappedDestinationIndex = getMappedDestinationIndex(sourceIndex);
            const char* destName = info->mChangeStageName.cstr();
            return isSameString(destName, getDestinationStage(sourceIndex))
                || isSameString(destName, getDestinationStage(mappedDestinationIndex))
                || stageMatchesWorld(destName, getPaintingDestinationWorldId(sourceIndex))
                || stageMatchesWorld(destName, getPaintingDestinationWorldId(mappedDestinationIndex));
        }

        static bool matchesReturnTransition(s32 destinationIndex, const char* currentStage,
            const ChangeStageInfo* info) {
            if (!isReturnPaintingChange(info))
                return false;

            const s32 currentWorldId = findWorldIdByStageName(currentStage);
            if (currentWorldId < 0 || currentWorldId != getPaintingDestinationWorldId(destinationIndex))
                return false;

            const s32 mappedSourceIndex = getMappedSourceIndexForDestination(destinationIndex);
            const char* destName = info->mChangeStageName.cstr();
            return isSameString(destName, getSourceStage(destinationIndex))
                || isSameString(destName, getSourceStage(mappedSourceIndex))
                || stageMatchesWorld(destName, getPaintingSourceWorldId(destinationIndex))
                || stageMatchesWorld(destName, getPaintingSourceWorldId(mappedSourceIndex));
        }

        static void applyForwardDestination(GameDataFile* file, ChangeStageInfo* info, s32 sourceIndex) {
            const s32 destinationIndex = getMappedDestinationIndex(sourceIndex);
            const s32 sourceWorld = getPaintingSourceWorldId(sourceIndex);
            const s32 destinationWorld = getPaintingDestinationWorldId(destinationIndex);
            if (applyWarpInfoDestination(file, info, destinationWorld, "Come", sourceWorld))
                return;

            const char* destinationStage = getFallbackPaintingWarpStage(
                destinationWorld, getDestinationStage(destinationIndex));
            if (!destinationStage)
                return;

            info->mChangeStageName.copy(destinationStage);
            info->mChangeStageId.copy("Come");
            info->mPlacementString.copy("Come");
            rememberPendingLanding(destinationStage);
        }

        static void applyReturnDestination(GameDataFile* file, ChangeStageInfo* info, s32 destinationIndex) {
            const s32 sourceIndex = getMappedSourceIndexForDestination(destinationIndex);
            const s32 destinationWorld = getPaintingDestinationWorldId(destinationIndex);
            const s32 sourceWorld = getPaintingSourceWorldId(sourceIndex);
            if (applyWarpInfoDestination(file, info, sourceWorld, "Go", destinationWorld))
                return;

            const char* sourceStage = getFallbackPaintingWarpStage(
                sourceWorld, getSourceStage(sourceIndex));
            if (!sourceStage)
                return;

            info->mChangeStageName.copy(sourceStage);
            info->mChangeStageId.copy("Go");
            info->mPlacementString.copy("Go");
            rememberPendingLanding(sourceStage);
        }

        static void blockPaintingChange(ChangeStageInfo* info) {
            if (!info)
                return;
            info->mChangeStageName.copy("");
            info->mChangeStageId.copy("");
            info->mPlacementString.copy("");
            rememberPendingLanding(nullptr);
        }

    } // namespace

    static u32 seedForAttempt(u32 seed, u32 attempt) {
        return seed ^ (attempt * 0x27D4EB2Fu);
    }

    void buildRemapTable(u32 seed, u32 attempt) {
        const bool paintingRandoEnabled = Randomizer::isPaintingRandoEnabled();
        if (sInitialized && sLastSeed == seed && sLastAttempt == attempt && sLastPaintingRandoEnabled == paintingRandoEnabled)
            return;

        const u32 attemptSeed = seedForAttempt(seed, attempt);
        Randomizer::Rng rng = Randomizer::makeRng(attemptSeed, Randomizer::RngStream::Painting);
        if (paintingRandoEnabled) {
            shuffleDestinationSlots(rng);
            for (s32 selfMapAttempt = 0; selfMapAttempt < 32 && hasSelfMapping(); selfMapAttempt++)
                shuffleDestinationSlots(rng);
        } else {
            setDefaultDestinationSlots();
        }

        rebuildInverse();

        // Seed-tie which painting link each Cloud moon-credit slot unlocks. A separate rng
        // keeps this independent of the destination shuffle's variable retry count, and the
        // permutation preserves full link coverage even when Cloud credits < links.
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
            sCloudLinkOrder[i] = i;
        Randomizer::Rng cloudRng = Randomizer::makeRng(attemptSeed ^ 0xC10D11A6u, Randomizer::RngStream::Painting);
        for (s32 i = LD::sPaintingLinkCount - 1; i > 0; i--) {
            const s32 j = (s32)cloudRng.nextModulo((u32)(i + 1));
            const s32 tmp = sCloudLinkOrder[i];
            sCloudLinkOrder[i] = sCloudLinkOrder[j];
            sCloudLinkOrder[j] = tmp;
        }

        sInitialized = true;
        sLastSeed = seed;
        sLastAttempt = attempt;
        sLastPaintingRandoEnabled = paintingRandoEnabled;
        // Both tables above decide which cloud moons unlock which link.
        invalidateUnlockCache();

        // This ~12-line block is a pure function of (seed, attempt); report each
        // distinct pair once per session. See Randomizer::shouldNarrateBuild.
        static const char sPaintingTableLogTag = 0;
        const bool shouldLog = Randomizer::shouldNarrateBuild(&sPaintingTableLogTag, seed, attempt);

        if (shouldLog) {
            Randomizer::logPush("painting table: seed=%u attempt=%u links=%d mode=%s", seed, attempt, LD::sPaintingLinkCount,
                paintingRandoEnabled ? "shuffle" : "fixed");
            for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
                Randomizer::logPush("painting: %s -> %s",
                    getSourceStage(i) ? getSourceStage(i) : "???",
                    getDestinationStage(sDestinationBySource[i]) ? getDestinationStage(sDestinationBySource[i]) : "???");
            }
        }

        const s32 freeLinkCount = getFreePaintingLinkCount();
        if (freeLinkCount > 0 && shouldLog) {
            Randomizer::logPush("painting: Cloud credits=%d links=%d -> %d free link(s) unlocked from start",
                getCloudPaintingCreditCount(), LD::sPaintingLinkCount, freeLinkCount);
            for (s32 i = 0; i < freeLinkCount; i++)
                Randomizer::logPush("painting: free link -> %s", getSourceStage(sCloudLinkOrder[i]) ? getSourceStage(sCloudLinkOrder[i]) : "???");
        }
    }

    void clear() {
        sInitialized = false;
        sLastSeed = 0xFFFFFFFF;
        sLastAttempt = 0xFFFFFFFF;
        sLastPaintingRandoEnabled = false;
        sPendingLandingStage = nullptr;
        sPendingLandingWorldId = -1;
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            sDestinationBySource[i] = i;
            sSourceByDestination[i] = i;
            sCloudLinkOrder[i] = i;
        }
        // A reshuffled link table remaps which cloud moons feed which painting,
        // so cached unlock answers no longer describe this seed.
        invalidateUnlockCache();
    }

    bool isInitialized() { return sInitialized; }

    void installHooks() {
        calcWorldWarpHoleDestIdHook.installAtSym<"_ZNK14GameDataHolder23calcWorldWarpHoleDestIdEi">();
        calcWorldWarpHoleLabelAndStageNameHook.installAtMainOffset(0x53100c);
        calcWorldIdByWorldWarpHoleHook.installAtMainOffset(0x532140);
        worldWarpHoleInitHook.installAtMainOffset(0x34d314);
        worldWarpHoleControlHook.installAtMainOffset(0x34d7a8);
        // Paintings use the vanilla DemoWorldWarpHole flow (warp animation).
        // The label hook's destination-world store (see
        // calcWorldWarpHoleLabelAndStageNameHook) keeps the post-warp current
        // world correct, which vanilla normally writes in the code path our
        // early return skips. The kingdom-exit crash that used to fire in the
        // demo flow's world resource teardown was dangling moon-model archives
        // (from the since-removed custom moon-archive heap), not the demo
        // machinery itself.
    }

    s32 getPaintingCount() { return LD::sPaintingLinkCount; }

    s32 getSourceNode(s32 index) {
        if (!isValidPaintingIndex(index))
            return -1;
        return LD::sPaintingLinks[index].sourceNode;
    }

    s32 getDestinationNode(s32 index) {
        if (!isValidPaintingIndex(index))
            return -1;
        return LD::sPaintingLinks[index].destinationNode;
    }

    s32 getSourceWorldId(s32 index) {
        return getPaintingSourceWorldId(index);
    }

    s32 getDestinationWorldId(s32 index) {
        return getPaintingDestinationWorldId(index);
    }

    s32 getMappedDestinationIndex(s32 sourceIndex) {
        if (!isValidPaintingIndex(sourceIndex))
            return -1;
        if (!sInitialized || !Randomizer::isPaintingRandoEnabled())
            return sourceIndex;
        return sDestinationBySource[sourceIndex];
    }

    s32 getMappedSourceIndexForDestination(s32 destinationIndex) {
        if (!isValidPaintingIndex(destinationIndex))
            return -1;
        if (!sInitialized || !Randomizer::isPaintingRandoEnabled())
            return destinationIndex;
        return sSourceByDestination[destinationIndex];
    }

    s32 getMappedDestinationNode(s32 sourceIndex) {
        const s32 destinationIndex = getMappedDestinationIndex(sourceIndex);
        return getDestinationNode(destinationIndex);
    }

    s32 getMappedReturnSourceNode(s32 destinationIndex) {
        const s32 sourceIndex = getMappedSourceIndexForDestination(destinationIndex);
        return getSourceNode(sourceIndex);
    }

    s32 getMappedDestinationWorldIdForSourceWorld(s32 sourceWorldId) {
        const s32 sourceIndex = findSourceIndexByWorldId(sourceWorldId);
        if (!isValidPaintingIndex(sourceIndex))
            return -1;

        const s32 destinationIndex = getMappedDestinationIndex(sourceIndex);
        return getDestinationWorldId(destinationIndex);
    }

    s32 getMappedSourceWorldIdForDestinationWorld(s32 destinationWorldId) {
        const s32 destinationIndex = findDestinationIndexByWorldId(destinationWorldId);
        if (!isValidPaintingIndex(destinationIndex))
            return -1;

        const s32 sourceIndex = getMappedSourceIndexForDestination(destinationIndex);
        return getSourceWorldId(sourceIndex);
    }

    bool cloudMoonUnlocksPainting(const ShineDB::ShineEntry& moon, s32 paintingIndex) {
        return cloudMoonUnlocksPaintingIndex(moon, paintingIndex);
    }

    s32 getCloudMoonPaintingUnlockCount(const ShineDB::ShineEntry& moon) {
        const s32 cloudMoonSlot = getCloudMoonSlot(moon);
        if (cloudMoonSlot < 0)
            return 0;
        return getCloudMoonPaintingUnlockIndexBySlot(cloudMoonSlot) >= 0
            ? getCloudMoonPaintingCredit(moon)
            : 0;
    }

    s32 getCloudMoonPaintingUnlockIndex(const ShineDB::ShineEntry& moon, s32 unlockIndex) {
        const s32 unlockCount = getCloudMoonPaintingCredit(moon);
        if (unlockIndex < 0 || unlockIndex >= unlockCount)
            return -1;
        const s32 firstSlot = findCloudMoonSlot(moon);
        return getCloudMoonPaintingUnlockIndexBySlot(
            firstSlot >= 0 ? firstSlot + unlockIndex : -1);
    }

    s32 getCloudMoonSlot(const ShineDB::ShineEntry& moon) {
        return findCloudMoonSlot(moon);
    }

    bool isPaintingLinkFree(s32 paintingIndex) {
        return isFreePaintingLinkIndex(paintingIndex);
    }

    s32 getCloudLinkOrder(s16* outOrder, s32 maxCount) {
        const s32 count = LD::sPaintingLinkCount;
        if (!outOrder || maxCount <= 0)
            return count;
        const s32 writeCount = maxCount < count ? maxCount : count;
        for (s32 i = 0; i < writeCount; i++)
            outOrder[i] = static_cast<s16>(sCloudLinkOrder[i]);
        return count;
    }

    bool setCloudLinkOrder(const s16* order, s32 count) {
        if (!order || count != LD::sPaintingLinkCount)
            return false;

        bool seen[LD::sPaintingLinkCount] = {};
        for (s32 i = 0; i < count; i++) {
            const s32 link = order[i];
            if (!isValidPaintingIndex(link) || seen[link])
                return false;
            seen[link] = true;
        }

        for (s32 i = 0; i < count; i++)
            sCloudLinkOrder[i] = order[i];
        // Restoring a persisted order remaps cloud moon -> link just as
        // buildRemapTable does.
        invalidateUnlockCache();
        return true;
    }

    bool getPaintingUnlockKingdomNames(s32 paintingIndex, const char** sourceKingdom, const char** destinationKingdom) {
        if (sourceKingdom)
            *sourceKingdom = "???";
        if (destinationKingdom)
            *destinationKingdom = "???";
        if (!isValidPaintingIndex(paintingIndex))
            return false;

        const s32 sourceWorld = getSourceWorldId(paintingIndex);
        const s32 destinationIndex = getMappedDestinationIndex(paintingIndex);
        const s32 destinationWorld = getDestinationWorldId(destinationIndex);
        if (sourceKingdom)
            *sourceKingdom = WorldMeta::getDisplayNameForWorldId(sourceWorld);
        if (destinationKingdom)
            *destinationKingdom = WorldMeta::getDisplayNameForWorldId(destinationWorld);
        return sourceWorld >= 0 && destinationWorld >= 0;
    }

    void requestFreeLinkAnnouncement() {
        sFreeLinkAnnouncementPending = true;
    }

    // Consumed once a stage scene exists (see DebugMenuHooks), mirroring
    // AbilityOverlay::tryQueueStartingEquipment - queuing immediately at new-game
    // creation would draw the overlay during the save-transition/loading screen.
    // startDelay chains the announcement after that overlay's ability/capture
    // display so the two don't draw on top of each other.
    void tryQueueFreeLinkAnnouncement(s32 startDelay) {
        if (!sFreeLinkAnnouncementPending)
            return;
        sFreeLinkAnnouncementPending = false;

        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return;

        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            if (!isFreePaintingLinkIndex(i))
                continue;

            const char* sourceKingdom = nullptr;
            const char* destinationKingdom = nullptr;
            if (!getPaintingUnlockKingdomNames(i, &sourceKingdom, &destinationKingdom))
                continue;
            PaintingUnlockOverlay::queueFree(sourceKingdom, destinationKingdom, startDelay);
        }
    }

    bool isPaintingUnlocked(const GameDataFile* file, s32 paintingIndex) {
        return isPaintingUnlockedInternal(file, paintingIndex);
    }

    void invalidateUnlockCache() {
        sUnlockCacheRevision++;
        // 0 is the "never computed" stamp, so it must never be a live revision.
        if (sUnlockCacheRevision == 0)
            sUnlockCacheRevision = 1;
        sUnlockCacheFile = nullptr;
        sUnlockCacheRoleLayoutHash = 0;
        // A newly unlocked painting is also a new steady state for the resolve
        // logs above.
        sLastComeFailWorld = -2;
        sLastComeFailDetail = -2;
        sLastGoFailWorld = -2;
    }

    bool isWorldWarpHoleUnlocked(const WorldWarpHole* worldWarpHole) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return true;
        if (!worldWarpHole || !isActive())
            return false;

        const GameDataFile* file = tryGetGameDataFile(worldWarpHole);
        const char* changeStageId = worldWarpHole->mChangeStageId;
        const s32 currentWorldId = getCurrentWorldIdForPaintingActor(worldWarpHole, changeStageId, nullptr);

        if (isGoChangeStageIdForWorld(currentWorldId, changeStageId)) {
            const s32 sourceIndex = findSourceIndexByWorldId(currentWorldId);
            return isPaintingUnlockedInternal(file, sourceIndex);
        }

        if (isComeChangeStageId(changeStageId)) {
            const s32 destinationIndex = findDestinationIndexByWorldId(currentWorldId);
            if (!isValidPaintingIndex(destinationIndex))
                return false;
            return isPaintingUnlockedInternal(file, getMappedSourceIndexForDestination(destinationIndex));
        }

        return false;
    }

    bool isAnyPaintingUnlockedForWorld(const GameDataFile* file, s32 worldId) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return true;
        if (!isActive() || worldId < 0)
            return false;

        for (s32 i = 0; i < LD::sPaintingLinkCount; i++) {
            const s32 destinationIndex = getMappedDestinationIndex(i);
            if (getSourceWorldId(i) != worldId && getDestinationWorldId(destinationIndex) != worldId)
                continue;
            if (isPaintingUnlockedInternal(file, i))
                return true;
        }
        return false;
    }

    s32 calcUnlockedPaintingCount(const GameDataFile* file) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return 999;

        s32 count = 0;
        for (s32 i = 0; i < LD::sPaintingLinkCount; i++)
            if (isPaintingUnlockedInternal(file, i))
                count++;
        return count;
    }

    bool tryApplyPaintingChange(GameDataFile* file, const char* currentStage, ChangeStageInfo* info) {
        if (!sInitialized || !currentStage || !info || info->isEmpty())
            return false;

        for (s32 sourceIndex = 0; sourceIndex < LD::sPaintingLinkCount; sourceIndex++) {
            if (!matchesForwardTransition(sourceIndex, currentStage, info))
                continue;

            if (!isPaintingUnlockedInternal(file, sourceIndex)) {
                Randomizer::logPush("painting forward locked: %s", currentStage);
                blockPaintingChange(info);
                return true;
            }

            const char* vanillaDestination = getDestinationStage(sourceIndex);
            applyForwardDestination(file, info, sourceIndex);
            ArrivalLog::recordPaintingArrival(sourceIndex, true);
            Bingo::recordEvent({ Bingo::GoalType::Paintings, 1, -1,
                0x56000000u | static_cast<u32>(sourceIndex),
                Bingo::EventOrigin::Local });
            Randomizer::logPush("painting forward: %s -> %s (was %s)",
                currentStage,
                info->mChangeStageName.cstr(),
                vanillaDestination ? vanillaDestination : "???");
            applyPendingLandingWorld(file);
            return true;
        }

        for (s32 destinationIndex = 0; destinationIndex < LD::sPaintingLinkCount; destinationIndex++) {
            if (!matchesReturnTransition(destinationIndex, currentStage, info))
                continue;

            const s32 sourceIndex = getMappedSourceIndexForDestination(destinationIndex);
            if (!isPaintingUnlockedInternal(file, sourceIndex)) {
                Randomizer::logPush("painting return locked: %s", currentStage);
                blockPaintingChange(info);
                return true;
            }

            const char* vanillaSource = getSourceStage(destinationIndex);
            applyReturnDestination(file, info, destinationIndex);
            ArrivalLog::recordPaintingArrival(destinationIndex, false);
            Bingo::recordEvent({ Bingo::GoalType::Paintings, 1, -1,
                0x56000000u | static_cast<u32>(sourceIndex),
                Bingo::EventOrigin::Local });
            Randomizer::logPush("painting return: %s -> %s (was %s)",
                currentStage,
                info->mChangeStageName.cstr(),
                vanillaSource ? vanillaSource : "???");
            applyPendingLandingWorld(file);
            return true;
        }

        const s32 currentWorldId = findWorldIdByStageName(currentStage);
        if (isPaintingChangeStageIdForWorld(currentWorldId, info->mChangeStageId.cstr())
            || isPaintingChangeStageIdForWorld(currentWorldId, info->mPlacementString.cstr())) {
            Randomizer::logPush("painting change unresolved: cur=%s dest=%s id=%s place=%s",
                currentStage ? currentStage : "???",
                info->mChangeStageName.cstr(),
                info->mChangeStageId.cstr(),
                info->mPlacementString.cstr());
        }

        return false;
    }

} // namespace PaintingRando
