#include "hk/hook/InstrUtil.h"
#include "hk/hook/Trampoline.h"

#include <nn/fs.h>

#include "sead/filedevice/nin/seadNinFileDeviceBaseNin.h"
#include "sead/filedevice/seadArchiveFileDevice.h"
#include "sead/filedevice/seadFileDeviceMgr.h"
#include "sead/filedevice/seadPath.h"

#include "al/Project/File/FileLoader.h"

#include <filedevice/nin/seadNinSDFileDeviceNin.h>
#include <filedevice/seadFileDeviceMgr.h>
#include <filedevice/seadPath.h>
#include <resource/seadArchiveRes.h>

#include "Library/File/FileUtil.h"
#include "Multiplayer/ServerConfig.h"
#include "Randomizer/GlobalConfig.h"
#include "SDRedirect/SDPaths.h"

namespace SDRedirect {

    static HkTrampoline<void, sead::FileDeviceMgr*> createFileDeviceMgrHook = hk::hook::trampoline([](sead::FileDeviceMgr* thisPtr) -> void {
        createFileDeviceMgrHook.orig(thisPtr);
        thisPtr->mMountedSd = nn::fs::MountSdCard("sd").IsSuccess();
        if (!thisPtr->mMountedSd) {
            return;
        }

        SDPaths::ensureRandomizerRoot();
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Presets");
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Saves");
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/Saves/_none");
        // Multiplayer's own, fully separate save tree (see SaveRedirect::
        // Category) - same shape as Saves/, so playing MP can never read
        // or write a single-player save.
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/MPSaves");
        nn::fs::CreateDirectory("sd:/SMO-Randomizer/MPSaves/_none");
        GlobalConfig::load();
        ServerConfig::load();

        // Archipelago separate save tree
        SDPaths::ensureRandomizerRoot();
        nn::fs::CreateDirectory("sd:/SMO-Archipelago/Saves");
        nn::fs::CreateDirectory("sd:/SMO-Archipelago/Saves/_none");
        nn::fs::CreateDirectory("sd:/SMO-Archipelago/MPSaves");
        nn::fs::CreateDirectory("sd:/SMO-Archipelago/MPSaves/_none");
        nn::fs::CreateDirectory("sd:/SMO-Archipelago/Multi-Data");

        sead::NinSDFileDevice* sdFileDevice = new sead::NinSDFileDevice();
        thisPtr->mount(sdFileDevice);
    });

    static HkTrampoline<sead::FileDevice*, sead::FileDeviceMgr*, sead::SafeString&, sead::BufferedSafeString*> redirectFileDeviceHook = hk::hook::trampoline([](sead::FileDeviceMgr* thisPtr, sead::SafeString& path, sead::BufferedSafeString* pathNoDrive) -> sead::FileDevice* {
        sead::FixedSafeString<32> driveName;
        if (!sead::Path::getDriveName(&driveName, path)) {
            sead::FileDevice* sdDevice = thisPtr->findDevice("sd");
            if (sdDevice && sdDevice->isExistFile(path)) {
                if (pathNoDrive != nullptr)
                    pathNoDrive->copy(path);
                return sdDevice;
            }
        }
        return redirectFileDeviceHook.orig(thisPtr, path, pathNoDrive);
    });

    static HkTrampoline<sead::ArchiveRes*, al::FileLoader*, sead::SafeString&, const char*, sead::FileDevice*> fileLoaderLoadArcHook = hk::hook::trampoline([](al::FileLoader* thisPtr, sead::SafeString& path, const char* ext, sead::FileDevice* device) -> sead::ArchiveRes* {
        sead::FileDevice* sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");
        if (sdFileDevice && sdFileDevice->isExistFile(path))
            device = sdFileDevice;
        return fileLoaderLoadArcHook.orig(thisPtr, path, ext, device);
    });

    static HkTrampoline<bool, al::FileLoader*, sead::SafeString&, sead::FileDevice*> fileLoaderIsExistFileHook = hk::hook::trampoline([](al::FileLoader* thisPtr, sead::SafeString& path, sead::FileDevice* device) -> bool {
        sead::FileDevice* sdFileDevice = sead::FileDeviceMgr::instance()->findDevice("sd");
        if (sdFileDevice && sdFileDevice->isExistFile(path))
            device = sdFileDevice;
        return fileLoaderIsExistFileHook.orig(thisPtr, path, device);
    });

    static HkTrampoline<bool, sead::NinFileDeviceBase*, bool*, const sead::SafeString&> doIsExistDirectoryHook = hk::hook::trampoline([](sead::NinFileDeviceBase* thisPtr, bool* out, const sead::SafeString& path) -> bool {
        if (thisPtr->mDriveName.isEqual("sd")) {
            *out = true;
            return true;
        }
        return doIsExistDirectoryHook.orig(thisPtr, out, path);
    });

    void installHooks() {
        createFileDeviceMgrHook.installAtMainOffset(0x76C8D4);
        redirectFileDeviceHook.installAtMainOffset(0x76CFE0);
        fileLoaderLoadArcHook.installAtMainOffset(0xA5EF64);
        fileLoaderIsExistFileHook.installAtMainOffset(0xA5ED28);
        doIsExistDirectoryHook.installAtMainOffset(0x772A54);
    }

} // namespace SDRedirect
