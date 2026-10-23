#include "Randomizer/SubareaKeyDB.h"

#include "Randomizer/WorldMeta.h"

#include <cstring>

namespace SubareaKeyDB {

    namespace {

        constexpr s32 cKingdomCount = 17;

        // Sub-area stages whose own doors still belong to their kingdom's keys.
        // Shiveria Town is a sub-area as far as entrance rando is concerned -
        // its shop and costume doors are wired as nested, and its CSV
        // area_scope has to stay "subarea" for that routing to keep working -
        // but the pockets behind those doors are Snow Kingdom's own optional
        // content, so they ride Snow's key instead of going free. Naming the
        // kingdom here keeps that a key-side rule rather than a data change.
        struct KeyedSubareaHomeStage {
            const char* stage;
            s32 worldId;
        };
        constexpr KeyedSubareaHomeStage sKeyedSubareaHomeStages[] = {
            { "SnowWorldTownStage", WorldMeta::WorldId_Snow },
        };
        constexpr s32 sKeyedSubareaHomeStageCount
            = sizeof(sKeyedSubareaHomeStages) / sizeof(sKeyedSubareaHomeStages[0]);

        // Lazily built so this needs no init call site: the tables it reads are
        // all constexpr, so the first query can fill it and every later one is a
        // lookup.
        bool sMaskCacheBuilt = false;
        u32 sTypeMaskByKingdom[cKingdomCount] = {};
        bool sSlotCacheBuilt[Randomizer::SubareaUnlockMode_PerKingdomAndType + 1] = {};
        s8 sSlotByModeKingdomType[Randomizer::SubareaUnlockMode_PerKingdomAndType + 1]
                                 [cKingdomCount][EntranceDB2::SubareaType_Count]
            = {};

        void ensureMaskCache() {
            if (sMaskCacheBuilt)
                return;
            sMaskCacheBuilt = true;

            for (s32 i = 0; i < cKingdomCount; i++)
                sTypeMaskByKingdom[i] = 0;

            for (s32 index = 0; index < EntranceDB::sLinkCount; index++) {
                if (!isGatedLink(index))
                    continue;
                const s32 kingdom = kingdomForLink(index);
                if (kingdom < 0 || kingdom >= cKingdomCount)
                    continue;
                sTypeMaskByKingdom[kingdom] |= 1u << typeForLink(index);
            }
        }

    } // namespace

    bool isGatedType(SubareaType type) {
        for (s32 i = 0; i < sGatedTypeCount; i++) {
            if (sGatedTypes[i] == type)
                return true;
        }
        return false;
    }

    SubareaType typeForLink(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        return link ? link->subareaType : EntranceDB2::SubareaType_None;
    }

    s32 kingdomForLink(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        if (!link)
            return -1;
        // A link's home stage is a kingdom main stage exactly when the link is
        // overworld-scoped (verified across all 160 curated links), so this one
        // lookup answers both "which kingdom" and "is this a nested sub-area".
        if (const WorldMeta::Entry* entry = WorldMeta::findByMainStage(link->homeStage))
            return entry->worldId;
        for (s32 i = 0; i < sKeyedSubareaHomeStageCount; i++) {
            if (std::strcmp(link->homeStage, sKeyedSubareaHomeStages[i].stage) == 0)
                return sKeyedSubareaHomeStages[i].worldId;
        }
        return -1;
    }

    bool isGatedLink(s32 linkIndex) {
        const EntranceDB::EntranceLink* link = EntranceDB::entryAt(linkIndex);
        if (!link)
            return false;
        // Story routes are already marked vanilla (config/entrance_db2_report_ignores.csv
        // carries exactly the story pairs), and excluded rows never reach the
        // runtime's entrance resolution at all.
        if (!EntranceDB::isActiveLink(*link))
            return false;
        // The Deep Woods is a through-route, not a pocket: both of Wooded's
        // drops into it and both doors nested inside it stay free whatever door
        // kind they present, so a key can never strand the far side of a vine.
        if (EntranceDB::isDeepWoodsLoadingZone(*link))
            return false;
        if (!isGatedType(link->subareaType))
            return false;
        return kingdomForLink(linkIndex) >= 0;
    }

    u32 typeMaskForKingdom(s32 kingdom) {
        if (kingdom < 0 || kingdom >= cKingdomCount)
            return 0;
        ensureMaskCache();
        return sTypeMaskByKingdom[kingdom];
    }

    bool kingdomHasType(s32 kingdom, SubareaType type) {
        if (!isGatedType(type))
            return false;
        return (typeMaskForKingdom(kingdom) & (1u << type)) != 0;
    }

    s32 keySlotCountForMode(Randomizer::SubareaUnlockMode mode) {
        switch (mode) {
        case Randomizer::SubareaUnlockMode_PerType: {
            s32 count = 0;
            for (s32 i = 0; i < sGatedTypeCount; i++) {
                for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                    if (kingdomHasType(kingdom, sGatedTypes[i])) {
                        count++;
                        break;
                    }
                }
            }
            return count;
        }
        case Randomizer::SubareaUnlockMode_PerKingdom: {
            s32 count = 0;
            for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                if (typeMaskForKingdom(kingdom) != 0)
                    count++;
            }
            return count;
        }
        case Randomizer::SubareaUnlockMode_PerKingdomAndType: {
            s32 count = 0;
            for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                for (s32 i = 0; i < sGatedTypeCount; i++) {
                    if (kingdomHasType(kingdom, sGatedTypes[i]))
                        count++;
                }
            }
            return count;
        }
        default:
            return 0;
        }
    }

    bool keySlotAtForMode(Randomizer::SubareaUnlockMode mode, s32 slot, s32* outKingdom,
        SubareaType* outType) {
        if (slot < 0)
            return false;

        s32 seen = 0;

        // Kingdoms are walked in vanilla order (Cap through Darker) rather than
        // randomized order so the collection-list page reads the same every seed.
        switch (mode) {
        case Randomizer::SubareaUnlockMode_PerType:
            for (s32 i = 0; i < sGatedTypeCount; i++) {
                bool exists = false;
                for (s32 kingdom = 0; kingdom < cKingdomCount && !exists; kingdom++)
                    exists = kingdomHasType(kingdom, sGatedTypes[i]);
                if (!exists)
                    continue;
                if (seen++ != slot)
                    continue;
                if (outKingdom)
                    *outKingdom = -1;
                if (outType)
                    *outType = sGatedTypes[i];
                return true;
            }
            return false;

        case Randomizer::SubareaUnlockMode_PerKingdom:
            for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                if (typeMaskForKingdom(kingdom) == 0)
                    continue;
                if (seen++ != slot)
                    continue;
                if (outKingdom)
                    *outKingdom = kingdom;
                if (outType)
                    *outType = EntranceDB2::SubareaType_Count;
                return true;
            }
            return false;

        case Randomizer::SubareaUnlockMode_PerKingdomAndType:
            for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                for (s32 i = 0; i < sGatedTypeCount; i++) {
                    if (!kingdomHasType(kingdom, sGatedTypes[i]))
                        continue;
                    if (seen++ != slot)
                        continue;
                    if (outKingdom)
                        *outKingdom = kingdom;
                    if (outType)
                        *outType = sGatedTypes[i];
                    return true;
                }
            }
            return false;

        default:
            return false;
        }
    }

    static void ensureSlotCache(Randomizer::SubareaUnlockMode mode) {
        const s32 modeIndex = static_cast<s32>(mode);
        if (modeIndex < 0
            || modeIndex > Randomizer::SubareaUnlockMode_PerKingdomAndType
            || sSlotCacheBuilt[modeIndex])
            return;

        sSlotCacheBuilt[modeIndex] = true;
        std::memset(sSlotByModeKingdomType[modeIndex], -1,
            sizeof(sSlotByModeKingdomType[modeIndex]));

        const s32 slotCount = keySlotCountForMode(mode);
        for (s32 slot = 0; slot < slotCount; slot++) {
            s32 slotKingdom = -1;
            SubareaType slotType = EntranceDB2::SubareaType_None;
            if (!keySlotAtForMode(mode, slot, &slotKingdom, &slotType))
                continue;

            for (s32 kingdom = 0; kingdom < cKingdomCount; kingdom++) {
                if (slotKingdom >= 0 && slotKingdom != kingdom)
                    continue;
                for (s32 i = 0; i < sGatedTypeCount; i++) {
                    const SubareaType type = sGatedTypes[i];
                    if ((slotType == EntranceDB2::SubareaType_Count || slotType == type)
                        && kingdomHasType(kingdom, type))
                        sSlotByModeKingdomType[modeIndex][kingdom][type]
                            = static_cast<s8>(slot);
                }
            }
        }
    }

    s32 keySlotCount() { return keySlotCountForMode(Randomizer::getSubareaUnlockMode()); }

    bool keySlotAt(s32 slot, s32* outKingdom, SubareaType* outType) {
        return keySlotAtForMode(Randomizer::getSubareaUnlockMode(), slot, outKingdom, outType);
    }

    s32 findKeySlot(s32 kingdom, SubareaType type) {
        if (!kingdomHasType(kingdom, type))
            return -1;

        const Randomizer::SubareaUnlockMode mode = Randomizer::getSubareaUnlockMode();
        const s32 modeIndex = static_cast<s32>(mode);
        if (modeIndex < 0
            || modeIndex > Randomizer::SubareaUnlockMode_PerKingdomAndType)
            return -1;

        // This lookup sits in the entrance fixed point, so rebuilding a slot by
        // walking every preceding kingdom/type pair made fine-grained keys
        // multiply the cost of every solver probe. The immutable classification
        // is cached once per mode and makes the hot path O(1).
        ensureSlotCache(mode);
        return sSlotByModeKingdomType[modeIndex][kingdom][type];
    }

    s32 keyCountForMode(Randomizer::SubareaUnlockMode mode) { return keySlotCountForMode(mode); }

    s32 hostMoonCapacity(Randomizer::SubareaUnlockHost host) {
        switch (host) {
        case Randomizer::SubareaUnlockHost_Cap:
            return cCapMoonCapacity;
        case Randomizer::SubareaUnlockHost_Moon:
            return cMoonMoonCapacity;
        case Randomizer::SubareaUnlockHost_CapAndMoon:
            return cCapMoonCapacity + cMoonMoonCapacity;
        case Randomizer::SubareaUnlockHost_AllKingdoms:
            // The key count is the limiting factor, not the exact authored
            // moon total. Keep this as the full main-game pool so every current
            // Subarea Unlock mode has a truthful, future-proof capacity.
            return 657;
        default:
            return 0;
        }
    }

    bool isHostKingdom(s32 kingdom) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return false;
        switch (Randomizer::getSubareaUnlockHost()) {
        case Randomizer::SubareaUnlockHost_Cap:
            return kingdom == WorldMeta::WorldId_Cap;
        case Randomizer::SubareaUnlockHost_Moon:
            return kingdom == WorldMeta::WorldId_Moon;
        case Randomizer::SubareaUnlockHost_CapAndMoon:
            return kingdom == WorldMeta::WorldId_Cap || kingdom == WorldMeta::WorldId_Moon;
        case Randomizer::SubareaUnlockHost_AllKingdoms:
            return kingdom >= 0 && kingdom < WorldMeta::sWorldCount;
        default:
            return false;
        }
    }

    const char16_t* typeName(SubareaType type) {
        switch (type) {
        case EntranceDB2::SubareaType_WarpPipe:
            return u"Warp Pipe";
        case EntranceDB2::SubareaType_MoonPipe:
            return u"Moon Pipe";
        case EntranceDB2::SubareaType_OpenPassage:
            return u"Open Passage";
        case EntranceDB2::SubareaType_Doorway:
            return u"Doorway";
        case EntranceDB2::SubareaType_CappyDoor:
            return u"Scarecrow Door";
        case EntranceDB2::SubareaType_Beanstalk:
            return u"Beanstalk";
        case EntranceDB2::SubareaType_SparkPylon:
            return u"Spark Pylon";
        case EntranceDB2::SubareaType_Painting:
            return u"Painting";
        default:
            return u"Subarea";
        }
    }

    const char16_t* typeNamePlural(SubareaType type) {
        switch (type) {
        case EntranceDB2::SubareaType_WarpPipe:
            return u"Warp Pipes";
        case EntranceDB2::SubareaType_MoonPipe:
            return u"Moon Pipes";
        case EntranceDB2::SubareaType_OpenPassage:
            return u"Open Passages";
        case EntranceDB2::SubareaType_Doorway:
            return u"Doorways";
        case EntranceDB2::SubareaType_CappyDoor:
            return u"Scarecrow Doors";
        case EntranceDB2::SubareaType_Beanstalk:
            return u"Beanstalks";
        case EntranceDB2::SubareaType_SparkPylon:
            return u"Spark Pylons";
        case EntranceDB2::SubareaType_Painting:
            return u"Paintings";
        default:
            return u"Subareas";
        }
    }

    const char* typeNameAscii(SubareaType type) {
        switch (type) {
        case EntranceDB2::SubareaType_WarpPipe:
            return "Warp Pipe";
        case EntranceDB2::SubareaType_MoonPipe:
            return "Moon Pipe";
        case EntranceDB2::SubareaType_OpenPassage:
            return "Open Passage";
        case EntranceDB2::SubareaType_Doorway:
            return "Doorway";
        case EntranceDB2::SubareaType_CappyDoor:
            return "Scarecrow Door";
        case EntranceDB2::SubareaType_Beanstalk:
            return "Beanstalk";
        case EntranceDB2::SubareaType_SparkPylon:
            return "Spark Pylon";
        case EntranceDB2::SubareaType_Painting:
            return "Painting";
        default:
            return "Subarea";
        }
    }

} // namespace SubareaKeyDB
