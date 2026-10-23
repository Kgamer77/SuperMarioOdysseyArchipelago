#include "hk/hook/Trampoline.h"

#include <nn/fs.h>

#include <filedevice/nin/seadNinSaveFileDeviceNin.h>
#include <filedevice/seadFileDevice.h>
#include <filedevice/seadFileDeviceMgr.h>

#include "Library/Base/StringUtil.h"
#include "Multiplayer/MpDebugLog.h"
#include "Randomizer/ArrivalLog.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/Randomizer.h"
#include "SD/SDHooks.h"
#include "SDRedirect/SDPaths.h"

namespace SaveRedirect {
    namespace {
        static s32 sActiveSlot = -1;
        static Category sActiveCategory = Category::SinglePlayer;

        bool isValidSlot(s32 slot) {
            return slot >= 1 && slot <= 10;
        }

        bool isCommonSave(const char* name) {
            return name && al::isEqualString(name, "Common.bin");
        }

        bool isSdMounted() {
            sead::FileDeviceMgr* manager = sead::FileDeviceMgr::instance();
            return manager && manager->hasMountedSd();
        }

        // "Saves" (single-player) vs "MPSaves" (multiplayer) - two fully
        // separate directory trees, same Slot<N> shape under each, so playing
        // MP can never read or write a single-player save.
        const char* categoryRoot(Category category) {
            switch (category) {
            case Category::Multiplayer:
                return "MPSaves";
            case Category::MultiplayerGuest:
                return "MPSaves/_guest";

            case Category::Archipelago_MultiData:
                return "MultiData";

            default:
                return "Saves";
            }
        }

        const char* commonRoot(Category category) {
            return category == Category::SinglePlayer ? "Saves" : "MPSaves";
        }

        void ensureSaveRoot(Category category) {
            SDPaths::ensureRandomizerRoot();
            al::StringTmp<64> root = { "sd:/SMO-Randomizer/%s", categoryRoot(category) };
            nn::fs::CreateDirectory(root.cstr());
            al::StringTmp<64> none = { "sd:/SMO-Randomizer/%s/_none", categoryRoot(category) };
            nn::fs::CreateDirectory(none.cstr());
        }

        void ensureActiveSlotDirectory() {
            if (!isValidSlot(sActiveSlot))
                return;

            ensureSaveRoot(sActiveCategory);
            al::StringTmp<96> slotPath = { "sd:/SMO-Randomizer/%s/Slot%d", categoryRoot(sActiveCategory), sActiveSlot };
            nn::fs::CreateDirectory(slotPath.cstr());
        }

        void makeWritePath(al::StringTmp<128>* out, const char* name) {
            ensureSaveRoot(sActiveCategory);
            if (isCommonSave(name) || !isValidSlot(sActiveSlot)) {
                out->format("SMO-Randomizer/%s/%s", isCommonSave(name) ? commonRoot(sActiveCategory) : categoryRoot(sActiveCategory), name);
                return;
            }

            ensureActiveSlotDirectory();
            out->format("SMO-Randomizer/%s/Slot%d/%s", categoryRoot(sActiveCategory), sActiveSlot, name);
        }

        void makeReadPath(al::StringTmp<128>* out, const char* name) {
            if (isCommonSave(name)) {
                out->format("SMO-Randomizer/%s/%s", commonRoot(sActiveCategory), name);
                return;
            }

            if (!isValidSlot(sActiveSlot)) {
                out->format("SMO-Randomizer/%s/_none/%s", categoryRoot(sActiveCategory), name);
                return;
            }

            out->format("SMO-Randomizer/%s/Slot%d/%s", categoryRoot(sActiveCategory), sActiveSlot, name);
        }

        bool deleteFileIfPresent(const char* path) {
            if (!path || !path[0])
                return false;
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return true;
            nn::fs::CloseFile(handle);
            return nn::fs::DeleteFile(path).IsSuccess();
        }
    } // namespace

    static HkTrampoline<s32, void*, sead::FileDevice*, const char*> saveWriteHook = hk::hook::trampoline([](void* thisPtr, sead::FileDevice* _, const char* name) -> s32 {
        if (!isSdMounted()) {
            Randomizer::logPush("SAVE REDIRECT write bypassed: SD unavailable name=%s", name ? name : "<null>");
            return saveWriteHook.orig(thisPtr, _, name);
        }

        al::StringTmp<128> newPath;
        makeWritePath(&newPath, name);
        Randomizer::logPush("SAVE REDIRECT write active=%d name=%s path=%s", sActiveSlot, name ? name : "<null>", newPath.cstr());
        sead::NinSaveFileDevice newDevice = sead::NinSaveFileDevice("sd");
        const s32 result = saveWriteHook.orig(thisPtr, &newDevice, newPath.cstr());
        if (!isCommonSave(name)) {
            Bingo::flushIfDirty();
            ArrivalLog::flushIfDirty(sActiveSlot);
        }
        Randomizer::logPush("SAVE REDIRECT write result=%d name=%s", result, name ? name : "<null>");
        return result;
    });

    static HkTrampoline<s32, void*, sead::FileDevice*, const char*, void*> saveReadHook = hk::hook::trampoline([](void* thisPtr, sead::FileDevice* _, const char* name, void* flags) -> s32 {
        if (!isSdMounted()) {
            Randomizer::logPush("SAVE REDIRECT read bypassed: SD unavailable name=%s", name ? name : "<null>");
            return saveReadHook.orig(thisPtr, _, name, flags);
        }

        ensureSaveRoot(sActiveCategory);
        al::StringTmp<128> newPath;
        makeReadPath(&newPath, name);
        Randomizer::logPush("SAVE REDIRECT read active=%d name=%s path=%s", sActiveSlot, name ? name : "<null>", newPath.cstr());
        sead::NinSaveFileDevice newDevice = sead::NinSaveFileDevice("sd");
        const s32 result = saveReadHook.orig(thisPtr, &newDevice, newPath.cstr(), flags);
        Randomizer::logPush("SAVE REDIRECT read result=%d name=%s", result, name ? name : "<null>");
        // Mirrored to the SD-card log because logPush only reaches the
        // emulator's own console, which Citron does not surface - and
        // this is the line that says which file the game actually
        // asked for, which is exactly what a guest loading a
        // downloaded room save needs to prove.
        MpDebugLog::write("SAVE READ name=%s -> %s (active=%d cat=%d result=%d)", name ? name : "<null>", newPath.cstr(), sActiveSlot, (int)sActiveCategory,
            result);
        return result;
    });

    void setActiveSlot(s32 slot, Category category) {
        // Only update state here. The slot directory is created lazily by
        // makeWritePath()/RandoSaveFile at write time, when the SD card is
        // guaranteed to be mounted. Doing fs work here is unsafe because
        // setActiveSlot can run during hkMain (before the SD is mounted).
        sActiveSlot = isValidSlot(slot) ? slot : -1;
        sActiveCategory = category;
        Randomizer::logPush("SAVE REDIRECT active=%d category=%d requested=%d", sActiveSlot, (int)sActiveCategory, slot);
    }

    s32 getActiveSlot() {
        return sActiveSlot;
    }

    Category getActiveCategory() {
        return sActiveCategory;
    }

    bool deleteSlot(s32 slot, u32 seed, Category category) {
        if (!isValidSlot(slot) || !isSdMounted())
            return false;

        const bool wasActive = sActiveSlot == slot && sActiveCategory == category;
        const char* root = categoryRoot(category);

        bool ok = true;
        al::StringTmp<128> path;
        path.format("sd:/SMO-Randomizer/%s/Slot%d/rando.bin", root, slot);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot%d/rando.bin.pending", root, slot);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot%d/rando.bin.backup", root, slot);
        ok = deleteFileIfPresent(path.cstr()) && ok;

        // SaveData can retain a rotating set of File1.bin through File10.bin.
        // File0.bin existed in older redirect builds, so remove it as well.
        for (s32 i = 0; i <= 10; i++) {
            path.format("sd:/SMO-Randomizer/%s/Slot%d/File%d.bin", root, slot, i);
            ok = deleteFileIfPresent(path.cstr()) && ok;
        }

        ok = Bingo::remove(slot, category) && ok;
        ok = ArrivalLog::remove(slot) && ok;

        // Spoiler logs are keyed by seed only, still shared across
        // categories - a MP and SP save that happen to land on the same seed
        // share a spoiler log entry, same as two SP slots always have. Not
        // worth splitting; the seed itself is already the real key.
        path.format("sd:/SMO-Randomizer/Spoiler-Logs/SpoilerLog - %u.txt", seed);
        ok = deleteFileIfPresent(path.cstr()) && ok;

        // Keep the active routing in place when an I/O failure leaves the
        // deletion incomplete.  The death flow stays closed and can retry,
        // rather than accidentally creating a slotless fresh save.
        if (ok && wasActive)
            setActiveSlot(-1, category);

        Randomizer::logPush("SAVE REDIRECT delete slot=%d category=%d seed=%u ok=%d", slot, (int)category, seed, (int)ok);
        return ok;
    }

    bool deleteGuestTemp() {
        if (!isSdMounted())
            return false;
        bool ok = true;
        al::StringTmp<160> path;
        const char* root = categoryRoot(Category::MultiplayerGuest);
        path.format("sd:/SMO-Randomizer/%s/Slot1/rando.bin", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot1/rando.bin.pending", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot1/rando.bin.backup", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        for (s32 i = 0; i <= 10; i++) {
            path.format("sd:/SMO-Randomizer/%s/Slot1/File%d.bin", root, i);
            ok = deleteFileIfPresent(path.cstr()) && ok;
        }
        path.format("sd:/SMO-Randomizer/%s/Slot1/board.bin", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot1/arrivals.bin", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        path.format("sd:/SMO-Randomizer/%s/Slot1/tracker_room_code.bin", root);
        ok = deleteFileIfPresent(path.cstr()) && ok;
        if (ok && sActiveCategory == Category::MultiplayerGuest)
            setActiveSlot(-1, Category::MultiplayerGuest);
        return ok;
    }

    void installHooks() {
        // No active slot until the title file-select screen picks one; slotless
        // file reads fall back to vanilla fresh-file init. (Also: fs work here
        // would abort, since the SD card is not mounted yet at install time.)
        saveWriteHook.installAtSym<"_ZN2al21SaveDataSequenceWrite5writeEPN4sead10FileDeviceEPKc">();
        saveReadHook.installAtSym<"_ZN2al20SaveDataSequenceRead4readEPN4sead10FileDeviceEPKcPj">();
    }

} // namespace SaveRedirect
