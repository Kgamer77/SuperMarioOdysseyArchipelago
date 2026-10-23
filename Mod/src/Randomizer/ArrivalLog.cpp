#include "Randomizer/ArrivalLog.h"

#include "Multiplayer/algorithms/crc32.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/Randomizer.h"
#include "SD/SDHooks.h"

#include "Library/Base/StringUtil.h"

#include "nn/fs.h"

#include <cstddef>
#include <cstring>

namespace ArrivalLog {
    namespace {

        namespace LD = Randomizer::LogicData;

        constexpr u32 cMagic = 0x4C565241; // 'ARVL'
        constexpr u16 cVersion = 2;
        constexpr u16 cLegacyVersion = 1;
        constexpr s32 cBitCount = cLinkCount * cKindCount;
        constexpr s32 cWordCount = (cBitCount + 31) / 32;
        constexpr s32 cPaintingLinkCount = LD::sPaintingLinkCount;
        // Two directions per painting link, packed into one word.
        static_assert(cPaintingLinkCount * 2 <= 32,
            "Painting arrival bits must fit a single word; widen paintingBits first.");

#pragma pack(push, 1)
        // The mutable bits are kept in one CRC-covered region. Version 2 also
        // covers SavedLog::autoGrantCount; version 1 uses its legacy bits-only
        // CRC so old arrival logs remain readable.
        struct SavedBits {
            u32 entrances[cWordCount];
            u32 paintings;
        };

        struct SavedLog {
            u32 magic;
            u16 version;
            u16 linkCount;
            u16 paintingLinkCount;
            u16 autoGrantCount;
            u32 crc32;
            SavedBits bits;
        };
#pragma pack(pop)

        u32 sBits[cWordCount] = {};
        u32 sPaintingBits = 0;
        u32 sSignature = 0;
        bool sDirty = false;
        s32 sSlot = -1;
        s32 sAutoGrantSummaryCount = 0;

        // Written out rather than derived from the enum value: ConnKind_None
        // occupies 0 and ConnKind_Home aliases ConnKind_HomeOut, so an arithmetic
        // mapping would silently shift the whole bit layout if either changed.
        s32 slotForKind(EntranceRando::ConnKind kind) {
            switch (kind) {
            case EntranceRando::ConnKind_SubNear:
                return 0;
            case EntranceRando::ConnKind_SubFar:
                return 1;
            case EntranceRando::ConnKind_HomeOut:
                return 2;
            case EntranceRando::ConnKind_HomeIn:
                return 3;
            default:
                return -1;
            }
        }

        s32 bitIndex(s32 link, EntranceRando::ConnKind kind) {
            const s32 slot = slotForKind(kind);
            if (link < 0 || link >= cLinkCount || slot < 0)
                return -1;
            return link * cKindCount + slot;
        }

        s32 paintingBitIndex(s32 paintingIndex, bool forward) {
            if (paintingIndex < 0 || paintingIndex >= cPaintingLinkCount)
                return -1;
            return paintingIndex * 2 + (forward ? 0 : 1);
        }

        void refreshSignature() {
            u32 hash = 0x811C9DC5u;
            for (s32 i = 0; i < cWordCount; i++) {
                hash ^= sBits[i];
                hash *= 0x01000193u;
            }
            hash ^= sPaintingBits;
            hash *= 0x01000193u;
            sSignature = hash;
        }

        u32 calcCrc32(const void* data, size_t size) {
            const u8* bytes = static_cast<const u8*>(data);
            u32 crc = 0xFFFFFFFF;
            for (size_t i = 0; i < size; i++)
                crc = (crc >> 8) ^ crc32::s_Table[(crc ^ bytes[i]) & 0xFF];
            return crc ^ 0xFFFFFFFF;
        }

        u32 calcSavedCrc(const SavedLog& saved) {
            if (saved.version == cLegacyVersion)
                return calcCrc32(&saved.bits, sizeof(saved.bits));

            // Version 2 extends the protected region to include the compact
            // auto-grant summary. Version 1 used this same two-byte slot as
            // padding, so legacy records remain readable with their old CRC.
            return calcCrc32(&saved.autoGrantCount,
                sizeof(saved.autoGrantCount) + sizeof(saved.bits));
        }

        bool isValidSlot(s32 slot) { return slot >= 1 && slot <= 10; }

        bool fileExists(const char* path) {
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;
            nn::fs::CloseFile(handle);
            return true;
        }

        // Category-aware, like every other per-slot artifact. This used to be
        // hardcoded to Saves/, which meant a multiplayer run read and wrote the
        // SINGLE-PLAYER arrival log for the same slot number - harmless-looking
        // until remove() got involved, where a guest starting slot 1 would
        // delete the player's real single-player slot 1 log. Defaults to the
        // active category the same way RandoSaveFile's own helpers do, so no
        // call site has to change.
        al::StringTmp<128> makePath(s32 slot) {
            const char* root = "Saves";
            switch (SaveRedirect::getActiveCategory()) {
            case SaveRedirect::Category::Multiplayer:
                root = "MPSaves";
                break;
            case SaveRedirect::Category::MultiplayerGuest:
                root = "MPSaves/_guest";
                break;
            default:
                break;
            }
            return al::StringTmp<128>(
                "sd:/SMO-Randomizer/%s/Slot%d/arrivals.bin", root, slot);
        }

    } // namespace

    void reset() {
        std::memset(sBits, 0, sizeof(sBits));
        sPaintingBits = 0;
        sAutoGrantSummaryCount = 0;
        refreshSignature();
        sDirty = false;
        sSlot = -1;
    }

    void recordArrival(EntranceRando::Conn conn) {
        const s32 bit = bitIndex(conn.link, conn.kind);
        if (bit < 0)
            return;
        u32& word = sBits[bit / 32];
        const u32 mask = 1u << (bit % 32);
        if (word & mask)
            return;
        word |= mask;
        refreshSignature();
        sDirty = true;
        Randomizer::logPush("ARRIVAL record link=%d kind=%d sig=%08x",
            conn.link, static_cast<s32>(conn.kind), sSignature);
    }

    bool hasArrived(s32 link, EntranceRando::ConnKind kind) {
        const s32 bit = bitIndex(link, kind);
        if (bit < 0)
            return false;
        return (sBits[bit / 32] & (1u << (bit % 32))) != 0;
    }

    void recordPaintingArrival(s32 paintingIndex, bool forward) {
        const s32 bit = paintingBitIndex(paintingIndex, forward);
        if (bit < 0)
            return;
        const u32 mask = 1u << bit;
        if (sPaintingBits & mask)
            return;
        sPaintingBits |= mask;
        refreshSignature();
        sDirty = true;
        Randomizer::logPush("ARRIVAL record painting=%d forward=%d sig=%08x",
            paintingIndex, forward ? 1 : 0, sSignature);
    }

    bool hasPaintingArrival(s32 paintingIndex, bool forward) {
        const s32 bit = paintingBitIndex(paintingIndex, forward);
        if (bit < 0)
            return false;
        return (sPaintingBits & (1u << bit)) != 0;
    }

    void recordAutoGrantSummary(s32 count) {
        if (count < 0)
            count = 0;
        if (count > 0xFFFF)
            count = 0xFFFF;
        if (sAutoGrantSummaryCount == count)
            return;
        sAutoGrantSummaryCount = count;
        sDirty = true;
        if (count > 0)
            Randomizer::logPush(
                "%d items had no available locations and were granted at the start.",
                count);
    }

    s32 getAutoGrantSummaryCount() {
        return sAutoGrantSummaryCount;
    }

    u32 getSignature() { return sSignature; }

    bool load(s32 slot) {
        reset();
        if (!isValidSlot(slot))
            return false;
        // Claim the slot up front: an empty log still belongs to it, so a later
        // delete of this slot clears the in-memory state.
        sSlot = slot;

        nn::fs::FileHandle handle;
        const auto path = makePath(slot);
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false; // No log yet: a save from before this feature, or no arrival made.

        SavedLog saved = {};
        const bool read = nn::fs::ReadFile(handle, 0, &saved, sizeof(saved)).IsSuccess();
        nn::fs::CloseFile(handle);
        if (!read)
            return false;

        // The two counts guard against a table resize silently reinterpreting
        // the bit layout; a mismatch drops the log rather than crediting the
        // wrong doors, which only costs the player the pre-resize arrivals.
        if (saved.magic != cMagic
            || (saved.version != cVersion && saved.version != cLegacyVersion)
            || saved.linkCount != cLinkCount
            || saved.paintingLinkCount != cPaintingLinkCount
            || saved.crc32 != calcSavedCrc(saved)) {
            Randomizer::logPush("ARRIVAL load reject slot=%d magic=%08x ver=%u links=%u paintings=%u",
                slot, saved.magic, saved.version, saved.linkCount, saved.paintingLinkCount);
            return false;
        }

        std::memcpy(sBits, saved.bits.entrances, sizeof(sBits));
        sPaintingBits = saved.bits.paintings;
        sAutoGrantSummaryCount = saved.version == cVersion ? saved.autoGrantCount : 0;
        refreshSignature();
        sDirty = false;
        sSlot = slot;
        Randomizer::logPush("ARRIVAL load slot=%d sig=%08x", slot, sSignature);
        return true;
    }

    bool save(s32 slot) {
        if (!isValidSlot(slot))
            return false;

        SavedLog saved = {};
        saved.magic = cMagic;
        saved.version = cVersion;
        saved.linkCount = static_cast<u16>(cLinkCount);
        saved.paintingLinkCount = static_cast<u16>(cPaintingLinkCount);
        saved.autoGrantCount = static_cast<u16>(sAutoGrantSummaryCount);
        std::memcpy(saved.bits.entrances, sBits, sizeof(saved.bits.entrances));
        saved.bits.paintings = sPaintingBits;
        saved.crc32 = calcSavedCrc(saved);

        const auto path = makePath(slot);
        nn::fs::DeleteFile(path.cstr());
        if (nn::fs::CreateFile(path.cstr(), sizeof(SavedLog)).IsFailure())
            return false;

        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Write).IsFailure())
            return false;
        const bool ok = nn::fs::WriteFile(handle, 0, &saved, sizeof(saved),
            nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                            .IsSuccess();
        nn::fs::CloseFile(handle);
        if (ok) {
            sDirty = false;
            sSlot = slot;
        }
        return ok;
    }

    bool flushIfDirty(s32 slot) {
        if (!sDirty)
            return true;
        return save(slot);
    }

    bool remove(s32 slot) {
        if (!isValidSlot(slot))
            return false;
        const auto path = makePath(slot);
        bool ok = true;
        if (fileExists(path.cstr()))
            ok = nn::fs::DeleteFile(path.cstr()).IsSuccess();
        // Only drop the in-memory log when the slot being deleted is the one it
        // belongs to; deleting another slot must not wipe a live run.
        if (sSlot == slot)
            reset();
        return ok;
    }

} // namespace ArrivalLog
