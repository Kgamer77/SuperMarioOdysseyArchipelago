#include "Multiplayer/algorithms/CaptureSyncTable.h"

#include <cmath>

#include "al/Library/Base/StringUtil.h"

#include "hk/hook/Trampoline.h"

#include "sead/math/seadQuat.h"
#include "sead/math/seadVector.h"

#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/Math/MathUtil.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/actors/PuppetHackActor.h"
#include "Multiplayer/algorithms/AlUnlinked.h"
#include "Multiplayer/algorithms/crc32.h"
#include "hk/ro/RoUtil.h"

#include "al/Library/Joint/JointControllerKeeper.h"
#include "al/Library/Joint/JointSpringControllerHolder.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"

#include "Multiplayer/puppets/LateModelDrawer.h"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
// initSubActorKeeperNoFile / registerSubActorSyncClipping.
#include "al/Library/LiveActor/ActorInitFunction.h"
// initChildActorWithArchiveNameNoPlacementInfo.
#include "al/Library/LiveActor/ActorInitUtil.h"
// getSubActorNum / getSubActor.
#include "al/Library/LiveActor/LiveActorFunction.h"

namespace CaptureSync {
    // ---- what an adapter is allowed to put on the wire ----
    //
    // The four floats an adapter samples ride in PlayerInf's animation blend
    // slots 2..5, and Client::updatePlayerInfo filters every slot through
    // `isfinite && >= 0 && <= 1` before it reaches PuppetInfo::blendWeights.
    // That filter is deliberate - it is what makes the -1 capture-frame marker
    // and the -2 ride marker unmistakable for real blend weights - but it also
    // means a slot outside [0, 1] is not clamped, it is DROPPED, and the
    // receiver silently keeps the previous frame's value.
    //
    // So an adapter payload has to look like a blend weight. A signed unit
    // value (a quaternion component, a direction cosine) goes over as
    // (v + 1) / 2; a magnitude goes over divided by a fixed maximum.
    constexpr f32 clampUnit(f32 v) { return v < -1.0f ? -1.0f : (v > 1.0f ? 1.0f : v); }
    constexpr f32 encodeSignedUnit(f32 v) { return clampUnit(v) * 0.5f + 0.5f; }
    constexpr f32 decodeSignedUnit(f32 v) { return clampUnit(v * 2.0f - 1.0f); }

    // --- SubActor Setup ---
    void setupModelWithSubActors(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        const CaptureSync::Entry* entry = CaptureSync::findByHackName(actor->getHackType());
        if (!entry || !entry->subActors || entry->subActorCount <= 0)
            return;

        const bool isEarly = PuppetCapturePool::isEarlyBuildPhase();
        LateModelDrawer::ScopedLateBuild lateScope(!isEarly);

        // NoFile, not the suffix-taking overload: a pooled capture model has no
        // SubActor byml of its own to read a list from - the sub-actors it needs
        // are named by the table. Same call HackMapParts::init makes for its own
        // CapTarget child.
        AlUnlinked::initSubActorKeeperNoFile(actor, initInfo, entry->subActorCount);

        for (s32 i = 0; i < entry->subActorCount; i++) {
            const CaptureSync::CaptureSubActor& subDef = entry->subActors[i];
            al::LiveActor* child = new al::LiveActor(subDef.archive);

            al::initChildActorWithArchiveNameNoPlacementInfo(child, initInfo, subDef.archive, subDef.suffix);

            if (child->getHitSensorKeeper()) {
                child->getHitSensorKeeper()->clear();
            }
            child->mCollider = nullptr;
            child->mCollisionParts = nullptr;
            child->mNerveKeeper = nullptr;

            al::invalidateClipping(child);
            AlUnlinked::registerSubActorSyncClipping(actor, child);

            // Held rather than looked up later: every name-based sub-actor
            // lookup goes through findSubActor, which is the search that
            // faulted on a null sub-actor name.
            if (!actor->mAdapterSubActor)
                actor->mAdapterSubActor = child;
        }
    }

    void setupSenobi(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        AlUnlinked::initJointControllerKeeper(actor, 2);
        AlUnlinked::initJointLocalTransControllerX(actor, &actor->mExtra[0], "StretchPoint");
    }
    void applySenobi(PuppetHackActor* actor, const float* in) {
        actor->mExtra[0] = in[0] * 1000.0f;
    }
    constexpr CaptureAdapter sAdapterSenobi = {
        nullptr, setupSenobi, applySenobi
    };

    // --- HackMapParts ---
    void sampleHackMapParts(const al::LiveActor* hackActor, float* out) {
        for (int i = 0; i < 4; i++) {
            out[i] = AlUnlinked::getSklAnimBlendWeight(hackActor, 2 + i);
        }
    }
    void setupHackMapParts(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        // One call instead of keeper + new Holder + init: it sizes the keeper
        // from the archive's own InitJointSpringCtrl entry count and returns
        // null when the archive has none, which a capture model reached
        // through a wildcard row very well might. Matches HackMapParts::init.
        AlUnlinked::tryCreateAndInitJointSpringControllers(actor, "InitJointSpringCtrl");
    }
    void applyHackMapParts(PuppetHackActor* actor, const float* in) {
        for (int i = 0; i < 4; i++) {
            actor->mExtra[i] = in[i];
        }
    }
    constexpr CaptureAdapter sAdapterHackMapParts = {
        sampleHackMapParts, setupHackMapParts, applyHackMapParts
    };

    // --- Radicon: no adapter, because the capture is not the car ---
    //
    // The RC car row used to build ObjectData/RadiConCar and give it the car's
    // suspension springs, and the report was "the car renders under the man".
    // It was doing exactly what it was told: THE CAPTURE IS THE MAN.
    //
    //     RadiconNpc::init:
    //         al::initActorWithArchiveName(this, info, "CityMan", "RadiCon")
    //         ...
    //         rs::createCapTargetInfo(this, "RadiCon")
    //
    // (ida_context/Npc/RadiconNpc.md, 0x003bc514.) The cap target is created on
    // the NPC, so PlayerHackKeeper::getHack() returns the NPC, the position on
    // the wire is the NPC's, and the car - built separately as a RadiconCar and
    // reached through the LinkTargetRadiConCar placement link - is a world
    // object the man drives, not the thing the player becomes. Drawing the car
    // at the man's transform is precisely how it ended up standing inside him.
    //
    // So the row now names CityMan with the "RadiCon" suffix, the same way the
    // Yukimaru rows name SnowManRacer + "Hack". The car is deliberately not
    // shown: it is a second actor with a position of its own, which is a
    // ride-shaped problem (see Build::Ride) and not something one capture model
    // can carry.

    // --- Yoshi Tongue SubActor Data ---
    // ---- Yoshi: the tongue ----
    //
    // The tongue is NOT part of the Yoshi model. YoshiTongue is its own
    // al::LiveActor, built by YoshiStateHackPlay while Mario is riding Yoshi
    // and registered as a sub-actor of the Yoshi model actor
    // (ida_context/Player/YoshiStateHackPlay.md:501). A puppet built from
    // ObjectData/Yoshi therefore has the head animation and nothing to extend,
    // which is exactly what was seen.
    //
    // Nothing about it can be read off the hack actor either - it hangs off the
    // state machine, not the actor - so the local one is captured as it is
    // constructed. There is only ever one, because there is only ever one local
    // Mario.
    al::LiveActor* sLocalYoshiTongue = nullptr;

    static HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&> yoshiTongueInitHook
        = hk::hook::trampoline([](al::LiveActor* tongue, const al::ActorInitInfo& initInfo) -> void {
              yoshiTongueInitHook.orig(tongue, initInfo);
              sLocalYoshiTongue = tongue;
              MpDebugLog::write("[CaptureSync] local YoshiTongue = %p", tongue);
          });

    // YoshiTongue::calcTongueTipPos(sead::Vector3f*) const, main offset
    // 0x004a1e18. Twenty bytes: it copies a Vector3f out of the actor and
    // returns - no side effects, safe to call every frame.
    bool tryGetLocalTongueTip(sead::Vector3f* out) {
        if (!sLocalYoshiTongue)
            return false;

        static constexpr uintptr_t cCalcTongueTipPosOffset = 0x004a1e18;
        using Fn = void (*)(const al::LiveActor*, sead::Vector3f*);
        reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cCalcTongueTipPosOffset)(
            sLocalYoshiTongue, out);
        return true;
    }

    // How far the tip has to be from Yoshi before the tongue counts as out.
    // The tongue does not toggle alive/dead - YoshiTongue::endHack just returns
    // it to its idle nerve - so there is no flag to read. Its own tip position
    // is the honest signal, and it costs no extra float on the wire.
    constexpr f32 cTongueOutMin = 20.0f;

    // Saturation point for the encoded length. Only "how far out" and "which
    // way" survive the trip, so this only has to be longer than the tongue can
    // actually reach.
    constexpr f32 cTongueMaxLen = 2000.0f;

    // Direction and length, both encoded into [0, 1] - see clampUnit above. The
    // raw world-space offset this used to send was several hundred units on
    // every axis, so the receiver's blend-weight filter threw all four slots
    // away and applyYoshi only ever saw stale sub-1.0 values: the length test
    // below could not pass and a remote Yoshi's tongue never came out at all.
    void sampleYoshi(const al::LiveActor* hackActor, float* out) {
        // Zero length; the direction is not read when the tongue is in.
        out[0] = 0.5f;
        out[1] = 0.5f;
        out[2] = 0.5f;
        out[3] = 0.0f;

        if (!hackActor)
            return;

        sead::Vector3f tip = sead::Vector3f::zero;
        if (!tryGetLocalTongueTip(&tip))
            return;

        // Measured as an offset from the Yoshi, not as a world position: the
        // puppet is somewhere else entirely, so only the relationship between
        // the two travels.
        const sead::Vector3f& root = al::getTrans(hackActor);
        const sead::Vector3f offset(tip.x - root.x, tip.y - root.y, tip.z - root.z);
        const f32 length
            = std::sqrt(offset.x * offset.x + offset.y * offset.y + offset.z * offset.z);
        if (!(length > 0.0f))
            return;

        out[0] = encodeSignedUnit(offset.x / length);
        out[1] = encodeSignedUnit(offset.y / length);
        out[2] = encodeSignedUnit(offset.z / length);
        out[3] = length >= cTongueMaxLen ? 1.0f : length / cTongueMaxLen;
    }

    void setupYoshi(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        setupModelWithSubActors(actor, initInfo);
    }

    void applyYoshi(PuppetHackActor* actor, const float* in) {
        al::LiveActor* tongue = actor->mAdapterSubActor;
        if (!tongue)
            return;

        const sead::Vector3f offset(
            decodeSignedUnit(in[0]), decodeSignedUnit(in[1]), decodeSignedUnit(in[2]));
        const f32 length = in[3] * cTongueMaxLen;

        if (length < cTongueOutMin) {
            if (!al::isDead(tongue))
                tongue->makeActorDead();
            return;
        }

        if (al::isDead(tongue)) {
            tongue->makeActorAlive();
            al::showModel(tongue);
        }

        // Root at the Yoshi, pointing at the synced tip. The real tongue is
        // shaped by a YoshiTongueJointControlKeeper that a plain archive actor
        // does not have, so this is a straight stand-in: right place, right
        // direction, no bend. Whether it reads as a tongue depends on the
        // model's bind pose and which axis it extends along, neither of which
        // is knowable without looking at it - tune here.
        al::setTrans(tongue, al::getTrans(actor));

        sead::Quatf facing = sead::Quatf::unit;
        al::makeQuatFrontUp(&facing, offset, sead::Vector3f::ey);
        al::setQuat(tongue, facing);
    }

    constexpr CaptureAdapter sAdapterYoshi = {
        sampleYoshi, setupYoshi, applyYoshi
    };

    // --- Yukimaru (Shiverian Racer) ---
    //
    // Two things make this one different from every other capture in the
    // table, and both come out of Yukimaru::init (ida_context/Npc/Yukimaru.md,
    // 0x003f1278).
    //
    // First, the archive is shared with the racer's other two lives. Its own
    // init is
    //
    //     al::initActorWithArchiveName(this, info, "SnowManRacer", "Hack")
    //
    // and ObjectData/SnowManRacer ships InitActorHack / InitActorRacer /
    // InitActorPlayer with NO unsuffixed InitActor at all - the same archive
    // backs the NPC, the race opponents and YukimaruRacePlayer. Built without
    // the "Hack" suffix a pooled model gets the wrong init set and none of the
    // *Hack actions (ActionAnimCtrlHack.byml). That is what the table's suffix
    // column is for; nothing here has to do it.
    //
    // Second, the snow ball. It is not animated by the model: init builds a
    // joint controller keeper and binds a GLOBAL QUAT controller on the
    // "Rotate" joint to a sead::Quatf the actor owns at +0x148, seeded from
    // the actor's own quat and then driven every frame by YukimaruStateMove,
    // which is handed &this->0x148 in YukimaruStateHack's constructor
    // (YukimaruStateHack.md:101). A pooled model has no state machine - its
    // nerve keeper is nulled like every capture model's - so unless that
    // quaternion travels, a remote Shiverian slides around with a ball that
    // never turns.
    //
    // The quat is world-space, which is exactly what suits the wire: the
    // puppet's own transform is synced separately, so the two are independent
    // and neither needs rebasing on arrival.
    constexpr ptrdiff_t cYukimaruRotateQuatOffset = 0x148;

    void sampleYukimaru(const al::LiveActor* hackActor, float* out) {
        // Identity, so a puppet that never gets a real sample sits still
        // rather than snapping to some arbitrary rotation.
        out[0] = encodeSignedUnit(0.0f);
        out[1] = encodeSignedUnit(0.0f);
        out[2] = encodeSignedUnit(0.0f);
        out[3] = encodeSignedUnit(1.0f);

        if (!hackActor)
            return;

        // Safe to reach into: this adapter is only ever selected for the row
        // whose hackName is "Yukimaru", and that name comes from the hack
        // actor's own InitHackCap.byml, so the actor IS a Yukimaru.
        const sead::Quatf& rotate = *reinterpret_cast<const sead::Quatf*>(
            reinterpret_cast<const u8*>(hackActor) + cYukimaruRotateQuatOffset);

        out[0] = encodeSignedUnit(rotate.x);
        out[1] = encodeSignedUnit(rotate.y);
        out[2] = encodeSignedUnit(rotate.z);
        out[3] = encodeSignedUnit(rotate.w);
    }

    void setupYukimaru(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        // One controller, same as vanilla. The keeper is sized here rather than
        // through tryCreateAndInitJointSpringControllers because this is not a
        // spring bone and the archive has no InitJointSpringCtrl to size from.
        AlUnlinked::initJointControllerKeeper(actor, 1);
        AlUnlinked::initJointGlobalQuatController(actor, &actor->mExtraQuat, "Rotate");
    }

    void applyYukimaru(PuppetHackActor* actor, const float* in) {
        sead::Quatf rotate;
        rotate.x = decodeSignedUnit(in[0]);
        rotate.y = decodeSignedUnit(in[1]);
        rotate.z = decodeSignedUnit(in[2]);
        rotate.w = decodeSignedUnit(in[3]);

        // The [0, 1] wire encoding costs a bit of precision, and a joint
        // controller handed a non-unit quaternion scales the joint with it.
        const f32 lengthSq = rotate.x * rotate.x + rotate.y * rotate.y + rotate.z * rotate.z
            + rotate.w * rotate.w;
        if (!(lengthSq > 0.0f)) {
            actor->mExtraQuat = sead::Quatf::unit;
            return;
        }

        const f32 inv = 1.0f / std::sqrt(lengthSq);
        rotate.x *= inv;
        rotate.y *= inv;
        rotate.z *= inv;
        rotate.w *= inv;

        actor->mExtraQuat = rotate;
    }

    constexpr CaptureAdapter sAdapterYukimaru = {
        sampleYukimaru, setupYukimaru, applyYukimaru
    };

    // YukimaruRacePlayer::init does what Yukimaru::init does - one joint
    // controller keeper, one global quat controller on "Rotate" - but from a
    // quaternion at +0x130 instead of +0x148 (ida_context/MapObj/
    // YukimaruRacePlayer.md, 0x034ee28). Same encoding, same applyYukimaru on
    // the far side; only the read differs.
    constexpr ptrdiff_t cYukimaruRacePlayerRotateQuatOffset = 0x130;

    void sampleYukimaruRacePlayer(const al::LiveActor* raceActor, float* out) {
        out[0] = encodeSignedUnit(0.0f);
        out[1] = encodeSignedUnit(0.0f);
        out[2] = encodeSignedUnit(0.0f);
        out[3] = encodeSignedUnit(1.0f);

        if (!raceActor)
            return;

        const sead::Quatf& rotate = *reinterpret_cast<const sead::Quatf*>(
            reinterpret_cast<const u8*>(raceActor) + cYukimaruRacePlayerRotateQuatOffset);

        out[0] = encodeSignedUnit(rotate.x);
        out[1] = encodeSignedUnit(rotate.y);
        out[2] = encodeSignedUnit(rotate.z);
        out[3] = encodeSignedUnit(rotate.w);
    }

    // --- HackFork / HackBoard (the Volbonan fork and the signpost) ---
    //
    // Why these two looked "not synced" while the log said the model was
    // built, leased, alive and drawn: the pooled fork is placed at the hack
    // actor's transform, which for a fork stuck in lava or a signpost in the
    // ground is the position of the REAL one standing right there. The copy
    // therefore lands exactly on top of the original and the only way to tell
    // them apart is the thing that makes a captured fork a captured fork - it
    // bends. That bend never travelled, so what a spectator saw was an
    // unchanged fork and a hat moving on its own.
    //
    // The bend is not an animation. HackFork::init (ida_context/MapObj/
    // HackFork.md, 0x0029c6a4) builds a JointControllerKeeper of ten and, for
    // each of the joints Stick01..Stick05,
    //
    //     al::initJointLocalAxisRotator(this, this->bendAxis, &this->bendAngle,
    //                                   "StickNN", false);
    //     al::initJointLocalXRotator(this, &this->twist, "StickNN");
    //
    // with bendAxis at +0x234, bendAngle at +0x240 and twist at +0x230.
    // HackFork::bendAndTwist writes exactly those: the angle ramps by one
    // degree a frame up to 22.5, and the axis is the normalised cross product
    // of the fork's own dir and the stick input.
    //
    // Three of the four wire slots carry the axis and the fourth the angle.
    // The twist is deliberately dropped: it is a spin about each stick's own
    // length, so it costs a slot to say something an onlooker can barely see,
    // while the bend is the entire silhouette.
    constexpr ptrdiff_t cHackForkTwistOffset = 0x230;
    constexpr ptrdiff_t cHackForkBendAxisOffset = 0x234;
    constexpr ptrdiff_t cHackForkBendAngleOffset = 0x240;
    // HackFork::bendAndTwist's own fminf ceiling.
    constexpr f32 cHackForkBendAngleMax = 22.5f;
    // Stick05 is the last joint the vanilla init asks for.
    constexpr const char* cHackForkStickJoints[]
        = { "Stick01", "Stick02", "Stick03", "Stick04", "Stick05" };

    void sampleHackFork(const al::LiveActor* hackActor, float* out) {
        // A straight, unbent fork: any axis, zero angle.
        out[0] = encodeSignedUnit(1.0f);
        out[1] = encodeSignedUnit(0.0f);
        out[2] = encodeSignedUnit(0.0f);
        out[3] = 0.0f;

        if (!hackActor)
            return;

        // Safe to reach into for the same reason sampleYukimaru is: this
        // adapter is only ever selected for a row whose hackName comes off a
        // HackFork's own InitHackCap.byml, so the actor IS a HackFork.
        const u8* raw = reinterpret_cast<const u8*>(hackActor);
        const sead::Vector3f& axis
            = *reinterpret_cast<const sead::Vector3f*>(raw + cHackForkBendAxisOffset);
        const f32 angle = *reinterpret_cast<const f32*>(raw + cHackForkBendAngleOffset);

        out[0] = encodeSignedUnit(axis.x);
        out[1] = encodeSignedUnit(axis.y);
        out[2] = encodeSignedUnit(axis.z);
        out[3] = angle <= 0.0f ? 0.0f
                               : (angle >= cHackForkBendAngleMax ? 1.0f
                                                                 : angle / cHackForkBendAngleMax);
    }

    void setupHackFork(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        // Ten, as vanilla: five axis rotators plus five X rotators.
        AlUnlinked::initJointControllerKeeper(actor, 10);

        actor->mExtraVec = sead::Vector3f::ex;
        actor->mExtra[0] = 0.0f;
        actor->mExtra[1] = 0.0f;
        actor->mAdapterCtrlCount = 0;

        for (const char* joint : cHackForkStickJoints) {
            // A signpost is the same class with a different model, and a
            // reduced archive need not carry all five sticks. Vanilla checks
            // the return before keeping it; so does this.
            void* controller = AlUnlinked::initJointLocalAxisRotator(
                actor, actor->mExtraVec, &actor->mExtra[0], joint, false);
            if (controller && actor->mAdapterCtrlCount < PuppetHackActor::kMaxAdapterCtrls)
                actor->mAdapterCtrls[actor->mAdapterCtrlCount++] = controller;

            AlUnlinked::initJointLocalXRotator(actor, &actor->mExtra[1], joint);
        }
    }

    void applyHackFork(PuppetHackActor* actor, const float* in) {
        sead::Vector3f axis(decodeSignedUnit(in[0]), decodeSignedUnit(in[1]),
            decodeSignedUnit(in[2]));

        // A joint rotator handed a non-unit axis scales the joint with it, the
        // same trap the Yukimaru quaternion has.
        const f32 lengthSq = axis.x * axis.x + axis.y * axis.y + axis.z * axis.z;
        if (lengthSq > 0.0f) {
            const f32 inv = 1.0f / std::sqrt(lengthSq);
            axis.x *= inv;
            axis.y *= inv;
            axis.z *= inv;
        } else {
            axis = sead::Vector3f::ex;
        }

        actor->mExtraVec = axis;
        actor->mExtra[0] = in[3] * cHackForkBendAngleMax;

        // The angle is read back through the pointer every frame, the axis is
        // not - JointLocalAxisRotator copied it at construction. Vanilla
        // re-writes it into every controller it kept; so does this.
        for (s32 i = 0; i < actor->mAdapterCtrlCount; i++)
            AlUnlinked::setAxisRotatorAxis(actor->mAdapterCtrls[i], axis);
    }

    constexpr CaptureAdapter sAdapterHackFork = {
        sampleHackFork, setupHackFork, applyHackFork
    };

    // --- Fukankun (the binoculars) ---
    //
    // Same shape of problem as the fork: the pooled binoculars sit on the real
    // ones and the difference between captured and not is entirely in the head
    // joint, which never moved on the copy.
    //
    // Fukankun::init memcpys sead::Matrix34f::ident into +0x190 and binds it
    // with al::initJointLocalMtxController(this, &mtx, "Head"); the only thing
    // that ever writes it afterwards is
    // al::rotateMtxXDirDegree(&mtx, &mtx, degrees), i.e. an accumulating
    // rotation about the head's local X - looking up and down through the
    // binoculars.
    //
    // So only the 2x2 sub-block that an X rotation touches has to travel, and
    // sending all four of its entries verbatim rather than an extracted angle
    // keeps this independent of which of m[1][2] / m[2][1] sead's row-major
    // layout puts the sine in.
    constexpr ptrdiff_t cFukankunHeadMtxOffset = 0x190;

    void sampleFukankun(const al::LiveActor* hackActor, float* out) {
        // Identity sub-block: a head that is not tilted at all.
        out[0] = encodeSignedUnit(1.0f);
        out[1] = encodeSignedUnit(0.0f);
        out[2] = encodeSignedUnit(0.0f);
        out[3] = encodeSignedUnit(1.0f);

        if (!hackActor)
            return;

        const sead::Matrix34f& mtx = *reinterpret_cast<const sead::Matrix34f*>(
            reinterpret_cast<const u8*>(hackActor) + cFukankunHeadMtxOffset);

        out[0] = encodeSignedUnit(mtx.m[1][1]);
        out[1] = encodeSignedUnit(mtx.m[1][2]);
        out[2] = encodeSignedUnit(mtx.m[2][1]);
        out[3] = encodeSignedUnit(mtx.m[2][2]);
    }

    void setupFukankun(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        // Written out rather than copied from sead::Matrix34f::ident: this runs
        // during scene init and has no business depending on a static this
        // module may not have had a constructor run for.
        sead::Matrix34f& mtx = actor->mExtraMtx;
        mtx.m[0][0] = 1.0f;
        mtx.m[0][1] = 0.0f;
        mtx.m[0][2] = 0.0f;
        mtx.m[0][3] = 0.0f;
        mtx.m[1][0] = 0.0f;
        mtx.m[1][1] = 1.0f;
        mtx.m[1][2] = 0.0f;
        mtx.m[1][3] = 0.0f;
        mtx.m[2][0] = 0.0f;
        mtx.m[2][1] = 0.0f;
        mtx.m[2][2] = 1.0f;
        mtx.m[2][3] = 0.0f;

        AlUnlinked::initJointControllerKeeper(actor, 1);
        AlUnlinked::initJointLocalMtxController(actor, &actor->mExtraMtx, "Head");
    }

    void applyFukankun(PuppetHackActor* actor, const float* in) {
        sead::Matrix34f& mtx = actor->mExtraMtx;
        mtx.m[1][1] = decodeSignedUnit(in[0]);
        mtx.m[1][2] = decodeSignedUnit(in[1]);
        mtx.m[2][1] = decodeSignedUnit(in[2]);
        mtx.m[2][2] = decodeSignedUnit(in[3]);
    }

    constexpr CaptureAdapter sAdapterFukankun = {
        sampleFukankun, setupFukankun, applyFukankun
    };

    // --- BazookaElectric (the Mini Rocket) ---
    //
    // BazookaElectric::init binds a global quat controller to the "Direction"
    // joint from a sead::Quatf it owns at +0x1C0 (ida_context/MapObj/
    // BazookaElectric.md, 0x0219248) - the aim, which is what the rider is
    // actually doing with it and the only part of a captured rocket that moves
    // before it fires. Identical in shape to the Yukimaru snowball, so it uses
    // the same storage and the same [0,1] quaternion encoding.
    constexpr ptrdiff_t cBazookaDirectionQuatOffset = 0x1C0;

    void sampleBazookaElectric(const al::LiveActor* hackActor, float* out) {
        out[0] = encodeSignedUnit(0.0f);
        out[1] = encodeSignedUnit(0.0f);
        out[2] = encodeSignedUnit(0.0f);
        out[3] = encodeSignedUnit(1.0f);

        if (!hackActor)
            return;

        const sead::Quatf& aim = *reinterpret_cast<const sead::Quatf*>(
            reinterpret_cast<const u8*>(hackActor) + cBazookaDirectionQuatOffset);

        out[0] = encodeSignedUnit(aim.x);
        out[1] = encodeSignedUnit(aim.y);
        out[2] = encodeSignedUnit(aim.z);
        out[3] = encodeSignedUnit(aim.w);
    }

    void setupBazookaElectric(PuppetHackActor* actor, const al::ActorInitInfo& initInfo) {
        AlUnlinked::initJointControllerKeeper(actor, 1);
        AlUnlinked::initJointGlobalQuatController(actor, &actor->mExtraQuat, "Direction");
    }

    void applyBazookaElectric(PuppetHackActor* actor, const float* in) {
        // Shares applyYukimaru's normalisation for the same reason: the [0, 1]
        // encoding costs precision and a joint controller scales the joint by
        // a non-unit quaternion.
        applyYukimaru(actor, in);
    }

    constexpr CaptureAdapter sAdapterBazookaElectric = {
        sampleBazookaElectric, setupBazookaElectric, applyBazookaElectric
    };

    // Sub-actor archives are built with al::initChildActorWithArchiveNameNo
    // PlacementInfo, which FAULTS on an archive that is not resident rather than
    // returning - the same fault that took out stage load through buildOne. Every
    // name here must be a real ObjectData entry.
    //
    // YoshiTongue is confirmed twice over: YoshiTongue::init (ida_context/Player/
    // YoshiTongue.md, 0x004a0990) loads ObjectData/YoshiTongue, and the archive
    // exists in romfs. It is created by YoshiStateHackPlay, i.e. only while Mario
    // is riding Yoshi, which is why a puppet built from the Yoshi model alone has
    // the head animation but nothing to extend.
    static constexpr CaptureSubActor sYoshiSubActors[] = {
        { "YoshiTongue", nullptr, nullptr }
    };

    // ---- variant families (see Entry::variants) ----
    //
    // Order is load-bearing twice over - it is the residency fallback order AND
    // the wire index - so a family may be appended to, but never reordered, for
    // as long as an older build can be on the other end of the socket.
    //
    // The anagram letters. AnagramAlphabet, the holder, is the only one of the
    // pair the actor factory registers, and it builds its letters itself with
    // al::initLinksActor - which never goes through
    // al::createPlacementActorFromFactory, so the letters are invisible to the
    // placement bridge and only the holder's own placement can announce them.
    // The holder has no model to name an archive from, so the family is listed
    // and residency picks: a stage loads only the letters its word spells.
    //
    // Which letter the remote player is riding now travels as the index into
    // this array, so a stage spelling MARIO no longer shows five Ms. The sender
    // reads it off its own AnagramAlphabetCharacter, whose model name IS the
    // archive: init goes through al::initActorChangeModel with the name the
    // holder pulled out of the placement link (ida_context/MapObj/
    // AnagramAlphabet.md, 0x021447c).
    static constexpr const char* sAnagramAlphabetArchives[] = {
        "AnagramAlphabetM", "AnagramAlphabetA", "AnagramAlphabetI", "AnagramAlphabetO",
        "AnagramAlphabetR"
    };

    // The Fukuwarai face pieces, both faces. Same story - FukuwaraiWatcher is
    // the registered class and it builds its parts with al::initLinksActor -
    // and every piece of one face carries the same HackName, which is why every
    // Goomba piece rendered as the angry mouth and every Mario piece as the
    // nose: one model per hack key, and the key could not tell them apart.
    //
    // It can now. These are the names FukuwaraiFaceParts itself compares
    // al::getModelName(this) against (ida_context/Npc/FukuwaraiFaceParts.md,
    // 0x0392920), which is the same string the sender reads off the piece it is
    // carrying, so the index names the exact piece.
    static constexpr const char* sFukuwaraiMarioArchives[] = {
        "FukuwaraiMarioNose", "FukuwaraiMarioMouth", "FukuwaraiMarioEyeRight",
        "FukuwaraiMarioEyeLeft", "FukuwaraiMarioEyebrowRight", "FukuwaraiMarioEyebrowLeft",
        "FukuwaraiMarioMustache"
    };
    static constexpr const char* sFukuwaraiKuriboArchives[] = {
        "FukuwaraiKuriboMouthAngry", "FukuwaraiKuriboEyeRight", "FukuwaraiKuriboEyeLeft",
        "FukuwaraiKuriboEyebrowRight", "FukuwaraiKuriboEyebrowLeft",
        "FukuwaraiKuriboMarioEyeRight", "FukuwaraiKuriboMarioEyeLeft"
    };

    // Both New Donk City manholes. Two separate map-parts placements with two
    // different models but one shared HackName, so the pool keyed both on
    // "Manhole", whichever registered first won, and the big one always came out
    // small. Same fix as the face pieces: the sender says which model it is
    // standing on and the receiver builds that one.
    static constexpr const char* sManholeArchives[] = {
        "CityWorldHomeManhole001", "CityWorldHomeManhole002"
    };

    // Chain Chomp's chain is NOT one sub-actor. Wanwan::init builds a
    // BreedaChainList("WanwanChain" / "WanwanBigChain", 8, ...) - eight link
    // actors managed by a list object and attached with
    // BreedaChainList::registerHostSubActorSyncClipping (ida_context/Enemy/
    // Wanwan.md:591-608). Creating one child would show a single floating link,
    // so the chain stays absent until that list is replicated. Its other two
    // children are deliberately not wanted on a puppet either: WanwanShootMark
    // is a targeting reticle and WanwanPile is the stake, which belongs to the
    // real placement.
    //
    // Gamane has no sub-actors at all. Gamane::init is just
    // al::initActorWithArchiveName(this, info, "Gamane", 0) - the old
    // CaptureTypes.h "has sub-actors" annotation was wrong, and GamaneBullet is
    // a SensorMsg type (rs::isMsgGamaneBullet), not a child model.

    static constexpr Entry sTable[] = {
        // Tier 1 (already working ~30, corrected to hack domain)
        { "Frog", "Frog", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "ElectricWire", "ElectricWire", nullptr, "ElectricWireMover", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "KuriboWing", "KuriboWing", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Wanwan", "Wanwan", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "WanwanBig", "Wanwan", "WanwanBig", "WanwanBig", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "TRex", "TRexSleep", nullptr, "TRex", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "TRex", "TRexPatrol", nullptr, "TRex", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Fukankun", "Fukankun", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterFukankun },
        { "Killer", "KillerLauncher", nullptr, "Killer", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Megane", "Megane", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        // PossessedMapParts, not HackMapParts. al::tryGetClassName reads the
        // placement's UnitConfig/ParameterConfigName, which is a FACTORY REGISTRY
        // KEY and need not match the C++ class it builds - here it builds a
        // HackMapParts. Measured in mp_debug:
        //   mapparts placement class=PossessedMapParts placementModel=HackMoveTree
        //   actorModel=HackMoveTree -> row=(none)
        // which is why Tree and Boulder registered nothing and the puppet kept
        // sliding around in Mario's default pose.
        { "Cactus", "PossessedMapParts", "HackMoveCactus", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts },
        { "Kuribo", "KuriboPossessed", nullptr, "Kuribo", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "BazookaElectric", "BazookaElectric", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterBazookaElectric },
        { "Kakku", "Kakku", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "JugemFishing", "JugemFishing", nullptr, "Jugem", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Fastener", "Fastener", nullptr, nullptr, "NoShadow", Build::Generic, nullptr, 0, nullptr },
        // The same zipper, placed by the other class that owns one. Both
        // Fastener::init and FastenerObj::init build ObjectData/Fastener and
        // both call rs::createCapTargetInfo (ida_context/MapObj/Fastener.md
        // 0x0270f10, FastenerObj.md 0x027a64c), but only "Fastener" was in the
        // table - so a stage whose zippers are FastenerObj registered nothing
        // and the pool answered "not placed" while the puppet stood inside the
        // wall with only its hat showing. Measured in mp_debug, in
        // LakeWorldHomeStage. Same hackName, so this shares the pool entry
        // rather than costing a second model.
        { "Fastener", "FastenerObj", nullptr, "Fastener", "NoShadow", Build::Generic, nullptr, 0, nullptr },
        { "Pukupuku", "Pukupuku", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "PackunPoison", "PackunPoison", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Senobi", "SenobiGeneratePoint", nullptr, "Senobi", nullptr, Build::Generic, nullptr, 0, &sAdapterSenobi },
        { "FireBros", "FireBrosPossessed", nullptr, "FireBros", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Tank", "Tank", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Gamane", "Gamane", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Tree", "PossessedMapParts", "HackMoveTree", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts },
        { "RockForest", "PossessedMapParts", "HackMoveRockForest", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts },
        { "Imomu", "Imomu", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Manhole", "PossessedMapParts", "CityWorldHomeManhole001", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts, sManholeArchives, 2 },
        // CityWorldHomeStage places BOTH manholes (unit=CityWorldHomeManhole002,
        // param=PossessedMapParts, no ModelName - so the actor model name is the
        // unit name). The four map-parts rows are all qualified by model, with no
        // wildcard PossessedMapParts row to fall back to, so the second manhole
        // matched nothing and never registered.
        //
        // Registering was only half of it. Both rows carry the same hackName, so
        // the pool built one model for "Manhole" from whichever placement it saw
        // first - always the small one - and the big manhole rendered small. The
        // shared family above is what tells the two apart on the wire; both rows
        // must point at the SAME array so the index means the same archive on
        // both machines.
        { "Manhole", "PossessedMapParts", "CityWorldHomeManhole002", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts, sManholeArchives, 2 },
        // The Bowser statue. Another map-parts capture, not a Statue: its
        // HackName lives in ObjectData/HackMoveRock/InitHackCap.byml and
        // HackObjList gives it TutorialName "HackMove" like Cactus and Tree.
        // Measured as missing in mp_debug: "no table row for hackName=StatueKoopa".
        { "StatueKoopa", "PossessedMapParts", "HackMoveRock", nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackMapParts },
        { "Car", "Car", nullptr, nullptr, "TQSV", Build::Generic, nullptr, 0, nullptr },
        // CityMan + "RadiCon", not RadiConCar. RadiconNpc::init is what carries
        // the CapTargetInfo, so the player becomes the man with the controller
        // and the car stays his - see the "--- Radicon ---" note near the top of
        // this file.
        { "Radicon", "RadiconNpc", nullptr, "CityMan", "RadiCon", Build::Generic, nullptr, 0, nullptr },
        { "Byugo", "Byugo", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "PukupukuSnow", "Pukupuku", "PukupukuSnow", "Pukupuku", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Hosui", "Hosui", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Bubble", "Bubble", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "HackFork", "HackFork", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, &sAdapterHackFork },
        { "HammerBros", "HammerBrosPossessed", nullptr, "HammerBros", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "PackunFire", "PackunFire", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Tsukkun", "Tsukkun", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Statue", "Statue", nullptr, "StatueJizo", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "KaronWing", "KaronWing", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        // Banzai Bill. The old row keyed on placementClass
        // "KillerLauncherMagnum", which is the placement's UnitConfigName -
        // but al::tryGetClassName returns UnitConfig/ParameterConfigName, and
        // for every one of the eleven Magnum launchers in the game that is
        // plain "KillerLauncher" (config/enemy_capture_map.csv). So the row
        // never matched anything: the big launcher fell through to the
        // ordinary "Killer" row above, KillerMagnum registered nowhere and the
        // pool answered "acquire miss for hackType=KillerMagnum (not placed)"
        // in KillerRailCollisionExStage - a stage that places Magnum launchers
        // and nothing else.
        //
        // KillerLauncher::init tells the two apart the same way this row now
        // does, by comparing al::getModelName(this) against
        // "KillerLauncherMagnum" (ida_context/Enemy/KillerLauncher.md
        // 0x0147eac), and Killer::init picks archive "KillerMagnum" over
        // "Killer" off the flag that sets (Killer.md 0x0145694).
        { "KillerMagnum", "KillerLauncher", "KillerLauncherMagnum", "KillerMagnum", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Bull", "Bull", nullptr, nullptr, nullptr, Build::Generic, nullptr, 0, nullptr },
        { "Koopa", "Koopa", nullptr, "KoopaHack", nullptr, Build::Generic, nullptr, 0, nullptr },
        // No archive override: there is no ObjectData/AnagramAlphabet. The five
        // letters ship as AnagramAlphabetA / I / M / O / R and the placement
        // carries the one it wants in ModelName, so the pool has to take the
        // actor model name. Which of the five a given puppet shows is the wire
        // variant's job - see sAnagramAlphabetArchives.
        //
        // ...and the placement class is the HOLDER, not the letter. Only
        // AnagramAlphabet is in ProjectActorFactory; AnagramAlphabet::init
        // builds each AnagramAlphabetCharacter itself with al::initLinksActor
        // (ida_context/MapObj/AnagramAlphabet.md 0x0214510), which does not go
        // through al::createPlacementActorFromFactory - the one function the
        // placement bridge is hooked on - so no letter ever reached the pool
        // and it answered "not placed". The holder has no model of its own to
        // name an archive from, hence the family list.
        { "AnagramAlphabetCharacter", "AnagramAlphabet", nullptr, "AnagramAlphabetM", nullptr, Build::Generic, nullptr, 0, nullptr, sAnagramAlphabetArchives, 5 },
        { "Yoshi", "Yoshi", nullptr, nullptr, nullptr, Build::YoshiModel, sYoshiSubActors, 1, &sAdapterYoshi },
        // The Shiverian Racer. Placed as class "Yukimaru" (SnowWorldLobby000);
        // the archive and the mandatory "Hack" suffix are both spelled out
        // because the actor model name alone would build the NPC, not the
        // capture - see sAdapterYukimaru. Measured as missing in mp_debug:
        // "no table row for hackName=Yukimaru".
        { "Yukimaru", "Yukimaru", nullptr, "SnowManRacer", "Hack", Build::Generic, nullptr, 0, &sAdapterYukimaru },
        // The same archive, proven resident by the racers instead. Measured as
        // "acquire miss for hackType=Yukimaru (not placed)" in
        // SnowWorldLobbyExStage: a capturable Shiverian was plainly standing
        // there, but nothing in that stage carries ParameterConfigName
        // "Yukimaru" - the lobby's Shiverians are placed as racers.
        // YukimaruRacer, YukimaruRacerTiago and YukimaruRacePlayer all build
        // ObjectData/SnowManRacer (with the "Racer" / "Player" suffixes, but
        // the archive is what residency is about), so any of them proves the
        // pool may build the "Hack" variant. They share the hackName, so they
        // share the single pooled model rather than adding one each.
        { "Yukimaru", "YukimaruRacer", nullptr, "SnowManRacer", "Hack", Build::Generic, nullptr, 0, &sAdapterYukimaru },
        { "Yukimaru", "YukimaruRacerTiago", nullptr, "SnowManRacer", "Hack", Build::Generic, nullptr, 0, &sAdapterYukimaru },
        { "Yukimaru", "YukimaruRacePlayer", nullptr, "SnowManRacer", "Hack", Build::Generic, nullptr, 0, &sAdapterYukimaru },

        // Rides. Not captures - neither class creates a CapTargetInfo, so
        // PlayerHackKeeper never sees them - but the model side is identical:
        // ObjectData/Motorcycle and ObjectData/SphinxRide both exist and both
        // are resident wherever one is placed.
        { "Motorcycle", "Motorcycle", nullptr, nullptr, nullptr, Build::Ride, nullptr, 0, nullptr },
        { "SphinxRide", "SphinxRide", nullptr, nullptr, nullptr, Build::Ride, nullptr, 0, nullptr },

        // Tier 2 & 3
        //
        // The six rows below were Build::Unsupported, i.e. "registered so the
        // log says why, never built" - which is what put a floating Mario over
        // Madame Broode's Chain Chomp, Knucklotec's fist and all four puzzle
        // pieces. Each was blocked on the same missing fact: the archive the
        // capturable actor is actually built from is not the placement's, and
        // nothing in the placement says what it is. All six are now named from
        // the decompiled init that builds them, and the pool's existing
        // residency check (archiveExists + isArchiveResident, before anything
        // is allocated) is what keeps a wrong name harmless - it falls back to
        // the puppet's own body, exactly as Unsupported did.
        //
        // BreedaWanwan::init: al::initActorWithArchiveName(this, info,
        // "BreedaWanwan", nullptr) plus rs::createCapTargetInfo
        // (ida_context/Boss/Breeda/BreedaWanwan.md, 0x004e198). The chomp is
        // Breeda's own child, so a Breeda placement is what proves it resident.
        // The chain is deliberately still absent - it is a BreedaChainList of
        // eight link actors, not one sub-actor (see the note above sTable).
        { "BreedaWanwan", "Breeda", nullptr, "BreedaWanwan", nullptr, Build::Generic, nullptr, 0, nullptr },
        // BossKnuckleHand::init: archive "BossKnuckleL" for both hands - the
        // right one is the same model with scale.x negated
        // (ida_context/Boss/BossKnuckle/BossKnuckleHand.md, 0x00295c4). The
        // observed placement archive was BossKnuckleBody, which is the boss,
        // not the fist.
        { "BossKnuckleHand", "BossKnuckle", nullptr, "BossKnuckleL", nullptr, Build::Generic, nullptr, 0, nullptr },
        // The sliding picture puzzles. Gotogoton::init goes through
        // al::initMapPartsActor, so the archive is the placement's own unit
        // name - GotogotonLake000 in GotogotonExStage, GotogotonCity000 in
        // CityWorldFactoryStage - while both placements carry
        // ParameterConfigName "Gotogoton". They are therefore left as two
        // unqualified rows on one class and told apart by which archive the
        // stage has loaded; the old rows guessed model names ("GotogotonLake")
        // that no placement carries, so neither ever matched.
        { "GotogotonLake", "Gotogoton", nullptr, "GotogotonLake000", nullptr, Build::Generic, nullptr, 0, nullptr },
        { "GotogotonCity", "Gotogoton", nullptr, "GotogotonCity000", nullptr, Build::Generic, nullptr, 0, nullptr },
        // The face-building puzzle pieces, same shape of problem twice over:
        // the pieces are link children of FukuwaraiWatcher and never reach the
        // placement bridge, the watcher has no model to qualify a row with,
        // and the two faces live in different stages. Two unqualified rows on
        // the one class, each with its face's variant family, and residency
        // decides which face this stage has - while the wire variant decides
        // which PIECE of it the puppet is holding.
        { "FukuwaraiFacePartsKuribo", "FukuwaraiWatcher", nullptr, "FukuwaraiKuriboMouthAngry", nullptr, Build::Generic, nullptr, 0, nullptr, sFukuwaraiKuriboArchives, 7 },
        { "FukuwaraiFacePartsMario", "FukuwaraiWatcher", nullptr, "FukuwaraiMarioNose", nullptr, Build::Generic, nullptr, 0, nullptr, sFukuwaraiMarioArchives, 7 },
        { "CarryMeat", "BirdCarryMeat", nullptr, nullptr, nullptr, Build::Unsupported, nullptr, 0, nullptr },
        // The signpost, and it has to come with a model name. HackFork::init
        // reads ModelName off the placement and builds either HackFork or
        // HackBoard (ida_context/MapObj/HackFork.md:685-745); only the HackBoard
        // one carries HackName "Guidepost". The old row claimed class "Stake",
        // which is the Chain Chomp pile and not a capture at all, so the pool
        // registered Guidepost against ObjectData/Stake ("Guidepost: archive
        // Stake not built") while every real signpost fell through to the
        // wildcard HackFork row and pooled a signboard under the fork's key
        // ("built HackFork (archive HackBoard)"). Both are in mp_debug.
        { "Guidepost", "HackFork", "HackBoard", "HackBoard", nullptr, Build::Generic, nullptr, 0, &sAdapterHackFork },
    };

    static constexpr int sTableSize = sizeof(sTable) / sizeof(sTable[0]);

#ifndef ANALYZER
    static constexpr std::array<const char*, sTableSize> sPlacementClasses = []() {
        std::array<const char*, sTableSize> arr {};
        for (size_t i = 0; i < sTableSize; i++) {
            arr[i] = sTable[i].placementClass;
        }
        return arr;
    }();
    static constexpr crc32::HashArray<sTableSize> sPlacementClassHashes(sPlacementClasses);
#endif

    namespace {

        // Which placed vehicle is which ride. Small and fixed: a stage places a
        // handful of these at most, and this is only ever asked about the one
        // actor the local player is bound to.
        constexpr s32 kMaxRidePlacements = 8;

        struct RidePlacement {
            const al::LiveActor* actor;
            const char* hackName;
        };

        RidePlacement sRidePlacements[kMaxRidePlacements];
        s32 sRidePlacementCount = 0;

    } // namespace

    namespace {

        struct RideActionSet {
            const char* hackName;
            const char* idle;
            const char* moving;
        };

        // Action names read straight off each class's own startAction calls
        // (ida_context/MapObj/Motorcycle.md, ida_context/Npc/SphinxRide.md).
        // Jaxi also has RunSlow, Turn, Jump and Land; only the two states that
        // can be told apart from position alone are used here.
        constexpr RideActionSet cRideActions[] = {
            { "Motorcycle", "Wait", "Run" },
            { "SphinxRide", "Wait", "Run" },
        };

    } // namespace

    const char* findRideAction(const char* hackName, bool moving) {
        if (!hackName || hackName[0] == '\0')
            return nullptr;
        for (const RideActionSet& set : cRideActions) {
            if (al::isEqualString(set.hackName, hackName))
                return moving ? set.moving : set.idle;
        }
        return nullptr;
    }

    void noteRidePlacement(al::LiveActor* actor, const Entry* entry) {
        if (!actor || !entry || entry->build != Build::Ride)
            return;
        if (sRidePlacementCount >= kMaxRidePlacements)
            return;

        for (s32 i = 0; i < sRidePlacementCount; i++) {
            if (sRidePlacements[i].actor == actor)
                return;
        }

        sRidePlacements[sRidePlacementCount].actor = actor;
        sRidePlacements[sRidePlacementCount].hackName = entry->hackName;
        sRidePlacementCount++;
        MpDebugLog::write("[CaptureSync] ride placement %s actor=%p (%d/%d)", entry->hackName,
            actor, sRidePlacementCount, kMaxRidePlacements);
    }

    void resetRidePlacements() {
        // Pointers into a scene heap that is being torn down - forget them,
        // never touch them. Same rule as PuppetCapturePool::onStageKill.
        sRidePlacementCount = 0;
    }

    const char* findRideName(const al::LiveActor* actor) {
        if (!actor)
            return nullptr;
        for (s32 i = 0; i < sRidePlacementCount; i++) {
            if (sRidePlacements[i].actor == actor)
                return sRidePlacements[i].hackName;
        }
        return nullptr;
    }

    s32 findVariantIndex(const Entry* entry, const char* archiveName) {
        if (!entry || !entry->variants || entry->variantCount <= 0)
            return -1;
        if (!archiveName || archiveName[0] == '\0')
            return -1;

        for (s32 i = 0; i < entry->variantCount; i++) {
            if (entry->variants[i] && al::isEqualString(entry->variants[i], archiveName))
                return i;
        }
        return -1;
    }

    const char* getVariantArchive(const Entry* entry, s32 variant) {
        if (!entry || !entry->variants || variant < 0 || variant >= entry->variantCount)
            return nullptr;
        return entry->variants[variant];
    }

    void encodeWireName(char* out, s32 outLen, const char* hackName, s32 variant) {
        if (!out || outLen <= 0)
            return;

        out[0] = '\0';
        if (!hackName)
            return;

        s32 len = 0;
        while (hackName[len] != '\0' && len < outLen - 1) {
            out[len] = hackName[len];
            len++;
        }
        out[len] = '\0';

        // A truncated hack name would resolve to no table row at all, so the
        // name always wins: the suffix is only added when it fits whole. Two
        // digits is the whole range this can ever need - the largest family in
        // the table has seven members.
        if (variant < 0 || variant > 99)
            return;

        const s32 suffixLen = variant < 10 ? 2 : 3;
        if (len + suffixLen >= outLen)
            return;

        out[len++] = '#';
        if (variant >= 10)
            out[len++] = static_cast<char>('0' + (variant / 10));
        out[len++] = static_cast<char>('0' + (variant % 10));
        out[len] = '\0';
    }

    s32 decodeWireName(char* nameOut, s32 nameOutLen, const char* wire) {
        if (!nameOut || nameOutLen <= 0)
            return -1;

        nameOut[0] = '\0';
        if (!wire)
            return -1;

        s32 len = 0;
        while (wire[len] != '\0' && wire[len] != '#' && len < nameOutLen - 1) {
            nameOut[len] = wire[len];
            len++;
        }
        nameOut[len] = '\0';

        if (wire[len] != '#')
            return -1;

        // Digits only. A peer that puts anything else after the '#' is either
        // corrupt or from a future protocol; either way the bare name is still
        // usable and "no variant" is the honest answer.
        s32 variant = 0;
        s32 digits = 0;
        for (const char* c = wire + len + 1; *c != '\0'; c++) {
            if (*c < '0' || *c > '9')
                return -1;
            variant = variant * 10 + (*c - '0');
            if (++digits > 2)
                return -1;
        }
        return digits > 0 ? variant : -1;
    }

    const Entry* findByHackName(const char* hackName) {
        if (!hackName || hackName[0] == '\0')
            return nullptr;
        for (int i = 0; i < sTableSize; i++) {
            if (al::isEqualString(sTable[i].hackName, hackName))
                return &sTable[i];
        }
        return nullptr;
    }

    s32 findByPlacementAll(const char* className, const char* modelName, const Entry** out,
        s32 maxOut) {
        if (!className || className[0] == '\0' || !out || maxOut <= 0)
            return 0;
#ifndef ANALYZER
        if (sPlacementClassHashes.FindIndex(className) < 0)
            return 0;
#endif
        // Two passes, and the order matters. Several classes carry more than one
        // capture and are told apart by the model/object name: Pukupuku places
        // both Pukupuku and PukupukuSnow, Wanwan places both sizes, HackMapParts
        // places four. A single pass would let the class's wildcard row (the one
        // with no modelName) shadow the qualified row whenever it appears first.
        s32 count = 0;
        for (int i = 0; i < sTableSize && count < maxOut; i++) {
            if (!sTable[i].modelName || !modelName)
                continue;
            if (al::isEqualString(sTable[i].placementClass, className)
                && al::isEqualString(sTable[i].modelName, modelName))
                out[count++] = &sTable[i];
        }

        // A qualified match is the answer on its own - that is what stops a
        // Magnum launcher from also registering the ordinary Bullet Bill, and
        // a signpost from also registering the fork.
        if (count > 0)
            return count;

        // ...and where nothing qualified, EVERY unqualified row for the class
        // is a candidate, not just the first. Two classes now carry more than
        // one capture with nothing in the placement to tell them apart -
        // Gotogoton (lake / city puzzle) and FukuwaraiWatcher (Goomba / Mario
        // face) - because in both cases the difference is which stage you are
        // standing in. PuppetCapturePool builds only what the stage has
        // actually loaded, so registering both and letting residency answer is
        // both correct and cheap.
        for (int i = 0; i < sTableSize && count < maxOut; i++) {
            if (sTable[i].modelName)
                continue;
            if (al::isEqualString(sTable[i].placementClass, className))
                out[count++] = &sTable[i];
        }
        return count;
    }

    const Entry* findByPlacement(const char* className, const char* modelName) {
        const Entry* best = nullptr;
        return findByPlacementAll(className, modelName, &best, 1) > 0 ? best : nullptr;
    }

    void installHooks() {
        // YoshiTongue::init(const al::ActorInitInfo&), main offset 0x004a0990.
        // Installed once at boot alongside PuppetCapturePool's; the tongue is
        // constructed and destroyed with every Yoshi ride, so the pointer is
        // refreshed each time rather than cached across one.
        static constexpr uintptr_t cYoshiTongueInitOffset = 0x004a0990;
        yoshiTongueInitHook.installAtMainOffset(cYoshiTongueInitOffset);
    }

}
