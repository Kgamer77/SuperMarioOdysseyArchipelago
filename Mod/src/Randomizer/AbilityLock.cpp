#include "Randomizer/AbilityRando.h"
#include "Randomizer/Randomizer.h"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Project/HitSensor/HitSensor.h"
#include "game/Player/HackCap.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerContinuousJump.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Player/PlayerInput.h"
#include "game/Player/PlayerJudgeDirectRolling.h"
#include "game/Player/PlayerJudgeGrabCeil.h"
#include "game/Player/PlayerJudgePreInputJump.h"
#include "game/Player/PlayerJudgeStartGroundSpin.h"
#include "game/Player/PlayerJudgeStartHipDrop.h"
#include "game/Player/PlayerJudgeStartRolling.h"
#include "game/Player/PlayerJudgeStartSquat.h"
#include "game/Player/PlayerJudgeWallCatch.h"
#include "game/Player/PlayerJudgeWallCatchInputDir.h"
#include "game/Player/PlayerJudgeWallKeep.h"
#include "game/Player/PlayerStateHipDrop.h"
#include "game/Player/PlayerStateSquat.h"
#include "game/Player/PlayerStateWait.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/ActorDimensionKeeper.h"
#include "game/Util/PlayerUtil.h"
#include "hk/hook/Trampoline.h"
#include "imgui.h"

#include <basis/seadTypes.h>
#include <cstdint>
#include <math/seadVector.h>
#include <typeinfo>

class PlayerJudgePoleClimb;
class IUsePlayerHack;

namespace {

    GameDataHolder* sLastGameData = nullptr;

    // The al::isPadTrigger* symbols are unreliable here (isPadTriggerB isn't even in the
    // symbol set), so derive one-frame triggers from the confirmed-working isPadHold* in
    // PlayerInput::update (runs once/frame for the main player). Used by motion-free
    // controls. Tracked for every physical button (not just whichever is currently
    // assigned to a role) so remapping mid-play never produces a false trigger on the
    // newly-assigned button next frame.
    bool sButtonHeldPrev[Randomizer::cControllerButtonCount] = {};
    bool sButtonTriggered[Randomizer::cControllerButtonCount] = {};

    bool isButtonHeld(Randomizer::ControllerButton button) {
        switch (button) {
        case Randomizer::ControllerButton::A:
            return al::isPadHoldA();
        case Randomizer::ControllerButton::B:
            return al::isPadHoldB();
        case Randomizer::ControllerButton::X:
            return al::isPadHoldX();
        case Randomizer::ControllerButton::Y:
            return al::isPadHoldY();
        case Randomizer::ControllerButton::L:
            return al::isPadHoldL();
        case Randomizer::ControllerButton::R:
            return al::isPadHoldR();
        case Randomizer::ControllerButton::ZL:
            return al::isPadHoldZL();
        case Randomizer::ControllerButton::ZR:
            return al::isPadHoldZR();
        default:
            return false;
        }
    }

    bool isButtonTriggered(Randomizer::ControllerButton button) {
        const s32 idx = static_cast<s32>(button);
        return idx >= 0 && idx < Randomizer::cControllerButtonCount && sButtonTriggered[idx];
    }

    void updateButtonFrameState() {
        for (s32 i = 0; i < Randomizer::cControllerButtonCount; i++) {
            const auto button = static_cast<Randomizer::ControllerButton>(i);
            const bool held = isButtonHeld(button);
            sButtonTriggered[i] = held && !sButtonHeldPrev[i];
            sButtonHeldPrev[i] = held;
        }
    }

    // Thin wrappers so every hook below reads as "is the up-throw/down-throw/spin-shake
    // role held/triggered" instead of a specific physical button - the actual button is
    // whatever Randomizer::get*Button() currently has assigned to that role.
    bool isUpThrowHeld() { return isButtonHeld(Randomizer::getUpThrowButton()); }
    bool isUpThrowTriggered() { return isButtonTriggered(Randomizer::getUpThrowButton()); }
    bool isDownThrowJumpHeld() { return isButtonHeld(Randomizer::getDownThrowJumpButton()); }
    bool isDownThrowJumpTriggered() { return isButtonTriggered(Randomizer::getDownThrowJumpButton()); }
    bool isSpinShakeHeld() { return isButtonHeld(Randomizer::getSpinShakeButton()); }
    bool isSpinShakeTriggered() { return isButtonTriggered(Randomizer::getSpinShakeButton()); }

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

    bool isUnlocked(GameDataHolderAccessor accessor, AbilityRando::AbilityId ability) {
        noteAccessor(accessor);
        return AbilityRando::isAbilityUnlocked(accessor, ability);
    }

    bool isUnlockedForActor(const al::LiveActor* actor, AbilityRando::AbilityId ability) {
        return isUnlocked(accessorForActor(actor), ability);
    }

    bool isUnlockedForInput(const PlayerInput* input, AbilityRando::AbilityId ability) {
        return isUnlocked(accessorForInput(input), ability);
    }

    bool isUnlockedForLastPlayer(AbilityRando::AbilityId ability) {
        return isUnlocked(accessorForLastPlayer(), ability);
    }

    bool isAnyPrimaryCapThrowUnlocked(GameDataHolderAccessor accessor) {
        return AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_NeutralThrow) || AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_UpThrow) || AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_DownThrow) || AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_SpinThrow);
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
            return isUnlockedForInput(input, AbilityRando::AbilityId_NeutralThrow);
        if (ax > ay)
            return isUnlockedForInput(input, AbilityRando::AbilityId_SpinThrow); // horizontal -> spin
        if (dir.y > 0.0f)
            return isUnlockedForInput(input, AbilityRando::AbilityId_UpThrow); // upward -> up throw
        return isUnlockedForInput(input, AbilityRando::AbilityId_NeutralThrow)
            || isUnlockedForInput(input, AbilityRando::AbilityId_DownThrow); // downward -> neutral/down
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
            return isUnlockedForInput(input, AbilityRando::AbilityId_NeutralThrow);
        if (ax > ay)
            return isUnlockedForInput(input, AbilityRando::AbilityId_SpinThrow);
        if (dir.y > 0.0f)
            return isUnlockedForInput(input, AbilityRando::AbilityId_UpThrow);
        return isUnlockedForInput(input, AbilityRando::AbilityId_DownThrow); // strict: real down throw
    }

    bool isAnyPrimaryCapThrowUnlockedForActor(const al::LiveActor* actor) {
        return isAnyPrimaryCapThrowUnlocked(accessorForActor(actor));
    }

    bool isHakoniwaPlayerActor(const al::LiveActor* actor) {
        return actor && typeid(*actor) == typeid(PlayerActorHakoniwa);
    }

    bool isHakoniwaPlayerInput(const PlayerInput* input) {
        return input && isHakoniwaPlayerActor(input->mLiveActor);
    }

    // The down-throw role takes priority over spin-shake in capThrowDirHook while it is
    // held. Spin-shake still supplies the single-hand shake trigger, though, and the game
    // resolves that single-hand down input as the forward/neutral-style throw. Do not let
    // that route borrow Down Throw's gate.
    bool isMotionFreeNeutralThrowFromHeldB(const PlayerInput* input) {
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(input)
            && isSpinShakeTriggered() && isDownThrowJumpHeld();
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

    bool isPlayerInSquatState(const PlayerInput* input) {
        if (!isHakoniwaPlayerInput(input))
            return false;

        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        return player->mStateSquat && !player->mStateSquat->isDead();
    }

    bool canPlayerLongJumpFromSquat(const PlayerInput* input) {
        if (!isPlayerInSquatState(input))
            return false;

        const PlayerActorHakoniwa* player = reinterpret_cast<const PlayerActorHakoniwa*>(input->mLiveActor);
        // This calls the hooked isEnableLongJump(), so both the ability lock and all vanilla
        // velocity/state requirements must pass. Merely moving the stick deeply is not enough.
        return player->mStateSquat->isEnableLongJump();
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

    // A crouched jump is represented by the Backflip route in 3D, but 2D sections use the
    // same input/state for their ordinary crouch jump. Requiring Backflip there can leave
    // Mario unable to jump even though the ability that entered the state (Crouch) is owned.
    bool canPlayerCrouchJumpIn2D(const PlayerInput* input) {
        return isPlayerIn2D(input)
            && isUnlockedForInput(input, AbilityRando::AbilityId_Crouch);
    }

    // Motion-free down-throw is the vanilla "both joysticks down while airborne" move
    // (the double-hand throw path). It only fires in the air — on the ground it does
    // nothing, like the motion does when standing still.
    bool isMotionFreeDownThrow(const PlayerInput* self) {
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && !isPlayerHacking(self) && !isPlayerIn2D(self)
            && isDownThrowJumpTriggered() && self->mLiveActor && !rs::isPlayerOnGround(self->mLiveActor)
            && isUnlockedForInput(self, AbilityRando::AbilityId_DownThrow);
    }

    static HkTrampoline<void, PlayerInput*> playerInputUpdateHook = hk::hook::trampoline([](PlayerInput* self) -> void {
        accessorForInput(self);
        playerInputUpdateHook.orig(self);
        if (isHakoniwaPlayerInput(self))
            updateButtonFrameState();
    });

    static HkTrampoline<bool, PlayerJudgeStartRolling*> rollHook = hk::hook::trampoline([](PlayerJudgeStartRolling* self) -> bool {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_Roll))
            return false;
        return rollHook.orig(self);
    });

    // Ground-pound landing/cancel windows use a dedicated rolling judge instead of judge().
    // Gate that final transition too, otherwise Ground Pound can start a locked Roll.
    static HkTrampoline<bool, PlayerJudgeStartRolling*> rollCancelHipDropHook = hk::hook::trampoline([](PlayerJudgeStartRolling* self) -> bool {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_Roll))
            return false;
        return rollCancelHipDropHook.orig(self);
    });

    // Long Jump and dive/head-slide landings use this separate speed + crouch judge to enter
    // rolling directly, bypassing both PlayerJudgeStartRolling entry points. Gate update(), which
    // computes the cached mIsJudge result; judge() itself is only an 8-byte inline getter.
    static HkTrampoline<void, PlayerJudgeDirectRolling*> directRollUpdateHook = hk::hook::trampoline([](PlayerJudgeDirectRolling* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_Roll)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        directRollUpdateHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> rollBoostHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_RollBoost))
            return false;
        if (rollBoostHook.orig(self))
            return true;
        // Motion-free: the spin-shake role boosts the roll (this is isTriggerRollingRestartSwing).
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && isSpinShakeTriggered();
    });

    static HkTrampoline<bool, const PlayerInput*, bool> rollTriggerHook = hk::hook::trampoline([](const PlayerInput* self, bool allowRestart) -> bool {
        if (!isHakoniwaPlayerInput(self))
            return rollTriggerHook.orig(self, allowRestart);
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_Roll))
            return false;
        if (isPlayerInRollingState(self) && !isUnlockedForInput(self, AbilityRando::AbilityId_RollBoost))
            return false;
        return rollTriggerHook.orig(self, allowRestart);
    });

    static HkTrampoline<bool, PlayerJudgeStartHipDrop*> groundPoundHook = hk::hook::trampoline([](PlayerJudgeStartHipDrop* self) -> bool {
        if (!isUnlockedForInput(self ? self->mInput : nullptr, AbilityRando::AbilityId_GroundPound))
            return false;
        return groundPoundHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> triggerHipDropInputHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_GroundPound))
            return false;
        return triggerHipDropInputHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> holdHipDropHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_GroundPound))
            return false;
        return holdHipDropHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*, bool> rollingCancelHipDropHook = hk::hook::trampoline([](const PlayerInput* self, bool allowSpin) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_GroundPound))
            return false;
        return rollingCancelHipDropHook.orig(self, allowSpin);
    });

    static HkTrampoline<bool, const PlayerInput*> diveHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_Dive))
            return false;
        return diveHook.orig(self);
    });

    static HkTrampoline<bool, PlayerJudgeStartGroundSpin*> spinHook = hk::hook::trampoline([](PlayerJudgeStartGroundSpin* self) -> bool {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_Spin))
            return false;
        return spinHook.orig(self);
    });

    static HkTrampoline<bool, PlayerJudgeStartSquat*> crouchHook = hk::hook::trampoline([](PlayerJudgeStartSquat* self) -> bool {
        if (!isUnlockedForInput(self ? self->mInput : nullptr, AbilityRando::AbilityId_Crouch))
            return false;
        return crouchHook.orig(self);
    });

    static HkTrampoline<bool, PlayerStateSquat*> longJumpHook = hk::hook::trampoline([](PlayerStateSquat* self) -> bool {
        if (!isUnlockedForActor(self ? self->mActor : nullptr, AbilityRando::AbilityId_LongJump))
            return false;
        return longJumpHook.orig(self);
    });

    // Jump inputs are buffered before some state transitions and can later be consumed without
    // calling PlayerInput::isTriggerJump again. Validate the state in which the buffer is consumed:
    // this closes spin/spin-flower Ground Pound Jump and released-crouch Backflip bypasses.
    static HkTrampoline<bool, PlayerJudgePreInputJump*> preInputJumpHook = hk::hook::trampoline([](PlayerJudgePreInputJump* self) -> bool {
        const PlayerInput* input = self ? self->mInput : nullptr;
        if (isPlayerInHipDropState(input)
            && !isUnlockedForInput(input, AbilityRando::AbilityId_GroundPoundJump))
            return false;
        if (isPlayerInSquatState(input)) {
            if (!canPlayerCrouchJumpIn2D(input)
                && !canPlayerLongJumpFromSquat(input)
                && !isUnlockedForInput(input, AbilityRando::AbilityId_Backflip))
                return false;
        }
        return preInputJumpHook.orig(self);
    });

    // Once the Hip Drop state ends, its landing window continues in PlayerStateWait. This is
    // another buffered Ground Pound Jump route and no longer has an active Hip Drop state to test.
    static HkTrampoline<bool, PlayerStateWait*> hipDropJumpCancelWindowHook = hk::hook::trampoline([](PlayerStateWait* self) -> bool {
        if (!isUnlockedForActor(self ? self->mActor : nullptr, AbilityRando::AbilityId_GroundPoundJump))
            return false;
        return hipDropJumpCancelWindowHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> jumpHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        // Motion-free: on the ground the down-throw role's button is a normal jump; once
        // airborne it becomes the down-throw, so suppress only an airborne jump on that
        // button (A still jumps if pressed together). It also stays a normal jump in a
        // capture and in 2D. NOTE: this assumes the down-throw role is never remapped to
        // A itself - the settings UI should not offer that combination, since the
        // !al::isPadHoldA() guard would then always defeat this suppression.
        if (Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && !isPlayerHacking(self) && !isPlayerIn2D(self) && isDownThrowJumpTriggered() && !al::isPadHoldA()
            && self->mLiveActor && !rs::isPlayerOnGround(self->mLiveActor))
            return false;
        if (!jumpHook.orig(self))
            return false;
        if (!isHakoniwaPlayerInput(self))
            return true;
        // Rolling has its own direct jump-to-long-jump path and never asks
        // PlayerStateSquat::isEnableLongJump, so gate that path here as well.
        if (isPlayerInRollingState(self)
            && !isUnlockedForInput(self, AbilityRando::AbilityId_LongJump))
            return false;
        if (isPlayerInHipDropState(self))
            return isUnlockedForInput(self, AbilityRando::AbilityId_GroundPoundJump);
        if (!isPlayerInSquatState(self) && (!self || !self->isHoldSquat()))
            return true;
        if (canPlayerCrouchJumpIn2D(self))
            return true;
        if (canPlayerLongJumpFromSquat(self))
            return true;
        return isUnlockedForInput(self, AbilityRando::AbilityId_Backflip);
    });

    static HkTrampoline<void, PlayerJudgeWallKeep*> wallSlideHook = hk::hook::trampoline([](PlayerJudgeWallKeep* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_WallJump)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        wallSlideHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgePoleClimb*> climbHook = hk::hook::trampoline([](PlayerJudgePoleClimb* self) -> void {
        if (!isUnlockedForLastPlayer(AbilityRando::AbilityId_Climb))
            return;
        climbHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgeGrabCeil*> grabCeilJudgeHook = hk::hook::trampoline([](PlayerJudgeGrabCeil* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_Swing)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        grabCeilJudgeHook.orig(self);
    });

    static HkTrampoline<void, al::ActorStateBase*, al::HitSensor*, al::HitSensor*> grabCeilAttackSensorHook = hk::hook::trampoline([](al::ActorStateBase* self, al::HitSensor* receiver, al::HitSensor* sender) -> void {
        if (!isUnlockedForLastPlayer(AbilityRando::AbilityId_Swing))
            return;
        grabCeilAttackSensorHook.orig(self, receiver, sender);
    });
    static HkTrampoline<void, PlayerJudgeWallCatch*> ledgeGrabHook = hk::hook::trampoline([](PlayerJudgeWallCatch* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_LedgeGrab)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        ledgeGrabHook.orig(self);
    });

    static HkTrampoline<void, PlayerJudgeWallCatchInputDir*> ledgeGrabInputDirHook = hk::hook::trampoline([](PlayerJudgeWallCatchInputDir* self) -> void {
        if (!isUnlockedForActor(self ? self->mPlayer : nullptr, AbilityRando::AbilityId_LedgeGrab)) {
            if (self)
                self->mIsJudge = false;
            return;
        }
        ledgeGrabInputDirHook.orig(self);
    });

    static HkTrampoline<bool, al::ActorStateBase*, sead::Vector3f*> sideSomersaultHook = hk::hook::trampoline([](al::ActorStateBase* self, sead::Vector3f* vec) -> bool {
        if (!isUnlockedForActor(self ? self->mActor : nullptr, AbilityRando::AbilityId_SideFlip))
            return false;
        return sideSomersaultHook.orig(self, vec);
    });

    static HkTrampoline<bool, al::ActorStateBase*, PlayerContinuousJump*> continuousJumpHook = hk::hook::trampoline([](al::ActorStateBase* self, PlayerContinuousJump* continuousJump) -> bool {
        u32 jumpType = *reinterpret_cast<u32*>(reinterpret_cast<uintptr_t>(self) + 0xb0) + 1;
        GameDataHolderAccessor accessor = accessorForActor(self ? self->mActor : nullptr);

        if (jumpType == 1 && !isUnlocked(accessor, AbilityRando::AbilityId_DoubleJump))
            return false;
        if (jumpType >= 2 && !isUnlocked(accessor, AbilityRando::AbilityId_TripleJump))
            return false;

        return continuousJumpHook.orig(self, continuousJump);
    });

    static HkTrampoline<bool, const PlayerInput*> capSingleHandThrowHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (isMotionFreeNeutralThrowFromHeldB(self)
            && !isUnlockedForInput(self, AbilityRando::AbilityId_NeutralThrow))
            return false;
        if (!canStartCapThrowForInput(self))
            return false;
        if (capSingleHandThrowHook.orig(self))
            return true;
        if (!isHakoniwaPlayerInput(self))
            return false;
        // The neutral (else) throw path in PlayerJudgePreInputCapThrow ignores
        // getCapThrowDir, so a plain button press always fires a neutral throw. Route the
        // up-throw and spin-shake roles through the single-hand path so the direction we
        // inject in capThrowDirHook is honored. The down-throw role is the double-hand
        // path instead — the single-hand path only promotes UPWARD throws to a real
        // directional throw, so a single-hand down just becomes a tilted neutral.
        if (Randomizer::isMotionFreeControlsEnabled()
            && (isUpThrowTriggered() || isSpinShakeTriggered()))
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
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_Vault))
            return false;
        return capSeparateJumpHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> holdCapSeparateJumpHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (!isUnlockedForInput(self, AbilityRando::AbilityId_Vault))
            return false;
        return holdCapSeparateJumpHook.orig(self);
    });

    static HkTrampoline<bool, const PlayerInput*> spinCapThrowHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        // With B held, R is still the shake that starts this path, but the throw it
        // produces is the single-hand forward/neutral variant rather than a down throw.
        if (isMotionFreeNeutralThrowFromHeldB(self)
            && !isUnlockedForInput(self, AbilityRando::AbilityId_NeutralThrow))
            return false;
        if (!canStartCapThrowForInput(self))
            return false;
        if (spinCapThrowHook.orig(self))
            return true;
        // Motion-free outer gate: spin-shake presses the spin throw (any time), down-throw
        // presses the airborne down-throw. getCapThrowDir forces the per-button direction
        // below.
        if (Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && isSpinShakeTriggered())
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
        if (!isUnlockedForActor(self, AbilityRando::AbilityId_Vault))
            return false;
        return capTouchJumpInputHook.orig(self);
    });

    static HkTrampoline<bool, const HackCap*> forceCapTouchJumpHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (!isUnlockedForActor(self, AbilityRando::AbilityId_Vault))
            return false;
        return forceCapTouchJumpHook.orig(self);
    });

    // A successful cap-touch sensor message is the authoritative start of a vault. Some neutral
    // throw collision paths reach it after the input-level HackCap checks, so reject the message
    // itself when the player's Vault ability is locked.
    static HkTrampoline<bool, al::HitSensor*, al::HitSensor*> capTouchJumpMessageHook = hk::hook::trampoline([](al::HitSensor* receiver, al::HitSensor* sender) -> bool {
        const al::LiveActor* player = sender ? sender->getParentActor() : nullptr;
        if (isHakoniwaPlayerActor(player)
            && !isUnlockedForActor(player, AbilityRando::AbilityId_Vault))
            return false;
        return capTouchJumpMessageHook.orig(receiver, sender);
    });

    // Landing on Cappy with Mario's foot uses PlayerCapTrample rather than PlayerCapTouchJump.
    // This is the normal neutral-throw + jump-on-cap Vault route.
    static HkTrampoline<bool, al::HitSensor*, al::HitSensor*> capTrampleMessageHook = hk::hook::trampoline([](al::HitSensor* receiver, al::HitSensor* sender) -> bool {
        const al::LiveActor* player = sender ? sender->getParentActor() : nullptr;
        if (isHakoniwaPlayerActor(player)
            && !isUnlockedForActor(player, AbilityRando::AbilityId_Vault))
            return false;
        return capTrampleMessageHook.orig(receiver, sender);
    });

    // ── Motion-free controls ────────────────────────────────────────────────────
    // The cap-throw type is derived entirely from getCapThrowDir() (zero = neutral,
    // +Y = up, -Y = down, dominant X = spin). Forcing that vector from a held button
    // gives up-throw = up-throw, down-throw = down-throw, spin-shake = spin without any
    // motion. The same vector feeds canStartCapThrowForInput above, so the ability
    // gating stays consistent.
    static const sead::Vector2f sThrowDirUp(0.0f, 1.0f);
    static const sead::Vector2f sThrowDirDown(0.0f, -1.0f);
    static const sead::Vector2f sThrowDirSpin(1.0f, 0.0f);

    static HkTrampoline<const sead::Vector2f*, const PlayerInput*> capThrowDirHook = hk::hook::trampoline([](const PlayerInput* self) -> const sead::Vector2f* {
        if (Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)) {
            if (isUpThrowHeld())
                return &sThrowDirUp;
            if (isDownThrowJumpHeld())
                return &sThrowDirDown;
            if (isSpinShakeHeld())
                return &sThrowDirSpin;
        }

        return capThrowDirHook.orig(self);
    });

    // The shake gesture funnels through JoyPadAccelPoseAnalyzer::isSwingAnyHand for the
    // cap spin, every capture's "hack swing", and the rolling boost (all via PlayerInput).
    // ORing the spin-shake role in here covers all of them at once; throw direction uses
    // different analyzer methods, so this never affects up/down throws.
    static HkTrampoline<bool, void*> swingAnyHandHook = hk::hook::trampoline([](void* analyzer) -> bool {
        if (swingAnyHandHook.orig(analyzer))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isSpinShakeTriggered();
    });

    // The PlayerInput swing predicates short-circuit on a "motion disabled" flag before they
    // ever reach isSwingAnyHand, so hooking the analyzer alone can miss captures/rolling. Hook
    // isTriggerSwingActionMario directly (offset 0x44c664; not in syms) — this is what every
    // capture's rs::isTriggerHackSwing resolves to — and OR in the spin-shake role unconditionally.
    static HkTrampoline<bool, const PlayerInput*> hackSwingHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (hackSwingHook.orig(self))
            return true;
        // Captures are separate actors whose update order vs PlayerInput::update isn't
        // guaranteed, so use the live hold (not the edge flag). A physical swing is also
        // "true" for a multi-frame window, so holding the shake button matches vanilla feel.
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self) && isSpinShakeHeld();
    });

    // Some captures inspect the direction or individual hand of a swing instead of the generic
    // isTriggerHackSwing predicate above. The spin-shake role has no physical swing vector/hand,
    // so those extra predicates remain false unless they are augmented explicitly. Treat it as
    // either supported direction/hand; capture code resolves simultaneous matches in its normal
    // priority order.
    static HkTrampoline<bool, const IUsePlayerHack*> hackSwingDirLeftHook = hk::hook::trampoline([](const IUsePlayerHack* self) -> bool {
        if (hackSwingDirLeftHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isSpinShakeHeld();
    });

    static HkTrampoline<bool, const IUsePlayerHack*> hackSwingDirRightHook = hk::hook::trampoline([](const IUsePlayerHack* self) -> bool {
        if (hackSwingDirRightHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isSpinShakeHeld();
    });

    static HkTrampoline<bool, const IUsePlayerHack*> hackSwingLeftHandHook = hk::hook::trampoline([](const IUsePlayerHack* self) -> bool {
        if (hackSwingLeftHandHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isSpinShakeHeld();
    });

    static HkTrampoline<bool, const IUsePlayerHack*> hackSwingRightHandHook = hk::hook::trampoline([](const IUsePlayerHack* self) -> bool {
        if (hackSwingRightHandHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isSpinShakeHeld();
    });

    // The spin-shake role's button must not also recenter the camera under motion-free
    // controls. Suppress a spin-shake-driven camera reset while leaving L (the other reset
    // button) working. PlayerInput::isTriggerCameraReset is not in syms/main.sym (offset
    // 0x44e124).
    static HkTrampoline<bool, const PlayerInput*> cameraResetHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && isSpinShakeHeld() && !al::isPadHoldL())
            return false;
        return cameraResetHook.orig(self);
    });

    // Returning a thrown Cappy is the Action button (Y/X) or a shake; spin-shake already
    // recalls via the shake. Also let the down-throw role recall it when motion-free is on.
    // (When you hold the cap, the down-throw role is the down-throw instead — that path
    // requires the cap, so the two never conflict.)
    static HkTrampoline<bool, const PlayerInput*> capReturnHook = hk::hook::trampoline([](const PlayerInput* self) -> bool {
        if (capReturnHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && !isPlayerHacking(self) && isDownThrowJumpTriggered();
    });

    // The "homing" follow-up attack (Append Cap Attack) re-throws the out cap toward an
    // auto-selected nearby target — vanilla triggers it with a shake while the cap is out.
    // Tie it to the spin-shake role so it works without motion. Targeting is automatic
    // (the lock-on picks the nearest valid target); there's nothing to aim.
    static HkTrampoline<bool, const PlayerInput*, bool> appendCapAttackHook = hk::hook::trampoline([](const PlayerInput* self, bool separate) -> bool {
        if (appendCapAttackHook.orig(self, separate))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isHakoniwaPlayerInput(self)
            && !isPlayerHacking(self) && isSpinShakeTriggered();
    });

    // Holding the throw button keeps a thrown cap pinned in place (spinning on a CapHanger /
    // Tire / FireHydrant, etc.) instead of returning — vanilla checks isHoldAction (Y/X). The
    // down-throw role also keeps the cap out while held. HackCap is always Mario's cap, so no
    // hacking/2D gating is needed here.
    static HkTrampoline<bool, const HackCap*> capKeepHeldHook = hk::hook::trampoline([](const HackCap* self) -> bool {
        if (capKeepHeldHook.orig(self))
            return true;
        return Randomizer::isMotionFreeControlsEnabled() && isDownThrowJumpHeld();
    });

} // namespace

void drawAbilityLockWindow() {
    if (!Randomizer::isAbilityLockFeatureEnabled())
        return;

    ImGui::SetNextWindowSize(ImVec2(340, 560), ImGuiCond_Once);
    ImGui::SetNextWindowPos(ImVec2(780, 20), ImGuiCond_Once);
    ImGui::SetNextWindowCollapsed(true, ImGuiCond_Appearing);
    ImGui::Begin("Ability Lock");

    GameDataHolderAccessor accessor = accessorForLastPlayer();
    GameDataHolderWriter writer(sLastGameData);

    const AbilityRando::AbilityEntry* startCap = AbilityRando::getStartingCapThrow();
    ImGui::Text("Starting cap throw: %s", startCap ? startCap->displayName : "None");
    ImGui::Separator();

    for (s32 i = 0; i < AbilityRando::getDebugAbilityCount(); i++) {
        const AbilityRando::AbilityEntry* ability = AbilityRando::getDebugAbility(i);
        if (!ability)
            continue;

        bool unlocked = AbilityRando::isAbilityDebugOverrideActive(*ability)
            ? AbilityRando::isAbilityDebugUnlocked(*ability)
            : (startCap && startCap->id == ability->id)
            ? true
            : AbilityRando::isAbilityInSave(accessor, *ability);
        const bool isStartingCap = startCap && startCap->id == ability->id;

        const bool changed = ImGui::Checkbox(ability->displayName, &unlocked);

        if (isStartingCap) {
            ImGui::SameLine();
            ImGui::TextDisabled("(Start)");
        }

        if (changed && sLastGameData) {
            AbilityRando::setAbilityDebugUnlocked(*ability, unlocked);
            AbilityRando::setAbilityUnlocked(writer, *ability, unlocked);
        }
    }

    ImGui::Separator();
    if (ImGui::Button("Unlock All") && sLastGameData)
        AbilityRando::unlockAll(writer);
    ImGui::SameLine();
    if (ImGui::Button("Lock All") && sLastGameData)
        AbilityRando::clearAll(writer);

    ImGui::End();
}

void InstallAbilityLockHooks() {
    if (!Randomizer::isAbilityLockFeatureEnabled())
        return;

    playerInputUpdateHook.installAtSym<"_ZN11PlayerInput6updateEv">();
    rollHook.installAtSym<"_ZNK23PlayerJudgeStartRolling5judgeEv">();
    // PlayerJudgeStartRolling::judgeCancelHipDrop is not present in syms/main.sym.
    rollCancelHipDropHook.installAtMainOffset(0x45b678);
    // PlayerJudgeDirectRolling::update is not present in syms/main.sym.
    directRollUpdateHook.installAtMainOffset(0x459108);
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
    // Buffered jump consumers are not present in syms/main.sym.
    preInputJumpHook.installAtMainOffset(0x45ac88);
    hipDropJumpCancelWindowHook.installAtMainOffset(0x48d4d4);
    jumpHook.installAtSym<"_ZNK11PlayerInput13isTriggerJumpEv">();
    wallSlideHook.installAtSym<"_ZN19PlayerJudgeWallKeep6updateEv">();
    climbHook.installAtSym<"_ZN20PlayerJudgePoleClimb6updateEv">();
    grabCeilJudgeHook.installAtSym<"_ZN19PlayerJudgeGrabCeil6updateEv">();
    grabCeilAttackSensorHook.installAtMainOffset(0x2e63e0);
    ledgeGrabHook.installAtSym<"_ZN20PlayerJudgeWallCatch6updateEv">();
    ledgeGrabInputDirHook.installAtSym<"_ZN28PlayerJudgeWallCatchInputDir6updateEv">();
    sideSomersaultHook.installAtSym<"_ZN22PlayerStateRunHakoniwa11tryTurnJumpEPN4sead7Vector3IfEE">();
    continuousJumpHook.installAtSym<"_ZN15PlayerStateJump24tryCountUpContinuousJumpEP20PlayerContinuousJump">();

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
    // rs::sendMsgPlayerCapTouchJump is not present in syms/main.sym.
    capTouchJumpMessageHook.installAtMainOffset(0x57a768);
    // rs::sendMsgPlayerCapTrample is not present in syms/main.sym.
    capTrampleMessageHook.installAtMainOffset(0x57a7a4);

    // Motion-free controls (X up-throw / B down-throw / R spin-shake).
    capThrowDirHook.installAtSym<"_ZNK11PlayerInput14getCapThrowDirEv">();
    // al::JoyPadAccelPoseAnalyzer::isSwingAnyHand is not in syms/main.sym.
    swingAnyHandHook.installAtMainOffset(0x86628c);
    // PlayerInput::isTriggerSwingActionMario (capture shake) is not in syms/main.sym.
    hackSwingHook.installAtMainOffset(0x44c664);
    // Directional/per-hand capture swing helpers are not present in syms/main.sym.
    hackSwingDirLeftHook.installAtMainOffset(0x56dea0);
    hackSwingDirRightHook.installAtMainOffset(0x56def8);
    hackSwingLeftHandHook.installAtMainOffset(0x56e030);
    hackSwingRightHandHook.installAtMainOffset(0x56e050);
    // PlayerInput::isTriggerCameraReset is not in syms/main.sym.
    cameraResetHook.installAtMainOffset(0x44e124);
    // PlayerInput::isTriggerCapReturn is not in syms/main.sym.
    capReturnHook.installAtMainOffset(0x44cf1c);
    appendCapAttackHook.installAtSym<"_ZNK11PlayerInput24isTriggerAppendCapAttackEb">();
    // HackCap::isHoldInputKeepLockOn is not in syms/main.sym.
    capKeepHeldHook.installAtMainOffset(0x3fe57c);
}
