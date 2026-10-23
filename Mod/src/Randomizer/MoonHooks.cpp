#include "Randomizer/MoonHooks.h"

#include "hk/hook/InstrUtil.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "hk/ro/RoUtil.h"
#include "hk/svc/cpu.h"
#include "hk/util/Math.h"

#include "al/Library/Event/EventFlowFunction.h"
#include "al/Library/Execute/ExecuteDirector.h"
#include "al/Library/Execute/ExecuteTableHolderDraw.h"
#include "al/Library/Execute/ExecutorListActorModelDraw.h"
#include "al/Library/HitSensor/HitSensorKeeper.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorClippingFunction.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorMovementFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/ActorSensorUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Message/MessageHolder.h"
#include "al/Library/Model/ModelDrawerBase.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Placement/PlacementFunction.h"
#include "al/Library/Play/Layout/WindowConfirm.h"
#include "al/Library/Scene/SceneObjHolder.h"

#include "game/Demo/ProjectDemoDirector.h"
#include "game/Item/Shine.h"
#include "game/Item/ShineInfo.h"
#include "game/MapObj/CapMessageShowInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Scene/StageScene.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/GameProgressData.h"
#include "game/System/GameSystem.h"
#include "game/Util/DemoUtil.h"
#include "game/Util/ItemUtil.h"
#include "game/Util/SensorMsgFunction.h"
#include "game/Util/StageLayoutFunction.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <typeinfo>

#include "Item/Shine.h"
#include "Library/Base/StringUtil.h"
#include "Library/LiveActor/ActorInitInfo.h"
#include "Library/Nerve/NerveUtil.h"
#include "Library/Placement/PlacementId.h"
#include "Library/Scene/Scene.h"
#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/server/Client.hpp"
#include "Project/HitSensor/HitSensor.h"
#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureOverlay.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/FoolPunishment.h"
#include "Randomizer/HintEconomy.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MoonRequirement.h"
#include "Randomizer/MovementAbilityLock.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PaintingUnlockOverlay.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/SphinxRando.h"
#include "Randomizer/StoryProgressHooks.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/TimerMode.h"
#include "Randomizer/UnlockRewards.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"
#include "System/GameDataHolderAccessor.h"
#include "heap/seadFrameHeap.h"
#include "heap/seadHeapMgr.h"
#include "helpers.hpp"
#include "prim/seadSafeString.h"

namespace al {
    class LiveActor;
    class ActorInitInfo;
    void initChildActorWithArchiveNameNoPlacementInfo(al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeStringBase<char>& archiveName,
        const char* suffix);
    void invalidateClipping(al::LiveActor*);
    void startAction(al::LiveActor*, const char*);
    bool isActionEnd(const al::LiveActor*);
} // namespace al

namespace MoonHooks {

    static bool isHooksCreated = false;
    static constexpr const char* cMoonRockKeyArchive = "ShineMoonRock";
    static constexpr const char* cMoonRockKeyDotArchive = "ShineDotKey";
    static constexpr const char* cMoonRockKeyGrandArchive = "ShineGrandMoonRock";
    static constexpr const char* cMoonRockArchive = "MoonRock";
    static constexpr const char* cMoonRockWithKeyArchive = "MoonRockWithKey";
    static constexpr const char* cMoonRockKeyBlockedMessageLabel = "MoonRockKeyRequired";
    static constexpr const char* cSubareaKeyBlockedMessageLabel = "SubareaKeyRequired";
    static constexpr const char* cToadetteMedalArchive = "ShineToken";
    static constexpr const char* cToadetteMedalDotArchive = "ShineDotToken";
    static constexpr const char* cToadetteMedalGrandArchive = "ShineGrandToken";
    // Moon pickups and demo-archive preloading are on the game thread.  The
    // normal flow trace uses synchronous OutputDebugString calls on Ryujinx, so
    // keep it opt-in; release-block and unstick diagnostics remain unconditional.
    static constexpr bool cLogMoonGetDetails = false;
    // FrameHeap creation is only useful while diagnosing an allocator crash. It
    // can emit dozens of synchronous debugger messages on a stage reload, so it
    // must not be on in a normal build.
    static constexpr bool cLogMoonHeapDiagnostics = false;

    static u32 makeActorActionKey(const al::LiveActor* actor, u32 domain) {
        u32 hash = 2166136261u ^ domain;
        auto append = [&hash](const char* text) {
            while (text && *text) {
                hash ^= static_cast<u8>(*text++);
                hash *= 16777619u;
            }
        };
        append(actor ? actor->getName() : nullptr);
        if (actor) {
            GameDataHolderAccessor accessor(actor);
            GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
            append(file ? file->getStageNameCurrent() : nullptr);
            const sead::Vector3f& trans = al::getTrans(actor);
            u32 bits[3];
            std::memcpy(bits, &trans, sizeof(bits));
            for (s32 i = 0; i < 3; i++) {
                hash ^= bits[i];
                hash *= 16777619u;
            }
        }
        return hash ? hash : domain;
    }

    static HkTrampoline<void, al::LiveActor*, s32> lifeMaxUpItemGetHook = hk::hook::trampoline([](al::LiveActor* item, s32 appearCoinCount) -> void {
        const u32 key = makeActorActionKey(item, 0x54000000u);
        lifeMaxUpItemGetHook.orig(item, appearCoinCount);
        Bingo::recordEvent({ Bingo::GoalType::LifeUpHearts, 1, -1, key, Bingo::EventOrigin::Local });
    });

    static HkTrampoline<void, al::LiveActor*, s32> lifeMaxUpItem2DGetHook = hk::hook::trampoline([](al::LiveActor* item, s32 appearCoinCount) -> void {
        const u32 key = makeActorActionKey(item, 0x54000000u);
        lifeMaxUpItem2DGetHook.orig(item, appearCoinCount);
        Bingo::recordEvent({ Bingo::GoalType::LifeUpHearts, 1, -1, key, Bingo::EventOrigin::Local });
    });

    static constexpr u32 listPtrNop[] = {
        0x1D18F0,
        0x1D1934,
        0x4DBC20,
        0x4DBF08,
        0x4DBF14,
        0x4DC1D4,
        0x4DEA48,
        0x4d0600,
        0x4d0608,
        0x4d0630,
        0x4d0670,
        0x1CDB2C,
        0x1CE2B4,
        0x1CE1C8,
        0x1d255c,
        0x1d275c,
        0x32adfc,
        0x1d1b38,
        0x1ce980,
        0x1ce9b8,
        0x1d04b4,
        0x1cdb2c,
        0x1cd79c,
        0x1cd84c,
        0x1cd860,
        0x1ce2a0,
        0x1ce288,
        0x1ce310,
    };
    static constexpr u32 listPtrMov[] = {
        0x4DBF54,
        0x4DBF60,
        0x4DC578,
        0x4DC584,
        0x4DC66C,
        0x4DC8B0,
        0x4DCA64,
        0x4DCA9C,
        0x4DE2F8,
        0x4DE6AC,
        0x4DE710,
        0x4DE974,
        0x4DE984,
        0x4DE9F8,
        0x4DEA70,
        0x4DEB30,
        0x4DED44,
        0x4DED80,
        0x4DED90,
        0x4DEE70,
        0x4DEE58,
        0x4EEF14,
        0x4EEF48,
        0x4EEF08,
        0x4DF0AC,
        0x4DF16C,
        0x4EECB0,
        0x4EED90,
        0x4EED9C,
        0x4DF180,
        0x4DC740,
        0x4EFAE8,
        0x1D56DC,
        0x1d1c88,
        0x1d1c60,
        0x1d1c98,
        0x1d2b44,
    };
    static hk::hook::a64::AsmBlock<true, 1>* listNop[hk::util::arraySize(listPtrNop)];
    static hk::hook::a64::AsmBlock<true, 1>* listMov[hk::util::arraySize(listPtrMov)];

    void uninstallMoonCutsceneHooks() {
        if (!isHooksCreated)
            return;
        for (hk::hook::a64::AsmBlock<true, 1>* b : listNop) {
            b->uninstall();
        }
        for (hk::hook::a64::AsmBlock<true, 1>* b : listMov) {
            b->uninstall();
        }
    }

    void createMoonCutsceneHooks() {
        if (!isHooksCreated) {
            for (int i = 0; i < hk::util::arraySize(listPtrNop); i++) {
                listNop[i] = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"nop", true>());
            }
            for (int i = 0; i < hk::util::arraySize(listPtrMov); i++) {
                listMov[i] = new hk::hook::a64::AsmBlock<true, 1>(hk::hook::a64::assemble<"mov w0, #1", true>());
            }
            isHooksCreated = true;
        }
    }

    void installMoonCutsceneHooks() {
        // Guard against a flag setter running before the blocks exist.
        createMoonCutsceneHooks();
        for (int i = 0; i < hk::util::arraySize(listPtrNop); i++) {
            listNop[i]->installAtMainOffset(listPtrNop[i]);
        }
        for (int i = 0; i < hk::util::arraySize(listPtrMov); i++) {
            listMov[i]->installAtMainOffset(listPtrMov[i]);
        }
    }

    enum class MoonDemoRoute : u8
    {
        None,
        Pending,
        Normal,
        Dot,
        Special,
        Story,
        Grand,
        Direct,
    };

    // This is deliberately a short-lived collection record, rather than another
    // interpretation of sLatestGot*.  The latter remains useful for layout/text
    // code after a demo has finished, but using it to select a reusable actor
    // lets a second reward in the same stage inherit the first reward's model.
    struct MoonGetDemoContext {
        const ShineDB::ShineEntry* destination;
        const ShineDB::ShineEntry* physical;
        Shine* source;
        const void* state;
        u32 serial;
        MoonDemoRoute route;
    };
    static MoonGetDemoContext sMoonGetDemoContext = {};
    static u32 sMoonGetDemoSerial = 0;
    static s32 sMoonDemoNativeInvariantLogCount = 0;
    // Shine::init is the only native collection query that gets the Completed
    // Kingdom presentation override. Keep the actor in a stack-scoped context
    // while the original init runs so the call-site patch can identify exactly
    // which Shine is being initialized, including nested linked-shine inits.
    struct CompletedKingdomShineInitContext {
        Shine* shine;
        CompletedKingdomShineInitContext* previous;
    };
    static CompletedKingdomShineInitContext* sCompletedKingdomShineInitContext = nullptr;

    // The registry is scene-local: entries are sampled once by Shine::init and
    // consumed by the first real pickup. No save/list/query API consults it.
    struct ForcedCompletedKingdomShine {
        Shine* shine;
        const ShineInfo* info;
    };
    static constexpr s32 cForcedCompletedKingdomShineMax = 512;
    static ForcedCompletedKingdomShine sForcedCompletedKingdomShines[cForcedCompletedKingdomShineMax] = {};
    static s32 sForcedCompletedKingdomShineCount = 0;

    static bool isForcedCompletedKingdomShine(const Shine* shine, const ShineInfo* info = nullptr);
    static void consumeForcedCompletedKingdomShine(const Shine* shine, const ShineInfo* info = nullptr);
    static bool commitForcedCompletedKingdomShine(Shine* shine);

    static const char* getMoonDemoRouteName(MoonDemoRoute route) {
        switch (route) {
        case MoonDemoRoute::Pending:
            return "pending";
        case MoonDemoRoute::Normal:
            return "normal";
        case MoonDemoRoute::Dot:
            return "dot";
        case MoonDemoRoute::Special:
            return "special";
        case MoonDemoRoute::Story:
            return "story";
        case MoonDemoRoute::Grand:
            return "grand";
        case MoonDemoRoute::Direct:
            return "direct";
        default:
            return "none";
        }
    }

    static void clearMoonGetDemoContext() {
        sMoonGetDemoContext = {};
    }

    static void beginMoonGetDemoContext(const ShineDB::ShineEntry* physical, const ShineDB::ShineEntry* destination, MoonDemoRoute route = MoonDemoRoute::Pending) {
        if (++sMoonGetDemoSerial == 0)
            ++sMoonGetDemoSerial;
        sMoonGetDemoContext = {
            destination,
            physical,
            nullptr,
            nullptr,
            sMoonGetDemoSerial,
            route,
        };
    }

    static void attachMoonGetDemoContext(Shine* source, MoonDemoRoute route) {
        if (!source || !sMoonGetDemoContext.destination)
            return;
        // grantMoonDirect also backs bought-list rewards. Those synthesize a
        // native Shine solely for their legacy cutscene, which is intentionally
        // outside Custom Moon presentation scope. Do not let that synthetic
        // source convert a Direct context into a normal custom-model request.
        if (sMoonGetDemoContext.route == MoonDemoRoute::Direct)
            return;
        sMoonGetDemoContext.source = source;
        sMoonGetDemoContext.route = route;
    }

    static void completeMoonGetDemoContext(const void* state, MoonDemoRoute route) {
        if (!sMoonGetDemoContext.destination)
            return;
        if ((state && sMoonGetDemoContext.state == state) || (!state && sMoonGetDemoContext.route == route)) {
            clearMoonGetDemoContext();
        }
    }

    static bool sGrandMoonDemoPending = false;
    // Grand presentation remains entirely native. This pointer is retained only
    // for its native action/watchdog path; all custom presentation uses the
    // state-local generic LiveActor slots below.
    static Shine* sGrandMoonDemoRequestShine = nullptr;
    static Shine* sNormalRepeatGrandShine = nullptr;
    static sead::Vector3f sRepeatGrandStartTrans;
    static sead::Vector3f sRepeatGrandUpDir;
    static constexpr s32 cInitializedShineMax = 512;
    static Shine* sInitializedShines[cInitializedShineMax] = {};
    static s32 sInitializedShineCount = 0;
    static constexpr uintptr_t cAlShowModelIfHideOffset = 0x008df5c4;
    static constexpr uintptr_t cAlHideModelIfShowOffset = 0x008df65c;
    static constexpr uintptr_t cAlStartActionOffset = 0x008d2d00;
    static constexpr uintptr_t cAlClearMclAnimOffset = 0x008d4894;
    static constexpr uintptr_t cAlIsExistActionOffset = 0x008d3140;
    static constexpr uintptr_t cAlIsActionPlayingOffset = 0x008d2f94;
    static constexpr uintptr_t cAlGetActionNameOffset = 0x008d332c;
    static constexpr uintptr_t cAlGetActionFrameOffset = 0x008d3370;
    static constexpr uintptr_t cAlStartHitReactionOffset = 0x008d3860;
    static constexpr uintptr_t cAlStartSeOffset = 0x009e0358;
    static constexpr uintptr_t cAlResetPositionOffset = 0x008e28b8;
    static constexpr uintptr_t cAlCalcUpDirOffset = 0x008ee7e4;
    static constexpr uintptr_t cRsTryAppearMultiCoinFromObjOffset = 0x005562a0;
    static constexpr uintptr_t cRsIsActiveDemoShineDotGetOffset = 0x00550cc0;
    static constexpr uintptr_t cRsIsActionEndDemoPlayerOffset = 0x0056a93c;
    static constexpr uintptr_t cShineNrvGotOffset = 0x01cbe530;
    static constexpr uintptr_t cShineNrvCoinGetOffset = 0x01cbe5c8;
    static constexpr uintptr_t cShineEmptyConvertReactionOffset = 0x0181e61f;
    // These Shine methods are present in the main executable but intentionally
    // absent from the linked symbol set. Keep their calls offset-based just like
    // the other game-private helpers in this file.
    static constexpr uintptr_t cShineGetCurrentModelOffset = 0x001ce7b0;
    static constexpr uintptr_t cShineHideAllModelOffset = 0x001cebe0;
    static constexpr uintptr_t cShineShowCurrentModelOffset = 0x001d01b4;
    static constexpr uintptr_t cShineAddDemoActorWithModelOffset = 0x001d0368;
    static constexpr uintptr_t cShineIsEmptyForDemoGetGrandOffset = 0x001d06e0;

    template <typename Fn>
    static Fn mainOffsetFunction(uintptr_t offset) {
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
    }

    static al::LiveActor* getCurrentShineModelAtMainOffset(Shine* shine) {
        if (!shine)
            return nullptr;
        using Fn = al::LiveActor* (*)(Shine*);
        return mainOffsetFunction<Fn>(cShineGetCurrentModelOffset)(shine);
    }

    static void hideAllShineModelsAtMainOffset(Shine* shine) {
        if (!shine)
            return;
        using Fn = void (*)(Shine*);
        mainOffsetFunction<Fn>(cShineHideAllModelOffset)(shine);
    }

    static void showCurrentShineModelAtMainOffset(Shine* shine) {
        if (!shine)
            return;
        using Fn = void (*)(Shine*);
        mainOffsetFunction<Fn>(cShineShowCurrentModelOffset)(shine);
    }

    static void addShineDemoActorWithModelAtMainOffset(Shine* shine) {
        if (!shine)
            return;
        using Fn = void (*)(Shine*);
        mainOffsetFunction<Fn>(cShineAddDemoActorWithModelOffset)(shine);
    }

    static bool isEmptyShineForDemoGetGrandAtMainOffset(const Shine* shine) {
        if (!shine)
            return false;
        using Fn = bool (*)(const Shine*);
        return mainOffsetFunction<Fn>(cShineIsEmptyForDemoGetGrandOffset)(shine);
    }

    static Shine* getDemoShineActorByOffset(const al::Scene* scene);
    static Shine* getDemoShineForRead(const al::Scene* scene);
    static bool isShineActor(al::LiveActor* actor);
    static bool isCustomMoonDemoActor(const al::LiveActor* actor);
    static void showMoonDemoPresentation(al::LiveActor* actor);
    static StageScene* tryGetCurrentStageScene();
    static GameDataFile* tryGetCurrentGameDataFile();
    static bool isDeadGameDataFile(const GameDataFile* file);

    static void hideModelIfShowAtMainOffset(al::LiveActor* actor) {
        if (!actor)
            return;
        using Fn = void (*)(al::LiveActor*);
        mainOffsetFunction<Fn>(cAlHideModelIfShowOffset)(actor);
    }

    static void showModelIfHideAtMainOffset(al::LiveActor* actor) {
        if (!actor)
            return;
        using Fn = void (*)(al::LiveActor*);
        mainOffsetFunction<Fn>(cAlShowModelIfHideOffset)(actor);
    }

    static void prepareCustomMoonDemoPresentationActor(al::LiveActor* actor) {
        if (!actor)
            return;
        // Capability preflight intentionally ends the actor while its model is
        // hidden. LiveActor::makeActorAlive preserves that hide flag, and then
        // deliberately omits the actor from the draw executor. Calling
        // showModelIfHide while it is still dead only clears the flag; the native
        // get-demo's subsequent makeActorAlive then performs the normal show and
        // draw-executor registration.
        al::invalidateClipping(actor);
        showMoonDemoPresentation(actor);
    }

    static void startActionAtMainOffset(al::LiveActor* actor, const char* action) {
        using Fn = void (*)(al::LiveActor*, const char*);
        mainOffsetFunction<Fn>(cAlStartActionOffset)(actor, action);
    }

    static void startHitReactionAtMainOffset(const al::LiveActor* actor, const char* reaction) {
        using Fn = void (*)(const al::LiveActor*, const char*);
        mainOffsetFunction<Fn>(cAlStartHitReactionOffset)(actor, reaction);
    }

    static void startSeAtMainOffset(const al::IUseAudioKeeper* audioKeeper, const char* sound) {
        using Fn = void (*)(const al::IUseAudioKeeper*, const sead::SafeString&);
        const sead::SafeString soundName(sound);
        mainOffsetFunction<Fn>(cAlStartSeOffset)(audioKeeper, soundName);
    }

    static void resetPositionAtMainOffset(al::LiveActor* actor, const sead::Vector3f& position) {
        using Fn = void (*)(al::LiveActor*, const sead::Vector3f&);
        mainOffsetFunction<Fn>(cAlResetPositionOffset)(actor, position);
    }

    static void calcUpDirAtMainOffset(sead::Vector3f* upDir, const al::LiveActor* actor) {
        using Fn = void (*)(sead::Vector3f*, const al::LiveActor*);
        mainOffsetFunction<Fn>(cAlCalcUpDirOffset)(upDir, actor);
    }

    static bool tryAppearMultiCoinFromObjAtMainOffset(al::LiveActor* actor, const sead::Vector3f& trans, const sead::Quatf& quat, s32 step, f32 offsetAbove) {
        using Fn = bool (*)(al::LiveActor*, const sead::Vector3f&, const sead::Quatf&, s32, f32);
        return mainOffsetFunction<Fn>(cRsTryAppearMultiCoinFromObjOffset)(actor, trans, quat, step, offsetAbove);
    }

    static bool isActiveDemoShineDotGetAtMainOffset(const al::Scene* scene) {
        if (!scene)
            return false;
        using Fn = bool (*)(const al::Scene*);
        return mainOffsetFunction<Fn>(cRsIsActiveDemoShineDotGetOffset)(scene);
    }

    // -- Moon-get pickup timing instrumentation --------------------------------
    //
    // Temporary diagnostic for the reported "pause between touching a moon and
    // the get animation starting". It splits that window into the places the cost
    // can actually live, so a hardware run identifies which:
    //
    //   touch=  the Shine's own get nerve starting (exeWaitRequestDemo for a
    //           first-time pickup, exeGot for the already-collected chain) to the
    //           get animation. This is the whole player-visible gap; every span
    //           below sits inside it, so touch= minus tot= is the part that runs
    //           before the reward is even credited.
    //   set=    time inside our own GameDataFunction::setGotShine hook (credit
    //           remap, unlock feedback, bingo, hint sweep).
    //   c2f=    credit to the first frame of the get state's NrvDemoGetFirst.
    //           Large here with a small set= means the cost is native, not ours.
    //   bind=   time inside the custom archive bind (resolution + slot swap).
    //   f2g=    NrvDemoGetFirst to NrvDemoGet, i.e. how long the state waits on
    //           the presentation actor's "GetShineSub" action. Large here with a
    //           proportional frame count is a clip-length difference in the bound
    //           custom archive, not a CPU cost.
    //   fr=     update ticks spent in NrvDemoGetFirst.
    //   worst=  longest single frame in that span. Large here with a small frame
    //           count is a one-frame stall (first draw of a model that has never
    //           been rendered this stage visit). Equal to normal frame time means
    //           there is no stall at all.
    //
    // The first run showed set/bind/c2f/f2g all at normal frame pacing for the
    // normal route, which is why touch= was added: it covers the moon's own rise
    // and coin payout, the only remaining guest-side part of the gap. Story and
    // grand rewards get their own line from their own demo states.
    //
    // getSystemTick is a bare cntpct_el0 read, but each report writes several
    // synchronous debug lines. Keep the diagnostic opt-in for normal play: on
    // Ryujinx those writes can themselves look exactly like a pickup hitch.
    // Enable only for an isolated timing capture.
    static constexpr bool cProfileMoonGetTimings = false;

    static u64 sMoonGetProfileTickHz = 0;
    // Set at the first step of the Shine's own get nerve, which runs before the
    // credit that starts every other span here -- so it deliberately survives
    // moonGetProfileBeginCollect and is cleared only when a line is emitted. A
    // route that emits no line (grand) can leave this stale, but the next pickup
    // overwrites it at its own first step before any line is built from it.
    static u64 sMoonGetProfileTouchTick = 0;
    static u64 sMoonGetProfileCollectTick = 0;
    static u64 sMoonGetProfileSetGotShineUs = 0;
    static u64 sMoonGetProfileFirstStepTick = 0;
    static u64 sMoonGetProfilePrevFrameTick = 0;
    static u64 sMoonGetProfileWorstFrameUs = 0;
    static u64 sMoonGetProfileBindUs = 0;
    static s32 sMoonGetProfileFirstStepFrames = 0;
    static const char* sMoonGetProfileArchive = nullptr;

    // Second window: the opening frames of the animation itself, which is where
    // the dropped frames are actually reported. Eight is enough to cover the
    // layout appearing and its text rewrite without logging for the whole demo.
    // set= turned out to be the largest single number on the path (136 ms on the
    // first pickup of a session), so it gets split into the four things it
    // actually contains.
    static u64 sMoonGetProfileBingoUs = 0;
    static u64 sMoonGetProfileUnlockUs = 0;
    static u64 sMoonGetProfileOrigUs = 0;

    static constexpr s32 cMoonGetPostFrameCount = 8;
    static bool sMoonGetPostActive = false;
    static u64 sMoonGetPostPrevTick = 0;
    static u64 sMoonGetPostWorstUs = 0;
    static s32 sMoonGetPostFrames = 0;

    static u64 moonGetProfileTick() {
        if (!cProfileMoonGetTimings)
            return 0;
        return hk::svc::getSystemTick();
    }

    static u64 moonGetProfileUs(u64 startTick, u64 endTick) {
        if (!cProfileMoonGetTimings || startTick == 0 || endTick <= startTick)
            return 0;
        if (sMoonGetProfileTickHz == 0)
            sMoonGetProfileTickHz = hk::svc::getSystemTickFrequency();
        if (sMoonGetProfileTickHz == 0)
            return 0;
        return ((endTick - startTick) * 1000000ull) / sMoonGetProfileTickHz;
    }

    // Collection is the start of every measured window. Reset here rather than at
    // the demo state so a pickup that never reaches a get demo cannot leave stale
    // ticks behind for the next one.
    static void moonGetProfileBeginCollect() {
        if (!cProfileMoonGetTimings)
            return;
        sMoonGetProfileCollectTick = moonGetProfileTick();
        sMoonGetProfileSetGotShineUs = 0;
        sMoonGetProfileFirstStepTick = 0;
        sMoonGetProfilePrevFrameTick = 0;
        sMoonGetProfileWorstFrameUs = 0;
        sMoonGetProfileBindUs = 0;
        sMoonGetProfileFirstStepFrames = 0;
        sMoonGetProfileArchive = nullptr;
        sMoonGetProfileBingoUs = 0;
        sMoonGetProfileUnlockUs = 0;
        sMoonGetProfileOrigUs = 0;
    }

    // Scope guard for the sub-spans of setGotShine; the hook has several exits
    // and both a remapped and a physical branch, so each span records itself.
    struct MoonGetProfileSpan {
        u64* out;
        u64 start;
        explicit MoonGetProfileSpan(u64* target)
            : out(target)
            , start(moonGetProfileTick()) { }
        ~MoonGetProfileSpan() { *out += moonGetProfileUs(start, moonGetProfileTick()); }
    };

    // Emitted once per pickup, at the moment the visible get animation begins:
    // the handover from NrvDemoGetFirst to NrvDemoGet for normal/special/Dot, and
    // the first step of the story route's own hold-up state.
    static void logMoonGetPickupTimings(const char* route) {
        if (!cProfileMoonGetTimings || sMoonGetProfileCollectTick == 0)
            return;

        // Two lines: logPush truncates a single entry at 128 characters.
        const u64 now = moonGetProfileTick();
        Randomizer::logPush("MOONGET[%s] arc=%s touch=%uus tot=%uus set=%uus", route ? route : "?", sMoonGetProfileArchive ? sMoonGetProfileArchive : "<native>",
            static_cast<u32>(moonGetProfileUs(sMoonGetProfileTouchTick, now)), static_cast<u32>(moonGetProfileUs(sMoonGetProfileCollectTick, now)),
            static_cast<u32>(sMoonGetProfileSetGotShineUs));
        Randomizer::logPush("MOONGET set: bingo=%uus unlock=%uus orig=%uus", static_cast<u32>(sMoonGetProfileBingoUs), static_cast<u32>(sMoonGetProfileUnlockUs),
            static_cast<u32>(sMoonGetProfileOrigUs));
        Randomizer::logPush("MOONGET c2f=%uus bind=%uus f2g=%uus fr=%d worst=%uus",
            static_cast<u32>(moonGetProfileUs(sMoonGetProfileCollectTick, sMoonGetProfileFirstStepTick)), static_cast<u32>(sMoonGetProfileBindUs),
            static_cast<u32>(moonGetProfileUs(sMoonGetProfileFirstStepTick, now)), sMoonGetProfileFirstStepFrames,
            static_cast<u32>(sMoonGetProfileWorstFrameUs));

        // One line per pickup: drop both windows so a state that re-enters its
        // nerve cannot log the same collection twice.
        sMoonGetProfileCollectTick = 0;
        sMoonGetProfileTouchTick = 0;

        // Everything above stops here, at the handover -- but the visible hitch
        // is reported *at* the start of the animation, and the got-shine layout
        // and its text rewrite both land after this point. Start a second window
        // over the opening frames of NrvDemoGet so that part stops being blind.
        sMoonGetPostFrames = 0;
        sMoonGetPostWorstUs = 0;
        // The report above emits synchronous debug output. Start the post-demo
        // window after it, otherwise the first sampled "game" frame is mostly
        // the profiler's own OutputDebugString work.
        sMoonGetPostPrevTick = moonGetProfileTick();
        sMoonGetPostActive = true;
    }

    // Sampled from exeDemoGet, which runs once per update of the state that
    // actually plays the animation.
    static void sampleMoonGetPostFrame() {
        if (!cProfileMoonGetTimings || !sMoonGetPostActive)
            return;

        const u64 now = moonGetProfileTick();
        const u64 frameUs = moonGetProfileUs(sMoonGetPostPrevTick, now);
        if (frameUs > sMoonGetPostWorstUs)
            sMoonGetPostWorstUs = frameUs;
        sMoonGetPostPrevTick = now;

        if (++sMoonGetPostFrames < cMoonGetPostFrameCount)
            return;

        sMoonGetPostActive = false;
        Randomizer::logPush("MOONGET post fr=%d worst=%uus", sMoonGetPostFrames, static_cast<u32>(sMoonGetPostWorstUs));
    }

    // setGotShine has several early returns; scope the measurement so every one
    // of them records the same span.
    struct MoonGetProfileSetGotShineScope {
        MoonGetProfileSetGotShineScope() { moonGetProfileBeginCollect(); }
        ~MoonGetProfileSetGotShineScope() { sMoonGetProfileSetGotShineUs = moonGetProfileUs(sMoonGetProfileCollectTick, moonGetProfileTick()); }
    };

    static al::LiveActor* findRepeatCoinEmitter(Shine* source) {
        if (!source)
            return nullptr;

        al::LiveActor* sourceCandidates[] = {
            source,
            source->mModelEmpty,
            source->mModelShine,
        };
        for (al::LiveActor* actor : sourceCandidates)
            if (actor && actor->getActorItemKeeper())
                return actor;

        StageScene* scene = tryGetCurrentStageScene();
        Shine* demoShine = getDemoShineForRead(scene);
        if (demoShine && demoShine != source) {
            al::LiveActor* demoCandidates[] = {
                demoShine,
                demoShine->mModelEmpty,
                demoShine->mModelShine,
            };
            for (al::LiveActor* actor : demoCandidates)
                if (actor && actor->getActorItemKeeper())
                    return actor;
        }

        // A normal in-stage ShineEmpty is the exact actor retail uses for this
        // payout. Borrow its configured item keeper while still spawning at the
        // Grand Star's position.
        for (s32 i = 0; i < sInitializedShineCount; i++) {
            Shine* shine = sInitializedShines[i];
            if (!shine || shine == source)
                continue;

            al::LiveActor* candidates[] = {
                shine,
                shine->mModelEmpty,
                shine->mModelShine,
            };
            for (al::LiveActor* actor : candidates)
                if (actor && actor->getActorItemKeeper())
                    return actor;
        }

        return nullptr;
    }

    // Shine::exeCoin's own emitter choice: the empty model once the shine is
    // collected, the shine model before that, and the Shine itself when it has
    // no separate models.
    static al::LiveActor* getNativeCoinEmitter(Shine* shine) {
        if (!shine)
            return nullptr;
        if (!shine->mModelEmpty)
            return shine;
        return shine->mIsGotShine ? shine->mModelEmpty : shine->mModelShine;
    }

    // rs::tryAppearMultiCoinFromObj -> al::setAppearItemOffset writes straight
    // through the emitter's ItemKeeper with no null check, and a moon archive we
    // swapped in can load a model that carries no item list at all -- the grand
    // moon models are the known case, since retail never runs a coin payout on
    // one. Detect that before native does and take the payout over ourselves.
    static bool hasUsableNativeCoinEmitter(Shine* shine) {
        al::LiveActor* emitter = getNativeCoinEmitter(shine);
        return emitter && emitter->getActorItemKeeper();
    }

    // GrandMoonEmpty has the correct one-shot "Got" cue, but it can be
    // initialized as a demo actor without an active audio keeper. The cue is
    // non-positional, so a regular in-stage MoonEmpty is a safe, exact audio
    // source for the ordinary repeat-moon jingle.
    static al::LiveActor* findRepeatJingleEmitter(Shine* source) {
        for (s32 i = 0; i < sInitializedShineCount; i++) {
            Shine* shine = sInitializedShines[i];
            if (!shine || shine == source || shine->mModelType == ShineType::Grand)
                continue;
            if (shine->mModelEmpty)
                return shine->mModelEmpty;
        }

        return source ? source->mModelEmpty : nullptr;
    }

    static HkTrampoline<void, Shine*> shineExeWaitRequestDemoHook = hk::hook::trampoline([](Shine* shine) -> void {
        // A first-time pickup enters this nerve, not ShineNrvGot (which retail
        // reserves for the already-collected/no-demo chain), so this is the
        // collection instant for everything that actually plays a get cutscene.
        if (cProfileMoonGetTimings && shine && al::isFirstStep(shine))
            sMoonGetProfileTouchTick = moonGetProfileTick();

        // The game's global demo actor is a real Shine and is read as one by
        // fixed-offset native code (including Shine::getColorFrame and
        // Shine::onSwitchGet). Never replace it. Record which route is about to
        // run and bind the custom generic actor only after the state selects its
        // route-local presentation slot.
        const bool isGrandGet = shine && shine->mModelType == ShineType::Grand;
        const bool isMainGet = shine && !isGrandGet && shine->mIsMainShine;
        const bool isDotGet = shine && !isGrandGet && !isMainGet && shine->mModelType == ShineType::Dot;
        const MoonDemoRoute route = isGrandGet ? MoonDemoRoute::Grand : (isMainGet ? MoonDemoRoute::Story : (isDotGet ? MoonDemoRoute::Dot : (shine && shine->mIsPowerStar ? MoonDemoRoute::Special : MoonDemoRoute::Normal)));
        attachMoonGetDemoContext(shine, route);

        if (!shine || shine->mModelType != ShineType::Grand || !shine->mIsGotShine) {
            shineExeWaitRequestDemoHook.orig(shine);
            return;
        }

        // Repeat Grand rewards take the native no-demo conversion path below,
        // so there is no state kill/DemoEnd that could consume this context.
        completeMoonGetDemoContext(nullptr, MoonDemoRoute::Grand);

        // Retail routes an already-collected normal Moon through ShineNrvGot:
        // it plays GetShineEmpty, performs the short conversion reaction,
        // then enters the coin payout. Grand Shines with stage-change data
        // request the Grand demo instead, so route only already-collected
        // Grands into that same no-demo animation chain.
        sGrandMoonDemoPending = false;
        sNormalRepeatGrandShine = shine;
        Randomizer::logPush("already-collected Grand Shine: skipped demo and entered repeat animation");
        al::setNerve(shine, reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + cShineNrvGotOffset));
    });

    static HkTrampoline<void, Shine*> shineExeGotHook = hk::hook::trampoline([](Shine* shine) -> void {
        // A forced actor normally commits through the native receiveMsg path.
        // Keep this first-step fallback for routes that enter ShineNrvGot
        // without issuing that call themselves.
        if (shine && al::isFirstStep(shine))
            commitForcedCompletedKingdomShine(shine);

        // The no-demo/already-collected counterpart of the sample in
        // exeWaitRequestDemo. Taken ahead of the repeat-Grand branch so the
        // borrowed payout below is covered too.
        if (cProfileMoonGetTimings && shine && al::isFirstStep(shine))
            sMoonGetProfileTouchTick = moonGetProfileTick();

        if (!shine || shine != sNormalRepeatGrandShine) {
            shineExeGotHook.orig(shine);
            return;
        }

        constexpr s32 cRiseFrames = 24;
        constexpr s32 cConvertFrames = 10;
        constexpr f32 cRiseDistance = 120.0f;

        const s32 step = al::getNerveStep(shine);
        al::LiveActor* visual = shine->mModelEmpty ? shine->mModelEmpty : static_cast<al::LiveActor*>(shine);

        if (al::isFirstStep(shine)) {
            sRepeatGrandStartTrans = al::getTrans(shine);
            calcUpDirAtMainOffset(&sRepeatGrandUpDir, shine);
            al::LiveActor* jingleEmitter = findRepeatJingleEmitter(shine);
            if (jingleEmitter)
                startSeAtMainOffset(jingleEmitter, "Got");
        }

        const f32 rate = step >= cRiseFrames ? 1.0f : static_cast<f32>(step) / static_cast<f32>(cRiseFrames);
        const f32 easedRate = 1.0f - (1.0f - rate) * (1.0f - rate);
        sead::Vector3f position;
        position.x = sRepeatGrandStartTrans.x + sRepeatGrandUpDir.x * cRiseDistance * easedRate;
        position.y = sRepeatGrandStartTrans.y + sRepeatGrandUpDir.y * cRiseDistance * easedRate;
        position.z = sRepeatGrandStartTrans.z + sRepeatGrandUpDir.z * cRiseDistance * easedRate;
        resetPositionAtMainOffset(shine, position);

        if (step == cRiseFrames) {
            startHitReactionAtMainOffset(visual, reinterpret_cast<const char*>(hk::ro::getMainModule()->range().start() + cShineEmptyConvertReactionOffset));
        }

        if (step >= cRiseFrames + cConvertFrames) {
            al::setNerve(shine, reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + cShineNrvCoinGetOffset));
        }
    });

    static HkTrampoline<bool, Shine*> multiMoonCutsceneHook = hk::hook::trampoline([](Shine* shine) -> bool {
        sNormalRepeatGrandShine = nullptr;
        // Fast moon demos are shortened by raw asm patches later in the StageScene grand-moon
        // flow. We still need the native request to run so the demo director registers the grand
        // shine and StageScene enters the proper ShineGrand sequence instead of falling straight
        // through to post-collection stage-change logic.
        const bool started = multiMoonCutsceneHook.orig(shine);
        if (started) {
            sGrandMoonDemoPending = true;
            // This is the actor the demo is supposed to animate; see
            // restoreGrandDemoShineActor.
            sGrandMoonDemoRequestShine = shine;
        }
        return started;
    });

    static HkTrampoline<void, Shine*> shineExeCoinHook = hk::hook::trampoline([](Shine* shine) -> void {
        const bool isRepeatGrand = shine && shine == sNormalRepeatGrandShine;
        // Repeat Grand rewards always take this payout. Everything else takes it
        // only when the native one would fault on a keeper-less emitter: a triple
        // reward demoted back to the normal get sequence keeps its grand model,
        // so it reaches native Shine::exeCoin as an ordinary moon whose empty
        // model has no ItemKeeper.
        if (!shine || (!isRepeatGrand && hasUsableNativeCoinEmitter(shine))) {
            shineExeCoinHook.orig(shine);
            return;
        }

        if (al::isFirstStep(shine)) {
            shine->_2F8 = 0;
            al::LiveActor* model = shine->mIsGotShine ? shine->mModelEmpty : shine->mModelShine;
            hideModelIfShowAtMainOffset(model ? model : static_cast<al::LiveActor*>(shine));
            Randomizer::logPush("%s coin payout: native emitter=%p borrowed emitter=%p", isRepeatGrand ? "Grand repeat" : "keeper-less",
                getNativeCoinEmitter(shine), findRepeatCoinEmitter(shine));
        }

        sead::Quatf quat;
        al::calcQuat(&quat, shine);
        const sead::Vector3f& trans = al::getTrans(shine);
        al::LiveActor* emitter = findRepeatCoinEmitter(shine);
        if (emitter) {
            if (tryAppearMultiCoinFromObjAtMainOffset(emitter, trans, quat, al::getNerveStep(shine), 100.0f))
                shine->_2F8++;
        } else if (al::getNerveStep(shine) % 10 == 0) {
            // Last-resort fallback for scenes without a normal demo Shine.
            // Increment one at a time so the HUD retains its normal count-up
            // cadence instead of jumping by five in a single frame.
            GameDataHolderAccessor accessor(static_cast<const al::IUseSceneObjHolder*>(shine));
            if (accessor.mData)
                GameDataFunction::addCoin(GameDataHolderWriter(accessor.mData), 1);
            shine->_2F8++;
        }

        if (shine->_2F8 >= 5) {
            al::validateClipping(shine);
            if (isRepeatGrand)
                sNormalRepeatGrandShine = nullptr;
            shine->makeActorDead();
        }
    });

    struct EventFlowChoiceInfoLayout {
        s32 selectedIndex;
        s32 cancelIndex;
        s32 registeredChoiceCount;
        s32 choiceCount;
        const char16_t** choiceMessages;
        const char16_t* question;
        bool isContinuePreSelectIndex;
    };

    static constexpr uintptr_t cGameDataFileFindShineOffset = 0x523d54;

    static al::LiveActor* getShineModelA(Shine* shine) {
        return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(shine) + 0x2e0);
    }

    static al::LiveActor* getShineModelB(Shine* shine) {
        return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(shine) + 0x2e8);
    }

    static al::LiveActor* getShineStandActor(Shine* shine) {
        return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(shine) + 0x2b0);
    }

    static constexpr s32 kShineTableMax = 256;
    struct ShineColorEntry {
        Shine* shine;
        s32 mclFrame;
        s32 destinationWorldId;
    };
    struct ActorColorEntry {
        al::LiveActor* actor;
        s32 mclFrame;
        bool allowFlaggedShineOrig;
    };
    static ShineColorEntry sShineColorTable[kShineTableMax];
    static s32 sShineColorCount = 0;
    static ActorColorEntry sActorColorTable[kShineTableMax * 4];
    static s32 sActorColorCount = 0;
    static al::LiveActor* sPowerStarActorTable[kShineTableMax * 3];
    static s32 sPowerStarActorCount = 0;
    static al::LiveActor* sMushroomShineActorTable[kShineTableMax * 3];
    static s32 sMushroomShineActorCount = 0;
    static al::LiveActor* sRandomizedShineActorTable[kShineTableMax * 3];
    static s32 sRandomizedShineActorCount = 0;
    static al::LiveActor* sWhiteMoonActorTable[kShineTableMax * 4] = {};
    static s32 sWhiteMoonActorCount = 0;
    static Shine* sStoryMoonDemoSource = nullptr;
    static const ShineDB::ShineEntry* sStoryMoonDemoItem = nullptr;
    static al::LiveActor* sStoryMoonDemoDisplayActor = nullptr;
    static al::LiveActor* sStoryMoonDemoActor = nullptr;
    static s32 sStoryMoonDemoColorFrame = -1;
    static bool sStoryMoonDemoHasExplicitItem = false;
    static s32 sStoryMoonDemoColorWriteLogCount = 0;
    static Shine* sGrandMoonDemoSource = nullptr;
    static al::LiveActor* sGrandMoonDemoActors[3] = {};
    static const ShineDB::ShineEntry* sGrandMoonDemoItem = nullptr;
    static s32 sGrandMoonDemoColorFrame = -1;
    static bool sGrandMoonDemoHasExplicitItem = false;
    static s32 sGrandMoonDemoColorWriteLogCount = 0;

    // Defined with the tables themselves further down; both hold pointers into
    // the scene heap and must die with the stage that filled them.
    static void clearMoonDemoActorTables();
    static void clearGetShineStockActors();
    static void clearStoryMoonDemoStockActor();

    void clearShineColorTable() {
        clearMoonDemoActorTables();
        sShineColorCount = 0;
        sActorColorCount = 0;
        sPowerStarActorCount = 0;
        sMushroomShineActorCount = 0;
        sRandomizedShineActorCount = 0;
        sWhiteMoonActorCount = 0;
        sStoryMoonDemoSource = nullptr;
        sStoryMoonDemoItem = nullptr;
        sStoryMoonDemoDisplayActor = nullptr;
        sStoryMoonDemoActor = nullptr;
        sStoryMoonDemoColorFrame = -1;
        sStoryMoonDemoHasExplicitItem = false;
        sStoryMoonDemoColorWriteLogCount = 0;
        sGrandMoonDemoSource = nullptr;
        sGrandMoonDemoActors[0] = nullptr;
        sGrandMoonDemoActors[1] = nullptr;
        sGrandMoonDemoActors[2] = nullptr;
        sGrandMoonDemoItem = nullptr;
        sGrandMoonDemoColorFrame = -1;
        sGrandMoonDemoHasExplicitItem = false;
        sGrandMoonDemoColorWriteLogCount = 0;
        sGrandMoonDemoPending = false;
        sGrandMoonDemoRequestShine = nullptr;
        clearMoonGetDemoContext();
        sMoonGetDemoSerial = 0;
        sMoonDemoNativeInvariantLogCount = 0;
        // State objects are rebuilt per scene and can land on the same address, so
        // the remembered stock actors must not survive a stage change.
        clearGetShineStockActors();
        clearStoryMoonDemoStockActor();
        sNormalRepeatGrandShine = nullptr;
        sRepeatGrandStartTrans = sead::Vector3f::zero;
        sRepeatGrandUpDir = sead::Vector3f::ey;
        sInitializedShineCount = 0;
        sCompletedKingdomShineInitContext = nullptr;
        sForcedCompletedKingdomShineCount = 0;
        std::memset(sForcedCompletedKingdomShines, 0, sizeof(sForcedCompletedKingdomShines));
    }

    static s32 getSavedKingdomMoonRequirement(GameDataFile* file, s32 worldId) {
        if (const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, worldId))
            return entry->requirement.moonCount;
        return RequirementRando::getRequirement(worldId);
    }

    static void shineColorTableSet(Shine* shine, s32 mclFrame, s32 destinationWorldId = -1) {
        if (!shine)
            return;
        for (s32 i = 0; i < sShineColorCount; i++) {
            if (sShineColorTable[i].shine == shine) {
                sShineColorTable[i].mclFrame = mclFrame;
                if (destinationWorldId >= 0)
                    sShineColorTable[i].destinationWorldId = destinationWorldId;
                return;
            }
        }
        if (sShineColorCount < kShineTableMax)
            sShineColorTable[sShineColorCount++] = {
                shine,
                mclFrame,
                destinationWorldId,
            };
    }

    static s32 shineColorTableGet(Shine* shine) {
        for (s32 i = 0; i < sShineColorCount; i++)
            if (sShineColorTable[i].shine == shine)
                return sShineColorTable[i].mclFrame;
        return -1;
    }

    static bool itemAwardsFillerCoins(const ShineDB::ShineEntry& entry);
    static bool itemAwardsHintTokens(const ShineDB::ShineEntry& entry);
    static bool isEligibleCompletedKingdomMoon(const ShineDB::ShineEntry* physical, const ShineDB::ShineEntry* dest);
    static s32 destWorld(const ShineDB::ShineEntry* dest);
    static const ShineDB::ShineEntry* findEntryFromShineInfo(const ShineInfo* shineInfo);
    static const ShineDB::ShineEntry* findDisplayEntryFromShineInfo(const ShineInfo* info);

    static void actorColorTableSet(al::LiveActor* actor, s32 mclFrame, bool allowFlaggedShineOrig = false) {
        if (!actor || mclFrame < 0)
            return;
        for (s32 i = 0; i < sActorColorCount; i++) {
            if (sActorColorTable[i].actor == actor) {
                sActorColorTable[i].mclFrame = mclFrame;
                sActorColorTable[i].allowFlaggedShineOrig = sActorColorTable[i].allowFlaggedShineOrig || allowFlaggedShineOrig;
                return;
            }
        }
        if (sActorColorCount < (s32)(sizeof(sActorColorTable) / sizeof(sActorColorTable[0])))
            sActorColorTable[sActorColorCount++] = { actor, mclFrame, allowFlaggedShineOrig };
    }

    static s32 actorColorTableGet(al::LiveActor* actor) {
        for (s32 i = 0; i < sActorColorCount; i++)
            if (sActorColorTable[i].actor == actor)
                return sActorColorTable[i].mclFrame;
        return -1;
    }

    static void actorColorTableClear(al::LiveActor* actor) {
        if (!actor)
            return;
        for (s32 i = 0; i < sActorColorCount; i++) {
            if (sActorColorTable[i].actor != actor)
                continue;
            sActorColorTable[i] = sActorColorTable[--sActorColorCount];
            return;
        }
    }

    // Moon Kingdom's ShineMoon/ShineDotMoon resources carry their finished
    // white material in the archive.  A negative color frame is not sufficient
    // to preserve it: native shine setup and later action changes can still
    // start the Color MCL on root, auxiliary, and reusable demo actors.  Track
    // the exact actors whose active reward must keep the archive material so all
    // of those writes can be suppressed without affecting other moon styles.
    static bool isWhiteMoonActor(const al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < sWhiteMoonActorCount; i++)
            if (sWhiteMoonActorTable[i] == actor)
                return true;
        return false;
    }

    static void unregisterWhiteMoonActor(al::LiveActor* actor) {
        if (!actor)
            return;
        for (s32 i = 0; i < sWhiteMoonActorCount; i++) {
            if (sWhiteMoonActorTable[i] != actor)
                continue;
            sWhiteMoonActorTable[i] = sWhiteMoonActorTable[--sWhiteMoonActorCount];
            sWhiteMoonActorTable[sWhiteMoonActorCount] = nullptr;
            return;
        }
    }

    static void registerWhiteMoonActor(al::LiveActor* actor) {
        if (!actor || isWhiteMoonActor(actor))
            return;
        if (sWhiteMoonActorCount < static_cast<s32>(sizeof(sWhiteMoonActorTable) / sizeof(sWhiteMoonActorTable[0]))) {
            sWhiteMoonActorTable[sWhiteMoonActorCount++] = actor;
        }
    }

    static void clearWhiteMoonColor(al::LiveActor* actor) {
        if (!actor || !actor->mModelKeeper || !actor->mModelKeeper->mAnimMcl)
            return;
        using Fn = void (*)(al::LiveActor*);
        mainOffsetFunction<Fn>(cAlClearMclAnimOffset)(actor);
    }

    static bool actorColorTableAllowsFlaggedShineOrig(al::LiveActor* actor) {
        for (s32 i = 0; i < sActorColorCount; i++)
            if (sActorColorTable[i].actor == actor)
                return sActorColorTable[i].allowFlaggedShineOrig;
        return false;
    }

    static void applyShineColor(al::LiveActor* actor, f32 frame) {
        if (!actor || !actor->mModelKeeper)
            return;
        if (actor->mModelKeeper->mAnimMcl)
            al::startMclAnimAndSetFrameAndStop(actor, "Color", frame);
    }

    static bool isShineActor(al::LiveActor* actor) {
        return actor && std::strcmp(typeid(*actor).name(), typeid(Shine).name()) == 0;
    }

    // A Shine's visible model is its selected child actor, not its LiveActor
    // root.  Keep the three presentation operations together so a demo puppet
    // never updates a root while its actual moon model remains hidden or paused.
    static void hideMoonDemoPresentation(al::LiveActor* actor) {
        if (!actor)
            return;
        if (isShineActor(actor)) {
            hideAllShineModelsAtMainOffset(static_cast<Shine*>(actor));
            return;
        }
        hideModelIfShowAtMainOffset(actor);
    }

    static void showMoonDemoPresentation(al::LiveActor* actor) {
        if (!actor)
            return;
        if (isShineActor(actor)) {
            showCurrentShineModelAtMainOffset(static_cast<Shine*>(actor));
            return;
        }
        showModelIfHideAtMainOffset(actor);
    }

    static al::LiveActor* getMoonDemoPresentationActionActor(al::LiveActor* actor) {
        if (!isShineActor(actor))
            return actor;
        al::LiveActor* model = getCurrentShineModelAtMainOffset(static_cast<Shine*>(actor));
        return model ? model : actor;
    }

    static bool isPowerStarActor(al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < sPowerStarActorCount; i++)
            if (sPowerStarActorTable[i] == actor)
                return true;
        return false;
    }

    static void registerPowerStarActor(al::LiveActor* actor) {
        if (!actor || isPowerStarActor(actor))
            return;
        if (sPowerStarActorCount < (s32)(sizeof(sPowerStarActorTable) / sizeof(sPowerStarActorTable[0])))
            sPowerStarActorTable[sPowerStarActorCount++] = actor;
    }

    static bool isMushroomShineActor(al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < sMushroomShineActorCount; i++)
            if (sMushroomShineActorTable[i] == actor)
                return true;
        return false;
    }

    static void registerMushroomShineActor(al::LiveActor* actor) {
        if (!actor || isMushroomShineActor(actor))
            return;
        if (sMushroomShineActorCount < (s32)(sizeof(sMushroomShineActorTable) / sizeof(sMushroomShineActorTable[0])))
            sMushroomShineActorTable[sMushroomShineActorCount++] = actor;
    }

    static bool isRandomizedShineActor(al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < sRandomizedShineActorCount; i++)
            if (sRandomizedShineActorTable[i] == actor)
                return true;
        return false;
    }

    static void registerRandomizedShineActor(al::LiveActor* actor) {
        if (!actor || isRandomizedShineActor(actor))
            return;
        if (sRandomizedShineActorCount < (s32)(sizeof(sRandomizedShineActorTable) / sizeof(sRandomizedShineActorTable[0])))
            sRandomizedShineActorTable[sRandomizedShineActorCount++] = actor;
    }

    static void registerMushroomShine(Shine* shine) {
        if (!shine)
            return;
        registerMushroomShineActor((al::LiveActor*)shine);
        registerMushroomShineActor(getShineStandActor(shine));
        registerMushroomShineActor(getShineModelA(shine));
        registerMushroomShineActor(getShineModelB(shine));
    }

    static void registerRandomizedShine(Shine* shine) {
        if (!shine)
            return;
        registerRandomizedShineActor((al::LiveActor*)shine);
        registerRandomizedShineActor(getShineStandActor(shine));
        registerRandomizedShineActor(getShineModelA(shine));
        registerRandomizedShineActor(getShineModelB(shine));
    }

    // Kingdom a reward is PRESENTED as: model/archive, color, Power-Star vs grand
    // handling, and the moon-get cutscene's kingdom name. After the moon->kingdom
    // redistribution a moon belongs to its assigned kingdom, so it looks and reads
    // as that kingdom (a moon moved into Mushroom shows as a Mushroom Power Star,
    // etc.) even though its home worldId never changes. Falls back to home.
    static s32 destWorld(const ShineDB::ShineEntry* dest) {
        if (!dest)
            return -1;
        const s32 assigned = MoonRando::getAssignedKingdomForEntry(dest);
        return assigned >= 0 ? assigned : dest->worldId;
    }

    static bool isMushroomDestination(const ShineDB::ShineEntry* dest) {
        return dest && destWorld(dest) == WorldMeta::WorldId_Mushroom;
    }

    static bool destPaysTriple(const ShineDB::ShineEntry* dest);

    static bool isPowerStarDestination(const ShineDB::ShineEntry* dest) {
        // A Mushroom moon reshuffled into a triple by Randomize Triple Moons is a
        // grand reward, not a Power Star, so it gets the grand presentation.
        return isMushroomDestination(dest) && !destPaysTriple(dest);
    }

    // Whether this reward item pays out as a triple/grand (gives 3, grand model
    // and get-sequence). Honors "Randomize Triple Moons": a reshuffled triple
    // pays out and looks grand even at a normally-single location, and a demoted
    // vanilla grand pays out as a single. Falls back to the vanilla flag when the
    // option is off.
    static bool destPaysTriple(const ShineDB::ShineEntry* dest) {
        return MoonRando::isRewardMultiEntry(dest);
    }

    // WorldMeta supplies the custom ShineGrand<kingdom> archive where romfs has
    // one. Bowsers, Moon, and Darker have no matching custom archive, so their
    // null grand entry still falls through to the stock global ShineGrand model.
    // This keeps a triple reward visibly grand even for a kingdom without a
    // supplied skin.
    static const char* resolveMoonArchive(s32 worldId, bool isGrand) {
        const char* archive = WorldMeta::getMoonArchiveForWorldId(worldId, isGrand);
        if (isGrand && archive == WorldMeta::getMoonArchiveForWorldId(worldId, false))
            return "ShineGrand";
        return archive ? archive : (isGrand ? "ShineGrand" : "ShineMoon");
    }

    static bool isToadetteMedalDestination(const ShineDB::ShineEntry* dest) {
        return dest && MoonRando::isToadetteMedalItem(dest->uid, dest->stage);
    }

    static bool usesPowerStarGetSequence(const ShineDB::ShineEntry* dest) {
        return isPowerStarDestination(dest) || isToadetteMedalDestination(dest);
    }

    // Kingdoms whose moons are a reward channel in their own right rather than
    // requirement fodder: Cap/Cloud/Moon pay coins or unlock paintings by their
    // use selector, Mushroom carries captures, Dark/Darker carry abilities. None
    // of them has a moon requirement to complete, so their moons never expire.
    static bool isSpecialRewardKingdom(s32 worldId) {
        switch (worldId) {
        case WorldMeta::WorldId_Cap:
        case WorldMeta::WorldId_Cloud:
        case WorldMeta::WorldId_Moon:
        case WorldMeta::WorldId_Mushroom:
        case WorldMeta::WorldId_Dark:
        case WorldMeta::WorldId_Darker:
            return true;
        default:
            return false;
        }
    }

    // Both key kinds present as a key. Subarea Keys landed after the Moon Rock
    // Key work and only picked up the list, tracker and get-message treatment,
    // so the physical moon - the one place the player actually meets the key -
    // still spawned as its kingdom's ordinary moon.
    //
    // They share the Moon Rock Key's archives rather than getting their own: a
    // fourth key archive would add another distinct model name to the draw
    // executor's fixed drawer buffer, which has no bounds check, and the two
    // kinds are already told apart by the row text, the get message and the
    // tracker column.
    static bool isKeyRewardItem(const ShineDB::ShineEntry* dest) {
        return dest && (MoonRando::isMoonRockKeyItem(dest->uid, dest->stage) || MoonRando::isSubareaKeyItem(dest->uid, dest->stage));
    }

    // True when the reward is safe to spawn in the native already-collected
    // presentation after its credited kingdom reaches the requirement.
    //
    // Physical story/multi locations are excluded by their location identity.
    // Reward-level progression or special presentation is also excluded:
    // capture and ability unlocks, fool moons, keys of either kind, Toadette
    // Medals, Cloud painting links, coin rewards, and the special kingdoms
    // above. A key handed to the player as already-collected reads as scenery
    // they can skip, when it is the one thing in the kingdom they cannot.
    static bool isEligibleCompletedKingdomMoon(const ShineDB::ShineEntry* physical, const ShineDB::ShineEntry* dest) {
        // The eligibility decision is independent of the trigger. Keep the
        // diagnostics scoped to the option so normal play stays quiet.
        const bool logging = Randomizer::isCompletedKingdomMoonCollectedEnabled();
        if (!dest || !physical) {
            if (logging)
                Randomizer::logPush("CKM item skip: physical=%p dest=%p", physical, dest);
            return false;
        }

        const s32 kingdom = destWorld(dest);
        const bool badKingdom = kingdom < 0 || kingdom >= WorldMeta::sWorldCount || isSpecialRewardKingdom(kingdom);
        const bool capture = CaptureRando::getCaptureCountForMoon(*dest) > 0;
        const bool ability = AbilityRando::getAbilityCountForMoon(*dest) > 0;
        const bool fool = CaptureRando::isFoolMoon(*dest) || AbilityRando::isFoolMoon(*dest);
        const bool painting = PaintingRando::getCloudMoonPaintingUnlockCount(*dest) > 0;
        const bool key = isKeyRewardItem(dest);
        const bool medal = (physical->flags & ShineDB::ShineFlag_Toadette) != 0 || (dest->flags & ShineDB::ShineFlag_Toadette) != 0 || MoonRando::isToadetteMedalItem(dest->uid, dest->stage);
        const bool story = (physical->flags & ShineDB::ShineFlag_Story) != 0 || (dest->flags & ShineDB::ShineFlag_Story) != 0;
        const bool triple = destPaysTriple(dest);
        const bool multi = MoonRando::isMultiMoonLocation(physical->uid, physical->stage);
        const bool currencyReward = itemAwardsFillerCoins(*dest) || itemAwardsHintTokens(*dest);
        const bool inert = !badKingdom && !capture && !ability && !fool && !painting && !key && !medal && !story && !triple && !multi && !currencyReward;
        if (logging)
            Randomizer::logPush("CKM item %s -> %s N%d inert=%d [kingdom=%d cap=%d abil=%d fool=%d paint=%d key=%d "
                                "medal=%d story=%d triple=%d multi=%d currency=%d]",
                physical->name ? physical->name : "?", dest->name ? dest->name : "?", kingdom, inert, badKingdom, capture, ability, fool, painting,
                key, medal, story, triple, multi, currencyReward);
        return inert;
    }

    static void registerPowerStarShine(Shine* shine) {
        if (!shine)
            return;
        registerMushroomShine(shine);
        registerPowerStarActor((al::LiveActor*)shine);
        registerPowerStarActor(getShineStandActor(shine));
        registerPowerStarActor(getShineModelA(shine));
        registerPowerStarActor(getShineModelB(shine));
    }

    // A Moon Rock Key's visible color identifies the rock it opens.  Use that
    // target kingdom directly instead of relying on the carrier moon's current
    // roster assignment; the two normally agree, but the key identity remains
    // authoritative across allocation modes and future carrier changes.
    static s32 getMoonColorWorld(const ShineDB::ShineEntry* dest) {
        if (!dest)
            return -1;
        const s32 keyKingdom = MoonRando::getMoonRockKeyKingdomForItem(dest->uid, dest->stage);
        return keyKingdom >= 0 ? keyKingdom : destWorld(dest);
    }

    static s32 getMoonColorFrame(const ShineDB::ShineEntry* dest) {
        if (!dest)
            return -1;
        // Toadette Medals use their own archive material, like Power Stars.
        if (isToadetteMedalDestination(dest))
            return -1;
        // Vanilla Power Stars use frame 99, which intentionally leaves the
        // PowerStar archive's own yellow material alone.
        const s32 colorWorld = getMoonColorWorld(dest);
        if (colorWorld == WorldMeta::WorldId_Moon || isPowerStarDestination(dest))
            return -1;
        return WorldMeta::getMclFrameForWorldId(colorWorld);
    }

    // Story/grand collection states use reusable generic Shine actors, not the
    // destination archive installed on the physical world actor. They therefore
    // need the destination kingdom's MCL frame, except Moon Kingdom, whose
    // white destination material must be left untouched.
    static s32 getMoonDemoColorFrame(const ShineDB::ShineEntry* dest) {
        if (!dest)
            return -1;
        if (isToadetteMedalDestination(dest))
            return -1;
        // The physical ShineMoon archive is white. Its generic get-demo actor
        // must retain that material too; forcing frame 27 here makes it orange.
        //
        // Power Stars need the same exemption for the same reason getMoonColorFrame
        // gives them one: their archive's yellow material is the finished look and
        // vanilla addresses it with frame 99, so the kingdom frame is meaningless
        // here. Mushroom's frame is 0, and forcing it turned the star green partway
        // through the ordinary get animation while the world model stayed yellow.
        const s32 colorWorld = getMoonColorWorld(dest);
        if (colorWorld == WorldMeta::WorldId_Moon || isPowerStarDestination(dest))
            return -1;
        return WorldMeta::getMclFrameForWorldId(colorWorld);
    }

    static bool keepsNativeMoonDemoColor(const ShineDB::ShineEntry* dest) {
        return dest && getMoonColorWorld(dest) == WorldMeta::WorldId_Moon;
    }

    // dest->flags alone can't tell whether THIS actor is actually showing as
    // a Power Star: a linked shine forced away from Grand (see
    // sInsideLinkedShineInit) still has dest->flags & ShineFlag_Multi set
    // (the true reward), even though it's displaying Mushroom Kingdom's
    // "PowerStar" archive. isPowerStarActor is the authoritative per-instance
    // signal for what's actually loaded -- applying a moon-style MCL color
    // frame onto a PowerStar's different material layout produces garbage.
    static void applyMoonColor(al::LiveActor* actor, const ShineDB::ShineEntry* dest, bool allowFlaggedShineOrig = false) {
        if (isPowerStarActor(actor))
            return;
        const s32 mclFrame = getMoonColorFrame(dest);
        if (mclFrame < 0)
            return;
        actorColorTableSet(actor, mclFrame, allowFlaggedShineOrig);
        applyShineColor(actor, mclFrame);
    }

    static void registerMoonColor(al::LiveActor* actor, const ShineDB::ShineEntry* dest, bool allowFlaggedShineOrig = false) {
        if (isPowerStarActor(actor))
            return;
        const s32 mclFrame = getMoonColorFrame(dest);
        if (mclFrame < 0)
            return;
        actorColorTableSet(actor, mclFrame, allowFlaggedShineOrig);
    }

    static void applyMoonColor(Shine* shine, const ShineDB::ShineEntry* dest, bool allowFlaggedShineOrig = false) {
        if (!shine || isPowerStarActor((al::LiveActor*)shine))
            return;
        const s32 mclFrame = getMoonColorFrame(dest);
        shineColorTableSet(shine, mclFrame, dest ? destWorld(dest) : -1);
        if (mclFrame < 0)
            return;
        applyMoonColor((al::LiveActor*)shine, dest, allowFlaggedShineOrig);
        applyMoonColor(getShineStandActor(shine), dest, allowFlaggedShineOrig);
        applyMoonColor(getShineModelA(shine), dest, allowFlaggedShineOrig);
        applyMoonColor(getShineModelB(shine), dest, allowFlaggedShineOrig);
    }

    static void registerRootShineColor(Shine* shine, const ShineDB::ShineEntry* dest, bool allowFlaggedShineOrig) {
        if (!shine || isPowerStarActor((al::LiveActor*)shine))
            return;
        const s32 mclFrame = getMoonColorFrame(dest);
        shineColorTableSet(shine, mclFrame, dest ? destWorld(dest) : -1);
        if (mclFrame < 0)
            return;
        actorColorTableSet((al::LiveActor*)shine, mclFrame, allowFlaggedShineOrig);
        applyShineColor((al::LiveActor*)shine, mclFrame);
    }

    static HkTrampoline<void, al::LiveActor*, const char*, s32, bool> setStageShineAnimFrameHook = hk::hook::trampoline([](al::LiveActor* actor, const char* stageName, s32 arg, bool flag) -> void {
        if (isPowerStarActor(actor))
            return;
        if (isWhiteMoonActor(actor))
            return;
        const bool isShine = isShineActor(actor);
        const s32 mclFrame = actorColorTableGet(actor);
        if (mclFrame >= 0) {
            setStageShineAnimFrameHook.orig(actor, stageName, mclFrame, false);
            if (flag && al::isMtpAnimExist(actor, "Color"))
                setStageShineAnimFrameHook.orig(actor, stageName, mclFrame, true);
            applyShineColor(actor, mclFrame);
            return;
        }
        if (isShine && ((Shine*)actor)->mIsPowerStar)
            return;
        if (flag && isShine)
            return;
        // A presentation actor with no registered frame is one we chose
        // to leave on its archive's own material (a Power Star's yellow,
        // Moon Kingdom's white, a medal's own colours). Native reads that
        // as "no frame given" and resolves the kingdom the player happens
        // to be standing in instead -- StageSceneStateGetShine::exeDemoGet
        // passes exactly that, frame -1, on the slot it just latched.
        if (isCustomMoonDemoActor(actor))
            return;
        setStageShineAnimFrameHook.orig(actor, stageName, arg, flag);
    });

    static const char* getCurrentStageName() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return nullptr;
        HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
        if (!seq->mGameDataHolderAccessor)
            return nullptr;
        return GameDataFunction::getCurrentStageName(seq->mGameDataHolderAccessor);
    }

    static constexpr const char* cSnowGrandPrixRaceStage = "SnowWorldLobby000Stage";
    static constexpr const char* cSnowGrandPrixSourceStage = "SnowWorldLobby001Stage";
    static constexpr const char* cSnowGrandPrixObjId = "obj331";

    static bool isSnowGrandPrixPlacement(const char* stage, const char* objId) {
        return stage && objId && std::strcmp(stage, cSnowGrandPrixRaceStage) == 0 && std::strcmp(objId, cSnowGrandPrixObjId) == 0;
    }

    // The Grand Prix reward is authored in race lobby 000, while its canonical
    // ShineInfo/save identity is lobby 001 + obj331.  The map patch gives both
    // the race-linked reward and its later standing version that canonical objId;
    // resolving their stage here makes them one physical location for placement,
    // save ownership, repeat collection, and Snow scenario progression.
    static HkTrampoline<const char*, GameDataHolderAccessor, const al::ActorInitInfo&> getPlacementStageNameHook = hk::hook::trampoline([](GameDataHolderAccessor accessor, const al::ActorInitInfo& info) -> const char* {
        const char* stage = getPlacementStageNameHook.orig(accessor, info);
        if (!stage || std::strcmp(stage, cSnowGrandPrixRaceStage) != 0)
            return stage;

        al::PlacementId id;
        al::getPlacementId(&id, info);
        if (!isSnowGrandPrixPlacement(stage, id.getId())) {
            return stage;
        }

        return cSnowGrandPrixSourceStage;
    });

    static GameDataFile* tryGetCurrentGameDataFile();

    static bool tryMakePlacementMoonKey(al::StringTmp<128>* objId, al::StringTmp<128>* stage, const al::ActorInitInfo& info) {
        al::PlacementId id;
        al::getPlacementId(&id, info);
        al::StringTmp<128> placement = al::makeStringPlacementId(&id);

        const char* placementStr = placement.cstr();
        const char* parenOpen = std::strchr(placementStr, '(');
        const char* bracketOpen = parenOpen ? std::strchr(parenOpen, '[') : nullptr;

        if (parenOpen)
            objId->format("%.*s", (s32)(parenOpen - placementStr), placementStr);
        else
            objId->format("%s", placementStr);

        if (parenOpen && bracketOpen)
            stage->format("%.*s", (s32)(bracketOpen - parenOpen - 1), parenOpen + 1);
        else {
            const char* currentStage = getCurrentStageName();
            if (!currentStage || !currentStage[0])
                return false;
            stage->format("%s", currentStage);
        }

        // Keep the randomizer's pre-init archive/reward lookup on the same
        // canonical identity as the vanilla ShineInfo lookup hooked above.
        if (isSnowGrandPrixPlacement(stage->cstr(), objId->cstr()))
            stage->format("%s", cSnowGrandPrixSourceStage);

        return !objId->isEmpty() && !stage->isEmpty();
    }

    static s32 parseShineObjId(const char* objId) {
        if (!objId || objId[0] != 'o' || objId[1] != 'b' || objId[2] != 'j')
            return -1;
        s32 objNum = 0;
        const char* p = objId + 3;
        if (*p == '\0')
            return -1;
        while (*p >= '0' && *p <= '9')
            objNum = objNum * 10 + (*p++ - '0');
        return *p == '\0' ? objNum : -1;
    }

    struct ShopMoonDisplayInfo {
        const GameDataFile::HintInfo* hint;
        const ShineDB::ShineEntry* physical;
        const ShineDB::ShineEntry* dest;
    };

    static ShopMoonDisplayInfo sPendingShopPreviewDisplayInfo = {};

    static const ShineDB::ShineEntry* getShopMoonDisplayEntry(const ShopMoonDisplayInfo& info) {
        return info.dest ? info.dest : info.physical;
    }

    static const GameDataFile::HintInfo* findShopHintForStage(GameDataFile* file, const char* stageName) {
        if (!file || !stageName || !stageName[0])
            return nullptr;

        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if (!hint->isShop || hint->stageName.isEmpty() || hint->objId.isEmpty())
                continue;
            if (std::strcmp(hint->stageName.cstr(), stageName) == 0)
                return hint;
        }

        return nullptr;
    }

    static const ShineDB::ShineEntry* findPhysicalMoonFromHint(const GameDataFile::HintInfo* hint) {
        if (!hint || hint->stageName.isEmpty())
            return nullptr;

        if (!hint->objId.isEmpty()) {
            const s32 objNum = parseShineObjId(hint->objId.cstr());
            if (objNum >= 0)
                return ShineDB::findEntry(ShineDB::findUid(hint->stageName.cstr(), objNum), hint->stageName.cstr());
        }

        return ShineDB::findEntry(hint->uniqueId, hint->stageName.cstr());
    }

    static ShopMoonDisplayInfo findShopMoonDisplayInfoForStage(GameDataFile* file, const char* stageName) {
        ShopMoonDisplayInfo info = { nullptr, nullptr, nullptr };
        info.hint = findShopHintForStage(file, stageName);
        if (!info.hint)
            return info;

        info.physical = findPhysicalMoonFromHint(info.hint);
        if (MoonRando::isInitialized()) {
            info.dest = MoonRando::getCreditMoon(info.hint->objId.cstr(), info.hint->stageName.cstr());
            // Credit-map lookup is the normal fast path.  The resolved
            // physical entry also gives an exact placement key, so use it as
            // a fallback and keep the shop text, preview texture, and preview
            // model on the same randomized item.
            if (!info.dest && info.physical)
                info.dest = MoonRando::getItemForLocation(info.physical->uid, info.physical->stage);
        }
        if (!info.dest)
            info.dest = info.physical;
        return info;
    }

    static ShopMoonDisplayInfo findCurrentShopMoonDisplayInfo() {
        return findShopMoonDisplayInfoForStage(tryGetCurrentGameDataFile(), getCurrentStageName());
    }

    static bool isPendingShopPreviewDisplayActive() {
        return sPendingShopPreviewDisplayInfo.dest != nullptr;
    }

    static const char* getShopMoonTextureSuffix(const char* fallbackSuffix) {
        const ShopMoonDisplayInfo shopInfo = findCurrentShopMoonDisplayInfo();
        const ShineDB::ShineEntry* displayEntry = getShopMoonDisplayEntry(shopInfo);
        if (!displayEntry)
            return fallbackSuffix;

        // A Toadette Medal keeps its own icon whatever kingdom it is credited to,
        // matching the ShineToken model the shop preview shine already spawns
        // with (getMoonArchiveForShopDisplay). The suffix reaches
        // ShopLayoutInfo::loadTexture's `format("Moon%s", ...)`
        // (ida_context/Layout/ShopLayoutInfo.md), so "Token" selects the
        // MoonToken texture added to ObjectData/TextureShine.
        if (isToadetteMedalDestination(displayEntry))
            return "Token";

        switch (destWorld(displayEntry)) {
        case WorldMeta::WorldId_Cap:
            return "Cap";
        case WorldMeta::WorldId_Cascade:
            return "Waterfall";
        case WorldMeta::WorldId_Sand:
            return "Sand";
        case WorldMeta::WorldId_Wooded:
            return "Forest";
        case WorldMeta::WorldId_Lake:
            return "Lake";
        case WorldMeta::WorldId_Cloud:
            return "Cloud";
        case WorldMeta::WorldId_Lost:
            return "Clash";
        case WorldMeta::WorldId_Metro:
            return "City";
        case WorldMeta::WorldId_Seaside:
            return "Sea";
        case WorldMeta::WorldId_Snow:
            return "Snow";
        case WorldMeta::WorldId_Luncheon:
            return "Lava";
        case WorldMeta::WorldId_Ruined:
            return "Attack";
        case WorldMeta::WorldId_Bowsers:
            return "Sky";
        case WorldMeta::WorldId_Moon:
            return "Moon";
        case WorldMeta::WorldId_Mushroom:
            return "Peach";
        case WorldMeta::WorldId_Dark:
            return "Special1";
        case WorldMeta::WorldId_Darker:
            return "Special2";
        default:
            return fallbackSuffix;
        }
    }

    static uintptr_t createShopLayoutInfoForCurrentMoon(void* shopLayoutInfo, const void* itemList, int itemCount, const char* worldDevelopName) {
        using Fn = uintptr_t (*)(void*, const void*, int, const char*);
        static constexpr uintptr_t cShopLayoutInfoCtorOffset = 0x207860;
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cShopLayoutInfoCtorOffset)(shopLayoutInfo, itemList, itemCount,
            getShopMoonTextureSuffix(worldDevelopName));
    }

    static const char* getMoonArchiveForShopDisplay(const ShineDB::ShineEntry* dest) {
        if (!dest)
            return nullptr;

        const bool isGrand = destPaysTriple(dest);
        if (isKeyRewardItem(dest))
            return isGrand ? cMoonRockKeyGrandArchive : cMoonRockKeyArchive;
        if (isToadetteMedalDestination(dest))
            return isGrand ? cToadetteMedalGrandArchive : cToadetteMedalArchive;

        // The Dark/Darker shop preview normally uses generic ShineMoon because
        // those kingdoms' ordinary archive lacks the shop presentation. Keep
        // that established single-moon behavior, but use its custom grand skin
        // when the reward is a triple.
        if (destWorld(dest) == WorldMeta::WorldId_Dark || destWorld(dest) == WorldMeta::WorldId_Darker)
            return isGrand ? resolveMoonArchive(destWorld(dest), true) : "ShineMoon";

        return resolveMoonArchive(destWorld(dest), isGrand);
    }

    static bool isShopShineInit(const Shine* shine) {
        if (!shine)
            return false;

        // Shine::setShopShine() sets a private byte at 0x2A8 before Shine::init runs.
        return reinterpret_cast<const u8*>(shine)[0x2A8] != 0;
    }

    static const ShineDB::ShineEntry* findPhysicalMoonFromKey(const char* objId, const char* stage) {
        s32 objNum = parseShineObjId(objId);
        if (objNum < 0 || !stage)
            return nullptr;
        return ShineDB::findEntry(ShineDB::findUid(stage, objNum), stage);
    }

    static const ShineDB::ShineEntry* findDisplayMoonFromKey(const char* objId, const char* stage) {
        if (MoonRando::isInitialized()) {
            if (const ShineDB::ShineEntry* mapped = MoonRando::getCreditMoon(objId, stage))
                return mapped;
        }

        const ShineDB::ShineEntry* physical = findPhysicalMoonFromKey(objId, stage);
        return physical && AbilityRando::isAbilityMoon(*physical) ? physical : nullptr;
    }

    static const ShineDB::ShineEntry* findDestMoonFromInitInfo(const al::ActorInitInfo& info) {
        al::StringTmp<128> objId;
        al::StringTmp<128> stage;
        if (!tryMakePlacementMoonKey(&objId, &stage, info))
            return nullptr;

        return findDisplayMoonFromKey(objId.cstr(), stage.cstr());
    }

    static const ShineDB::ShineEntry* findDestMoonFromShineInfo(const ShineInfo* shineInfo) {
        if (!shineInfo || shineInfo->isEmpty())
            return nullptr;

        return findDisplayMoonFromKey(shineInfo->mObjId.cstr(), shineInfo->mStageName.cstr());
    }

    static const ShineDB::ShineEntry* findEntryFromShineInfo(const ShineInfo* shineInfo);
    static const ShineDB::ShineEntry* findDisplayEntryFromShineInfo(const ShineInfo* info);

    // A vanilla grand slot: the physical location authors its own Grand model and
    // get-sequence, so its presentation is retail's and must never be second
    // guessed by the capability check below.
    static bool isVanillaGrandLocation(const Shine* shine) {
        const ShineDB::ShineEntry* physical = shine && shine->curShineInfo ? findEntryFromShineInfo(shine->curShineInfo) : nullptr;
        return physical && (physical->flags & ShineDB::ShineFlag_Multi) != 0;
    }

    static bool isExistActionAtMainOffset(const al::LiveActor* actor, const char* action) {
        if (!actor || !action)
            return false;
        using Fn = bool (*)(const al::LiveActor*, const char*);
        return mainOffsetFunction<Fn>(cAlIsExistActionOffset)(actor, action);
    }

    static bool isActionPlayingAtMainOffset(const al::LiveActor* actor, const char* action) {
        if (!actor || !action)
            return false;
        using Fn = bool (*)(const al::LiveActor*, const char*);
        return mainOffsetFunction<Fn>(cAlIsActionPlayingOffset)(actor, action);
    }

    static const char* getActionNameAtMainOffset(const al::LiveActor* actor) {
        if (!actor)
            return nullptr;
        using Fn = const char* (*)(const al::LiveActor*);
        return mainOffsetFunction<Fn>(cAlGetActionNameOffset)(actor);
    }

    static f32 getActionFrameAtMainOffset(const al::LiveActor* actor) {
        if (!actor)
            return -1.0f;
        using Fn = f32 (*)(const al::LiveActor*);
        return mainOffsetFunction<Fn>(cAlGetActionFrameOffset)(actor);
    }

    // al::getActionFrameMax. The offset is deliberately function-local: the grand
    // demo declares its own constant for the same address further down, and this
    // file must not end up with two namespace-scope names for it.
    static f32 getActionFrameMaxAtMainOffset(const al::LiveActor* actor, const char* action) {
        if (!actor || !action)
            return -1.0f;
        static constexpr uintptr_t cGetActionFrameMaxOffset = 0x008d3400;
        using Fn = f32 (*)(const al::LiveActor*, const char*);
        return mainOffsetFunction<Fn>(cGetActionFrameMaxOffset)(actor, action);
    }

    static const char* getShineDemoArchive(Shine* shine);
    static bool isNativeGrandDemoSource(const Shine* shine);
    static bool isGrandMoonPresentationArchive(const char* archiveName);

    // Every triple reward resolves to a ShineGrand* archive. WorldMeta selects a
    // kingdom skin when one is supplied and resolveMoonArchive falls back to the
    // global model for the remaining worlds. Moon Rock Keys and Toadette Medals
    // likewise use their supplied ShineGrand* form only when they pay triple.
    static bool hasNonGrandSwappedArchive(Shine* shine) {
        const char* archive = getShineDemoArchive(shine);
        return archive && !isGrandMoonPresentationArchive(archive);
    }

    // Kept as the record of why a swapped model used to be able to strand the
    // grand cutscene: exeDemoGetFirst starts "GetShineGrand" on the pickup itself
    // and waits on al::isActionEnd, so a model without that action never let the
    // step finish -- the timer-challenge softlock. Shines built by a host object
    // at runtime are the awkward case, since they reach Shine::init with the
    // host's ActorInitInfo and resolve their reward only later from the ShineInfo.
    // The wait is now ours rather than the model's; see grandDemoRiseIsActionEndHook.
    //
    // (Dot shines stay a genuine hazard for anything that would retype them:
    // Shine::control skips its rotate/"Spin"-effect block while mModelType == Dot,
    // and running that block against a ShineDot model dies in al::tryDeleteEffect.)

    static void applyShineDestinationState(Shine* shine, const ShineDB::ShineEntry* dest, bool registerActors = true, bool beforeModelInit = false) {
        if (!shine || !dest)
            return;

        // Shine::exeWaitRequestDemo selects the get sequence from mModelType, and
        // that value belongs to the location: a triple slot plays the triple
        // animation, a story slot its story animation, an ordinary slot the
        // ordinary one, whatever reward is sitting there. The reward decides the
        // model instead -- the archive swap is keyed on destPaysTriple and the
        // destination kingdom -- so a triple randomized onto an ordinary slot
        // plays that slot's animation holding the grand model, and a single moon
        // on a vanilla triple slot plays the triple animation holding the single
        // model. Nothing here promotes or demotes from the reward.
        //
        // Keeping a grand slot grand used to mean risking a softlock, because
        // exeDemoGetFirst waits on a "GetShineGrand" action a swapped-in model may
        // not have. That wait is now ours (grandDemoRiseIsActionEndHook), and the
        // later phases animate a bound presentation actor rather than the pickup
        // (bindGrandMoonDemoActor), so no model can strand the sequence any more.

        shine->mIsPowerStar = usesPowerStarGetSequence(dest);
        if (!registerActors)
            return;

        registerRandomizedShine(shine);
        if (isMushroomDestination(dest))
            registerMushroomShine(shine);
        if (shine->mIsPowerStar)
            registerPowerStarShine(shine);
    }

    static bool isMoonArchiveName(const char* archiveName) {
        return archiveName && (std::strcmp(archiveName, "PowerStar") == 0 || std::strcmp(archiveName, "PowerStarEmpty") == 0 || al::isEqualSubString(archiveName, "Shine"));
    }

    // Probes whether ObjectData/<archiveName>.szs is present in romfs, using the
    // game's own al::isExistArchive (not exported in syms, so reached by offset).
    static bool moonArchiveExists(const char* archiveName) {
        if (!archiveName || !archiveName[0])
            return false;
        using Fn = bool (*)(const sead::SafeString&);
        static constexpr uintptr_t cIsExistArchiveOffset = 0x00897ee0;
        al::StringTmp<128> resourcePath("ObjectData/%s", archiveName);
        Fn fn = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cIsExistArchiveOffset);
        return fn(resourcePath);
    }

    // Resolved Dot names are interned so a returned pointer stays valid for as
    // long as any caller holds it. The set of Dot archives is small and fixed
    // (one per world plus the special presentations), so a slot per distinct
    // name costs nothing.
    //
    // This used to be one shared static scratch buffer, which is only good
    // until the next call -- and initActorWithArchiveNameHook holds the result
    // across registerShineDemoArchive, which builds the moon-demo registry
    // (rebuilt once per stage load) by resolving its own entries through here.
    // The moon that triggered that rebuild was left pointing at the registry's
    // last probe, "ShineDotPlanet". Nothing ships that archive, so al::Resource
    // is constructed with a null ArchiveRes, al::ResourceSystem::createResource
    // returns null for it, and al::findOrCreateActorResource passes that null
    // straight to al::Resource::tryGetByml, which dereferences it -- Shine::init
    // died there on any stage whose first randomized moon was a 2D placement.
    // The romfs probe below cannot catch that: it runs before the rewrite, on
    // the name the caller actually asked for.
    static constexpr s32 cDotArchiveNameMax = 32;
    static char sDotArchiveNames[cDotArchiveNameMax][64];
    static s32 sDotArchiveNameCount = 0;

    static const char* internDotArchiveName(const char* name) {
        for (s32 i = 0; i < sDotArchiveNameCount; i++) {
            if (std::strcmp(sDotArchiveNames[i], name) == 0)
                return sDotArchiveNames[i];
        }
        if (sDotArchiveNameCount >= cDotArchiveNameMax) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: dot archive intern table full archive=%s", name);
            return "ShineDot";
        }
        char* slot = sDotArchiveNames[sDotArchiveNameCount++];
        std::strncpy(slot, name, sizeof(sDotArchiveNames[0]) - 1);
        slot[sizeof(sDotArchiveNames[0]) - 1] = '\0';
        return slot;
    }

    static const char* makeDotArchiveName(const char* archive, s32 worldId) {
        if (!archive)
            return archive;

        if (worldId == WorldMeta::WorldId_Mushroom)
            return "ShineDotPeach";

        if (std::strncmp(archive, "Shine", 5) == 0) {
            char candidate[64];
            std::snprintf(candidate, sizeof(candidate), "ShineDot%s", archive + 5);
            if (!moonArchiveExists(candidate))
                return "ShineDot";
            return internDotArchiveName(candidate);
        }
        return archive;
    }

    static bool isDotArchiveName(const char* archiveName) {
        return archiveName && std::strstr(archiveName, "Dot") != nullptr;
    }

    // rs::tryInitLinkShine (main offset 0x00555890) is the vanilla path that
    // creates a Shine "linked" into a host object (FrailBox, ReactionObject,
    // SneakingMan, ...) -- a moon hidden inside a container, which vanilla
    // never expects to be a Grand/Triple-Moon. Moon Rando can still credit
    // one there (e.g. under Triple Moons Anywhere), and the resulting
    // archive/actor-type swap overflowed the per-stage SceneHeap (confirmed:
    // FrailBox in Sand Kingdom crediting a Mushroom Kingdom Grand moon).
    // Scope this flag around the synchronous tryInitLinkShine ->
    // initLinksActor -> Shine::init -> archive-load call chain so
    // initActorWithArchiveNameHook below can skip the destination archive
    // swap entirely for any linked shine, regardless of what's credited
    // there -- it keeps the vanilla archive for this exact physical
    // placement (never anything exotic like a kingdom's "PowerStar" slot).
    // The reward/uid bookkeeping is untouched; only the shown model is
    // capped to the safe, physical-location default.
    //
    // cForceLinkedShineToNonGrand is the single on/off switch for that
    // fallback -- both initActorWithArchiveNameHook's isGrand computation
    // and shineInitHook's Power-Star-registry registration below key off
    // it together, so they can't drift out of sync while testing whether
    // sceneHeapCreateHook's SceneHeap increase is enough to let ShineGrand
    // load safely (currently false = testing +4MB with Grand allowed;
    // flip to true to fall back to the confirmed crash-free PowerStar
    // presentation for linked shines).
    static constexpr bool cForceLinkedShineToNonGrand = false;
    static bool sInsideLinkedShineInit = false;
    static HkTrampoline<Shine*, const al::ActorInitInfo&, const char*, int> tryInitLinkShineHook = hk::hook::trampoline([](const al::ActorInitInfo& info, const char* linkName, int index) -> Shine* {
        const bool previous = sInsideLinkedShineInit;
        sInsideLinkedShineInit = true;
        Shine* shine = tryInitLinkShineHook.orig(info, linkName, index);
        sInsideLinkedShineInit = previous;
        return shine;
    });

    // Keep normalization storage with the caller.  A previous shared static
    // buffer made registry lookup re-enter itself while deduplicating an archive
    // and could compare a buffer that had just been overwritten.
    static bool copyCustomMoonBaseArchive(const char* archiveName, char* out, size_t outSize) {
        if (!archiveName || !out || outSize == 0)
            return false;

        std::strncpy(out, archiveName, outSize - 1);
        out[outSize - 1] = '\0';
        const size_t len = std::strlen(out);
        if (len > 5 && std::strcmp(out + len - 5, "Empty") == 0)
            out[len - 5] = '\0';
        return out[0] != '\0';
    }

    static constexpr u8 cMoonDemoCapabilityNormal = 1 << 0;
    static constexpr u8 cMoonDemoCapabilityDot = 1 << 1;
    static constexpr u8 cMoonDemoCapabilitySpecial = 1 << 2;
    static constexpr u8 cMoonDemoCapabilityStory = 1 << 3;
    static constexpr u8 cMoonDemoCapabilityGrand = 1 << 4;

    // Which family of model the GET CUTSCENE shows when the reward's family and
    // the location's disagree.
    //
    // Only the matching family carries a route's clips -- ShineGrand* has the
    // four DemoGetShineGrand* phases and nothing else, every ordinary Shine* has
    // the GetShine/DemoGetShine set and nothing else -- and a clip from the wrong
    // family cannot stand in, because each one drives its own root motion on the
    // shared AllRoot/Ground bones for its own cutscene (see
    // cUseCrossFamilyActionAlias). So the choice is genuinely either the reward's
    // model with no get animation, or the location's model with the real one.
    //
    // Both settings were tried in-game. Keeping the reward's model leaves it with
    // no clip for its location's route, so it holds its idle for the whole
    // cutscene; following the location plays the real choreography but visibly
    // swaps the moon for those few seconds -- a triple on an ordinary slot
    // presents as a single, a single on a vanilla grand slot as a triple.
    //
    // true = follow the location. Chosen because a cutscene that animates reads
    // better than a correct-but-inert moon, and because the swap is confined to
    // the presentation actor: the world model before pickup, the moon count, the
    // tracker and the result text all still come from the reward, and the
    // presentation actor is still recoloured to the reward's kingdom, so the
    // reward's identity is never actually lost.
    //
    // The way out of the trade-off is assets, not code: give each family the
    // other's clips (see cUseCrossFamilyActionAlias for the bone remap that
    // needs). Once a model owns a clip under the location's own action name,
    // set this back to false -- startAction will find it and the model can be
    // the reward's and animate.
    static constexpr bool cMoonDemoModelFollowsLocation = true;

    struct SupportedMoonDemoArchive {
        char archiveName[32];
        u8 routeMask;
    };
    // The registry is derived from WorldMeta's normal, Dot, and Grand archives,
    // plus the fourteen non-world archives below. A triple reward shows its
    // ShineGrand* model on whatever route its location plays, so each supplied
    // grand skin needs a generic presentation actor. The grand route still uses
    // the state-owned actor as its motion driver.
    static constexpr s32 cExplicitMoonDemoArchiveCount = 14;
    static constexpr s32 cSupportedMoonDemoArchiveMax = WorldMeta::sWorldCount * 3 + cExplicitMoonDemoArchiveCount;
    static_assert(cSupportedMoonDemoArchiveMax == WorldMeta::sWorldCount * 3 + cExplicitMoonDemoArchiveCount,
        "Custom moon demo registry capacity must match its sources");
    static SupportedMoonDemoArchive sSupportedMoonDemoArchives[cSupportedMoonDemoArchiveMax] = {};
    static s32 sSupportedMoonDemoArchiveCount = 0;
    static bool sSupportedMoonDemoArchivesBuilt = false;

    static bool isGrandMoonPresentationArchive(const char* archiveName) {
        char base[64];
        return copyCustomMoonBaseArchive(archiveName, base, sizeof(base)) && std::strncmp(base, "ShineGrand", sizeof("ShineGrand") - 1) == 0;
    }

    static SupportedMoonDemoArchive* findSupportedMoonDemoArchive(const char* archiveName) {
        if (!archiveName || !archiveName[0])
            return nullptr;
        char base[64];
        if (!copyCustomMoonBaseArchive(archiveName, base, sizeof(base)))
            return nullptr;
        for (s32 i = 0; i < sSupportedMoonDemoArchiveCount; i++) {
            if (std::strcmp(base, sSupportedMoonDemoArchives[i].archiveName) == 0)
                return &sSupportedMoonDemoArchives[i];
        }
        return nullptr;
    }

    static void addSupportedMoonDemoArchive(const char* archiveName, u8 routeMask) {
        if (!archiveName || !archiveName[0])
            return;
        char base[64];
        if (!copyCustomMoonBaseArchive(archiveName, base, sizeof(base)))
            return;
        if (!moonArchiveExists(base))
            return;
        if (SupportedMoonDemoArchive* existing = findSupportedMoonDemoArchive(base)) {
            existing->routeMask |= routeMask;
            return;
        }
        if (sSupportedMoonDemoArchiveCount >= cSupportedMoonDemoArchiveMax) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: registry capacity=%d exhausted by archive=%s route=registry", cSupportedMoonDemoArchiveMax, base);
            return;
        }
        SupportedMoonDemoArchive& entry = sSupportedMoonDemoArchives[sSupportedMoonDemoArchiveCount++];
        std::strncpy(entry.archiveName, base, sizeof(entry.archiveName) - 1);
        entry.archiveName[sizeof(entry.archiveName) - 1] = '\0';
        entry.routeMask = routeMask;
    }

    static void validateSupportedMoonDemoArchives() {
        for (s32 i = 0; i < sSupportedMoonDemoArchiveCount; i++) {
            for (s32 j = i + 1; j < sSupportedMoonDemoArchiveCount; j++) {
                if (std::strcmp(sSupportedMoonDemoArchives[i].archiveName, sSupportedMoonDemoArchives[j].archiveName) == 0) {
                    Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: duplicate registry archive=%s route=registry", sSupportedMoonDemoArchives[i].archiveName);
                }
            }
        }
        if (cLogMoonGetDetails) {
            Randomizer::logPush("custom moon demo registry: %d/%d unique archives", sSupportedMoonDemoArchiveCount, cSupportedMoonDemoArchiveMax);
        }
    }

    static void buildSupportedMoonDemoArchives() {
        if (sSupportedMoonDemoArchivesBuilt)
            return;
        sSupportedMoonDemoArchivesBuilt = true;

        // Every 3D archive is a candidate for every 3D route, the grand one
        // included; preflightCustomMoonDemoActor decides per archive which of
        // them it can actually drive.
        constexpr u8 cNormalRoutes = cMoonDemoCapabilityNormal | cMoonDemoCapabilitySpecial | cMoonDemoCapabilityStory | cMoonDemoCapabilityGrand;
        // A grand model is only ever asked for the grand route while the
        // presentation follows the location, so registering it as a candidate for
        // the other three would only make the preflight report three failures it
        // was never going to be used for.
        constexpr u8 cGrandRoutes = cMoonDemoModelFollowsLocation ? cMoonDemoCapabilityGrand : cNormalRoutes;
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            const char* normal = resolveMoonArchive(worldId, false);
            addSupportedMoonDemoArchive(normal, cNormalRoutes);
            const char* dot = makeDotArchiveName(normal, worldId);
            addSupportedMoonDemoArchive(dot, cMoonDemoCapabilityDot);
            addSupportedMoonDemoArchive(resolveMoonArchive(worldId, true), cGrandRoutes);
        }

        addSupportedMoonDemoArchive("PowerStar", cNormalRoutes);
        // The model a triple reward shows on every route. Whether it can actually
        // drive the ordinary routes' actions is decided by the runtime preflight,
        // not assumed here; selectCustomMoonDemoActor falls back to the kingdom's
        // ordinary moon for whichever routes it cannot serve.
        addSupportedMoonDemoArchive("ShineGrand", cGrandRoutes);
        addSupportedMoonDemoArchive(cMoonRockKeyArchive, cNormalRoutes);
        addSupportedMoonDemoArchive(cMoonRockKeyDotArchive, cMoonDemoCapabilityDot);
        addSupportedMoonDemoArchive(cMoonRockKeyGrandArchive, cGrandRoutes);
        addSupportedMoonDemoArchive(cToadetteMedalArchive, cNormalRoutes);
        addSupportedMoonDemoArchive(cToadetteMedalDotArchive, cMoonDemoCapabilityDot);
        addSupportedMoonDemoArchive(cToadetteMedalGrandArchive, cGrandRoutes);
        addSupportedMoonDemoArchive("ShineSprite", cNormalRoutes);
        addSupportedMoonDemoArchive("ShineTriforce", cNormalRoutes);
        addSupportedMoonDemoArchive("ShinePlanet", cNormalRoutes);
        // Some special presentation archives have a bespoke Dot form while
        // others intentionally share ShineDot.  makeDotArchiveName validates
        // that choice through the game's archive probe before registration.
        addSupportedMoonDemoArchive(makeDotArchiveName("ShineSprite", -1), cMoonDemoCapabilityDot);
        addSupportedMoonDemoArchive(makeDotArchiveName("ShineTriforce", -1), cMoonDemoCapabilityDot);
        addSupportedMoonDemoArchive(makeDotArchiveName("ShinePlanet", -1), cMoonDemoCapabilityDot);
        validateSupportedMoonDemoArchives();
    }

    struct ShineDemoArchiveEntry {
        Shine* shine;
        char archiveName[32];
        const ShineDB::ShineEntry* destination;
        bool sourceIsDot;
        bool sourceUsesGrandPresentation;
    };
    static constexpr s32 kMyShineTableMax = 512;
    static s32 sMyShineDemoArchiveCount = 0;
    static ShineDemoArchiveEntry sMyShineDemoArchiveTable[kMyShineTableMax];

    static void registerShineDemoArchive(Shine* shine, const char* archiveName, const ShineDB::ShineEntry* destination, bool sourceIsDot,
        bool sourceUsesGrandPresentation) {
        if (!shine || !archiveName || !archiveName[0])
            return;

        char baseArchive[64];
        if (!copyCustomMoonBaseArchive(archiveName, baseArchive, sizeof(baseArchive)))
            return;

        // This table records the archive that was actually selected for a root
        // Shine source, and the Grand entry is what proves to the native Grand
        // demo that this particular source really loaded the Grand resource.
        // Record it straight from the swap rather than through the presentation
        // registry, so a source stays provable even if that registry never took
        // the archive (its romfs probe, its capacity).
        const char* recordedArchive = baseArchive;
        if (!isGrandMoonPresentationArchive(baseArchive)) {
            buildSupportedMoonDemoArchives();
            SupportedMoonDemoArchive* supported = findSupportedMoonDemoArchive(baseArchive);
            if (!supported) {
                Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: registration failed archive=%s route=registry", archiveName);
                return;
            }
            recordedArchive = supported->archiveName;
        }
        for (s32 i = 0; i < sMyShineDemoArchiveCount; i++) {
            if (sMyShineDemoArchiveTable[i].shine == shine) {
                std::strncpy(sMyShineDemoArchiveTable[i].archiveName, recordedArchive, 31);
                sMyShineDemoArchiveTable[i].archiveName[31] = '\0';
                sMyShineDemoArchiveTable[i].destination = destination;
                sMyShineDemoArchiveTable[i].sourceIsDot = sourceIsDot;
                sMyShineDemoArchiveTable[i].sourceUsesGrandPresentation = sourceUsesGrandPresentation;
                return;
            }
        }
        if (sMyShineDemoArchiveCount >= kMyShineTableMax) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: source archive table full archive=%s route=registry", recordedArchive);
            return;
        }
        ShineDemoArchiveEntry& entry = sMyShineDemoArchiveTable[sMyShineDemoArchiveCount++];
        entry.shine = shine;
        std::strncpy(entry.archiveName, recordedArchive, 31);
        entry.archiveName[31] = '\0';
        entry.destination = destination;
        entry.sourceIsDot = sourceIsDot;
        entry.sourceUsesGrandPresentation = sourceUsesGrandPresentation;
    }

    static const ShineDemoArchiveEntry* findShineDemoArchiveEntry(const Shine* shine) {
        if (!shine)
            return nullptr;
        for (s32 i = 0; i < sMyShineDemoArchiveCount; i++) {
            if (sMyShineDemoArchiveTable[i].shine == shine)
                return &sMyShineDemoArchiveTable[i];
        }
        return nullptr;
    }

    static const char* getShineDemoArchive(Shine* shine) {
        const ShineDemoArchiveEntry* entry = findShineDemoArchiveEntry(shine);
        return entry ? entry->archiveName : nullptr;
    }

    static bool isNativeGrandDemoSource(const Shine* shine) {
        const ShineDemoArchiveEntry* entry = findShineDemoArchiveEntry(shine);
        return entry && entry->sourceUsesGrandPresentation && isGrandMoonPresentationArchive(entry->archiveName);
    }

    struct DemoArchiveActorEntry {
        char archiveName[32];
        al::LiveActor* actor;
        u8 capabilityMask;
    };
    static constexpr s32 cDemoArchiveActorMax = cSupportedMoonDemoArchiveMax;
    static_assert(cDemoArchiveActorMax == cSupportedMoonDemoArchiveMax, "Demo actor storage must be sized from the supported archive registry");
    static s32 sDemoArchiveActorCount = 0;
    static DemoArchiveActorEntry sDemoArchiveActors[cDemoArchiveActorMax] = {};
    static bool sCreatingCustomMoonDemoActors = false;
    static s32 sMoonDemoCapabilityReleaseBlockCount = 0;
    static bool sMoonDemoBulkPreloadComplete = false;
    static bool sMoonDemoBulkPreloadInProgress = false;

    static void clearMoonDemoActorTables() {
        sDemoArchiveActorCount = 0;
        sMyShineDemoArchiveCount = 0;
        sSupportedMoonDemoArchiveCount = 0;
        sSupportedMoonDemoArchivesBuilt = false;
        sCreatingCustomMoonDemoActors = false;
        sMoonDemoCapabilityReleaseBlockCount = 0;
        sMoonDemoBulkPreloadComplete = false;
        sMoonDemoBulkPreloadInProgress = false;
    }

    static bool isCustomMoonDemoActor(const al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < sDemoArchiveActorCount; i++)
            if (sDemoArchiveActors[i].actor == actor)
                return true;
        return false;
    }

    // This accessor is now simple by construction: the global demo slot is
    // never overwritten with one of the generic presentation actors.
    static Shine* getDemoShineForRead(const al::Scene* scene) {
        return getDemoShineActorByOffset(scene);
    }

    static DemoArchiveActorEntry* findCustomMoonDemoActorEntryByArchive(const char* archiveName) {
        if (!archiveName || !archiveName[0])
            return nullptr;
        char base[64];
        if (!copyCustomMoonBaseArchive(archiveName, base, sizeof(base)))
            return nullptr;
        for (s32 i = 0; i < sDemoArchiveActorCount; i++) {
            if (std::strcmp(base, sDemoArchiveActors[i].archiveName) == 0)
                return &sDemoArchiveActors[i];
        }
        return nullptr;
    }

    static al::LiveActor* findCustomMoonDemoActorByArchive(const char* archiveName) {
        DemoArchiveActorEntry* entry = findCustomMoonDemoActorEntryByArchive(archiveName);
        return entry ? entry->actor : nullptr;
    }

    static bool preflightCustomMoonDemoAction(al::LiveActor* actor, const char* archiveName, const char* routeName, const char* actionName) {
        if (!actor || !actionName)
            return false;
        // al::isExistAction is the same source of truth al::startAction resolves
        // against -- the model's Skl/Mtp/Mcl/Mts/Vis animation set -- so it both
        // answers this question and needs no dry run to do it.
        //
        // This used to start the action on a hidden actor and accept
        // al::isActionPlaying as the proof. That test is unreliable: it asks
        // ActionAnimCtrl for its playing-action name first and only falls back to
        // the raw playing-animation name when that is null, so for any action the
        // ActionKeeper could not serve -- which is every action not listed in a
        // model's ActionAnimCtrl.byml -- it can report a correctly started
        // animation as "did not start". It happened to give the right verdicts
        // here, but only because these actors' keepers had never played anything.
        const bool exists = isExistActionAtMainOffset(actor, actionName);
        if (!exists) {
            sMoonDemoCapabilityReleaseBlockCount++;
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: capability archive=%s route=%s requested=%s source=<preload>", archiveName ? archiveName : "?",
                routeName, actionName);
        }
        return exists;
    }

    static u8 preflightCustomMoonDemoActor(al::LiveActor* actor, const SupportedMoonDemoArchive& supported) {
        u8 capabilities = 0;
        const u8 normalAndSpecial = cMoonDemoCapabilityNormal | cMoonDemoCapabilitySpecial;
        // exeDemoGetFirst starts GetShineSub, then the shared exeDemoGet state
        // starts DemoGetShine on state +0x50 for normal and special as well as
        // Dot. Both actions must start on the same custom presentation actor.
        const bool supportsGetShineSub = (supported.routeMask & normalAndSpecial) != 0 && preflightCustomMoonDemoAction(actor, supported.archiveName, "normal/special", "GetShineSub");
        const bool supportsSharedDemoGet = supportsGetShineSub && preflightCustomMoonDemoAction(actor, supported.archiveName, "normal/special", "DemoGetShine");
        if (supportsGetShineSub && supportsSharedDemoGet) {
            capabilities |= supported.routeMask & normalAndSpecial;
        }
        if ((supported.routeMask & cMoonDemoCapabilityDot) != 0 && preflightCustomMoonDemoAction(actor, supported.archiveName, "dot", "DemoGetShine")) {
            capabilities |= cMoonDemoCapabilityDot;
        }
        if ((supported.routeMask & cMoonDemoCapabilityStory) != 0 && preflightCustomMoonDemoAction(actor, supported.archiveName, "story", "GetShine")) {
            capabilities |= cMoonDemoCapabilityStory;
        }
        // The grand state hands its selected slot (+0x98) one action per phase,
        // and the visible reward skin mirrors each supported one while its native
        // driver supplies the location-authored root pose (see GrandMoonPuppet).
        // Unlike every other route, these phase actions are not exit gates: each
        // phase exits on rs::isActionEndDemoPlayer, Mario's own demo action. A
        // model missing some or all of them therefore cannot stall the cutscene;
        // it remains on its idle while the native driver continues the path.
        // The probe below records the checklist a custom triple-moon model fills.
        if ((supported.routeMask & cMoonDemoCapabilityGrand) != 0) {
            capabilities |= cMoonDemoCapabilityGrand;
            static const char* const cGrandPhaseActions[] = {
                "DemoGetShineGrandStart",
                "DemoGetShineGrandCloseUp",
                "DemoGetShineGrandCloseUpEnd",
                "DemoGetShineGrandEnd",
            };
            for (const char* action : cGrandPhaseActions) {
                if (cLogMoonGetDetails && !isExistActionAtMainOffset(actor, action)) {
                    Randomizer::logPush("grand presentation archive=%s has no \"%s\" (it will hold its pose in that phase)", supported.archiveName, action);
                }
            }
        }
        return capabilities;
    }

    // initActorModelKeeper loads the GPU allocator from
    // ActorInitInfo::actorSceneInfo.graphicsSystemInfo + 0x288.  That pointer is
    // only usable during the engine's native actor-init window; it is null after
    // StageScene::init has returned on the scene initialization thread.
    static void* getCustomMoonDemoGpuMemAllocator(const al::ActorInitInfo& info) {
        const void* graphicsInfo = info.actorSceneInfo.graphicsSystemInfo;
        if (!graphicsInfo)
            return nullptr;
        static constexpr uintptr_t cGraphicsSystemInfoGpuMemAllocatorOffset = 0x288;
        return *reinterpret_cast<void* const*>(reinterpret_cast<uintptr_t>(graphicsInfo) + cGraphicsSystemInfoGpuMemAllocatorOffset);
    }

    static void createCustomMoonDemoActor(const al::ActorInitInfo& info, const char* archiveName, const char* route) {
        if (!archiveName || !archiveName[0] || sCreatingCustomMoonDemoActors)
            return;
        buildSupportedMoonDemoArchives();
        SupportedMoonDemoArchive* supported = findSupportedMoonDemoArchive(archiveName);
        if (!supported) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: actor registration failed archive=%s route=%s", archiveName, route ? route : "?");
            return;
        }
        if (findCustomMoonDemoActorByArchive(supported->archiveName))
            return;
        if (sDemoArchiveActorCount >= cDemoArchiveActorMax) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: actor storage full archive=%s route=%s", supported->archiveName, route ? route : "?");
            return;
        }
        if (!getCustomMoonDemoGpuMemAllocator(info)) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: actor init outside graphics window archive=%s route=%s graphics=%p", supported->archiveName,
                route ? route : "?", static_cast<void*>(info.actorSceneInfo.graphicsSystemInfo));
            return;
        }

        // This actor stays a real generic LiveActor. It is never placed in a
        // Shine slot, so no padded fake-Shine allocation is needed or permitted.
        void* storage = ::operator new(sizeof(al::LiveActor));
        if (!storage) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: scene heap allocation failed archive=%s route=%s", supported->archiveName, route ? route : "?");
            return;
        }

        sCreatingCustomMoonDemoActors = true;
        al::LiveActor* actor = new (storage) al::LiveActor("CustomMoonDemo");
        const sead::SafeString customArchiveName(supported->archiveName);
        al::initChildActorWithArchiveNameNoPlacementInfo(actor, info, customArchiveName, "Demo");
        al::invalidateClipping(actor);
        sCreatingCustomMoonDemoActors = false;

        DemoArchiveActorEntry& entry = sDemoArchiveActors[sDemoArchiveActorCount++];
        std::strncpy(entry.archiveName, supported->archiveName, sizeof(entry.archiveName) - 1);
        entry.archiveName[sizeof(entry.archiveName) - 1] = '\0';
        entry.actor = actor;
        entry.capabilityMask = preflightCustomMoonDemoActor(actor, *supported);
        // Every one of these is a reusable presentation actor that must not be in
        // the world until a get-demo binds it. The capability probe used to leave
        // it dead as a side effect of its dry run, which meant an archive with no
        // probed route -- a grand model, once the presentation follows the
        // location -- was silently never killed. State it directly instead.
        actor->makeActorDead();

        if (std::strcmp(entry.archiveName, "PowerStar") == 0)
            registerPowerStarActor(actor);
    }

    static const char* getCustomMoonDemoInfoName(const char* archiveName) {
        if (!archiveName)
            return nullptr;
        if (std::strcmp(archiveName, "ShinePlanet") == 0 || std::strcmp(archiveName, "ShinePlanetEmpty") == 0)
            return "PowerPlanetMain";
        if (std::strcmp(archiveName, "ShineTriforce") == 0 || std::strcmp(archiveName, "ShineTriforceEmpty") == 0)
            return "TriforceMain";
        if (std::strcmp(archiveName, "ShineSprite") == 0 || std::strcmp(archiveName, "ShineSpriteEmpty") == 0)
            return "ShineSprite";
        return nullptr;
    }

    static constexpr uintptr_t cRsSetDemoInfoDemoNameOffset = 0x00550108;

    static void preloadCustomMoonDemoActorsFromRootShineInit(const al::ActorInitInfo& info, const ShineDB::ShineEntry* destination, bool sourceIsDot);

    static HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&, const sead::SafeStringBase<char>&, const char*> initActorWithArchiveNameHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeStringBase<char>& archiveStr, const char* suffix) -> void {
        if (isMoonArchiveName(archiveStr.cstr())) {
            // Shine::init creates several auxiliary generic LiveActors
            // through this same function. They need the destination
            // archive and color just like their root, but must never be
            // recorded in Shine* tables or used as a demo source.
            const bool isRootShine = isShineActor(actor);
            const ShineDB::ShineEntry* physical = nullptr;
            const ShineDB::ShineEntry* dest = nullptr;
            bool useShopDisplayArchive = false;
            if (isPendingShopPreviewDisplayActive()) {
                physical = sPendingShopPreviewDisplayInfo.physical;
                dest = sPendingShopPreviewDisplayInfo.dest;
                useShopDisplayArchive = true;
            } else {
                al::StringTmp<128> objId;
                al::StringTmp<128> placementStage;
                if (tryMakePlacementMoonKey(&objId, &placementStage, info)) {
                    physical = findPhysicalMoonFromKey(objId.cstr(), placementStage.cstr());
                    dest = findDisplayMoonFromKey(objId.cstr(), placementStage.cstr());
                }
            }

            // Runtime-created shines (NPC/event-flow rewards and Hint Art)
            // can have no usable placement key even though Shine::init has
            // already populated curShineInfo. Resolve their physical and
            // destination identities from that stable ShineInfo before model
            // creation, otherwise the completed-kingdom Empty path keeps the
            // stock yellow archive and never receives its kingdom tint.
            if (!dest && isRootShine) {
                Shine* runtimeShine = static_cast<Shine*>(actor);
                if (runtimeShine->curShineInfo) {
                    physical = findEntryFromShineInfo(runtimeShine->curShineInfo);
                    dest = physical && MoonRando::isInitialized() ? MoonRando::getItemForLocation(physical->uid, physical->stage) : nullptr;
                    if (!dest)
                        dest = findDisplayEntryFromShineInfo(runtimeShine->curShineInfo);
                }
            }

            if (dest) {
                if (isRootShine)
                    registerRandomizedShineActor(actor);
                // See cForceLinkedShineToNonGrand above: currently
                // testing with Grand allowed (relying on
                // sceneHeapCreateHook's +4MB); flip that one constant
                // to fall back to the confirmed crash-free PowerStar
                // presentation for linked shines if this still crashes.
                const bool isGrand = !(cForceLinkedShineToNonGrand && sInsideLinkedShineInit) && destPaysTriple(dest);
                const bool isKeyReward = isKeyRewardItem(dest);
                const bool isToadetteMedal = isToadetteMedalDestination(dest);
                // A key takes priority on the rare chance a moon is both
                // (independent random pools that can overlap) - matches this
                // codebase's existing precedence for that archive.
                const bool isDotArchive = isDotArchiveName(archiveStr.cstr());
                const char* destArchive = isKeyReward ? (isDotArchive ? cMoonRockKeyDotArchive : (isGrand ? cMoonRockKeyGrandArchive : cMoonRockKeyArchive)) : (isToadetteMedal ? (isDotArchive ? cToadetteMedalDotArchive : (isGrand ? cToadetteMedalGrandArchive : cToadetteMedalArchive)) : (useShopDisplayArchive ? getMoonArchiveForShopDisplay(dest) : resolveMoonArchive(destWorld(dest), isGrand)));
                if (destArchive && !isKeyReward && !isToadetteMedal && isDotArchive)
                    destArchive = makeDotArchiveName(destArchive, destWorld(dest));
                // Swapped archives load through the vanilla actor-resource
                // path (correct category + heap, torn down with the world);
                // never swap to an archive missing from romfs.
                if (destArchive && !moonArchiveExists(destArchive))
                    destArchive = nullptr;
                if (destArchive) {
                    const bool allowFlaggedShineOrig = physical && physical->worldId == WorldMeta::WorldId_Mushroom;
                    const bool keepWhite = keepsNativeMoonDemoColor(dest);
                    if (destWorld(dest) == WorldMeta::WorldId_Mushroom)
                        registerMushroomShineActor(actor);
                    if ((destWorld(dest) == WorldMeta::WorldId_Mushroom && !isGrand) || isToadetteMedal)
                        registerPowerStarActor(actor);
                    // Generic remap/color applies to root and auxiliary
                    // actors alike. Only the root owns a persistent
                    // Shine identity and source-archive record.
                    if (keepWhite) {
                        actorColorTableClear(actor);
                        registerWhiteMoonActor(actor);
                    } else {
                        unregisterWhiteMoonActor(actor);
                    }
                    registerMoonColor(actor, dest, allowFlaggedShineOrig);
                    if (isRootShine) {
                        registerShineDemoArchive(static_cast<Shine*>(actor), destArchive, dest, isDotArchive, isGrand);
                    }
                    initActorWithArchiveNameHook.orig(actor, info, destArchive, suffix);
                    if (keepWhite)
                        clearWhiteMoonColor(actor);
                    else
                        applyMoonColor(actor, dest, allowFlaggedShineOrig);
                    // ModelKeeper's GPU allocator is live only while the
                    // native root actor is initializing. Construct the
                    // reusable generic presentation actor here, never from
                    // the post-StageScene::init trampoline.
                    if (isRootShine && sMoonDemoBulkPreloadComplete)
                        preloadCustomMoonDemoActorsFromRootShineInit(info, dest, isDotArchive);
                    return;
                }
            }
        }

        initActorWithArchiveNameHook.orig(actor, info, archiveStr, suffix);
        if (actor && EntranceHooks::shouldHideRandoFakeExitPipeActor(info))
            actor->makeActorDead();
    });

    // MoonRock::init reaches this shared implementation through initActor or
    // initActorSuffix; neither route uses initActorWithArchiveName. Replacing
    // the archive at this point preserves the original Moon Rock init flow and
    // swaps only its archive before any model resources are initialized.
    static HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&, const sead::SafeStringBase<char>&, const sead::SafeStringBase<char>&, const char*>
        initActorImplHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeStringBase<char>& directory,
                                                     const sead::SafeStringBase<char>& archiveName, const char* suffix) -> void {
            if (Randomizer::isMoonRockKeyEnabled() && std::strcmp(archiveName.cstr(), cMoonRockArchive) == 0 && moonArchiveExists(cMoonRockWithKeyArchive)) {
                initActorImplHook.orig(actor, info, directory, cMoonRockWithKeyArchive, suffix);
                return;
            }
            initActorImplHook.orig(actor, info, directory, archiveName, suffix);
        });

    static HkTrampoline<void, Shine*, const al::ActorInitInfo&> shineInitHook = hk::hook::trampoline([](Shine* shine, const al::ActorInitInfo& info) -> void {
        HintSystem::observeShineActorInitInfo(info);
        const bool useShopPreviewDisplay = isShopShineInit(shine);
        const ShopMoonDisplayInfo shopInfo = useShopPreviewDisplay ? findCurrentShopMoonDisplayInfo() : ShopMoonDisplayInfo {};
        const ShineDB::ShineEntry* dest = useShopPreviewDisplay ? getShopMoonDisplayEntry(shopInfo) : findDestMoonFromInitInfo(info);
        if (useShopPreviewDisplay)
            sPendingShopPreviewDisplayInfo = shopInfo;

        applyShineDestinationState(shine, dest, false, true);
        CompletedKingdomShineInitContext initContext = {
            shine,
            sCompletedKingdomShineInitContext,
        };
        sCompletedKingdomShineInitContext = &initContext;
        shineInitHook.orig(shine, info);
        sCompletedKingdomShineInitContext = initContext.previous;
        if (useShopPreviewDisplay)
            sPendingShopPreviewDisplayInfo = {};

        if (sInitializedShineCount < cInitializedShineMax)
            sInitializedShines[sInitializedShineCount++] = shine;

        applyShineDestinationState(shine, dest);
        if (shine->mIsPowerStar)
            registerPowerStarShine(shine);
        // A linked shine forced away from its true Grand/Mushroom
        // presentation (see cForceLinkedShineToNonGrand +
        // sInsideLinkedShineInit + the matching override in
        // initActorWithArchiveNameHook) keeps mIsPowerStar == false --
        // it correctly reflects the real reward (still plays the
        // Grand get-demo), so the branch above never fires for it.
        // Register it here anyway (now that .orig() has populated its
        // sub-actor pointers) so isPowerStarActor is true for this
        // shine and all its sub-parts before any later
        // color-application code checks it, without touching
        // mIsPowerStar/the get-sequence itself. Must key off the same
        // cForceLinkedShineToNonGrand switch as isGrand above --
        // otherwise, when that's off (Grand actually loads), this
        // would wrongly mark a real ShineGrand actor as Power Star
        // and skip the per-kingdom color tint it still needs.
        else if (cForceLinkedShineToNonGrand && sInsideLinkedShineInit && dest && destWorld(dest) == WorldMeta::WorldId_Mushroom && destPaysTriple(dest))
            registerPowerStarShine(shine);
    });

    static HkTrampoline<void, al::LiveActor*, const char*, bool> syncVisAnimFrame2DHook = hk::hook::trampoline([](al::LiveActor* actor, const char* stageName, bool flag) -> void {
        if (isPowerStarActor(actor))
            return;
        if (isWhiteMoonActor(actor))
            return;
        const s32 mclFrame = actorColorTableGet(actor);
        if (mclFrame >= 0) {
            applyShineColor(actor, mclFrame);
            return;
        }
        if (al::isEqualString(typeid(*actor).name(), typeid(Shine).name())) {
            Shine* shine = (Shine*)actor;
            if (shine->mIsPowerStar)
                return;
            s32 shineFrame = shineColorTableGet(shine);
            if (shineFrame >= 0) {
                applyShineColor(actor, shineFrame);
                return;
            }
        }
        syncVisAnimFrame2DHook.orig(actor, stageName, flag);
    });

    static HkTrampoline<void, Shine*> shineAfterPlacementHook = hk::hook::trampoline([](Shine* shine) -> void {
        ShineInfo* initialShineInfo = shine ? shine->curShineInfo : nullptr;
        const bool isInitialShopShine = initialShineInfo && initialShineInfo->isShop();
        const ShopMoonDisplayInfo initialShopInfo = isInitialShopShine ? findCurrentShopMoonDisplayInfo() : ShopMoonDisplayInfo {};
        const ShineDB::ShineEntry* initialDest = isInitialShopShine ? getShopMoonDisplayEntry(initialShopInfo) : findDisplayEntryFromShineInfo(initialShineInfo);
        const ShineDB::ShineEntry* initialPhysical = isInitialShopShine ? initialShopInfo.physical : findEntryFromShineInfo(initialShineInfo);
        if (!initialDest)
            initialDest = initialShopInfo.dest;
        if (!initialPhysical)
            initialPhysical = initialShopInfo.physical;
        const bool allowFlaggedShineOrig = initialPhysical && initialPhysical->worldId == WorldMeta::WorldId_Mushroom;

        applyShineDestinationState(shine, initialDest);
        registerRootShineColor(shine, initialDest, allowFlaggedShineOrig);
        shineAfterPlacementHook.orig(shine);
        // Must stay here, not further down: this hook returns early for shines
        // with no destination entry, and Multiplayer still has to register
        // those. It used to own a second trampoline on Shine::initAfterPlacement
        // (one trampoline per function -- see the note in Multiplayer/main.hpp).
        mpOnShineInitAfterPlacement(shine);

        ShineInfo* shineInfo = shine->curShineInfo;
        const bool isShopShine = shineInfo && shineInfo->isShop();
        const ShopMoonDisplayInfo shopInfo = isShopShine ? findCurrentShopMoonDisplayInfo() : ShopMoonDisplayInfo {};
        const ShineDB::ShineEntry* dest = isShopShine ? getShopMoonDisplayEntry(shopInfo) : findDisplayEntryFromShineInfo(shineInfo);
        if (!dest)
            dest = shopInfo.dest;
        if (!dest)
            return;
        applyShineDestinationState(shine, dest);
        if (shine->mIsPowerStar)
            registerPowerStarShine(shine);

        const ShineDB::ShineEntry* physical = nullptr;
        if (shineInfo && !shineInfo->isEmpty()) {
            s32 physObjNum = std::atoi(shineInfo->mObjId.cstr() + strlen("obj"));
            physical = ShineDB::findEntry(ShineDB::findUid(shineInfo->mStageName.cstr(), physObjNum), shineInfo->mStageName.cstr());
        }
        if (!physical)
            physical = shopInfo.physical;
        // Fires once per shine actor, so every stage load re-listed that
        // stage's whole mapping - and the mapping is fixed for the seed. Keep a
        // short sample for eyeballing and leave the authoritative listing to the
        // spoiler log's placement section.
        static s32 sLoggedShineMappings = 0;
        static constexpr s32 cMaxLoggedShineMappings = 32;
        if (physical && sLoggedShineMappings < cMaxLoggedShineMappings) {
            if (++sLoggedShineMappings == cMaxLoggedShineMappings)
                Randomizer::logPush("%s -> %s (further mappings: see spoiler log)", physical->name, dest->name);
            else
                Randomizer::logPush("%s -> %s", physical->name, dest->name);
        }

        applyMoonColor(shine, dest, physical && physical->worldId == WorldMeta::WorldId_Mushroom);
    });

    static HkTrampoline<void, Shine*> shineMakeActorAliveHook = hk::hook::trampoline([](Shine* shine) -> void {
        shineMakeActorAliveHook.orig(shine);

        s32 frame = shineColorTableGet(shine);
        if (frame >= 0) {
            actorColorTableSet((al::LiveActor*)shine, frame);
            actorColorTableSet(getShineStandActor(shine), frame);
            actorColorTableSet(getShineModelA(shine), frame);
            actorColorTableSet(getShineModelB(shine), frame);
            applyShineColor((al::LiveActor*)shine, frame);
            applyShineColor(getShineStandActor(shine), frame);
            applyShineColor(getShineModelA(shine), frame);
            applyShineColor(getShineModelB(shine), frame);
        }
    });

    // StageSceneStateGetShine::exeDemoGetFirst copies Shine::getColorFrame() onto the separate
    // get-demo sub-shine (the one Mario holds up). getColorFrame reads the live "Color" MCL frame
    // of the model, which is not reliably our destination value at demo time, so the cutscene moon
    // reverts to the current kingdom's color. Force the destination kingdom's frame (the same value
    // our recolor table drives onto the world model) so the demo moon matches the kingdom it
    // belongs to. Color only -- no model swap.
    static HkTrampoline<s32, Shine*> shineGetColorFrameHook = hk::hook::trampoline([](Shine* shine) -> s32 {
        if (isWhiteMoonActor(shine))
            return -1;
        if (shine) {
            const s32 frame = shineColorTableGet(shine);
            if (frame >= 0)
                return frame;
        }
        return shineGetColorFrameHook.orig(shine);
    });

    // Shine::createShineEffectInsideObject creates the decorative glow hint
    // shown while a moon sits inside its container (FrailBox crate etc.) --
    // a helper actor registered onto the Shine as an extra sub-actor. It
    // runs from the container's init AFTER the linked Shine itself is
    // already fully built (FrailBox::init constructs the ItemGenerator ->
    // tryInitLinkShine -> complete Shine::init with the swapped archive
    // first, then calls this separately). With the ShineGrand archive
    // loaded, that extra registration deterministically null-derefs inside
    // FrameHeap::tryAlloc: identical registers every run, a 24-byte
    // allocation against ~3MB free, fault address 0x0, and total
    // insensitivity to SceneHeap size (+4MB changed nothing) -- a broken/
    // uninitialized allocator pointer in the Grand init configuration, NOT
    // out-of-memory. No vanilla Grand moon is ever inside a container, so
    // this path is simply unsupported for the Grand config. Skip the
    // decorative glow for exactly that combination: the fully-working
    // Grand triple moon still pops out on break; only the sparkle hint
    // inside the crate is lost, and only for triple-moon-in-container
    // placements. (With cForceLinkedShineToNonGrand the PowerStar archive
    // loads instead, which tolerates the glow fine -- confirmed in-game --
    // so the skip is gated to the Grand-actually-loaded case.)
    static HkTrampoline<void, Shine*, const al::ActorInitInfo&, const sead::Vector3f&, const char*> shineCreateEffectInsideObjectHook = hk::hook::trampoline([](Shine* shine, const al::ActorInitInfo& info, const sead::Vector3f& trans, const char* name) -> void {
        const ShineInfo* shineInfo = shine ? shine->curShineInfo : nullptr;
        const ShineDB::ShineEntry* dest = findDisplayEntryFromShineInfo(shineInfo);
        const bool grandLoaded = !cForceLinkedShineToNonGrand && dest && destPaysTriple(dest);
        if (grandLoaded) {
            Randomizer::logPush("CSEIO: skipping in-container glow for Grand destUid=%d", dest->uid);
            return;
        }
        shineCreateEffectInsideObjectHook.orig(shine, info, trans, name);
    });

    // Heap-growth was investigated for the crate crash and conclusively
    // ruled out -- the crash was never out-of-memory (see the skip-glow
    // comment on shineCreateEffectInsideObjectHook above for the evidence):
    //   1. "WorldResourceCap" (WorldResourceLoader, main offset 0x5145ec,
    //      500MB) -- never actually created during a real play session, per
    //      this same log-every-call diagnostic.
    //   2. "SceneHeap" (~489MB, created fresh per stage load) at +32MB --
    //      the parent pool couldn't even satisfy creation; engine
    //      HaltWithDetail abort on the very first kingdom load.
    //   3. "SceneHeap" at +4MB -- created fine, crash completely unchanged.
    // Left as pure diagnostic logging (no size change) so a future crash's
    // X00 register can be matched against a specific FrameHeap::create
    // call in the log.
    static HkTrampoline<sead::FrameHeap*, size_t, const sead::SafeString&, sead::Heap*, s32, sead::Heap::HeapDirection, bool> sceneHeapCreateHook = hk::hook::trampoline(
        [](size_t size, const sead::SafeString& name, sead::Heap* parent, s32 alignment, sead::Heap::HeapDirection direction, bool arg6) -> sead::FrameHeap* {
            sead::FrameHeap* heap = sceneHeapCreateHook.orig(size, name, parent, alignment, direction, arg6);
            if (cLogMoonHeapDiagnostics) {
                Randomizer::logPush("FrameHeap::create name=%s size=%u heap=%p", name.cstr(), (u32)size, heap);
            }
            return heap;
        });

    static const ShineInfo* sPendingDestInfo = nullptr;
    static const ShineDB::ShineEntry* sLatestGotDisplayEntry = nullptr;
    static const ShineDB::ShineEntry* sLatestGotPhysicalEntry = nullptr;
    static const ShineDB::ShineEntry* sLatestGotDestinationEntry = nullptr;
    static bool sLatestGotUsePhysicalWorld = false;
    static const StageSceneLayout* sSkippedForeignMoonCountLayout = nullptr;
    static bool sBypassCreditRemap = false;
    static bool sCheckingDestSlot = false;
    enum class MoonRewardSource : u8
    {
        WorldPickup,
        RemoteSync,
        DirectPurchase,
        AutoGrant,
    };
    static MoonRewardSource sMoonRewardSource = MoonRewardSource::WorldPickup;
    static constexpr uintptr_t cDemoReturnToHomeOffset = 0x9fc;

    // ShineCounter::startCountAnim always seeds the displayed total one moon
    // below getCurrentShineNum, then runs its Add animation back to the real
    // total. That is correct for a moon credited to the kingdom Mario is in,
    // but creates a fake visual tick (and its sound) when the randomized item
    // belongs to another kingdom and the current kingdom's total did not move.
    static bool shouldSkipForeignMoonCountAnim() {
        const ShineDB::ShineEntry* physical = sMoonGetDemoContext.physical;
        const ShineDB::ShineEntry* destination = sMoonGetDemoContext.destination;
        return physical && destination && destWorld(destination) != physical->worldId;
    }

    static HkTrampoline<void, StageSceneLayout*, bool>
        startShineCountAnimHook = hk::hook::trampoline(
            [](StageSceneLayout* layout, bool isAddTenShines) -> void {
                if (shouldSkipForeignMoonCountAnim()) {
                    sSkippedForeignMoonCountLayout = layout;
                    return;
                }
                sSkippedForeignMoonCountLayout = nullptr;
                startShineCountAnimHook.orig(layout, isAddTenShines);
            });

    static HkTrampoline<bool, const StageSceneLayout*>
        isEndShineCountAnimHook = hk::hook::trampoline(
            [](const StageSceneLayout* layout) -> bool {
                if (sSkippedForeignMoonCountLayout == layout) {
                    sSkippedForeignMoonCountLayout = nullptr;
                    return true;
                }
                return isEndShineCountAnimHook.orig(layout);
            });

    RemoteShineGrantScope::RemoteShineGrantScope()
        : mPreviousSource(static_cast<u8>(sMoonRewardSource)) {
        sMoonRewardSource = MoonRewardSource::RemoteSync;
    }

    RemoteShineGrantScope::~RemoteShineGrantScope() {
        sMoonRewardSource = static_cast<MoonRewardSource>(mPreviousSource);
    }

    static bool isBossRematchStageName(const char* stageName) {
        return stageName && std::strncmp(stageName, "Revenge", 7) == 0;
    }

    static bool isPaintingBossRoomStageName(const char* stageName) {
        return stageName && std::strncmp(stageName, "PeachWorldPicture", 17) == 0;
    }

    static bool isDemoReturnToHomeRequested(const GameDataFile* file) {
        if (!file)
            return false;
        return *reinterpret_cast<const bool*>(
            reinterpret_cast<uintptr_t>(file) + cDemoReturnToHomeOffset);
    }

    static void clearDemoReturnToHome(GameDataFile* file, const char* reason) {
        if (!file)
            return;

        bool& requested = *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(file) + cDemoReturnToHomeOffset);
        if (!requested)
            return;

        requested = false;
        Randomizer::logPush("Cleared stale ReturnToHome (%s)", reason ? reason : "boss rematch");
    }

    void sanitizeBossRematchReturnToHome(GameDataFile* file, const char* stageName) {
        if (isBossRematchStageName(stageName) || isPaintingBossRoomStageName(stageName)) {
            clearDemoReturnToHome(file, stageName);
        }
    }

    static al::LiveActor* getStateActor(void* state, uintptr_t offset) {
        if (!state)
            return nullptr;
        return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + offset);
    }

    static const al::Scene* getStateScene(void* state) {
        if (!state)
            return nullptr;
        return *reinterpret_cast<const al::Scene**>(reinterpret_cast<uintptr_t>(state) + 0x18);
    }

    static const ShineDB::ShineEntry* getActiveMoonDemoDestination() {
        // Direct grants can be followed by their own non-StateGetShine flow
        // (bought moon demos are intentionally outside this runtime change).
        // Do not let an unclaimed direct context color or select a later pickup.
        return sMoonGetDemoContext.route == MoonDemoRoute::Direct ? nullptr : sMoonGetDemoContext.destination;
    }

    // rs::getDemoShineActor is three unguarded loads and no null check anywhere:
    //
    //     LDR X8, [X0,#0x90]   ; Scene::mLiveActorKit
    //     LDR X8, [X8,#0xD0]   ; LiveActorKit::mDemoDirector
    //     LDR X0, [X8,#0x78]   ; DemoDirector's genuine source Shine
    //
    // The public headers' Scene field layout is not ABI-compatible with this
    // retail build. Walking through getSceneObjHolder therefore adds a virtual
    // call and a header-defined SceneObjHolder access that native does not make;
    // the host dump from the normal-get crash stops in that added call before
    // custom binding begins. Mirror the native getter's actual path, but retain
    // null checks so a route without a DemoDirector returns null rather than
    // faulting.
    static Shine* getDemoShineActorByOffset(const al::Scene* scene) {
        if (!scene)
            return nullptr;
        constexpr uintptr_t cSceneLiveActorKitOffset = 0x90;
        constexpr uintptr_t cLiveActorKitDemoDirectorOffset = 0xd0;
        constexpr uintptr_t cDemoDirectorSourceShineOffset = 0x78;

        const uintptr_t sceneAddress = reinterpret_cast<uintptr_t>(scene);
        const void* liveActorKit = *reinterpret_cast<void* const*>(sceneAddress + cSceneLiveActorKitOffset);
        if (!liveActorKit)
            return nullptr;

        const uintptr_t liveActorKitAddress = reinterpret_cast<uintptr_t>(liveActorKit);
        const void* demoDirector = *reinterpret_cast<void* const*>(liveActorKitAddress + cLiveActorKitDemoDirectorOffset);
        if (!demoDirector)
            return nullptr;

        return *reinterpret_cast<Shine* const*>(reinterpret_cast<uintptr_t>(demoDirector) + cDemoDirectorSourceShineOffset);
    }

    static bool isGrandMoonDemoActor(const al::LiveActor* actor) {
        if (!actor)
            return false;
        for (s32 i = 0; i < 3; i++)
            if (sGrandMoonDemoActors[i] == actor)
                return true;
        return false;
    }

    static bool isStoryMoonDemoActor(const al::LiveActor* actor) {
        return actor && (actor == sStoryMoonDemoDisplayActor || actor == sStoryMoonDemoActor);
    }

    static s32 getDemoMoonColorFrame(al::LiveActor* actor, Shine* sourceShine = nullptr) {
        s32 frame = -1;
        const ShineDB::ShineEntry* contextItem = getActiveMoonDemoDestination();
        bool hasExplicitItem = contextItem != nullptr;
        if (contextItem)
            frame = getMoonDemoColorFrame(contextItem);
        if (sourceShine && sourceShine->curShineInfo) {
            if (!contextItem) {
                const ShineDB::ShineEntry* physical = findEntryFromShineInfo(sourceShine->curShineInfo);
                const ShineDB::ShineEntry* item = physical && MoonRando::isInitialized() ? MoonRando::getItemForLocation(physical->uid, physical->stage) : nullptr;
                if (!item)
                    item = findDisplayEntryFromShineInfo(sourceShine->curShineInfo);
                hasExplicitItem = item != nullptr;
                frame = getMoonDemoColorFrame(item);
            }
        }
        if (frame < 0 && sourceShine && !hasExplicitItem)
            frame = shineColorTableGet(sourceShine);
        if (frame < 0 && !hasExplicitItem)
            frame = actorColorTableGet(actor);
        if (frame < 0 && !hasExplicitItem && (!sourceShine || sourceShine == sStoryMoonDemoSource))
            frame = sStoryMoonDemoColorFrame;
        return frame;
    }

    static void registerLatestGotMoonColor(al::LiveActor* actor, Shine* sourceShine = nullptr) {
        const s32 frame = getDemoMoonColorFrame(actor, sourceShine);
        if (!actor || frame < 0)
            return;
        actorColorTableSet(actor, frame);
        applyShineColor(actor, frame);
    }

    // StageSceneStateGetShine owns the generic actors used by every ordinary
    // moon-get demo during a stage visit.  Their color tables must be replaced
    // from the active collection context at the start of each demo; falling back to
    // an existing actor entry can otherwise preserve a previous story moon.
    static void registerCurrentMoonGetDemoColor(al::LiveActor* actor) {
        const ShineDB::ShineEntry* destination = getActiveMoonDemoDestination();
        const s32 frame = getMoonDemoColorFrame(destination);
        if (!actor)
            return;
        if (keepsNativeMoonDemoColor(destination)) {
            actorColorTableClear(actor);
            registerWhiteMoonActor(actor);
            clearWhiteMoonColor(actor);
            return;
        }
        // Moon-rock key presentation actors are shared between collections in a
        // scene. A Moon key must not leave its white-color guard on the next key.
        unregisterWhiteMoonActor(actor);
        if (frame < 0) {
            actorColorTableClear(actor);
            return;
        }
        actorColorTableSet(actor, frame);
        applyShineColor(actor, frame);
    }

    enum class MoonDemoBindResult : u8
    {
        Native,
        Bound,
        Failed,
    };

    static u8 getMoonDemoRouteCapability(MoonDemoRoute route) {
        switch (route) {
        case MoonDemoRoute::Normal:
            return cMoonDemoCapabilityNormal;
        case MoonDemoRoute::Dot:
            return cMoonDemoCapabilityDot;
        case MoonDemoRoute::Special:
            return cMoonDemoCapabilitySpecial;
        case MoonDemoRoute::Story:
            return cMoonDemoCapabilityStory;
        case MoonDemoRoute::Grand:
            return cMoonDemoCapabilityGrand;
        default:
            return 0;
        }
    }

    static const char* getMoonDemoRouteAction(MoonDemoRoute route) {
        switch (route) {
        case MoonDemoRoute::Normal:
        case MoonDemoRoute::Special:
            return "GetShineSub -> DemoGetShine";
        case MoonDemoRoute::Dot:
            return "DemoGetShine";
        case MoonDemoRoute::Story:
            return "GetShine";
        case MoonDemoRoute::Grand:
            return "DemoGetShineGrandStart -> CloseUp -> CloseUpEnd -> End";
        default:
            return "<none>";
        }
    }

    // The normal and special state routes always use a 3D presentation model.
    // Dot is the sole route that requests a valid Dot archive; Story holds a
    // 3D actor even when the physical pickup was Dot.
    //
    // `rewardAccurate` remains the caller's way to ask for the kingdom's ordinary
    // moon explicitly (selectCustomMoonDemoActor's fallback for an archive that
    // cannot drive its route at all).
    static const char* resolveDemoArchiveForEntry(const ShineDB::ShineEntry* dest, MoonDemoRoute route, bool rewardAccurate = true) {
        if (!dest)
            return nullptr;
        const bool isDot = route == MoonDemoRoute::Dot;
        // Dot has no grand form and always resolves the ordinary archive.
        const bool isGrand = rewardAccurate && !isDot && (cMoonDemoModelFollowsLocation ? route == MoonDemoRoute::Grand : destPaysTriple(dest));
        if (isKeyRewardItem(dest))
            return isDot ? cMoonRockKeyDotArchive : (isGrand ? cMoonRockKeyGrandArchive : cMoonRockKeyArchive);
        if (isToadetteMedalDestination(dest))
            return isDot ? cToadetteMedalDotArchive : (isGrand ? cToadetteMedalGrandArchive : cToadetteMedalArchive);
        const char* archive = resolveMoonArchive(destWorld(dest), isGrand);
        return isDot ? makeDotArchiveName(archive, destWorld(dest)) : archive;
    }

    // ActorInitInfo's graphics allocation context is transient.  Preload only
    // the presentations a real source can use while that source is still in its
    // native init call.  This keeps model allocation out of actor execution and
    // avoids constructing every supported archive in a scene that may not need it.
    // A Dot source also gets its normal counterpart because Story uses a 3D held
    // actor even for a 2D physical pickup.
    static void preloadCustomMoonDemoActorsFromRootShineInit(const al::ActorInitInfo& info, const ShineDB::ShineEntry* destination, bool sourceIsDot) {
        if (!destination)
            return;

        // Build every 3D presentation this reward can need. Which route this
        // source ends up on is decided by its location long after this init
        // window has closed, so all of them have to exist by now: the ordinary
        // model for the normal/special/story routes, the grand model for the
        // grand route, and (when the model follows the reward instead of the
        // location) whichever one the reward itself calls for.
        // createCustomMoonDemoActor deduplicates by archive, so an ordinary
        // reward on an ordinary slot still costs the one actor it always did.
        if (const char* ordinary = resolveDemoArchiveForEntry(destination, MoonDemoRoute::Normal, false)) {
            createCustomMoonDemoActor(info, ordinary, "normal/special/story");
        }
        if (const char* grand = resolveDemoArchiveForEntry(destination, MoonDemoRoute::Grand)) {
            createCustomMoonDemoActor(info, grand, "grand");
        }
        if (const char* rewardModel = resolveDemoArchiveForEntry(destination, MoonDemoRoute::Normal)) {
            createCustomMoonDemoActor(info, rewardModel, "normal/special/story reward");
        }

        if (sourceIsDot) {
            if (const char* dot = resolveDemoArchiveForEntry(destination, MoonDemoRoute::Dot)) {
                createCustomMoonDemoActor(info, dot, "dot");
            }
        }
    }

    static void preloadRegisteredCustomMoonDemoActors(const al::ActorInitInfo& info) {
        for (s32 i = 0; i < sMyShineDemoArchiveCount; i++) {
            const ShineDemoArchiveEntry& entry = sMyShineDemoArchiveTable[i];
            preloadCustomMoonDemoActorsFromRootShineInit(info, entry.destination, entry.sourceIsDot);
        }
    }

    // ------------------------------------------------------------------
    // Draw-executor drawer capacity.
    //
    // al::ExecutorListActorModelDrawBase keeps one al::ModelDrawerBase per
    // distinct model name, in a buffer sized once at construction from the
    // list's authored al::ExecuteOrder entry -- and the item lists are authored
    // small (several of them hold eight drawers).
    // al::ExecutorListActorModelDrawBase::registerActorModel appends to that
    // buffer with no bounds check at all, so the first model past capacity
    // writes a drawer pointer over whatever the scene heap placed after it.
    // al::ExecuteTableHolderDraw::init allocates each list object and its
    // drawer buffer back to back, so the victim is the *next* list's vtable
    // pointer and the fault surfaces later, as a BLR through a heap address
    // from inside that list's own registerActorModel.
    //
    // Vanilla never approaches those limits: a stage places one or two distinct
    // moon models.  This mod gives every moon its destination kingdom's model
    // and builds one presentation actor per distinct moon-get demo archive, so
    // a moon-heavy stage -- Metro reloading after its Moon Rock, on a seed that
    // spreads rewards over many kingdoms -- introduces dozens of model names no
    // authored capacity budgeted for.
    //
    // The lists are built by al::ExecuteDirector::init and their buffers are
    // still empty when it returns, so each one can simply be re-based onto a
    // larger, equally empty buffer.  The originals are deliberately left alone:
    // the vanilla teardown never frees them either (~ExecutorListActorModelDraw
    // Base only destroys the drawers, walking mList by mCapacity), and they die
    // with the scene heap they came from.
    // ------------------------------------------------------------------
    static constexpr s32 cMoonModelDrawerHeadroom = cSupportedMoonDemoArchiveMax;

    void reserveMoonModelDrawerCapacity(al::ExecuteDirector* director) {
        if (!director || !director->mDrawTables)
            return;
        for (s32 t = 0; t < director->mDrawTableCount; t++) {
            al::ExecuteTableHolderDraw* holder = director->mDrawTables[t];
            if (!holder || !holder->mExecutorsActorModel)
                continue;
            for (s32 i = 0; i < holder->mExecutorsActorModelSize; i++) {
                al::ExecutorListActorModelDrawBase* list = holder->mExecutorsActorModel[i];
                // Re-basing is only safe while the list is untouched. mSize is
                // zero for every list at this point; if that ever stops being
                // true, leave that list exactly as the engine built it.
                if (!list || !list->mList || list->mSize != 0 || list->mCapacity <= 0)
                    continue;
                const s32 grown = list->mCapacity + cMoonModelDrawerHeadroom;
                al::ModelDrawerBase** buffer = new al::ModelDrawerBase*[grown]();
                if (!buffer)
                    continue;
                list->mList = buffer;
                list->mCapacity = grown;
            }
        }
    }

    // The scene's executor tables exist only once init has returned, and every
    // list is still empty at that moment. Multiplayer/hooks.hpp has its own
    // trampoline on this symbol; it is not installed (installMultiplayer is
    // disabled), and only one of the two may ever be.
    static HkTrampoline<void, al::ExecuteDirector*, const void*> executeDirectorInitHook = hk::hook::trampoline([](al::ExecuteDirector* director, const void* initInfo) -> void {
        executeDirectorInitHook.orig(director, initInfo);
        reserveMoonModelDrawerCapacity(director);
    });

    static s32 sDrawerCapacityDropLogCount = 0;
    static constexpr s32 cDrawerCapacityDropLogMax = 8;

    // Headroom is a budget, not a proof.  Cover the missing bounds check itself
    // so that an overrun nobody predicted degrades into "this model does not
    // draw in this one pass" instead of corrupting the executor table.  Reusing
    // a drawer that already exists costs no slot, so a full list still allows
    // it -- only the append is refused.
    static HkTrampoline<void, al::ExecutorListActorModelDrawBase*, al::LiveActor*> registerActorModelHook = hk::hook::trampoline([](al::ExecutorListActorModelDrawBase* list, al::LiveActor* actor) -> void {
        // Late puppet/capture models can safely use the ordinary
        // forward/deferred model drawers, but they must not enter a
        // depth/shadow executor whose director-sized resources were
        // finalized during scene initialization. This hook already
        // owns the sole trampoline for registerActorModel; filtering
        // here prevents a late depth drawer from being appended to
        // the global list (removing it only from ActorExecuteInfo
        // afterwards would leave that global list still executing it).
        if (LateModelDrawer::isLateBuildActive() && list) {
            const char* listName = list->getName();
            if (listName && (std::strstr(listName, "Depth") || std::strstr(listName, "Shadow"))) {
                return;
            }
        }

        if (list && actor && list->mSize >= list->mCapacity) {
            const al::ModelKeeper* keeper = actor->getModelKeeper();
            const char* modelName = keeper ? keeper->getName() : nullptr;
            bool reusesDrawer = false;
            for (s32 i = 0; modelName && list->mList && i < list->mSize; i++) {
                const al::ModelDrawerBase* drawer = list->mList[i];
                if (drawer && al::isEqualString(drawer->getName(), modelName)) {
                    reusesDrawer = true;
                    break;
                }
            }
            if (!reusesDrawer) {
                if (sDrawerCapacityDropLogCount < cDrawerCapacityDropLogMax) {
                    sDrawerCapacityDropLogCount++;
                    Randomizer::logPush("draw executor \"%s\" full at %d drawers, dropped model=%s", list->getName() ? list->getName() : "?", list->mCapacity,
                        modelName ? modelName : "?");
                }
                return;
            }
        }
        registerActorModelHook.orig(list, actor);
    });

    // StageScene creates its reusable get-demo states near the end of its native
    // initialization.  Their DemoGetShine* child initialization is the last
    // known-safe model-allocation window: ActorInitInfo still carries a live
    // GraphicsSystemInfo/GpuMemAllocator, but placement actors have already
    // registered their destination archives.  Do not move this work to the
    // post-StageScene::init hook; that was the null-allocator crash path.
    static HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&, const sead::SafeStringBase<char>&, const char*> demoActorCreateHook = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& info, const sead::SafeStringBase<char>& archiveName, const char* suffix) -> void {
        demoActorCreateHook.orig(actor, info, archiveName, suffix);
        if (sMoonDemoBulkPreloadComplete || sMoonDemoBulkPreloadInProgress || !al::isEqualSubString(archiveName.cstr(), "DemoGetShine"))
            return;

        sMoonDemoBulkPreloadInProgress = true;
        if (cLogMoonGetDetails) {
            Randomizer::logPush("custom moon demo native preload: %d registered source archives", sMyShineDemoArchiveCount);
        }
        preloadRegisteredCustomMoonDemoActors(info);
        sMoonDemoBulkPreloadInProgress = false;
        sMoonDemoBulkPreloadComplete = true;
        if (sMoonDemoCapabilityReleaseBlockCount > 0) {
            Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: %d unsupported demo capabilities detected during native preload",
                sMoonDemoCapabilityReleaseBlockCount);
        }
    });

    static bool claimMoonGetDemoContext(void* state, MoonDemoRoute route, Shine* source, bool allowDirect) {
        MoonGetDemoContext& context = sMoonGetDemoContext;
        if (!context.destination || (context.state && context.state != state))
            return false;

        const bool direct = allowDirect && (context.route == MoonDemoRoute::Direct || context.route == MoonDemoRoute::Pending);
        if (!direct) {
            if (!source)
                return false;
            if (context.source && context.source != source)
                return false;
            // A few native callers reach the get state without visiting the
            // Shine wait nerve. Their collection record is still valid once its
            // real source matches; let the state determine its exact route.
            if (context.route != route && context.route != MoonDemoRoute::Pending)
                return false;
            if (!context.source) {
                // Some get routes ask the DemoDirector to start before their
                // GameDataFunction::setGotShine call creates this context. Once
                // the state begins, recover that ordering only when the genuine
                // source still resolves to the exact physical location being
                // credited. This is especially important for promoted Grand
                // rewards: the old mismatch fell through to a normal custom
                // bind with no source archive immediately before the crash.
                const ShineDB::ShineEntry* sourcePhysical = source->curShineInfo ? findEntryFromShineInfo(source->curShineInfo) : nullptr;
                if (!sourcePhysical || !context.physical || sourcePhysical->uid != context.physical->uid || std::strcmp(sourcePhysical->stage, context.physical->stage) != 0)
                    return false;
                context.source = source;
                if (cLogMoonGetDetails) {
                    Randomizer::logPush("moon demo context: late-attached source=%p location=%s route=%s", source,
                        context.physical->name ? context.physical->name : "?", getMoonDemoRouteName(route));
                }
            }
        }

        context.state = state;
        context.route = route;
        return true;
    }

    static void logMoonDemoRouteFailure(MoonDemoRoute route, const char* archive, const al::LiveActor* actor, Shine* source) {
        sMoonDemoCapabilityReleaseBlockCount++;
        const char* currentAction = getActionNameAtMainOffset(actor);
        const MoonGetDemoContext& context = sMoonGetDemoContext;
        Randomizer::logPush("CUSTOM MOON RELEASE BLOCK: archive=%s route=%s requested=%s action=%s source=%s(%p) destination=%s serial=%u",
            archive ? archive : "<none>", getMoonDemoRouteName(route), getMoonDemoRouteAction(route), currentAction ? currentAction : "<none>",
            context.physical && context.physical->name ? context.physical->name : "<direct>", source,
            context.destination && context.destination->name ? context.destination->name : "<none>", context.serial);
    }

    static MoonDemoBindResult selectCustomMoonDemoActor(void* state, MoonDemoRoute route, Shine* source, bool allowDirect, al::LiveActor** outActor,
        const char** outArchive, bool* outPuppetSkin = nullptr) {
        if (outActor)
            *outActor = nullptr;
        if (outArchive)
            *outArchive = nullptr;
        if (outPuppetSkin)
            *outPuppetSkin = false;

        const bool hasContext = claimMoonGetDemoContext(state, route, source, allowDirect);
        // A Direct context that this route did not explicitly claim belongs to
        // a bought/list flow. Its native visual is intentionally out of scope;
        // do not fall through to an unrelated source-archive mapping.
        if (!hasContext && sMoonGetDemoContext.route == MoonDemoRoute::Direct)
            return MoonDemoBindResult::Native;
        const char* archive = hasContext ? resolveDemoArchiveForEntry(sMoonGetDemoContext.destination, route) : (source ? getShineDemoArchive(source) : nullptr);
        if (!archive) {
            // A randomized source without a registered archive is itself a
            // registration failure, not permission to reveal a stock model.
            // Native, non-randomized rewards still take their untouched path.
            if (hasContext || (source && isRandomizedShineActor(static_cast<al::LiveActor*>(source)))) {
                logMoonDemoRouteFailure(route, nullptr, nullptr, source);
                return MoonDemoBindResult::Failed;
            }
            return MoonDemoBindResult::Native;
        }

        if (outArchive)
            *outArchive = archive;
        DemoArchiveActorEntry* entry = findCustomMoonDemoActorEntryByArchive(archive);
        const u8 capability = getMoonDemoRouteCapability(route);
        if (!entry || !entry->actor || capability == 0 || (entry->capabilityMask & capability) == 0) {
            // The grand model is the reward-accurate presentation for a triple,
            // but only the grand animation is authored on it.  On the 3D
            // single-moon routes, keep that correct visible model and let the
            // native location actor drive the location's action underneath it.
            // The driver is hidden and its root pose is copied to the grand skin, so
            // the cutscene keeps the location's choreography without reverting a
            // triple reward back to a single model.  Dot has no 3D Grand form,
            // and arbitrary unsupported archives still use the conservative
            // fallback below.
            const char* ordinary = hasContext ? resolveDemoArchiveForEntry(sMoonGetDemoContext.destination, route, false) : nullptr;
            DemoArchiveActorEntry* ordinaryEntry = ordinary && std::strcmp(ordinary, archive) != 0 ? findCustomMoonDemoActorEntryByArchive(ordinary) : nullptr;
            if (ordinaryEntry && ordinaryEntry->actor && capability != 0 && (ordinaryEntry->capabilityMask & capability) != 0) {
                const bool canPuppetGrand = entry && entry->actor && outPuppetSkin && isGrandMoonPresentationArchive(archive) && (route == MoonDemoRoute::Normal || route == MoonDemoRoute::Special || route == MoonDemoRoute::Story);
                if (canPuppetGrand) {
                    *outPuppetSkin = true;
                    if (outActor)
                        *outActor = entry->actor;
                    // Unconditional: this is the branch where the visible model
                    // and the animation driver stop being the same actor, which
                    // is the precondition for every presentation substitution
                    // below it.
                    Randomizer::logPush("%s demo: puppet skin %p archive=%s driver=native (not %s)", getMoonDemoRouteName(route), entry->actor, archive, ordinary);
                    return MoonDemoBindResult::Bound;
                }
                if (cLogMoonGetDetails) {
                    Randomizer::logPush("%s demo: %s cannot drive this route, showing %s instead", getMoonDemoRouteName(route), archive, ordinary);
                }
                if (outArchive)
                    *outArchive = ordinary;
                if (outActor)
                    *outActor = ordinaryEntry->actor;
                return MoonDemoBindResult::Bound;
            }
            logMoonDemoRouteFailure(route, archive, entry ? entry->actor : nullptr, source);
            return MoonDemoBindResult::Failed;
        }

        if (outActor)
            *outActor = entry->actor;
        return MoonDemoBindResult::Bound;
    }

    static void clearStoryMoonDemoColorState() {
        sStoryMoonDemoSource = nullptr;
        sStoryMoonDemoItem = nullptr;
        sStoryMoonDemoDisplayActor = nullptr;
        sStoryMoonDemoActor = nullptr;
        sStoryMoonDemoColorFrame = -1;
        sStoryMoonDemoHasExplicitItem = false;
        sStoryMoonDemoColorWriteLogCount = 0;
    }

    // Story-moon demos recolor a separate stand actor. Prefer the color recorded on the exact
    // Shine that launched the demo: story progression can update the global latest-shine state
    // before the stand actor reaches its color keyframe.
    static HkTrampoline<s32, const al::LiveActor*> getStageShineAnimFrameHook = hk::hook::trampoline([](const al::LiveActor* actor) -> s32 {
        if (isWhiteMoonActor(actor))
            return -1;
        if (isStoryMoonDemoActor(actor) && sStoryMoonDemoColorFrame >= 0)
            return sStoryMoonDemoColorFrame;
        if (isGrandMoonDemoActor(actor) && sGrandMoonDemoColorFrame >= 0)
            return sGrandMoonDemoColorFrame;
        const s32 frame = actorColorTableGet(const_cast<al::LiveActor*>(actor));
        if (frame >= 0)
            return frame;
        return getStageShineAnimFrameHook.orig(actor);
    });

    static void captureStoryMoonDemoColor(void* state) {
        Shine* sourceShine = reinterpret_cast<Shine*>(getStateActor(state, 0xb0));
        al::LiveActor* displayActor = getStateActor(state, 0x88);
        al::LiveActor* standActor = getStateActor(state, 0x98);
        const ShineDB::ShineEntry* physical = sMoonGetDemoContext.physical ? sMoonGetDemoContext.physical : (sourceShine && sourceShine->curShineInfo ? findEntryFromShineInfo(sourceShine->curShineInfo) : nullptr);
        const ShineDB::ShineEntry* item = getActiveMoonDemoDestination();
        if (!item)
            item = physical && MoonRando::isInitialized() ? MoonRando::getItemForLocation(physical->uid, physical->stage) : nullptr;
        if (!item && sourceShine && sourceShine->curShineInfo)
            item = findDisplayEntryFromShineInfo(sourceShine->curShineInfo);

        // The source actor and its ShineInfo storage can both be reused.  The
        // resolved placement item is the stable identity for a get demo, just
        // as it is for grand moons below.
        if (sourceShine && (sourceShine != sStoryMoonDemoSource || item != sStoryMoonDemoItem)) {
            sStoryMoonDemoSource = sourceShine;
            sStoryMoonDemoItem = item;
            sStoryMoonDemoColorFrame = -1;
            sStoryMoonDemoColorWriteLogCount = 0;
            sStoryMoonDemoHasExplicitItem = item != nullptr;
            sStoryMoonDemoColorFrame = getMoonDemoColorFrame(item);
            if (cLogMoonGetDetails) {
                Randomizer::logPush("Story moon color location=%s item=%s world=%d frame=%d", physical && physical->name ? physical->name : "(none)",
                    item && item->name ? item->name : "(none)", item ? item->worldId : -1, sStoryMoonDemoColorFrame);
            }
        }
        if (displayActor)
            sStoryMoonDemoDisplayActor = displayActor;
        if (standActor)
            sStoryMoonDemoActor = standActor;
        registerLatestGotMoonColor(displayActor, sourceShine);
        registerLatestGotMoonColor(standActor, sourceShine);
    }

    // State +0x88 is the Story route's held presentation actor. It is a
    // generic LiveActor slot, unlike +0xb0 which remains the genuine source
    // Shine for story bookkeeping and the stand actor at +0x98.
    struct StoryMoonDemoStockActor {
        const void* state;
        al::LiveActor* slot88;
        bool overridden;
    };
    static StoryMoonDemoStockActor sStoryMoonDemoStockActor = {};

    static void rememberStoryMoonDemoStockActor(void* state) {
        if (!state || sStoryMoonDemoStockActor.state == state)
            return;
        sStoryMoonDemoStockActor = { state, getStateActor(state, 0x88), false };
    }

    static void restoreStoryMoonDemoStockActor(void* state) {
        if (!state || sStoryMoonDemoStockActor.state != state)
            return;
        if (sStoryMoonDemoStockActor.overridden) {
            *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x88) = sStoryMoonDemoStockActor.slot88;
        }
        sStoryMoonDemoStockActor = {};
    }

    static void clearStoryMoonDemoStockActor() {
        sStoryMoonDemoStockActor = {};
    }

    static void logMoonDemoNativeActorInvariant(const char* route, const al::Scene* scene) {
        if (!cLogMoonGetDetails || !scene || sMoonDemoNativeInvariantLogCount >= 24)
            return;
        Shine* source = getDemoShineActorByOffset(scene);
        StageScene* stageScene = static_cast<StageScene*>(const_cast<al::Scene*>(scene));
        const al::LiveActor* demoShine = stageScene ? stageScene->mDemoShine : nullptr;
        const al::LiveActor* demoDot = stageScene ? stageScene->mDemoDotShine : nullptr;
        Randomizer::logPush("custom moon invariant route=%s source=%p genuine=%d stageDemo=%p native=%d stageDot=%p native=%d", route ? route : "?", source,
            source && isShineActor(source), demoShine, !isCustomMoonDemoActor(demoShine), demoDot, !isCustomMoonDemoActor(demoDot));
        sMoonDemoNativeInvariantLogCount++;
    }

    // A skin is used only when the reward's archive cannot execute the action
    // authored for its physical location (currently ShineGrand on normal,
    // special, and story routes).  The native state slot remains the driver so
    // every state transition and completion check stays retail; the reusable
    // skin only supplies the visible reward model.
    static void armLocationMoonDemoPuppet(void* state, MoonDemoRoute route, al::LiveActor* driver, al::LiveActor* skin);
    static void beginLocationMoonDemoPuppet(al::LiveActor* driver);
    static void updateLocationMoonDemoPuppet();
    static void detachLocationMoonDemoPuppet(const void* state);
    static bool startLocationMoonDemoPuppetSkinAction(const al::LiveActor* driver, const char* actionName);

    static MoonDemoBindResult bindStoryMoonDemoActor(void* state) {
        Shine* sourceShine = reinterpret_cast<Shine*>(getStateActor(state, 0xb0));
        al::LiveActor* model = nullptr;
        const char* archive = nullptr;
        bool puppetSkin = false;
        rememberStoryMoonDemoStockActor(state);
        const MoonDemoBindResult result = selectCustomMoonDemoActor(state, MoonDemoRoute::Story, sourceShine, false, &model, &archive, &puppetSkin);
        if (result != MoonDemoBindResult::Bound)
            return result;

        al::LiveActor* driver = puppetSkin ? sStoryMoonDemoStockActor.slot88 : model;
        if (!driver) {
            logMoonDemoRouteFailure(MoonDemoRoute::Story, archive, model, sourceShine);
            return MoonDemoBindResult::Failed;
        }
        if (!puppetSkin) {
            *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x88) = model;
            sStoryMoonDemoStockActor.overridden = true;
        } else {
            armLocationMoonDemoPuppet(state, MoonDemoRoute::Story, driver, model);
        }
        prepareCustomMoonDemoPresentationActor(model);
        registerCurrentMoonGetDemoColor(model);
        logMoonDemoNativeActorInvariant("story", getStateScene(state));
        if (cLogMoonGetDetails) {
            Randomizer::logPush("story demo: bound archive=%s skin=%p driver=%p source=%p", archive ? archive : "?", model, driver, sourceShine);
        }
        sMoonGetProfileArchive = archive;
        return MoonDemoBindResult::Bound;
    }

    // exeWaitPlayerAnimFirst starts "GetShineCity" rather than "GetShine" while the
    // player is a joined session musician, and then waits on that exact action.
    // Capability preflight already proved GetShine can start; this is only the
    // Metro-name redirect for a bound custom model, never an isExistAction-based
    // acceptance check for an otherwise unsupported actor.
    static void storyMoonDemoStartActionHook(al::LiveActor* actor, const char* actionName) {
        if (isCustomMoonDemoActor(actor) && actionName && !isExistActionAtMainOffset(actor, actionName) && isExistActionAtMainOffset(actor, "GetShine")) {
            Randomizer::logPush("story demo: %p has no \"%s\", falling back to \"GetShine\"", actor, actionName);
            actionName = "GetShine";
        }
        al::startAction(actor, actionName);
        beginLocationMoonDemoPuppet(actor);
        startLocationMoonDemoPuppetSkinAction(actor, actionName);
    }

    // The story route's two later action starts on the held moon (+0x88):
    // exeDemoGetStart's "DemoGetShineStart" and exeDemoAppearLayout's
    // "DemoGetShineCloseUp". The native actor stays the driver; this only carries
    // each phase boundary across to the reward's visible model.
    static void storyMoonDemoStartPhaseActionHook(al::LiveActor* actor, const char* actionName) {
        al::startAction(actor, actionName);
        startLocationMoonDemoPuppetSkinAction(actor, actionName);
    }

    // The reusable demo Shine at +0x88 is the moon Mario actually holds up. Vanilla assigns its
    // current-kingdom color in exeWaitPlayerAnimFirst, before the later stand actor at +0x98 is
    // selected. Capture before that write so setStageShineAnimFrameHook can replace it.
    static HkTrampoline<void, void*> storyMoonDemoWaitFirstHook = hk::hook::trampoline([](void* state) -> void {
        // Before the color capture, so the capture records the model we just bound
        // as the display actor rather than the stock one it is replacing.
        if (al::isFirstStep(reinterpret_cast<const al::IUseNerve*>(state))) {
            if (bindStoryMoonDemoActor(state) == MoonDemoBindResult::Failed) {
                // Custom-only policy: never fall back to the stock held moon.
                // Skip straight to Story's count phase as a corruption guard;
                // collection credit has already been committed by setGotShine.
                static constexpr uintptr_t cStoryNrvDemoShineCountOffset = 0x01d8e270;
                logMoonGetPickupTimings("story-unstick");
                al::setNerve(reinterpret_cast<al::IUseNerve*>(state),
                    reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + cStoryNrvDemoShineCountOffset));
                return;
            }
            // exeWaitPlayerAnimFirst is where the story hold-up actually starts,
            // so it closes the pickup window for this route the same way
            // NrvDemoGet does for the shared one.
            logMoonGetPickupTimings("story");
        } else {
            // The story route never enters exeDemoGet, so without this its
            // opening frames had no post window at all -- which is why a long
            // freeze here left no trace in the log.
            sampleMoonGetPostFrame();
        }
        captureStoryMoonDemoColor(state);
        storyMoonDemoWaitFirstHook.orig(state);
        captureStoryMoonDemoColor(state);
    });

    static HkTrampoline<void, void*> storyMoonDemoStateKillHook = hk::hook::trampoline([](void* state) -> void {
        detachLocationMoonDemoPuppet(state);
        restoreStoryMoonDemoStockActor(state);
        completeMoonGetDemoContext(state, MoonDemoRoute::Story);
        storyMoonDemoStateKillHook.orig(state);
    });

    static HkTrampoline<void, void*> storyMoonDemoGetStartHook = hk::hook::trampoline([](void* state) -> void {
        storyMoonDemoGetStartHook.orig(state);
        // exeDemoGetStart assigns the separate stand actor at state + 0x98.
        captureStoryMoonDemoColor(state);
        // Same trap the grand demo hit (see keepGrandDemoShineUpdating): exeDemoLand
        // waits for this actor's "ShineStandOn" to end, but a clipped actor is
        // skipped by movement/calcAnim, so its action sits on frame 0 -- "playing"
        // and never ending -- and the demo's closing camera pull-back is enough to
        // clip it. exeDemoGetStart has just made its owning Shine dead, too.
        if (al::LiveActor* stand = getStateActor(state, 0x98))
            al::invalidateClipping(stand);
    });

    static HkTrampoline<void, void*> storyMoonDemoAppearLayoutHook = hk::hook::trampoline([](void* state) -> void {
        captureStoryMoonDemoColor(state);
        storyMoonDemoAppearLayoutHook.orig(state);
        // DemoGetShineCloseUp restarts the reusable display actor's action and
        // can restore the physical kingdom's material on its first frame.
        captureStoryMoonDemoColor(state);
    });

    // exeDemoLand ends on `isActionEndDemoPlayer(player) && (!state[0x98] ||
    // isActionEnd(state[0x98]))`. Putting the genuine Shine back at + 0xb0 made
    // state + 0x98 -- that Shine's stand actor, read from its + 0x2b0 -- real for
    // the first time. It used to come back null off the custom actor parked in the
    // demo slot, so the second half of that condition was skipped and the step
    // always ended on the player alone. The demo now genuinely waits for the stand
    // moon to finish "ShineStandOn", and a swapped custom model that cannot finish
    // it stalls the end of the cutscene.
    //
    // Vanilla's own null-guard is the way out: clearing the slot ends the step on
    // the next frame with no nerve surgery. Only a stand moon that has provably
    // stopped advancing is dropped, so a landing that is merely long still plays
    // out in full.
    static constexpr s32 cStoryLandStallFrames = 30; // 0.5s with no animation progress
    // Nothing asks the stand moon to animate before step 25 ("ShineStandOn") and
    // step 31 (the Color write), so start watching only once both have passed.
    static constexpr s32 cStoryLandWatchFromStep = 31;
    // Absolute ceiling on the stand-moon half of the wait. A model whose action
    // keeps advancing but never satisfies al::isActionEnd -- a looping
    // "ShineStandOn", say -- resets the stall counter every frame and never trips
    // the check above, which is what still left a long hang at the end of the
    // cutscene. Dropping the condition cannot cut the scene short: exeDemoLand
    // also gates on rs::isActionEndDemoPlayer, so Mario's own landing animation
    // still has to finish before the step ends either way.
    static constexpr s32 cStoryLandHardCapStep = 150; // 2.5s

    struct StoryLandStallWatch {
        const void* state;
        f32 frame;
        s32 frozenFrames;
    };
    static StoryLandStallWatch sStoryLandStall = {};

    static void releaseStuckStoryMoonLanding(void* state) {
        al::LiveActor** slot = reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x98);
        al::LiveActor* stand = *slot;
        if (!stand)
            return;

        const s32 step = al::getNerveStep(reinterpret_cast<const al::IUseNerve*>(state));
        const f32 frame = getActionFrameAtMainOffset(stand);
        const char* action = getActionNameAtMainOffset(stand);

        // What actually happens is not a model that stops animating: it plays
        // "ShineStandOn" all the way to the clip's last frame and then never
        // satisfies al::isActionEnd (observed at frame 58 of 58, released at step
        // 114). Spending another cStoryLandStallFrames proving that costs half a
        // second of held camera on every story moon, so stop as soon as the clip
        // has run out. The frozen-frame counter below still covers the other
        // shape of the failure, where the action stalls partway.
        const f32 frameMax = getActionFrameMaxAtMainOffset(stand, action);
        const bool playedOut = step > cStoryLandWatchFromStep && frameMax > 0.0f && frame >= frameMax - 1.0f;

        if (!playedOut && step < cStoryLandHardCapStep) {
            if (sStoryLandStall.state != state || step <= cStoryLandWatchFromStep) {
                sStoryLandStall = { state, frame, 0 };
                return;
            }
            if (frame > sStoryLandStall.frame) {
                sStoryLandStall.frame = frame;
                sStoryLandStall.frozenFrames = 0;
                return;
            }
            if (++sStoryLandStall.frozenFrames < cStoryLandStallFrames)
                return;
        }

        Randomizer::logPush("story demo: stand moon %p %s \"%s\" frame=%d/%d step=%d -> released", stand, playedOut ? "played out" : "stalled in",
            action ? action : "?", (s32)frame, (s32)frameMax, step);
        *slot = nullptr;
    }

    static HkTrampoline<void, void*> storyMoonDemoLandHook = hk::hook::trampoline([](void* state) -> void {
        captureStoryMoonDemoColor(state);
        storyMoonDemoLandHook.orig(state);
        // The native update starts ShineStandOn and its hit-reaction animation. Both can restore
        // the physical kingdom's material after the pre-update color write, especially when the
        // shortened demo skips directly over the vanilla color keyframe.
        captureStoryMoonDemoColor(state);
        // After orig, so the step counter and the action it just started are current.
        releaseStuckStoryMoonLanding(state);
    });

    // Action changes can start and seek material animations through separate helpers rather
    // than the combined start/set helper below. Keep Moon-destination actors on their archive's
    // un-tinted white material through all three paths.
    static HkTrampoline<void, al::LiveActor*, const char*> startMclAnimHook = hk::hook::trampoline([](al::LiveActor* actor, const char* animName) -> void {
        if (actor && animName && isWhiteMoonActor(actor) && std::strcmp(animName, "Color") == 0)
            return;
        startMclAnimHook.orig(actor, animName);
    });

    static HkTrampoline<void, al::LiveActor*, f32> setMclAnimFrameAndStopHook = hk::hook::trampoline([](al::LiveActor* actor, f32 frame) -> void {
        // Registered ShineMoon-family resources contain only the Color MCL animation.
        if (isWhiteMoonActor(actor))
            return;
        setMclAnimFrameAndStopHook.orig(actor, frame);
    });

    // Keep every explicit Color write to the active story-moon stand on the randomized item's
    // frame. This also catches writes triggered indirectly by action changes rather than the
    // one vanilla call site in exeDemoLand.
    static HkTrampoline<void, al::LiveActor*, const char*, f32> startMclAnimAndSetFrameAndStopHook = hk::hook::trampoline([](al::LiveActor* actor, const char* animName, f32 frame) -> void {
        // A swapped moon archive may lack the requested MCL animation (e.g. a per-kingdom
        // archive without a "Color" material anim). Starting a missing MCL anim dereferences
        // a null AnimPlayerSimple and crashes in al::startMclAnimAndSetFrameAndStop ->
        // AnimPlayerSimple::startAnim during Shine::init. The game's own rs::setStageShineAnimFrame
        // funnels its "Color" write through this same hook, so guarding here covers both vanilla
        // and our applyShineColor calls.
        if (!actor || !animName || !al::isMclAnimExist(actor, animName))
            return;
        const bool isColorWrite = std::strcmp(animName, "Color") == 0;
        if (isColorWrite && isWhiteMoonActor(actor))
            return;
        if (isColorWrite && isStoryMoonDemoActor(actor) && sStoryMoonDemoHasExplicitItem && sStoryMoonDemoColorFrame >= 0) {
            if (cLogMoonGetDetails && sStoryMoonDemoColorWriteLogCount < 8) {
                Randomizer::logPush("Story moon Color write actor=%p requested=%d forced=%d", actor, static_cast<s32>(frame), sStoryMoonDemoColorFrame);
                sStoryMoonDemoColorWriteLogCount++;
            }
            startMclAnimAndSetFrameAndStopHook.orig(actor, animName, static_cast<f32>(sStoryMoonDemoColorFrame));
            return;
        }
        if (isColorWrite && isGrandMoonDemoActor(actor) && sGrandMoonDemoHasExplicitItem && sGrandMoonDemoColorFrame >= 0) {
            if (cLogMoonGetDetails && sGrandMoonDemoColorWriteLogCount < 12) {
                Randomizer::logPush("Grand moon Color write actor=%p requested=%d forced=%d", actor, static_cast<s32>(frame), sGrandMoonDemoColorFrame);
                sGrandMoonDemoColorWriteLogCount++;
            }
            startMclAnimAndSetFrameAndStopHook.orig(actor, animName, static_cast<f32>(sGrandMoonDemoColorFrame));
            return;
        }
        startMclAnimAndSetFrameAndStopHook.orig(actor, animName, frame);
    });

    // StageSceneStateGetShineMain::exeDemoLand applies the stand's Color MCL at frame 31.
    // Replace that one call so action/hit-reaction setup cannot restore the physical kingdom's
    // frame after our actor registration.
    static void setStoryMoonDemoColor(al::LiveActor* actor, const char* animName, f32 fallbackFrame) {
        if (isStoryMoonDemoActor(actor) && sStoryMoonDemoHasExplicitItem) {
            if (sStoryMoonDemoColorFrame >= 0) {
                al::startMclAnimAndSetFrameAndStop(actor, animName, static_cast<f32>(sStoryMoonDemoColorFrame));
            }
            return;
        }
        const s32 frame = getDemoMoonColorFrame(actor);
        if (frame >= 0) {
            al::startMclAnimAndSetFrameAndStop(actor, animName, static_cast<f32>(frame));
            return;
        }
        // Moon Kingdom uses a white destination archive. Do not let the generic
        // demo's fallback frame overwrite it with the orange story-moon tint.
        if (keepsNativeMoonDemoColor(getActiveMoonDemoDestination()))
            return;

        al::startMclAnimAndSetFrameAndStop(actor, animName, fallbackFrame);
    }

    // StageSceneStateGetShine owns three distinct generic presentation slots.
    // Snapshot each stock actor once and restore every overridden slot on kill;
    // no StageScene field or DemoShineActorGroup slot is ever touched.
    struct GetShineStockActors {
        const void* state;
        al::LiveActor* slot38;
        al::LiveActor* slot40;
        al::LiveActor* slot48;
        u8 overriddenMask;
        MoonDemoRoute route;
    };
    static GetShineStockActors sGetShineStockActors = {};

    static void rememberGetShineStockActors(void* state) {
        if (!state || sGetShineStockActors.state == state)
            return;
        sGetShineStockActors = {
            state,
            getStateActor(state, 0x38),
            getStateActor(state, 0x40),
            getStateActor(state, 0x48),
            0,
            MoonDemoRoute::None,
        };
    }

    static void restoreGetShineStockActors(void* state) {
        if (!state || sGetShineStockActors.state != state)
            return;
        const uintptr_t base = reinterpret_cast<uintptr_t>(state);
        if (sGetShineStockActors.overriddenMask & 0x1)
            *reinterpret_cast<al::LiveActor**>(base + 0x38) = sGetShineStockActors.slot38;
        if (sGetShineStockActors.overriddenMask & 0x2)
            *reinterpret_cast<al::LiveActor**>(base + 0x40) = sGetShineStockActors.slot40;
        if (sGetShineStockActors.overriddenMask & 0x4)
            *reinterpret_cast<al::LiveActor**>(base + 0x48) = sGetShineStockActors.slot48;
        sGetShineStockActors = {};
    }

    static void clearGetShineStockActors() {
        sGetShineStockActors = {};
    }

    static bool isSpecialGetShineRoute(void* state, Shine* source) {
        const char* achievementName = state ? *reinterpret_cast<const char**>(reinterpret_cast<uintptr_t>(state) + 0x98) : nullptr;
        const bool sourceIsPowerStar = source && isShineActor(source) && source->mIsPowerStar;
        // This mirrors StageSceneStateGetShine::exeDemoGetFirst exactly: an
        // achievement name wins, otherwise the real source Shine's mIsPowerStar.
        return achievementName || sourceIsPowerStar;
    }

    static MoonDemoBindResult bindGetShineDemoActor(void* state) {
        const u64 profileBindStart = moonGetProfileTick();
        const al::Scene* scene = getStateScene(state);
        Shine* sourceShine = getDemoShineForRead(scene);
        const bool special = isSpecialGetShineRoute(state, sourceShine);
        const MoonDemoRoute route = special ? MoonDemoRoute::Special : MoonDemoRoute::Normal;
        al::LiveActor* model = nullptr;
        const char* archive = nullptr;
        bool puppetSkin = false;
        const MoonDemoBindResult result = selectCustomMoonDemoActor(state, route, sourceShine, special, &model, &archive, &puppetSkin);
        if (result != MoonDemoBindResult::Bound) {
            sMoonGetProfileBindUs = moonGetProfileUs(profileBindStart, moonGetProfileTick());
            return result;
        }

        rememberGetShineStockActors(state);
        const uintptr_t offset = special ? 0x48 : 0x38;
        al::LiveActor* driver = puppetSkin ? (special ? sGetShineStockActors.slot48 : sGetShineStockActors.slot38) : model;
        if (!driver) {
            logMoonDemoRouteFailure(route, archive, model, sourceShine);
            sMoonGetProfileBindUs = moonGetProfileUs(profileBindStart, moonGetProfileTick());
            return MoonDemoBindResult::Failed;
        }
        if (!puppetSkin) {
            *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + offset) = model;
            sGetShineStockActors.overriddenMask |= special ? 0x4 : 0x1;
        } else {
            armLocationMoonDemoPuppet(state, route, driver, model);
        }
        sGetShineStockActors.route = route;
        prepareCustomMoonDemoPresentationActor(model);
        registerCurrentMoonGetDemoColor(model);
        logMoonDemoNativeActorInvariant(getMoonDemoRouteName(route), scene);
        if (cLogMoonGetDetails) {
            Randomizer::logPush("%s demo: bound archive=%s skin=%p driver=%p source=%p slot=%x", getMoonDemoRouteName(route), archive ? archive : "?", model,
                driver, sourceShine, static_cast<u32>(offset));
        }
        sMoonGetProfileArchive = archive;
        sMoonGetProfileBindUs = moonGetProfileUs(profileBindStart, moonGetProfileTick());
        return MoonDemoBindResult::Bound;
    }

    // exeDemoGetFirst (normal/special) and exeDemoGet's Dot branch are the only
    // writers of the state's active presentation slot at + 0x50, and both write
    // it from inside the very first-step block an emergency unstick skips.
    // Everything downstream reads that slot unguarded: exeDemoGetFirst's own tail
    // calls rs::isActionEndDemoPlayer(+0x50) before it returns, and
    // exeDemoShineCount starts a hit reaction on it and calls its kill(). The
    // constructor leaves the slot null, so skipping the setup block without
    // filling it faulted in al::getPlayerActor on the same frame.
    //
    // Point the slot at the actor native itself would have latched, but do not
    // appear it, start an action on it, or add it as a demo actor: the stock
    // model stays invisible exactly as the failure path intends, while every
    // later step of the state has a real actor to talk to.
    static void fillGetShineDemoActorSlot(void* state, uintptr_t stockOffset) {
        if (!state)
            return;
        al::LiveActor* stock = getStateActor(state, stockOffset);
        *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x50) = stock;
        Randomizer::logPush("custom moon unstick: demo slot +50 kept at stock +%x actor=%p", static_cast<u32>(stockOffset), stock);
    }

    // Set when the isFirstStep hook skips exeDemoGetFirst's setup block; consumed
    // by the guard on that same call's rs::isActionEndDemoPlayer tail.
    static bool sGetShineDemoFirstStepSkipped = false;

    static constexpr uintptr_t cGetShineNrvDemoShineCountOffset = 0x01d8dc48;
    static void advanceGetShineDemoToCount(void* state, const char* route) {
        Randomizer::logPush("custom moon emergency unstick route=%s: skipping unsupported presentation only", route ? route : "?");
        al::setNerve(reinterpret_cast<al::IUseNerve*>(state),
            reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + cGetShineNrvDemoShineCountOffset));
    }

    // This is deliberately a replacement for exeDemoGetFirst's existing
    // `al::isFirstStep(this)` call rather than an entry trampoline for the whole
    // state method.  It runs before the native method selects +0x38/+0x48 into
    // +0x50, preserves the native function's prologue/epilogue, and avoids
    // re-entering a large state-machine method through a generated trampoline.
    static bool exeDemoGetFirstIsFirstStepHook(const al::IUseNerve* nerve) {
        const bool isFirst = al::isFirstStep(nerve);
        if (!isFirst)
            return false;

        if (cProfileMoonGetTimings) {
            sMoonGetProfileFirstStepTick = moonGetProfileTick();
            sMoonGetProfilePrevFrameTick = sMoonGetProfileFirstStepTick;
            sMoonGetProfileWorstFrameUs = 0;
            sMoonGetProfileFirstStepFrames = 0;
        }

        void* state = const_cast<al::IUseNerve*>(nerve);
        clearStoryMoonDemoColorState();
        if (bindGetShineDemoActor(state) != MoonDemoBindResult::Failed)
            return true;

        // Do not expose the stock normal/special model after a custom capability
        // failure. Returning false skips exeDemoGetFirst's setup block, so it
        // cannot start an action on the stock slot before the next update runs the
        // count nerve selected below.
        //
        // The skipped block is also where + 0x50 is latched, so hand the slot the
        // same actor native would have chosen (achievement name or the source
        // Shine's mIsPowerStar select + 0x48, everything else + 0x38) before the
        // rest of this call, and every later step, reads it.
        const bool special = isSpecialGetShineRoute(state, getDemoShineForRead(getStateScene(state)));
        fillGetShineDemoActorSlot(state, special ? 0x48 : 0x38);
        sGetShineDemoFirstStepSkipped = true;
        advanceGetShineDemoToCount(state, getMoonDemoRouteName(sMoonGetDemoContext.route));
        return false;
    }

    // exeDemoGetFirst's tail is
    //     isActionEndDemoPlayer(+0x50) && isActionEnd(+0x50) -> setNerve(NrvDemoGet)
    // and it runs even on a frame whose first-step block was skipped. Left alone
    // it would override the count nerve the unstick just selected and play the
    // shared get demo on a presentation actor that was never set up. Report "not
    // ended" for that one frame so the function returns without touching the
    // nerve; the null check keeps the same call site off a slot that somehow
    // never got an actor at all.
    static bool exeDemoGetFirstIsActionEndDemoPlayerHook(const al::LiveActor* demoActor) {
        // This tail runs once per update of NrvDemoGetFirst, so it doubles as the
        // frame counter for that span. A single long gap here is a one-frame
        // stall; many short ones mean the state is simply waiting on an action.
        if (cProfileMoonGetTimings && sMoonGetProfileFirstStepTick != 0) {
            const u64 now = moonGetProfileTick();
            const u64 frameUs = moonGetProfileUs(sMoonGetProfilePrevFrameTick, now);
            if (frameUs > sMoonGetProfileWorstFrameUs)
                sMoonGetProfileWorstFrameUs = frameUs;
            sMoonGetProfilePrevFrameTick = now;
            sMoonGetProfileFirstStepFrames++;
        }

        const bool skipped = sGetShineDemoFirstStepSkipped;
        sGetShineDemoFirstStepSkipped = false;
        if (skipped || !demoActor)
            return false;
        using Fn = bool (*)(const al::LiveActor*);
        return mainOffsetFunction<Fn>(cRsIsActionEndDemoPlayerOffset)(demoActor);
    }

    // StageSceneStateGetShine::kill ends normal, Dot, and special get demos.
    static HkTrampoline<void, void*> getShineStateKillHook = hk::hook::trampoline([](void* state) -> void {
        const MoonDemoRoute route = sGetShineStockActors.state == state ? sGetShineStockActors.route : MoonDemoRoute::None;
        detachLocationMoonDemoPuppet(state);
        restoreGetShineStockActors(state);
        completeMoonGetDemoContext(state, route);
        getShineStateKillHook.orig(state);
    });

    static void exeDemoGetFirstStartActionHook(al::LiveActor* actor, const char* actionName) {
        if (actor) {
            for (s32 i = 0; i < sDemoArchiveActorCount; i++) {
                if (sDemoArchiveActors[i].actor == actor) {
                    const char* customName = getCustomMoonDemoInfoName(sDemoArchiveActors[i].archiveName);
                    if (customName) {
                        using Fn = void (*)(const al::LiveActor*, const char*);
                        mainOffsetFunction<Fn>(cRsSetDemoInfoDemoNameOffset)(actor, customName);
                    }
                    break;
                }
            }
        }
        al::startAction(actor, actionName);
        beginLocationMoonDemoPuppet(actor);
        startLocationMoonDemoPuppetSkinAction(actor, actionName);
    }

    // exeDemoGet's "DemoGetShine" on the presentation slot (+0x50), the second
    // and final beat of the normal/special/Dot route.
    static void getShineDemoStartPhaseActionHook(al::LiveActor* actor, const char* actionName) {
        al::startAction(actor, actionName);
        startLocationMoonDemoPuppetSkinAction(actor, actionName);
    }

    static void captureGrandMoonDemoColor(void* state) {
        // getStageShineAnimFrameHook checks isStoryMoonDemoActor before isGrandMoonDemoActor.
        // If a grand/multi-moon demo reuses the same generic actor as an earlier story-moon
        // demo this visit, that stale check would still win and paint this grand moon with
        // the previous story mission's color. Clear it so the grand-moon lookup below decides.
        clearStoryMoonDemoColorState();

        Shine* sourceShine = getDemoShineForRead(getStateScene(state));
        const ShineDB::ShineEntry* physical = sMoonGetDemoContext.physical ? sMoonGetDemoContext.physical : (sourceShine && sourceShine->curShineInfo ? findEntryFromShineInfo(sourceShine->curShineInfo) : nullptr);
        const ShineDB::ShineEntry* item = getActiveMoonDemoDestination();
        if (!item)
            item = physical && MoonRando::isInitialized() ? MoonRando::getItemForLocation(physical->uid, physical->stage) : nullptr;
        if (!item && sourceShine && sourceShine->curShineInfo)
            item = findDisplayEntryFromShineInfo(sourceShine->curShineInfo);

        if (sourceShine != sGrandMoonDemoSource || item != sGrandMoonDemoItem) {
            sGrandMoonDemoSource = sourceShine;
            sGrandMoonDemoItem = item;
            sGrandMoonDemoHasExplicitItem = item != nullptr;
            sGrandMoonDemoColorFrame = getMoonDemoColorFrame(item);
            sGrandMoonDemoColorWriteLogCount = 0;
            if (cLogMoonGetDetails) {
                Randomizer::logPush("Grand moon color location=%s item=%s world=%d frame=%d", physical && physical->name ? physical->name : "(none)",
                    item && item->name ? item->name : "(none)", item ? item->worldId : -1, sGrandMoonDemoColorFrame);
            }
        }

        sGrandMoonDemoActors[0] = getStateActor(state, 0x88);
        sGrandMoonDemoActors[1] = getStateActor(state, 0x90);
        sGrandMoonDemoActors[2] = getStateActor(state, 0x98);
        for (s32 i = 0; i < 3; i++) {
            al::LiveActor* actor = sGrandMoonDemoActors[i];
            if (!actor)
                continue;
            if (sGrandMoonDemoColorFrame < 0) {
                actorColorTableClear(actor);
                continue;
            }
            actorColorTableSet(actor, sGrandMoonDemoColorFrame);
            applyShineColor(actor, sGrandMoonDemoColorFrame);
        }
    }

    // The grand choreography is not a pose the moon holds, it is four authored
    // actions with their own paths, timings and camera framing, and a reward model
    // that does not carry them cannot reproduce it by imitation. So do not put our
    // model in the state's slot and hope it can dance: let the state drive its own
    // actor exactly as retail does, keep that actor's model hidden, and copy its
    // pose onto the reward's model every frame. The moon on screen is ours; the
    // native actor supplies its path while any matching archive-local action is
    // mirrored onto the skin at the same phase boundary. A model without that
    // action safely remains on idle while still following the native path.
    struct GrandMoonPuppet {
        void* state;
        al::LiveActor* driver; // the state's own moon: real choreography, hidden
        al::LiveActor* skin; // the reward's model: visible, follows the driver
        // The Grand state does not select +0x98 until after its opening rise.
        // +0x88 is the state's prebuilt ShineGrand and can drive an unsupported
        // source action during that first phase.
        al::LiveActor* riseDriver;
        Shine* riseSource; // invisible native proxy used by the first state step
        bool skinAttached;
        bool riseActive;
        bool riseSourceHidden;
    };
    static GrandMoonPuppet sGrandMoonPuppet = {};

    static constexpr uintptr_t cAlResetQuatPositionOffset = 0x008e2910;
    static constexpr uintptr_t cAlSetScaleOffset = 0x008eeef8;
    static constexpr uintptr_t cRsAddDemoActorOffset = 0x0054f624;

    struct LocationMoonDemoPuppet {
        const void* state;
        MoonDemoRoute route;
        al::LiveActor* driver; // native location actor: action/timing/exit gates
        al::LiveActor* skin; // reward archive: visible model
        bool attached;
    };
    static LocationMoonDemoPuppet sLocationMoonDemoPuppet = {};

    // The skin is never used as a state driver, so an archive-local idle is safe:
    // it provides visible life while the driver's location-authored action supplies
    // the camera-facing movement and timing.
    static bool startMoonDemoPresentationIdle(al::LiveActor* actor);
    static bool startMoonDemoPresentationAction(al::LiveActor* actor, const char* actionName, const char* routeName);

    static void addMoonDemoPresentationActor(al::LiveActor* actor) {
        if (!actor)
            return;
        if (isShineActor(actor)) {
            addShineDemoActorWithModelAtMainOffset(static_cast<Shine*>(actor));
            return;
        }
        using AddDemoActorFn = void (*)(al::LiveActor*, bool);
        mainOffsetFunction<AddDemoActorFn>(cRsAddDemoActorOffset)(actor, false);
    }

    static void copyMoonDemoPresentationPose(al::LiveActor* target, const al::LiveActor* driver) {
        if (!target || !driver)
            return;
        using ResetFn = void (*)(al::LiveActor*, const sead::Quatf&, const sead::Vector3f&);
        mainOffsetFunction<ResetFn>(cAlResetQuatPositionOffset)(target, al::getQuat(driver), al::getTrans(driver));
        using ScaleFn = void (*)(al::LiveActor*, const sead::Vector3f&);
        mainOffsetFunction<ScaleFn>(cAlSetScaleOffset)(target, al::getScale(driver));
    }

    // The grand state starts a phase's action before the frame's demo-kit update,
    // and the skin is only attached from inside that update (it needs a driver
    // pose first, or it would flash at the origin). Hold the phase the state
    // asked for and play it the moment the skin goes live, so the opening beat
    // is not simply lost. Action names come from the game binary's literals, so
    // the pointer stays valid.
    static const char* sGrandMoonPuppetPendingAction = nullptr;

    static void activateGrandMoonPuppetSkin(GrandMoonPuppet& puppet) {
        if (puppet.skinAttached || !puppet.skin)
            return;
        puppet.skinAttached = true;
        // A preflighted archive actor can still carry its hidden flag.  It has to
        // be visible before makeActorAlive so the normal draw-executor
        // registration happens; show again afterwards for actors whose appear
        // path toggles that flag.
        showMoonDemoPresentation(puppet.skin);
        puppet.skin->makeActorAlive();
        showMoonDemoPresentation(puppet.skin);
        al::invalidateClipping(puppet.skin);
        // The world is paused for the cutscene: only actors on the demo list
        // are updated, and an actor that never updates never rebuilds its
        // model matrix from the pose we are writing.
        addMoonDemoPresentationActor(puppet.skin);
        const char* pending = sGrandMoonPuppetPendingAction;
        sGrandMoonPuppetPendingAction = nullptr;
        if (!pending || !startMoonDemoPresentationAction(puppet.skin, pending, "grand")) {
            startMoonDemoPresentationIdle(puppet.skin);
        }
    }

    // The native state actor owns the cutscene's root motion and effects, but
    // its action never reaches this generic reward actor. Dispatch the matching
    // archive-local action once at each native action boundary so the visible
    // model advances its own skeleton/material/visibility animations in lockstep.
    // A model with no form of this beat at all falls back to its idle.
    static bool startGrandMoonPuppetSkinAction(const char* actionName) {
        GrandMoonPuppet& puppet = sGrandMoonPuppet;
        if (!puppet.skin)
            return false;
        if (!puppet.skinAttached) {
            sGrandMoonPuppetPendingAction = actionName;
            return false;
        }
        return startMoonDemoPresentationAction(puppet.skin, actionName, "grand");
    }

    // The same dispatch for the ordinary/story routes, where the roles are the
    // other way round: the native location actor is the driver and the reward's
    // model is the skin (see armLocationMoonDemoPuppet). Without this the skin
    // was started once on its idle at attach time and then only had its root
    // pose copied, so a triple reward on an ordinary slot -- and any reward whose
    // model is not the one the location authored -- rode the whole cutscene on a
    // single looping animation while the hidden driver played the real one.
    //
    // Mirrors only what the puppet's own driver was told to play; a phase that
    // animates some other actor is none of the skin's business.
    static bool startLocationMoonDemoPuppetSkinAction(const al::LiveActor* driver, const char* actionName) {
        LocationMoonDemoPuppet& puppet = sLocationMoonDemoPuppet;
        if (!puppet.attached || !puppet.skin || puppet.driver != driver)
            return false;
        return startMoonDemoPresentationAction(puppet.skin, actionName, getMoonDemoRouteName(puppet.route));
    }

    static void hideGrandMoonRiseSource(GrandMoonPuppet& puppet, Shine* source) {
        if (!source)
            return;
        puppet.riseSource = source;
        if (!puppet.riseSourceHidden) {
            hideMoonDemoPresentation(source);
            puppet.riseSourceHidden = true;
        }
    }

    static void resetLocationMoonDemoPuppet() {
        if (sLocationMoonDemoPuppet.driver)
            showMoonDemoPresentation(sLocationMoonDemoPuppet.driver);
        if (sLocationMoonDemoPuppet.skin) {
            hideMoonDemoPresentation(sLocationMoonDemoPuppet.skin);
            sLocationMoonDemoPuppet.skin->makeActorDead();
        }
        sLocationMoonDemoPuppet = {};
    }

    static void armLocationMoonDemoPuppet(void* state, MoonDemoRoute route, al::LiveActor* driver, al::LiveActor* skin) {
        if (!state || !driver || !skin || driver == skin)
            return;
        resetLocationMoonDemoPuppet();
        sLocationMoonDemoPuppet = { state, route, driver, skin, false };
    }

    static void copyLocationMoonDemoPuppetPose() {
        LocationMoonDemoPuppet& puppet = sLocationMoonDemoPuppet;
        if (!puppet.driver || !puppet.skin)
            return;
        hideMoonDemoPresentation(puppet.driver);
        copyMoonDemoPresentationPose(puppet.skin, puppet.driver);
    }

    static void beginLocationMoonDemoPuppet(al::LiveActor* driver) {
        LocationMoonDemoPuppet& puppet = sLocationMoonDemoPuppet;
        if (!puppet.state || puppet.attached || !puppet.skin || puppet.driver != driver)
            return;

        puppet.attached = true;
        showMoonDemoPresentation(puppet.skin);
        puppet.skin->makeActorAlive();
        showMoonDemoPresentation(puppet.skin);
        al::invalidateClipping(puppet.skin);
        addMoonDemoPresentationActor(puppet.skin);
        startMoonDemoPresentationIdle(puppet.skin);
        copyLocationMoonDemoPuppetPose();
        if (cLogMoonGetDetails) {
            Randomizer::logPush("%s demo: reward skin %p now follows native location driver %p", getMoonDemoRouteName(puppet.route), puppet.skin, puppet.driver);
        }
    }

    static void updateLocationMoonDemoPuppet() {
        if (sLocationMoonDemoPuppet.attached)
            copyLocationMoonDemoPuppetPose();
    }

    static void detachLocationMoonDemoPuppet(const void* state) {
        if (!state || sLocationMoonDemoPuppet.state == state)
            resetLocationMoonDemoPuppet();
    }

    static void detachGrandMoonPuppet() {
        if (sGrandMoonPuppet.driver && !al::isDead(sGrandMoonPuppet.driver))
            showMoonDemoPresentation(sGrandMoonPuppet.driver);
        if (sGrandMoonPuppet.riseDriver && sGrandMoonPuppet.riseDriver != sGrandMoonPuppet.driver && !al::isDead(sGrandMoonPuppet.riseDriver)) {
            showMoonDemoPresentation(sGrandMoonPuppet.riseDriver);
        }
        if (sGrandMoonPuppet.skin) {
            hideMoonDemoPresentation(sGrandMoonPuppet.skin);
            sGrandMoonPuppet.skin->makeActorDead();
        }
        // The real Shine remains the native first-step proxy, but the generic
        // reward actor is the sole visible model. Restore the source only while
        // handing control back so cancellation cannot leave it hidden.
        if (sGrandMoonPuppet.riseSource && sGrandMoonPuppet.riseSourceHidden && sGrandMoonPuppet.riseSource != sGrandMoonPuppet.driver && sGrandMoonPuppet.riseSource != sGrandMoonPuppet.riseDriver && !al::isDead(sGrandMoonPuppet.riseSource)) {
            showMoonDemoPresentation(sGrandMoonPuppet.riseSource);
        }
        sGrandMoonPuppet = {};
        sGrandMoonPuppetPendingAction = nullptr;
    }

    // The first grand state reads the physical Shine's root pose after its demo
    // kit update to place Mario. When a native +0x88/+0x90 actor drives an
    // unsupported source action, update only that invisible root on both sides
    // of the kit update; the post-update write is what the state's Mario lerp
    // reads. Never push a pose into either of the source Shine's visible children.
    static void updateGrandMoonRiseSourceProxy(bool hideSourceModel) {
        if (!sGrandMoonPuppet.state || !sGrandMoonPuppet.skin)
            return;
        if (!sGrandMoonPuppet.riseActive || !sGrandMoonPuppet.riseSource || !sGrandMoonPuppet.riseDriver || getStateActor(sGrandMoonPuppet.state, 0x98)) {
            return;
        }
        if (sGrandMoonPuppet.riseDriver != sGrandMoonPuppet.riseSource) {
            copyMoonDemoPresentationPose(sGrandMoonPuppet.riseSource, sGrandMoonPuppet.riseDriver);
        }
        // A source control pass can reveal one of its child models again.  The
        // post-kit pass runs after that control work and before the frame is
        // drawn, so one hide there keeps the generic reward skin exclusive
        // without invalidating the source's shadows several times per frame.
        if (hideSourceModel)
            hideMoonDemoPresentation(sGrandMoonPuppet.riseSource);
    }

    // Runs after the demo kit update, so the generic reward actor gets the
    // driver's newly advanced pose. Every phase of the grand cutscene calls the
    // kit update exactly once.
    static void updateGrandMoonPuppet() {
        if (!sGrandMoonPuppet.state || !sGrandMoonPuppet.skin)
            return;
        al::LiveActor* stateDriver = getStateActor(sGrandMoonPuppet.state, 0x98);
        al::LiveActor* driver = stateDriver ? stateDriver : (sGrandMoonPuppet.riseActive ? sGrandMoonPuppet.riseDriver : nullptr);
        if (!driver)
            return;

        activateGrandMoonPuppetSkin(sGrandMoonPuppet);
        if (!sGrandMoonPuppet.skinAttached)
            return;
        sGrandMoonPuppet.driver = driver;

        if (driver != sGrandMoonPuppet.riseSource)
            hideMoonDemoPresentation(driver);
        copyMoonDemoPresentationPose(sGrandMoonPuppet.skin, driver);
    }

    static MoonDemoBindResult bindGrandMoonDemoActor(void* state) {
        if (sGrandMoonPuppet.state == state)
            return MoonDemoBindResult::Bound;

        const al::Scene* scene = getStateScene(state);
        Shine* sourceShine = getDemoShineForRead(scene);
        al::LiveActor* model = nullptr;
        const char* archive = nullptr;
        const MoonDemoBindResult result = selectCustomMoonDemoActor(state, MoonDemoRoute::Grand, sourceShine, false, &model, &archive);
        if (result != MoonDemoBindResult::Bound)
            return result;

        detachGrandMoonPuppet();
        sGrandMoonPuppet = { state, nullptr, model, nullptr, nullptr, false, false, false };
        prepareCustomMoonDemoPresentationActor(model);
        registerCurrentMoonGetDemoColor(model);
        Randomizer::logPush("grand demo: bound archive=%s skin=%p source=%p (puppet)", archive ? archive : "?", model, sourceShine);
        return MoonDemoBindResult::Bound;
    }

    // exeDemoGetFirst is the one grand phase that animates the physical Shine
    // rather than a presentation actor: it starts "GetShineGrand" on it, paces
    // Mario's approach from that action's frame count, and leaves the step once
    // both that action and Mario's own demo action have ended. The model there
    // follows the reward and is not guaranteed to have "GetShineGrand" -- an
    // ordinary moon on a vanilla triple slot does not -- so the moon half of that
    // wait can never arrive.
    //
    // Hand the step back to its other gate rather than inventing a duration for
    // it: with no action to wait on, the moon is "done" immediately and the step
    // runs exactly as long as Mario's animation, which is what every later grand
    // phase is timed against anyway. An invented clock is what left Mario holding
    // the moon in the air after his animation had finished, with the cutscene and
    // the jingle drifting apart behind it.
    static constexpr s32 cGrandRiseSubstituteApproachFrames = 30;
    static bool sGrandRiseSubstituted = false;

    static bool beginGrandMoonRisePuppet(Shine* source, const char* actionName) {
        GrandMoonPuppet& puppet = sGrandMoonPuppet;
        if (!source || !actionName || !puppet.state || !puppet.skin)
            return false;

        // The source is still the state machine's positional proxy during this
        // opening step, but it is not a dependable visible skin: it owns two
        // children and showCurrentModel chooses between them from mIsGotShine.
        // Put the preloaded generic reward actor on screen from the very first
        // frame instead. If the source can run the action it remains the driver;
        // otherwise use the state's native grand actor and copy only its root
        // pose back to the invisible source before the state reads that pose.
        if (isExistActionAtMainOffset(source, actionName)) {
            puppet.riseDriver = source;
            puppet.riseActive = true;
            activateGrandMoonPuppetSkin(puppet);
            hideGrandMoonRiseSource(puppet, source);
            copyMoonDemoPresentationPose(puppet.skin, source);
            al::startAction(source, actionName);
            startGrandMoonPuppetSkinAction(actionName);
            if (cLogMoonGetDetails) {
                Randomizer::logPush("grand demo: reward skin %p follows source rise driver %p", puppet.skin, source);
            }
            return true;
        }

        // The Grand state's constructor always creates these two native actors:
        // +0x88 is ShineGrand, +0x90 is ShineGrandEmpty.  +0x98 is populated
        // only after this rise, so mirror the native empty/non-empty selection
        // for the temporary driver too.
        const uintptr_t driverOffset = isEmptyShineForDemoGetGrandAtMainOffset(source) ? 0x90 : 0x88;
        al::LiveActor* driver = getStateActor(puppet.state, driverOffset);
        if (!driver || !isExistActionAtMainOffset(driver, actionName))
            return false;

        puppet.riseDriver = driver;
        puppet.riseActive = true;
        activateGrandMoonPuppetSkin(puppet);
        hideGrandMoonRiseSource(puppet, source);

        driver->makeActorAlive();
        showMoonDemoPresentation(driver);
        al::invalidateClipping(driver);
        using ResetFn = void (*)(al::LiveActor*, const sead::Quatf&, const sead::Vector3f&);
        mainOffsetFunction<ResetFn>(cAlResetQuatPositionOffset)(driver, al::getQuat(source), al::getTrans(source));
        copyMoonDemoPresentationPose(puppet.skin, driver);
        al::startAction(driver, actionName);
        startGrandMoonPuppetSkinAction(actionName);
        addMoonDemoPresentationActor(driver);
        hideMoonDemoPresentation(driver);

        if (cLogMoonGetDetails) {
            Randomizer::logPush("grand demo: source %p has no \"%s\"; reward skin %p follows native rise driver %p (+%x)", source, actionName, puppet.skin, driver,
                static_cast<u32>(driverOffset));
        }
        return true;
    }

    static void grandDemoStartShineActionHook(al::LiveActor* actor, const char* actionName) {
        sGrandRiseSubstituted = false;
        if (actor && isShineActor(actor) && beginGrandMoonRisePuppet(static_cast<Shine*>(actor), actionName)) {
            return;
        }
        if (actor && actionName && isExistActionAtMainOffset(actor, actionName)) {
            al::startAction(actor, actionName);
            return;
        }
        sGrandRiseSubstituted = true;
        // Mario spends this whole step holding the moon over his head. Leaving it
        // on no action at all is what made that read as a hang rather than a
        // pose, so put it on its idle: the moon keeps turning in his hands for
        // exactly as long as his animation runs.
        const bool idling = startMoonDemoPresentationIdle(actor);
        if (cLogMoonGetDetails) {
            Randomizer::logPush("grand demo: %p has no \"%s\", %s and pacing the rise from Mario's action", actor, actionName ? actionName : "?",
                idling ? "idling the moon" : "leaving the moon on its current action");
        }
    }

    // These are the four +0x98 action starts in StageSceneStateGetShineGrand.
    // Keep the state-owned driver completely native, then advance the visible
    // skin with the same archive-local action before this phase's demo kit update.
    static void grandDemoStartPresentationActionHook(al::LiveActor* actor, const char* actionName) {
        al::startAction(actor, actionName);
        startGrandMoonPuppetSkinAction(actionName);
    }

    static constexpr uintptr_t cAlGetActionFrameMaxOffset = 0x008d3400;
    static f32 grandDemoRiseFrameMaxHook(const al::LiveActor* actor, const char* actionName) {
        // Only feeds al::calcNerveRate for the lerp that walks Mario to the moon.
        // Short enough that he has arrived well before his animation ends, so the
        // step never finishes with him stranded partway.
        if (sGrandMoonPuppet.riseActive && sGrandMoonPuppet.riseDriver) {
            using Fn = f32 (*)(const al::LiveActor*, const char*);
            return mainOffsetFunction<Fn>(cAlGetActionFrameMaxOffset)(sGrandMoonPuppet.riseDriver, actionName);
        }
        if (sGrandRiseSubstituted)
            return static_cast<f32>(cGrandRiseSubstituteApproachFrames);
        using Fn = f32 (*)(const al::LiveActor*, const char*);
        return mainOffsetFunction<Fn>(cAlGetActionFrameMaxOffset)(actor, actionName);
    }

    static bool grandDemoRiseIsActionEndHook(const al::LiveActor* actor) {
        if (sGrandMoonPuppet.riseActive && sGrandMoonPuppet.riseDriver)
            return al::isActionEnd(sGrandMoonPuppet.riseDriver);
        if (sGrandRiseSubstituted)
            return true;
        return al::isActionEnd(actor);
    }

    // A reward's model and its location's choreography come from different
    // places, so a presentation skin is routinely asked for an action its
    // archive was never authored with. The two families do not overlap at all:
    // every ordinary Shine* archive ships GetShine/GetShineSub/GetShineEmpty/
    // GetShineCity/DemoGetShine/DemoGetShineStart/DemoGetShineCloseUp, and every
    // ShineGrand* archive ships GetShineGrand plus the four DemoGetShineGrand*
    // phases -- neither has a single action from the other set.
    //
    // Substituting the model's own version of the same beat was tried on
    // hardware and is WRONG, which is why it is off. These clips are not poses:
    // each carries its own root motion and its own duration, authored for one
    // specific cutscene. Playing the grand rise underneath the ordinary get made
    // the moon travel its own path while Mario and the camera did something else,
    // and the reverse did the same to the grand cutscene. The per-frame root pose
    // copy cannot cancel it, because the motion is inside the skeleton rather
    // than on the actor transform.
    //
    // From the FSKA tables in romfs/ObjectData (clip: frames, animated bones):
    //   ShineSand       GetShineSub 1, GetShine 12, DemoGetShine 125,
    //                   DemoGetShineStart 240, DemoGetShineCloseUp 144
    //                   -> all on { AllRoot, Shine }; Wait 1 on { Shine, Shine00,
    //                      Shine01 }
    //   ShineGrandSand  GetShineGrand 12, DemoGetShineGrandStart 632,
    //                   DemoGetShineGrandCloseUp 150, ...CloseUpEnd 0, ...End 247
    //                   -> all on { ShineGrand, ShineGrand00 }; Wait 1 likewise
    //
    // The skeletons (FSKL) are:
    //   ShineSand       AllRoot, Shine
    //   ShineGrandSand  ShineGrand, AllRoot, ShineGrand00
    //
    // Note the durations (240 vs 632) and that only AllRoot is common, and that
    // neither family's clips even use the same pair: the ordinary ones drive
    // { AllRoot, Shine }, the grand ones { ShineGrand, ShineGrand00 }. So
    // copying an FSKA sub-file across as-is would leave the moon body inert.
    // Importing one has to re-point its two bone tracks, which lines up as
    //   AllRoot <-> ShineGrand   (the driver)
    //   Shine   <-> ShineGrand00 (the body)
    // on top of rebuilding the BFRES string pool, sub-file dictionary and
    // relocation table -- i.e. a job for a BFRES editor, not a byte patch.
    //
    // Left in place as the record of that result, and because it is the right
    // switch to flip if the missing clips are ever authored into the models.
    static constexpr bool cUseCrossFamilyActionAlias = false;

    struct MoonDemoActionAlias {
        const char* requested;
        const char* substitute;
    };
    static constexpr MoonDemoActionAlias sMoonDemoActionAliases[] = {
        // Ordinary/story beat -> the grand model's equivalent.
        { "GetShine", "GetShineGrand" },
        { "GetShineSub", "GetShineGrand" },
        { "GetShineCity", "GetShineGrand" },
        { "GetShineEmpty", "GetShineGrand" },
        { "DemoGetShine", "DemoGetShineGrandStart" },
        { "DemoGetShineStart", "DemoGetShineGrandStart" },
        { "DemoGetShineCloseUp", "DemoGetShineGrandCloseUp" },
        // Grand beat -> the single-moon model's equivalent. The grand route has
        // one phase more than the ordinary one, so its CloseUpEnd reuses the
        // ordinary close-up rather than inventing a beat that does not exist.
        { "GetShineGrand", "GetShineSub" },
        { "DemoGetShineGrandStart", "DemoGetShineStart" },
        { "DemoGetShineGrandCloseUp", "DemoGetShineCloseUp" },
        { "DemoGetShineGrandCloseUpEnd", "DemoGetShineCloseUp" },
        { "DemoGetShineGrandEnd", "DemoGetShine" },
    };

    static const char* findMoonDemoActionAlias(const char* actionName) {
        if (!cUseCrossFamilyActionAlias || !actionName)
            return nullptr;
        for (const MoonDemoActionAlias& alias : sMoonDemoActionAliases) {
            if (std::strcmp(alias.requested, actionName) == 0)
                return alias.substitute;
        }
        return nullptr;
    }

    // "Does this model have this beat" is answered by al::isExistAction alone.
    //
    // It resolves purely against the model's own animation set --
    // isSklAnimExist || isMtpAnimExist || isMclAnimExist || isMtsAnimExist ||
    // isVisAnimExist -- and never consults ActionAnimCtrl. al::startAction then
    // tries the ActionKeeper first and falls back to starting those same
    // animations by name, so an archive with no ActionAnimCtrl entry (or no
    // ActionAnimCtrl at all, which is every ShineGrand* archive here) still
    // animates.
    //
    // What must NOT be used as the success test is al::isActionPlaying: it asks
    // ActionAnimCtrl for its playing-action name first and only falls through to
    // the raw playing-animation name when that is null. For exactly the actions
    // the keeper could not serve -- which is every cross-family action here --
    // it keeps reporting the stale keeper name, so a correctly started animation
    // reads as "did not start".
    static bool tryStartMoonDemoPresentationAction(al::LiveActor* presentation, const char* actionName) {
        if (!presentation || !actionName || !isExistActionAtMainOffset(presentation, actionName)) {
            return false;
        }
        startActionAtMainOffset(presentation, actionName);
        return true;
    }

    // Logged unconditionally, unlike the rest of the moon-get detail tracing:
    // a handful of lines per collection, only on the paths that had to
    // substitute something, and they are the only record of what a reward's
    // model actually managed to play on a location it was not authored for.
    static bool startMoonDemoPresentationAction(al::LiveActor* actor, const char* actionName, const char* routeName) {
        if (!actor || !actionName)
            return false;
        // Shine's animation resources are owned by its selected model child, so
        // an action started on the root can leave the visible moon frozen.
        al::LiveActor* presentation = getMoonDemoPresentationActionActor(actor);
        if (!presentation)
            return false;
        if (!routeName)
            routeName = "?";

        if (tryStartMoonDemoPresentationAction(presentation, actionName))
            return true;

        const char* substitute = findMoonDemoActionAlias(actionName);
        if (tryStartMoonDemoPresentationAction(presentation, substitute)) {
            Randomizer::logPush("%s demo: skin %p no \"%s\", using \"%s\"", routeName, presentation, actionName, substitute);
            return true;
        }

        const bool idling = startMoonDemoPresentationIdle(actor);
        Randomizer::logPush("%s demo: skin %p no \"%s\", no \"%s\" either, %s", routeName, presentation, actionName, substitute ? substitute : "<none>",
            idling ? "idling" : "unchanged");
        return false;
    }

    // What a model gets when the choreography asks for a beat it has no form of
    // at all. It must be an *idle*: starting an unrelated timed sequence runs its
    // own timeline underneath the cutscene, which reads as the moon doing
    // something disconnected from Mario and the camera. An idle spin is the only
    // honest stand-in: it never claims to be in step with anything.
    static bool startMoonDemoPresentationIdle(al::LiveActor* actor) {
        if (!actor)
            return false;
        // Shine's animation resources are owned by its selected model child.
        // Starting an action on the root can leave the visible moon frozen even
        // while the root itself is correctly advancing in the demo list.
        al::LiveActor* presentation = getMoonDemoPresentationActionActor(actor);
        if (!presentation)
            return false;
        static const char* const cIdleActions[] = { "Spin", "Rotate", "Idle", "Wait" };
        for (const char* idle : cIdleActions) {
            // Reached once per unmatched phase boundary, so an idle that is
            // already running is left alone rather than visibly reset. Models
            // with no ActionKeeper cannot answer that question, and take the
            // animation route inside tryStartMoonDemoPresentationAction.
            if (isActionPlayingAtMainOffset(presentation, idle))
                return true;
            if (tryStartMoonDemoPresentationAction(presentation, idle))
                return true;
        }
        // Nothing idle-shaped either: leave whatever it is already playing
        // rather than freezing it on a pose of our choosing.
        return false;
    }

    // Both grand-demo steps below wait on an action to report itself finished:
    // DemoGetFirst on the demo shine actor's "GetShineGrand", DemoGetStart on the
    // player's. A model that cannot play its action never satisfies that, and the
    // state then sits in the same step forever with the world paused and the
    // player bound -- only the camera still answers. A randomized reward must
    // never be able to end a run that way, so a step that overruns by a wide
    // margin is pushed to the nerve it was waiting to reach. The demo continues
    // from there, at worst having skipped part of its animation.
    static constexpr uintptr_t cGrandNrvDemoGetStartOffset = 0x01d8dd38;
    static constexpr uintptr_t cGrandNrvDemoAppearLayoutOffset = 0x01d8de98;
    static void advanceGrandDemo(void* state, uintptr_t nerveOffset) {
        al::setNerve(reinterpret_cast<al::IUseNerve*>(state), reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + nerveOffset));
    }

    // Each waiting step of the grand demo watches the actor whose action it is
    // waiting on, and only gives up once that action has demonstrably stopped
    // advancing -- a plain frame budget cannot tell "still animating" from
    // "frozen" and will cut a legitimately long step short. A last-resort cap
    // stays, far beyond any real cutscene, so nothing can wait forever.
    static constexpr s32 cGrandDemoStallFrames = 30; // 0.5s with no animation progress
    static constexpr s32 cGrandDemoHardCapStep = 1800; // 30s: only a total stall reaches this

    struct GrandDemoStallWatch {
        const void* phase;
        const void* state;
        f32 frame;
        s32 frozenFrames;
    };
    static GrandDemoStallWatch sGrandDemoStall = {};

    // waitingOn may be null: pass it only for a step whose exit is gated on a
    // plain al action. The player's demo action is not one (hence the game's own
    // rs::isActionEndDemoPlayer), and its al action frame would read as
    // permanently frozen, so a player-gated step gets the hard cap alone.
    static bool tryUnstickGrandDemo(void* state, const al::LiveActor* waitingOn, uintptr_t nerveOffset, const char* stepName) {
        const s32 step = al::getNerveStep(reinterpret_cast<const al::IUseNerve*>(state));
        f32 frame = -1.0f;
        bool stalled = false;

        if (waitingOn) {
            frame = getActionFrameAtMainOffset(waitingOn);
            if (sGrandDemoStall.phase != stepName || sGrandDemoStall.state != state || step <= 1) {
                sGrandDemoStall = { stepName, state, frame, 0 };
                return false;
            }
            if (frame > sGrandDemoStall.frame) {
                sGrandDemoStall.frame = frame;
                sGrandDemoStall.frozenFrames = 0;
            } else {
                sGrandDemoStall.frozenFrames++;
            }
            stalled = sGrandDemoStall.frozenFrames >= cGrandDemoStallFrames;
        }

        if (!stalled && step < cGrandDemoHardCapStep)
            return false;
        Randomizer::logPush("grand demo: %s is not progressing (action frame %d held for %d frames, step %d), "
                            "advancing past it",
            stepName, (s32)frame, sGrandDemoStall.frozenFrames, step);
        advanceGrandDemo(state, nerveOffset);
        return true;
    }

    // Vanilla's own grand-moon nerve (Shine::exeDemoGetGrand) does exactly one
    // thing on its first step: al::invalidateClipping. That is what keeps the
    // physical shine updating for the whole demo -- a clipped actor is skipped by
    // movement/calcAnim, so its action stays parked on frame 0, "playing" but
    // never ending, and the step's al::isActionEnd never arrives. A shine that
    // only became Grand after Shine::init runs a different nerve and never gets
    // that call, and the grand demo camera pulling back is enough to clip it.
    // Nothing more than this: registering the shine as a demo actor as well would
    // update it twice a frame and run its animation at double speed.
    static void keepGrandDemoShineUpdating(const al::Scene* scene) {
        if (al::LiveActor* demoActor = getDemoShineActorByOffset(scene))
            al::invalidateClipping(demoActor);
    }

    static HkTrampoline<void, void*> grandMoonDemoGetFirstHook = hk::hook::trampoline([](void* state) -> void {
        const al::Scene* scene = getStateScene(state);
        const s32 step = al::getNerveStep(reinterpret_cast<const al::IUseNerve*>(state));
        if (step == 0) {
            Shine* demoShine = getDemoShineForRead(scene);
            al::LiveActor* demoActor = demoShine;
            claimMoonGetDemoContext(state, MoonDemoRoute::Grand, demoShine, false);
            if (cLogMoonGetDetails) {
                Randomizer::logPush("grand demo first step: actor=%p name=%s has GetShineGrand=%d requester=%p", demoActor,
                    demoActor && demoActor->getName() ? demoActor->getName() : "?", (s32)isExistActionAtMainOffset(demoActor, "GetShineGrand"),
                    sGrandMoonDemoRequestShine);
            }
            // Claim the later phases' presentation slots before exeDemoGetStart
            // selects one of them into +0x98.
            bindGrandMoonDemoActor(state);
            // Before orig starts the action, so the actor is already unclipped
            // when its first frame is evaluated.
            keepGrandDemoShineUpdating(scene);
        }
        const al::LiveActor* riseDriver = sGrandMoonPuppet.riseActive ? sGrandMoonPuppet.riseDriver : getDemoShineActorByOffset(scene);
        if (tryUnstickGrandDemo(state, riseDriver, cGrandNrvDemoGetStartOffset, "DemoGetFirst"))
            return;
        grandMoonDemoGetFirstHook.orig(state);
    });

    // Every moon-get state calls this once per frame. Both puppets only do work
    // while their own state owns them.
    static HkTrampoline<void, al::Scene*> updateKitListDemoPlayerHook = hk::hook::trampoline([](al::Scene* scene) -> void {
        updateGrandMoonRiseSourceProxy(false);
        updateLocationMoonDemoPuppet();
        updateKitListDemoPlayerHook.orig(scene);
        updateGrandMoonRiseSourceProxy(true);
        updateGrandMoonPuppet();
    });

    // StageSceneStateGetShineGrand::kill ends the grand cutscene however it got
    // there, so it is the one place that must always hand the state's own actors
    // back and drop the substitute clock.
    static HkTrampoline<void, void*> grandMoonDemoStateKillHook = hk::hook::trampoline([](void* state) -> void {
        detachGrandMoonPuppet();
        sGrandRiseSubstituted = false;
        grandMoonDemoStateKillHook.orig(state);
    });

    // The Dot route owns only state +0x40. Its source Shine remains global and
    // native because the native route copies pose/orientation from it before
    // animating the generic presentation actor.
    static MoonDemoBindResult bindDotMoonDemoActor(void* state) {
        const al::Scene* scene = getStateScene(state);
        Shine* sourceShine = getDemoShineForRead(scene);
        al::LiveActor* model = nullptr;
        const char* archive = nullptr;
        const MoonDemoBindResult result = selectCustomMoonDemoActor(state, MoonDemoRoute::Dot, sourceShine, false, &model, &archive);
        if (result != MoonDemoBindResult::Bound)
            return result;

        rememberGetShineStockActors(state);
        *reinterpret_cast<al::LiveActor**>(reinterpret_cast<uintptr_t>(state) + 0x40) = model;
        sGetShineStockActors.overriddenMask |= 0x2;
        sGetShineStockActors.route = MoonDemoRoute::Dot;
        prepareCustomMoonDemoPresentationActor(model);
        registerCurrentMoonGetDemoColor(model);
        logMoonDemoNativeActorInvariant("dot", scene);
        if (cLogMoonGetDetails) {
            Randomizer::logPush("dot demo: bound archive=%s actor=%p source=%p slot=40", archive ? archive : "?", model, sourceShine);
        }
        sMoonGetProfileArchive = archive;
        return MoonDemoBindResult::Bound;
    }

    static HkTrampoline<void, void*> dotMoonDemoGetHook = hk::hook::trampoline([](void* state) -> void {
        // exeDemoGet is shared by normal, special, and Dot rewards. Native
        // only selects state +0x40 when this exact ProjectDemoDirector
        // predicate is true; normal rewards arrive here immediately after
        // their +0x38/+0x48 setup. Do not infer Dot from the shared nerve.
        const al::Scene* scene = getStateScene(state);
        const bool isFirst = al::isFirstStep(reinterpret_cast<const al::IUseNerve*>(state));
        if (isFirst && isActiveDemoShineDotGetAtMainOffset(scene)) {
            if (bindDotMoonDemoActor(state) == MoonDemoBindResult::Failed) {
                // appear() sends Dot rewards straight to this nerve, so
                // exeDemoGetFirst never ran and + 0x50 is still the
                // constructor's null. Skipping orig leaves it that way, and
                // exeDemoShineCount dereferences the slot unguarded.
                fillGetShineDemoActorSlot(state, 0x40);
                logMoonGetPickupTimings("dot-unstick");
                advanceGetShineDemoToCount(state, "dot");
                return;
            }
        }
        // First step of NrvDemoGet is where the visible get animation starts,
        // so it closes the pickup timing window for the shared routes.
        // Logged after the Dot bind above so its archive is recorded too.
        if (isFirst)
            logMoonGetPickupTimings(isActiveDemoShineDotGetAtMainOffset(scene) ? "dot" : "normal");
        else
            // Not on the first step: that one is the handover frame the line
            // above already reports, and its gap is measured from there.
            sampleMoonGetPostFrame();
        dotMoonDemoGetHook.orig(state);
    });

    static HkTrampoline<void, void*> grandMoonDemoGetStartHook = hk::hook::trampoline([](void* state) -> void {
        // Gated on the player's demo action, which no al action frame reflects:
        // hard cap only. This step legitimately runs long (wipe close, result
        // layout, achievement save), and the earlier flat 7s budget was cutting
        // it off mid-cutscene.
        if (tryUnstickGrandDemo(state, nullptr, cGrandNrvDemoAppearLayoutOffset, "DemoGetStart"))
            return;
        captureGrandMoonDemoColor(state);
        grandMoonDemoGetStartHook.orig(state);
        // The native first-step logic chooses the normal or empty grand-moon actor at +0x98,
        // starts its action, and applies the current kingdom's stage frame. Rebind and recolor
        // after that update as well so both the selected actor and its later Color writes stay
        // on the randomized item's kingdom.
        captureGrandMoonDemoColor(state);
    });

    static HkTrampoline<bool, const al::Scene*> isExistDemoReturnToHomeHook = hk::hook::trampoline([](const al::Scene* scene) -> bool {
        // StageScene checks ReturnToHome before ShineGrand. A grand-moon
        // collection can set the home flag while its demo request is already
        // active, so hide ReturnToHome until the grand demo reaches DemoEnd.
        if (sGrandMoonDemoPending)
            return false;
        return isExistDemoReturnToHomeHook.orig(scene);
    });

    static HkTrampoline<void, void*> grandMoonDemoEndHook = hk::hook::trampoline([](void* state) -> void {
        // DemoEnd is the intended point for vanilla to request ReturnToHome.
        // Release the priority guard before running the native first step.
        const ShineDB::ShineEntry* physical = sMoonGetDemoContext.physical;
        const bool isBossRematch = physical && isBossRematchStageName(physical->stage);
        sGrandMoonDemoPending = false;
        sGrandMoonDemoRequestShine = nullptr;
        completeMoonGetDemoContext(nullptr, MoonDemoRoute::Grand);
        grandMoonDemoEndHook.orig(state);

        // Boss rematches already own their authored return to the painting
        // room. With entrance randomization the logical current world can be
        // non-Peach, causing retail DemoEnd to request ReturnToHome as well.
        // Do not carry that second transition into the destination scene.
        if (isBossRematch) {
            const al::Scene* scene = getStateScene(state);
            GameDataHolderAccessor accessor(scene);
            clearDemoReturnToHome(accessor.mData ? accessor.mData->getGameDataFile() : nullptr, "boss-rematch grand demo");
        }
    });

    static HkTrampoline<bool, const al::LiveActor*> isPlayerDamageStopDemoHook = hk::hook::trampoline([](const al::LiveActor* actor) -> bool {
        // A grand-moon get from a boss refight leaves GameDataFile's deferred
        // ReturnToHome flag set (refights have no scripted "peace" sequence to
        // consume it, unlike a first-time boss kill). The next unrelated stage's
        // StageScene::init() -> tryChangeDemo() then sees that flag and calls
        // this with the new scene's player actor, which doesn't exist yet at
        // that point in init(), crashing inside al::getPlayerActor. A null actor
        // can't be damage-stopped, so this is a pure safety net.
        if (!actor)
            return false;
        return isPlayerDamageStopDemoHook.orig(actor);
    });

    static const ShineDB::ShineEntry* findEntryFromShineInfo(const ShineInfo* shineInfo) {
        if (!shineInfo || shineInfo->isEmpty())
            return nullptr;
        s32 objNum = parseShineObjId(shineInfo->mObjId.cstr());
        if (objNum < 0)
            return nullptr;
        return ShineDB::findEntry(ShineDB::findUid(shineInfo->mStageName.cstr(), objNum), shineInfo->mStageName.cstr());
    }

    static bool isShineInfoForEntry(const ShineInfo* shineInfo, const ShineDB::ShineEntry* entry) {
        if (!shineInfo || !entry || shineInfo->isEmpty() || entry->objId < 0)
            return false;
        return parseShineObjId(shineInfo->mObjId.cstr()) == entry->objId && std::strcmp(shineInfo->mStageName.cstr(), entry->stage) == 0;
    }

    static bool initShineInfoFromEntry(ShineInfo* shineInfo, const ShineDB::ShineEntry* entry) {
        if (!shineInfo || !entry || entry->objId < 0)
            return false;

        shineInfo->mStageName.copy(entry->stage);
        shineInfo->mObjId.format("obj%d", entry->objId);
        shineInfo->mLabel.format("ScenarioName_obj%d", entry->objId);
        shineInfo->setType(destPaysTriple(entry) ? ShineInfo::Type_GrandShine : ShineInfo::Type_Shine);
        return true;
    }

    static bool grantMoonDirectWithSource(GameDataHolderWriter writer,
        const ShineDB::ShineEntry* entry,
        const ShineDB::ShineEntry* displayLocationOverride,
        MoonRewardSource rewardSource) {
        ShineInfo shineInfo;
        if (!initShineInfoFromEntry(&shineInfo, entry))
            return false;

        const MoonRando::Placement* placement = MoonRando::isInitialized() ? MoonRando::getPlacementForItem(entry->uid, entry->stage) : nullptr;
        const ShineDB::ShineEntry* displayLocation = displayLocationOverride ? displayLocationOverride : (placement && placement->location ? placement->location : entry);

        // Direct grants have no physical Shine actor to attach later. Keep an
        // explicit Direct route so a stale direct reward cannot be claimed by a
        // subsequent world pickup; special/NPC routes may claim it explicitly.
        beginMoonGetDemoContext(displayLocation, entry, MoonDemoRoute::Direct);

        const bool oldBypass = sBypassCreditRemap;
        const MoonRewardSource oldRewardSource = sMoonRewardSource;
        sBypassCreditRemap = true;
        sMoonRewardSource = rewardSource;
        GameDataFunction::setGotShine(writer, &shineInfo);
        sMoonRewardSource = oldRewardSource;
        sBypassCreditRemap = oldBypass;

        sLatestGotDisplayEntry = entry;
        sLatestGotPhysicalEntry = displayLocation;
        sLatestGotDestinationEntry = entry;
        sLatestGotUsePhysicalWorld = true;
        Randomizer::logPush("direct moon grant display location=%s item=%s", displayLocation && displayLocation->name ? displayLocation->name : "<none>",
            entry && entry->name ? entry->name : "<none>");
        return true;
    }

    bool grantMoonDirect(GameDataHolderWriter writer, const ShineDB::ShineEntry* entry,
        const ShineDB::ShineEntry* displayLocationOverride) {
        return grantMoonDirectWithSource(
            writer, entry, displayLocationOverride, MoonRewardSource::DirectPurchase);
    }

    static bool autoGrantHasDetailedNotification(const ShineDB::ShineEntry& entry) {
        if (CaptureRando::getCaptureCountForMoon(entry) > 0
            || AbilityRando::getAbilityCountForMoon(entry) > 0
            || CaptureRando::isFoolMoon(entry) || AbilityRando::isFoolMoon(entry))
            return true;
        if (MoonRando::getSubareaKeySlotForItem(entry.uid, entry.stage) >= 0)
            return true;
        return PaintingRando::getCloudMoonPaintingUnlockCount(entry) > 0;
    }

    bool grantMoonAuto(GameDataHolderWriter writer, const ShineDB::ShineEntry* entry,
        const ShineDB::ShineEntry* displayLocationOverride) {
        const bool alreadyCollected = writer.mData && writer->getGameDataFile()
            && isMoonAcquiredDirect(writer->getGameDataFile(), entry);
        const bool granted = grantMoonDirectWithSource(
            writer, entry, displayLocationOverride, MoonRewardSource::AutoGrant);
        if (granted && !alreadyCollected && entry
            && !autoGrantHasDetailedNotification(*entry))
            AbilityOverlay::queueAutoGrant(entry->name
                    ? entry->name
                    : "Granted at start");
        return granted;
    }

    void applyRemoteShine(GameDataHolderWriter writer, const ShineInfo* shineInfo) {
        if (!shineInfo)
            return;
        const bool oldBypass = sBypassCreditRemap;
        // GameDataFile::setGotShine arms mIsPlayDemoReturnToHome whenever the
        // credited slot is a Grand Shine, and StageScene plays that "back to
        // the Odyssey" demo off the flag alone - it has no idea the moon
        // belonged to somebody else. A peer collecting a Grand Moon (live, or
        // replayed in a batch as this client joins) would otherwise rip this
        // player out of whatever they were doing for a cutscene they had no
        // part in. Same reasoning as the demo suppression in
        // collectStageShineSilently below: a remote grant is bookkeeping on
        // this client, never a performance.
        //
        // Sampled around the grant rather than cleared unconditionally: this
        // runs on the game thread at packet-drain time, which can land inside
        // the local player's own Grand Moon demo, and that demo legitimately
        // requests the same flag at DemoEnd. Only a request this grant itself
        // raised is taken back.
        GameDataFile* file = writer->getGameDataFile();
        const bool hadReturnToHome = isDemoReturnToHomeRequested(file);

        RemoteShineGrantScope remoteGrantScope;
        sBypassCreditRemap = true;
        GameDataFunction::setGotShine(writer, shineInfo);
        sBypassCreditRemap = oldBypass;

        if (!hadReturnToHome)
            clearDemoReturnToHome(file, "remote shine grant");
    }

    void collectStageShineSilently(Shine* shine) {
        if (!shine)
            return;

        // The receive path used to hand a peer's moon to the live Shine actor
        // via Shine::getDirect(), which is the *player pickup* entry point:
        // it resets the shine onto the player, calls
        // rs::requestStartDemoShineGet (or, for a main/story shine, adds the
        // demo actors and enters the demo nerve itself) and hands the scene
        // to the moon-get cutscene. On the collecting player that is correct;
        // on everyone else it hijacked the camera for a moon they did not
        // touch - and on join, once per moon in the replayed backlog. It is
        // also how an untouched Scarecrow/P-Switch/NPC moon got dragged
        // through its appear demo here, since the actor was force-revived
        // first.
        //
        // A remote grant is bookkeeping: credit the moon (the caller does
        // that through applyRemoteShine) and make the world agree, with no
        // demo, no camera and no player control loss.
        const bool wasAlive = !al::isDead(shine);

        if (wasAlive) {
            // Still fire the stage switches the moon's collection drives
            // (SwitchGetOn / SwitchNoSaveGetOn / SwitchShineGetOn), so doors,
            // cages and platforms that key off "this moon is gone" stay
            // consistent with the save this client just credited. What we do
            // NOT want is onSwitchGet's other half: it ends with
            // GameDataFunction::tryChangeNextStage on the shine's own
            // ChangeStageInfo, which for any moon that exits a sub-area would
            // warp this player out of their stage because a peer bought
            // something in a shop. Neutralise the payload for the call, the
            // same technique HintSystem's shineOnSwitchGetHook already uses
            // for an unsafe one, and put the original back afterwards.
            GameDataHolderAccessor accessor(static_cast<const al::IUseSceneObjHolder*>(shine));
            ChangeStageInfo emptyInfo(accessor.mData, "", "", false, -1,
                ChangeStageInfo::NO_SUB_SCENARIO);
            ChangeStageInfo* originalInfo = shine->mChangeStageInfo;
            shine->mChangeStageInfo = &emptyInfo;
            shine->onSwitchGet();
            shine->mChangeStageInfo = originalInfo;
        }

        // onSwitchGet's tail kills the actor itself (the same vfunc
        // Shine::getDirect ends on), so this is normally already done - but a
        // shine that was dead when the packet landed never ran it, and must
        // stay dead rather than being revived into a demo. Either way the
        // moon is gone from this player's world without a cutscene.
        if (!al::isDead(shine))
            shine->makeActorDead();

        Randomizer::logPush("remote shine applied silently (wasAlive=%d)", wasAlive ? 1 : 0);
    }

    bool isRemoteShineGrantActive() {
        return sMoonRewardSource == MoonRewardSource::RemoteSync;
    }

    bool isAutoGrantActive() {
        return sMoonRewardSource == MoonRewardSource::AutoGrant;
    }

    bool isMoonAcquiredDirect(GameDataFile* file, const ShineDB::ShineEntry* entry) {
        if (!file)
            return false;

        ShineInfo shineInfo;
        if (!initShineInfoFromEntry(&shineInfo, entry))
            return false;

        const bool oldChecking = sCheckingDestSlot;
        sCheckingDestSlot = true;
        const bool isAcquired = file->isGotShine(&shineInfo);
        sCheckingDestSlot = oldChecking;
        return isAcquired;
    }

    static bool isKingdomRequirementSatisfied(GameDataFile* file, s32 kingdomId) {
        if (!file || kingdomId < 0 || kingdomId >= WorldMeta::sWorldCount)
            return false;

        const bool visited = file->getCurrentWorldId() == kingdomId || (Randomizer::getVisitedKingdoms() & (1u << kingdomId)) != 0;
        const s32 requirement = getSavedKingdomMoonRequirement(file, kingdomId);
        const s32 collected = file->getShineNum(kingdomId);
        return visited && requirement > 0 && collected >= requirement;
    }

    static bool isCompletedKingdomShineSpawnEligible(GameDataFile* file, const ShineInfo* info) {
        if (!Randomizer::isCompletedKingdomMoonCollectedEnabled() || !file || !info || info->isEmpty() || info->isShop())
            return false;

        const ShineDB::ShineEntry* physical = findEntryFromShineInfo(info);
        if (!physical)
            return false;

        const ShineDB::ShineEntry* destination = physical;
        if (MoonRando::isInitialized()) {
            if (const ShineDB::ShineEntry* mapped = MoonRando::getCreditMoon(info->mObjId.cstr(), info->mStageName.cstr()))
                destination = mapped;
        }

        return isEligibleCompletedKingdomMoon(physical, destination) && isKingdomRequirementSatisfied(file, destWorld(destination));
    }

    static bool registerForcedCompletedKingdomShine(Shine* shine, const ShineInfo* info) {
        if (!shine || !info)
            return false;
        if (isForcedCompletedKingdomShine(shine, info))
            return true;
        if (sForcedCompletedKingdomShineCount >= cForcedCompletedKingdomShineMax)
            return false;

        sForcedCompletedKingdomShines[sForcedCompletedKingdomShineCount++] = {
            shine,
            info,
        };
        return true;
    }

    static bool isSameForcedShineInfo(const ShineInfo* lhs, const ShineInfo* rhs) {
        if (!lhs || !rhs)
            return false;
        if (lhs == rhs)
            return true;
        return !lhs->isEmpty() && !rhs->isEmpty() && lhs->mObjId == rhs->mObjId && lhs->mStageName == rhs->mStageName;
    }

    static bool isForcedCompletedKingdomShine(const Shine* shine, const ShineInfo* info) {
        if (!shine && !info)
            return false;
        for (s32 i = 0; i < sForcedCompletedKingdomShineCount; i++) {
            const ForcedCompletedKingdomShine& entry = sForcedCompletedKingdomShines[i];
            if ((shine && entry.shine == shine) || (info && isSameForcedShineInfo(entry.info, info)))
                return true;
        }
        return false;
    }

    static Shine* findForcedCompletedKingdomShine(const ShineInfo* info) {
        if (!info)
            return nullptr;
        for (s32 i = 0; i < sForcedCompletedKingdomShineCount; i++)
            if (isSameForcedShineInfo(sForcedCompletedKingdomShines[i].info, info))
                return sForcedCompletedKingdomShines[i].shine;
        return nullptr;
    }

    static void consumeForcedCompletedKingdomShine(const Shine* shine, const ShineInfo* info) {
        for (s32 i = 0; i < sForcedCompletedKingdomShineCount; i++) {
            const ForcedCompletedKingdomShine& entry = sForcedCompletedKingdomShines[i];
            if ((!shine || entry.shine == shine) && (!info || isSameForcedShineInfo(entry.info, info))) {
                sForcedCompletedKingdomShines[i] = sForcedCompletedKingdomShines[--sForcedCompletedKingdomShineCount];
                sForcedCompletedKingdomShines[sForcedCompletedKingdomShineCount] = {};
                return;
            }
        }
    }

    static void finishForcedCompletedKingdomShine(Shine* shine, const ShineInfo* info) {
        if (!isForcedCompletedKingdomShine(shine, info))
            return;
        consumeForcedCompletedKingdomShine(shine, info);
        // A forced actor never entered a Moon Get demo. Drop any stale context
        // left by an earlier route before the native repeat reaction continues.
        clearMoonGetDemoContext();
    }

    static bool commitForcedCompletedKingdomShine(Shine* shine) {
        if (!shine || !shine->curShineInfo || !isForcedCompletedKingdomShine(shine, shine->curShineInfo))
            return false;

        GameDataHolderAccessor accessor(shine);
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : tryGetCurrentGameDataFile();
        if (!file)
            return false;

        const ShineDB::ShineEntry* destination = findDisplayEntryFromShineInfo(shine->curShineInfo);
        if (destination && isMoonAcquiredDirect(file, destination)) {
            finishForcedCompletedKingdomShine(shine, shine->curShineInfo);
            return false;
        }

        GameDataFunction::setGotShine(GameDataHolderWriter(file->getGameDataHolder()), shine->curShineInfo);
        finishForcedCompletedKingdomShine(shine, shine->curShineInfo);
        return true;
    }

    // This is called only from the BL inside Shine::init. The normal acquisition
    // query always runs first; the override is therefore invisible to every
    // other GameDataFunction/GameDataFile/NPC/event caller.
    static bool shineInitIsGotShineHook(GameDataHolderAccessor accessor, const ShineInfo* info) {
        const bool isGot = GameDataFunction::isGotShine(accessor, info);
        if (isGot)
            return true;

        CompletedKingdomShineInitContext* context = sCompletedKingdomShineInitContext;
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        if (!context || !context->shine || isShopShineInit(context->shine) || !isCompletedKingdomShineSpawnEligible(file, info))
            return false;

        return registerForcedCompletedKingdomShine(context->shine, info);
    }

    static HkTrampoline<void, Shine*> shineGetHook = hk::hook::trampoline([](Shine* shine) -> void {
        // Shine::get normally performs the setGotShine itself. Let that
        // native call (and our normal remap/save hook) happen first, then
        // use the fallback only for routes that do not issue it.
        const bool forced = isForcedCompletedKingdomShine(shine, shine ? shine->curShineInfo : nullptr);
        shineGetHook.orig(shine);
        if (forced)
            commitForcedCompletedKingdomShine(shine);
    });

    static bool isMoonRockOpenAttemptMessage(const al::SensorMsg* message) {
        return message && (rs::isMsgCapAttack(message) || rs::isMsgCapTouchWall(message) || rs::isMsgPlayerAndCapHipDropAll(message));
    }

    static s32 tryGetMoonRockActorKingdom(const al::LiveActor* actor) {
        if (!actor || !Randomizer::isMoonRockKeyEnabled() || !MoonRando::isInitialized())
            return -1;

        GameDataHolderAccessor accessor(actor);
        const char* stageName = accessor.mData ? GameDataFunction::getCurrentStageName(accessor) : nullptr;
        if (!stageName)
            return -1;

        // Shiveria Town is the only Snow Kingdom sub-area whose randomizable
        // entrances (SnowCostumeEx, ShopDoor) can drop the player there
        // directly from an unrelated kingdom, bypassing SnowWorldHomeStage
        // entirely - so it is the only sub-stage that needs an explicit
        // kingdom mapping here.
        s32 worldId;
        if (std::strcmp(stageName, "SnowWorldTownStage") == 0) {
            worldId = WorldMeta::WorldId_Snow;
        } else {
            const WorldMeta::Entry* entry = WorldMeta::findByMainStage(stageName);
            worldId = entry ? entry->worldId : -1;
        }

        if (worldId < 0 || !MoonRando::kingdomHasMoonRockKey(worldId))
            return -1;
        return worldId;
    }

    static bool isMoonRockKeyMissingForActor(const al::LiveActor* actor) {
        const s32 kingdom = tryGetMoonRockActorKingdom(actor);
        if (kingdom < 0)
            return false;

        const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
        if (!key)
            return false;

        GameDataHolderAccessor accessor(actor);
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        // A team-mate has credited this kingdom's key moon. Additive to the
        // general moon-sync pipeline, which is scene-readiness-gated and has a
        // documented history of missing a credit - and this one gates a real
        // soft-lock, so it gets its own signal.
        //
        // The signal is now a durable shared event backed by a persisted mask
        // (DurableSync), not the session-only RAM array this used to read. That
        // array was never saved and never replayed on join, which is why a late
        // joiner arrived at a Moon Rock they could not open and why the same
        // player lost the unlock again on every restart.
        if (Client::hasRemoteMoonRockKeyUnlock(kingdom))
            return false;
        return !isMoonAcquiredDirect(file, key);
    }

    static bool isMoonRockKeyBlockedMessage(const char* fileName, const char* key) {
        return fileName && key && std::strcmp(fileName, "CapMessage") == 0 && std::strcmp(key, cMoonRockKeyBlockedMessageLabel) == 0;
    }

    const char16_t* tryMakeMoonRockKeyCapMessage(const char* fileName, const char* key) {
        if (!isMoonRockKeyBlockedMessage(fileName, key))
            return nullptr;
        return u"We need to find the Moon Rock Key\nbefore we can open the Moon Rock.";
    }

    static bool showMoonRockKeyBlockedMessage(const al::IUseSceneObjHolder* holder) {
        return holder && rs::tryShowCapMessage(holder, cMoonRockKeyBlockedMessageLabel, 90, 0);
    }

    // Subarea Keys. The denial happens inside the stage transition, long before
    // there is a scene to talk through, so the reason is parked here and the
    // line is spoken once the bounce lands and the player has control back.
    static bool sSubareaKeyMessagePending = false;
    // The refusal is armed while the player is still walking INTO the door, so
    // the line must not start counting down yet: the outgoing wipe and the stage
    // reload are still ahead of it, and a message shown during either is torn
    // down with the scene and only flashes. Wait for the arrival to re-init the
    // stage, then settle, then speak.
    static bool sSubareaKeyMessageAwaitingArrival = false;
    static s32 sSubareaKeyMessageDelay = 0;
    static s32 sSubareaKeyMessageKingdom = -1;
    static SubareaKeyDB::SubareaType sSubareaKeyMessageType = EntranceDB2::SubareaType_None;
    // The stage the bounce is returning to. The line belongs to that arrival and
    // nowhere else - see noteSubareaKeyDeniedArrival.
    static sead::FixedSafeString<0x40> sSubareaKeyMessageStage;
    static s32 sSubareaKeyMessageWaited = 0;
    static bool sSubareaKeyMessageLoggedRefusal = false;
    // Settle time after the arrival, so the incoming wipe has finished and Mario
    // is standing outside the door again before Cappy speaks.
    static constexpr s32 cSubareaKeyMessageDelayFrames = 60;
    // How long to keep *asking* before taking the channel outright. In testing
    // tryShowCapMessage refused for eleven seconds straight on one refusal while
    // an ambient Cappy line held the channel, then took instantly on every later
    // one - so asking politely works in the common case and needs a fallback.
    static constexpr s32 cSubareaKeyMessagePoliteFrames = 60;

    void armSubareaKeyDeniedMessage(s32 kingdom, s32 subareaType, const char* returnStage) {
        sSubareaKeyMessagePending = true;
        sSubareaKeyMessageAwaitingArrival = true;
        sSubareaKeyMessageDelay = cSubareaKeyMessageDelayFrames;
        sSubareaKeyMessageWaited = 0;
        sSubareaKeyMessageLoggedRefusal = false;
        sSubareaKeyMessageKingdom = kingdom;
        sSubareaKeyMessageType = static_cast<SubareaKeyDB::SubareaType>(subareaType);
        sSubareaKeyMessageStage.copy(returnStage ? returnStage : "");
    }

    void noteSubareaKeyDeniedArrival(const char* stageName) {
        if (!sSubareaKeyMessagePending)
            return;

        // The bounce lands back in the stage the player just tried to leave. Any
        // OTHER arrival means the player got somewhere else before the line was
        // ever spoken, so the refusal is stale - drop it. Without this the line
        // stays armed and surfaces in whatever stage finally accepts it, which
        // is how a Cloud Kingdom refusal ended up being spoken in a Sand Kingdom
        // sub-area two doors later.
        if (!sSubareaKeyMessageStage.isEmpty() && stageName && std::strcmp(sSubareaKeyMessageStage.cstr(), stageName) != 0) {
            Randomizer::logPush("subarea key message dropped: armed for %s, arrived at %s", sSubareaKeyMessageStage.cstr(), stageName);
            sSubareaKeyMessagePending = false;
            return;
        }

        sSubareaKeyMessageAwaitingArrival = false;
    }

    static bool isSubareaKeyBlockedMessage(const char* fileName, const char* key) {
        return fileName && key && std::strcmp(fileName, "CapMessage") == 0 && std::strcmp(key, cSubareaKeyBlockedMessageLabel) == 0;
    }

    const char16_t* tryMakeSubareaKeyCapMessage(const char* fileName, const char* key) {
        if (!isSubareaKeyBlockedMessage(fileName, key))
            return nullptr;

        static sead::WFixedSafeString<0x200> text;
        text = u"";
        text.appendWithFormat(u"We can't get into this kind of place\nin %s yet!\nWe need to find its %s key first.",
            WorldMeta::getDisplayName16ForWorldId(sSubareaKeyMessageKingdom), SubareaKeyDB::typeName(sSubareaKeyMessageType));
        return text.cstr();
    }

    void updateSubareaKeyDeniedMessage(const al::IUseSceneObjHolder* holder) {
        if (!sSubareaKeyMessagePending || sSubareaKeyMessageAwaitingArrival)
            return;
        if (sSubareaKeyMessageDelay > 0) {
            sSubareaKeyMessageDelay--;
            return;
        }

        if (!holder)
            return;

        // Ask first. With nothing else holding Cappy this takes immediately and
        // behaves like every other cap message in the game.
        if (rs::tryShowCapMessage(holder, cSubareaKeyBlockedMessageLabel, 90, 0)) {
            sSubareaKeyMessagePending = false;
            return;
        }

        if (!sSubareaKeyMessageLoggedRefusal) {
            sSubareaKeyMessageLoggedRefusal = true;
            Randomizer::logPush("subarea key message refused: active=%d delayed=%d", (s32)rs::isActiveCapMessage(holder), (s32)rs::isDelayCapMessage(holder));
        }

        // Still refused. This line is a direct answer to a door the player just
        // walked into, so it outranks whatever ambient hint is holding the
        // channel: take it outright rather than let the explanation go missing.
        // showCapMessage is the non-try variant and does not decline.
        if (++sSubareaKeyMessageWaited >= cSubareaKeyMessagePoliteFrames) {
            rs::showCapMessage(holder, cSubareaKeyBlockedMessageLabel, 90, 0);
            Randomizer::logPush("subarea key message forced after %d frame(s) of refusal", sSubareaKeyMessageWaited);
            sSubareaKeyMessagePending = false;
        }
    }

    // MoonRock::init and its linked cap-demo actor both query this native state
    // before choosing their active/Standby setup.  Keep the vanilla condition,
    // then hide the active (glowing) setup until this kingdom's key is owned.
    static HkTrampoline<bool, const al::LiveActor*> isEnableOpenMoonRockHook = hk::hook::trampoline([](const al::LiveActor* actor) -> bool {
        if (!isEnableOpenMoonRockHook.orig(actor))
            return false;

        return !Randomizer::isMoonRockKeyEnabled() || !isMoonRockKeyMissingForActor(actor);
    });

    static HkTrampoline<bool, al::LiveActor*, const al::SensorMsg*, al::HitSensor*, al::HitSensor*> moonRockReceiveMsgHook = hk::hook::trampoline([](al::LiveActor* self, const al::SensorMsg* message, al::HitSensor* other, al::HitSensor* sensor) -> bool {
        if (isMoonRockOpenAttemptMessage(message) && isMoonRockKeyMissingForActor(self)) {
            const bool isHipDrop = rs::isMsgPlayerAndCapHipDropAll(message);
            // Hip drops must keep their normal descent and land on the Moon Rock.
            // Report the blocked attempt, but do not claim the hip drop was handled:
            // doing so leaves the player's HipDrop state active while ZR is held.
            if (isHipDrop) {
                showMoonRockKeyBlockedMessage(self);
                return false;
            }
            if (other && sensor)
                rs::requestHitReactionToAttacker(message, other, sensor);
            showMoonRockKeyBlockedMessage(self);
            return true;
        }
        return moonRockReceiveMsgHook.orig(self, message, other, sensor);
    });

    static bool setPaneShineNameFromEntry(al::LayoutActor* layout, const char* pane, const ShineDB::ShineEntry* entry) {
        if (!layout || !pane || !entry)
            return false;

        if (entry->objId >= 0 && entry->stage) {
            al::StringTmp<0x40> label("ScenarioName_obj%d", entry->objId);
            if (rs::trySetPaneStageMessageIfExist(layout, pane, entry->stage, label.cstr()))
                return true;
        }

        if (!entry->name)
            return false;

        sead::WFixedSafeString<0x100> name;
        name.convertFromMultiByteString(entry->name, std::strlen(entry->name));
        al::setPaneString(layout, pane, name.cstr(), 0);
        return true;
    }

    static void appendPaneShineWorldName(al::LayoutActor* layout, const char* pane, s32 worldId) {
        sead::WFixedSafeString<0x100> text;
        text.appendWithFormat(u"\n%s\n(%s)", al::getPaneStringBuffer(layout, pane), ShineDB::getWorldNameFromId(worldId));
        al::setPaneString(layout, pane, text.cstr());
    }

    static const char16_t* makeMoonRockKeyNameMessage(const ShineDB::ShineEntry* entry);
    static const char16_t* makeSubareaKeyNameMessage(const ShineDB::ShineEntry* entry);
    static const char16_t* makeEntryHintStyleMessage(const ShineDB::ShineEntry* entry);

    static bool trySetRandomizedShineText(al::LayoutActor* layout, const char* pane, const ShineInfo* info) {
        if (!sLatestGotPhysicalEntry || !sLatestGotDestinationEntry)
            return false;
        if (info && !info->isEmpty() && !isShineInfoForEntry(info, sLatestGotDestinationEntry))
            return false;

        if (const char16_t* keyName = makeMoonRockKeyNameMessage(sLatestGotDestinationEntry)) {
            al::setPaneString(layout, pane, keyName, 0);
            return true;
        }
        if (const char16_t* keyName = makeSubareaKeyNameMessage(sLatestGotDestinationEntry)) {
            al::setPaneString(layout, pane, keyName, 0);
            return true;
        }

        // Toadette Medals keep their real name here (unlike Moon Rock Key) - the
        // "You Got a ..." header pane is overridden separately (see
        // makeToadetteMedalGetHeaderMessage) so the name subtitle stays genuine
        // and still tells the player what they actually picked up.

        if (sLatestGotUsePhysicalWorld) {
            const char16_t* message = makeEntryHintStyleMessage(sLatestGotPhysicalEntry);
            if (!message)
                return false;
            al::setPaneString(layout, pane, message, 0);
            return true;
        }

        if (!setPaneShineNameFromEntry(layout, pane, sLatestGotPhysicalEntry))
            return false;

        appendPaneShineWorldName(layout, pane, destWorld(sLatestGotDestinationEntry));
        return true;
    }

    bool isGotShineRaw(const GameDataFile* file, const ShineInfo* shineInfo) {
        if (!file || !shineInfo)
            return false;

        const bool wasCheckingDestSlot = sCheckingDestSlot;
        sCheckingDestSlot = true;
        const bool isGot = file->isGotShine(shineInfo);
        sCheckingDestSlot = wasCheckingDestSlot;
        return isGot;
    }

    static const ShineDB::ShineEntry* findTravelingPeachLocation(s32 worldId) {
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry& entry = ShineDB::sShineTable[i];
            if (entry.worldId != worldId || !entry.name)
                continue;
            if (std::strncmp(entry.name, "Peach in ", 9) == 0 || std::strcmp(entry.name, "Princess Peach, Home Again!") == 0)
                return &entry;
        }
        return nullptr;
    }

    static bool isRandomizedLocationCollected(const GameDataFile* file, const ShineDB::ShineEntry* location) {
        if (!file || !location)
            return false;

        const ShineDB::ShineEntry* item = MoonRando::getItemForLocation(location->uid, location->stage);
        if (!item)
            return false;

        ShineInfo itemInfo;
        if (!initShineInfoFromEntry(&itemInfo, item))
            return false;
        return isGotShineRaw(file, &itemInfo);
    }

    // Traveling Peach bypasses the ordinary linked-shine query and checks whether
    // the Peach-named moon item for a world is owned. Under moon randomization that
    // confuses a shuffled reward with the physical Peach location: obtaining "Peach
    // in the Lake Kingdom" at Cap, for example, makes Lake use its already-collected
    // dialogue. Resolve the queried world's physical Peach location first, then test
    // the item assigned to that location.
    static HkTrampoline<bool, GameDataHolderAccessor, const char*> checkGetShineForWorldTravelingPeachHook = hk::hook::trampoline([](GameDataHolderAccessor accessor, const char* worldName) -> bool {
        if (!MoonRando::isInitialized() || !accessor.mData || !worldName)
            return checkGetShineForWorldTravelingPeachHook.orig(accessor, worldName);

        const s32 worldId = GameDataFunction::findWorldIdByDevelopName(accessor, worldName);
        const ShineDB::ShineEntry* location = findTravelingPeachLocation(worldId);
        if (!location)
            return checkGetShineForWorldTravelingPeachHook.orig(accessor, worldName);

        return isRandomizedLocationCollected(accessor.mData->getGameDataFile(), location);
    });

    static HkTrampoline<bool, const al::LiveActor*> checkEnableAppearMoonWorldTravelingPeachHook = hk::hook::trampoline([](const al::LiveActor* actor) -> bool {
        if (!MoonRando::isInitialized() || !actor)
            return checkEnableAppearMoonWorldTravelingPeachHook.orig(actor);

        GameDataHolderAccessor accessor(actor);
        if (!accessor.mData)
            return checkEnableAppearMoonWorldTravelingPeachHook.orig(actor);

        const GameDataFile* file = accessor.mData->getGameDataFile();
        for (s32 worldId = WorldMeta::WorldId_Cap; worldId < WorldMeta::WorldId_Moon; worldId++) {
            if (!isRandomizedLocationCollected(file, findTravelingPeachLocation(worldId)))
                return false;
        }
        return true;
    });

    // A Subarea Key rides on an ordinary Cap/Moon Kingdom moon, so without this
    // its get demo looks like any other moon and the player never learns which
    // doors just opened. Announce it the same way a capture or ability unlock is
    // announced - same queue, so a moon carrying several things still shows them
    // together and in order.
    static void queueSubareaKeyUnlock(const ShineDB::ShineEntry& moon) {
        if (!Randomizer::isSubareaUnlockEnabled())
            return;
        const s32 slot = MoonRando::getSubareaKeySlotForItem(moon.uid, moon.stage);
        if (slot < 0)
            return;

        s32 kingdom = -1;
        SubareaKeyDB::SubareaType type = EntranceDB2::SubareaType_None;
        if (!SubareaKeyDB::keySlotAt(slot, &kingdom, &type))
            return;

        // A coarser mode leaves one half of the pair unnamed: a per-type key
        // covers every kingdom, a per-kingdom key every door kind.
        const char* scope = kingdom >= 0 ? WorldMeta::getDisplayNameForWorldId(kingdom) : "All-Kingdom";
        const char* kind = type != EntranceDB2::SubareaType_Count ? SubareaKeyDB::typeNameAscii(type) : "Subarea";
        static sead::FixedSafeString<0x80> sName;
        sName.format("%s %s Key", scope, kind);

        // Keys are unique per slot, so a re-collect cannot happen here (the
        // caller already returned for an already-collected location) and there
        // is no "(again)" case to feed.
        //
        // Bracketed even though it is a single entry: appendEntry alone leaves
        // the entry's index/count to whatever the batch machinery last set, so
        // an unbracketed key inherited the preceding capture batch's size and
        // announced itself as "1 / 3". Inside a moon's reward group this folds
        // into the group's numbering instead.
        AbilityOverlay::startBatchPublic();
        AbilityOverlay::appendEntry(sName.cstr(), false);
        AbilityOverlay::finishBatchPublic();
    }

    static void queueCloudPaintingUnlocks(GameDataHolderWriter writer, const ShineDB::ShineEntry& moon, const GameDataFile* file, bool alreadyCollected) {
        if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            return;

        const s32 unlockCount = PaintingRando::getCloudMoonPaintingUnlockCount(moon);
        for (s32 i = 0; i < unlockCount; i++) {
            const s32 paintingIndex = PaintingRando::getCloudMoonPaintingUnlockIndex(moon, i);
            if (paintingIndex < 0)
                continue;

            const char* sourceKingdom = nullptr;
            const char* destinationKingdom = nullptr;
            if (!PaintingRando::getPaintingUnlockKingdomNames(paintingIndex, &sourceKingdom, &destinationKingdom))
                continue;

            // Pre-commit: isPaintingUnlocked reflects other cloud moons (or a re-collect), so
            // "already unlocked" => this link is a duplicate -> "(again)" + bonus coins.
            const bool alreadyUnlocked = PaintingRando::isPaintingUnlocked(file, paintingIndex);
            PaintingUnlockOverlay::queue(sourceKingdom, destinationKingdom, alreadyUnlocked);
            if (alreadyUnlocked && !alreadyCollected)
                GameDataFunction::addCoin(writer, Randomizer::UnlockRewards::kPaintingDuplicateCoins);
        }
        // Update the Soft Nuzlocke carry state only after duplicate feedback has
        // observed the pre-collection state of the link.
        Nuzlocke::unlockPaintingForMoon(const_cast<GameDataFile*>(file), moon);
    }

    // A special-kingdom moon item pays filler coins only when its selector says Coins.
    // Other selector values reserve the item for unlocks even when it grants no direct
    // capture/ability feedback.
    static bool itemAwardsFillerCoins(const ShineDB::ShineEntry& entry) {
        if (CaptureRando::getCaptureCountForMoon(entry) > 0 || AbilityRando::getAbilityCountForMoon(entry) > 0)
            return false;
        if (CaptureRando::isFoolMoon(entry) || AbilityRando::isFoolMoon(entry))
            return false;

        // Redistributed kingdom: a moon's coin/unlock role follows the kingdom it
        // was reassigned to (its capture/ability role above already does).
        switch (destWorld(&entry)) {
        case WorldMeta::WorldId_Cap:
            return Randomizer::getCapKingdomMoonUse() == Randomizer::SpecialMoonUse_Coins;
        case WorldMeta::WorldId_Cloud:
            return Randomizer::getCloudKingdomMoonUse() == Randomizer::SpecialMoonUse_Coins;
        case WorldMeta::WorldId_Moon:
            return Randomizer::getMoonKingdomMoonUse() == Randomizer::SpecialMoonUse_Coins;
        case WorldMeta::WorldId_Mushroom:
            return !Randomizer::isCaptureRandoEnabled();
        case WorldMeta::WorldId_Dark:
        case WorldMeta::WorldId_Darker:
            return !Randomizer::isLockedAbilitiesEnabled();
        default:
            return false;
        }
    }

    // Hint Tokens use the same first-collection exclusions as filler coins,
    // but only the three user-selectable special kingdoms can be token sources.
    // This deliberately follows the credited/assigned destination kingdom.
    static bool itemAwardsHintTokens(const ShineDB::ShineEntry& entry) {
        if (CaptureRando::getCaptureCountForMoon(entry) > 0 || AbilityRando::getAbilityCountForMoon(entry) > 0 || CaptureRando::isFoolMoon(entry) || AbilityRando::isFoolMoon(entry))
            return false;

        switch (destWorld(&entry)) {
        case WorldMeta::WorldId_Cap:
            return Randomizer::getCapKingdomMoonUse() == Randomizer::SpecialMoonUse_HintTokens;
        case WorldMeta::WorldId_Cloud:
            return Randomizer::getCloudKingdomMoonUse() == Randomizer::SpecialMoonUse_HintTokens;
        case WorldMeta::WorldId_Moon:
            return Randomizer::getMoonKingdomMoonUse() == Randomizer::SpecialMoonUse_HintTokens;
        default:
            return false;
        }
    }

    static void awardMoonCurrencyReward(GameDataHolderWriter writer, GameDataFile* file, const ShineDB::ShineEntry& entry, bool alreadyCollected) {
        // Direct purchases never reimburse their own price. Remote sync uses
        // the same first-time check as a local pickup so each peer earns the
        // token exactly once for the credited item.
        if (alreadyCollected || sMoonRewardSource == MoonRewardSource::DirectPurchase)
            return;
        if (itemAwardsFillerCoins(entry)) {
            // Existing remote moon sync never awarded filler coins. Preserve
            // that economy while still mirroring the new token reward.
            if (sMoonRewardSource == MoonRewardSource::WorldPickup)
                GameDataFunction::addCoin(writer, Randomizer::UnlockRewards::kFillerCoins);
            return;
        }
        const s32 tokenAmount = destPaysTriple(&entry) ? 3 : 1;
        if (itemAwardsHintTokens(entry) && HintEconomy::awardHintTokens(file, tokenAmount)) {
            // AbilityOverlay is the established queued moon-reward overlay.
            // This appends after capture/ability feedback (the caller invokes
            // us after applyUnlockFeedback), preserving its existing batches
            // instead of competing with the native moon-get presentation.
            AbilityOverlay::startBatchPublic();
            AbilityOverlay::appendEntry(tokenAmount == 1 ? "Got a Hint Token!" : "Got 3 Hint Tokens!", false);
            AbilityOverlay::finishBatchPublic();
        }
    }

    static Randomizer::FoolPunishment getFoolPunishment(const ShineDB::ShineEntry& entry) {
        if (CaptureRando::isFoolMoon(entry))
            return CaptureRando::getFoolPunishment(entry);
        if (AbilityRando::isFoolMoon(entry))
            return AbilityRando::getFoolPunishment(entry);
        return Randomizer::FoolPunishment::None;
    }

    static void applyFoolPunishment(GameDataHolderWriter writer, const ShineDB::ShineEntry& entry) {
        const Randomizer::FoolPunishment punishment = getFoolPunishment(entry);
        s32 currentCoins = GameDataFunction::getCoinNum(writer);

        switch (punishment) {
        case Randomizer::FoolPunishment::Lose100Coins:
            GameDataFunction::subCoin(writer, currentCoins < 100 ? currentCoins : 100);
            break;
        case Randomizer::FoolPunishment::Lose500Coins:
            GameDataFunction::subCoin(writer, currentCoins < 500 ? currentCoins : 500);
            break;
        case Randomizer::FoolPunishment::Lose1000Coins:
            GameDataFunction::subCoin(writer, currentCoins < 1000 ? currentCoins : 1000);
            break;
        case Randomizer::FoolPunishment::LoseHalfCoins:
            GameDataFunction::subCoin(writer, currentCoins / 2);
            break;
        case Randomizer::FoolPunishment::GainOneCoin:
            if (currentCoins < 9999) {
                GameDataFunction::addCoin(writer, 1);
            }
            break;
        // TeleportToStart is commented out of the FoolPunishment enum: warping to
        // the run's start kingdom is the crash still under investigation, and it
        // used the same ChangeStageInfo("start", …) sequence the debug Fast Travel
        // menu does - which is what ruled the transition mechanism out. Restore
        // both together once the arrival is safe.
        case Randomizer::FoolPunishment::Jackpot:
            if (currentCoins < 9999) {
                GameDataFunction::addCoin(writer, 9999 - currentCoins);
            }
            break;
        case Randomizer::FoolPunishment::Kill:
            // Safety net for a punishment rolled before the run mode was known.
            // rollFoolPunishment keeps this out of Nuzlocke draws entirely, so
            // reaching here means the roll and the mode disagreed.
            if (!Randomizer::isNuzlockeRunMode())
                GameDataFunction::killPlayer(writer);
            break;
        case Randomizer::FoolPunishment::None:
        default:
            break;
        }
    }

    // Everything one moon pickup hands out is announced as a single numbered
    // run: two captures plus a Subarea Key read "(1 / 3)" through "(3 / 3)"
    // rather than as separate popups that each look like the only reward. A
    // moon granting exactly one thing shows no counter at all, which is what
    // makes the old stale "1 / 3" on a lone key impossible to reproduce.
    //
    // Scoped rather than paired by hand because the group has to span both
    // applyUnlockFeedback and awardMoonCurrencyReward (Hint Tokens live in the
    // latter), and applyUnlockFeedback returns early for a re-collect.
    struct MoonRewardGroupScope {
        MoonRewardGroupScope() { AbilityOverlay::beginRewardGroup(); }
        ~MoonRewardGroupScope() { AbilityOverlay::endRewardGroup(); }
    };

    // Capture/ability/cloud unlock feedback for a first-time moon. A new location whose reward
    // is already owned pays the duplicate bonus and shows "(again)". Call before the collection
    // is committed so ownership reflects the prior state.
    static void applyUnlockFeedback(GameDataHolderWriter writer, GameDataFile* file, const ShineDB::ShineEntry& entry, bool alreadyCollected) {
        // A re-collected location has already paid out its item. Do not run capture or
        // ability assignment again, and do not present an unlock/duplicate overlay for it.
        if (alreadyCollected) {
            MpDebugLog::write("applyUnlockFeedback: item=%s uid=%d SKIPPED (alreadyCollected=true)", entry.name ? entry.name : "<none>", entry.uid);
            return;
        }

        // CaptureOverlay and AbilityOverlay append to the same notification queue. Start a
        // fresh queue once for this moon before either contributes its batch, otherwise
        // delayed entries from an earlier moon can appear alongside this reward.
        if (sMoonRewardSource == MoonRewardSource::AutoGrant)
            AbilityOverlay::beginQueueAppend(70);
        else
            AbilityOverlay::beginQueue(70);

        const s32 captureCount = CaptureRando::getCaptureCountForMoon(entry);
        // Computed pre-commit: a Multi (triple) moon can mix new and duplicate capture slots
        // in one collection, so this is a per-slot mask, not a single moon-wide flag.
        const u32 captureDuplicateMask = CaptureRando::getDuplicateCaptureSlotMask(writer, entry);
        const bool captureGrantsNew = CaptureRando::grantsNewCapture(writer, entry);
        const bool captureDuplicate = captureCount > 0 && !captureGrantsNew;
        CaptureRando::unlockForMoon(writer, entry);
        CaptureOverlay::queueForMoon(entry, captureDuplicateMask);
        if (captureDuplicate)
            GameDataFunction::addCoin(writer, Randomizer::UnlockRewards::kCaptureDuplicateCoins);

        const AbilityRando::UnlockResult abilityResult = AbilityRando::unlockForMoon(writer, entry, false);
        if (abilityResult == AbilityRando::UnlockResult::Unlocked || abilityResult == AbilityRando::UnlockResult::AlreadyOwned) {
            AbilityOverlay::queueForMoon();
            if (abilityResult == AbilityRando::UnlockResult::AlreadyOwned && !AbilityRando::isFoolMoon(entry))
                GameDataFunction::addCoin(writer, Randomizer::UnlockRewards::kAbilityDuplicateCoins);
        }

        MpDebugLog::write("applyUnlockFeedback: item=%s uid=%d captureCount=%d captureDupMask=0x%x "
                          "captureGrantsNew=%d abilityResult=%d",
            entry.name ? entry.name : "<none>", entry.uid, captureCount, captureDuplicateMask, captureGrantsNew ? 1 : 0, (int)abilityResult);

        if (CaptureRando::isFoolMoon(entry) || AbilityRando::isFoolMoon(entry)) {
            applyFoolPunishment(writer, entry);
            AbilityOverlay::queueFool(getFoolPunishment(entry));
        }

        queueSubareaKeyUnlock(entry);
        queueCloudPaintingUnlocks(writer, entry, file, false);
    }

    void setGotShineHook(GameDataHolderWriter writer, const ShineInfo* shineInfo) {
        MoonGetProfileSetGotShineScope profileScope;
        const bool forced = isForcedCompletedKingdomShine(nullptr, shineInfo);
        if (!sBypassCreditRemap && MoonRando::isInitialized() && shineInfo && !shineInfo->isEmpty()) {
            const ShineDB::ShineEntry* dest = MoonRando::getCreditMoon(shineInfo->mObjId.cstr(), shineInfo->mStageName.cstr());
            if (dest && dest->objId >= 0) {
                sLatestGotPhysicalEntry = findEntryFromShineInfo(shineInfo);
                sLatestGotDestinationEntry = dest;
                beginMoonGetDemoContext(sLatestGotPhysicalEntry, dest, MoonDemoRoute::Pending);

                ShineInfo destInfo;
                initShineInfoFromEntry(&destInfo, dest);
                // The remapped entry is a new ShineInfo, so carry over the
                // native shop marker or GameDataFile will save the reward
                // without incrementing its shop-moon counter.
                destInfo.mIsShop = shineInfo->isShop();
                if (cLogMoonGetDetails) {
                    Randomizer::logPush("setGotShine location=%s item=%s home=%d assigned=%d",
                        sLatestGotPhysicalEntry && sLatestGotPhysicalEntry->name ? sLatestGotPhysicalEntry->name : "?",
                        dest->name ? dest->name : "?", dest->worldId, MoonRando::getAssignedKingdomForEntry(dest));
                }
                GameDataFile* file = writer->getGameDataFile();
                const bool alreadyCollected = isGotShineRaw(file, &destInfo);
                {
                    MoonGetProfileSpan span(&sMoonGetProfileBingoUs);
                    Bingo::recordMoon(sLatestGotPhysicalEntry, dest, alreadyCollected);
                }
                // Timer Mode buys its time from the item that was actually
                // credited, not the location it was found at, so a remapped
                // triple pays the triple bonus wherever it turned up.
                TimerMode::awardMoon(dest, alreadyCollected);
                {
                    MoonRewardGroupScope rewardGroup;
                    {
                        MoonGetProfileSpan span(&sMoonGetProfileUnlockUs);
                        applyUnlockFeedback(writer, file, *dest, alreadyCollected);
                    }
                    awardMoonCurrencyReward(writer, file, *dest, alreadyCollected);
                }

                sLatestGotDisplayEntry = dest;
                sLatestGotUsePhysicalWorld = false;
                sPendingDestInfo = &destInfo;
                // Peers get the remapped item we actually credit, not the
                // location it was found at, or their save state diverges
                // from ours. Before orig, so the not-yet-collected check
                // inside still sees the pre-commit state.
                //
                // mpOnSetGotShine builds destInfo's own HintInfo lookup
                // from destInfo's stageName/objId (which are dest's own,
                // via initShineInfoFromEntry), so it already sends
                // dest->uid to peers when the moon isn't yet collected --
                // a separate explicit send here would double-send the
                // same moon.
                mpOnSetGotShine(writer, &destInfo);
                {
                    // orig runs the native commit plus setGotShineFileHook:
                    // invalidateRedistributedShineCounts and
                    // StoryProgressHooks::healAllMainScenarios.
                    MoonGetProfileSpan span(&sMoonGetProfileOrigUs);
                    // setGotShineHook.orig(writer, &destInfo);
                }
                sPendingDestInfo = nullptr;
                if (forced)
                    finishForcedCompletedKingdomShine(findForcedCompletedKingdomShine(shineInfo), shineInfo);
                return;
            }
        }
        if (const ShineDB::ShineEntry* physical = findEntryFromShineInfo(shineInfo)) {
            const ShineDB::ShineEntry* bingoPhysical = physical;
            const ShineDB::ShineEntry* bingoItem = physical;
            if (sBypassCreditRemap && sMoonGetDemoContext.route == MoonDemoRoute::Direct) {
                if (sMoonGetDemoContext.physical)
                    bingoPhysical = sMoonGetDemoContext.physical;
                if (sMoonGetDemoContext.destination)
                    bingoItem = sMoonGetDemoContext.destination;
            }
            sLatestGotDisplayEntry = physical;
            sLatestGotPhysicalEntry = physical;
            sLatestGotDestinationEntry = physical;
            if (!sBypassCreditRemap)
                beginMoonGetDemoContext(physical, physical, MoonDemoRoute::Pending);
            GameDataFile* file = writer->getGameDataFile();
            const bool alreadyCollected = isGotShineRaw(file, shineInfo);
            {
                MoonGetProfileSpan span(&sMoonGetProfileBingoUs);
                Bingo::recordMoon(bingoPhysical, bingoItem, alreadyCollected);
            }
            TimerMode::awardMoon(bingoItem, alreadyCollected);
            sLatestGotUsePhysicalWorld = false;
            {
                MoonRewardGroupScope rewardGroup;
                {
                    MoonGetProfileSpan span(&sMoonGetProfileUnlockUs);
                    applyUnlockFeedback(writer, file, *physical, alreadyCollected);
                }
                awardMoonCurrencyReward(writer, file, *physical, alreadyCollected);
            }
        } else {
            sLatestGotDisplayEntry = nullptr;
            sLatestGotPhysicalEntry = nullptr;
            sLatestGotDestinationEntry = nullptr;
            sLatestGotUsePhysicalWorld = false;
            if (!sBypassCreditRemap)
                clearMoonGetDemoContext();
        }
        if (cLogMoonGetDetails) {
            Randomizer::logPush("setGotShine -> %s (%s)", shineInfo->mObjId.cstr(), shineInfo->mStageName.cstr());
        }

        // Passthrough path: the shine picked up is the shine credited.
        // mpOnSetGotShine builds its own HintInfo lookup from shineInfo's
        // stageName/objId and already sends the credited uid to peers when
        // the moon isn't yet collected -- a separate explicit send here
        // would double-send the same moon (this previously happened, and
        // peers would receive two ShineCollect packets for one pickup).
        mpOnSetGotShine(writer, shineInfo);
        // setGotShineHook.orig(writer, shineInfo);
        if (forced)
            finishForcedCompletedKingdomShine(findForcedCompletedKingdomShine(shineInfo), shineInfo);
    }

    // The no-argument native getShineNum is not a reliable collection revision
    // for randomized rewards: GameDataFile commits the reward's save slot while
    // callers can still observe the physical location's native total.  Advance a
    // small revision only after the actual save write, so logical kingdom totals
    // are rebuilt exactly when an item is acquired.
    static s32 sRedistributedCounts[32]; // Accommodate max possible LD::sKingdomCount
    static const GameDataFile* sRedistributedCountFile = nullptr;
    static u32 sRedistributedCountRoleLayoutHash = 0;
    static u32 sRedistributedCountRevision = 1;
    static u32 sCachedRedistributedCountRevision = 0;

    static void invalidateRedistributedShineCounts() {
        sRedistributedCountRevision++;
        if (sRedistributedCountRevision == 0)
            sRedistributedCountRevision = 1;
        sRedistributedCountRoleLayoutHash = 0;
        // Cloud-moon painting unlocks are derived from the same save state and
        // are cached for the same reason (their readers run per frame). This is
        // the one place a shine actually reaches the file, so it is where both
        // caches go stale.
        PaintingRando::invalidateUnlockCache();
        // The collection list's Unlocked Paintings page caches its rows off the
        // same unlocks and is only rebuilt on a dirty flag, so dropping the
        // unlock cache without this left an open page rendering stale rows and
        // a stale count after a peer's Cloud moon landed mid-visit.
        HintSystem::invalidatePaintingListRows();
    }

    // Collecting a moon is the only *gameplay* way the redistributed totals go
    // stale, which is why the invalidation above lives in setGotShineFileHook.
    // A Soft Nuzlocke reroll is the other way and it is not gameplay: it clears
    // every acquisition flag, draws a new carried set, and replaces the kingdom
    // assignment table wholesale, all without a single shine reaching the file.
    // Nothing about the cache key changes across that - same GameDataFile, same
    // revision - so without this the tracker and the HUD keep serving the dead
    // run's per-kingdom totals until the player happens to pick up a moon.
    void invalidateShineCountCache() {
        invalidateRedistributedShineCounts();
    }

    static HkTrampoline<void, GameDataFile*, const ShineInfo*> setGotShineFileHook = hk::hook::trampoline([](GameDataFile* file, const ShineInfo* shineInfo) -> void {
        auto suppressEarlyGrandMoonReturnHome = [&]() {
            if (!sGrandMoonDemoPending || !file)
                return;

            // GameDataFile::setGotShine sets mIsPlayDemoReturnToHome for a grand
            // shine. Suppress that early request after the moon is safely
            // committed. The pending guard remains active until
            // StageSceneStateGetShineGrand::exeDemoEnd requests it again.
            *reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(file) + 0x9fc) = false;
            if (cLogMoonGetDetails) {
                Randomizer::logPush("Grand moon committed; deferred ReturnToHome until demo end");
            }
        };

        const ShineInfo* pending = sPendingDestInfo;
        sPendingDestInfo = nullptr;
        if (pending && MoonRando::isInitialized()) {
            sCheckingDestSlot = true;
            setGotShineFileHook.orig(file, pending);
            sCheckingDestSlot = false;
            invalidateRedistributedShineCounts();
            suppressEarlyGrandMoonReturnHome();
            StoryProgressHooks::healAllMainScenarios(file);
            return;
        }
        setGotShineFileHook.orig(file, shineInfo);
        invalidateRedistributedShineCounts();
        suppressEarlyGrandMoonReturnHome();
        StoryProgressHooks::healAllMainScenarios(file);
    });

    // GameDataFunction::makeTextureSaveDataFileName builds the save-slot
    // thumbnail names, and for a few kingdoms it decides between texture
    // variants by asking isGotShine - but it asks *mPlayingFile*, not the slot
    // it is currently naming. StageSceneStateOption's constructor runs that on
    // the scene-init thread from TitleMenuScene::init, and on the "Return to
    // Main Menu" path mPlayingFile is already torn down (observed as 0x28), so
    // the vanilla call dereferences it and dies. Forwarding a file the holder
    // does not own can only crash, so answer "not collected" instead - the
    // thumbnail falls back to the plain kingdom texture.
    static HkTrampoline<bool, GameDataFile*, const ShineInfo*> isGotShineHook = hk::hook::trampoline([](GameDataFile* dataFile, const ShineInfo* shineInfo) -> bool {
        if (isDeadGameDataFile(dataFile))
            return false;

        if (HintSystem::isDirectShineListViewActive())
            return isGotShineHook.orig(dataFile, shineInfo);

        if (MoonRando::isInitialized() && shineInfo && !shineInfo->isEmpty() && !sCheckingDestSlot) {
            const ShineDB::ShineEntry* dest = MoonRando::getCreditMoon(shineInfo->mObjId.cstr(), shineInfo->mStageName.cstr());
            if (dest && dest->objId >= 0) {
                ShineInfo destInfo;
                initShineInfoFromEntry(&destInfo, dest);
                return isGotShineHook.orig(dataFile, &destInfo);
            }
        }
        return isGotShineHook.orig(dataFile, shineInfo);
    });

    static void rebuildRedistributedShineCounts(const GameDataFile* file) {
        const u32 roleLayoutHash = MoonRando::getMoonRoleLayoutHash();
        if (sRedistributedCountFile == file && sCachedRedistributedCountRevision == sRedistributedCountRevision && sRedistributedCountRoleLayoutHash == roleLayoutHash) {
            return;
        }
        sRedistributedCountFile = file;
        sCachedRedistributedCountRevision = sRedistributedCountRevision;
        sRedistributedCountRoleLayoutHash = roleLayoutHash;

        for (s32 i = 0; i < 32; i++)
            sRedistributedCounts[i] = 0;

        bool countedItems[ShineDB::sShineTableSize] = {};
        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo& hint = file->getHintList()[i];
            if (hint.stageName.isEmpty())
                continue;

            // HintInfo identifies the physical location.  Moon randomization
            // saves acquisition on the reward item instead, and triple
            // redistribution assigns that item's value to its credited
            // kingdom.  Reading all three values from the physical entry is
            // what made tracker/progression totals disagree with the moon list.
            const ShineDB::ShineEntry* physical = ShineDB::findEntry(hint.uniqueId, hint.stageName.cstr());
            const ShineDB::ShineEntry* item = !hint.objId.isEmpty() ? MoonRando::getCreditMoon(hint.objId.cstr(), hint.stageName.cstr()) : nullptr;
            if (!item && physical)
                item = MoonRando::getItemForLocation(physical->uid, physical->stage);
            if (!item)
                item = physical;
            const s32 itemIndex = ShineDB::findIndex(item);
            if (!item || itemIndex < 0 || countedItems[itemIndex])
                continue;

            if (!isMoonAcquiredDirect(const_cast<GameDataFile*>(file), item))
                continue;

            countedItems[itemIndex] = true;
            const s32 kingdom = MoonRando::getAssignedKingdomForEntry(item);
            if (kingdom >= 0 && kingdom < 32)
                sRedistributedCounts[kingdom] += MoonRando::isRewardMultiEntry(item) ? 3 : 1;
        }
    }

    // A Soft Nuzlocke reroll keeps a per-kingdom moon count but hands back no
    // moons, so a kingdom's total is what this seed has actually found plus what
    // the last run carried in.  Adding it in these three overloads rather than at
    // each reader is the whole point: the HUD counter, the tracker, how many
    // moons the globe will let Mario deposit - and therefore the unlock
    // requirement, which is levied on the deposit - all resolve through them, and
    // none of them has to know the carry exists.
    static HkTrampoline<s32, const GameDataFile*, s32> getShineNumHook = hk::hook::trampoline([](const GameDataFile* file, s32 worldId) -> s32 {
        // A torn-down file is left entirely to vanilla: neither the hint-list walk
        // below nor the carried-moon record is safe to read off one.
        if (!file || worldId < 0 || worldId >= 32 || isDeadGameDataFile(file))
            return getShineNumHook.orig(file, worldId);

        const s32 carried = Nuzlocke::getCarriedMoons(file, worldId);
        if (!MoonRando::isKingdomRedistributionActive())
            return getShineNumHook.orig(file, worldId) + carried;

        rebuildRedistributedShineCounts(file);
        return sRedistributedCounts[worldId] + carried;
    });

    // The argument-less overload is the same per-kingdom total for the kingdom the
    // player is standing in, and it is what the HUD's Power Moon counter reads
    // (ShineCounter -> GameDataFunction::getCurrentShineNum -> this, before game
    // clear).  Vanilla resolves it by indexing mShineNum directly, so the s32
    // overload's hook above never sees it and the counter kept showing the raw
    // save total: a reward's value comes from its own vanilla GrandShine flag and
    // is credited to its own home kingdom, neither of which follows "Randomize
    // Triple Moons". That is why the counter disagreed with the tracker, which
    // has always read the redistributed table. Route both overloads through it.
    static HkTrampoline<s32, const GameDataFile*> getCurrentWorldShineNumHook = hk::hook::trampoline([](const GameDataFile* file) -> s32 {
        if (!file || isDeadGameDataFile(file))
            return getCurrentWorldShineNumHook.orig(file);

        // Vanilla clamps a negative (develop) world id to 0 before indexing.
        const s32 currentWorldId = file->getCurrentWorldId();
        const s32 worldId = currentWorldId > 0 ? currentWorldId : 0;
        if (worldId >= 32)
            return getCurrentWorldShineNumHook.orig(file);

        const s32 carried = Nuzlocke::getCarriedMoons(file, worldId);
        if (!MoonRando::isKingdomRedistributionActive())
            return getCurrentWorldShineNumHook.orig(file) + carried;

        rebuildRedistributedShineCounts(file);
        return sRedistributedCounts[worldId] + carried;
    });

    // Same reason as the overload above: vanilla adds up the twenty mShineNum
    // slots inline, so it never reaches either per-kingdom hook. This is the
    // grand total shown on the file-select slot, and leaving it on the raw save
    // made it disagree with the sum of the kingdom totals everything else shows.
    static HkTrampoline<s32, const GameDataFile*> getTotalShineNumHook = hk::hook::trampoline([](const GameDataFile* file) -> s32 {
        if (!file || isDeadGameDataFile(file))
            return getTotalShineNumHook.orig(file);

        s32 carried = 0;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
            carried += Nuzlocke::getCarriedMoons(file, i);
        if (!MoonRando::isKingdomRedistributionActive())
            return getTotalShineNumHook.orig(file) + carried;

        rebuildRedistributedShineCounts(file);
        s32 total = carried;
        for (s32 i = 0; i < sNumWorlds; i++)
            total += sRedistributedCounts[i];
        return total;
    });

    // Same reasoning as isGotShineHook: this overload takes the save as `this`
    // too, and it is reachable from the same enumeration paths.
    static HkTrampoline<bool, GameDataFile*, s32> isGotShineIndexHook = hk::hook::trampoline([](GameDataFile* dataFile, s32 index) -> bool {
        if (isDeadGameDataFile(dataFile))
            return false;

        if (HintSystem::isDirectShineListViewActive())
            return isGotShineIndexHook.orig(dataFile, index);

        if (dataFile && index >= 0 && index < 0x400 && !sCheckingDestSlot) {
            const GameDataFile::HintInfo* info = &dataFile->getHintList()[index];
            if (info && !info->stageName.isEmpty() && !info->objId.isEmpty()) {
                // Linked-shine event nodes can retain a home-stage spelling in
                // their HintInfo. Recover the canonical physical entry before
                // asking Moon Rando for the reward assigned to that location.
                const ShineDB::ShineEntry* source = ShineDB::findEntry(info->uniqueId, info->stageName.cstr());
                if (!source) {
                    for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
                        if (ShineDB::sShineTable[i].uid == info->uniqueId) {
                            source = &ShineDB::sShineTable[i];
                            break;
                        }
                    }
                }
                const char* sourceObjId = info->objId.cstr();
                const char* sourceStage = info->stageName.cstr();
                char canonicalObjId[32];
                if (source) {
                    nn::util::SNPrintf(canonicalObjId, sizeof(canonicalObjId), "obj%d", source->objId);
                    sourceObjId = canonicalObjId;
                    sourceStage = source->stage;
                }
                const ShineDB::ShineEntry* destination = MoonRando::isInitialized() ? MoonRando::getCreditMoon(sourceObjId, sourceStage) : nullptr;
                if (destination && destination->objId >= 0) {
                    ShineInfo destInfo;
                    if (initShineInfoFromEntry(&destInfo, destination))
                        return isGotShineHook.orig(dataFile, &destInfo);
                }
            }
        }
        return isGotShineIndexHook.orig(dataFile, index);
    });

    static GameDataHolder* tryGetGameDataHolder() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return nullptr;
        // mSequence is only a GameDataHolder owner when it really is the
        // HakoniwaSequence; the accessor lives at a fixed offset, so a blind
        // cast on any other sequence reads whatever happens to sit there.
        if (!al::isEqualString(system->mSequence->mName.cstr(), "HakoniwaSequence"))
            return nullptr;
        return ((HakoniwaSequence*)system->mSequence)->mGameDataHolderAccessor.mData;
    }

    // GameDataHolder's constructor allocates exactly five GameDataFile objects
    // (`operator new[](0x28)` for mFiles, then five slots filled in order), and
    // every save the game hands out is one of them.
    static constexpr s32 cGameDataFileCount = 5;

    // True means `file` provably cannot be dereferenced as a save. Anything
    // short of proof answers false, so callers keep their old behaviour
    // wherever the holder is out of reach.
    static bool isDeadGameDataFile(const GameDataFile* file) {
        // The observed bad value was 0x28 - offsetof(GameDataHolder, mNextFile)
        // off a null holder. Nothing in the first page is ever mapped, so this
        // catches the whole null-plus-field-offset family without needing the
        // holder to be reachable.
        if (reinterpret_cast<uintptr_t>(file) < 0x1000)
            return true;

        GameDataHolder* holder = tryGetGameDataHolder();
        if (!holder)
            return false;
        for (s32 i = 0; i < cGameDataFileCount; i++) {
            if (holder->getGameDataFile(i) == file)
                return false;
        }
        return true;
    }

    static GameDataFile* tryGetCurrentGameDataFile() {
        GameDataHolder* holder = tryGetGameDataHolder();
        if (!holder)
            return nullptr;

        // mPlayingFile is not valid for the whole lifetime of the holder: on the
        // "Return to Main Menu" path it holds a torn-down value (0x28 was seen
        // in the wild) while the title scene rebuilds. Only hand back a pointer
        // the holder still owns.
        GameDataFile* file = holder->getGameDataFile();
        return isDeadGameDataFile(file) ? nullptr : file;
    }

    static bool isMessageForHint(const GameDataFile::HintInfo* hint, const char* fileName, const char* key) {
        if (!hint || !fileName || !key || hint->stageName.isEmpty())
            return false;
        if (std::strcmp(hint->stageName.cstr(), fileName) != 0)
            return false;

        if (hint->scenarioName && std::strcmp(hint->scenarioName, key) == 0)
            return true;

        if (!hint->objId.isEmpty()) {
            al::StringTmp<0x40> label;
            label.format("ScenarioName_%s", hint->objId.cstr());
            return std::strcmp(label.cstr(), key) == 0;
        }

        return false;
    }

    static bool isScenarioMessageForEntry(const ShineDB::ShineEntry* entry, const char* fileName, const char* key) {
        if (!entry || !fileName || !key || !entry->stage || entry->objId < 0)
            return false;
        if (std::strcmp(entry->stage, fileName) != 0)
            return false;

        al::StringTmp<0x40> label("ScenarioName_obj%d", entry->objId);
        return std::strcmp(label.cstr(), key) == 0;
    }

    static const char16_t* getMoonRockKeyKingdomShortName(s32 kingdom) {
        switch (kingdom) {
        case WorldMeta::WorldId_Cap:
            return u"Cap";
        case WorldMeta::WorldId_Cascade:
            return u"Cascade";
        case WorldMeta::WorldId_Sand:
            return u"Sand";
        case WorldMeta::WorldId_Wooded:
            return u"Wooded";
        case WorldMeta::WorldId_Lake:
            return u"Lake";
        case WorldMeta::WorldId_Cloud:
            return u"Cloud";
        case WorldMeta::WorldId_Lost:
            return u"Lost";
        case WorldMeta::WorldId_Metro:
            return u"Metro";
        case WorldMeta::WorldId_Seaside:
            return u"Seaside";
        case WorldMeta::WorldId_Snow:
            return u"Snow";
        case WorldMeta::WorldId_Luncheon:
            return u"Luncheon";
        case WorldMeta::WorldId_Ruined:
            return u"Ruined";
        case WorldMeta::WorldId_Bowsers:
            return u"Bowser's";
        case WorldMeta::WorldId_Moon:
            return u"Moon";
        default:
            return nullptr;
        }
    }

    static const char16_t* makeMoonRockKeyNameMessage(s32 kingdom) {
        const char16_t* shortName = getMoonRockKeyKingdomShortName(kingdom);
        if (!shortName)
            return nullptr;

        static sead::WFixedSafeString<0x100> sName;
        sName = u"";
        sName.appendWithFormat(u"%s Kingdom's Moon Rock Key", shortName);
        return sName.cstr();
    }

    static const char16_t* makeMoonRockKeyNameMessage(const ShineDB::ShineEntry* entry) {
        if (!entry)
            return nullptr;
        return makeMoonRockKeyNameMessage(MoonRando::getMoonRockKeyKingdomForItem(entry->uid, entry->stage));
    }

    // Same treatment as a Moon Rock Key: the carrier moon is displayed under the
    // name of what it actually grants, everywhere a shine name is shown.
    static const char16_t* makeSubareaKeyNameMessage(const ShineDB::ShineEntry* entry) {
        if (!entry || !Randomizer::isSubareaUnlockEnabled())
            return nullptr;
        const s32 slot = MoonRando::getSubareaKeySlotForItem(entry->uid, entry->stage);
        if (slot < 0)
            return nullptr;

        s32 kingdom = -1;
        SubareaKeyDB::SubareaType type = EntranceDB2::SubareaType_None;
        if (!SubareaKeyDB::keySlotAt(slot, &kingdom, &type))
            return nullptr;

        static sead::WFixedSafeString<0x100> sName;
        sName = u"";
        if (kingdom >= 0)
            sName.appendWithFormat(u"%s ", WorldMeta::getDisplayName16ForWorldId(kingdom));
        else
            sName.append(u"All-Kingdom ");
        if (type != EntranceDB2::SubareaType_Count)
            sName.appendWithFormat(u"%s Key", SubareaKeyDB::typeName(type));
        else
            sName.append(u"Subarea Key");
        return sName.cstr();
    }

    static const char16_t* makeEntryNameMessage(const ShineDB::ShineEntry* entry) {
        if (const char16_t* keyName = makeMoonRockKeyNameMessage(entry))
            return keyName;
        if (const char16_t* keyName = makeSubareaKeyNameMessage(entry))
            return keyName;

        if (!entry || !entry->name)
            return nullptr;

        static sead::WFixedSafeString<0x100> sName;
        sName.convertFromMultiByteString(entry->name, std::strlen(entry->name));
        return sName.cstr();
    }

    static const char16_t* makeEntryHintStyleMessage(const ShineDB::ShineEntry* entry) {
        if (!entry || !entry->name)
            return nullptr;

        static sead::WFixedSafeString<0x200> sMessage;
        sead::WFixedSafeString<0x100> name;
        if (const char16_t* keyName = makeMoonRockKeyNameMessage(entry))
            name = keyName;
        else if (const char16_t* keyName = makeSubareaKeyNameMessage(entry))
            name = keyName;
        else
            name.convertFromMultiByteString(entry->name, std::strlen(entry->name));

        sMessage = u"";
        sMessage.appendWithFormat(u"%s in %s", name.cstr(), ShineDB::getWorldNameFromId(entry->worldId));
        return sMessage.cstr();
    }

    static const char16_t* tryMakeLatestGotDisplayMessage(const char* fileName, const char* key) {
        if (!sLatestGotPhysicalEntry || !sLatestGotDestinationEntry)
            return nullptr;
        if (!isScenarioMessageForEntry(sLatestGotDestinationEntry, fileName, key) && !isScenarioMessageForEntry(sLatestGotPhysicalEntry, fileName, key))
            return nullptr;

        if (const char16_t* keyName = makeMoonRockKeyNameMessage(sLatestGotDestinationEntry))
            return keyName;
        if (const char16_t* keyName = makeSubareaKeyNameMessage(sLatestGotDestinationEntry))
            return keyName;

        return sLatestGotUsePhysicalWorld ? makeEntryHintStyleMessage(sLatestGotPhysicalEntry) : makeEntryNameMessage(sLatestGotPhysicalEntry);
    }

    static bool isSameHintWorld(s32 hintWorldId, s32 worldId) {
        return hintWorldId == worldId;
    }

    static bool isSameString16(const char16_t* lhs, const char16_t* rhs) {
        if (!lhs || !rhs)
            return false;

        while (*lhs && *rhs) {
            if (*lhs != *rhs)
                return false;
            lhs++;
            rhs++;
        }

        return *lhs == *rhs;
    }

    static bool containsString16(const char16_t* text, const char16_t* needle) {
        if (!text || !needle || !*needle)
            return false;

        for (const char16_t* cur = text; *cur; cur++) {
            const char16_t* haystack = cur;
            const char16_t* search = needle;
            while (*haystack && *search && *haystack == *search) {
                haystack++;
                search++;
            }
            if (!*search)
                return true;
        }

        return false;
    }

    static const ShineDB::ShineEntry* findEntryFromHintInfo(const GameDataFile::HintInfo* hint) {
        if (!hint || hint->stageName.isEmpty())
            return nullptr;
        return ShineDB::findEntry(hint->uniqueId, hint->stageName.cstr());
    }

    // Kingdom a hint record belongs to for roster purposes. Vanilla answers
    // HintInfo::worldId - the moon's home kingdom - but once the moon->kingdom
    // redistribution is active a moon is listed and counted under its ASSIGNED
    // kingdom instead (HintSystem keeps the same mapping for the collection
    // list pages).
    static s32 hintNpcListKingdom(const GameDataFile::HintInfo* hint) {
        if (!hint)
            return -1;
        if (!MoonRando::isKingdomRedistributionActive())
            return hint->worldId;

        const ShineDB::ShineEntry* item = findEntryFromHintInfo(hint);
        const s32 kingdom = item ? MoonRando::getAssignedKingdomForEntry(item) : -1;
        return kingdom >= 0 ? kingdom : hint->worldId;
    }

    // hintNpcListKingdom is a linear moon-table scan per record, and the NPC's
    // three roster functions ask about all 0x400 of them - calcRestHintNum while
    // its dialogue is on screen. Same fix HintSystem's list pages needed: build
    // the mapping once and answer from it. Keyed on file, seed, and role layout,
    // the values the assignment is a function of.
    static s8 sHintNpcKingdomCache[0x400];
    static const GameDataFile* sHintNpcKingdomCacheFile = nullptr;
    static u32 sHintNpcKingdomCacheSeed = 0;
    static u32 sHintNpcKingdomCacheRoleLayoutHash = 0;

    static void ensureHintNpcKingdomCache(const GameDataFile* file) {
        const u32 seed = Randomizer::getSeed();
        const u32 roleLayoutHash = MoonRando::getMoonRoleLayoutHash();
        if (!file || (sHintNpcKingdomCacheFile == file && sHintNpcKingdomCacheSeed == seed && sHintNpcKingdomCacheRoleLayoutHash == roleLayoutHash))
            return;

        for (s32 i = 0; i < 0x400; i++)
            sHintNpcKingdomCache[i] = static_cast<s8>(hintNpcListKingdom(&file->getHintList()[i]));
        sHintNpcKingdomCacheFile = file;
        sHintNpcKingdomCacheSeed = seed;
        sHintNpcKingdomCacheRoleLayoutHash = roleLayoutHash;
    }

    // One candidate test for the hint NPC, mirroring GameDataFile's own filter
    // (shared verbatim by isEnableUnlockHint, calcRestHintNum and
    // unlockHintImpl) with only the kingdom test rerouted.
    static bool isHintNpcCandidate(const GameDataFile* file, const GameDataFile::HintInfo* hint, s32 hintKingdom, s32 worldId, bool allowWorldWarpHole) {
        if (!hint || hint->stageName.isEmpty())
            return false;
        // Warp-pipe moons stay off the hint roster until Peach's Castle has been
        // unlocked and visited.
        if (!allowWorldWarpHole && std::strcmp(hint->objectName.cstr(), "WorldWarpHoleShine") == 0)
            return false;
        if (hintKingdom != worldId || hint->isDisabled)
            return false;

        // The scenario mask says when this record's own placement exists, and it
        // is written in its HOME kingdom's scenario numbering. Vanilla can read
        // the current kingdom's scenario for it because home and current are the
        // same kingdom by then; a redistributed record's home is somewhere else,
        // so ask that kingdom. An empty mask means "every scenario".
        const u32 scenarios = hint->progressBitFlag.getDirect();
        if (scenarios != 0) {
            const s32 homeWorldId = hint->worldId > 0 ? hint->worldId : 0;
            const s32 scenarioNo = file->getScenarioNo(homeWorldId);
            if (scenarioNo < 1 || (scenarios & (1u << (scenarioNo - 1))) == 0)
                return false;
        }

        return !hint->isGet && hint->hintStatus == GameDataFile::HintStatus_Lock && !hint->isMoonRock;
    }

    struct HintNpcScan {
        GameDataFile::HintInfo* first = nullptr;
        s32 count = 0;
    };

    // Vanilla walks the per-kingdom hint table, which is grouped by home kingdom
    // when the save is built and cannot be regrouped after the fact, so the
    // redistributed roster has to be recovered by scanning the flat hint list.
    // The order the NPC hands hints out in therefore follows the save's hint
    // list rather than that table; which moon comes first is the only thing
    // that changes.
    static HintNpcScan scanHintNpcCandidates(GameDataFile* file) {
        HintNpcScan scan;
        if (!file)
            return scan;

        const s32 worldId = file->getCurrentWorldId();
        const GameProgressData* progress = file->getGameProgressData();
        // Vanilla asks GameDataFunction::getWorldIndexPeach(), which is absent
        // from syms/main.sym - and is an 8-byte `return 14`, the same value as
        // WorldId_Mushroom.
        const s32 peachWorldId = WorldMeta::WorldId_Mushroom;
        const bool allowWorldWarpHole = progress && progress->isUnlockWorld(peachWorldId) && progress->isAlreadyGoWorld(peachWorldId);

        ensureHintNpcKingdomCache(file);
        for (s32 i = 0; i < 0x400; i++) {
            GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if (!isHintNpcCandidate(file, hint, sHintNpcKingdomCache[i], worldId, allowWorldWarpHole))
                continue;
            if (!scan.first)
                scan.first = hint;
            scan.count++;
        }
        return scan;
    }

    // The three functions the hint NPC drives: whether it has anything left to
    // say, how many hints remain, and picking the one it names. All three read
    // "moons of the kingdom you are standing in" off HintInfo::worldId, which
    // stops being that kingdom's roster once the redistribution is on - so the
    // NPC named moons the kingdom's collection list does not hold, and never
    // named the ones it does.
    static HkTrampoline<bool, GameDataFile*> isEnableUnlockHintHook = hk::hook::trampoline([](GameDataFile* file) -> bool {
        if (!file || isDeadGameDataFile(file) || !MoonRando::isKingdomRedistributionActive())
            return isEnableUnlockHintHook.orig(file);
        return scanHintNpcCandidates(file).first != nullptr;
    });

    static HkTrampoline<s32, GameDataFile*> calcRestHintNumHook = hk::hook::trampoline([](GameDataFile* file) -> s32 {
        if (!file || isDeadGameDataFile(file) || !MoonRando::isKingdomRedistributionActive())
            return calcRestHintNumHook.orig(file);
        return scanHintNpcCandidates(file).count;
    });

    static HkTrampoline<void, GameDataFile*, s32> unlockHintImplHook = hk::hook::trampoline([](GameDataFile* file, s32 hintStatus) -> void {
        if (!file || isDeadGameDataFile(file) || !MoonRando::isKingdomRedistributionActive()) {
            unlockHintImplHook.orig(file, hintStatus);
            return;
        }

        GameDataFile::HintInfo* hint = scanHintNpcCandidates(file).first;
        if (!hint)
            return;

        // The same three writes vanilla makes, in the same order: the record
        // itself, the save flag, and the "what did he just name" pointer
        // every hint-message reader resolves through.
        hint->hintStatus = hintStatus;
        if (GameDataHolder* holder = file->getGameDataHolder())
            GameDataFunction::setRequireSave(GameDataHolderWriter(holder));
        file->setLatestHint(hint);
    });

    static bool isOriginalMoonMessage(const GameDataFile::HintInfo* hint, const char16_t* message) {
        const ShineDB::ShineEntry* entry = findEntryFromHintInfo(hint);
        if (!entry || !entry->name || !message)
            return false;

        sead::WFixedSafeString<0x100> moonName;
        moonName.convertFromMultiByteString(entry->name, std::strlen(entry->name));
        return isSameString16(moonName.cstr(), message);
    }

    static bool isOriginalMoonMessageInText(const GameDataFile::HintInfo* hint, const char16_t* message) {
        const ShineDB::ShineEntry* entry = findEntryFromHintInfo(hint);
        if (!entry || !entry->name || !message)
            return false;

        sead::WFixedSafeString<0x100> moonName;
        moonName.convertFromMultiByteString(entry->name, std::strlen(entry->name));
        return containsString16(message, moonName.cstr());
    }

    static const GameDataFile::HintInfo* findHintByWorldIndex(GameDataFile* file, s32 worldId, s32 index) {
        if (!file || worldId < 0 || index < 0)
            return nullptr;

        using Fn = const GameDataFile::HintInfo* (*)(const GameDataFile*, s32, s32);
        const GameDataFile::HintInfo* hint = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cGameDataFileFindShineOffset)(file, worldId, index);
        if (!hint || hint->stageName.isEmpty())
            return nullptr;
        return hint;
    }

    static const GameDataFile::HintInfo* findHintByWorldMessage(GameDataFile* file, s32 worldId, const char16_t* message, bool allowLocked = false) {
        if (!file || !message)
            return nullptr;

        const GameDataFile::HintInfo* latest = file->getLatestHint();
        if (latest && isSameHintWorld(latest->worldId, worldId) && !latest->stageName.isEmpty() && isOriginalMoonMessage(latest, message))
            return latest;

        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if ((allowLocked || hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc) && isSameHintWorld(hint->worldId, worldId) && !hint->stageName.isEmpty() && isOriginalMoonMessage(hint, message))
                return hint;
        }

        return nullptr;
    }

    static const GameDataFile::HintInfo* findHintByWorldText(GameDataFile* file, s32 worldId, const char16_t* message, bool allowLocked = false) {
        if (!file || !message)
            return nullptr;

        const GameDataFile::HintInfo* latest = file->getLatestHint();
        if (latest && isSameHintWorld(latest->worldId, worldId) && !latest->stageName.isEmpty() && isOriginalMoonMessageInText(latest, message))
            return latest;

        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if ((allowLocked || hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc) && isSameHintWorld(hint->worldId, worldId) && !hint->stageName.isEmpty() && isOriginalMoonMessageInText(hint, message))
                return hint;
        }

        return nullptr;
    }

    static const GameDataFile::HintInfo* findHintByStageMessage(GameDataFile* file, const char* fileName, const char16_t* message, bool allowLocked = false) {
        if (!file || !fileName || !message)
            return nullptr;

        const GameDataFile::HintInfo* latest = file->getLatestHint();
        if (latest && !latest->stageName.isEmpty() && std::strcmp(latest->stageName.cstr(), fileName) == 0 && isOriginalMoonMessage(latest, message))
            return latest;

        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if ((allowLocked || hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc) && !hint->stageName.isEmpty() && std::strcmp(hint->stageName.cstr(), fileName) == 0 && isOriginalMoonMessage(hint, message))
                return hint;
        }

        return nullptr;
    }

    static bool shouldReplaceHintMessage(GameDataFile* file, const GameDataFile::HintInfo* hint, bool allowLocked) {
        if (!hint)
            return false;
        if (allowLocked)
            return true;
        if (file && file->getLatestHint() == hint)
            return true;
        if (file) {
            const ShineDB::ShineEntry* item = findEntryFromHintInfo(hint);
            if (item && file->customIsGotShine(item->uid, item->stage))
                return true;
        }
        return hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc;
    }

    static const char16_t* makeRandomizedHintMessage(GameDataFile* file, const GameDataFile::HintInfo* hint, bool allowLocked = false) {
        if (!shouldReplaceHintMessage(file, hint, allowLocked))
            return nullptr;
        if (!Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized())
            return nullptr;

        const ShineDB::ShineEntry* item = findEntryFromHintInfo(hint);
        if (!item)
            return nullptr;

        const MoonRando::Placement* placement = MoonRando::getPlacementForItem(item->uid, item->stage);
        if (!placement || !placement->location || !placement->location->name)
            return nullptr;

        static sead::WFixedSafeString<0x200> sHintMessage;
        sead::WFixedSafeString<0x100> locationName;
        if (const char16_t* keyName = makeMoonRockKeyNameMessage(item))
            locationName = keyName;
        else if (const char16_t* keyName = makeSubareaKeyNameMessage(item))
            locationName = keyName;
        else
            locationName.convertFromMultiByteString(placement->location->name, std::strlen(placement->location->name));

        sHintMessage = u"";
        sHintMessage.appendWithFormat(u"%s in %s", locationName.cstr(), ShineDB::getWorldNameFromId(placement->location->worldId));
        return sHintMessage.cstr();
    }

    static const char16_t* tryMakeRandomizedHintMessage(GameDataHolderAccessor accessor, s32 worldId, s32 index, bool allowLocked = false) {
        // A direct collection-list page is reordered by the triple-moon item
        // assignment.  Its world/index must be resolved through HintSystem's
        // list resolver, which also backs hint purchases.  Calling the raw
        // GameDataFile lookup here instead used the vanilla row at that index:
        // it produced a plausible but unrelated location name, then sent the
        // player to a moon with a different reward.
        if (HintSystem::isDirectShineListViewActive())
            return HintSystem::tryMakeHintMessage(accessor, worldId, index, allowLocked);

        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : tryGetCurrentGameDataFile();
        return makeRandomizedHintMessage(file, findHintByWorldIndex(file, worldId, index), allowLocked);
    }

    static const char16_t* tryMakeRandomizedHintMessageFromOriginalText(GameDataHolderAccessor accessor, s32 worldId, const char16_t* message,
        bool allowLocked = false) {
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : tryGetCurrentGameDataFile();
        return makeRandomizedHintMessage(file, findHintByWorldMessage(file, worldId, message, allowLocked), allowLocked);
    }

    static const char16_t* tryMakeLatestRandomizedHintMessage(const char* fileName, const char* key) {
        GameDataFile* file = tryGetCurrentGameDataFile();
        if (!file)
            return nullptr;

        bool allowLocked = false;
        const GameDataFile::HintInfo* hint = file->getLatestHint();
        if (!isMessageForHint(hint, fileName, key)) {
            hint = nullptr;
            for (s32 i = 0; i < 0x400; i++) {
                const GameDataFile::HintInfo* cur = &file->getHintList()[i];
                if (cur->hintStatus == GameDataFile::HintStatus_UnlockByNpc && isMessageForHint(cur, fileName, key)) {
                    hint = cur;
                    break;
                }
            }
        }

        if (!hint) {
            for (s32 i = 0; i < 0x400; i++) {
                const GameDataFile::HintInfo* cur = &file->getHintList()[i];
                if (isMessageForHint(cur, fileName, key)) {
                    hint = cur;
                    allowLocked = true;
                    break;
                }
            }
        }

        return makeRandomizedHintMessage(file, hint, allowLocked);
    }

    static const char16_t* tryMakeLatestRandomizedHintMessageFromOriginalText(const char* fileName, const char16_t* message) {
        GameDataFile* file = tryGetCurrentGameDataFile();
        if (const char16_t* hintMessage = makeRandomizedHintMessage(file, findHintByStageMessage(file, fileName, message)))
            return hintMessage;
        return makeRandomizedHintMessage(file, findHintByStageMessage(file, fileName, message, true), true);
    }

    static const char16_t* tryMakeCurrentWorldRandomizedHintMessageFromText(const char16_t* message) {
        GameDataFile* file = tryGetCurrentGameDataFile();
        if (!file || !message)
            return nullptr;

        const s32 currentWorldId = file->getCurrentWorldId();
        if (currentWorldId >= 0) {
            if (const char16_t* hintMessage = makeRandomizedHintMessage(file, findHintByWorldText(file, currentWorldId, message, true), true))
                return hintMessage;
        }

        const s32 displayWorldId = file->getCurrentWorldIdForDisp();
        if (displayWorldId != currentWorldId) {
            if (const char16_t* hintMessage = makeRandomizedHintMessage(file, findHintByWorldText(file, displayWorldId, message, true), true))
                return hintMessage;
        }

        return nullptr;
    }

    static StageScene* tryGetCurrentStageScene() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return nullptr;

        HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
        if (!seq->mCurrentScene)
            return nullptr;
        if (!al::isEqualString(typeid(*seq->mCurrentScene).name(), typeid(StageScene).name()))
            return nullptr;

        return (StageScene*)seq->mCurrentScene;
    }

    static bool isScenarioStartLayout(const al::IUseLayout* layout) {
        if (!layout)
            return false;

        StageScene* scene = tryGetCurrentStageScene();
        if (!scene)
            return false;
        return layout == static_cast<const al::IUseLayout*>(scene->mScenarioStartLayout);
    }

    static const ShineDB::ShineEntry* findLocationEntryFromHintInfo(const GameDataFile::HintInfo* hint) {
        if (!hint || hint->stageName.isEmpty() || hint->objId.isEmpty())
            return nullptr;

        const s32 objNum = parseShineObjId(hint->objId.cstr());
        if (objNum < 0)
            return nullptr;

        return ShineDB::findEntry(ShineDB::findUid(hint->stageName.cstr(), objNum), hint->stageName.cstr());
    }

    static bool isLocationCollected(GameDataFile* file, const ShineDB::ShineEntry* location);

    static const GameDataFile::HintInfo* findStoryHintByMainScenario(GameDataFile* file, s32 worldId, s32 mainScenarioNo) {
        if (!file || worldId < 0 || mainScenarioNo < 0)
            return nullptr;

        const GameDataFile::HintInfo* firstMatch = nullptr;
        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if (!isSameHintWorld(hint->worldId, worldId) || hint->mainScenarioNo != mainScenarioNo)
                continue;

            const ShineDB::ShineEntry* location = findLocationEntryFromHintInfo(hint);
            if (!location || !(location->flags & ShineDB::ShineFlag_Story))
                continue;

            if (!firstMatch)
                firstMatch = hint;
            if (!isLocationCollected(file, location))
                return hint;
        }

        return firstMatch;
    }

    static const ShineDB::ShineEntry* getPlacedItemForLocation(const ShineDB::ShineEntry* location) {
        if (!location || location->objId < 0 || !location->stage)
            return nullptr;

        al::StringTmp<0x40> objId("obj%d", location->objId);
        return MoonRando::getCreditMoon(objId.cstr(), location->stage);
    }

    static bool isLocationCollected(GameDataFile* file, const ShineDB::ShineEntry* location) {
        if (!file || !location)
            return false;

        ShineInfo info;
        if (!initShineInfoFromEntry(&info, location))
            return false;

        return file->isGotShine(&info);
    }

    static s32 resolveMainScenarioNoForTitle(GameDataFile* file, s32 logicalWorldId) {
        return StoryProgressHooks::resolveMainScenarioNo(file, logicalWorldId);
    }

    static const char16_t* makeRandomizedScenarioTitleMessageForLocation(const ShineDB::ShineEntry* location) {
        if (!location || !location->name || !(location->flags & ShineDB::ShineFlag_Story) || !Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized())
            return nullptr;

        const ShineDB::ShineEntry* placedItem = getPlacedItemForLocation(location);
        if (!placedItem)
            return nullptr;

        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));

        static sead::WFixedSafeString<0x200> sScenarioTitleMessage;
        sScenarioTitleMessage = u"";
        sScenarioTitleMessage.appendWithFormat(u"%s (%s)", locationName.cstr(), ShineDB::getWorldNameFromId(destWorld(placedItem)));
        return sScenarioTitleMessage.cstr();
    }

    static const char16_t* makeRandomizedScenarioTitleMessage(const GameDataFile::HintInfo* hint) {
        return makeRandomizedScenarioTitleMessageForLocation(findLocationEntryFromHintInfo(hint));
    }

    static const ShineDB::ShineEntry* findStoryLocationFromStageMessage(const char* fileName, const char* key) {
        if (!fileName || !key)
            return nullptr;

        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry* entry = ShineDB::entryAt(i);
            if (!entry || entry->objId < 0 || !(entry->flags & ShineDB::ShineFlag_Story) || std::strcmp(entry->stage, fileName) != 0)
                continue;

            al::StringTmp<0x40> label("ScenarioName_obj%d", entry->objId);
            if (std::strcmp(label.cstr(), key) == 0)
                return entry;
        }

        return nullptr;
    }

    static const ShineDB::ShineEntry* findStoryLocationFromMessage(const char* fileName, const char16_t* message) {
        if (!fileName || !message)
            return nullptr;

        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry* entry = ShineDB::entryAt(i);
            if (!entry || !entry->name || !(entry->flags & ShineDB::ShineFlag_Story) || std::strcmp(entry->stage, fileName) != 0)
                continue;

            sead::WFixedSafeString<0x100> moonName;
            moonName.convertFromMultiByteString(entry->name, std::strlen(entry->name));
            if (isSameString16(moonName.cstr(), message))
                return entry;
        }

        return nullptr;
    }

    static const char16_t* tryMakeScenarioTitleFromStageMessage(const char* fileName, const char* key) {
        if (!tryGetCurrentStageScene())
            return nullptr;
        return makeRandomizedScenarioTitleMessageForLocation(findStoryLocationFromStageMessage(fileName, key));
    }

    static const char16_t* tryMakeScenarioTitleFromOriginalMessage(const char* fileName, const char16_t* message) {
        if (!tryGetCurrentStageScene())
            return nullptr;
        return makeRandomizedScenarioTitleMessageForLocation(findStoryLocationFromMessage(fileName, message));
    }

    static const char16_t* tryMakeScenarioTitleForWorld(GameDataFile* file, s32 worldId, s32 mainScenarioNo) {
        if (!file || worldId < 0)
            return nullptr;

        return makeRandomizedScenarioTitleMessage(findStoryHintByMainScenario(file, worldId, mainScenarioNo));
    }

    static const char16_t* tryMakeCurrentWorldRandomizedScenarioTitle() {
        StageScene* scene = tryGetCurrentStageScene();
        GameDataFile* file = tryGetCurrentGameDataFile();
        if (!scene || !file)
            return nullptr;

        s32 worldIds[4] = {
            -1,
            -1,
            -1,
            -1,
        };

        worldIds[0] = file->getCurrentWorldId();
        worldIds[1] = file->getCurrentWorldIdForDisp();

        if (!scene->mStageName.isEmpty()) {
            if (const WorldMeta::Entry* entry = WorldMeta::findByMainStage(scene->mStageName.cstr()))
                worldIds[2] = entry->worldId;
        }

        if (const char* currentStage = getCurrentStageName()) {
            if (const WorldMeta::Entry* entry = WorldMeta::findByMainStage(currentStage))
                worldIds[3] = entry->worldId;
        }

        for (s32 i = 0; i < (s32)(sizeof(worldIds) / sizeof(worldIds[0])); i++) {
            const s32 worldId = worldIds[i];
            if (worldId < 0)
                continue;

            bool alreadyTried = false;
            for (s32 j = 0; j < i; j++)
                if (worldIds[j] == worldId)
                    alreadyTried = true;
            if (alreadyTried)
                continue;

            const s32 mainScenarioNo = resolveMainScenarioNoForTitle(file, worldId);
            if (const char16_t* title = tryMakeScenarioTitleForWorld(file, worldId, mainScenarioNo))
                return title;
        }

        return nullptr;
    }

    static HkTrampoline<void, al::LayoutActor*, const al::Scene*> setScenarioNameHook = hk::hook::trampoline([](al::LayoutActor* layout, const al::Scene* scene) -> void {
        setScenarioNameHook.orig(layout, scene);
        if (!layout)
            return;

        const char16_t* title = tryMakeCurrentWorldRandomizedScenarioTitle();
        if (!title)
            return;

        // rs::setScenarioName owns the objective title shown at the top
        // of Cascade's post-moon sequence. Its vanilla lookup can fall
        // through to a literal slash placeholder after randomized story
        // progress changes the active quest. Replace both the visible
        // text and its shadow after native setup has completed.
        al::setPaneString(layout, "TxtScenario", title, 0);
        if (al::isExistPane(layout, "TxtScenarioSh"))
            al::setPaneString(layout, "TxtScenarioSh", title, 0);
    });

    static bool isStoryMoonTitleMessage(const char16_t* message) {
        if (!message)
            return false;

        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            const ShineDB::ShineEntry* entry = ShineDB::entryAt(i);
            if (!entry || !entry->name || !(entry->flags & ShineDB::ShineFlag_Story))
                continue;

            sead::WFixedSafeString<0x100> moonName;
            moonName.convertFromMultiByteString(entry->name, std::strlen(entry->name));
            if (isSameString16(moonName.cstr(), message))
                return true;
        }

        return false;
    }

    static HkTrampoline<const char16_t*, const al::LayoutActor*, s32, s32> tryFindShineMessageLayoutHook = hk::hook::trampoline([](const al::LayoutActor* layout, s32 worldId, s32 index) -> const char16_t* {
        if (layout) {
            GameDataHolderAccessor accessor(layout);
            if (isScenarioStartLayout(layout)) {
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : tryGetCurrentGameDataFile();
                if (const char16_t* title = makeRandomizedScenarioTitleMessage(findHintByWorldIndex(file, worldId, index)))
                    return title;

                const char16_t* originalMessage = tryFindShineMessageLayoutHook.orig(layout, worldId, index);
                if (isStoryMoonTitleMessage(originalMessage)) {
                    if (const char16_t* title = tryMakeCurrentWorldRandomizedScenarioTitle())
                        return title;
                }
                return originalMessage;
            }

            HintSystem::observeShineListQuery(layout, worldId, index);
            // The informational Dark/Darker Side pages have no native
            // stage-message labels behind their rows, so never fall
            // through to tryFindShineMessage's unchecked lookup. Moon
            // Rock Key rows render their own text (this is what the
            // native page update writes into each row); painting rows
            // keep the bought-hint message fallback.
            if (HintSystem::isCustomShineListInfoPage(worldId)) {
                if (const char16_t* message = HintSystem::tryMakeMoonRockKeyRowText(accessor, worldId, index))
                    return message;
                if (const char16_t* message = HintSystem::tryMakeSubareaKeyRowText(accessor, worldId, index))
                    return message;
                if (const char16_t* message = HintSystem::tryMakeHintMessage(accessor, worldId, index))
                    return message;
                return u"";
            }
            if (const char16_t* message = tryMakeRandomizedHintMessage(accessor, worldId, index))
                return message;
            if (const char16_t* message = HintSystem::tryMakeActionPrompt(layout, accessor, worldId, index))
                return message;
            const char16_t* originalMessage = tryFindShineMessageLayoutHook.orig(layout, worldId, index);
            if (Randomizer::isHintSystemEnabled() && HintSystem::isDirectShineListViewActive())
                return originalMessage;
            if (const char16_t* message = tryMakeRandomizedHintMessageFromOriginalText(accessor, worldId, originalMessage))
                return message;
            return originalMessage;
        }
        return tryFindShineMessageLayoutHook.orig(layout, worldId, index);
    });

    static HkTrampoline<const char16_t*, const al::LiveActor*, const al::IUseMessageSystem*, s32, s32> tryFindShineMessageActorHook = hk::hook::trampoline([](const al::LiveActor* actor, const al::IUseMessageSystem* messageSystem, s32 worldId, s32 index) -> const char16_t* {
        if (actor) {
            GameDataHolderAccessor accessor(actor);
            if (const char16_t* message = tryMakeRandomizedHintMessage(accessor, worldId, index, true))
                return message;
            const char16_t* originalMessage = tryFindShineMessageActorHook.orig(actor, messageSystem, worldId, index);
            if (const char16_t* message = tryMakeRandomizedHintMessageFromOriginalText(accessor, worldId, originalMessage, true))
                return message;
            return originalMessage;
        }
        return tryFindShineMessageActorHook.orig(actor, messageSystem, worldId, index);
    });

    static HkTrampoline<const char16_t*, const al::IUseMessageSystem*, const char*, const char*> getStageMessageStringHook = hk::hook::trampoline([](const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) -> const char16_t* {
        if (const char16_t* message = tryMakeLatestGotDisplayMessage(fileName, key))
            return message;
        if (const char16_t* message = tryMakeMoonRockKeyCapMessage(fileName, key))
            return message;
        if (const char16_t* title = tryMakeScenarioTitleFromStageMessage(fileName, key))
            return title;
        const char16_t* originalMessage = getStageMessageStringHook.orig(messageSystem, fileName, key);
        if (const char16_t* title = tryMakeScenarioTitleFromOriginalMessage(fileName, originalMessage))
            return title;
        if (const char16_t* message = SphinxRando::tryMakeQuestion(originalMessage))
            return message;
        if (const char16_t* message = tryMakeLatestRandomizedHintMessage(fileName, key))
            return message;
        if (const char16_t* message = tryMakeLatestRandomizedHintMessageFromOriginalText(fileName, originalMessage))
            return message;
        return originalMessage;
    });

    static HkTrampoline<bool, const char16_t**, const al::IUseMessageSystem*, const char*, const char*> tryGetStageMessageStringHook = hk::hook::trampoline([](const char16_t** out, const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) -> bool {
        if (const char16_t* message = tryMakeLatestGotDisplayMessage(fileName, key)) {
            if (out)
                *out = message;
            return true;
        }
        if (const char16_t* message = tryMakeMoonRockKeyCapMessage(fileName, key)) {
            if (out)
                *out = message;
            return true;
        }
        if (const char16_t* title = tryMakeScenarioTitleFromStageMessage(fileName, key)) {
            if (out)
                *out = title;
            return true;
        }
        const bool found = tryGetStageMessageStringHook.orig(out, messageSystem, fileName, key);
        if (found && out) {
            if (const char16_t* title = tryMakeScenarioTitleFromOriginalMessage(fileName, *out)) {
                *out = title;
                return found;
            }
            if (const char16_t* message = SphinxRando::tryMakeQuestion(*out)) {
                *out = message;
                return found;
            }
            if (const char16_t* message = tryMakeLatestRandomizedHintMessage(fileName, key)) {
                *out = message;
                return found;
            }
            if (const char16_t* message = tryMakeLatestRandomizedHintMessageFromOriginalText(fileName, *out))
                *out = message;
        } else if (const char16_t* message = tryMakeLatestRandomizedHintMessage(fileName, key)) {
            if (out)
                *out = message;
            return true;
        }
        return found;
    });

    static HkTrampoline<bool, const al::IUseMessageSystem*, const char*, const char*> isExistLabelInSystemMessageHook = hk::hook::trampoline([](const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) -> bool {
        if (MoonRequirement::isBlockedMessage(fileName, key))
            return true;
        if (isMoonRockKeyBlockedMessage(fileName, key))
            return true;
        if (isSubareaKeyBlockedMessage(fileName, key))
            return true;
        if (HintSystem::isToadetteMedalHuntMessage(fileName, key))
            return true;
        if (PurpleCoinRewards::tryGetShopMessageOverride(fileName, key))
            return true;
        // Same reason as every label above: an invented CapMessage label
        // that does not "exist" makes CapMessageLayout render the literal
        // "NULL" without ever reaching getSystemMessageString, where
        // ProgressionHooks would have supplied the text.
        if (MovementAbilityLock::tryMakeCapMessage(fileName, key))
            return true;
        // Painting cap messages: report WorldWarp labels as present so the
        // vanilla CapMessageLayout proceeds to getSystemMessageString (which
        // ProgressionHooks overrides with a fallback) instead of hardcoding
        // on-screen "NULL" for a remapped/force-opened painting.
        if (fileName && key && std::strcmp(fileName, "CapMessage") == 0 && std::strncmp(key, "WorldWarp", 9) == 0)
            return true;
        return isExistLabelInSystemMessageHook.orig(messageSystem, fileName, key);
    });

    static HkTrampoline<bool, al::LayoutActor*, const char*, const char*, const char*> trySetPaneStageMessageIfExistHook = hk::hook::trampoline([](al::LayoutActor* layout, const char* paneName, const char* fileName, const char* key) -> bool {
        if (layout && paneName && fileName && key && Randomizer::isSphinxRandoEnabled()) {
            const char16_t* originalMessage = nullptr;
            const bool found = tryGetStageMessageStringHook.orig(&originalMessage, layout, fileName, key);
            if (found) {
                if (const char16_t* sphinxMessage = SphinxRando::tryMakeQuestion(originalMessage)) {
                    al::setPaneString(layout, paneName, sphinxMessage, 0);
                    return true;
                }
            }
        }

        return trySetPaneStageMessageIfExistHook.orig(layout, paneName, fileName, key);
    });

    static HkTrampoline<void, al::IUseLayout*, const char*, const char16_t*, u16> setPaneStringHook = hk::hook::trampoline([](al::IUseLayout* layout, const char* paneName, const char16_t* message, u16 unk) -> void {
        if (const char16_t* captureGuideMessage = HintSystem::tryMakeCaptureGuidePaneString(layout, paneName)) {
            setPaneStringHook.orig(layout, paneName, captureGuideMessage, unk);
            return;
        }

        if (isScenarioStartLayout(layout)) {
            if ((paneName && std::strcmp(paneName, "TxtMessage") == 0) || isStoryMoonTitleMessage(message)) {
                if (const char16_t* title = tryMakeCurrentWorldRandomizedScenarioTitle()) {
                    setPaneStringHook.orig(layout, paneName, title, unk);
                    return;
                }
            }
            setPaneStringHook.orig(layout, paneName, message, unk);
            return;
        }

        if (paneName && std::strcmp(paneName, "TxtMessage") == 0) {
            if (const char16_t* hintMessage = tryMakeCurrentWorldRandomizedHintMessageFromText(message)) {
                setPaneStringHook.orig(layout, paneName, hintMessage, unk);
                return;
            }
        }
        setPaneStringHook.orig(layout, paneName, message, unk);
    });

    static bool tryPatchSphinxChoiceInfo(const al::EventFlowNode* node, EventFlowChoiceInfoLayout* choiceInfo) {
        if (!node || !choiceInfo || !choiceInfo->choiceMessages || !Randomizer::isSphinxRandoEnabled())
            return false;

        const s32 answerCount = choiceInfo->registeredChoiceCount;
        if (answerCount != 4 || choiceInfo->choiceCount < 4)
            return false;

        const char16_t* question = nullptr;
        const char16_t* answers[4] = {};
        if (!SphinxRando::tryMakeChoiceFromAnswers(node, choiceInfo, choiceInfo->question, choiceInfo->choiceMessages, answerCount, &question, answers))
            return false;

        choiceInfo->question = question;
        for (s32 i = 0; i < 4; i++)
            choiceInfo->choiceMessages[i] = answers[i];

        static s32 sLogCount = 0;
        if (sLogCount < 8) {
            Randomizer::logPush("sphinx choice info randomized");
            sLogCount++;
        }
        return true;
    }

    static HkTrampoline<uintptr_t, const al::EventFlowNode*, EventFlowChoiceInfoLayout*> startChoiceEventHook = hk::hook::trampoline([](const al::EventFlowNode* node, EventFlowChoiceInfoLayout* choiceInfo) -> uintptr_t {
        tryPatchSphinxChoiceInfo(node, choiceInfo);
        return startChoiceEventHook.orig(node, choiceInfo);
    });

    static HkTrampoline<uintptr_t, void*, s32> npcEventCtrlInfoEndChoiceHook = hk::hook::trampoline([](void* ctrlInfo, s32 selectedIndex) -> uintptr_t {
        EventFlowChoiceInfoLayout** choiceInfoSlot = nullptr;
        EventFlowChoiceInfoLayout* choiceInfo = nullptr;
        if (ctrlInfo) {
            choiceInfoSlot = reinterpret_cast<EventFlowChoiceInfoLayout**>(reinterpret_cast<uintptr_t>(ctrlInfo) + 0xb8);
            choiceInfo = *choiceInfoSlot;
        }

        const s32 remappedIndex = SphinxRando::tryRemapSelectedChoiceInfo(choiceInfo, selectedIndex);
        if (remappedIndex != selectedIndex) {
            static s32 sLogCount = 0;
            if (sLogCount < 8) {
                Randomizer::logPush("sphinx end choice remapped");
                sLogCount++;
            }
        }

        if (choiceInfo)
            choiceInfo->selectedIndex = remappedIndex;
        if (choiceInfoSlot)
            *choiceInfoSlot = nullptr;
        return reinterpret_cast<uintptr_t>(ctrlInfo);
    });

    static const char* getEventFlowEventName(const al::EventFlowEventData* eventData) {
        if (!eventData)
            return nullptr;
        return *reinterpret_cast<const char* const*>(eventData);
    }

    static bool isEventFlowEventName(const al::EventFlowEventData* eventData, const char* name) {
        const char* eventName = getEventFlowEventName(eventData);
        return eventName && name && std::strcmp(eventName, name) == 0;
    }

    static bool isEventFlowEventNamePrefix(const al::EventFlowEventData* eventData, const char* prefix) {
        const char* eventName = getEventFlowEventName(eventData);
        if (!eventName || !prefix)
            return false;
        return std::strncmp(eventName, prefix, std::strlen(prefix)) == 0;
    }

    static bool driveForcedSphinxRecord(void* sphinxQuiz, bool correct) {
        if (!sphinxQuiz)
            return true;

        s32* recordTimer = reinterpret_cast<s32*>(reinterpret_cast<uintptr_t>(sphinxQuiz) + 0x1e0);
        bool* isFailed = reinterpret_cast<bool*>(reinterpret_cast<uintptr_t>(sphinxQuiz) + 0x1e8);
        if (*recordTimer < 0) {
            *recordTimer = 45;
            *isFailed = !correct;
        }

        const bool isDone = *recordTimer < 2;
        *recordTimer = isDone ? -1 : *recordTimer - 1;
        if (isDone) {
            *isFailed = !correct;
            SphinxRando::clearLastSelectedAnswerResult();
        }
        return isDone;
    }

    static HkTrampoline<bool, void*, const al::EventFlowEventData*> sphinxQuizReceiveEventHook = hk::hook::trampoline([](void* sphinxQuiz, const al::EventFlowEventData* eventData) -> bool {
        const s32 answerCorrectState = SphinxRando::getLastSelectedAnswerCorrectState();
        const bool isRecordSuccess = isEventFlowEventName(eventData, "RecordSuccess");
        const bool isRecordFailed = isEventFlowEventName(eventData, "RecordFailed");

        if (isRecordSuccess || isRecordFailed)
            PurpleCoinRewards::recordNpcConversation(reinterpret_cast<al::LiveActor*>(sphinxQuiz));

        if (answerCorrectState == 1 && (isRecordSuccess || isRecordFailed)) {
            const s32 questionId = SphinxRando::getCurrentQuestionId();
            if (questionId >= 0)
                Bingo::recordEvent({ Bingo::GoalType::SphinxQuestions, 1, -1, 0x53000000u | static_cast<u32>(questionId), Bingo::EventOrigin::Local });
        }

        if (answerCorrectState == 1 && isRecordFailed) {
            static s32 sLogCount = 0;
            if (sLogCount < 8) {
                Randomizer::logPush("sphinx forced correct record");
                sLogCount++;
            }
            return driveForcedSphinxRecord(sphinxQuiz, true);
        }

        if (answerCorrectState == 0 && isRecordSuccess) {
            static s32 sLogCount = 0;
            if (sLogCount < 8) {
                Randomizer::logPush("sphinx forced failed record");
                sLogCount++;
            }
            return driveForcedSphinxRecord(sphinxQuiz, false);
        }

        if (answerCorrectState >= 0 && isEventFlowEventName(eventData, "JudgeContinuationCorrect"))
            return answerCorrectState == 1;

        if (answerCorrectState >= 0 && isEventFlowEventNamePrefix(eventData, "JudgeAfterCorrect")) {
            static s32 sLogCount = 0;
            if (sLogCount < 16) {
                Randomizer::logPush("sphinx forced judge correct=%d", answerCorrectState);
                sLogCount++;
            }
            return answerCorrectState == 1;
        }

        const bool result = sphinxQuizReceiveEventHook.orig(sphinxQuiz, eventData);
        if ((isRecordSuccess || isRecordFailed) && result)
            SphinxRando::clearLastSelectedAnswerResult();
        return result;
    });

    static const char16_t* getShopItemMessage(const al::IUseMessageSystem* messageSystem, const char* fileName, const char* key) {
        if (MoonRando::isInitialized() && fileName && al::isEqualString(fileName, "ItemMoon") && key) {
            const ShineDB::ShineEntry* entry = nullptr;
            GameSystem* system = GameSystemFunction::getGameSystem();
            HakoniwaSequence* seq = system ? (HakoniwaSequence*)system->mSequence : nullptr;
            if (seq && seq->mGameDataHolderAccessor) {
                GameDataHolderWriter writer(seq->mGameDataHolderAccessor);
                GameDataFile* file = writer->getGameDataFile();
                const char* curStage = GameDataFunction::getCurrentStageName(seq->mGameDataHolderAccessor);
                if (file && curStage) {
                    const ShopMoonDisplayInfo shopInfo = findShopMoonDisplayInfoForStage(file, curStage);
                    entry = getShopMoonDisplayEntry(shopInfo);
                }
            }

            if (entry) {
                static sead::WFixedSafeString<256> sBuf;
                static constexpr const char* kExplainSuffix = "_Explain";
                const size_t keyLen = std::strlen(key);
                const size_t explainSuffixLen = std::strlen(kExplainSuffix);
                // The model and texture are selected from destWorld(entry),
                // not entry->worldId.  The latter is immutable home metadata
                // and becomes stale when Randomize Triple Moons redistributes
                // an item to another kingdom.
                const s32 displayWorld = destWorld(entry);
                sBuf = u"";
                if (keyLen >= explainSuffixLen && std::strcmp(key + keyLen - explainSuffixLen, kExplainSuffix) == 0) {
                    sBuf.appendWithFormat(u"A Power Moon that was recovered from %s.", ShineDB::getWorldNameFromId(displayWorld));
                } else {
                    sBuf.appendWithFormat(u"%s Power Moon", ShineDB::getWorldNameFromId(displayWorld));
                }
                return sBuf.cstr();
            }
        }
        return al::getSystemMessageString(messageSystem, fileName, key);
    }

    static void clearCaptureUnlockPane(al::LayoutActor* layout) {
        if (!layout || !al::isExistPane(layout, "TxtDay"))
            return;

        al::setPaneString(layout, "TxtDay", u"", 0);
        al::hidePane(layout, "TxtDay");
    }

    static const ShineDB::ShineEntry* findDisplayEntryFromShineInfo(const ShineInfo* info) {
        if (!info || info->isEmpty())
            return nullptr;
        if (isShineInfoForEntry(info, sLatestGotDisplayEntry))
            return sLatestGotDisplayEntry;

        const ShineDB::ShineEntry* physical = findEntryFromShineInfo(info);
        const ShineDB::ShineEntry* mapped = MoonRando::isInitialized() ? MoonRando::getCreditMoon(info->mObjId.cstr(), info->mStageName.cstr()) : nullptr;

        return mapped ? mapped : physical;
    }

    // Plausible names for the "You Got a Power Moon!" header pane and its
    // drop-shadow/glow duplicates. Shared by the probe below and the medal
    // header override, so anything the probe reports as present is something
    // the override already writes.
    static const char* const kShineTextHeaderPanes[] = {
        "TxtGet",
        // TxtGet renders correctly but a second, untouched copy of the
        // original "You Got a Moon" text still shows behind it - these are
        // guesses at that second pane's name (a common drop-shadow/glow
        // duplicate pane convention in this engine; "<pane>Sh" is the
        // convention every decompiled call site uses, e.g. TxtScenarioSh).
        "TxtGetS",
        "TxtGetSh",
        "TxtGetShadow",
        "TxtGetKage",
        "TxtGet00",
        "TxtGet01",
        "TxtGetBack",
        "TxtGetGlow",
        "TxtGetOutline",
    };

    // Diagnostic for the Toadette Medal "You Got a ..." header override: static
    // analysis (ida_context/Scene/StageSceneStateGetShine*.md) only turned up
    // "TxtScenario" (the moon-name subtitle, already handled below) and
    // "TxtDay" as panes written on this screen - no separate fixed header pane
    // or message key was found, and StageSceneLayout's own construction (where
    // a static caption would most likely be set) isn't decompiled. Rather than
    // guess a pane name into an override, probe read-only for plausible header
    // pane names on this same layout the first time this hook runs, so the
    // next in-game test's log reveals what's actually there.
    //
    // Three DIFFERENT layouts reach this hook, one per get-shine state that
    // shineTextHook is branch-patched into: "GetShineSub" (ordinary moon),
    // "GetShine"/"GetShineCity" (story moon, StageSceneStateGetShineMain) and
    // "GetShineGrand" (triple, StageSceneStateGetShineGrand) - see the layout
    // names in ida_context/Scene/StageSceneStateGetShine*.md. Probing only once
    // per boot described whichever layout happened to come first and said
    // nothing about the other two, which is how "TxtGet exists" got generalized
    // into a write that null-deref'd on the story and grand screens. Probe once
    // per distinct layout name instead.
    static constexpr s32 cShineTextProbeMaxLayouts = 8;
    static char sProbedShineTextLayouts[cShineTextProbeMaxLayouts][64] = {};
    static s32 sProbedShineTextLayoutCount = 0;

    static bool markShineTextLayoutProbed(const char* layoutName) {
        for (s32 i = 0; i < sProbedShineTextLayoutCount; i++) {
            if (std::strcmp(sProbedShineTextLayouts[i], layoutName) == 0)
                return false;
        }
        if (sProbedShineTextLayoutCount >= cShineTextProbeMaxLayouts)
            return false;
        char* slot = sProbedShineTextLayouts[sProbedShineTextLayoutCount++];
        std::strncpy(slot, layoutName, sizeof(sProbedShineTextLayouts[0]) - 1);
        slot[sizeof(sProbedShineTextLayouts[0]) - 1] = '\0';
        return true;
    }

    static void logShineTextPaneProbeOnce(al::LayoutActor* layout, const char* pane) {
        if (!layout)
            return;
        const char* layoutName = layout->getName() ? layout->getName() : "<null>";
        if (!markShineTextLayoutProbed(layoutName))
            return;

        Randomizer::logPush("moon hooks got-shine pane probe layout=%s pane=%s", layoutName, pane ? pane : "<null>");

        // Every header-ish pane name that turns up anywhere in ida_context,
        // plus the TxtGet family above. The story and grand layouts keep their
        // caption somewhere in here; whichever line this logs is the pane the
        // medal override should be extended to write.
        static const char* const kCandidatePanes[] = {
            "TxtTitle",
            "TxtTitleSh",
            "TxtCaption",
            "TxtCaptionSh",
            "TxtHeader",
            "TxtGetTitle",
            "TxtName",
            "TxtNameC",
            "TxtMoon",
            "TxtShine",
            "TxtKind",
            "TxtType",
            "TxtLabel",
            "TxtSub",
            "TxtCategory",
            "TxtGetKind",
            "TxtShineKind",
            "TxtMsg",
            "TxtMessage",
            "TxtMessageSh",
            "TxtInfo",
            "TxtMain",
            "TxtSubTitle",
            "TxtGetName",
            "TxtGetMoon",
            "TxtWorld",
            "TxtWorldSh",
            "TxtRegion",
            "TxtRegionSh",
            "TxtRegionCaption",
        };
        for (const char* candidate : kCandidatePanes) {
            if (al::isExistPane(layout, candidate))
                Randomizer::logPush("moon hooks got-shine pane probe found pane=%s", candidate);
        }
        for (const char* candidate : kShineTextHeaderPanes) {
            if (al::isExistPane(layout, candidate))
                Randomizer::logPush("moon hooks got-shine pane probe found pane=%s", candidate);
        }
    }

    // al::setPaneString resolves the pane and hands the result straight to
    // al::setTextBoxStringLength with no null check
    // (ida_context/Library/Layout/LayoutActorUtil.md), so naming a pane the
    // layout does not have is an immediate null deref rather than a no-op.
    // "TxtGet" exists on the ordinary moon's GetShineSub layout but NOT on the
    // story (GetShine) or triple (GetShineGrand) layouts, which is exactly how
    // collecting a Toadette Medal story or triple moon crashed. Only ever write
    // panes al::isExistPane confirms; the probe above reports what a layout
    // actually offers.
    static void setToadetteMedalHeaderPanes(al::LayoutActor* layout, const char16_t* message) {
        if (!layout)
            return;
        for (const char* pane : kShineTextHeaderPanes) {
            if (al::isExistPane(layout, pane))
                al::setPaneString(layout, pane, message, 0);
        }
    }

    static bool shineTextHook(al::LayoutActor* param_1, char* param_2, char* param_3, char* param_4) {
        bool ret = rs::trySetPaneStageMessageIfExist(param_1, param_2, param_3, param_4);
        if (!ret)
            return ret;

        logShineTextPaneProbeOnce(param_1, param_2);

        const ShineInfo* info = GameDataFunction::getLatestGetShineInfo(param_1);
        GameDataHolderWriter writer(param_1);
        sead::WFixedSafeString<0x20> k;

        const ShineDB::ShineEntry* displayEntry = findDisplayEntryFromShineInfo(info);
        if (!displayEntry && (!info || info->isEmpty()))
            displayEntry = sLatestGotDisplayEntry;
        if (displayEntry) {
            // Redistributed kingdom, so the get-cutscene title names the kingdom the
            // moon was reassigned to (matching its model), not its home kingdom.
            k.append(ShineDB::getWorldNameFromId(destWorld(displayEntry)));
        }

        // "You Got a Medal!" header override: TxtGet (confirmed present on the
        // ordinary got-moon layout via logShineTextPaneProbeOnce's runtime probe
        // - static analysis of StageSceneStateGetShine*.md only turned up
        // TxtScenario/TxtDay, so this pane wasn't reachable through decompiled
        // call sites alone) is separate from TxtScenario (param_2 here, the name
        // subtitle handled below) - overriding it leaves the real moon name
        // intact underneath, matching what the user asked for. Kept to a single
        // short line ("You got a Medal" rendered oversized/overflowing -
        // the pane was sized for vanilla's short "Power Moon" text).
        //
        // setToadetteMedalHeaderPanes skips panes the layout doesn't have, so
        // the story and grand get-shine layouts (which have no TxtGet) simply
        // keep their vanilla caption instead of crashing on the write.
        if (Randomizer::isToadetteMedalModeEnabled() && displayEntry && MoonRando::isToadetteMedalItem(displayEntry->uid, displayEntry->stage)) {
            static constexpr const char16_t* cToadetteMedalHeaderMessage = u"You got a Medal";
            setToadetteMedalHeaderPanes(param_1, cToadetteMedalHeaderMessage);
        }

        // `displayEntry` normally supplied the answer above.  In that common
        // path the old loop still compared every HintInfo record even though
        // its body was guarded by k.isEmpty().  This function runs on normal,
        // story, and grand get-layout setup, so avoid the redundant work on the
        // exact shared pickup path while retaining the physical-location
        // fallback when no display entry is available.
        if (k.isEmpty() && info) {
            for (int x = 0; x < 0x400; x++) {
                GameDataFile::HintInfo* curInfo = &writer->getGameDataFile()->getHintList()[x];
                if (info->mStageName != curInfo->stageName || info->mObjId != curInfo->objId)
                    continue;
                // `curInfo` is the physical location.  The cutscene names the
                // kingdom credited for its reward item, matching the reshuffled
                // moon list and progress counter.
                const ShineDB::ShineEntry* physical = ShineDB::findEntry(curInfo->uniqueId, curInfo->stageName.cstr());
                const ShineDB::ShineEntry* item = physical ? MoonRando::getItemForLocation(physical->uid, physical->stage) : nullptr;
                if (!item)
                    item = physical;
                s32 kingdom = MoonRando::getAssignedKingdomForEntry(item);
                if (kingdom < 0)
                    kingdom = curInfo->worldId;
                k.append(ShineDB::getWorldNameFromId(kingdom));
                break;
            }
        }

        clearCaptureUnlockPane(param_1);
        if (trySetRandomizedShineText(param_1, param_2, info))
            return ret;

        if (k.isEmpty())
            return ret;

        sead::WFixedSafeString<0x100> s;
        s.appendWithFormat(u"\n%s\n(%s)", al::getPaneStringBuffer(param_1, param_2), k.cstr());
        al::setPaneString(param_1, param_2, s.cstr());
        return ret;
    }

    void installHooks() {
        // Acquisition helpers shared by placed, pop-up, and auto-get six-health
        // Life-Up Hearts. Ordinary three-health recovery hearts use a different
        // class and intentionally do not advance this goal.
        lifeMaxUpItemGetHook.installAtMainOffset(0x001c7d34);
        lifeMaxUpItem2DGetHook.installAtMainOffset(0x001c8f98);
        // Present in the game binary but absent from syms/main.sym.
        getPlacementStageNameHook.installAtMainOffset(0x00577c08);
        demoActorCreateHook
            .installAtSym<"_ZN2al44initChildActorWithArchiveNameNoPlacementInfoEPNS_9LiveActorERKNS_13ActorInitInfoERKN4sead14SafeStringBaseIcEEPKc">();
        executeDirectorInitHook.installAtSym<"_ZN2al15ExecuteDirector4initERKNS_21ExecuteSystemInitInfoE">();
        // Present in the game binary but absent from syms/main.sym. The drawer
        // buffer this guards is enlarged in reserveMoonModelDrawerCapacity.
        registerActorModelHook.installAtMainOffset(0x008940fc);
        initActorWithArchiveNameHook.installAtSym<"_ZN2al24initActorWithArchiveNameEPNS_9LiveActorERKNS_13ActorInitInfoERKN4sead14SafeStringBaseIcEEPKc">();
        // Present in the game binary but absent from syms/main.sym.
        tryInitLinkShineHook.installAtMainOffset(0x00555890);
        initActorImplHook.installAtMainOffset(0x008dc12c);
        shineInitHook.installAtSym<"_ZN5Shine4initERKN2al13ActorInitInfoE">();
        // Shine::init's collection query is the sole Completed Kingdom
        // presentation override; all other acquisition callers stay native.
        hk::hook::writeBranchLinkAtMainOffset(0x001cd878, shineInitIsGotShineHook);
        startMclAnimHook.installAtSym<"_ZN2al12startMclAnimEPNS_9LiveActorEPKc">();
        setMclAnimFrameAndStopHook.installAtSym<"_ZN2al22setMclAnimFrameAndStopEPNS_9LiveActorEf">();
        startMclAnimAndSetFrameAndStopHook.installAtSym<"_ZN2al30startMclAnimAndSetFrameAndStopEPNS_9LiveActorEPKcf">();
        // These three targets are present in the game binary but absent from syms/main.sym.
        getStageShineAnimFrameHook.installAtMainOffset(0x005566bc);
        setStageShineAnimFrameHook.installAtSym<"_ZN2rs22setStageShineAnimFrameEPN2al9LiveActorEPKcib">();
        syncVisAnimFrame2DHook.installAtSym<"_ZN2rs18syncVisAnimFrame2DEPN2al9LiveActorEPKcb">();
        shineAfterPlacementHook.installAtSym<"_ZN5Shine18initAfterPlacementEv">();
        shineMakeActorAliveHook.installAtSym<"_ZN5Shine7controlEv">();
        shineGetHook.installAtSym<"_ZN5Shine3getEv">();
        shineGetColorFrameHook.installAtSym<"_ZNK5Shine13getColorFrameEv">();
        // Present in the game binary but absent from syms/main.sym.
        shineCreateEffectInsideObjectHook.installAtMainOffset(0x001d0550);
        sceneHeapCreateHook.installAtSym<"_ZN4sead9FrameHeap6createEmRKNS_14SafeStringBaseIcEEPNS_4HeapEiNS5_13HeapDirectionEb">();
        isGotShineHook.installAtSym<"_ZNK12GameDataFile10isGotShineEPK9ShineInfo">();
        isGotShineIndexHook.installAtSym<"_ZNK12GameDataFile10isGotShineEi">();
        // The hint NPC's three roster functions. Present in the game binary but
        // absent from syms/main.sym.
        isEnableUnlockHintHook.installAtMainOffset(0x00521c50);
        calcRestHintNumHook.installAtMainOffset(0x00521d70);
        unlockHintImplHook.installAtMainOffset(0x00521e90);
        // Present in the game binary but absent from syms/main.sym.
        isEnableOpenMoonRockHook.installAtMainOffset(0x0052916c);
        moonRockReceiveMsgHook.installAtMainOffset(0x002c6118);
        checkGetShineForWorldTravelingPeachHook.installAtMainOffset(0x00533598);
        checkEnableAppearMoonWorldTravelingPeachHook.installAtMainOffset(0x005335d4);
        // setGotShineHook.installAtSym<"_ZN16GameDataFunction11setGotShineE20GameDataHolderWriterPK9ShineInfo">();
        setGotShineFileHook.installAtSym<"_ZN12GameDataFile11setGotShineEPK9ShineInfo">();
        getShineNumHook.installAtSym<"_ZNK12GameDataFile11getShineNumEi">();
        getCurrentWorldShineNumHook.installAtSym<"_ZNK12GameDataFile11getShineNumEv">();
        getTotalShineNumHook.installAtSym<"_ZNK12GameDataFile16getTotalShineNumEv">();
        tryFindShineMessageLayoutHook.installAtSym<"_ZN16GameDataFunction19tryFindShineMessageEPKN2al11LayoutActorEii">();
        tryFindShineMessageActorHook.installAtSym<"_ZN16GameDataFunction19tryFindShineMessageEPKN2al9LiveActorEPKNS0_17IUseMessageSystemEii">();
        setScenarioNameHook.installAtMainOffset(0x004d2840);
        getStageMessageStringHook.installAtSym<"_ZN2al21getStageMessageStringEPKNS_17IUseMessageSystemEPKcS4_">();
        tryGetStageMessageStringHook.installAtSym<"_ZN2al24tryGetStageMessageStringEPPKDsPKNS_17IUseMessageSystemEPKcS7_">();
        isExistLabelInSystemMessageHook.installAtSym<"_ZN2al27isExistLabelInSystemMessageEPKNS_17IUseMessageSystemEPKcS4_">();
        trySetPaneStageMessageIfExistHook.installAtSym<"_ZN2rs29trySetPaneStageMessageIfExistEPN2al11LayoutActorEPKcS4_S4_">();
        setPaneStringHook.installAtSym<"_ZN2al13setPaneStringEPNS_10IUseLayoutEPKcPKDst">();
        startChoiceEventHook.installAtMainOffset(0x55a0a0);
        npcEventCtrlInfoEndChoiceHook.installAtMainOffset(0x3a3460);
        sphinxQuizReceiveEventHook.installAtMainOffset(0x3d3848);
        // Present in the game binary but absent from syms/main.sym.
        shineExeWaitRequestDemoHook.installAtMainOffset(0x001d141c);
        shineExeGotHook.installAtMainOffset(0x001d2d48);
        multiMoonCutsceneHook.installAtSym<"_ZN2rs29requestStartDemoShineGrandGetEP5Shine">();
        // Present in the game binary but absent from syms/main.sym.
        shineExeCoinHook.installAtMainOffset(0x001d31b0);
        storyMoonDemoWaitFirstHook.installAtMainOffset(0x004de1ac);
        storyMoonDemoStateKillHook.installAtMainOffset(0x004de150);
        // exeWaitPlayerAnimFirst's al::startAction on the held moon (state + 0x88).
        hk::hook::writeBranchLinkAtMainOffset(0x004de224, storyMoonDemoStartActionHook);
        // The story route's remaining two action starts on that same held moon:
        // exeDemoGetStart's "DemoGetShineStart" and exeDemoAppearLayout's
        // "DemoGetShineCloseUp". The +0x80 camera object's identically named
        // starts (0x4de484 / 0x4de79c) are deliberately left alone.
        hk::hook::writeBranchLinkAtMainOffset(0x004de4c8, storyMoonDemoStartPhaseActionHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004de7a8, storyMoonDemoStartPhaseActionHook);
        storyMoonDemoGetStartHook.installAtMainOffset(0x004de320);
        storyMoonDemoAppearLayoutHook.installAtMainOffset(0x004de748);
        storyMoonDemoLandHook.installAtMainOffset(0x004deb5c);
        getShineStateKillHook.installAtMainOffset(0x004dbc5c);
        // exeDemoGetFirst's al::isFirstStep(this), before it latches a
        // presentation slot into state + 0x50.
        hk::hook::writeBranchLinkAtMainOffset(0x004dbda8, exeDemoGetFirstIsFirstStepHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004dbf08, exeDemoGetFirstStartActionHook);
        // exeDemoGet's "DemoGetShine" on that same presentation slot (+0x50).
        hk::hook::writeBranchLinkAtMainOffset(0x004dc18c, getShineDemoStartPhaseActionHook);
        // exeDemoGetFirst's tail rs::isActionEndDemoPlayer(state + 0x50), which
        // runs on skipped first-step frames too.
        hk::hook::writeBranchLinkAtMainOffset(0x004dbf54, exeDemoGetFirstIsActionEndDemoPlayerHook);
        grandMoonDemoGetFirstHook.installAtMainOffset(0x004dcfd8);
        grandMoonDemoStateKillHook.installAtMainOffset(0x004dcf8c);
        updateKitListDemoPlayerHook.installAtMainOffset(0x004d22d8);
        // exeDemoGetFirst's three reads of the physical Shine's "GetShineGrand":
        // the action start, the frame count Mario's approach is paced from, and
        // the end condition the step leaves on.
        hk::hook::writeBranchLinkAtMainOffset(0x004dd04c, grandDemoStartShineActionHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004dd214, grandDemoRiseFrameMaxHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004dd24c, grandDemoRiseIsActionEndHook);
        // Preserve the four per-phase action starts on the state's own moon
        // (+0x98), then mirror them once onto the visible generic skin. The
        // native actor remains the choreography/timing driver throughout.
        hk::hook::writeBranchLinkAtMainOffset(0x004dd3b4, grandDemoStartPresentationActionHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004dd7dc, grandDemoStartPresentationActionHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004dda04, grandDemoStartPresentationActionHook);
        hk::hook::writeBranchLinkAtMainOffset(0x004ddc28, grandDemoStartPresentationActionHook);
        dotMoonDemoGetHook.installAtMainOffset(0x004dbfcc);
        grandMoonDemoGetStartHook.installAtMainOffset(0x004dd28c);
        grandMoonDemoEndHook.installAtMainOffset(0x004ddbe0);
        isExistDemoReturnToHomeHook.installAtMainOffset(0x0055162c);
        isPlayerDamageStopDemoHook.installAtMainOffset(0x0056fdb8);
        hk::hook::writeBranchLinkAtMainOffset(0x004ded74, setStoryMoonDemoColor);

        hk::hook::writeBranchLinkAtMainOffset(0x004dc504, shineTextHook);
        hk::hook::a64::assemble<"nop">().installAtMainOffset(0x004dc540);
        hk::hook::writeBranchLinkAtMainOffset(0x004dd680, shineTextHook);
        hk::hook::a64::assemble<"nop">().installAtMainOffset(0x004dd6bc);
        hk::hook::writeBranchLinkAtMainOffset(0x004de63c, shineTextHook);
        hk::hook::a64::assemble<"nop">().installAtMainOffset(0x004de678);
        hk::hook::writeBranchLinkAtMainOffset(0x208fc8, createShopLayoutInfoForCurrentMoon);
        hk::hook::writeBranchLinkAtMainOffset(0x2089C4, getShopItemMessage);
        hk::hook::writeBranchLinkAtMainOffset(0x208A44, getShopItemMessage);
    }

} // namespace MoonHooks
