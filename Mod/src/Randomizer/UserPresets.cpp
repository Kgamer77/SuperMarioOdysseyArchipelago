#include "Randomizer/UserPresets.h"

#include "Library/Base/StringUtil.h"

#include "nn/fs.h"

#include "SDRedirect/SDPaths.h"
#include <cstddef>
#include <cstring>

namespace UserPresets {
    namespace {
        constexpr u32 cMagic = 0x53525055; // 'UPRS'
        constexpr u32 cCurrentVersion = 1;
        constexpr u32 cFirstCompatibleVersion = 1;
        constexpr const char* cDir = "sd:/SMO-Randomizer/Presets";
        constexpr s32 cPathLength = 128;

#pragma pack(push, 1)
        // This is the stable on-disk prefix. New fields must be appended after
        // the entries array so newer files can still be read by older builds.
        struct HeaderPrefix {
            u32 magic;
            u32 version;
            char16_t name[kNameLength];
            char description[kDescriptionLength];
            u8 entryCount;
        };

        struct Header {
            u32 magic;
            u32 version;
            char16_t name[kNameLength];
            char description[kDescriptionLength];
            u8 entryCount;
            Entry entries[kMaxEntries];
        };

        static_assert(offsetof(Header, entries) == sizeof(HeaderPrefix),
            "Preset entries must remain immediately after the stable prefix");

        struct Slot {
            Header header;
            bool valid = false;
            char path[cPathLength] = {};
        };

        Slot sSlots[kMaxUserPresets];

        al::StringTmp<128> makeLegacyPath(s32 index) {
            return al::StringTmp<128>("%s/preset_%02d.bin", cDir, index);
        }

        u32 nameHash(const char16_t* name) {
            // FNV-1a over both bytes of each UTF-16 code unit. The readable
            // part of the filename is useful to a person, while the hash
            // keeps names that sanitize to the same slug distinct.
            u32 hash = 2166136261u;
            for (s32 i = 0; name && name[i] != u'\0'; i++) {
                const u16 code = static_cast<u16>(name[i]);
                hash ^= static_cast<u8>(code & 0xffu);
                hash *= 16777619u;
                hash ^= static_cast<u8>((code >> 8) & 0xffu);
                hash *= 16777619u;
            }
            return hash;
        }

        bool isAsciiAlphaNumeric(char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                || (c >= '0' && c <= '9');
        }

        void makeNameSlug(char* out, s32 outSize, const char16_t* name) {
            if (!out || outSize <= 0)
                return;

            s32 length = 0;
            bool previousSeparator = false;
            for (s32 i = 0; name && name[i] != u'\0' && length < outSize - 1; i++) {
                const u16 code = static_cast<u16>(name[i]);
                char c = '_';
                if (code <= 0x7f && isAsciiAlphaNumeric(static_cast<char>(code)))
                    c = static_cast<char>(code);
                else if (code == u'-')
                    c = '-';

                if (c == '_') {
                    if (previousSeparator)
                        continue;
                    previousSeparator = true;
                } else {
                    previousSeparator = false;
                }
                out[length++] = c;
            }

            while (length > 0 && out[length - 1] == '_')
                length--;
            if (length == 0) {
                const char* fallback = "unnamed";
                for (s32 i = 0; fallback[i] != '\0' && i < outSize - 1; i++)
                    out[length++] = fallback[i];
            }
            out[length] = '\0';
        }

        al::StringTmp<128> makeNamedPath(const char16_t* name) {
            char slug[64] = {};
            makeNameSlug(slug, sizeof(slug), name);
            return al::StringTmp<128>("%s/preset_%s_%08x.bin", cDir, slug, nameHash(name));
        }

        void copyPath(char* out, const char* path) {
            if (!out)
                return;
            if (!path) {
                out[0] = '\0';
                return;
            }
            std::strncpy(out, path, cPathLength - 1);
            out[cPathLength - 1] = '\0';
        }

        bool isValidIndex(s32 index) {
            return index >= 0 && index < kMaxUserPresets;
        }

        bool isValidHeader(const Header& header) {
            // Versions are append-only metadata. Unknown trailing fields and
            // newer option IDs are intentionally ignored, so a version bump
            // must not make an otherwise readable preset disappear.
            return header.magic == cMagic && header.version >= cFirstCompatibleVersion;
        }

        bool readSlot(const char* path, Header* out) {
            if (!path || !out)
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;

            long fileSize = 0;
            const bool sizeOk = nn::fs::GetFileSize(&fileSize, handle).IsSuccess()
                && fileSize >= static_cast<long>(sizeof(HeaderPrefix));
            *out = {};

            // Read only the stable prefix first. This accepts both the old
            // fixed-size files and future files with an extended payload.
            const bool prefixRead = sizeOk
                && nn::fs::ReadFile(handle, 0, out, sizeof(HeaderPrefix)).IsSuccess();
            if (!prefixRead) {
                nn::fs::CloseFile(handle);
                return false;
            }

            const s32 storedCount = out->entryCount;
            const s32 readCount = storedCount > kMaxEntries ? kMaxEntries : storedCount;
            const long entriesEnd = static_cast<long>(sizeof(HeaderPrefix))
                + static_cast<long>(readCount) * sizeof(Entry);
            const bool entriesSizeOk = fileSize >= entriesEnd;
            const bool entriesRead = entriesSizeOk && (readCount == 0 || nn::fs::ReadFile(handle, sizeof(HeaderPrefix), out->entries, static_cast<ulong>(readCount) * sizeof(Entry)).IsSuccess());
            nn::fs::CloseFile(handle);
            if (!entriesRead)
                return false;

            out->name[kNameLength - 1] = u'\0';
            out->description[kDescriptionLength - 1] = '\0';
            out->entryCount = static_cast<u8>(readCount);
            return isValidHeader(*out);
        }

        bool writeSlot(const char* path, const Header& header) {
            if (!path)
                return false;

            SDPaths::ensureRandomizerRoot();
            nn::fs::CreateDirectory(cDir);

            nn::fs::DeleteFile(path);
            if (nn::fs::CreateFile(path, sizeof(Header)).IsFailure())
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsFailure())
                return false;

            const bool ok = nn::fs::WriteFile(handle, 0, &header, sizeof(Header),
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            return ok;
        }

        void clearSlot(Slot* slot) {
            if (!slot)
                return;
            slot->header = {};
            slot->valid = false;
            slot->path[0] = '\0';
        }

        s32 findFreeSlot();
        bool u16Equal(const char16_t* a, const char16_t* b);

        s32 findSlotByPath(const char* path) {
            if (!path)
                return -1;
            for (s32 i = 0; i < kMaxUserPresets; i++) {
                if (sSlots[i].valid && std::strcmp(sSlots[i].path, path) == 0)
                    return i;
            }
            return -1;
        }

        s32 findSlotByName(const char16_t* name) {
            if (!name)
                return -1;
            for (s32 i = 0; i < kMaxUserPresets; i++) {
                if (sSlots[i].valid && u16Equal(sSlots[i].header.name, name))
                    return i;
            }
            return -1;
        }

        bool addSlot(const char* path, const Header& header) {
            if (!path || findSlotByPath(path) >= 0 || findSlotByName(header.name) >= 0)
                return false;

            const s32 index = findFreeSlot();
            if (index < 0)
                return false;
            sSlots[index].header = header;
            sSlots[index].valid = true;
            copyPath(sSlots[index].path, path);
            return true;
        }

        bool hasBinExtension(const char* name) {
            if (!name)
                return false;
            const size_t length = std::strlen(name);
            return length > 4 && std::strcmp(name + length - 4, ".bin") == 0;
        }

        bool isLegacyPresetFilename(const char* name) {
            if (!name || std::strlen(name) != 13
                || std::strncmp(name, "preset_", 7) != 0
                || name[9] != '.' || std::strcmp(name + 9, ".bin") != 0)
                return false;
            return name[7] >= '0' && name[7] <= '9'
                && name[8] >= '0' && name[8] <= '9';
        }

        void loadNamedFiles() {
            nn::fs::DirectoryHandle handle;
            if (nn::fs::OpenDirectory(&handle, cDir, nn::fs::OpenDirectoryMode_File).IsFailure())
                return;

            while (true) {
                s64 entryCount = 0;
                nn::fs::DirectoryEntry entry;
                if (nn::fs::ReadDirectory(&entryCount, &entry, handle, 1).IsFailure() || entryCount == 0)
                    break;

                const char* name = entry.mName;
                if (!hasBinExtension(name) || isLegacyPresetFilename(name)
                    || std::strcmp(name, "preset.bin") == 0)
                    continue;
                const size_t nameLength = std::strlen(name);
                if (nameLength + std::strlen(cDir) + 2 >= cPathLength)
                    continue;

                char path[cPathLength] = {};
                std::strcpy(path, cDir);
                std::strcat(path, "/");
                std::strcat(path, name);

                Header header = {};
                if (readSlot(path, &header))
                    addSlot(path, header);
                if (isFull())
                    break;
            }
            nn::fs::CloseDirectory(handle);
        }

        s32 findFreeSlot() {
            for (s32 i = 0; i < kMaxUserPresets; i++) {
                if (!sSlots[i].valid)
                    return i;
            }
            return -1;
        }

        bool u16Equal(const char16_t* a, const char16_t* b) {
            if (!a || !b)
                return false;
            s32 i = 0;
            for (; a[i] != u'\0' || b[i] != u'\0'; i++) {
                if (a[i] != b[i])
                    return false;
            }
            return true;
        }

        void u16Copy(char16_t* dst, const char16_t* src, s32 dstCapacity) {
            s32 i = 0;
            for (; src && src[i] != u'\0' && i < dstCapacity - 1; i++)
                dst[i] = src[i];
            dst[i] = u'\0';
        }
    } // namespace

    void loadAll() {
        for (s32 i = 0; i < kMaxUserPresets; i++) {
            clearSlot(&sSlots[i]);
        }

        // Prefer the readable filename when both it and a legacy slot file
        // exist (for example, if cleanup of an old file was interrupted).
        loadNamedFiles();

        for (s32 i = 0; i < kMaxUserPresets; i++) {
            const al::StringTmp<128> path = makeLegacyPath(i);
            Header header = {};
            if (readSlot(path.cstr(), &header))
                addSlot(path.cstr(), header);
        }
    }

    s32 count() {
        s32 n = 0;
        for (s32 i = 0; i < kMaxUserPresets; i++) {
            if (sSlots[i].valid)
                n++;
        }
        return n;
    }

    bool isFull() {
        return findFreeSlot() < 0;
    }

    s32 nthValidIndex(s32 n) {
        if (n < 0)
            return -1;
        for (s32 i = 0; i < kMaxUserPresets; i++) {
            if (!sSlots[i].valid)
                continue;
            if (n == 0)
                return i;
            n--;
        }
        return -1;
    }

    const char16_t* getName(s32 index) {
        if (!isValidIndex(index) || !sSlots[index].valid)
            return u"";
        return sSlots[index].header.name;
    }

    const char* getDescription(s32 index) {
        if (!isValidIndex(index) || !sSlots[index].valid)
            return "";
        return sSlots[index].header.description;
    }

    const Entry* getEntries(s32 index, s32* outCount) {
        if (!isValidIndex(index) || !sSlots[index].valid) {
            if (outCount)
                *outCount = 0;
            return nullptr;
        }
        if (outCount)
            *outCount = sSlots[index].header.entryCount;
        return sSlots[index].header.entries;
    }

    s32 findByName(const char16_t* name) {
        return findSlotByName(name);
    }

    bool save(const char16_t* name, const char* description,
        const Entry* entries, s32 entryCount, s32 overwriteIndex) {
        const s32 index = overwriteIndex >= 0 ? overwriteIndex : findFreeSlot();
        if (!isValidIndex(index))
            return false;

        Header header = {};
        header.magic = cMagic;
        header.version = cCurrentVersion;
        u16Copy(header.name, name, kNameLength);
        if (description) {
            std::strncpy(header.description, description, kDescriptionLength - 1);
            header.description[kDescriptionLength - 1] = '\0';
        }
        header.entryCount = static_cast<u8>(
            entryCount > kMaxEntries ? kMaxEntries : (entryCount < 0 ? 0 : entryCount));
        if (header.entryCount > 0 && !entries)
            return false;
        for (s32 i = 0; i < header.entryCount; i++)
            header.entries[i] = entries[i];

        const al::StringTmp<128> newPath = makeNamedPath(header.name);
        if (!writeSlot(newPath.cstr(), header))
            return false;

        if (sSlots[index].valid && std::strcmp(sSlots[index].path, newPath.cstr()) != 0)
            nn::fs::DeleteFile(sSlots[index].path);
        sSlots[index].header = header;
        sSlots[index].valid = true;
        copyPath(sSlots[index].path, newPath.cstr());
        return true;
    }

    bool remove(s32 index) {
        if (!isValidIndex(index) || !sSlots[index].valid)
            return false;

        nn::fs::DeleteFile(sSlots[index].path);
        clearSlot(&sSlots[index]);
        return true;
    }

} // namespace UserPresets
