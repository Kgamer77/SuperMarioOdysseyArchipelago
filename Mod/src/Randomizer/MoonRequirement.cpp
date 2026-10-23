#include "Randomizer/MoonRequirement.h"

#include "Randomizer/KingdomProgress.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/WorldMeta.h"

#include "Library/Base/StringUtil.h"
#include "game/MapObj/CapMessageShowInfo.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "prim/seadSafeString.h"

namespace MoonRequirement {
    namespace {
        namespace LD = Randomizer::LogicData;

        static_assert(WorldMeta::sWorldCount == LD::sKingdomCount,
            "World peace scenario table must match WorldMeta kingdom count.");

        constexpr const char* kMoonHomeStage = "MoonWorldHomeStage";
        constexpr const char* kMoonChapelStage = "MoonWorldWeddingRoomStage";
        constexpr const char* kCapMessageFile = "CapMessage";
        constexpr const char* kBlockedMessageLabel = "MoonChurchMoonRequirement";

        enum BlockReason
        {
            BlockReason_None,
            BlockReason_MoonKingdomMoons,
            BlockReason_OtherKingdomRequirements,
            BlockReason_WorldPeace,
            BlockReason_ToadetteMedals,
        };

        s32 sLastRemaining = 0;
        s32 sLastRequirement = 0;
        s32 sLastMedalsRemaining = 0;
        BlockReason sLastBlockReason = BlockReason_None;

        bool isSameString(const char* left, const char* right) {
            return left && right && al::isEqualString(left, right);
        }

        GameDataFile* tryGetFile(GameDataHolderAccessor accessor) {
            if (!accessor.mData)
                return nullptr;
            return accessor->getGameDataFile();
        }

        s32 getCollectedForRequirement(GameDataFile* file, s32 worldId) {
            if (!file || worldId < 0 || worldId >= WorldMeta::sWorldCount)
                return 0;

            const s32 collected = file->getShineNum(worldId);
            return collected > 0 ? collected : 0;
        }

        // The chapel door requires every other kingdom's moon requirement regardless of
        // whether kingdom-order randomization is active.
        bool areOtherKingdomRequirementsSatisfied(GameDataHolderAccessor accessor) {
            GameDataFile* file = tryGetFile(accessor);
            if (file && KingdomProgress::get(file))
                return KingdomProgress::areAllEntriesExceptComplete(
                    file, WorldMeta::WorldId_Moon);

            for (s32 i = 0; i < RequirementRando::getRequirementKingdomCount(); i++) {
                const s32 worldId = RequirementRando::getRequirementKingdomWorldId(i);
                if (worldId == WorldMeta::WorldId_Moon)
                    continue;

                const s32 requirement = RequirementRando::getRequirement(worldId);
                if (requirement <= 0)
                    continue;

                if (getCollectedForRequirement(file, worldId) < requirement)
                    return false;
            }

            return true;
        }

        s32 getEffectiveScenario(GameDataHolderAccessor accessor, s32 worldId) {
            if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
                return -1;

            s32 scenario = Randomizer::getInitialKingdomScenario(worldId);
            if (!accessor.mData)
                return scenario;

            const WorldMeta::Entry* entry = WorldMeta::getByWorldId(worldId);
            if (!entry)
                return scenario;

            const s32 savedScenario = GameDataFunction::getWorldScenarioNo(accessor, entry->scenarioWorldId);
            return savedScenario > scenario ? savedScenario : scenario;
        }

        bool isWorldPeaceSatisfied(GameDataHolderAccessor accessor) {
            // StoryProgression has no special gate. OnKingdomComplete already
            // promotes completed kingdoms to World Peace as the player leaves
            // them, and the chapel independently requires every other kingdom
            // to be complete, so a second World Peace check there is redundant.
            if (Randomizer::getWorldPeaceRequirementMode() != Randomizer::WorldPeaceRequirementMode_Required)
                return true;

            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
                if (!KingdomRando::isRouteWorld(worldId))
                    continue;

                const s32 peaceScenario = LD::sPeaceScenarioByKingdom[worldId];
                if (peaceScenario <= 0)
                    continue;

                if (getEffectiveScenario(accessor, worldId) < peaceScenario)
                    return false;
            }

            return true;
        }

        // Only the hybrid mode gates the chapel on Toadette Medals; pure
        // ToadetteMedals mode's actual win path is the Castle Interior tombstone
        // interaction, not a change to this gate. DefeatBowser is unaffected
        // either way.
        bool areToadetteMedalsSatisfied(GameDataHolderAccessor accessor) {
            if (Randomizer::getBowserWinCondition() != Randomizer::BowserWinCondition_ToadetteMedalsAndDefeat)
                return true;

            GameDataFile* file = tryGetFile(accessor);
            if (!file)
                return false;

            return MoonRando::getToadetteMedalCollectedCount(file) >= Randomizer::getToadetteMedalRatio().required;
        }

        s32 getToadetteMedalsRemaining(GameDataHolderAccessor accessor) {
            if (Randomizer::getBowserWinCondition() != Randomizer::BowserWinCondition_ToadetteMedalsAndDefeat)
                return 0;

            const s32 required = Randomizer::getToadetteMedalRatio().required;
            GameDataFile* file = tryGetFile(accessor);
            if (!file)
                return required;

            const s32 remaining = required - MoonRando::getToadetteMedalCollectedCount(file);
            return remaining > 0 ? remaining : 0;
        }

        // Message tags contain embedded zero words, so write this string with an explicit offset.
        s32 appendRawMessageData(char16_t* dst, s32 offset, const char16_t* src, s32 size) {
            for (s32 i = 0; i < size; i++)
                dst[offset++] = src[i];
            return offset;
        }

        s32 appendRawMessageText(char16_t* dst, s32 offset, const char16_t* src) {
            while (*src)
                dst[offset++] = *src++;
            return offset;
        }

        s32 appendBowserName(char16_t* buffer, s32 offset) {
            constexpr char16_t kBowserIconTag[] = {
                0x000e, 0x0008, 0x0003, 0x0004, 0x0006, 0x0043
            };
            constexpr char16_t kRedColorTag[] = {
                0x000e, 0x0000, 0x0003, 0x0002, 0x0003
            };
            constexpr char16_t kResetColorTag[] = {
                0x000e, 0x0000, 0x0003, 0x0002, 0xffff
            };

            offset = appendRawMessageData(buffer, offset, kBowserIconTag,
                sizeof(kBowserIconTag) / sizeof(kBowserIconTag[0]));
            offset = appendRawMessageText(buffer, offset, u" ");
            offset = appendRawMessageData(buffer, offset, kRedColorTag,
                sizeof(kRedColorTag) / sizeof(kRedColorTag[0]));
            offset = appendRawMessageText(buffer, offset, u"Bowser");
            offset = appendRawMessageData(buffer, offset, kResetColorTag,
                sizeof(kResetColorTag) / sizeof(kResetColorTag[0]));
            return offset;
        }

        void appendBowserName(sead::WBufferedSafeString* message) {
            char16_t* buffer = message->getBuffer();
            s32 offset = message->calcLength();

            offset = appendBowserName(buffer, offset);
            buffer[offset] = 0;
        }

        void makeConfrontBowserMessage(
            sead::WBufferedSafeString* message, const char16_t* prefix) {
            char16_t* buffer = message->getBuffer();
            s32 offset = appendRawMessageText(buffer, 0, prefix);

            offset = appendBowserName(buffer, offset);
            buffer[offset] = 0;
        }
    } // namespace

    const char* getBlockedMessageLabel() {
        return kBlockedMessageLabel;
    }

    bool isMoonHomeStage(const char* stageName) {
        return isSameString(stageName, kMoonHomeStage);
    }

    bool isMoonChapelStage(const char* stageName) {
        return isSameString(stageName, kMoonChapelStage);
    }

    bool isMoonWorld(GameDataHolderAccessor accessor) {
        if (!accessor.mData || !accessor.mData->getGameDataFile())
            return false;
        return GameDataFunction::getCurrentWorldId(accessor) == WorldMeta::WorldId_Moon;
    }

    bool isBlockedMessage(const char* fileName, const char* key) {
        return isSameString(fileName, kCapMessageFile) && isSameString(key, kBlockedMessageLabel);
    }

    s32 getRequirement() {
        // The Moon Kingdom requirement is now produced by RequirementRando (vanilla
        // base 7, scaled by level + difficulty like other kingdoms) whenever enabled.
        if (!Randomizer::isMoonKingdomRequirementsEnabled())
            return 0;
        return RequirementRando::getRequirement(WorldMeta::WorldId_Moon);
    }

    s32 getCollected(GameDataHolderAccessor accessor) {
        GameDataFile* file = tryGetFile(accessor);
        if (!file)
            return 0;

        // getShineNum is the persistent per-kingdom collection total. Subtracting
        // pay-shines turns it into the currently held inventory, which made depositing
        // Moon Kingdom moons relock the chapel.
        s32 collected = file->getShineNum(WorldMeta::WorldId_Moon);
        return collected > 0 ? collected : 0;
    }

    s32 getRemaining(GameDataHolderAccessor accessor) {
        const s32 remaining = getRequirement() - getCollected(accessor);
        return remaining > 0 ? remaining : 0;
    }

    bool isSatisfied(GameDataHolderAccessor accessor) {
        // The base chapel lock is always active: every other kingdom's moon requirement must be
        // satisfied before the door opens. The Moon Kingdom requirement, World Peace and (in
        // Toadette Medals + Defeat mode) the medal count are extra checks layered on top of this
        // lock, never a replacement for it.
        if (!areOtherKingdomRequirementsSatisfied(accessor))
            return false;

        const s32 requirement = getRequirement();
        if (requirement > 0 && getCollected(accessor) < requirement)
            return false;

        if (!isWorldPeaceSatisfied(accessor))
            return false;

        return areToadetteMedalsSatisfied(accessor);
    }

    bool isToadetteMedalHuntChapelLocked() {
        return Randomizer::getBowserWinCondition() == Randomizer::BowserWinCondition_ToadetteMedals;
    }

    bool showBlockedMessage(const al::IUseSceneObjHolder* holder) {
        if (!holder)
            return false;

        GameDataHolderAccessor accessor(holder);
        sLastRequirement = getRequirement();
        sLastRemaining = getRemaining(accessor);
        sLastMedalsRemaining = getToadetteMedalsRemaining(accessor);

        // Mirror isSatisfied's precedence: outstanding Moon Kingdom moons first, then the
        // always-active "complete every kingdom" lock, then World Peace, then (Toadette Medals
        // + Defeat mode only) outstanding medals.
        if (sLastRemaining > 0)
            sLastBlockReason = BlockReason_MoonKingdomMoons;
        else if (!areOtherKingdomRequirementsSatisfied(accessor))
            sLastBlockReason = BlockReason_OtherKingdomRequirements;
        else if (!isWorldPeaceSatisfied(accessor))
            sLastBlockReason = BlockReason_WorldPeace;
        else if (sLastMedalsRemaining > 0)
            sLastBlockReason = BlockReason_ToadetteMedals;
        else
            sLastBlockReason = BlockReason_None;

        return rs::tryShowCapMessage(holder, getBlockedMessageLabel(), 90, 0);
    }

    const char16_t* tryMakeCapMessage(const char* fileName, const char* key) {
        if (!isBlockedMessage(fileName, key))
            return nullptr;

        if (isToadetteMedalHuntChapelLocked())
            return u"You must find another way to fight Bowser";

        static sead::WFixedSafeString<0x200> sMessage;
        sMessage = u"";

        if (sLastBlockReason == BlockReason_OtherKingdomRequirements) {
            makeConfrontBowserMessage(
                &sMessage, u"We need to complete every\nkingdom before we confront ");
            return sMessage.cstr();
        }

        if (sLastBlockReason == BlockReason_WorldPeace) {
            makeConfrontBowserMessage(
                &sMessage, u"We need World Peace in every\nkingdom before we confront ");
            return sMessage.cstr();
        }

        if (sLastBlockReason == BlockReason_ToadetteMedals) {
            const s32 medalsRemaining = sLastMedalsRemaining > 0 ? sLastMedalsRemaining : 1;
            if (medalsRemaining == 1) {
                makeConfrontBowserMessage(
                    &sMessage, u"We need 1 more Toadette Medal\nbefore we confront ");
            } else {
                sMessage.appendWithFormat(
                    u"We need %d more Toadette Medals\nbefore we confront ",
                    medalsRemaining);
                appendBowserName(&sMessage);
            }
            return sMessage.cstr();
        }

        const s32 remaining = sLastRemaining > 0 ? sLastRemaining : 1;
        if (remaining == 1) {
            makeConfrontBowserMessage(
                &sMessage, u"We need 1 more Moon Kingdom Moon\nbefore we confront ");
        } else {
            sMessage.appendWithFormat(
                u"We need %d more Moon Kingdom Moons\nbefore we confront ",
                remaining);
            appendBowserName(&sMessage);
        }

        if (sLastRequirement > 0)
            return sMessage.cstr();

        return u"The chapel door is open.";
    }

} // namespace MoonRequirement
