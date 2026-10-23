#include "Randomizer/MovementAbilityLock.h"

#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/Randomizer.h"

#include "Library/Base/StringUtil.h"
#include "Library/LiveActor/ActorActionFunction.h"
#include "Library/Nerve/NerveStateBase.h"
#include "game/MapObj/CapMessageShowInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameSystem.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include <cstring>
#include <prim/seadSafeString.h>

class PlayerConst;
class PlayerCarryKeeper;
class PlayerRecoverySafetyPoint;
class SphinxRide;
class Motorcycle;
class RocketFlower;
class DokanPuppetController;
class DokanInfo;
class AppearSwitchTimer;

namespace al {
    class HitSensor;
    class SensorMsg;
} // namespace al

// No header in the project defines this, and the swim hooks need the player it
// belongs to. Every player state derives from al::ActorStateBase (see
// PlayerStateSquat, whose constructor has the same shape), so declaring the base
// lets the compiler find mActor rather than hardcoding an offset for it.
class PlayerStateSwim : public al::ActorStateBase { };

namespace MovementAbilityLock {
    namespace {

        // Cappy line ids. Neither exists in the game's CapMessage archive - like
        // MoonHooks' key-required labels they are answered by the shared
        // getSystemMessageString override, which routes through tryMakeCapMessage.
        constexpr const char* cCapMessageFile = "CapMessage";
        constexpr const char* cJaxiBlockedLabel = "RandoJaxiRideLocked";
        constexpr const char* cScooterBlockedLabel = "RandoScooterRideLocked";
        constexpr const char* cSwimBlockedLabel = "RandoSwimLocked";
        constexpr const char* cNpcTalkBlockedLabel = "RandoNpcTalkLocked";
        constexpr const char* cRocketFlowerBlockedLabel = "RandoRocketFlowerLocked";
        constexpr const char* cPipe2DBlockedLabel = "RandoPipe2DLocked";
        constexpr const char* cTimerChallengeBlockedLabel = "RandoTimerChallengeLocked";

        // A refusal is spoken once per time the player leans into the thing, not
        // once per frame they hold it there. The refusing code runs every such
        // frame, so "it has been this long since one" is what re-arms the line.
        constexpr s32 cRefusalReleaseFrames = 20;

        // How long one swimming frame keeps the recovery-area answer flipped. The
        // swimming nerve re-arms it every frame Mario is actually in the water, so
        // it only has to outlast the gap between that nerve running and the
        // player's control() consulting the recovery point. The budget also drains
        // through repeated queries within a frame, which is harmless: still
        // swimming means still re-armed.
        constexpr s32 cSwimBlockFrames = 6;

        // Frames after a stage load during which "Mario is swimming" is read as
        // "the arrival itself put him in the water" rather than "he jumped in".
        // The two want different answers: a mid-stage swim bubbles him back to his
        // safety point, while an arrival has no safety point in this stage worth
        // returning to and has to retreat back out of the door instead.
        constexpr s32 cArrivalWindowFrames = 60;

        // Time in the bubble before the retreat transition fires, so the player
        // sees why they are being sent back rather than being bounced instantly.
        constexpr s32 cRetreatBubbleFrames = 50;

        s32 sSwimBlockFrames = 0;
        s32 sFramesSinceStageEntry = 0x7FFFFFFF;

        const char* sPendingCapMessage = nullptr;

        // One per refusing lock: whether its line has been spoken for the current
        // ride/approach, and how long since the refusal last fired.
        struct RefusalLatch {
            bool spoken;
            s32 idleFrames;
        };
        RefusalLatch sJaxiRefusal = { false, cRefusalReleaseFrames };
        RefusalLatch sScooterRefusal = { false, cRefusalReleaseFrames };
        RefusalLatch sPipe2DRefusal = { false, cRefusalReleaseFrames };

        // Where a retreat sends the player: the stage they came from and the door
        // to come back out of, recorded by EntranceHooks as it routes the
        // transition. Empty means there is no known way back, in which case an
        // underwater arrival falls back to the ordinary safety-point bubble.
        sead::FixedSafeString<128> sRetreatStage;
        sead::FixedSafeString<128> sRetreatEntranceId;
        // The stage the recorded target was meant for. An arrival somewhere else
        // - a warp, a death restart, a demo relocation - must not reuse it, or the
        // retreat would send the player to a stage they never came from.
        sead::FixedSafeString<128> sRetreatForStage;
        bool sRetreatArmed = false;
        s32 sRetreatCountdown = 0;

        sead::FixedSafeString<128> sLastSeenStage;

        // Tri-state so the first look after a load records the answer instead of
        // announcing it: -1 unknown, 0 not yet earned, 1 earned. Re-seeded on every
        // stage entry, so only a change made during play is announced.
        s32 sMoonGravityEarnedState = -1;

        // Copied from MoonHooks: the player's save has to be readable from hooks
        // that are handed no actor and no scene - createMarioConst runs during
        // player init, before anything else in this file has seen a frame.
        GameDataHolder* tryGetGameDataHolder() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;
            // mSequence only owns a GameDataHolder when it really is the
            // HakoniwaSequence; the accessor sits at a fixed offset, so a blind
            // cast on any other sequence reads whatever happens to be there.
            if (!al::isEqualString(system->mSequence->mName.cstr(), "HakoniwaSequence"))
                return nullptr;
            return ((HakoniwaSequence*)system->mSequence)->mGameDataHolderAccessor.mData;
        }

        bool isAbilityOwned(AbilityRando::AbilityId ability) {
            GameDataHolder* holder = tryGetGameDataHolder();
            if (!holder) {
                // No readable save: fall back to what the settings alone say. This
                // keeps a lock from being applied to a menu/boot-time player that
                // has no progress to read, rather than guessing "locked".
                return AbilityRando::isMovementAbilityGranted(ability);
            }
            return AbilityRando::isAbilityUnlocked(GameDataHolderAccessor(holder), ability);
        }

        bool isSwimBlocked() { return !isAbilityOwned(AbilityRando::AbilityId_Swim); }

        // The whole of PlayerActorHakoniwa's own "fall into the abyss and recover"
        // entry point - the helper its control() tail-calls when it decides the
        // player is standing somewhere they cannot be. It cancels a bind, drops a
        // carried object, cancels equipment, calls PlayerStateAbyss::prepareRecovery
        // (which is what makes it a bubble-and-respawn rather than a death) and sets
        // the abyss nerve.
        //
        // Every argument is a player member, read straight off the vanilla call site
        // at 0x0042033c: LDR X1,[X19,#0x148] / LDP X2,X4,[X19,#0x1F8] /
        // LDR X3,[X24] where X24 = X19+0x1F0 / LDR X5,[X19,#0x3B8].
        //
        // This replaced an earlier attempt that lied to the recovery-area checks so
        // vanilla would start the same sequence itself. That never fired: the block
        // it feeds sits at the end of a long control() with several earlier exits,
        // so reaching it was never guaranteed. Calling the helper is.
        void startPlayerAbyssRecovery(al::LiveActor* player) {
            if (!player)
                return;
            u8* base = reinterpret_cast<u8*>(player);
            auto member = [base](ptrdiff_t offset) -> void* {
                return *reinterpret_cast<void**>(base + offset);
            };
            using Fn = void (*)(void*, void*, void*, void*, void*, void*);
            reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + 0x004273f4)(
                player, member(0x148), member(0x1F8), member(0x1F0), member(0x200),
                member(0x3B8));
        }

        // PlayerStateAbyss::appear picks between its Fall and Recovery nerves on
        // PlayerRecoverySafetyPoint::isValid. Outside Kids Mode that refuses unless
        // the player is inside an authored recovery area (or Cappy can rescue them),
        // so a blocked swim got the fall-into-the-void animation rather than the
        // bubble. Forcing it true for the moment the block fires is what selects the
        // assist-mode behaviour: PlayerStateRecoveryDead, which is startRecovery plus
        // the TractorBubbleStart animation and a respawn at the last safe point.
        s32 sForceSafetyPointValidFrames = 0;

        // Only worth forcing when the game actually has a point to send them back
        // to. Both offsets are read straight out of isValid's own disassembly - the
        // 3D slot and the 2D one - so they are exactly as reliable as the function
        // being hooked. With neither set, the override stands down and vanilla falls
        // as before rather than recovering to nowhere.
        bool hasRecordedSafetyPoint(const PlayerRecoverySafetyPoint* point) {
            if (!point)
                return false;
            const u8* raw = reinterpret_cast<const u8*>(point);
            return raw[64] != 0 || raw[112] != 0;
        }

        // rs::isPlayerSafetyPointRecovery - already bubbling back to safety.
        bool isPlayerRecovering(const al::LiveActor* player) {
            using Fn = bool (*)(const al::LiveActor*);
            return reinterpret_cast<Fn>(
                hk::ro::getMainModule()->range().start() + 0x00570dd4)(player);
        }

        // SphinxRide::tryGetOffAndCancelBind - true when the rider left. Not in
        // syms/main.sym, so it is reached through its main offset.
        bool sphinxTryGetOffAndCancelBind(SphinxRide* self) {
            using Fn = bool (*)(SphinxRide*);
            return reinterpret_cast<Fn>(
                hk::ro::getMainModule()->range().start() + 0x003d74ac)(self);
        }

        // Low gravity under a moon-count mode arrives without a moon of its own to
        // announce it, so watch the count and speak up on the moon that crosses the
        // line. The Ability Lock mode is deliberately excluded: there the ability is
        // a real reward and the normal unlock banner already covers it.
        void updateMoonGravityUnlockNotice(GameDataHolder* holder) {
            if (!holder || !Randomizer::isMoonGravityCountLocked()) {
                sMoonGravityEarnedState = -1;
                return;
            }

            const GameDataHolderAccessor accessor(holder);
            const s32 requirement = AbilityRando::getMoonGravityRequirement();
            const bool earned = requirement > 0
                && AbilityRando::getMoonGravityCollected(accessor) >= requirement;

            const s32 previous = sMoonGravityEarnedState;
            sMoonGravityEarnedState = earned ? 1 : 0;
            if (previous != 0 || !earned)
                return;

            const AbilityRando::AbilityEntry* entry
                = AbilityRando::getAbility(AbilityRando::AbilityId_MoonLowGravity);
            AbilityOverlay::queueUnlock(
                entry && entry->displayName ? entry->displayName : "Moon Kingdom Low Gravity");
        }

        // Call every frame the lock refuses. Speaks only on the first such frame of a
        // ride/approach; backing off and coming again says it once more.
        void noteRefused(RefusalLatch& refusal, const char* label) {
            refusal.idleFrames = 0;
            if (refusal.spoken)
                return;
            refusal.spoken = true;
            sPendingCapMessage = label;
        }

        void ageRefusal(RefusalLatch& refusal) {
            if (refusal.idleFrames >= cRefusalReleaseFrames) {
                refusal.spoken = false;
                return;
            }
            refusal.idleFrames++;
        }

        // An arrival that landed the player in deep water wants the retreat, not
        // the bubble - the stage they just entered has no safety point worth
        // returning to, and a door they can be put back through does.
        bool isSwimRetreatPending() {
            return sRetreatArmed
                || (sFramesSinceStageEntry < cArrivalWindowFrames && !sRetreatStage.isEmpty());
        }

        // ── Moon Kingdom low gravity ─────────────────────────────────────────
        //
        // PlayerActorHakoniwa::init reads the "IsMoon" placement arg and asks for
        // the "Moon" PlayerConst; that const is the whole of Moon Kingdom's reduced
        // gravity, along with the jump and air tuning that goes with it. Asking for
        // the ordinary const instead is the entire lock, and it is confined to Moon
        // Kingdom because nothing else is placed with IsMoon.
        //
        // The const is built once during player init and its pointer is then cached
        // by the collider, the joint keeper and every player state, so it cannot be
        // swapped while a stage is live. Earning low gravity while already standing
        // in Moon Kingdom therefore takes effect on the next load of the stage - a
        // sub-area, a death, or re-entering the kingdom - rather than instantly.
        HkTrampoline<PlayerConst*, const char*> createMarioConstHook
            = hk::hook::trampoline([](const char* name) -> PlayerConst* {
                  if (name && al::isEqualString(name, "Moon")
                      && !isAbilityOwned(AbilityRando::AbilityId_MoonLowGravity))
                      name = "";
                  return createMarioConstHook.orig(name);
              });

        // ── Swim ─────────────────────────────────────────────────────────────
        //
        // Rather than driving the abyss nerve by hand, this re-uses the vanilla
        // poison/deadly-liquid path wholesale: PlayerActorHakoniwa's control()
        // bubbles Mario back to his safety point when his recovery point reports an
        // active recovery area whose safe part he is NOT standing in. Answering
        // "active" and "not in the safe part" while he is swimming reproduces that
        // exactly, and cancelling binds, dropping a carried object, the bubble, the
        // camera watcher and the respawn all come free with it.
        //
        // It also degrades safely: with no valid safety point recorded, vanilla
        // declines and Mario simply swims, rather than dying or being stranded.
        // Blocked swimming hands the player straight to the vanilla abyss recovery:
        // the bubble, the dropped carry, the cancelled bind and the respawn at the
        // last safe point all come from the game's own code.
        //
        // Returns true when the nerve has been taken over and must not run.
        bool tryBlockSwim(PlayerStateSwim* self) {
            if (!isSwimBlocked())
                return false;
            sSwimBlockFrames = cSwimBlockFrames;
            // An arrival that landed in deep water is the retreat's business - it has
            // a door to put the player back through, which beats this stage's safety
            // point. Let him swim for the moment it takes to fire.
            if (isSwimRetreatPending())
                return false;
            al::LiveActor* player = self ? self->mActor : nullptr;
            if (!player || isPlayerRecovering(player))
                return false;
            // Two frames is all appear() needs, and keeping the window short keeps
            // the override away from anything else that asks.
            sForceSafetyPointValidFrames = 2;
            startPlayerAbyssRecovery(player);
            // Once per bubble rather than once per frame: the swim nerve stops
            // running the moment the abyss nerve is set, so this is reached once
            // per time Mario goes in. update() keeps offering it to Cappy until it
            // takes, which lands it as he surfaces rather than mid-recovery.
            sPendingCapMessage = cSwimBlockedLabel;
            return true;
        }

        // The nerves that mean Mario is in the water off his feet. The first four are
        // the entry ones - falling in, going under, diving in - so the block fires the
        // moment he is committed rather than a nerve later.
        //
        // Deliberately not exeSwimWalk or exeSwimLand: wading with your feet on the
        // ground, and climbing back out, are not swimming. That is what "not standing
        // on ground but fully in the water" means here.
        HkTrampoline<void, PlayerStateSwim*> swimFallInHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimFallInHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimHeadInHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimHeadInHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimDiveInHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimDiveInHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimDiveHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimDiveHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimSurfaceHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimSurfaceHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimMoveHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimMoveHook.orig(self);
              });

        HkTrampoline<void, PlayerStateSwim*> swimPaddleHook
            = hk::hook::trampoline([](PlayerStateSwim* self) -> void {
                  if (tryBlockSwim(self))
                      return;
                  swimPaddleHook.orig(self);
              });

        HkTrampoline<bool, const PlayerRecoverySafetyPoint*> safetyPointIsValidHook
            = hk::hook::trampoline([](const PlayerRecoverySafetyPoint* self) -> bool {
                  if (sForceSafetyPointValidFrames > 0 && hasRecordedSafetyPoint(self))
                      return true;
                  return safetyPointIsValidHook.orig(self);
              });

        // ── Grab ─────────────────────────────────────────────────────────────
        //
        // One chokepoint covers every pick-up - seeds, nuts, bags, bombs, vegetables
        // - because they all go through PlayerCarryKeeper::startCarry. Refusing it
        // leaves the object entirely untouched, so nothing downstream has a
        // half-carried state to unwind.
        HkTrampoline<bool, PlayerCarryKeeper*, al::HitSensor*> startCarryHook
            = hk::hook::trampoline([](PlayerCarryKeeper* self, al::HitSensor* held) -> bool {
                  if (!isAbilityOwned(AbilityRando::AbilityId_Grab))
                      return false;
                  return startCarryHook.orig(self, held);
              });

        // ── Jaxi ─────────────────────────────────────────────────────────────
        //
        // Mario mounts as usual; what he does not get is the ride. exeRun is Jaxi's
        // running nerve and does three things in order: offer a dismount, start the
        // Run/RunSlow action, then call updateRun to actually move. Taking the nerve
        // over and doing only the first parks Jaxi in its idle pose under a rider
        // who can still get off.
        //
        // Skipping updateRun alone was not enough: nothing else leaves the Run nerve,
        // so the Run action kept playing and Jaxi ran on the spot.
        HkTrampoline<void, SphinxRide*> sphinxRunHook
            = hk::hook::trampoline([](SphinxRide* self) -> void {
                  if (isAbilityOwned(AbilityRando::AbilityId_JaxiRide)) {
                      sphinxRunHook.orig(self);
                      return;
                  }
                  noteRefused(sJaxiRefusal, cJaxiBlockedLabel);
                  // Getting off has to keep working, and the running nerve is the
                  // only place that offers it.
                  if (sphinxTryGetOffAndCancelBind(self))
                      return;
                  // Jaxi's own idle action, the one exeWait plays. tryStart rather
                  // than start so it is not restarted from frame zero every frame.
                  al::tryStartActionIfNotPlaying(
                      reinterpret_cast<al::LiveActor*>(self), "Wait");
              });

        // ── Motor Scooter ────────────────────────────────────────────────────
        //
        // Same shape, reached differently. exeRideWait is the scooter's
        // idle-while-ridden nerve and is the one that carries dismount, gravity and
        // velocity damping, so running its body in place of the three nerves that
        // drive, jump and turn leaves the player parked on a scooter they can still
        // step off. Declared before its users so they can call through to it.
        HkTrampoline<void, Motorcycle*> motorcycleRideWaitHook = hk::hook::trampoline(
            [](Motorcycle* self) -> void { motorcycleRideWaitHook.orig(self); });

        bool isScooterBlocked() { return !isAbilityOwned(AbilityRando::AbilityId_ScooterRide); }

        HkTrampoline<void, Motorcycle*> motorcycleRideRunStartHook
            = hk::hook::trampoline([](Motorcycle* self) -> void {
                  if (isScooterBlocked()) {
                      noteRefused(sScooterRefusal, cScooterBlockedLabel);
                      motorcycleRideWaitHook.orig(self);
                      return;
                  }
                  motorcycleRideRunStartHook.orig(self);
              });

        HkTrampoline<void, Motorcycle*> motorcycleRideRunHook
            = hk::hook::trampoline([](Motorcycle* self) -> void {
                  if (isScooterBlocked()) {
                      motorcycleRideWaitHook.orig(self);
                      return;
                  }
                  motorcycleRideRunHook.orig(self);
              });

        HkTrampoline<void, Motorcycle*> motorcycleRideWaitJumpHook
            = hk::hook::trampoline([](Motorcycle* self) -> void {
                  if (isScooterBlocked()) {
                      noteRefused(sScooterRefusal, cScooterBlockedLabel);
                      motorcycleRideWaitHook.orig(self);
                      return;
                  }
                  motorcycleRideWaitJumpHook.orig(self);
              });

        // ── Talking to NPCs ──────────────────────────────────────────────────
        //
        // rs::isPlayerInputTriggerStartTalk is the press that turns a balloon prompt
        // into a conversation, and the balloon node is its only caller - so refusing
        // it leaves the "Talk" prompt on screen and takes nothing else with it. The
        // node's else branch simply re-requests the balloon, which is exactly the
        // asked-for behaviour: the button is there, it just does not open the dialog.
        //
        // Deliberately not rs::tryLockStartEventTalkDemo, which sits beside it in the
        // same condition but is also what the DemoForceStart nodes use - hooking that
        // would block cutscenes too.
        HkTrampoline<bool, const al::LiveActor*> npcTalkStartHook
            = hk::hook::trampoline([](const al::LiveActor* player) -> bool {
                  if (!npcTalkStartHook.orig(player))
                      return false;
                  if (isAbilityOwned(AbilityRando::AbilityId_NpcTalk))
                      return true;
                  // Every press is a deliberate act, so it answers each time rather
                  // than latching like the vehicles do.
                  sPendingCapMessage = cNpcTalkBlockedLabel;
                  return false;
              });

        // ── Rocket Flower ────────────────────────────────────────────────────
        //
        // RocketFlowerFunction::requestEquipRocketFlower is the one funnel from
        // "Mario touched the flower" to RocketFlowerEquipWatcher::equip, and
        // RocketFlower::receiveMsg already has a branch for it answering no: the
        // flower drops its Equip sensor, replays its Appear action and goes back to
        // waiting. So refusing it leaves a flower that is still there and still works
        // the moment the ability is unlocked, with no state to unwind.
        //
        // NOT RocketFlower::isEnableEquip, which is only a nerve-state predicate -
        // receiveMsg inlines the same test rather than calling it, so hooking it
        // changed nothing and Mario still got the dash.
        HkTrampoline<bool, RocketFlower*, al::HitSensor*> rocketFlowerEquipRequestHook
            = hk::hook::trampoline([](RocketFlower* flower, al::HitSensor* sensor) -> bool {
                  if (!isAbilityOwned(AbilityRando::AbilityId_RocketFlower)) {
                      // This is the moment the flower has flown back and reached
                      // Mario, so it is the moment worth commenting on. A refusal
                      // drops the Equip sensor for 30 frames, so touching it again
                      // is a fresh deliberate act - no latch needed.
                      sPendingCapMessage = cRocketFlowerBlockedLabel;
                      return false;
                  }
                  return rocketFlowerEquipRequestHook.orig(flower, sensor);
              });

        // ── 2D pipes ─────────────────────────────────────────────────────────
        //
        // The pipe that flattens Mario into a 2D section is an ordinary "Dokan"
        // placement whose DstDokan link points at a "Dokan2D": Dokan::init spots
        // that pair and builds the actor from the DokanChange2D archive instead of
        // the placement's own model, and DokanKoopa (Bowser's Kingdom) is built
        // from the same archive unconditionally. So the archive name is exactly
        // "this pipe changes dimension" and nothing else in the game carries it -
        // an ordinary warp pipe keeps its Dokan model, and the pipes on the 2D side
        // keep their Dokan2D one.
        //
        // Only the way in is withheld. Coming back out is a Dokan2D pipe, which
        // never matches here, so a player who is already flat is never stranded.
        const char* getActorModelName(const al::LiveActor* actor) {
            if (!actor)
                return nullptr;
            // al::getModelName is LDR X8,[X0,#0x88] / LDR X0,[X8,#8] and does not
            // check the model keeper it reads through. Every Dokan has one, but
            // these hooks run on every sensor touch, so the pointer is checked here
            // rather than trusted.
            if (!*reinterpret_cast<void* const*>(reinterpret_cast<const u8*>(actor) + 0x88))
                return nullptr;
            // Not in syms/main.sym, so it goes through its main offset like the
            // other unlisted helpers in this file.
            using Fn = const char* (*)(const al::LiveActor*);
            return reinterpret_cast<Fn>(
                hk::ro::getMainModule()->range().start() + 0x008e2000)(actor);
        }

        bool isChange2DDokan(const al::LiveActor* dokan) {
            const char* model = getActorModelName(dokan);
            // isStartWithString rather than isEqualString: DokanKoopa asks for the
            // archive with a "Koopa" suffix, which some model names carry through.
            return model && al::isStartWithString(model, "DokanChange2D");
        }

        // DokanPuppetController::judgeMsgBindStart is the pipe's own "do I take this
        // player" answer - the one Dokan::receiveMsg gives to MsgBindStart, shared
        // with DokanMaze and DokanStageChange. Answering no is what an exit-only or
        // mini-game pipe already does, so Mario simply stays on his feet with no
        // bind to unwind.
        //
        // The vanilla answer is asked for first and only overturned when it was yes:
        // it is a pure predicate, and letting it rule out every near miss (wrong
        // direction, too slow, already 2D) is what keeps the refusal line for the
        // moment the pipe really would have swallowed him.
        HkTrampoline<bool, DokanPuppetController*, al::HitSensor*, al::HitSensor*,
            const al::LiveActor*, const DokanInfo*>
            dokanJudgeBindStartHook = hk::hook::trampoline(
                [](DokanPuppetController* self, al::HitSensor* player, al::HitSensor* pipe,
                    const al::LiveActor* dokan, const DokanInfo* info) -> bool {
                    if (!dokanJudgeBindStartHook.orig(self, player, pipe, dokan, info))
                        return false;
                    if (isAbilityOwned(AbilityRando::AbilityId_Pipe2D)
                        || !isChange2DDokan(dokan))
                        return true;
                    // Leaning on the pipe asks again every frame, so the line is
                    // latched like the vehicles rather than spoken per frame.
                    noteRefused(sPipe2DRefusal, cPipe2DBlockedLabel);
                    return false;
                });

        // The other way in, and the only one that does not ask the judge: a captured
        // Mario running or ground-pounding into a pipe, which Dokan::attackSensor
        // answers by cancelling the capture and binding the player itself. Its whole
        // body is that one sequence, so skipping it for a withheld 2D pipe leaves the
        // pipe inert to a capture exactly as it is to Mario.
        //
        // Silent on purpose: attackSensor runs on sensor overlap rather than on
        // intent, so speaking here would talk at anyone standing next to the pipe.
        // The judge above still answers the moment they try it on foot.
        HkTrampoline<void, al::LiveActor*, al::HitSensor*, al::HitSensor*> dokanAttackSensorHook
            = hk::hook::trampoline([](al::LiveActor* dokan, al::HitSensor* ownSensor,
                                       al::HitSensor* otherSensor) -> void {
                  if (!isAbilityOwned(AbilityRando::AbilityId_Pipe2D)
                      && isChange2DDokan(dokan))
                      return;
                  dokanAttackSensorHook.orig(dokan, ownSensor, otherSensor);
              });

        // ── Timer challenges ─────────────────────────────────────────────────
        //
        // P-Switches (TrampleSwitchTimer, CapSwitchTimer, HipDropSwitchTimer) and
        // scarecrows (CapRack) do not run timers of their own: each one owns an
        // AppearSwitchTimer and starts it through onSwitch. That timer is therefore
        // the single place that covers every timed challenge in the game, and
        // exeOnWait is the nerve that counts it down.
        //
        // Ending it is offSwitch - the game's own "time is up", and the same call
        // exeOnWait already makes itself when the player leaves the challenge's
        // area. So everything unwinds exactly as a natural expiry: the appeared
        // actors are killed, the appear demo's camera is ended, the stage switches
        // (including CapRackKeepOn) go back off, the timer BGM stops and the switch
        // can be pressed again. It also leaves the OffWait nerve set, which is why
        // this fires once per press rather than once per frame.
        //
        // Deliberately not a refusal at onSwitch: that would leave the switch actor
        // pressed with nothing counting, and nothing would put it back.
        void appearSwitchTimerOffSwitch(AppearSwitchTimer* self) {
            using Fn = void (*)(AppearSwitchTimer*);
            reinterpret_cast<Fn>(
                hk::ro::getMainModule()->range().start() + 0x00217314)(self);
        }

        // The same timer also serves switches placed with no time limit at all,
        // which stay on until the player leaves the challenge's area. Those are not
        // timed challenges and are left alone. -1 is how vanilla spells "no limit":
        // both exeOnWait and offSwitch test this field before touching the timer SE
        // and BGM, and the offset is theirs (LDR W8,[X19,#0x68]).
        bool hasTimeLimit(const AppearSwitchTimer* self) {
            return self
                && *reinterpret_cast<const s32*>(
                       reinterpret_cast<const u8*>(self) + 0x68)
                != -1;
        }

        HkTrampoline<void, AppearSwitchTimer*> appearSwitchTimerOnWaitHook
            = hk::hook::trampoline([](AppearSwitchTimer* self) -> void {
                  if (isAbilityOwned(AbilityRando::AbilityId_TimerChallenge)
                      || !hasTimeLimit(self)) {
                      appearSwitchTimerOnWaitHook.orig(self);
                      return;
                  }
                  sPendingCapMessage = cTimerChallengeBlockedLabel;
                  appearSwitchTimerOffSwitch(self);
              });

        // The trample switch is the one starter a player can lean on: its press is
        // al::isMsgPlayerTouch, which arrives every frame Mario stands on it. Ending
        // the challenge on its first counting frame puts the switch back in OffWait
        // a moment later with his foot still on it, so it pressed again, and again -
        // the thing thumped up and down in a loop. The other three starters are
        // single deliberate acts (a cap throw, a ground pound, Cappy landing on a
        // rack) and cannot repeat like that.
        //
        // So the first touch of an approach is let through - that is the one that
        // runs the vanilla press, the instant expiry and Cappy's line - and every
        // touch after it is dropped until Mario has been off the switch for
        // cTrampleReleaseFrames. Answering false to a touch is what the switch
        // itself does whenever it is not in a state to accept one, so nothing
        // downstream can tell the difference.
        constexpr s32 cTrampleReleaseFrames = 30;

        // Compared, never dereferenced, and dropped on stage entry: the switch it
        // names is only ever "the one being stood on right now".
        const al::LiveActor* sHeldTrampleSwitch = nullptr;
        s32 sHeldTrampleSwitchIdleFrames = 0;

        // al::isMsgPlayerTouch, another one missing from syms/main.sym.
        bool isPlayerTouchMsg(const al::SensorMsg* msg) {
            using Fn = bool (*)(const al::SensorMsg*);
            return reinterpret_cast<Fn>(
                hk::ro::getMainModule()->range().start() + 0x008f88c4)(msg);
        }

        HkTrampoline<bool, al::LiveActor*, const al::SensorMsg*, al::HitSensor*, al::HitSensor*>
            trampleSwitchTimerReceiveMsgHook = hk::hook::trampoline(
                [](al::LiveActor* self, const al::SensorMsg* msg, al::HitSensor* otherSensor,
                    al::HitSensor* ownSensor) -> bool {
                    if (isAbilityOwned(AbilityRando::AbilityId_TimerChallenge)
                        || !isPlayerTouchMsg(msg))
                        return trampleSwitchTimerReceiveMsgHook.orig(
                            self, msg, otherSensor, ownSensor);

                    const bool stillStandingOnIt = sHeldTrampleSwitch == self;
                    sHeldTrampleSwitch = self;
                    sHeldTrampleSwitchIdleFrames = 0;
                    if (stillStandingOnIt)
                        return false;
                    return trampleSwitchTimerReceiveMsgHook.orig(
                        self, msg, otherSensor, ownSensor);
                });

    } // namespace

    void noteRetreatTarget(const char* stage, const char* entranceId, const char* destStage) {
        if (!stage || !stage[0] || !entranceId || !entranceId[0] || !destStage
            || !destStage[0]) {
            sRetreatStage.clear();
            sRetreatEntranceId.clear();
            sRetreatForStage.clear();
            return;
        }
        sRetreatStage.copy(stage);
        sRetreatEntranceId.copy(entranceId);
        sRetreatForStage.copy(destStage);
    }

    void notifyStageEntered() {
        sFramesSinceStageEntry = 0;
        sSwimBlockFrames = 0;
        sRetreatArmed = false;
        sRetreatCountdown = 0;
        // The switch it named belongs to the stage being left, and a new one can be
        // built at the same address.
        sHeldTrampleSwitch = nullptr;
        sHeldTrampleSwitchIdleFrames = 0;
    }

    bool isUnderwaterRetreatActive() { return sRetreatArmed; }

    const char16_t* tryMakeCapMessage(const char* fileName, const char* key) {
        if (!fileName || !key || std::strcmp(fileName, cCapMessageFile) != 0)
            return nullptr;
        if (std::strcmp(key, cJaxiBlockedLabel) == 0)
            return u"We need to learn how\nto ride Jaxi first";
        if (std::strcmp(key, cScooterBlockedLabel) == 0)
            return u"We need our driver's license\nfirst!";
        if (std::strcmp(key, cSwimBlockedLabel) == 0)
            return u"Water is basically just blue lava\nuntil we find the right upgrade.";
        if (std::strcmp(key, cNpcTalkBlockedLabel) == 0)
            return u"We don't know how to talk\nto anyone yet!";
        if (std::strcmp(key, cRocketFlowerBlockedLabel) == 0)
            return u"You're not ready to use\na Rocket Flower yet";
        if (std::strcmp(key, cPipe2DBlockedLabel) == 0)
            return u"We can't get flat until\nwe find the right ability";
        if (std::strcmp(key, cTimerChallengeBlockedLabel) == 0)
            return u"We need to find something\nto slow down time for this";
        return nullptr;
    }

    void update(const al::IUseSceneObjHolder* holder) {
        GameDataHolder* holderData = tryGetGameDataHolder();
        GameDataFile* file = holderData ? holderData->getGameDataFile() : nullptr;

        // Stage entry is detected here rather than from a transition hook: the
        // current stage name only changes once the load has actually happened,
        // which is exactly when the arrival window should start.
        const char* stage = file ? file->getStageNameCurrent() : nullptr;
        if (stage && !al::isEqualString(stage, sLastSeenStage.cstr())) {
            sLastSeenStage.copy(stage);
            notifyStageEntered();
            // Re-seeded rather than kept: a load, a death or any other arrival must
            // not replay a banner for something already owned.
            sMoonGravityEarnedState = -1;
            // Only a routed arrival at the stage the target was recorded for can
            // use it; anything else drops back to the ordinary bubble.
            if (!al::isEqualString(stage, sRetreatForStage.cstr())) {
                sRetreatStage.clear();
                sRetreatEntranceId.clear();
                sRetreatForStage.clear();
            }
        }

        updateMoonGravityUnlockNotice(holderData);

        if (sFramesSinceStageEntry < 0x7FFFFFFF)
            sFramesSinceStageEntry++;
        // One tick per gameplay frame. The swimming nerve re-arms it every frame
        // Mario is actually in the water, so it only decays once he is out.
        if (sSwimBlockFrames > 0)
            sSwimBlockFrames--;
        if (sForceSafetyPointValidFrames > 0)
            sForceSafetyPointValidFrames--;
        ageRefusal(sJaxiRefusal);
        ageRefusal(sScooterRefusal);
        ageRefusal(sPipe2DRefusal);
        // Touches stop arriving the moment Mario steps off, so this running out is
        // exactly "he is no longer standing on it".
        if (sHeldTrampleSwitch && ++sHeldTrampleSwitchIdleFrames >= cTrampleReleaseFrames)
            sHeldTrampleSwitch = nullptr;

        if (sPendingCapMessage && holder) {
            if (rs::tryShowCapMessage(holder, sPendingCapMessage, 90, 0))
                sPendingCapMessage = nullptr;
        }

        // An arrival that dropped the player straight into deep water: let the
        // bubble read for a moment, then send them back out of the door they came
        // in through. With no recorded way back there is nothing to do here, and
        // the ordinary safety-point bubble has already handled it.
        if (!sRetreatArmed && sSwimBlockFrames > 0 && !sRetreatStage.isEmpty()
            && sFramesSinceStageEntry < cArrivalWindowFrames) {
            sRetreatArmed = true;
            sRetreatCountdown = cRetreatBubbleFrames;
        }

        if (sRetreatArmed && sRetreatCountdown > 0 && --sRetreatCountdown == 0) {
            if (file && holderData) {
                // Unrouted on purpose: the stage and door below were computed BY
                // the entrance routing when the player came through, so letting it
                // shuffle them a second time would send them somewhere new instead
                // of back. Same reason the Subarea Key denial finishes through
                // changeNextStageHook.orig rather than the routing path.
                EntranceHooks::bypassNextTransition();
                ChangeStageInfo info(holderData, sRetreatEntranceId.cstr(), sRetreatStage.cstr(),
                    true, -1, ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
                file->changeNextStage(&info, 0);
            }
            sRetreatArmed = false;
            sRetreatStage.clear();
            sRetreatEntranceId.clear();
            sRetreatForStage.clear();
        }
    }

    void installHooks() {
        createMarioConstHook.installAtSym<"_ZN14PlayerFunction16createMarioConstEPKc">();

        // None of the offsets below are present in syms/main.sym.
        swimFallInHook.installAtMainOffset(0x00487a8c); // PlayerStateSwim::exeSwimFallIn
        swimHeadInHook.installAtMainOffset(0x00487e3c); // PlayerStateSwim::exeSwimHeadIn
        swimDiveInHook.installAtMainOffset(0x00488548); // PlayerStateSwim::exeSwimDiveIn
        swimDiveHook.installAtMainOffset(0x00487810); // PlayerStateSwim::exeSwimDive
        swimSurfaceHook.installAtMainOffset(0x0048597c); // PlayerStateSwim::exeSwimSurface
        swimMoveHook.installAtMainOffset(0x00486510); // PlayerStateSwim::exeSwimMove
        swimPaddleHook.installAtMainOffset(0x00486be4); // PlayerStateSwim::exeSwimPaddle
        startCarryHook.installAtMainOffset(0x0042e81c); // PlayerCarryKeeper::startCarry
        // PlayerRecoverySafetyPoint::isValid
        safetyPointIsValidHook.installAtMainOffset(0x00460c90);

        sphinxRunHook.installAtMainOffset(0x003d6ef0); // SphinxRide::exeRun

        motorcycleRideWaitHook.installAtMainOffset(0x002ca608); // Motorcycle::exeRideWait
        motorcycleRideRunStartHook.installAtMainOffset(0x002cbbb4); // Motorcycle::exeRideRunStart
        motorcycleRideRunHook.installAtMainOffset(0x002cc86c); // Motorcycle::exeRideRun
        motorcycleRideWaitJumpHook.installAtMainOffset(0x002cac98); // Motorcycle::exeRideWaitJump

        // rs::isPlayerInputTriggerStartTalk
        npcTalkStartHook.installAtMainOffset(0x005723c4);

        // RocketFlowerFunction::requestEquipRocketFlower
        rocketFlowerEquipRequestHook.installAtMainOffset(0x002ff210);

        // DokanPuppetController::judgeMsgBindStart
        dokanJudgeBindStartHook.installAtMainOffset(0x0025b2f0);
        dokanAttackSensorHook.installAtMainOffset(0x002576ec); // Dokan::attackSensor

        // AppearSwitchTimer::exeOnWait
        appearSwitchTimerOnWaitHook.installAtMainOffset(0x002178a0);
        // TrampleSwitchTimer::receiveMsg
        trampleSwitchTimerReceiveMsgHook.installAtMainOffset(0x0033c3c4);
    }

} // namespace MovementAbilityLock
