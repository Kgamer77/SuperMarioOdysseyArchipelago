#include "Randomizer/CaptureHooks.h"

#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/Randomizer.h"
#include "ShineDB.h"
#include "helpers.hpp"

#include "al/Library/LiveActor/LiveActor.h"
#include "game/Player/CapTargetInfo.h"
#include "game/Player/HackCap.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Player/PlayerStateHack.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include <cstring>
#include <typeinfo>

namespace al {
    class HitSensor;
}

namespace CaptureHooks {

    static const ShineDB::ShineEntry* findEntryFromHintInfo(const GameDataFile::HintInfo* info) {
        if (!info || info->stageName.isEmpty())
            return nullptr;
        return ShineDB::findEntry(info->uniqueId, info->stageName.cstr());
    }

    static void unlockFromHintInfo(GameDataFile* file, const GameDataFile::HintInfo* info) {
        if (!file)
            return;
        const ShineDB::ShineEntry* entry = findEntryFromHintInfo(info);
        if (!entry)
            return;
        if (AbilityRando::unlockForMoon(GameDataHolderWriter(file->getGameDataHolder()), *entry) == AbilityRando::UnlockResult::Unlocked)
            AbilityOverlay::queueLastUnlocks();
        CaptureRando::unlockForMoon(GameDataHolderWriter(file->getGameDataHolder()), *entry);
    }

    static const char* skipTypeNameLength(const char* name) {
        if (!name)
            return nullptr;

        const char* p = name;
        while (*p >= '0' && *p <= '9')
            p++;
        return p == name ? name : p;
    }

    static const CaptureRando::CaptureEntry* findCaptureByName(const char* name) {
        if (!name || !name[0])
            return nullptr;

        if (const CaptureRando::CaptureEntry* capture = CaptureRando::findCaptureByHackName(name))
            return capture;

        const char* convertedName = tryConvertName(name);
        if (convertedName != name)
            if (const CaptureRando::CaptureEntry* capture = CaptureRando::findCaptureByHackName(convertedName))
                return capture;

        const size_t len = std::strlen(name);
        static constexpr const char* kHackSuffix = "Hack";
        static constexpr size_t kHackSuffixLen = 4;
        if (len > kHackSuffixLen && std::strcmp(name + len - kHackSuffixLen, kHackSuffix) == 0 && len - kHackSuffixLen < 64) {
            char strippedName[64];
            std::memcpy(strippedName, name, len - kHackSuffixLen);
            strippedName[len - kHackSuffixLen] = '\0';
            return CaptureRando::findCaptureByHackName(strippedName);
        }

        return nullptr;
    }

    static const CaptureRando::CaptureEntry* findCaptureForActor(const al::LiveActor* actor) {
        if (!actor)
            return nullptr;

        const char* actorName = actor->getName();
        if (const CaptureRando::CaptureEntry* capture = findCaptureByName(actorName))
            return capture;

        const char* typeName = skipTypeNameLength(typeid(*actor).name());
        return findCaptureByName(typeName);
    }

    static const CaptureRando::CaptureEntry* findCaptureForSensor(const al::HitSensor* sensor) {
        return sensor ? findCaptureForActor(sensor->getParentActor()) : nullptr;
    }

    static const CaptureRando::CaptureEntry* findCaptureForCapTarget(const HackCap* cap) {
        const CapTargetInfo* targetInfo = cap ? cap->getCapTargetInfo() : nullptr;
        return targetInfo ? findCaptureByName(targetInfo->getHackName()) : nullptr;
    }

    static const CaptureRando::CaptureEntry* findCaptureForSenderCap(const al::HitSensor* sensor) {
        al::LiveActor* actor = sensor ? sensor->getParentActor() : nullptr;
        if (!actor)
            return nullptr;

        const char* typeName = skipTypeNameLength(typeid(*actor).name());
        if (std::strcmp(typeName, "HackCap") != 0)
            return nullptr;

        return findCaptureForCapTarget((HackCap*)actor);
    }

    static const CaptureRando::CaptureEntry* findStageStartCapture(const PlayerStateHack* state) {
        if (!state || !state->mHackKeeper)
            return nullptr;

        if (const CaptureRando::CaptureEntry* capture = findCaptureByName(state->mHackKeeper->getCurrentHackName()))
            return capture;
        if (state->mHackKeeper->mStageStartCapTargetInfo)
            if (const CaptureRando::CaptureEntry* capture = findCaptureByName(state->mHackKeeper->mStageStartCapTargetInfo->getHackName()))
                return capture;
        if (const CaptureRando::CaptureEntry* capture = findCaptureForActor(state->mHackKeeper->mStageStartActor))
            return capture;
        return findCaptureForSensor(state->mHackKeeper->mStageStartActorSensor);
    }

    static s32 sForceHackBypassDepth = 0;
    static s32 sTemporaryStageStartRecordSuppressDepth = 0;

    class ScopedForceHackBypass {
    public:
        ScopedForceHackBypass() { sForceHackBypassDepth++; }
        ~ScopedForceHackBypass() { sForceHackBypassDepth--; }
    };

    class ScopedTemporaryStageStartRecordSuppress {
    public:
        ScopedTemporaryStageStartRecordSuppress(bool enabled)
            : mEnabled(enabled) {
            if (mEnabled)
                sTemporaryStageStartRecordSuppressDepth++;
        }

        ~ScopedTemporaryStageStartRecordSuppress() {
            if (mEnabled && sTemporaryStageStartRecordSuppressDepth > 0)
                sTemporaryStageStartRecordSuppressDepth--;
        }

    private:
        bool mEnabled;
    };

    // Some variant captures internally run a second lock check against their base
    // species while the catch starts (e.g. capturing a Big Chain Chomp also checks
    // "Wanwan"). The variant unlock must be sufficient on its own, so passing the
    // variant check arms a one-shot skip for the base species check that follows.
    struct VariantBaseCapture {
        const char* variant;
        const char* base;
    };
    static constexpr VariantBaseCapture sVariantBaseCaptures[] = {
        { "WanwanBig", "Wanwan" }, // Big Chain Chomp -> Chain Chomp
        { "PukupukuSnow", "Pukupuku" }, // Cheep Cheep (Snow) -> Cheep Cheep
        { "KillerMagnum", "Killer" }, // Banzai Bill -> Bullet Bill
        { "Guidepost", "HackFork" }, // Pole -> Volbonan
    };
    static const char* sPendingBaseCheckSkip = nullptr;

    // Last capture each message was reported for; see isCaptureLocked.
    static const char* sLastLockedLogged = nullptr;
    static const char* sLastSkipLogged = nullptr;

    static void logCaptureOnce(const char*& lastLogged, const char* name, const char* format) {
        if (!name || (lastLogged && std::strcmp(lastLogged, name) == 0))
            return;
        lastLogged = name;
        Randomizer::logPush(format, name);
    }

    static bool isCaptureLocked(GameDataHolderAccessor accessor,
        const CaptureRando::CaptureEntry* capture) {
        if (!Randomizer::isCaptureLockFeatureEnabled())
            return false;
        if (sForceHackBypassDepth > 0)
            return false;
        if (!capture)
            return false;

        // These two fire from the capture-attempt path, which the game runs on
        // contact - so walking into a locked capture logged the same line every
        // frame of the touch. Report a state change, not an occurrence: repeat
        // the message only when the capture it names is different from the last
        // one reported. The strings are static CaptureEntry fields, so holding
        // the pointer is safe across frames.

        if (sPendingBaseCheckSkip && std::strcmp(capture->hackName, sPendingBaseCheckSkip) == 0) {
            sPendingBaseCheckSkip = nullptr;
            logCaptureOnce(sLastSkipLogged, capture->hackName,
                "skipping base capture check for %s (variant already unlocked)");
            return false;
        }

        if (CaptureRando::isCaptureUnlocked(accessor, *capture)) {
            for (const VariantBaseCapture& pair : sVariantBaseCaptures) {
                if (std::strcmp(capture->hackName, pair.variant) == 0) {
                    sPendingBaseCheckSkip = pair.base;
                    break;
                }
            }
            return false;
        }

        logCaptureOnce(sLastLockedLogged, capture->displayName, "capture locked: %s");
        return true;
    }

    static bool isSensorCaptureLocked(al::HitSensor* sensor) {
        const CaptureRando::CaptureEntry* capture = findCaptureForSensor(sensor);
        al::LiveActor* actor = sensor ? sensor->getParentActor() : nullptr;
        return isCaptureLocked(GameDataHolderAccessor(actor), capture);
    }

    static HkTrampoline<void, HackCap*, const char*, bool, const sead::Vector3f&>
        startCatchHook = hk::hook::trampoline(
            [](HackCap* cap, const char* catchName, bool isDirect, const sead::Vector3f& trans) -> void {
                const CaptureRando::CaptureEntry* capture = findCaptureByName(catchName);

                if (isCaptureLocked(GameDataHolderAccessor(cap), capture))
                    return;

                startCatchHook.orig(cap, catchName, isDirect, trans);
            });

    static HkTrampoline<void, HackCap*, al::HitSensor*, const CapTargetInfo*>
        forceHackHook = hk::hook::trampoline(
            [](HackCap* cap, al::HitSensor* sensor, const CapTargetInfo* targetInfo) -> void {
                ScopedForceHackBypass bypass;
                forceHackHook.orig(cap, sensor, targetInfo);
            });

    static HkTrampoline<void, PlayerStateHack*>
        prepareStageStartHackHook = hk::hook::trampoline(
            [](PlayerStateHack* state) -> void {
                ScopedTemporaryStageStartRecordSuppress suppress(
                    Randomizer::isCaptureRandoEnabled() && findStageStartCapture(state));
                prepareStageStartHackHook.orig(state);
            });

    static HkTrampoline<void, PlayerHackKeeper*>
        playerRecordHackHook = hk::hook::trampoline(
            [](PlayerHackKeeper* keeper) -> void {
                if (sTemporaryStageStartRecordSuppressDepth > 0) {
                    Randomizer::logPush("suppressing temporary stage-start capture record");
                    return;
                }

                playerRecordHackHook.orig(keeper);
            });

    static HkTrampoline<bool, al::HitSensor*, al::HitSensor*>
        sendMsgStartHackHook = hk::hook::trampoline(
            [](al::HitSensor* receiver, al::HitSensor* sender) -> bool {
                const CaptureRando::CaptureEntry* capture = findCaptureForSensor(receiver);
                if (!capture)
                    capture = findCaptureForSenderCap(sender);

                al::LiveActor* actor = receiver ? receiver->getParentActor() : nullptr;
                if (isCaptureLocked(GameDataHolderAccessor(actor), capture))
                    return false;
                return sendMsgStartHackHook.orig(receiver, sender);
            });

    static HkTrampoline<bool, HackCap*, al::HitSensor*>
        capSendMsgStartHackHook = hk::hook::trampoline(
            [](HackCap* cap, al::HitSensor* sensor) -> bool {
                const CaptureRando::CaptureEntry* capture = findCaptureForSensor(sensor);
                if (!capture)
                    capture = findCaptureForCapTarget(cap);
                if (isCaptureLocked(GameDataHolderAccessor(cap), capture))
                    return false;
                return capSendMsgStartHackHook.orig(cap, sensor);
            });

    static HkTrampoline<void, PlayerHackKeeper*, al::HitSensor*, al::HitSensor*, al::LiveActor*>
        playerStartHackHook = hk::hook::trampoline(
            [](PlayerHackKeeper* keeper, al::HitSensor* playerSensor, al::HitSensor* targetSensor,
                al::LiveActor* targetActor) -> void {
                const CaptureRando::CaptureEntry* capture = findCaptureForActor(targetActor);
                if (!capture)
                    capture = findCaptureForSensor(targetSensor);
                if (!capture && keeper && keeper->mHackCap && keeper->mHackCap->getCapTargetInfo())
                    capture = findCaptureByName(keeper->mHackCap->getCapTargetInfo()->getHackName());

                GameDataHolderAccessor accessor(targetActor ? targetActor : (keeper ? keeper->mParent : nullptr));
                if (isCaptureLocked(accessor, capture))
                    return;

                playerStartHackHook.orig(keeper, playerSensor, targetSensor, targetActor);
            });

    static HkTrampoline<void, GameDataFile*, const GameDataFile::HintInfo*>
        setGotShineHintHook = hk::hook::trampoline(
            [](GameDataFile* file, const GameDataFile::HintInfo* info) -> void {
                setGotShineHintHook.orig(file, info);
                unlockFromHintInfo(file, info);
            });

    static HkTrampoline<void, GameDataFile*, s32>
        setGotShineIndexHook = hk::hook::trampoline([](GameDataFile* file, s32 index) -> void {
            const GameDataFile::HintInfo* info = nullptr;
            if (file && index >= 0 && index < 0x400)
                info = &file->getHintList()[index];
            setGotShineIndexHook.orig(file, index);
            unlockFromHintInfo(file, info);
        });

    template <typename Hook>
    static void tryInstallHookAtDynamicSymbol(Hook& hook, const char* symbol, const char* label) {
        ptr addr = hk::ro::lookupSymbol(symbol);
        if (!addr) {
            Randomizer::logPush("capture hook missing: %s", label);
            return;
        }

        if (hook.installAtPtr(reinterpret_cast<void*>(addr)).succeeded())
            Randomizer::logPush("capture hook installed: %s", label);
    }

    void installHooks() {
        if (Randomizer::isCaptureLockFeatureEnabled()) {
            startCatchHook.installAtSym<"_ZN7HackCap10startCatchEPKcbRKN4sead7Vector3IfEE">();
            forceHackHook.installAtSym<"_ZN7HackCap9forceHackEPN2al9HitSensorEPK13CapTargetInfo">();
            prepareStageStartHackHook.installAtMainOffset(0x46F35C);
            playerRecordHackHook.installAtMainOffset(0x449674);
            tryInstallHookAtDynamicSymbol(
                sendMsgStartHackHook,
                "_ZN2rs16sendMsgStartHackEPN2al9HitSensorES2_",
                "rs::sendMsgStartHack");
            tryInstallHookAtDynamicSymbol(
                capSendMsgStartHackHook,
                "_ZN7HackCap16sendMsgStartHackEPN2al9HitSensorE",
                "HackCap::sendMsgStartHack");
            tryInstallHookAtDynamicSymbol(
                playerStartHackHook,
                "_ZN16PlayerHackKeeper9startHackEPN2al9HitSensorES2_PNS0_9LiveActorE",
                "PlayerHackKeeper::startHack");
        }

        if (Randomizer::isCaptureLockFeatureEnabled() || Randomizer::isAbilityLockFeatureEnabled()) {
            setGotShineHintHook.installAtSym<"_ZN12GameDataFile11setGotShineEPKNS_8HintInfoE">();
            setGotShineIndexHook.installAtSym<"_ZN12GameDataFile11setGotShineEi">();
        }
    }

} // namespace CaptureHooks
