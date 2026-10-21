#pragma once
/**
 * @file HackCap.h
 * @brief Main Class for HackCap (Cappy)
 * Vtable loc: 1D75520
 */

#include "HackCap/HackCapJointControlKeeper.h"
#include "Library/HitSensor/HitSensorKeeper.h"
#include "Library/HitSensor/SensorFunction.h"
#include "Library/LiveActor/LiveActor.h"
#include "Player/HackCapThrowParam.h"
#include "Player/IUsePlayerCollision.h"
#include "Player/PlayerCapActionHistory.h"
#include "Player/PlayerInput.h"

class PlayerWallActionHistory;
class PlayerEyeSensorHitHolder;
class IUsePlayerHeightCheck;
class PlayerWetControl;
class PlayerJointControlKeeper;
class HackCapJudgePreInputSeparateThrow;
class HackCapJudgePreInputSeparateJump;
class PlayerSeparateCapFlag;

class CapTargetInfo;

class PlayerActorHakoniwa; // use a stub instead of the actual class file

#define HACKSIZE sizeof(al::LiveActor)

class HackCap : public al::LiveActor {
public:
    HackCap(const al::LiveActor*, const char*, const PlayerInput*, struct PlayerAreaChecker const*, const PlayerWallActionHistory*,
        const PlayerCapActionHistory*, const PlayerEyeSensorHitHolder*, const PlayerSeparateCapFlag*, const IUsePlayerCollision*,
        const IUsePlayerHeightCheck*, const PlayerWetControl*, const PlayerJointControlKeeper*, HackCapJudgePreInputSeparateThrow*,
        HackCapJudgePreInputSeparateJump*);

    enum SwingHandType
    {
        Left,
        Right
    };

    void init(const al::ActorInitInfo&);
    void hide(bool);
    void movement(void);
    void updateShadowMaskOffset(void);
    void control(void);
    void updateTargetLayout(void);
    void updateCollider(void);
    void updateFrameOutLayout(void);
    void attackSpin(al::HitSensor*, al::HitSensor*, float);
    void prepareLockOn(al::HitSensor*);
    void sendMsgStartHack(al::HitSensor*);
    void receiveRequestTransferHack(al::HitSensor*, al::HitSensor*);
    void startThrowSeparatePlayHack(al::HitSensor*, const sead::Vector3f&, const sead::Vector3f&, float);
    void startHack(void);
    void emitHackStartEffect(void);
    void noticeHackMarioEnter(void);
    void noticeHackDemoPuppetableEnd(void);
    void recordHack(void);
    void addHackStartDemo(void);
    void addLockOnKeepDemo(void);
    void syncHackDamageVisibility(bool);
    void endHack(void);
    void startSpinAttack(const char*);
    void startThrow(bool, const sead::Vector3f&, const sead::Vector3f&, float, const sead::Vector2f&, const sead::Vector2f&, const sead::Vector3f&, bool,
        const sead::Vector3f&, HackCap::SwingHandType, bool, float, int);
    void startThrowSeparatePlay(const sead::Vector3f&, const sead::Vector3f&, float, bool);
    void startThrowSeparatePlayJump(const sead::Vector3f&, const sead::Vector3f&, float);
    void startCatch(const char*, bool, const sead::Vector3f&);
    CapTargetInfo* getCapTargetInfo() const {
        return *reinterpret_cast<CapTargetInfo* const*>(reinterpret_cast<const char*>(this) + 0x228);
    }
    void forcePutOn(void);
    void forceHack(al::HitSensor*, const CapTargetInfo*); // :eyes:
    void resetLockOnParam(void);
    void setupStartLockOn(void);
    void cancelCapState(void);
    void requestReturn(bool*);
    void tryReturn(bool, bool*);
    void updateCapPose(void);
    void followTarget(void);
    void syncPuppetSilhouette(void);
    void recordCapJump(PlayerWallActionHistory*);
    void getFlyingSpeedMax(void);
    void getThrowSpeed(void);
    void requestLockOnHitReaction(const CapTargetInfo*, const char*);
    void startPuppet(void);
    void endPuppet(void);
    void hidePuppetCap(void);
    void showPuppetCap(void);
    void hidePuppetCapSilhouette(void);
    void showPuppetCapSilhouette(void);
    void startPuppetCheckpointWarp(void);
    void startHackShineGetDemo(void);
    void endHackThrowAndReturnHack(void);
    void endHackShineGetDemo(void);
    void calcHackFollowTrans(sead::Vector3f*, bool);
    void makeFollowMtx(sead::Matrix34<float>*);
    void updateCapEyeShowHide(bool, int);
    void activateInvincibleEffect(void);
    void syncInvincibleEffect(bool);
    void updateSeparateMode(const PlayerSeparateCapFlag*);
    void startRescuePlayer(void);
    void prepareCooperateThrow(void);
    void requestForceFollowSeparateHide(void);
    void calcSeparateHideSpeedH(const sead::Vector3f&);
    void updateModelAlphaForSnapShot(void);
    void getPadRumblePort(void);
    void updateThrowJoint(void);
    void setupThrowStart(void);
    void getThrowHeight(void);
    void checkEnableThrowStartSpace(sead::Vector3f*, sead::Vector3f*, sead::Vector3f*, const sead::Vector3f&, float, float, bool, const sead::Vector3f&);
    void updateWaterArea(void);
    void getThrowRange(void);
    void getThrowBrakeTime(void);
    void startThrowCapEyeThrowAction(void);
    void tryCollideReflectReaction(void);
    void tryCollideWallReaction(void);
    void changeThrowParamInWater(int, bool);
    void addCurveOffset(void);
    void tryAppendAttack(void);
    void tryCollideWallReactionSpiral(void);
    void endThrowSpiral(void);
    void tryCollideWallReactionReflect(void);
    void tryCollideWallReactionRollingGround(void);
    void rollingGround(void);
    void tryChangeSeparateThrow(void);
    void getThrowBackSpeed(void);
    void updateLavaSurfaceMove(void);
    void tryCollideWallReactionStay(void);
    void getThrowStayTime(void);
    void getThrowStayTimeMax(void);
    void getThrowSpeedAppend(void);
    void getThrowRangeAppend(void);
    void tryCollideWallLockOn(void);
    void endHackThrowAndReturnHackOrHide(void);
    void clearThrowType(void);
    void calcReturnTargetPos(sead::Vector3f*);
    void attackSensor(al::HitSensor*, al::HitSensor*);
    void stayRollingOrReflect(void);
    bool receiveMsg(const al::SensorMsg*, al::HitSensor*, al::HitSensor*);
    void endMove(void);
    void prepareTransferLockOn(al::HitSensor*);
    void collideThrowStartArrow(al::HitSensor*, const sead::Vector3f&, const sead::Vector3f&, const sead::Vector3f&);
    void trySendAttackCollideAndReaction(bool*);
    void stayWallHit(void);
    void endHackThrow(void);

    bool isFlying(void) const;
    bool isNoPutOnHide(void) const;
    bool isEnableThrow(void) const;
    bool isEnableSpinAttack(void) const;
    bool isSpinAttack(void) const;
    bool isEnableRescuePlayer(void) const;
    bool isRescuePlayer(void) const;
    bool isEnableHackThrow(bool*) const;
    bool isSeparateHipDropLand(void) const;
    bool isSeparateHide(void) const;
    bool isSeparateThrowFlying(void) const;
    bool isEnableThrowSeparate(void) const;
    bool isHoldInputKeepLockOn(void) const;
    bool isRequestableReturn(void) const;
    bool isLockOnEnableHackTarget(void) const;
    bool isWaitHackLockOn(void) const;
    bool isCatched(void) const;
    bool isHide(void) const;
    bool isPutOn(void) const;
    bool isLockOnInterpolate(void) const;
    bool isEnablePreInput(void) const;
    bool isForceCapTouchJump(void) const;
    bool isHackInvalidSeparatePlay(void) const;
    bool isHoldSpinCapStay(void) const;
    bool isThrowTypeSpiral(void) const;
    bool isThrowTypeRolling(void) const;
    bool isEnableHackThrowAutoCatch(void) const;
    bool isEnableCapTouchJumpInput(void) const;

    void exeLockOn(void);
    void exeHack(void);
    void exeSpinAttack(void);
    void exeCatch(void);
    void exeTrample(void);
    void exeTrampleLockOn(void);
    void exeRescue(void);
    void exeHide(void);
    void exeThrowStart(void);
    void exeThrow(void);
    void exeThrowBrake(void);
    void exeThrowSpiral(void);
    void exeThrowTornado(void);
    void exeThrowRolling(void);
    void exeThrowRollingBrake(void);
    void exeThrowStay(void);
    void exeThrowAppend(void);
    void exeRebound(void);
    void exeReturn(void);
    void exeBlow(void);

    // The cap a captured enemy wears. HackCap hides its own model on the first
    // frame of a hack and shows this second actor instead, posed from HackCap's
    // own transform (updateCapPose). HackCap::init builds it from the hardcoded
    // "MarioCap" archive, never from the worn costume - see CaptureHat.
    al::LiveActor* mEquipmentHat; // 0x108
    al::LiveActor* mEquipmentHatDepthShadow; // 0x110
    al::LiveActor* mLockOnEyes; // 0x118
    al::LiveActor* mCapEyes; // 0x120
    PlayerActorHakoniwa* mPlayerActor; // 0x128
    void* mPlayerCollision; // 0x130
    // Cap costume name ("Mario", "MarioSailor", ...) as rs::getInitCapTypeName
    // resolved it at player init; what HackCap::init runs through
    // PlayerFunction::createCapModelName to build its own cap model.
    const char* mTypeName; // 0x138
    unsigned char padding_220[0x220 - 0x140];
    HackCapThrowParam* throwParam; // 0x220
    unsigned char padding_2B8[0x2B8 - 0x228];
    PlayerCapActionHistory* mCapActionHistory; // 0x2B8
    unsigned char padding_2E0[0x2E0 - 0x2C0];
    HackCapJointControlKeeper* mJointKeeper; // 0x2E0
};
