#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorSceneInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/IUseNerve.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Placement/PlacementInfo.h"

#include "game/Item/ShineInfo.h"
#include "game/MapObj/CheckpointFlag.h"
#include "game/MapObj/MoonRock.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/AchievementUtil.h"

#include "Multiplayer/algorithms/CaptureSyncTable.h"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/main.hpp"
#include "Multiplayer/puppets/LateModelDrawer.h"
#include "Multiplayer/puppets/PuppetCapturePool.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/sync/DurableSync.h"
#include "Multiplayer/sync/SyncedActor.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "ShineDB.h"
#include "helpers.hpp"

// Shine::initAfterPlacement is already trampolined by MoonHooks'
// shineAfterPlacementHook (one trampoline per function -- see the note in
// Multiplayer/main.hpp), which calls this right after the original runs.
void mpOnShineInitAfterPlacement(Shine* shine) {
    if (!Client::isSocketActive())
        return;

    if (shine && shine->mShineIdx >= 0) {
        Client::tryRegisterShine(shine);
    }
}

static HkTrampoline<void, void*, CoinCollect*> registerCoinCollectToListHook = hk::hook::trampoline(
    [](void* coinCollectHolder, CoinCollect* coin) -> void {
        registerCoinCollectToListHook.orig(coinCollectHolder, coin);
        Client::tryRegisterCoinCollect(coin);
    });

static HkTrampoline<void, void*, CoinCollect2D*> registerCoinCollect2DToListHook = hk::hook::trampoline(
    [](void* coinCollectHolder, CoinCollect2D* coin) -> void {
        registerCoinCollect2DToListHook.orig(coinCollectHolder, coin);
        Client::tryRegisterCoinCollect2D(coin);
    });

// GameDataFunction::setGotShine is already trampolined by MoonHooks'
// setGotShineHook (one trampoline per function -- see the note in
// Multiplayer/main.hpp), which calls this immediately before it commits.
//
// NOTE: `info` is the shine actually being *credited*, not the one physically
// picked up. Under moon rando those differ, and peers have to agree on the
// credited item or their save state diverges from ours.
//
// Because `info` is already the credited/destination ShineInfo, its
// got-flag must be read directly rather than through
// GameDataFunction::isGotShine. That native call flows into
// MoonHooks' isGotShineHook, which -- unless told otherwise -- treats
// whatever ShineInfo it's given as a *physical location* and runs it
// through MoonRando::getCreditMoon() again. For most moons that second
// remap finds nothing and is a no-op, but where a credited moon's
// stageName/objId also happens to key a real location entry (observed
// with Sand Kingdom moons), it resolves to a *different* destination and
// reports that slot's got-flag instead of this one's. An already-collected
// moon can then read back as not-yet-collected, and this function sends a
// stray ShineCollect packet to peers for a moon Player 1 didn't actually
// (re)earn. MoonHooks::isGotShineRaw reads the flag for exactly the
// ShineInfo it's given, with the remap disabled, which is what a
// credited/destination lookup needs here.
void mpOnSetGotShine(GameDataHolderWriter writer, const ShineInfo* info) {
    if (!info)
        return;

    // Not gated on the socket alone any more, and that is the whole point of
    // DurableSync existing. Everything below routes through
    // DurableSync::submit, which writes the moon to the SD card and retries it
    // until the server acknowledges it - a socket that is down for a few
    // seconds is exactly the case it was built for. Returning here first meant
    // a moon collected during a reconnect was never submitted at all: not
    // queued, not persisted, not retried, and only recoverable if the periodic
    // state vector later happened to notice it. isReady() is the right
    // predicate because it means "attached to a room and a team, so this
    // belongs in a log", and it stays false in solo play.
    if (!Client::isSocketActive() && !DurableSync::isReady())
        return;

    // Applying a peer's already-credited moon locally (MoonHooks::
    // applyRemoteShine) deliberately re-enters this exact hook through the
    // real setGotShine path, so unlock effects/animations fire correctly -
    // but that means this function can't tell "a peer's grant is being
    // applied" apart from "the local player actually picked this up"
    // without this check. Confirmed root cause of a runaway moon-grant
    // loop: without it, applying a remote grant immediately re-broadcast
    // it as a fresh local pickup, which the other peer then applied and
    // re-broadcast again - and since isGotShineRaw's remap can land a
    // bounced packet on a different HintInfo slot than intended (see the
    // comment above), the cascade spread across many slots/kingdoms at
    // once instead of just ping-ponging one moon back and forth.
    if (MoonHooks::isRemoteShineGrantActive() || MoonHooks::isAutoGrantActive())
        return;

    if (!MoonHooks::isGotShineRaw(writer->getGameDataFile(), info)) {
        for (int x = 0; x < 0x400; x++) {
            GameDataFile::HintInfo* curInfo = &writer->getGameDataFile()->getHintList()[x];
            if (info->mStageName == curInfo->stageName && info->mObjId == curInfo->objId) {
                // mpOnSetGotShine always runs after applyUnlockFeedback for
                // this exact moon on this save (see MoonHooks.cpp's
                // setGotShineHook) - safe to ask what unlockForMoon actually
                // just granted, unlike a static per-moon lookup (which
                // doesn't account for progression-chain resolution against
                // this save's own unlock state - confirmed wrong via real
                // reports). Sent on the wire (ShineCollect::abilityId) since
                // the RECEIVING player has no way to derive it themselves -
                // it depends on the sender's own save state, not anything
                // knowable from shineId alone.
                const AbilityRando::AbilityEntry* grantedAbility = AbilityRando::getLastUnlockedAbilityCount() > 0 ? AbilityRando::getLastUnlockedAbility(0) : nullptr;
                Client::sendShineCollectPacket(
                    curInfo->uniqueId, grantedAbility ? static_cast<int>(grantedAbility->id) : -1);

                // Dedicated fast-path signals, additive to the ShineCollect
                // send above - see MoonRockKeyUnlock.h/PaintingUnlock.h.
                // Mutually exclusive (a moon is never both), same as
                // PlayerEventLog::getShineExtras resolves them off this
                // same uid/stage pair just below.
                const s32 keyKingdom = MoonRando::getMoonRockKeyKingdomForItem(curInfo->uniqueId, curInfo->stageName.cstr());
                if (keyKingdom >= 0) {
                    Client::sendMoonRockKeyUnlockPacket(keyKingdom);
                } else if (const ShineDB::ShineEntry* entry = ShineDB::findEntry(curInfo->uniqueId, curInfo->stageName.cstr())) {
                    if (PaintingRando::getCloudMoonPaintingUnlockCount(*entry) > 0) {
                        Client::sendPaintingUnlockPacket(
                            PaintingRando::getCloudMoonPaintingUnlockIndex(*entry, 0));
                    }
                }

                const char* shineMessage = PlayerEventLog::getShineMessageWithSource(
                    curInfo->uniqueId, curInfo->stageName, curInfo->objId);
                PlayerEventLog::ShineExtras extras = PlayerEventLog::getShineExtras(curInfo->uniqueId, curInfo->stageName);
                PlayerEventLog::fillLastUnlockedAbility(extras);
                PlayerEventLog::addSelfEvent(PlayerEventLog::SHINE, shineMessage, extras);
                // Only one HintInfo slot should ever describe this pickup. If a
                // second slot's stageName/objId happens to match (e.g. after a
                // moon-rando remap rewrites the runtime HintInfo list), stopping
                // here keeps this from sending a second ShineCollect packet --
                // and a second PlayerEventLog entry -- for the same moon.
                break;
            }
        }
    }
}

static HkTrampoline<void, GameDataFile*, const char*> sendToadetteShinePacketHook = hk::hook::trampoline(
    [](GameDataFile* file, const char* name) -> void {
        if (!rs::checkGetAchievement(file->getGameDataHolder(), name)) {
            for (int i = 0; i < hk::util::arraySize(toadetteMoons); i++) {
                if (strcmp(toadetteMoons[i], name) == 0) {
                    Client::sendShineCollectPacket(2000 + i);

                    PlayerEventLog::addSelfEvent(PlayerEventLog::SHINE,
                        PlayerEventLog::getAchievementMessage(name));
                }
            }
            sendToadetteShinePacketHook.orig(file, name);
        }
    });

// GameDataFile::addCoinCollect is already trampolined by PurpleCoinRewards'
// addCoinCollectHook (one trampoline per function -- see the note in
// Multiplayer/main.hpp), which calls this before any of its own logic, since
// several of its paths return without ever reaching the original.
//
// The world id sent here must match the *logical* kingdom PurpleCoinRewards
// just credited the coin to locally, not the game's raw native
// getCurrentWorldIdNoDevelop. Under entrance rando a sub-area's logical
// kingdom can differ from the kingdom its physical stage belongs to (e.g. a
// wooded sub-area remapped under Cap Kingdom); addCoinCollectHook already
// works this out via PurpleCoinRewards::findOriginalWorldIdForStage before
// calling us, and peers need that same value on the wire so they credit the
// same kingdom the sender did -- otherwise a coin collected in a remapped
// sub-area shows up on the receiving player's *native* kingdom instead of
// the sender's logical one.
void mpOnAddCoinCollect(GameDataFile* file, const al::PlacementId* placeID) {
    if (!Client::isSocketActive() || !file || !placeID)
        return;

    const s32 nativeWorldId = file->getCurrentWorldIdNoDevelop();
    s32 worldId = nativeWorldId;

    GameDataHolder* holder = file->getGameDataHolder();
    if (holder) {
        GameDataHolderAccessor accessor(holder);
        const s32 remappedWorldId = PurpleCoinRewards::findOriginalWorldIdForStage(
            GameDataFunction::getCurrentStageName(accessor));
        // findOriginalWorldIdForStage keys off the physical stage, same as
        // the native path, so only treat it as a genuine remap when it
        // disagrees with the game's own current-world id (mirrors the
        // same-kingdom short circuit in PurpleCoinRewards::getRemappedWorldId).
        if (remappedWorldId >= 0 && remappedWorldId != GameDataFunction::getCurrentWorldId(accessor))
            worldId = remappedWorldId;
    }

    al::StringTmp<128> placeIDString;
    placeID->makeString(&placeIDString);
    Client::sendCoinCollectCollectPacket(placeIDString.cstr(), worldId, file->getStageNameCurrent());

    PlayerEventLog::addSelfEvent(PlayerEventLog::PURPLE, worldNames[worldId]);
}

// CheckpointFlag::exeGet is already trampolined by EntranceHooks'
// checkpointGetHook (one trampoline per function -- see the note in
// Multiplayer/main.hpp). It computes isFirstStep before running the original
// and calls this there, so the pre-orig ordering is preserved.
void mpOnCheckpointGet(CheckpointFlag* checkpoint) {
    if (!Client::isSocketActive() || !checkpoint)
        return;

    al::StringTmp<128> placementId = al::makeStringPlacementId(checkpoint->getPlacementId());
    Client::sendCheckpointGetPacket(placementId.cstr());

    PlayerEventLog::addSelfEvent(PlayerEventLog::CHECKPOINT,
        PlayerEventLog::getCheckpointMessage(placementId));
}

class StageSceneStateSelectMode : public al::IUseNerve { };
static HkTrampoline<void, StageSceneStateSelectMode*> startNewGameHook = hk::hook::trampoline(
    [](StageSceneStateSelectMode* thisPtr) -> void {
        if (al::isStep(thisPtr, 5)) {
            PlayerEventLog::addSelfEvent(PlayerEventLog::START, "");
            Client::sendGameStartPacket();
        }

        startNewGameHook.orig(thisPtr);
    });

static bool sCoinSyncSuppressed = false;

SuppressCoinSyncScope::SuppressCoinSyncScope()
    : mPrev(sCoinSyncSuppressed) {
    sCoinSyncSuppressed = true;
}

SuppressCoinSyncScope::~SuppressCoinSyncScope() {
    sCoinSyncSuppressed = mPrev;
}

bool isCoinSyncSuppressed() {
    return sCoinSyncSuppressed;
}

// GameDataFunction::addCoin/subCoin aren't hooked anywhere else, so these are
// new trampolines (unlike the mpOnXxx functions above, which hang off hooks
// the Randomizer already owns). Regular coins have no mod-owned bypass
// function like customAddCoinCollect/customUseCoinCollect, so applying a
// remote grant/spend (Client::updateAddCoin/updateSubCoin) re-invokes these
// same real functions -- SuppressCoinSyncScope is what stops that re-entry
// (and any other non-pickup programmatic call, e.g. granting starting coins)
// from being mistaken for a fresh local pickup/spend and re-sent.
static HkTrampoline<void, GameDataHolderWriter, s32> addCoinHook = hk::hook::trampoline(
    [](GameDataHolderWriter writer, s32 count) -> void {
        addCoinHook.orig(writer, count);

        if (!Client::isSocketActive() || count == 0 || isCoinSyncSuppressed())
            return;

        Client::sendAddCoinPacket(count);
        PlayerEventLog::addSelfEvent(PlayerEventLog::COIN, "", PlayerEventLog::ShineExtras(), count);
    });

static HkTrampoline<void, GameDataHolderWriter, s32> subCoinHook = hk::hook::trampoline(
    [](GameDataHolderWriter writer, s32 count) -> void {
        subCoinHook.orig(writer, count);

        if (!Client::isSocketActive() || count == 0 || isCoinSyncSuppressed())
            return;

        Client::sendSubCoinPacket(count);
    });

static HkTrampoline<void, MoonRock*> moonRockHook = hk::hook::trampoline([](MoonRock* moonRock) -> void {
    if (al::isFirstStep(moonRock)) {
        Client::sendMoonRockHitPacket(GameDataFunction::getCurrentWorldIdNoDevelop(moonRock));

        PlayerEventLog::addSelfEvent(PlayerEventLog::MOONROCK,
            worldNames[GameDataFunction::getCurrentWorldIdNoDevelop(moonRock)]);
    }

    moonRockHook.orig(moonRock);
});

// al::tryGetClassName(const char**, const al::PlacementInfo&) is the full
// implementation the ActorInitInfo overload is an 8-byte thunk into, and it is
// the only overload that answers for a bare PlacementInfo. It is present in the
// game binary but absent from the mod link map, so calling it by name does not
// link -- see ida_context/Library/Placement/PlacementFunction.md. Keep the call
// at the known main-module address, the same way EntranceHooks reaches its
// unexported helpers. Going through the real function rather than reading the
// byml here also means EnemySwapHooks' hook on this address still gets to
// rewrite a swapped actor's identity before the pool sees the name.
static bool tryGetPlacementClassName(const char** name, const al::PlacementInfo& placementInfo) {
    static constexpr ptrdiff_t kTryGetClassNamePlacementOffset = 0x0099e970;
    using Fn = bool (*)(const char**, const al::PlacementInfo&);
    return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start()
        + kTryGetClassNamePlacementOffset)(name, placementInfo);
}

static bool tryGetPlacementModelName(const char** name, const al::PlacementInfo& placementInfo) {
    static constexpr ptrdiff_t kTryGetModelNamePlacementOffset = 0x009a3270;
    using Fn = bool (*)(const char**, const al::PlacementInfo&);
    reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start()
        + kTryGetModelNamePlacementOffset)(name, placementInfo);

    if (!*name) {
        // al::tryGetObjectName(const char**, const al::PlacementInfo&) - 0x0099e688.
        // getModelName only answers for placements that carry an explicit ModelName or
        // ArchiveName key; HackMapParts variants carry neither and are told apart by
        // their UnitConfigName, which is what the model ends up named. Same fallback
        // order the engine uses, so the discriminator matches HackMapParts::init.
        static constexpr ptrdiff_t kTryGetObjectNamePlacementOffset = 0x0099e688;
        reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start()
            + kTryGetObjectNamePlacementOffset)(name, placementInfo);
    }
    return *name != nullptr;
}

// Records which capture classes this stage places, so PuppetCapturePool knows
// which archives are resident and therefore safe to build a model from later.
//
// NOT a hook. al::createPlacementActorFromFactory already has exactly one
// trampoline on it -- EnemySwapHooks' createPlacementActorHook, or
// ProgressionHooks' under #if !SMOR_FEATURE_CHAOS_ANY -- and both of those call
// here. A second HkTrampoline on the same function is not additive: hakkun
// copies the hooked function's first instruction into the trampoline slot
// WITHOUT relocating it, so the second install copies the first hook's
// PC-relative B, and orig() becomes a branch into unmapped memory (see the sync
// bridge note in Multiplayer/main.hpp). Installing one here did exactly that --
// al::initPlacementByStageInfo branched to garbage on the first placed object of
// every stage, and Citron "recovered" by stepping PC+4 through unmapped memory
// forever: black screen at 6 fps rather than a clean crash.
//
// This creates nothing. The version it replaces built a PuppetHackActor here,
// per puppet slot, for every capture class in the stage - which is the cost the
// pool exists to avoid. All that happens now is a hash lookup and, for the
// handful of hits, a string copy.
void mpOnCreatePlacementActor(const al::PlacementInfo* placement) {
    // Registration moved to mpOnPlacementActorCreated so it can discriminate on actor model name.
}

// Same trampoline as mpOnCreatePlacementActor above, but fired once the
// final actor exists (see the sync bridge note in Multiplayer/main.hpp) -
// PuppetCapturePool needs to know the real actor's class actually finished
// construction, which isn't knowable yet at mpOnCreatePlacementActor's call
// site (that fires from placement DATA alone, before construction runs).
void mpOnPlacementActorCreated(al::LiveActor* actor, const al::PlacementInfo* placement) {
    if (!Client::isSocketActive() || !actor || !placement)
        return;

    al::Sequence* sequence = Client::getSequence();
    if (sequence) {
        auto scene = sequence->mCurrentScene;

        if (!scene || !scene->mIsAlive
            || !al::isEqualString(scene->mName.cstr(), "StageScene")) {
            return;
        }
    }

    const char* className = nullptr;
    const char* modelName = nullptr;
    tryGetPlacementClassName(&className, *placement);
    tryGetPlacementModelName(&modelName, *placement);

    // The real placed actor of a capturable class just finished construction
    // - since that construction succeeds only if its resource archive is
    // already resident, this is a precise, observed signal (not a guess)
    // that it's now safe to build PuppetCapturePool's own stand-in model for
    // that same archive. See PuppetCapturePool.h for the crash history this
    // replaces a frame-delay heuristic for.
    const char* actorModelName = nullptr;
    if (actor->getModelKeeper()) {
        static constexpr ptrdiff_t kGetModelNameOffset = 0x008e2000;
        using FnGetModelName = const char* (*)(const al::LiveActor*);
        actorModelName = reinterpret_cast<FnGetModelName>(hk::ro::getMainModule()->range().start()
            + kGetModelNameOffset)(actor);
    }
    if (className)
        PuppetCapturePool::onPlacementActorCreated(className, modelName, actorModelName);

    // A ride is matched back from the actor at send time, not from a name:
    // the sender follows the local player's bind sensor to its host and has
    // only a pointer. Record the pairing while both are in hand.
    if (className) {
        const CaptureSync::Entry* rideEntry = CaptureSync::findByPlacement(
            className, actorModelName ? actorModelName : modelName);
        if (rideEntry && rideEntry->build == CaptureSync::Build::Ride)
            CaptureSync::noteRidePlacement(actor, rideEntry);
    }

    // Generic actor sync registers here too. This is the hook the reference
    // design installs its own trampoline for (GameActorInitHook); we cannot,
    // because hakkun allows one trampoline per function and this one is
    // already owned - see the sync-bridge note in Multiplayer/main.hpp.
    //
    // Note this fires only for PLACEMENT actors, where the reference design's
    // hook fires for every actor. For enemies and world objects - all Phase 1
    // syncs - that is the set we want, and the safer subset.
    SyncedActor::entry(actor, className);
}

static HkTrampoline<PlayerCostumeInfo*, al::LiveActor*, al::ActorInitInfo&, char*, char*, al::AudioKeeper*, bool>
    initMarioModelActorHook = hk::hook::trampoline(
        [](al::LiveActor* actor, al::ActorInitInfo& initInfo, char* bodyModel, char* capModel,
            al::AudioKeeper* keeper, bool isCloset) -> PlayerCostumeInfo* {
            if (!isCloset && !LateModelDrawer::isPuppetCostumeBuildActive())
                Client::sendCostumeInfPacket(bodyModel, capModel);
            return initMarioModelActorHook.orig(actor, initInfo, bodyModel, capModel, keeper, isCloset);
        });

// A late costume/capture model cannot join the scene's fixed clipping list.
// The normal constructors still call these helpers (including inside the
// game's 2D Mario builder), so the late-build scope suppresses only those
// registrations. The stand-ins are intentionally never clipped; their
// ordinary model/executor tables are repaired separately by LateModelDrawer.
// These are new trampolines for distinct functions; the existing hooks in
// MoonHooks/EntranceHooks are on different targets.
static HkTrampoline<void, al::LiveActor*, const al::ActorInitInfo&> initActorClippingLateHook
    = hk::hook::trampoline([](al::LiveActor* actor, const al::ActorInitInfo& initInfo) -> void {
          if (LateModelDrawer::isLateBuildActive())
              return;
          initActorClippingLateHook.orig(actor, initInfo);
      });

static HkTrampoline<void, al::LiveActor*> invalidateClippingLateHook = hk::hook::trampoline(
    [](al::LiveActor* actor) -> void {
        if (LateModelDrawer::isLateBuildActive())
            return;
        invalidateClippingLateHook.orig(actor);
    });

static HkTrampoline<void, al::LiveActor*, const al::LiveActor*> syncClippingSubActorLateHook
    = hk::hook::trampoline([](al::LiveActor* actor, const al::LiveActor* subActor) -> void {
          if (LateModelDrawer::isLateBuildActive())
              return;
          syncClippingSubActorLateHook.orig(actor, subActor);
      });

static HkTrampoline<al::ActorSceneInfo*, const al::LiveActor*, s32> declareDepthShadowLateHook
    = hk::hook::trampoline([](const al::LiveActor* actor, s32 count) -> al::ActorSceneInfo* {
          if (LateModelDrawer::isLateBuildActive())
              // LiveActor::getSceneInfo() exists in RedStar but is absent from
              // this mod's link map. The matched implementation is just
              // `return mSceneInfo`, so read the already-public field directly
              // instead of creating an undefined relocation.
              return actor ? actor->mSceneInfo : nullptr;
          return declareDepthShadowLateHook.orig(actor, count);
      });

void installSyncHooks() {
    // Shine Syncing
    // NO hooks on GameDataFunction::setGotShine or Shine::initAfterPlacement
    // here -- MoonHooks already owns both and calls mpOnSetGotShine /
    // mpOnShineInitAfterPlacement for us.
    sendToadetteShinePacketHook.installAtSym<"_ZN12GameDataFile14getAchievementEPKc">();

    // CoinCollect Syncing
    // NO hook on GameDataFile::addCoinCollect here -- PurpleCoinRewards already
    // owns it and calls mpOnAddCoinCollect for us.
    registerCoinCollectToListHook
        .installAtSym<"_ZN17CoinCollectHolder19registerCoinCollectEP11CoinCollect">();
    registerCoinCollect2DToListHook
        .installAtSym<"_ZN17CoinCollectHolder21registerCoinCollect2DEP13CoinCollect2D">();

    // CheckpointFlag Syncing
    // NO hook on CheckpointFlag::exeGet here -- EntranceHooks already owns it
    // and calls mpOnCheckpointGet for us.

    // StartGame Syncing
    startNewGameHook.installAtSym<"_ZN20SceneStateSelectMode9exeDecideEv">();

    // MoonRock Syncing
    moonRockHook.installAtSym<"_ZN8MoonRock11exeReactionEv">();

    addCoinHook.installAtSym<"_ZN16GameDataFunction7addCoinE20GameDataHolderWriteri">();
    subCoinHook.installAtSym<"_ZN16GameDataFunction7subCoinE20GameDataHolderWriteri">();

    // Outfit Syncing
    initMarioModelActorHook.installAtSym<"R_ZN14PlayerFunction19initMarioModelActor">();

    // Late model construction has no safe place in the stage-sized clipping
    // and depth-shadow registries. Keep the registration helpers in one hook
    // each and gate them with LateModelDrawer::ScopedLateBuild.
    initActorClippingLateHook.installAtSym<"_ZN2al17initActorClippingEPNS_9LiveActorERKNS_13ActorInitInfoE">();
    invalidateClippingLateHook.installAtSym<"_ZN2al18invalidateClippingEPNS_9LiveActorE">();
    syncClippingSubActorLateHook.installAtSym<"_ZN2al22onSyncClippingSubActorEPNS_9LiveActorEPKS0_">();
    // No exported symbol is present for this small helper in the mod link
    // map. The backed-up IDA context identifies the exact main-module offset.
    declareDepthShadowLateHook.installAtMainOffset(0x008dab2c);
    LateModelDrawer::installHooks();
    PuppetCapturePool::installHooks();
    CaptureSync::installHooks();

    // Capture Syncing
    //
    // Both halves are live now. The send side (Client::sendCaptureInfPacket)
    // and PuppetInfo::isCaptured/curHack were already there; PuppetHackActor
    // has an implementation, and PuppetCapturePool leases those models out on
    // demand instead of pre-building one per puppet per capture class.
    //
    // NO hook on al::createPlacementActorFromFactory here -- EnemySwapHooks
    // (or ProgressionHooks in a non-chaos build) already owns its single
    // trampoline and calls mpOnCreatePlacementActor for us.
    // PuppetActor::tryRefreshCapture() falls back to the player's own body
    // whenever the pool cannot serve a request, so a stage whose captures were
    // never registered degrades rather than breaks.

    // The GpuMemAllocator bump the original design needed is deliberately NOT
    // reinstated: it was there to survive building capture models for every
    // puppet at stage load. The pool builds at most PuppetCapturePool
    // kMaxActors of them, on demand, so leave the game's own budget alone
    // unless a real hardware run says otherwise.
    // hk::hook::a64::assemble<"mov w2, {}">().arg(0x1d00000 + 0x0200000).installAtMainOffset(0x00878708);
    // hk::hook::a64::assemble<"mov w2, {}">().arg(0x3e00000 + 0x0200000).installAtMainOffset(0x00878730);
    // hk::hook::a64::assemble<"mov w2, {}">().arg(0x0300000 + 0x0200000).installAtMainOffset(0x0087875c);
}
