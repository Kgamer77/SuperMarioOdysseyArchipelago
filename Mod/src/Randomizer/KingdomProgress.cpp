#include "Randomizer/KingdomProgress.h"

#include "Multiplayer/MpDebugLog.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/WorldMeta.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"

#include "nn/util.h"

#include <cstdlib>
#include <cstring>

namespace KingdomProgress {
    namespace {

        // RKP2 stores the WorldList/current-world IDs used by the runtime. RKP1
        // accidentally stored scenarioWorldId, which swaps several kingdoms and
        // must not be interpreted as the same on-disk format.
        constexpr const char* kRecordPrefix = "RKP2:";
        constexpr const char* kLegacyRecordPrefix = "RKP1:";
        constexpr s32 kBranchStarts[cBranchCount] = { 3, 8 };

        GameDataFile* sLoadedFile = nullptr;
        KingdomProgressList sProgress = {};
        bool sIsLoaded = false;

        enum class DecodeError : u8
        {
            None,
            Missing,
            Prefix,
            BranchBits,
            EntryHex,
            Separator,
            ChecksumHex,
            TrailingData,
            ChecksumMismatch,
            InvalidList,
        };

        const char* decodeErrorName(DecodeError error) {
            switch (error) {
            case DecodeError::None:
                return "none";
            case DecodeError::Missing:
                return "missing";
            case DecodeError::Prefix:
                return "prefix";
            case DecodeError::BranchBits:
                return "branch-bits";
            case DecodeError::EntryHex:
                return "entry-hex";
            case DecodeError::Separator:
                return "separator";
            case DecodeError::ChecksumHex:
                return "checksum-hex";
            case DecodeError::TrailingData:
                return "trailing";
            case DecodeError::ChecksumMismatch:
                return "checksum";
            case DecodeError::InvalidList:
                return "invariants";
            }
            return "unknown";
        }

        u32 updateChecksum(u32 hash, u8 value) {
            hash ^= value;
            return hash * 16777619u;
        }

        u16 packEntry(const KingdomProgressEntry& entry) {
            u16 packed = static_cast<u16>(entry.kingdomId & 0x1f);
            if (entry.completeStatus)
                packed |= 1u << 5;
            if (entry.requirement.requiresBoss)
                packed |= 1u << 6;
            packed |= static_cast<u16>((entry.requirement.moonCount & 0xff) << 7);
            return packed;
        }

        KingdomProgressEntry unpackEntry(u16 packed) {
            KingdomProgressEntry entry = {};
            entry.kingdomId = packed & 0x1f;
            entry.completeStatus = (packed & (1u << 5)) != 0;
            entry.requirement.requiresBoss = (packed & (1u << 6)) != 0;
            entry.requirement.moonCount = (packed >> 7) & 0xff;
            return entry;
        }

        u32 calcChecksum(const KingdomProgressList& list) {
            u32 hash = 2166136261u;
            hash = updateChecksum(hash, list.resolvedBranches);
            for (s32 i = 0; i < cKingdomCount; i++) {
                const u16 packed = packEntry(list.entries[i]);
                hash = updateChecksum(hash, static_cast<u8>(packed));
                hash = updateChecksum(hash, static_cast<u8>(packed >> 8));
            }
            return hash;
        }

        bool validate(const KingdomProgressList& list) {
            if ((list.resolvedBranches & ~((1u << cBranchCount) - 1)) != 0)
                return false;

            u32 seen = 0;
            bool foundIncomplete = false;
            for (s32 i = 0; i < cKingdomCount; i++) {
                const KingdomProgressEntry& entry = list.entries[i];
                if (entry.kingdomId < 0 || entry.kingdomId >= cKingdomCount)
                    return false;
                const u32 bit = 1u << entry.kingdomId;
                if ((seen & bit) != 0)
                    return false;
                seen |= bit;
                if (entry.requirement.moonCount > 0xff)
                    return false;

                if (!entry.completeStatus)
                    foundIncomplete = true;
                else if (foundIncomplete)
                    return false;
            }
            return seen == ((1u << cKingdomCount) - 1);
        }

        bool parseHex(const char* text, s32 digits, u32* outValue) {
            if (!text || !outValue || digits <= 0 || digits >= 16)
                return false;

            char buffer[16] = {};
            for (s32 i = 0; i < digits; i++) {
                const char c = text[i];
                const bool isHex = (c >= '0' && c <= '9')
                    || (c >= 'a' && c <= 'f')
                    || (c >= 'A' && c <= 'F');
                if (!isHex)
                    return false;
                buffer[i] = c;
            }

            *outValue = static_cast<u32>(std::strtoul(buffer, nullptr, 16));
            return true;
        }

        bool decode(const char* record, KingdomProgressList* out, DecodeError* outError = nullptr) {
            auto fail = [outError](DecodeError error) {
                if (outError)
                    *outError = error;
                return false;
            };
            if (outError)
                *outError = DecodeError::None;
            if (!record || !out)
                return fail(DecodeError::Missing);
            if (std::strncmp(record, kRecordPrefix, std::strlen(kRecordPrefix)) != 0)
                return fail(DecodeError::Prefix);

            const size_t recordLength = std::strlen(record);
            if (recordLength < std::strlen(kRecordPrefix) + 3 + cKingdomCount * 4 + 9)
                return fail(DecodeError::TrailingData);

            if (recordLength >= 128)
                return fail(DecodeError::TrailingData);

            const char* cursor = record + std::strlen(kRecordPrefix);
            u32 resolved = 0;
            if (!parseHex(cursor, 2, &resolved) || cursor[2] != ':')
                return fail(DecodeError::BranchBits);
            cursor += 3;

            KingdomProgressList decoded = {};
            decoded.resolvedBranches = static_cast<u8>(resolved);
            for (s32 i = 0; i < cKingdomCount; i++) {
                u32 packed = 0;
                if (!parseHex(cursor, 4, &packed))
                    return fail(DecodeError::EntryHex);
                decoded.entries[i] = unpackEntry(static_cast<u16>(packed));
                cursor += 4;
            }

            if (*cursor++ != ':')
                return fail(DecodeError::Separator);
            u32 savedChecksum = 0;
            if (!parseHex(cursor, 8, &savedChecksum))
                return fail(DecodeError::ChecksumHex);
            if (cursor[8] != '\0')
                return fail(DecodeError::TrailingData);
            if (savedChecksum != calcChecksum(decoded))
                return fail(DecodeError::ChecksumMismatch);
            if (!validate(decoded))
                return fail(DecodeError::InvalidList);

            *out = decoded;
            return true;
        }

        void encode(char* out, size_t outSize, const KingdomProgressList& list) {
            char entries[cKingdomCount * 4 + 1] = {};
            for (s32 i = 0; i < cKingdomCount; i++) {
                nn::util::SNPrintf(entries + i * 4, sizeof(entries) - i * 4, "%04X",
                    static_cast<u32>(packEntry(list.entries[i])));
            }
            nn::util::SNPrintf(out, outSize, "%s%02X:%s:%08X", kRecordPrefix,
                static_cast<u32>(list.resolvedBranches), entries, calcChecksum(list));
        }

        s32 branchIndexForStart(s32 index) {
            for (s32 branch = 0; branch < cBranchCount; branch++) {
                if (kBranchStarts[branch] == index)
                    return branch;
            }
            return -1;
        }

        bool branchResolved(const KingdomProgressList& list, s32 branch) {
            return branch >= 0 && branch < cBranchCount
                && (list.resolvedBranches & (1u << branch)) != 0;
        }

        void requestSave(GameDataFile* file) {
            if (file && file->getGameDataHolder())
                GameDataFunction::setRequireSave(GameDataHolderWriter(file->getGameDataHolder()));
        }

        bool isBossComplete(const GameDataFile* file, s32 worldId) {
            if (!file)
                return false;
            const WorldMeta::Entry* meta = WorldMeta::getByWorldId(worldId);
            if (!meta)
                return false;
            if (meta->worldId == WorldMeta::WorldId_Bowsers) {
                const s32 scenario = file->getScenarioNo(worldId);
                return scenario == 3 || scenario == 4;
            }
            return file->getScenarioNo(worldId) > 1;
        }

        s32 collectedMoons(const GameDataFile* file, s32 worldId) {
            if (!file)
                return 0;
            // The moon assignment belongs to the reward item, whereas pay-shines
            // are keyed by the physical globe where Mario deposited them.  Use
            // the awarded total so redistribution affects kingdom requirements
            // consistently before and after a deposit (including Moon Kingdom).
            const s32 count = file->getShineNum(worldId);
            return count > 0 ? count : 0;
        }

    } // namespace

    void reset() {
        sLoadedFile = nullptr;
        sProgress = {};
        sIsLoaded = false;
    }

    bool initializeNew(GameDataFile* file) {
        Randomizer::logPush("KP INIT begin file=%llx kr=%d routeCount=%d",
            (unsigned long long)(uintptr_t)file,
            Randomizer::isKingdomRandoEnabled(),
            KingdomRando::getRouteWorldCount());
        if (!file || KingdomRando::getRouteWorldCount() != cKingdomCount)
            return false;

        KingdomProgressList list = {};
        for (s32 i = 0; i < cKingdomCount; i++) {
            const s32 logicalWorldId = KingdomRando::getRouteWorldId(i);
            const WorldMeta::Entry* meta = WorldMeta::getByWorldId(logicalWorldId);
            if (!meta)
                return false;

            KingdomProgressEntry& entry = list.entries[i];
            entry.kingdomId = logicalWorldId;
            entry.completeStatus = false;
            entry.requirement.moonCount = 0;
            // Depositing enough moons always advances past Bowser's Kingdom now; beating
            // Mecha-Bowser is no longer required to reach the next kingdom.
            entry.requirement.requiresBoss = false;

            // Every kingdom is opt-in now (Kingdom Moons page), where this
            // used to name three kingdoms and give every other one a
            // requirement unconditionally - which is why a kingdom set to
            // Coins still showed one in the tracker and still held the globe.
            const bool hasMoonRequirement = RequirementRando::isRequirementKingdom(logicalWorldId);
            if (hasMoonRequirement)
                entry.requirement.moonCount = static_cast<u16>(RequirementRando::getRequirement(logicalWorldId));
        }

        if (!validate(list))
            return false;

        sLoadedFile = file;
        sProgress = list;
        sIsLoaded = true;
        file->customRemoveHackDictionaryPrefix(kLegacyRecordPrefix);
        Randomizer::logPush("progress init count=%d first=%d",
            cKingdomCount, sProgress.entries[0].kingdomId);
        const bool saved = save(file);
        debugLogState(file, saved ? "init-saved" : "init-save-failed");

        // Per-kingdom moon requirements are computed here from whatever seed
        // is active AT THIS MOMENT (RequirementRando::getRequirement), then
        // cached into this save file and never recomputed later - not even
        // when the seed is changed afterward via the in-game Seed keyboard
        // (Randomizer::setSeed/reinit doesn't touch KingdomProgress at all).
        // For online co-op, two players who set matching seeds AFTER their
        // saves already existed each keep their OWN stale, pre-seed-change
        // requirement numbers - never resynced to each other or to the seed
        // they agreed on. Logged here so a real session's log can show
        // whether this is actually what's happening: compare this file's
        // seed/requirement dump against the other player's.
        MpDebugLog::write("KingdomProgress::initializeNew seed=%u file=%llx",
            Randomizer::getSeed(), (unsigned long long)(uintptr_t)file);
        for (s32 i = 0; i < cKingdomCount; i++) {
            MpDebugLog::write("  kingdom=%d requirement=%d requiresBoss=%d",
                list.entries[i].kingdomId, list.entries[i].requirement.moonCount,
                list.entries[i].requirement.requiresBoss ? 1 : 0);
        }
        return saved;
    }

    bool prepareAuditComplete(GameDataFile* file) {
        if (!initializeNew(file))
            return false;

        // The audit profile is deliberately synthetic: every route entry is
        // already complete and both branch decisions have been resolved. Keep
        // the route ordering and requirement values produced by initializeNew
        // so the normal renderer and save validator still see a valid record.
        for (s32 i = 0; i < cKingdomCount; i++)
            sProgress.entries[i].completeStatus = true;
        sProgress.resolvedBranches = static_cast<u8>((1u << cBranchCount) - 1);
        const bool saved = save(file);
        debugLogState(file, saved ? "audit-complete" : "audit-complete-save-failed");
        return saved;
    }

    bool load(GameDataFile* file) {
        if (!file) {
            reset();
            return false;
        }

        const char* record = file->customFindHackDictionaryPrefix(kRecordPrefix);
        KingdomProgressList decoded = {};
        DecodeError error = DecodeError::None;
        if (!decode(record, &decoded, &error)) {
            Randomizer::logPush("KP LOAD FAIL file=%llx err=%s rec=%s",
                (unsigned long long)(uintptr_t)file, decodeErrorName(error),
                record ? record : "<null>");
            // Mirrored to the SD-card log as well as the emulator one: this is
            // the failure a guest hits when the downloaded room save does not
            // come back with its progression record, and mp_debug is the log
            // that can actually be pulled off the device afterwards. A null
            // record means the save that was read carried no entry at all
            // (an empty/defaulted file), rather than a corrupt one.
            MpDebugLog::write("KingdomProgress::load FAILED err=%s rec=%s dictEntries=%d",
                decodeErrorName(error), record ? record : "<null>",
                file->customCountHackDictionary());
            reset();
            return false;
        }

        sLoadedFile = file;
        sProgress = decoded;
        sIsLoaded = true;
        Randomizer::logPush("progress load frontier=%d branches=0x%x",
            getFirstIncompleteIndex(file), static_cast<u32>(sProgress.resolvedBranches));
        debugLogState(file, "load-ok");

        // Loaded straight from disk - these requirement numbers were whatever
        // seed was active back when initializeNew() first ran for this save,
        // NOT necessarily today's seed. See the matching comment in
        // initializeNew() above.
        MpDebugLog::write("KingdomProgress::load currentSeed=%u file=%llx",
            Randomizer::getSeed(), (unsigned long long)(uintptr_t)file);
        for (s32 i = 0; i < cKingdomCount; i++) {
            MpDebugLog::write("  kingdom=%d requirement=%d requiresBoss=%d",
                sProgress.entries[i].kingdomId, sProgress.entries[i].requirement.moonCount,
                sProgress.entries[i].requirement.requiresBoss ? 1 : 0);
        }
        return true;
    }

    bool save(GameDataFile* file) {
        if (!file || !sIsLoaded || sLoadedFile != file || !validate(sProgress))
            return false;

        char record[128] = {};
        encode(record, sizeof(record), sProgress);
        file->customRemoveHackDictionaryPrefix(kRecordPrefix);
        // Verify the fixed dictionary accepted the complete compact record. The
        // custom insertion path deliberately avoids treating this metadata string
        // as a real capture for vanilla telemetry.
        file->customAddHackDictionary(record);

        KingdomProgressList verified = {};
        const char* stored = file->customFindHackDictionaryPrefix(kRecordPrefix);
        DecodeError error = DecodeError::None;
        if (!stored || std::strcmp(stored, record) != 0
            || !decode(stored, &verified, &error)) {
            Randomizer::logPush("KP SAVE VERIFY FAIL stored=%d same=%d err=%s",
                stored ? 1 : 0, stored && std::strcmp(stored, record) == 0,
                decodeErrorName(error));
            Randomizer::logPush("KP SAVE wanted=%s", record);
            Randomizer::logPush("KP SAVE stored=%s", stored ? stored : "<null>");
            return false;
        }
        requestSave(file);
        Randomizer::logPush("progress save frontier=%d branches=0x%x",
            getFirstIncompleteIndex(file), static_cast<u32>(sProgress.resolvedBranches));
        return true;
    }

    bool isValidSave(GameDataFile* file) {
        if (!file) {
            Randomizer::logPush("KP VALID file=<null>");
            return false;
        }
        const char* record = file->customFindHackDictionaryPrefix(kRecordPrefix);
        KingdomProgressList decoded = {};
        DecodeError error = DecodeError::None;
        const bool valid = decode(record, &decoded, &error);
        Randomizer::logPush("KP VALID file=%llx ok=%d err=%s rec=%s",
            (unsigned long long)(uintptr_t)file, valid, decodeErrorName(error),
            record ? record : "<null>");
        return valid;
    }

    bool isValidRecordInBuffer(const u8* buffer, u32 size) {
        if (!buffer || size == 0)
            return false;

        const size_t prefixLength = std::strlen(kRecordPrefix);
        for (u32 offset = 0; offset + prefixLength < size; offset++) {
            if (std::memcmp(buffer + offset, kRecordPrefix, prefixLength) != 0)
                continue;

            char record[128] = {};
            u32 length = 0;
            while (length + 1 < sizeof(record)
                && offset + length < size
                && buffer[offset + length] != '\0') {
                record[length] = static_cast<char>(buffer[offset + length]);
                length++;
            }
            if (offset + length >= size || buffer[offset + length] != '\0')
                continue;

            KingdomProgressList decoded = {};
            DecodeError error = DecodeError::None;
            const bool valid = decode(record, &decoded, &error);
            Randomizer::logPush(
                "KP BUFFER record offset=%u len=%u valid=%d err=%s",
                offset, length, valid, decodeErrorName(error));
            if (valid)
                return true;
        }

        Randomizer::logPush("KP BUFFER missing valid %s record size=%u",
            kRecordPrefix, size);
        return false;
    }

    void debugLogState(GameDataFile* file, const char* context) {
        const char* record = file ? file->customFindHackDictionaryPrefix(kRecordPrefix) : nullptr;
        KingdomProgressList decoded = {};
        DecodeError error = DecodeError::None;
        const bool valid = decode(record, &decoded, &error);
        Randomizer::logPush("KP STATE %s file=%llx curN=%d loaded=%d owner=%llx valid=%d err=%s",
            context ? context : "?",
            (unsigned long long)(uintptr_t)file,
            file ? file->getCurrentWorldId() : -1,
            sIsLoaded,
            (unsigned long long)(uintptr_t)sLoadedFile,
            valid,
            decodeErrorName(error));
        // The full dump is 10 lines and this is called from every progression
        // checkpoint (init, both new-game writes, every stage init, both globe
        // phases). The record almost always matches the previous one, so keep
        // the one-line STATE header above - it is what shows the call happened -
        // and only re-print the record and route when something changed.
        static char sLastLoggedRecord[128] = {};
        static bool sHasLoggedRecord = false;
        const char* recordText = record ? record : "<null>";
        const bool recordChanged = !sHasLoggedRecord || std::strncmp(sLastLoggedRecord, recordText, sizeof(sLastLoggedRecord) - 1) != 0;
        if (!recordChanged) {
            Randomizer::logPush("KP RECORD unchanged");
            return;
        }
        sHasLoggedRecord = true;
        std::strncpy(sLastLoggedRecord, recordText, sizeof(sLastLoggedRecord) - 1);
        sLastLoggedRecord[sizeof(sLastLoggedRecord) - 1] = '\0';

        Randomizer::logPush("KP RECORD %s", recordText);
        if (!valid)
            return;

        s32 frontier = cKingdomCount;
        for (s32 i = 0; i < cKingdomCount; i++) {
            if (!decoded.entries[i].completeStatus) {
                frontier = i;
                break;
            }
        }
        Randomizer::logPush("KP LIST frontier=%d visible=%d branches=0x%x",
            frontier, frontier < cKingdomCount ? frontier + 1 : cKingdomCount,
            static_cast<u32>(decoded.resolvedBranches));
        for (s32 i = 0; i < cKingdomCount; i += 2) {
            const KingdomProgressEntry& a = decoded.entries[i];
            const KingdomProgressEntry& b = decoded.entries[i + 1];
            const WorldMeta::Entry* aMeta = WorldMeta::getByWorldId(a.kingdomId);
            const WorldMeta::Entry* bMeta = WorldMeta::getByWorldId(b.kingdomId);
            Randomizer::logPush(
                "KP E %d:N%d/L%d C%d M%d B%d | %d:N%d/L%d C%d M%d B%d",
                i, a.kingdomId, aMeta ? aMeta->worldId : -1, a.completeStatus,
                a.requirement.moonCount, a.requirement.requiresBoss,
                i + 1, b.kingdomId, bMeta ? bMeta->worldId : -1,
                b.completeStatus, b.requirement.moonCount,
                b.requirement.requiresBoss);
        }
    }

    const KingdomProgressList* get(GameDataFile* file) {
        if (!file)
            return nullptr;
        if (!sIsLoaded || sLoadedFile != file) {
            if (!load(file))
                return nullptr;
        }
        return &sProgress;
    }

    s32 getFirstIncompleteIndex(GameDataFile* file) {
        const KingdomProgressList* list = get(file);
        if (!list)
            return -1;
        for (s32 i = 0; i < cKingdomCount; i++) {
            if (!list->entries[i].completeStatus)
                return i;
        }
        return cKingdomCount;
    }

    s32 getVisibleCount(GameDataFile* file) {
        const s32 frontier = getFirstIncompleteIndex(file);
        if (frontier < 0)
            return 0;
        return frontier < cKingdomCount ? frontier + 1 : cKingdomCount;
    }

    bool isVisible(GameDataFile* file, s32 kingdomId) {
        const KingdomProgressList* list = get(file);
        const s32 count = getVisibleCount(file);
        if (!list)
            return false;
        for (s32 i = 0; i < count; i++) {
            if (list->entries[i].kingdomId == kingdomId)
                return true;
        }
        return false;
    }

    const KingdomProgressEntry* findEntry(GameDataFile* file, s32 kingdomId) {
        const KingdomProgressList* list = get(file);
        if (!list)
            return nullptr;
        for (s32 i = 0; i < cKingdomCount; i++) {
            if (list->entries[i].kingdomId == kingdomId)
                return &list->entries[i];
        }
        return nullptr;
    }

    bool requirementsSatisfied(GameDataFile* file, const KingdomProgressEntry& entry) {
        const s32 moons = collectedMoons(file, entry.kingdomId);
        const bool boss = !entry.requirement.requiresBoss
            || isBossComplete(file, entry.kingdomId);
        Randomizer::logPush("KP REQ N%d moons=%d/%d boss=%d/%d",
            entry.kingdomId, moons, entry.requirement.moonCount,
            boss, entry.requirement.requiresBoss);
        if (entry.requirement.moonCount > 0
            && moons < entry.requirement.moonCount)
            return false;
        if (!boss)
            return false;
        return true;
    }

    bool tryCompleteFrontier(GameDataFile* file, s32 currentKingdomId) {
        const s32 frontier = getFirstIncompleteIndex(file);
        if (frontier < 0 || frontier >= cKingdomCount) {
            Randomizer::logPush("KP COMPLETE reject frontier=%d curN=%d",
                frontier, currentKingdomId);
            return false;
        }

        KingdomProgressEntry& entry = sProgress.entries[frontier];
        if (entry.kingdomId != currentKingdomId) {
            Randomizer::logPush("KP COMPLETE reject mismatch idx=%d curN=%d wantN=%d",
                frontier, currentKingdomId, entry.kingdomId);
            return false;
        }
        if (!requirementsSatisfied(file, entry)) {
            Randomizer::logPush("KP COMPLETE reject requirements idx=%d N%d",
                frontier, currentKingdomId);
            return false;
        }

        entry.completeStatus = true;
        if (!save(file)) {
            entry.completeStatus = false;
            Randomizer::logPush("KP COMPLETE rollback save idx=%d N%d",
                frontier, currentKingdomId);
            return false;
        }
        Randomizer::logPush("KP COMPLETE success idx=%d N%d next=%d",
            frontier, currentKingdomId, getFirstIncompleteIndex(file));
        return true;
    }

    s32 getFrontierUnlockCandidates(GameDataFile* file, s32* outKingdomIds, s32 maxCount) {
        if (!outKingdomIds || maxCount <= 0)
            return 0;
        for (s32 i = 0; i < maxCount; i++)
            outKingdomIds[i] = -1;

        const KingdomProgressList* list = get(file);
        const s32 frontier = getFirstIncompleteIndex(file);
        if (!list || frontier < 0 || frontier >= cKingdomCount)
            return 0;

        const s32 branch = branchIndexForStart(frontier);
        if (branch >= 0 && !branchResolved(*list, branch) && maxCount >= 2) {
            outKingdomIds[0] = list->entries[frontier].kingdomId;
            outKingdomIds[1] = list->entries[frontier + 1].kingdomId;
            return 2;
        }

        outKingdomIds[0] = list->entries[frontier].kingdomId;
        return 1;
    }

    bool commitBranchChoice(GameDataFile* file, s32 selectedKingdomId) {
        const s32 frontier = getFirstIncompleteIndex(file);
        const s32 branch = branchIndexForStart(frontier);
        if (branch < 0 || branchResolved(sProgress, branch))
            return false;

        const s32 second = frontier + 1;
        if (selectedKingdomId == sProgress.entries[second].kingdomId) {
            const KingdomProgressEntry temp = sProgress.entries[frontier];
            sProgress.entries[frontier] = sProgress.entries[second];
            sProgress.entries[second] = temp;
        } else if (selectedKingdomId != sProgress.entries[frontier].kingdomId) {
            return false;
        }

        sProgress.resolvedBranches |= 1u << branch;
        if (!save(file)) {
            load(file);
            return false;
        }
        Randomizer::logPush("progress branch=%d selected=%d order=%d,%d",
            branch, selectedKingdomId, sProgress.entries[frontier].kingdomId,
            sProgress.entries[second].kingdomId);
        return true;
    }

    bool captureSyncState(GameDataFile* file, SyncState* out) {
        if (!out)
            return false;
        for (s32 branch = 0; branch < cBranchCount; branch++)
            out->branchChoice[branch] = -1;
        out->completedCount = 0;

        const KingdomProgressList* list = get(file);
        if (!list)
            return false;

        // getFirstIncompleteIndex IS the completed prefix length - it returns
        // the index of the first incomplete entry, or cKingdomCount when every
        // entry is done.
        const s32 frontier = getFirstIncompleteIndex(file);
        out->completedCount = (u8)(frontier < 0 ? 0 : frontier);

        for (s32 branch = 0; branch < cBranchCount; branch++) {
            if (branchResolved(*list, branch))
                out->branchChoice[branch] = (s8)list->entries[kBranchStarts[branch]].kingdomId;
        }
        return true;
    }

    bool applySyncState(GameDataFile* file, const SyncState& remote) {
        const KingdomProgressList* current = get(file);
        if (!current)
            return false;

        KingdomProgressList merged = *current;
        bool changed = false;

        // Branch resolutions first: they decide the ORDER, and the completed
        // prefix below is a statement about that order.
        for (s32 branch = 0; branch < cBranchCount; branch++) {
            const s8 choice = remote.branchChoice[branch];
            if (choice < 0 || branchResolved(merged, branch))
                continue;

            const s32 first = kBranchStarts[branch];
            const s32 second = first + 1;
            if (second >= cKingdomCount)
                continue;

            if (merged.entries[second].kingdomId == choice) {
                const KingdomProgressEntry temp = merged.entries[first];
                merged.entries[first] = merged.entries[second];
                merged.entries[second] = temp;
            } else if (merged.entries[first].kingdomId != choice) {
                // The chosen kingdom is not either sibling of this branch on
                // our ordering. Ignore rather than guess - a corrupt or
                // mismatched record must not be allowed to reorder the list.
                Randomizer::logPush("KP SYNC branch=%d choice=%d not a sibling (%d,%d)",
                    branch, (s32)choice, merged.entries[first].kingdomId,
                    merged.entries[second].kingdomId);
                continue;
            }

            merged.resolvedBranches |= (u8)(1u << branch);
            changed = true;
        }

        // Then the prefix, raised only.
        s32 localCount = 0;
        while (localCount < cKingdomCount && merged.entries[localCount].completeStatus)
            localCount++;

        s32 target = remote.completedCount > cKingdomCount ? cKingdomCount : remote.completedCount;
        if (target > localCount) {
            for (s32 i = localCount; i < target; i++)
                merged.entries[i].completeStatus = true;
            changed = true;
        } else {
            target = localCount;
        }

        // A branch whose two slots are both inside the completed prefix has
        // nothing left to choose, so record it as resolved. Without this the
        // record could claim an unresolved branch that the unlock prompt would
        // then try to offer for kingdoms already finished.
        for (s32 branch = 0; branch < cBranchCount; branch++) {
            if (branchResolved(merged, branch))
                continue;
            if (kBranchStarts[branch] + 1 < target) {
                merged.resolvedBranches |= (u8)(1u << branch);
                changed = true;
            }
        }

        if (!changed)
            return false;

        // Never persist a list the loader would reject. A merge that cannot
        // validate is dropped whole rather than half-applied.
        if (!validate(merged)) {
            Randomizer::logPush("KP SYNC merge failed validation, discarded");
            MpDebugLog::write("KingdomProgress::applySyncState: merged list failed validation");
            return false;
        }

        const KingdomProgressList previous = sProgress;
        sProgress = merged;
        if (!save(file)) {
            sProgress = previous;
            Randomizer::logPush("KP SYNC rollback save");
            return false;
        }

        Randomizer::logPush("KP SYNC applied count=%d branches=0x%x", target,
            (u32)sProgress.resolvedBranches);
        MpDebugLog::write("KingdomProgress::applySyncState: completed=%d branches=0x%x", target,
            (u32)sProgress.resolvedBranches);
        return true;
    }

    bool areAllEntriesExceptComplete(GameDataFile* file, s32 excludedKingdomId) {
        const KingdomProgressList* list = get(file);
        if (!list)
            return false;
        for (s32 i = 0; i < cKingdomCount; i++) {
            const KingdomProgressEntry& entry = list->entries[i];
            if (entry.kingdomId != excludedKingdomId && !entry.completeStatus)
                return false;
        }
        return true;
    }

} // namespace KingdomProgress
