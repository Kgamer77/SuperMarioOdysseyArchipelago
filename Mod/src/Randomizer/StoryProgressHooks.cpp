#include "Randomizer/StoryProgressHooks.h"

#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "al/Library/Camera/CameraPoser.h"
#include "al/Library/Camera/CameraPoserSceneInfo.h"
#include "al/Library/Camera/CameraTargetBase.h"
#include "al/Library/Camera/CameraTargetHolder.h"
#include "al/Library/Camera/CameraViewInfo.h"
#include "al/Library/LiveActor/ActorSceneInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Math/MathUtil.h"
#include "al/Library/Player/PlayerHolder.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"
#include "game/Scene/QuestInfo.h"
#include "game/Scene/QuestInfoHolder.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/Util/StageInputFunction.h"
#include "hk/hook/Trampoline.h"

#include <cstdlib>
#include <cstring>

// ----------------------------------------------------------------------------
// Shiveria progress doors (DoorSnow)
//
// The four "doors" in Shiveria are a single DoorSnow actor (obj643 in
// SnowWorldTownStage) that progressively opens, firing OutputSwitch1On..4On to
// reveal the Shiverians. Its open level is NOT a scenario value and NOT stored
// as a save flag: DoorSnow holds no SaveObjInfo. The level is derived from how
// many of the four Snow "Barrier" story moons have been collected -
// ShineFlag_Story without ShineFlag_Multi, in SnowWorldTownStage (uids 17/18/22/25:
// "The Icicle Barrier", "The Gusty Barrier", "The Ice Wall Barrier",
// "The Snowy Mountain Barrier"). Collecting them does not advance the kingdom
// scenario (the Bound Bowl Grand Prix multimoon does that), so this is genuinely
// sub-progress within a scenario.
//
// The actor exposes two paths:
//   reset(count)     - set the open state to `count` silently (no camera)
//   startDemo(count) - animate the open with a camera (DoorSnow::exeOpen ->
//                      al::startCamera, only al::endCamera once the open anim
//                      reaches its end branch)
//
// Two randomizer bugs stem from this:
//   * Wrong door count - the game's open-door event reads the original barrier
//     moons' flags, but the randomizer credits collection to the *placed* item,
//     so the count it restores on load is wrong on an out-of-order visit.
//   * Camera-freeze softlock - collecting a barrier moon (or its randomized
//     replacement) fires startDemo's camera in a state where the open animation
//     never completes, so the camera is started and never ended.
//
// Fix: derive the true door level physically through the placement map (exactly
// how ScenarioPatcher derives the kingdom scenario) and
//   * override reset's count with it (correct level on every load), and
//   * route startDemo through reset (correct level, applied silently with no
//     camera - kills the freeze).
// DoorSnow only exists in SnowWorldTownStage, so these hooks are inherently
// Snow-scoped; they are additionally gated on moon rando being enabled so
// vanilla behaviour is untouched when it is off.
// ----------------------------------------------------------------------------

namespace StoryProgressHooks {
    namespace {

        constexpr const char* cSnowTownStage = "SnowWorldTownStage";
        constexpr s32 cMaxMainScenarioCount = 32;
        // This runs in the moon-credit path; the detailed healing trace writes
        // synchronously through OutputDebugString on emulator builds.
        constexpr bool cLogScenarioHealing = false;
        constexpr uintptr_t cScenarioCameraSceneOffset = 0x18;
        constexpr uintptr_t cInformationWindowSeparateRequesterOffset = 0x40;
        constexpr uintptr_t cInformationWindowRequesterPlayerHolderOffset = 0x18;
        s32 sScenarioCameraTryStartDepth = 0;

        al::CameraTargetBase* tryGetCameraViewTarget(const al::CameraPoser* poser) {
            if (!poser || !poser->mSceneInfo || !poser->mViewInfo)
                return nullptr;

            al::CameraTargetHolder* holder = poser->mSceneInfo->targetHolder;
            const s32 viewIndex = poser->mViewInfo->getIndex();
            if (!holder || !holder->mViewTargetArray
                || viewIndex < 0 || viewIndex >= holder->mViewTargetSize) {
                return nullptr;
            }

            al::CameraTargetBase* target = holder->mViewTargetArray[viewIndex];
            if (!target && !holder->mTargetArray.isEmpty())
                target = holder->mTargetArray.unsafeAt(0);
            return target;
        }

        const al::Scene* getScenarioCameraScene(
            const StageSceneStateScenarioCamera* state) {
            if (!state)
                return nullptr;

            // StageSceneStateScenarioCamera derives from NerveStateBase and stores
            // its host scene at +0x18 (see the retail constructor at 0x4eee08).
            const auto* bytes = reinterpret_cast<const u8*>(state);
            return *reinterpret_cast<al::Scene* const*>(
                bytes + cScenarioCameraSceneOffset);
        }

        bool isScenarioCameraPlayerReady(
            const StageSceneStateScenarioCamera* state) {
            const al::Scene* scene = getScenarioCameraScene(state);
            al::PlayerHolder* scenePlayerHolder = scene ? al::getScenePlayerHolder(scene) : nullptr;
            al::LiveActor* player = scenePlayerHolder
                ? al::tryGetPlayerActor(scenePlayerHolder, 0)
                : nullptr;

            // StageSceneStateScenarioCamera::tryStart passes this actor to
            // keepMarioCapVisibilityEndDemo, which performs the same lookup with
            // the asserting getPlayerActor overload. Check it non-destructively
            // first so an early scenario-camera request can be retried later.
            al::PlayerHolder* actorPlayerHolder = player && player->mSceneInfo
                ? player->mSceneInfo->playerHolder
                : nullptr;
            return actorPlayerHolder
                && al::tryGetPlayerActor(actorPlayerHolder, 0);
        }

        bool isInformationWindowPlayerReady(const void* director) {
            if (!director)
                return false;

            const auto* directorBytes = reinterpret_cast<const u8*>(director);
            const void* requester = *reinterpret_cast<void* const*>(
                directorBytes + cInformationWindowSeparateRequesterOffset);
            if (!requester)
                return false;

            const auto* requesterBytes = reinterpret_cast<const u8*>(requester);
            const al::PlayerHolder* holder = *reinterpret_cast<const al::PlayerHolder* const*>(
                requesterBytes + cInformationWindowRequesterPlayerHolderOffset);
            return holder && al::tryGetPlayerActor(holder, 0);
        }

        s32 toLogicalWorldId(s32 gameWorldId) {
            return gameWorldId;
        }

        s32 toGameWorldId(s32 logicalWorldId) {
            // MainScenarioNo and HintInfo use the native WorldList ID. The
            // scenarioWorldId field is skybox/scenario metadata and swaps the
            // Lake/Wooded and Snow/Seaside pairs.
            return logicalWorldId;
        }

        bool isSameHintWorld(s32 hintWorldId, s32 logicalWorldId) {
            return hintWorldId == logicalWorldId;
        }

        const ShineDB::ShineEntry* findStoryLocation(const GameDataFile::HintInfo& hint) {
            if (hint.stageName.isEmpty() || hint.objId.isEmpty() || hint.mainScenarioNo < 0)
                return nullptr;

            const char* objId = hint.objId.cstr();
            if (std::strncmp(objId, "obj", 3) != 0)
                return nullptr;

            char* end = nullptr;
            const long objNum = std::strtol(objId + 3, &end, 10);
            if (!end || *end != '\0' || objNum < 0)
                return nullptr;

            const ShineDB::ShineEntry* location = ShineDB::findEntry(
                ShineDB::findUid(hint.stageName.cstr(), static_cast<s32>(objNum)),
                hint.stageName.cstr());
            return location && (location->flags & ShineDB::ShineFlag_Story)
                ? location
                : nullptr;
        }

        bool isPhysicalLocationCollected(GameDataFile* file, const ShineDB::ShineEntry& location) {
            const ShineDB::ShineEntry* placed = MoonRando::getItemForLocation(location.uid, location.stage);
            return placed
                ? file->customIsGotShine(placed->uid, placed->stage)
                : file->customIsGotShine(location.uid, location.stage);
        }

        bool isSnowBarrierMoon(const ShineDB::ShineEntry& e) {
            return e.worldId == WorldMeta::WorldId_Snow
                && (e.flags & ShineDB::ShineFlag_Story)
                && !(e.flags & ShineDB::ShineFlag_Multi)
                && e.stage && std::strcmp(e.stage, cSnowTownStage) == 0;
        }

        // Number of Snow barrier locations collected, read through the placement
        // map so it is correct regardless of where the items were shuffled.
        // Returns -1 when the file is unavailable (caller falls back to the
        // game's requested count).
        s32 computeSnowDoorCount(GameDataFile* file) {
            if (!file)
                return -1;

            s32 count = 0;
            for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
                const ShineDB::ShineEntry& moon = ShineDB::sShineTable[i];
                if (!isSnowBarrierMoon(moon))
                    continue;

                const ShineDB::ShineEntry* placed = MoonRando::getItemForLocation(moon.uid, moon.stage);
                const bool collected = placed
                    ? file->customIsGotShine(placed->uid, placed->stage)
                    : file->customIsGotShine(moon.uid, moon.stage);
                if (collected)
                    count++;
            }
            return count;
        }

        GameDataFile* fileFromActor(al::LiveActor* door) {
            GameDataHolder* holder = GameDataHolderAccessor(door);
            return holder ? holder->getGameDataFile() : nullptr;
        }

        static HkTrampoline<void, const al::LiveActor*>
            keepMarioCapVisibilityEndDemoHook = hk::hook::trampoline(
                [](const al::LiveActor* player) -> void {
                    if (sScenarioCameraTryStartDepth > 0) {
                        // This helper only preserves a visual cap flag. The
                        // scenario-camera path can reach it before the player's
                        // cap/demo internals are ready, where retail asserts.
                        return;
                    }

                    keepMarioCapVisibilityEndDemoHook.orig(player);
                });

        static HkTrampoline<bool, StageSceneStateScenarioCamera*>
            scenarioCameraTryStartHook = hk::hook::trampoline(
                [](StageSceneStateScenarioCamera* state) -> bool {
                    static StageSceneStateScenarioCamera* sLastDeferredState = nullptr;

                    if (!isScenarioCameraPlayerReady(state)) {
                        if (state != sLastDeferredState) {
                            Randomizer::logPush(
                                "Deferred scenario camera: player holder not ready");
                            sLastDeferredState = state;
                        }
                        return false;
                    }

                    sLastDeferredState = nullptr;
                    sScenarioCameraTryStartDepth++;
                    const bool started = scenarioCameraTryStartHook.orig(state);
                    sScenarioCameraTryStartDepth--;
                    return started;
                });

        // al::getPlayerPos(const PlayerHolder*, s32) @ 0x009a3fcc forwards straight
        // into al::getTrans(PlayerHolder::tryGetPlayer(idx)) with no null check.
        // StageScene::appear() calls this unconditionally near its very start (to
        // feed Nintendo's prepo telemetry, trySavePrepoStartStage) before the
        // player actor is guaranteed to exist in the scene. That invariant can be
        // broken by the same deferred-ReturnToHome timing gap documented on
        // isPlayerDamageStopDemoHook in MoonHooks.cpp (a grand moon collected from
        // a boss refight, followed immediately by an ER exit into the next stage):
        // the scenario-camera guard above already proves the player holder can
        // still be empty at this point ("Deferred scenario camera: player holder
        // not ready"). Return a safe zero vector instead of crashing on the null
        // actor.
        static HkTrampoline<const sead::Vector3f&, const al::PlayerHolder*, s32>
            getPlayerPosFromHolderHook = hk::hook::trampoline(
                [](const al::PlayerHolder* holder, s32 idx) -> const sead::Vector3f& {
                    if (!holder || !al::tryGetPlayerActor(holder, idx))
                        return sead::Vector3f::zero;

                    return getPlayerPosFromHolderHook.orig(holder, idx);
                });

        // InformationWindowHackRequester remains active while a stage is
        // returning from a boss/death transition. Its WaitNoHack state can pass
        // the not-yet-created player actor to rs::isPlayerHack, whose retail
        // implementation immediately calls al::getPlayerActor(actor, 0) and
        // dereferences the result. With no player there cannot be an active
        // capture, so report the neutral state until the actor is registered.
        static HkTrampoline<bool, const al::LiveActor*>
            isPlayerHackHook = hk::hook::trampoline(
                [](const al::LiveActor* actor) -> bool {
                    return actor ? isPlayerHackHook.orig(actor) : false;
                });

        // Several InformationWindow nerves intentionally convert a missing
        // player into a null scene-object holder before checking controller
        // mode. Retail dereferences that holder unconditionally; without a
        // player the neutral answer is single-player.
        static HkTrampoline<bool, const al::IUseSceneObjHolder*>
            isSeparatePlayHook = hk::hook::trampoline(
                [](const al::IUseSceneObjHolder* holder) -> bool {
                    return holder ? isSeparatePlayHook.orig(holder) : false;
                });

        // The director updates every InformationWindow requester consecutively.
        // Defer the subsystem as a unit during the short return/death window
        // where player 0 is absent, then resume the untouched retail update.
        static HkTrampoline<void, void*>
            informationWindowDirectorExecuteHook = hk::hook::trampoline(
                [](void* director) -> void {
                    if (!isInformationWindowPlayerReady(director))
                        return;

                    informationWindowDirectorExecuteHook.orig(director);
                });

        // alCameraPoserFunction::calcTargetTrans(sead::Vector3f*, const CameraPoser*)
        // @ 0x00834990. Every camera poser's start()/movement() calls this to
        // find its tracked target's world position: getTargetHolder() ->
        // getViewTarget(index), then an unchecked vtable call through the
        // returned target. Same timing gap as the getPlayerPos guard above: if
        // CameraPoseUpdater switches to a follow camera (e.g.
        // CameraPoserFollowLimit::start) before the scene has registered a
        // camera target for the not-yet-existing player, getTargetHolder()/
        // getViewTarget() return null and the vtable call crashes. Resolve the
        // target once and call it directly; checking and then calling the retail
        // helper would perform a second, unsafe holder lookup.
        static HkReplace<void, sead::Vector3f*, const al::CameraPoser*>
            calcTargetTransHook = hk::hook::replace(
                [](sead::Vector3f* out, const al::CameraPoser* poser) -> void {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    if (!target) {
                        *out = sead::Vector3f::zero;
                        return;
                    }

                    target->calcTrans(out);
                });

        // CameraVerticalAbsorber::update uses this variant after the follow
        // camera has already become active. Retail repeats the same unchecked
        // holder/target lookup as calcTargetTrans, then applies the poser's
        // CameraOffsetCtrlPreset. Preserve the complete retail calculation
        // whenever a target exists; while the player target is temporarily
        // absent during death/return transitions, retain the poser's last
        // target position instead of dereferencing the null holder.
        static HkTrampoline<void, sead::Vector3f*, const al::CameraPoser*>
            calcTargetTransWithOffsetHook = hk::hook::trampoline(
                [](sead::Vector3f* out, const al::CameraPoser* poser) -> void {
                    if (!tryGetCameraViewTarget(poser)) {
                        *out = poser ? poser->getAt() : sead::Vector3f::zero;
                        return;
                    }

                    calcTargetTransWithOffsetHook.orig(out, poser);
                });

        static HkReplace<void, sead::Quatf*, const al::CameraPoser*>
            calcTargetPoseHook = hk::hook::replace(
                [](sead::Quatf* out, const al::CameraPoser* poser) -> void {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    if (!target) {
                        *out = sead::Quatf::unit;
                        return;
                    }

                    sead::Vector3f up = sead::Vector3f::zero;
                    sead::Vector3f front = sead::Vector3f::zero;
                    target->calcUp(&up);
                    target->calcFront(&front);
                    al::makeQuatFrontUp(out, front, up);
                });

        static HkReplace<void, sead::Vector3f*, const al::CameraPoser*>
            calcTargetFrontHook = hk::hook::replace(
                [](sead::Vector3f* out, const al::CameraPoser* poser) -> void {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    if (!target) {
                        *out = sead::Vector3f::zero;
                        return;
                    }

                    target->calcFront(out);
                });

        static HkReplace<void, sead::Vector3f*, const al::CameraPoser*>
            calcTargetGravityHook = hk::hook::replace(
                [](sead::Vector3f* out, const al::CameraPoser* poser) -> void {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    if (!target) {
                        *out = { 0.0f, -1.0f, 0.0f };
                        return;
                    }

                    target->calcGravity(out);
                });

        static HkReplace<bool, f32*, const al::CameraPoser*>
            tryGetTargetRequestDistanceHook = hk::hook::replace(
                [](f32* out, const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    if (!target)
                        return false;

                    const f32 distance = target->getRequestDistance();
                    if (distance <= 0.0f)
                        return false;

                    *out = distance;
                    return true;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetCollideGroundHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isCollideGround() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetInWaterHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isInWater() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetInMoonGravityHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isInMoonGravity() : false;
                });

        // alCameraPoserFunction::isTargetClimbPole(const al::CameraPoser*)
        // @ 0x00835ce4. Same getTargetHolder()->getViewTarget(index) call as
        // calcTargetTrans above, but inlined here with no null check at all
        // (it tail-calls a vtable method straight off the result).
        // CameraPoserFollowLimit::start calls this directly - not through
        // calcTargetTrans - while CameraPoseUpdater is switching to a follow
        // camera, so it can hit the same not-yet-registered camera target
        // during the deferred-player timing gap and crash on the null
        // CameraTargetHolder. Resolve the target once and report "not climbing
        // a pole" when it is unavailable.
        static HkReplace<bool, const al::CameraPoser*>
            isTargetClimbPoleHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isClimbPole() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetGrabCeilHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isGrabCeil() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetInvalidMoveByInputHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isInvalidMoveByInput() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetEnableEndAfterInterpoleHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isEnableEndAfterInterpole() : false;
                });

        static HkReplace<bool, const al::CameraPoser*>
            isTargetWallCatchHook = hk::hook::replace(
                [](const al::CameraPoser* poser) -> bool {
                    al::CameraTargetBase* target = tryGetCameraViewTarget(poser);
                    return target ? target->isWallCatch() : false;
                });

        static HkTrampoline<bool, const GameDataFile*, const QuestInfo*>
            isPlayScenarioCameraHook = hk::hook::trampoline(
                [](const GameDataFile* constFile, const QuestInfo* info) -> bool {
                    if (!Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized()
                        || !constFile || !info || !info->mIsMainQuest) {
                        return isPlayScenarioCameraHook.orig(constFile, info);
                    }

                    GameDataFile* file = const_cast<GameDataFile*>(constFile);
                    const s32 logicalWorldId = toLogicalWorldId(file->getCurrentWorldId());
                    const s32 targetQuestNo = resolveMainScenarioNo(file, logicalWorldId);
                    if (targetQuestNo < 0)
                        return isPlayScenarioCameraHook.orig(constFile, info);

                    // ScenarioStartCameraHolder accepts the first eligible camera in
                    // placement order. Rebuild the active main-quest group before that
                    // scan so an earlier camera cannot remain eligible after its
                    // physical story mission has been completed.
                    QuestInfoHolder* questHolder = rs::getQuestInfoHolder(info);
                    if (questHolder && questHolder->mActiveQuestNo != targetQuestNo) {
                        Randomizer::logPush(
                            "Story camera quest sync: world=%d active=%d target=%d",
                            logicalWorldId, questHolder->mActiveQuestNo, targetQuestNo);
                        questHolder->updateActiveList(targetQuestNo);
                    }

                    // Preserve vanilla's exact active-QuestInfo checks within a quest.
                    // This matters in Shiveria, where all four barrier missions share
                    // quest 0 and are distinguished by their individual QuestInfos.
                    if (info->mQuestNo != targetQuestNo)
                        return false;

                    return isPlayScenarioCameraHook.orig(constFile, info);
                });

        // DoorSnow::reset(u32) @ 0x26056c - silently set the door's open level.
        // Original returns `this` (al*); we preserve a pointer return for ABI safety.
        static HkTrampoline<void*, al::LiveActor*, u32>
            doorSnowResetHook = hk::hook::trampoline([](al::LiveActor* door, u32 count) -> void* {
                u32 finalCount = count;
                if (Randomizer::isMoonRandoEnabled()) {
                    const s32 computed = computeSnowDoorCount(fileFromActor(door));
                    if (computed >= 0) {
                        finalCount = static_cast<u32>(computed);
                        if (finalCount != count)
                            Randomizer::logPush("DoorSnow reset count patch: %u -> %u", count, finalCount);
                    }
                }
                return doorSnowResetHook.orig(door, finalCount);
            });

        // DoorSnow::startDemo(u32) @ 0x26061c - the camera-bearing open. Replace
        // it with a silent reset to the physically-derived level.
        static HkTrampoline<void*, al::LiveActor*, u32>
            doorSnowStartDemoHook = hk::hook::trampoline([](al::LiveActor* door, u32 count) -> void* {
                if (!Randomizer::isMoonRandoEnabled())
                    return doorSnowStartDemoHook.orig(door, count);

                const s32 computed = computeSnowDoorCount(fileFromActor(door));
                const u32 finalCount = computed >= 0 ? static_cast<u32>(computed) : count;
                Randomizer::logPush("DoorSnow startDemo -> silent reset (count=%u, requested=%u)", finalCount, count);
                return doorSnowResetHook.orig(door, finalCount);
            });

    } // namespace

    s32 resolveMainScenarioNo(GameDataFile* file, s32 logicalWorldId) {
        if (!file || logicalWorldId < 0 || logicalWorldId >= WorldMeta::sWorldCount)
            return -1;

        const s32 gameWorldId = toGameWorldId(logicalWorldId);
        const s32 savedMainScenarioNo = file->getMainScenarioNo(gameWorldId);
        if (!Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized())
            return savedMainScenarioNo;

        bool hasCompletedLocation[cMaxMainScenarioCount] = {};
        s32 highestMainScenarioNo = -1;

        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo& hint = file->getHintList()[i];
            if (!isSameHintWorld(hint.worldId, logicalWorldId)
                || hint.mainScenarioNo < 0
                || hint.mainScenarioNo >= cMaxMainScenarioCount) {
                continue;
            }

            const ShineDB::ShineEntry* location = findStoryLocation(hint);
            if (!location)
                continue;

            const s32 mainScenarioNo = hint.mainScenarioNo;
            if (isPhysicalLocationCollected(file, *location))
                hasCompletedLocation[mainScenarioNo] = true;
            if (mainScenarioNo > highestMainScenarioNo)
                highestMainScenarioNo = mainScenarioNo;
        }

        if (highestMainScenarioNo < 0)
            return savedMainScenarioNo;

        s32 physicalMainScenarioNo = 0;
        for (s32 mainScenarioNo = 0; mainScenarioNo <= highestMainScenarioNo; mainScenarioNo++) {
            if (hasCompletedLocation[mainScenarioNo])
                physicalMainScenarioNo = mainScenarioNo + 1;
        }

        // The latest completed physical story location establishes story progress.
        // Earlier locations are therefore considered complete for this progression
        // field, but their moon flags remain untouched. Vanilla advances this saved
        // field when the randomized *item* is credited, so finding (for example)
        // Sand's Atop-the-Tower moon in another kingdom can move this value forward
        // even though the Sand mission itself has never been completed.
        return physicalMainScenarioNo;
    }

    void healAllMainScenarios(GameDataFile* file) {
        if (!file || !Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized())
            return;

        auto mainScenarioNo = file->getMainScenarioNumArr();
        if (!mainScenarioNo.begin())
            return;

        for (s32 logicalWorldId = 0;
             logicalWorldId < WorldMeta::sWorldCount && logicalWorldId < 16;
             logicalWorldId++) {
            const s32 physical = resolveMainScenarioNo(file, logicalWorldId);
            if (physical < 0 || mainScenarioNo[logicalWorldId] == physical)
                continue;

            if (cLogScenarioHealing) {
                Randomizer::logPush(
                    "Story progress heal: world=%d main=%d -> %d",
                    logicalWorldId, mainScenarioNo[logicalWorldId], physical);
            }
            mainScenarioNo[logicalWorldId] = physical;
        }
    }

    void installHooks() {
        // Neither helper is in syms/main.sym, so install at the retail 1.0.0
        // offsets documented in the corresponding ida_context entries.
        keepMarioCapVisibilityEndDemoHook.installAtMainOffset(0x0056b6e4);
        scenarioCameraTryStartHook.installAtMainOffset(0x004eeeec);
        getPlayerPosFromHolderHook.installAtMainOffset(0x009a3fcc);
        isPlayerHackHook.installAtMainOffset(0x0056f8f8);
        isSeparatePlayHook.installAtMainOffset(0x00575af4);
        informationWindowDirectorExecuteHook.installAtMainOffset(0x004af40c);
        calcTargetTransHook.installAtMainOffset(0x00834990);
        calcTargetTransWithOffsetHook.installAtMainOffset(0x00834b1c);
        calcTargetPoseHook.installAtMainOffset(0x00834da0);
        calcTargetFrontHook.installAtMainOffset(0x00834e30);
        calcTargetGravityHook.installAtMainOffset(0x00834eb0);
        tryGetTargetRequestDistanceHook.installAtMainOffset(0x008351c0);
        isTargetCollideGroundHook.installAtMainOffset(0x00835c48);
        isTargetInWaterHook.installAtMainOffset(0x00835c7c);
        isTargetInMoonGravityHook.installAtMainOffset(0x00835cb0);
        isTargetClimbPoleHook.installAtMainOffset(0x00835ce4);
        isTargetGrabCeilHook.installAtMainOffset(0x00835d18);
        isTargetInvalidMoveByInputHook.installAtMainOffset(0x00835d4c);
        isTargetEnableEndAfterInterpoleHook.installAtMainOffset(0x00835d80);
        isTargetWallCatchHook.installAtMainOffset(0x00835db4);
        isPlayScenarioCameraHook.installAtSym<
            "_ZNK12GameDataFile20isPlayScenarioCameraEPK9QuestInfo">();
        doorSnowResetHook.installAtMainOffset(0x26056c);
        doorSnowStartDemoHook.installAtMainOffset(0x26061c);
    }

} // namespace StoryProgressHooks
