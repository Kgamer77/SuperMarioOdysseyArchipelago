#pragma once
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/NerveStateBase.h"

#include "game/Player/HackCap.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerContinuousJump.h"
#include "game/Player/PlayerCounterAfterCapCatch.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Player/PlayerInput.h"
#include "game/Player/PlayerJudgeStartGroundSpin.h"
#include "game/Player/PlayerJudgeStartHipDrop.h"
#include "game/Player/PlayerJudgeStartRolling.h"
#include "game/Player/PlayerJudgeStartSquat.h"
#include "game/Player/PlayerJudgeWallCatch.h"
#include "game/Player/PlayerJudgeWallCatchInputDir.h"
#include "game/Player/PlayerJudgeWallKeep.h"
#include "game/Player/PlayerStateHipDrop.h"
#include "game/Player/PlayerStateSquat.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/ActorDimensionKeeper.h"
#include "game/Util/PlayerUtil.h"

#include <basis/seadTypes.h>
#include <cstdint>
#include <math/seadVector.h>
#include <typeinfo>

#include "Archipelago/Archipelago.h"
#include "imgui.h"
// #include "server/gamemode/GameModeManager.hpp"

class PlayerJudgePoleClimb;

namespace {

    GameDataHolder* sLastGameData = nullptr;

    // The al::isPadTrigger* symbols are unreliable here (isPadTriggerB isn't even in the
    // symbol set), so derive one-frame triggers from the confirmed-working isPadHold* in
    // PlayerInput::update (runs once/frame for the main player). Used by motion-free controls.
    bool sMainPadXHeldPrev = false, sMainPadXTriggered = false;
    bool sMainPadBHeldPrev = false, sMainPadBTriggered = false;
    bool sMainPadRHeldPrev = false, sMainPadRTriggered = false;

    void noteAccessor(GameDataHolderAccessor accessor) {
        if (accessor.mData)
            sLastGameData = accessor.mData;
    }

    GameDataHolderAccessor accessorForLastPlayer() {
        return GameDataHolderAccessor(sLastGameData);
    }

    GameDataHolderAccessor accessorForActor(const al::LiveActor* actor) {
        GameDataHolderAccessor accessor(actor);
        noteAccessor(accessor);
        return accessor;
    }

    GameDataHolderAccessor accessorForInput(const PlayerInput* input) {
        return accessorForActor(input ? input->mLiveActor : nullptr);
    }

    bool isUnlocked(GameDataHolderAccessor accessor, Archipelago::AbilityId ability) {
        noteAccessor(accessor);
        if (!Archipelago::isArchipelagoActive())
            return true;
        return Archipelago::isAbilityUnlocked(ability);
    }

    bool isUnlockedForActor(const al::LiveActor* actor, Archipelago::AbilityId ability) {
        return isUnlocked(accessorForActor(actor), ability);
    }

    bool isUnlockedForInput(const PlayerInput* input, Archipelago::AbilityId ability) {
        return isUnlocked(accessorForInput(input), ability);
    }

    bool isUnlockedForLastPlayer(Archipelago::AbilityId ability) {
        return isUnlocked(accessorForLastPlayer(), ability);
    }

    bool isMotionFreeControlsEnabled() {
        if (!Archipelago::isArchipelagoActive())
            return false;

        return Archipelago::isMotionRebound();
    }

    bool isAnyPrimaryCapThrowUnlocked(GameDataHolderAccessor accessor) {
        if (!Archipelago::isArchipelagoActive())
            return true;

        return Archipelago::isAbilityUnlocked(Archipelago::AbilityId_NeutralThrow) || Archipelago::isAbilityUnlocked(Archipelago::AbilityId_UpThrow) || Archipelago::isAbilityUnlocked(Archipelago::AbilityId_DownThrow) || Archipelago::isAbilityUnlocked(Archipelago::AbilityId_SpinThrow);
    }

    // Classify a cap-throw swing direction into a single throw type and require that ability. The
    // game's isThrowType* predicates overlap (a horizontal swing is both "LeftRight" and "Spiral";
    // any vertical swing is "Rolling" regardless of up/down), so we instead split by dominant axis
    // and the sign of Y. A downward swing maps to the same throw whether the player meant "neutral"
    // or "down" (the game produces identical input for both), so allow it if either is unlocked.
    bool canStartCapThrowForInput(const PlayerInput* input) {
        if (!input)
            return false;
        const sead::Vector2f& dir = input->getCapThrowDir();
        const float eps = 0.001f;
        const float ax = dir.x < 0.0f ? -dir.x : dir.x;
        const float ay = dir.y < 0.0f ? -dir.y : dir.y;
        if (ax < eps && ay < eps)
            return isUnlockedForInput(input, Archipelago::AbilityId_NeutralThrow);
        if (ax > ay)
            return isUnlockedForInput(input, Archipelago::AbilityId_SpinThrow); // horizontal -> spin
        if (dir.y > 0.0f)
            return isUnlockedForInput(input, Archipelago::AbilityId_UpThrow); // upward -> up throw
        return isUnlockedForInput(input, Archipelago::AbilityId_NeutralThrow) || isUnlockedForInput(input, Archipelago::AbilityId_DownThrow); // downward -> neutral/down
    }

    // Strict gate for the double-hand (both-joysticks) throw, which is the ONLY real
    // up/down throw. A downward swing here is a genuine Down Throw, so require DownThrow
    // (no neutral fallback) — that fallback only exists so a jittery single-hand neutral
    // press isn't misread as a down throw, and a single-hand press never takes this path.
    bool canStartDoubleHandThrow(const PlayerInput* input) {
        if (!input)
            return false;
        const sead::Vector2f& dir = input->getCapThrowDir();
        const float eps = 0.001f;
        const float ax = dir.x < 0.0f ? -dir.x : dir.x;
        const float ay = dir.y < 0.0f ? -dir.y : dir.y;
        if (ax < eps && ay < eps)
            return isUnlockedForInput(input, Archipelago::AbilityId_NeutralThrow);
        if (ax > ay)
            return isUnlockedForInput(input, Archipelago::AbilityId_SpinThrow);
        if (dir.y > 0.0f)
            return isUnlockedForInput(input, Archipelago::AbilityId_UpThrow);
        return isUnlockedForInput(input, Archipelago::AbilityId_DownThrow); // strict: real down throw
    }

    bool isAnyPrimaryCapThrowUnlockedForActor(const al::LiveActor* actor) {
        return isAnyPrimaryCapThrowUnlocked(accessorForActor(actor));
    }

    bool isHakoniwaPlayerInput(const PlayerInput* input) {
        return input && input->mLiveActor && typeid(*input->mLiveActor) == typeid(PlayerActorHakoniwa);
    }

    bool isPlayerInHipDropState(const PlayerInput* input) {
        if (!isHakoniwaPlayerInput(input))
            return false;

        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        return player->mStateHipDrop && !player->mStateHipDrop->isDead();
    }

    bool isPlayerInRollingState(const PlayerInput* input) {
        if (!isHakoniwaPlayerInput(input))
            return false;

        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        const al::NerveStateBase* rollingState = reinterpret_cast<const al::NerveStateBase*>(player->mStateRolling);
        return rollingState && !rollingState->isDead();
    }

    // True while controlling a capture. The B-button cappy moves (down-throw, jump
    // suppression) only apply to Mario on foot; in a capture B stays a normal jump.
    bool isPlayerHacking(const PlayerInput* input) {
        if (!isHakoniwaPlayerInput(input))
            return false;
        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        const PlayerHackKeeper* keeper = player->getPlayerHackKeeper();
        return keeper && keeper->isHack();
    }

    // True in 2D sections. The down-throw is a 3D move and B must stay a normal jump in 2D.
    bool isPlayerIn2D(const PlayerInput* input) {
        if (!isHakoniwaPlayerInput(input))
            return false;
        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        const ActorDimensionKeeper* keeper = player->getActorDimensionKeeper();
        return keeper && keeper->is2D();
    }

    // Motion-free B is the down-throw, which is the vanilla "both joysticks down while
    // airborne" move (the double-hand throw path). It only fires in the air — on the
    // ground it does nothing, like the motion does when standing still.
    bool isMotionFreeDownThrow(const PlayerInput* self) {
        return isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && !isPlayerHacking(self) && !isPlayerIn2D(self) && sMainPadBTriggered && self->mLiveActor && !rs::isPlayerOnGround(self->mLiveActor) && isUnlockedForInput(self, Archipelago::AbilityId_DownThrow);

        return false;
    }

    static HkTrampoline<void, PlayerInput*> playerInputUpdateHook = hk::hook::trampoline([](PlayerInput* self) -> void {
        accessorForInput(self);
        playerInputUpdateHook.orig(self);
        if (isHakoniwaPlayerInput(self)) {
            const bool xHeld = al::isPadHoldX();
            const bool bHeld = al::isPadHoldB();
            const bool rHeld = al::isPadHoldR();
            sMainPadXTriggered = xHeld && !sMainPadXHeldPrev;
            sMainPadBTriggered = bHeld && !sMainPadBHeldPrev;
            sMainPadRTriggered = rHeld && !sMainPadRHeldPrev;
            sMainPadXHeldPrev = xHeld;
            sMainPadBHeldPrev = bHeld;
            sMainPadRHeldPrev = rHeld;
        }
    });

    static HkTrampoline<bool, PlayerJudgeStartRolling*> rollHook = hk::hook::trampoline([](PlayerJudgeStartRolling* self) -> bool {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, Archipelago::AbilityId_Roll))
            return false;
        return rollHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> rollBoostHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_RollBoost))
            return false;
        if (rollBoostHook.orig(self))
            return true;
        // Motion-free: R is the shake, so it boosts the roll (this is
        // isTriggerRollingRestartSwing).
        return isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && sMainPadRTriggered;
    });

    static HkTrampoline<bool, const PlayerInput*, bool> rollTriggerHook = hk::hook::trampoline([](const PlayerInput* self, bool allowRestart) -> bool {
        if (!isHakoniwaPlayerInput(self))
            return rollTriggerHook.orig(self, allowRestart);
        if (!isUnlockedForInput(self, Archipelago::AbilityId_Roll))
            return false;
        if (isPlayerInRollingState(self) && !isUnlockedForInput(self, Archipelago::AbilityId_RollBoost))
            return false;
        return rollTriggerHook.orig(self, allowRestart);
    });

    static HkTrampoline<bool, PlayerJudgeStartHipDrop*> groundPoundHook = hk::hook::trampoline([](PlayerJudgeStartHipDrop* self) -> bool {
        if (!isUnlockedForInput(self ? self->mInput : nullptr, Archipelago::AbilityId_GroundPound))
            return false;
        return groundPoundHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> triggerHipDropInputHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_GroundPound))
            return false;
        return triggerHipDropInputHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> holdHipDropHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_GroundPound))
            return false;
        return holdHipDropHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*, bool> rollingCancelHipDropHook = hk::hook::trampoline([](const PlayerInput* self, bool allowSpin) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_GroundPound))
            return false;
        return rollingCancelHipDropHook.orig(self, allowSpin);
    });

    static HkTrampoline<bool, const PlayerInput*> diveHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_Dive))
            return false;
        return diveHook.orig(self);
    });

    static HkTrampoline<bool, PlayerJudgeStartGroundSpin*> spinHook = hk::hook::trampoline([](PlayerJudgeStartGroundSpin* self) -> bool {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, Archipelago::AbilityId_Spin))
            return false;
        return spinHook.orig(self);
    });

    static HkTrampoline<bool, PlayerJudgeStartSquat*> crouchHook = hk::hook::trampoline([](PlayerJudgeStartSquat* self) -> bool {
        if (!isUnlockedForInput(self ? self->mInput : nullptr, Archipelago::AbilityId_Crouch))
            return false;
        return crouchHook.orig(self);
    });

    static HkTrampoline<bool, PlayerStateSquat*> longJumpHook = hk::hook::trampoline([](PlayerStateSquat* self) -> bool {
        if (!isUnlockedForActor(self ? self->mActor : nullptr, Archipelago::AbilityId_LongJump))
            return false;
        return longJumpHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> jumpHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        // Motion-free: on the ground B is a normal jump; once airborne it becomes the
        // down-throw, so suppress only an airborne B-jump (A still jumps if pressed
        // together). B also stays a normal jump in a capture and in 2D.
        if (isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && !isPlayerHacking(self) && !isPlayerIn2D(self) && sMainPadBTriggered && !al::isPadHoldA() && self->mLiveActor && !rs::isPlayerOnGround(self->mLiveActor))
            return false;
        if (!jumpHook.orig(self))
            return false;
        if (!isHakoniwaPlayerInput(self))
            return true;
        if (isPlayerInHipDropState(self))
            return isUnlockedForInput(self, Archipelago::AbilityId_GroundPoundJump);
        if (!self || !self->isHoldSquat())
            return true;
        if (self->isMoveDeepDown() && isUnlockedForInput(self, Archipelago::AbilityId_LongJump))
            return true;
        return isUnlockedForInput(self, Archipelago::AbilityId_BackFlip);
    });

    static HkTrampoline<void, PlayerJudgeWallKeep*> wallSlideHook = hk::hook::trampoline([](PlayerJudgeWallKeep* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, Archipelago::AbilityId_WallJump)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        wallSlideHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgePoleClimb*> climbHook = hk::hook::trampoline([](PlayerJudgePoleClimb* self) -> void {
        if (!isUnlockedForLastPlayer(Archipelago::AbilityId_Climb))
            return;
        climbHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgeWallCatch*> ledgeGrabHook = hk::hook::trampoline([](PlayerJudgeWallCatch* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, Archipelago::AbilityId_LedgeGrab)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        ledgeGrabHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgeWallCatchInputDir*> ledgeGrabInputDirHook = hk::hook::trampoline([](PlayerJudgeWallCatchInputDir* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, Archipelago::AbilityId_LedgeGrab)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        ledgeGrabInputDirHook.orig(self);
    });

    static HkTrampoline<bool, al::ActorStateBase*, sead::Vector3f*> sideSomersaultHook = hk::hook::trampoline([](al::ActorStateBase* self, sead::Vector3f* vec) -> bool {
        if (!isUnlockedForActor(self ? self->mActor : nullptr, Archipelago::AbilityId_SideFlip))
            return false;
        return sideSomersaultHook.orig(self, vec);
    });

    static HkTrampoline<bool, al::ActorStateBase*, PlayerContinuousJump*> continuousJumpHook = hk::hook::trampoline([](al::ActorStateBase* self, PlayerContinuousJump* continuousJump) -> bool {
        u32 jumpType = *reinterpret_cast<u32*>(reinterpret_cast<uintptr_t>(self) + 0xb0) + 1;
        GameDataHolderAccessor accessor = accessorForActor(self ? self->mActor : nullptr);

        if (jumpType == 1 && !isUnlocked(accessor, Archipelago::AbilityId_DoubleJump))
            return false;
        if (jumpType >= 2 && !isUnlocked(accessor, Archipelago::AbilityId_TripleJump))
            return false;

        return continuousJumpHook.orig(self, continuousJump);
    });

    static HkTrampoline<bool, const PlayerInput*> capSingleHandThrowHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!canStartCapThrowForInput(self))
            return false;
        if (capSingleHandThrowHook.orig(self))
            return true;
        if (!isHakoniwaPlayerInput(self))
            return false;
        // The neutral (else) throw path in PlayerJudgePreInputCapThrow ignores
        // getCapThrowDir, so a plain button press always fires a neutral throw. Route X
        // (up) and R (spin) through the single-hand path so the direction we inject in
        // capThrowDirHook is honored. B (down) is the double-hand path instead — the
        // single-hand path only promotes UPWARD throws to a real directional throw, so a
        // single-hand down just becomes a tilted neutral.
        if (isMotionFreeControlsEnabled() && (sMainPadXTriggered || sMainPadRTriggered))
            return true;
        return false;
    });

    static HkTrampoline<bool, const PlayerInput*> capDoubleHandThrowHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        // Strict gate: this is the real up/down throw, so a down here needs DownThrow (not the
        // neutral fallback). Applies to BOTH the motion swing and the B button.
        if (!canStartDoubleHandThrow(self))
            return false;
        if (capDoubleHandThrowHook.orig(self))
            return true;
        // B down-throw is the double-hand (both-joysticks-down) move; airborne only.
        return isMotionFreeDownThrow(self);
    });

    static HkTrampoline<bool, const PlayerInput*> capAttackSeparateHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!canStartCapThrowForInput(self))
            return false;
        return capAttackSeparateHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> capSwingActionHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!canStartCapThrowForInput(self))
            return false;
        return capSwingActionHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> capSeparateJumpHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_Vault))
            return false;
        return capSeparateJumpHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> holdCapSeparateJumpHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, Archipelago::AbilityId_Vault))
            return false;
        return holdCapSeparateJumpHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> spinCapThrowHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!canStartCapThrowForInput(self))
            return false;
        if (spinCapThrowHook.orig(self))
            return true;
        // Motion-free outer gate: R presses the spin throw (any time), B presses the
        // airborne down-throw. getCapThrowDir forces the per-button direction below.
        if (isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && sMainPadRTriggered)
            return true;
        return isMotionFreeDownThrow(self);
    });

    // NOTE: we deliberately do NOT hook any of the throw-trajectory functions
    // (isThrowTypeLeftRight / isThrowTypeSpiral / isThrowTypeRolling, or HackCap::startThrow /
    // PlayerSpinCapAttack::startCapThrow). Those run both when the motion-throw's direction vectors
    // are populated and later when the trajectory is built, and mutating them for a locked ability
    // corrupts the throw (e.g. a Down-throw gesture loses its direction and fires as a neutral throw).
    // All gating happens at the trigger level via canStartCapThrowForInput; the trajectory is vanilla.

    static HkTrampoline<bool, const HackCap*> capThrowEnabledHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (!isAnyPrimaryCapThrowUnlockedForActor(self))
            return false;
        return capThrowEnabledHook.orig(self);
    });

    static HkTrampoline<bool, const HackCap*> capTouchJumpInputHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (!isUnlockedForActor(self, Archipelago::AbilityId_Vault))
            return false;
        return capTouchJumpInputHook.orig(self);
    });

    static HkTrampoline<bool, const HackCap*> forceCapTouchJumpHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (!isUnlockedForActor(self, Archipelago::AbilityId_Vault))
            return false;
        return forceCapTouchJumpHook.orig(self);
    });

    // ── Motion-free controls ────────────────────────────────────────────────────
    // The cap-throw type is derived entirely from getCapThrowDir() (zero = neutral,
    // +Y = up, -Y = down, dominant X = spin). Forcing that vector from a held button
    // gives X = up-throw, B = down-throw, R = spin without any motion. The same vector
    // feeds canStartCapThrowForInput above, so the ability gating stays consistent.
    static const sead::Vector2f sThrowDirUp(0.0f, 1.0f);
    static const sead::Vector2f sThrowDirDown(0.0f, -1.0f);
    static const sead::Vector2f sThrowDirSpin(1.0f, 0.0f);

    static HkTrampoline<const sead::Vector2f*, const PlayerInput*> capThrowDirHook = hk::hook::trampoline([](const PlayerInput* self) -> const sead::Vector2f* {
        if (isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)) {
            if (al::isPadHoldX())
                return &sThrowDirUp;
            if (al::isPadHoldB())
                return &sThrowDirDown;
            if (al::isPadHoldR())
                return &sThrowDirSpin;
        }

        return capThrowDirHook.orig(self);
    });

    // The shake gesture funnels through JoyPadAccelPoseAnalyzer::isSwingAnyHand for the
    // cap spin, every capture's "hack swing", and the rolling boost (all via PlayerInput).
    // ORing R in here covers all of them at once; throw direction uses different analyzer
    // methods, so this never affects up/down throws.
    static HkTrampoline<bool, void*> swingAnyHandHook = hk::hook::trampoline([](void* analyzer) -> bool {
        if (swingAnyHandHook.orig(analyzer))
            return true;
        return isMotionFreeControlsEnabled() && sMainPadRTriggered;
    });

    // The PlayerInput swing predicates short-circuit on a "motion disabled" flag before they
    // ever reach isSwingAnyHand, so hooking the analyzer alone can miss captures/rolling. Hook
    // isTriggerSwingActionMario directly (offset 0x44c664; not in syms) — this is what every
    // capture's rs::isTriggerHackSwing resolves to — and OR in R unconditionally.
    static HkTrampoline<bool, const PlayerInput*> hackSwingHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (hackSwingHook.orig(self))
            return true;
        // Captures are separate actors whose update order vs PlayerInput::update isn't
        // guaranteed, so use the live hold (not the edge flag). A physical swing is also
        // "true" for a multi-frame window, so holding R to shake matches vanilla feel.
        return isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && al::isPadHoldR();
    });

    // R is the spin/shake under motion-free controls, so it must not also recenter the
    // camera. Suppress an R-driven camera reset while leaving L (the other reset button)
    // working. PlayerInput::isTriggerCameraReset is not in syms/main.sym (offset 0x44e124).
    static HkTrampoline<bool, const PlayerInput*> cameraResetHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && al::isPadHoldR() && !al::isPadHoldL())
            return false;
        return cameraResetHook.orig(self);
    });

    // Returning a thrown Cappy is the Action button (Y/X) or a shake; R already recalls via
    // the shake. Also let B recall it when motion-free is on. (When you hold the cap, B is the
    // down-throw instead — that path requires the cap, so the two never conflict.)
    static HkTrampoline<bool, const PlayerInput*> capReturnHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (capReturnHook.orig(self))
            return true;
        return isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && !isPlayerHacking(self) && sMainPadBTriggered;
    });

    // The "homing" follow-up attack (Append Cap Attack) re-throws the out cap toward an
    // auto-selected nearby target — vanilla triggers it with a shake while the cap is out.
    // Tie it to R, the motion-free shake, so it works without motion. Targeting is automatic
    // (the lock-on picks the nearest valid target); there's nothing to aim.
    static HkTrampoline<bool, const PlayerInput*, bool> appendCapAttackHook = hk::hook::trampoline([](const PlayerInput* self, bool separate) -> bool {
        if (appendCapAttackHook.orig(self, separate))
            return true;
        return isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && !isPlayerHacking(self) && sMainPadRTriggered;
    });

    // Holding the throw button keeps a thrown cap pinned in place (spinning on a CapHanger /
    // Tire / FireHydrant, etc.) instead of returning — vanilla checks isHoldAction (Y/X). The
    // down-throw is done with B, so also treat a held B as "keep the cap out". HackCap is
    // always Mario's cap, so no hacking/2D gating is needed here.
    static HkTrampoline<bool, const HackCap*> capKeepHeldHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (capKeepHeldHook.orig(self))
            return true;
        return isMotionFreeControlsEnabled() && al::isPadHoldB();
    });

    // When HackCap Returns to Mario a jump becomes a Cappy Return Jump
    static bool PlayerJudgeCapCatchPopJudgeHook(PlayerCounterAfterCapCatch* self) {
        return self->isCapCatch() && isUnlockedForLastPlayer(Archipelago::AbilityId::AbilityId_CapReturnJump);
    }

    static bool MotorcycleIsPlayerInputTriggerRideHook(const al::SensorMsg* msg) {
        return al::isMsgBindInit(msg) && isUnlockedForLastPlayer(Archipelago::AbilityId::AbilityId_Scooter);
    }

    static bool SphinxRideIsPlayerInputTriggerRideHook(const al::SensorMsg* msg) {
        return al::isMsgBindInit(msg) && isUnlockedForLastPlayer(Archipelago::AbilityId::AbilityId_Jaxi);
    }

} // namespace

static void InstallAbilityLockHooks() {
    playerInputUpdateHook.installAtSym<"_ZN11PlayerInput6updateEv">();
    rollHook.installAtSym<"_ZNK23PlayerJudgeStartRolling5judgeEv">();
    rollBoostHook.installAtSym<"_ZNK11PlayerInput28isTriggerRollingRestartSwingEv">();
    rollTriggerHook.installAtSym<"_ZNK11PlayerInput16isTriggerRollingEb">();
    groundPoundHook.installAtSym<"_ZNK23PlayerJudgeStartHipDrop5judgeEv">();
    triggerHipDropInputHook.installAtSym<"_ZNK11PlayerInput16isTriggerHipDropEv">();
    holdHipDropHook.installAtSym<"_ZNK11PlayerInput13isHoldHipDropEv">();
    rollingCancelHipDropHook.installAtSym<"_ZNK11PlayerInput29isTriggerRollingCancelHipDropEb">();
    diveHook.installAtSym<"_ZNK11PlayerInput20isTriggerHeadSlidingEv">();
    spinHook.installAtSym<"_ZNK26PlayerJudgeStartGroundSpin5judgeEv">();
    crouchHook.installAtSym<"_ZNK21PlayerJudgeStartSquat5judgeEv">();
    longJumpHook.installAtSym<"_ZNK16PlayerStateSquat16isEnableLongJumpEv">();
    jumpHook.installAtSym<"_ZNK11PlayerInput13isTriggerJumpEv">();
    wallSlideHook.installAtSym<"_ZN19PlayerJudgeWallKeep6updateEv">();
    climbHook.installAtSym<"_ZN20PlayerJudgePoleClimb6updateEv">();
    ledgeGrabHook.installAtSym<"_ZN20PlayerJudgeWallCatch6updateEv">();
    ledgeGrabInputDirHook.installAtSym<"_ZN28PlayerJudgeWallCatchInputDir6updateEv">();
    sideSomersaultHook.installAtSym<"_ZN22PlayerStateRunHakoniwa11tryTurnJumpEPN4sead7Vector3IfEE">();
    continuousJumpHook.installAtSym<"_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump">();
    // continuousJumpHook.installAtSym<"_ZNK22PlayerJudgeCapCatchPop5judgeEv">();
    // hk::hook::writeBranchLinkAtMainOffset(0x4749a4, PlayerJudgeCapCatchPopJudgeHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x4746f8, PlayerJudgeCapCatchPopJudgeHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x4747bc, PlayerJudgeCapCatchPopJudgeHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x4745ac, PlayerJudgeCapCatchPopJudgeHook);

    // Scooter and Jaxi Hooks
    // hk::hook::writeBranchLinkAtMainOffset(0x2c7f64, MotorcycleIsPlayerInputTriggerRideHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x3d6098, SphinxRideIsPlayerInputTriggerRideHook);

    // Cap Throws
    capSingleHandThrowHook.installAtSym<"_ZNK11PlayerInput27isTriggerCapSingleHandThrowEv">();
    capDoubleHandThrowHook.installAtSym<"_ZNK11PlayerInput27isTriggerCapDoubleHandThrowEv">();
    capAttackSeparateHook.installAtSym<"_ZNK11PlayerInput26isTriggerCapAttackSeparateEv">();
    capSwingActionHook.installAtSym<"_ZNK11PlayerInput23isTriggerSwingActionCapEv">();
    capSeparateJumpHook.installAtSym<"_ZNK11PlayerInput24isTriggerCapSeparateJumpEv">();
    holdCapSeparateJumpHook.installAtSym<"_ZNK11PlayerInput21isHoldCapSeparateJumpEv">();
    spinCapThrowHook.installAtSym<"_ZNK11PlayerInput16isTriggerSpinCapEv">();
    capThrowEnabledHook.installAtSym<"_ZNK7HackCap13isEnableThrowEv">();
    capTouchJumpInputHook.installAtSym<"_ZNK7HackCap25isEnableCapTouchJumpInputEv">();
    forceCapTouchJumpHook.installAtSym<"_ZNK7HackCap19isForceCapTouchJumpEv">();

    // Motion-free controls (X up-throw / B down-throw / R spin-shake).
    capThrowDirHook.installAtSym<"_ZNK11PlayerInput14getCapThrowDirEv">();
    // al::JoyPadAccelPoseAnalyzer::isSwingAnyHand is not in syms/main.sym.
    swingAnyHandHook.installAtMainOffset(0x86628c);
    // PlayerInput::isTriggerSwingActionMario (capture shake) is not in syms/main.sym.
    hackSwingHook.installAtMainOffset(0x44c664);
    // PlayerInput::isTriggerCameraReset is not in syms/main.sym.
    cameraResetHook.installAtMainOffset(0x44e124);
    // PlayerInput::isTriggerCapReturn is not in syms/main.sym.
    capReturnHook.installAtMainOffset(0x44cf1c);
    appendCapAttackHook.installAtSym<"_ZNK11PlayerInput24isTriggerAppendCapAttackEb">();
    // HackCap::isHoldInputKeepLockOn is not in syms/main.sym.
    capKeepHeldHook.installAtMainOffset(0x3fe57c);
}
