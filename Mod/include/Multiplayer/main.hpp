#pragma once

#include "sead/basis/seadNew.h"
#include "sead/heap/seadHeap.h"

#include "al/Library/Sequence/Sequence.h"

#include "game/Player/PlayerActorBase.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"

namespace al {
    struct ActorInitInfo;
    class LiveActor;
    class PlacementId;
    class PlacementInfo;
    class Scene;
} // namespace al
class CheckpointFlag;
class GameDataFile;
class Shine;
class ShineInfo;

inline bool isInGame = false;
inline bool debugMode = false;

// ===== GLOBAL VARIABLES =====
inline bool gIsSceneAlive = false;
inline sead::Heap* gHeap = nullptr;
inline int debugPuppetIndex = 0;
inline int pageIndex = 0;
constexpr int maxPages = 4;

void installMultiplayerHooks();
void installSyncHooks();
void installModMenuHooks();
void installInitHooks();
void installOtherHooks();

// ===== SYNC BRIDGE =====
//
// hakkun copies a hooked function's first instruction into the trampoline slot
// without relocating it (see the TODO in hk/hook/Trampoline.h). Installing a
// second HkTrampoline on an already-hooked function therefore copies the first
// hook's PC-relative B, which retargets when executed from the pool -- orig()
// becomes a branch into unmapped memory. One trampoline per function, always.
//
// These six functions are already trampolined by the Randomizer, so the
// Multiplayer side hangs off the existing hooks instead of installing its own.
// Each is a no-op while Multiplayer is dormant (see Client::isSocketActive).
void mpOnActorInitInfo(al::ActorInitInfo* initInfo, al::Scene* scene);
void mpOnShineInitAfterPlacement(Shine* shine);
void mpOnSetGotShine(GameDataHolderWriter writer, const ShineInfo* creditedInfo);
void mpOnAddCoinCollect(GameDataFile* file, const al::PlacementId* placeId);
void mpOnCheckpointGet(CheckpointFlag* checkpoint);
// al::createPlacementActorFromFactory. Its one trampoline is owned by
// EnemySwapHooks or, in a non-chaos build, ProgressionHooks - the two are
// mutually exclusive on SMOR_FEATURE_CHAOS_ANY and BOTH call this.
void mpOnCreatePlacementActor(const al::PlacementInfo* placement);
// Same trampoline as mpOnCreatePlacementActor, but called once the final
// actor exists (after any enemy-swap replacement logic has run) - right
// alongside ProgressionHooks::onPlacementActorCreated, the same call site
// both EnemySwapHooks.cpp and ProgressionHooks.cpp already use to hand the
// finished actor + its placement info to Randomizer code. EnemyIdentity
// uses this to learn {stable key -> LiveActor*} for capturable enemies.
void mpOnPlacementActorCreated(al::LiveActor* actor, const al::PlacementInfo* placement);

// Suppresses addCoinHook/subCoinHook's outbound broadcast for the duration
// of the scope. Two situations need this: applying a peer's already-synced
// coin change locally (updateAddCoin/updateSubCoin re-invoking the real
// GameDataFunction::addCoin/subCoin so the receiving player's UI/counter
// updates, which would otherwise look like a fresh local pickup/spend and
// get re-broadcast), and a purely local/programmatic coin change that was
// never a real pickup to begin with (e.g. granting starting coins on seed
// generation, which both players already apply for themselves and must
// not also relay to whoever's already connected). Same shape as
// MoonHooks::RemoteShineGrantScope.
class SuppressCoinSyncScope {
public:
    SuppressCoinSyncScope();
    ~SuppressCoinSyncScope();
    SuppressCoinSyncScope(const SuppressCoinSyncScope&) = delete;
    SuppressCoinSyncScope& operator=(const SuppressCoinSyncScope&) = delete;

private:
    bool mPrev;
};
bool isCoinSyncSuppressed();

void drawMain(al::Sequence* seq);
void updatePlayerInfo(GameDataHolderAccessor holder, PlayerActorBase* playerBase, bool isYukimaru);
