#pragma once

#include <cstddef>
#include <cstdint>

#include "hk/ro/RoUtil.h"
#include "sead/basis/seadTypes.h"
#include "sead/math/seadMatrix.h"
#include "sead/math/seadQuat.h"
#include "sead/math/seadVector.h"

namespace al {
    class HitSensor;
    class LiveActor;
    struct ActorInitInfo;
    class SklAnimRetargettingInfo;
    class JointSpringControllerHolder;
} // namespace al

// al functions that lib/OdysseyHeaders DECLARES but syms/main.sym does not
// export. A direct call compiles happily and then fails at link with
// "undefined symbol: al::..." - which is how every one of these was found.
//
// Reaching them at their main-module offset is the technique the rest of the
// mod already uses for al::isExistArchive (PuppetCapturePool), al::tryGetClass
// Name / al::getModelName (syncHooks) and PlayerFunction::initYoshiModelActor
// (PuppetHackActor). Those grew one local helper at a time; this is the shared
// header the third of them said should exist.
//
// Deliberately NOT defined into namespace al. Providing our own definition for
// al::foo would keep the call sites unchanged, but it would also collide the
// day syms/main.sym starts exporting one of them, and the collision would be a
// link error in a file nobody was editing. Naming AlUnlinked at the call site
// keeps "this one is not a real link" visible where it matters.
//
// Offsets are SMO 1.3.0, taken from ida_context:
//   Library/LiveActor/ActorAnimFunction.md, ActorInitFunction.md,
//   Library/Joint/JointControllerKeeper.md, JointSpringControllerHolder.md
namespace AlUnlinked {

    template <typename Fn>
    inline Fn at(uintptr_t offset) {
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
    }

    // ---- animation ----

    inline f32 getSklAnimBlendWeight(const al::LiveActor* actor, s32 index) {
        return at<f32 (*)(const al::LiveActor*, s32)>(0x008d3f18)(actor, index);
    }

    inline const char* getPlayingSklAnimName(const al::LiveActor* actor, s32 index) {
        return at<const char* (*)(const al::LiveActor*, s32)>(0x008d3e7c)(actor, index);
    }

    inline s32 getPartialSklAnimSlotNum(al::LiveActor* actor) {
        return at<s32 (*)(al::LiveActor*)>(0x008d58b4)(actor);
    }

    inline void startPartialSklAnim(al::LiveActor* actor, const char* animName, s32 slot,
        s32 partsListIndex, const al::SklAnimRetargettingInfo* retarget) {
        at<void (*)(al::LiveActor*, const char*, s32, s32, const al::SklAnimRetargettingInfo*)>(
            0x008d5904)(actor, animName, slot, partsListIndex, retarget);
    }

    inline void clearPartialSklAnim(al::LiveActor* actor, s32 slot) {
        at<void (*)(al::LiveActor*, s32)>(0x008d5948)(actor, slot);
    }

    // Null when nothing is attached to that slot. There is no
    // al::isPartialAnimPlaying in the binary despite the header declaring one -
    // a null return here is the check that replaces it.
    inline const char* getPlayingPartialSklAnimName(const al::LiveActor* actor, s32 slot) {
        return at<const char* (*)(const al::LiveActor*, s32)>(0x008d5a18)(actor, slot);
    }

    // ---- sensors ----

    // The actor a hit sensor belongs to. Eight bytes - it loads one field.
    // Used to turn PlayerBindKeeper::getBindSensor() into "what is the local
    // player standing on", which is the only way to ask what they are riding:
    // Motorcycle and SphinxRide mount the player with a bind, not a capture.
    inline al::LiveActor* getSensorHost(const al::HitSensor* sensor) {
        return at<al::LiveActor* (*)(const al::HitSensor*)>(0x008f1ec0)(sensor);
    }

    // ---- sub actors ----

    inline void initSubActorKeeperNoFile(al::LiveActor* actor, const al::ActorInitInfo& initInfo,
        s32 maxSubActors) {
        at<void (*)(al::LiveActor*, const al::ActorInitInfo&, s32)>(0x008dbab4)(
            actor, initInfo, maxSubActors);
    }

    inline void registerSubActorSyncClipping(al::LiveActor* actor, al::LiveActor* subActor) {
        at<void (*)(al::LiveActor*, al::LiveActor*)>(0x008dbb10)(actor, subActor);
    }

    // ---- joint controllers ----

    inline void initJointControllerKeeper(const al::LiveActor* actor, s32 maxControllers) {
        at<void (*)(const al::LiveActor*, s32)>(0x008a86ac)(actor, maxControllers);
    }

    inline void initJointLocalTransControllerX(const al::LiveActor* actor, const f32* value,
        const char* jointName) {
        at<void (*)(const al::LiveActor*, const f32*, const char*)>(0x008a90c8)(
            actor, value, jointName);
    }

    // The joint's WORLD rotation, read out of `quat` every frame - so the
    // address has to outlive the call. Vanilla binds it to a field of the actor
    // itself (Yukimaru::init, ida_context/Npc/Yukimaru.md:298, on the "Rotate"
    // joint); a puppet binds it to storage PuppetHackActor owns, for the same
    // reason and with the same lifetime.
    inline void initJointGlobalQuatController(const al::LiveActor* actor,
        const sead::Quatf* quat, const char* jointName) {
        at<void (*)(const al::LiveActor*, const sead::Quatf*, const char*)>(0x008a9568)(
            actor, quat, jointName);
    }

    // A joint that rotates by `angle` (degrees) about `axis`, both in the
    // joint's local frame. HackFork::init builds five of these - one per
    // "Stick" joint - and that is the entire visible bend of a fork or a
    // signpost (ida_context/MapObj/HackFork.md, 0x0029c768).
    //
    // Returns the controller it created, and the caller needs it: the axis is
    // COPIED into the controller at construction, not read back through the
    // reference every frame. HackFork::bendAndTwist re-writes it into every
    // controller it kept whenever the axis changes (+0x28 / +0x30 of the
    // controller object), which is what cAxisRotatorAxisOffset below is for.
    // The ANGLE is read through the bound pointer, so that one address is
    // enough on its own.
    inline void* initJointLocalAxisRotator(const al::LiveActor* actor, const sead::Vector3f& axis,
        const f32* angleDegrees, const char* jointName, bool flag) {
        return at<void* (*)(const al::LiveActor*, const sead::Vector3f&, const f32*, const char*,
            bool)>(0x008a8a50)(actor, axis, angleDegrees, jointName, flag);
    }

    // Where al::JointLocalAxisRotator keeps its copy of the axis. Straight out
    // of HackFork::bendAndTwist, which writes the first two floats as one
    // 8-byte store at +0x28 and the third at +0x30.
    inline constexpr ptrdiff_t cAxisRotatorAxisOffset = 0x28;

    inline void setAxisRotatorAxis(void* controller, const sead::Vector3f& axis) {
        if (!controller)
            return;
        f32* dst = reinterpret_cast<f32*>(
            reinterpret_cast<u8*>(controller) + cAxisRotatorAxisOffset);
        dst[0] = axis.x;
        dst[1] = axis.y;
        dst[2] = axis.z;
    }

    // Twist about the joint's own X axis, read through the pointer every frame.
    inline void initJointLocalXRotator(const al::LiveActor* actor, const f32* angleDegrees,
        const char* jointName) {
        at<void (*)(const al::LiveActor*, const f32*, const char*)>(0x008a88dc)(
            actor, angleDegrees, jointName);
    }

    // The joint's whole local matrix, read out of `mtx` every frame - the same
    // binding Fukankun::init uses for its "Head" joint (ida_context/MapObj/
    // Fukankun.md, 0x0287b3c), where the matrix is an accumulating X rotation
    // and is the binocular head's entire visible movement.
    inline void initJointLocalMtxController(const al::LiveActor* actor, const sead::Matrix34f* mtx,
        const char* jointName) {
        at<void (*)(const al::LiveActor*, const sead::Matrix34f*, const char*)>(0x008a9480)(
            actor, mtx, jointName);
    }

    // The whole spring-bone setup in one call, and the reason to prefer it over
    // initJointControllerKeeper + new Holder + Holder::init by hand: it checks
    // al::isExistModelResourceYaml first and returns null when the archive has
    // no such init file, and it sizes the joint controller keeper from that
    // file's actual entry count instead of a guessed constant. Vanilla
    // HackMapParts, RadiconCar and Senobi all pass "InitJointSpringCtrl".
    //
    // The returned holder is only needed to drive the springs later
    // (offControlAll and friends); the controllers themselves are owned by the
    // actor's JointControllerKeeper, so a puppet can discard it.
    inline al::JointSpringControllerHolder* tryCreateAndInitJointSpringControllers(
        al::LiveActor* actor, const char* initFileName) {
        return at<al::JointSpringControllerHolder* (*)(al::LiveActor*, const char*)>(0x008af600)(
            actor, initFileName);
    }

} // namespace AlUnlinked
