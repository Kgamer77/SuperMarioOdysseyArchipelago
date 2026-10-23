#include "Randomizer/InputGateHooks.h"

#include "Randomizer/BingoBoard.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/TimerMode.h"

#include "hk/hook/Trampoline.h"

#include "al/Library/Controller/InputFunction.h"

#include "types.h"
#include <math/seadVector.h>

namespace al {
    class IUseSceneObjHolder;
}

namespace InputGate {
    namespace {
        bool shouldBlockHintConfirmationInput() {
            return HintSystem::isConfirmationDialogActive() && !HintSystem::isPollingConfirmationDialogInput();
        }

        // The board's opening lock and Timer Mode's are the same mechanism for
        // two mutually exclusive run modes, so at most one is ever active.
        bool shouldBlockGameplayInput() {
            return Bingo::shouldBlockGameplayInput() || TimerMode::shouldBlockGameplayInput();
        }

        bool consumeStartTrigger() {
            return Bingo::consumeStartTrigger() || TimerMode::consumeStartTrigger();
        }

        HkTrampoline<bool, s32> isPadTriggerAHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput())
                return false;

            return isPadTriggerAHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerBHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput())
                return false;

            return isPadTriggerBHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerYHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput())
                return false;

            if (!isPadTriggerYHook.orig(port))
                return false;
            if (HintSystem::tryHandleListBuyInput())
                return false;
            return true;
        });

        HkTrampoline<bool, s32> isPadTriggerLeftHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput()
                || Bingo::consumeHorizontalTrigger())
                return false;
            return isPadTriggerLeftHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerRightHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput()
                || Bingo::consumeHorizontalTrigger())
                return false;
            return isPadTriggerRightHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerZLHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockGameplayInput() || consumeStartTrigger())
                return false;
            return isPadTriggerZLHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerZRHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockGameplayInput() || consumeStartTrigger())
                return false;
            return isPadTriggerZRHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerXHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockGameplayInput())
                return false;
            return isPadTriggerXHook.orig(port);
        });

        HkTrampoline<const sead::Vector2f&, s32> getLeftStickHook = hk::hook::trampoline([](s32 port) -> const sead::Vector2f& {
            if (shouldBlockGameplayInput())
                return sead::Vector2f::zero;
            return getLeftStickHook.orig(port);
        });

        HkTrampoline<const sead::Vector2f&, s32> getRightStickHook = hk::hook::trampoline([](s32 port) -> const sead::Vector2f& {
            if (shouldBlockGameplayInput())
                return sead::Vector2f::zero;
            return getRightStickHook.orig(port);
        });

        HkTrampoline<bool, s32> isPadTriggerDownHook = hk::hook::trampoline([](s32 port) -> bool {
            if (shouldBlockHintConfirmationInput() || shouldBlockGameplayInput())
                return false;

            return isPadTriggerDownHook.orig(port);
        });

        HkTrampoline<bool, const al::IUseSceneObjHolder*> isTriggerUiDecideHook = hk::hook::trampoline([](const al::IUseSceneObjHolder* holder) -> bool {
            if (!isTriggerUiDecideHook.orig(holder))
                return false;
            if (HintSystem::tryHandleListHintInput())
                return false;
            return true;
        });

        HkTrampoline<bool, const al::IUseSceneObjHolder*> isTriggerUiCancelHook = hk::hook::trampoline([](const al::IUseSceneObjHolder* holder) -> bool {
            if (HintSystem::shouldPulseCollectionListCancel())
                return true;

            if (!isTriggerUiCancelHook.orig(holder))
                return false;
            // Checked before the confirmation block: the route panel is not a
            // confirmation, and B must reach it rather than closing the list.
            if (HintSystem::tryHandleListCancelInput())
                return false;
            if (shouldBlockHintConfirmationInput())
                return false;
            return true;
        });
    } // namespace

    void installHooks() {
        isPadTriggerAHook.installAtSym<"_ZN2al13isPadTriggerAEi">();
        isPadTriggerBHook.installAtMainOffset(0x85c380);
        isPadTriggerYHook.installAtSym<"_ZN2al13isPadTriggerYEi">();
        isPadTriggerXHook.installAtSym<"_ZN2al13isPadTriggerXEi">();
        isPadTriggerLeftHook.installAtSym<"_ZN2al16isPadTriggerLeftEi">();
        isPadTriggerRightHook.installAtSym<"_ZN2al17isPadTriggerRightEi">();
        isPadTriggerZLHook.installAtSym<"_ZN2al14isPadTriggerZLEi">();
        isPadTriggerZRHook.installAtSym<"_ZN2al14isPadTriggerZREi">();
        isPadTriggerDownHook.installAtSym<"_ZN2al16isPadTriggerDownEi">();
        getLeftStickHook.installAtSym<"_ZN2al12getLeftStickEi">();
        getRightStickHook.installAtSym<"_ZN2al13getRightStickEi">();
        isTriggerUiDecideHook.installAtSym<"_ZN2rs17isTriggerUiDecideEPKN2al18IUseSceneObjHolderE">();
        isTriggerUiCancelHook.installAtSym<"_ZN2rs17isTriggerUiCancelEPKN2al18IUseSceneObjHolderE">();
    }
} // namespace InputGate
