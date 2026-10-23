#include "Archipelago/ArchipelagoHooks.hpp"

#include "hk/hook/InstrUtil.h"
#include "hk/hook/Replace.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "hk/ro/RoUtil.h"
#include "hk/util/Math.h"

#include "sead/prim/seadSafeString.h"

#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/Memory/HeapUtil.h"

#include "game/Actors/GrowFlowerPot.h"
#include "game/Demo/DemoStateHackFirst.h"
#include "game/System/GameDataFunction.h"
#include "game/Util/StageLayoutFunction.h"

#include "Archipelago/Archipelago.h"
#include "helpers.hpp"
#include "rs/util.hpp"

// ===== Stage Changing =====
static void onGrandShineStageChange(GameDataHolderWriter writer, const ChangeStageInfo* stageInfo) {
    if (Archipelago::isArchipelagoActive()) {
        Archipelago::setScenario(stageInfo->mChangeStageName.cstr(), stageInfo->mScenarioNo);

        // Archipelago::sendStage(writer, stageInfo);
    } else {
        GameDataFunction::tryChangeNextStage(writer, stageInfo);
    }
}

static void changeNextStage(GameDataFile* file, const ChangeStageInfo* stageInfo, int param2) {
    if (!Archipelago::isArchipelagoActive()) {
        file->changeNextStage(stageInfo, param2);
    } else {
        ChangeStageInfo* erInfo = nullptr;
        if (file->isUseMissRestartInfo()) {
            erInfo = Archipelago::getLastERTransition();
        } else {
            erInfo = Archipelago::handleER(stageInfo);
        }

        //  Add Wooded shop moon warp

        if (!(al::isEqualString(stageInfo->mChangeStageId.cstr(), "obj846") || al::isEqualString(stageInfo->mChangeStageId.cstr(), "obj1084"))) {
            if (isPartOf(stageInfo->mChangeStageName.cstr(), "WorldHomeStage")) {
                if (Archipelago::setScenario(stageInfo->mChangeStageName.cstr(), stageInfo->mScenarioNo)) {
                    Archipelago::sendCorrectScenario(stageInfo);

                } else {
                    if (erInfo) {
                        file->changeNextStage(erInfo, param2);
                    } else {
                        file->changeNextStage(stageInfo, param2);
                    }
                }
            } else {
                // Non world transitions

                if (erInfo) {
                    file->changeNextStage(erInfo, param2);
                } else {
                    file->changeNextStage(stageInfo, param2);
                }
            }
        } else {
            // Catch cap and cascade shop moons

            file->changeNextStage(stageInfo, param2);
        }
    }
}

// static bool tryFindLinkDestStageInfoOverride(GameDataHolder* holder, const char** destStageName,
// const char** destLabel, const char* srcStageName,
//                                              const char* srcLabel) {
//     sead::S return holder->tryFindLinkDestStageInfo(destStageName, destLabel, srcStageName,
//     srcLabel);
// }

// includes paintings
// static HkTrampoline<void, GameDataFile*, const ChangeStageInfo*, int> changeNextStageHook =
//     hk::hook::trampoline([](GameDataFile* file, const ChangeStageInfo* stageInfo, int param2) ->
//     void {
//         if (!Archipelago::isArchipelagoActive()) {
//             changeNextStageHook.orig(file, stageInfo, param2);
//         } else {

//             //  Add Wooded shop moon warp

//             if (!(al::isEqualString(stageInfo->mChangeStageId.cstr(), "obj846") ||
//             al::isEqualString(stageInfo->mChangeStageId.cstr(), "obj1084"))) {
//                 if (isPartOf(stageInfo->mChangeStageName.cstr(), "WorldHomeStage")) {

//                     stageInfo->mScenarioNo)) {

//                         Archipelago::sendCorrectScenario(stageInfo);

//                     } else {

//                         file->changeNextStage(stageInfo, param2);
//                     }
//                 } else {
//                     // Non world transitions

//                     file->changeNextStage(stageInfo, param2);
//                 }
//             } else {
//                 // Catch cap and cascade shop moons

//                 file->changeNextStage(stageInfo, param2);
//             }
//         }
//     });

// ===== QOL Changes =====
bool growOnPlant(GrowFlowerPot* thisPtr) {
    if (Archipelago::isArchipelagoActive())
        // rs::addGrowFlowerGrowLevel(thisPtr, thisPtr->mPlacementId, 255);
        thisPtr->tryMaxGrowLevel();
    return al::isActionEnd(thisPtr);
}

// ===== Demo Hooks =====
// _ZN16HakoniwaSequence15exeBootLoadDataEv = 0x50F29C - 0x50F304
void onNewGameDemoStart(char* name, bool unkBool) {
    if (Archipelago::isArchipelagoActive()) {
        //     Archipelago::setConnectInitFlag(true);
        //     Archipelago::setFirstConnectFlag(true);
        Archipelago::clearCollectibles();
        Archipelago::clearScenarios();
    }

    al::createSceneHeap(name, unkBool);
    return;
}

// First time entering lost in demo from cloud
static void onUnlockLost(GameDataHolderWriter writer, int worldIndex) {
    // Send Beat Bowser in Cloud location
    if (Archipelago::isArchipelagoActive()) {
        Archipelago::setDefeatedBowserCloud(true);
        Archipelago::sendMoonCheck(2500);
    }

    GameDataFunction::unlockWorld(writer, worldIndex);

    return;
}

// On credits scene initialization
static void onCreditsStart(al::Scene* thisPtr, const al::SceneInitInfo info) {
    if (Archipelago::isArchipelagoActive()) {
        Archipelago::sendMoonCheck(2499);
    }

    thisPtr->initDrawSystemInfo(info);
    return;
}

//
bool skipHackCutscene(DemoStateHackFirst* thisPtr, IUsePlayerHack** param_1, const al::SensorMsg* param_2, al::HitSensor* param_3, al::HitSensor* param_4) {
    if (Archipelago::isArchipelagoActive())
        return false;

    return thisPtr->tryHackFirst(param_1, param_2, param_3, param_4);
}

static void updateListHook(GameProgressData* gameProgressData) {
    gameProgressData->updateList();
    if (Archipelago::isArchipelagoActive()) {
        for (int i = 0; i < 17; i++) {
            gameProgressData->mWorldIdForShineList[i] = i;
        }

        for (int i = 0; i < 17; i++) {
            gameProgressData->mWorldIdForWorldMap[i] = i;
        }

        gameProgressData->mUnlockWorldStatusFirstBranch = GameProgressData::FirstBranch::Lake;
        gameProgressData->mUnlockWorldStatusSecondBranch = GameProgressData::SecondBranch::Snow;

        if (gameProgressData->mUnlockWorldNum == 4) {
            gameProgressData->mIsUnlockWorld[GameDataFunction::getWorldIndexForest()] = true;
            gameProgressData->mIsUnlockWorld[GameDataFunction::getWorldIndexLake()] = false;
        }

        if (gameProgressData->mUnlockWorldNum == 9) {
            gameProgressData->mIsUnlockWorld[GameDataFunction::getWorldIndexSea()] = true;
            gameProgressData->mIsUnlockWorld[GameDataFunction::getWorldIndexSnow()] = false;
        }

        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexWaterfall()] =
        // GameDataFunction::getWorldIndexSky();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSand()] =
        // GameDataFunction::getWorldIndexCity();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexForest()] =
        // GameDataFunction::getWorldIndexLava();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexLake()] =
        // GameDataFunction::getWorldIndexSand();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexCity()] =
        // GameDataFunction::getWorldIndexForest();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSea()] =
        // GameDataFunction::getWorldIndexLake();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSnow()] =
        // GameDataFunction::getWorldIndexWaterfall();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexLava()] =
        // GameDataFunction::getWorldIndexPeach();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSky()] =
        // GameDataFunction::getWorldIndexSea();
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexPeach()] =
        // GameDataFunction::getWorldIndexSnow();

        // 0 = ??? assume Bowser
        // 1 =

        // for (int i = 0; i < 17; i++) {
        //     sead::FixedSafeString<128> paintingId = sead::FixedSafeString<128>();
        //     paintingId = "World Id: ";
        //     paintingId.append(intToCstr(i));
        //     paintingId.append(" Painting Id: ");
        //     paintingId.append(intToCstr(gameProgressData->mWorldIdForWorldWarpHole[i]));

        // }
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexWaterfall()] =
        // 1; gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSand()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexForest()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexLake()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexCity()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSea()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSnow()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexLava()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexSky()] = 1;
        // gameProgressData->mWorldIdForWorldWarpHole[GameDataFunction::getWorldIndexPeach()] = 1;
    }
}

int substituteSlotWorldId(GameProgressData* progress, int selection) {
    int nextWorldId = progress->calcNextLockedWorldIdForWorldMap(selection);
    if (Archipelago::isArchipelagoActive()) {
        switch (nextWorldId) {
        case 3:
            return GameDataFunction::getWorldIndexLake();
        case 4:
            return GameDataFunction::getWorldIndexForest();
        case 8:
            return GameDataFunction::getWorldIndexSnow();
        case 9:
            return GameDataFunction::getWorldIndexSea();
        }
    }

    return nextWorldId;
}

// deeets hooks for fixed world order. Crash on boot for somereason when installed
//  HkTrampoline<int, const void*, int> calcNextLockedLayoutHook =
//      hk::hook::trampoline([](const void* p, int selection) -> int { return
//      substituteSlotWorldId(selection, calcNextLockedLayoutHook.orig(p, selection)); });

// HkTrampoline<int, const void*, int> calcNextLockedSceneHook =
//     hk::hook::trampoline([](const void* p, int selection) -> int { return
//     substituteSlotWorldId(selection, calcNextLockedSceneHook.orig(p, selection)); });

// static HkTrampoline<bool, GameDataHolderAccessor, int> isUnlockedWorldHook =
// hk::hook::trampoline([](GameDataHolderAccessor accessor, int worldId) -> bool {
//     if (Archipelago::isArchipelagoActive()) {
//         if (worldId == (int)GameDataFunction::getWorldIndexHat() || worldId ==
//         (int)GameDataFunction::getWorldIndexWaterfall()) {
//             return true;
//         }
//         int curWorldId = GameDataFunction::getCurrentWorldId(accessor);
//         if (curWorldId >= 0 && worldId == curWorldId) {
//             return true;
//         }
//     }
//     return isUnlockedWorldHook.orig(accessor, worldId);
// });

static bool isUnlockWorldForHomeHook(GameDataHolderAccessor accessor, int worldId) {
    if (Archipelago::isArchipelagoActive()) {
        return true;
    }

    return GameDataFunction::isUnlockedWorld(accessor, worldId);
}

static bool isExistHomeHook(GameDataHolderAccessor accessor) {
    if (Archipelago::isArchipelagoActive()) {
        return true;
    }

    return GameDataFunction::isExistHome(accessor);
}

static int canDepositShinesHook(GameDataHolderAccessor accessor) {
    // Prevent shine deposit if work isn't unlocked
    if (Archipelago::isArchipelagoActive() && !GameDataFunction::isUnlockedCurrentWorld(accessor)) {
        return 0;
    }

    return GameDataFunction::getCurrentShineNum(accessor);
}

// static int exeDemoWorldSelectTalkMessageHook(TalkMessage* worldSelection) {
//     if (Archipelago::isArchipelagoActive()) {
//         GameDataHolderAccessor accessor(((al::LayoutActor*)worldSelection)->getSceneObjHolder());
//         int worldId = GameDataFunction::getCurrentWorldId(accessor);
//         if (worldId == GameDataFunction::getWorldIndexSand()) {
//             worldSelection->mCommonSelectParts->exeDecide();
//             return GameDataFunction::getWorldIndexLake();
//         }
//     }

//     return worldSelection->getSelectedChoiceIndex();
// }

// static void addPayShineHook(GameDataHolderWriter writer, int count) {
//     GameDataFunction::addPayShine(writer, count);
//     if (Archipelago::isArchipelagoActive()) {
//         int worldId = GameDataFunction::getCurrentWorldId(GameDataHolderAccessor(writer.mData));
//         if (worldId == GameDataFunction::getWorldIndexSand()) {
//             if (GameDataFunction::getPayShineNum(GameDataHolderAccessor(writer.mData)) >=
//                 Archipelago::getWorldUnlockCount(worldId))
//                 { GameProgressData* gameProgressData =
//                 writer.mData->getGameDataFile()->getGameProgressData();
//                 gameProgressData->mIsUnlockWorld[GameDataFunction::getWorldIndexForest()] = true;
//                 gameProgressData->mUnlockWorldNum += 1;  // = 4
//             }
//         }
//     }
// }

// =============================================================================
// Talkatoo% mode + Cappy Messenger hooks
// =============================================================================
//
// These hooks are inert until the corresponding ArchipelagoMode flags are
// flipped on:
//   - Talkatoo speech substitution:  ArchipelagoMode::setTalkatooMode(true)
//   - Cappy speech-bubble dispatch:  ArchipelagoMode::setCappyRsCalls(...)
//                                    + ArchipelagoMode::enqueueCappyMessage(...)
//
// All trampolines pass through to Orig when not in ARCHIPELAGO mode, so they
// are also safe to leave installed during multiplayer or freeze-tag modes.
//
// Symbol provenance is on each block. The mangled strings here mirror the
// entries appended to syms/main.sym in the same commit.

// ----- Talkatoo speech substitution -----
//
// Trampoline on GameDataFunction::tryFindShineMessage(const al::LiveActor*,
// const al::IUseMessageSystem*, s32 world_id, s32 index). Talkatoo's
// Poetter::exeWait picks an index from rs::calcShineIndexTableNameAvailable
// and calls this to resolve it to a char16_t* for the speech bubble. We let
// vanilla run, then if (a) Talkatoo% mode is on AND (b) the caller is a
// Poetter (vptr range-check against _ZTV7Poetter), substitute our buffer.
//
// Why vtable filter, not per-callsite hook: tryFindShineMessage is also
// called from cutscene cards, the pause-menu Power Moon list, and Achievement
// reveal popups. Substituting at those would visibly corrupt non-Talkatoo
// flows. The vtable check costs one load + one compare per call.

namespace TalkatooHook {

    // Address of _ZTV7Poetter resolved at install time. 0 = symbol lookup failed
    // (degraded mode — substitute hook returns vanilla for every caller).
    static uintptr_t g_poetterVtableAddr = 0;

    // Vtable span: _ZTV7Poetter primary table + immediately-following Poetter-only
    // aux symbols (_ZTT7Poetter, _ZTC7Poetter, _ZTI7Poetter). Range-check window
    // is widened from the primary table's ~0x1f8 bytes to 0x400 to also catch
    // vptrs that briefly point into a construction-vtable during ctor.
    constexpr uintptr_t kPoetterVtableSpan = 0x400;

    // UTF-16 buffer rotation. The hook returns a char16_t* that SMO stores at
    // Poetter+0x130 and reads via an EventFlow for the duration of the speech
    // bubble (~3-5 s). Four slots make overlapping bubbles + re-entrant calls
    // safe under the single-Talkatoo-per-scene invariant.
    static constexpr size_t kUtfBufCount = 4;
    static constexpr size_t kUtfBufWords = 200;
    static char16_t g_utfBuffers[kUtfBufCount][kUtfBufWords] = {};
    static size_t g_utfBufCursor = 0;

    // Widen UTF-8-ASCII (after the chooseTalkatooSpokenUtf8 stub) into the next
    // rotation slot. Returns the buffer pointer; never returns null (worst case:
    // an empty buffer).
    static const char16_t* asciiToUtf16BufStatic(const char* src) {
        const size_t slot = (g_utfBufCursor++) % kUtfBufCount;
        char16_t* dst = g_utfBuffers[slot];
        size_t o = 0;
        if (src) {
            while (src[o] != '\0' && o + 1 < kUtfBufWords) {
                dst[o] = static_cast<char16_t>(static_cast<unsigned char>(src[o]));
                ++o;
            }
        }
        dst[o] = 0;
        return dst;
    }

    // Range-check the actor's vptr (offset 0) against the Poetter vtable window.
    // Treats g_poetterVtableAddr == 0 as "not a Poetter" so an install-time
    // lookup failure degrades to vanilla speech instead of crashing.
    static bool actorIsPoetter(const void* actor) {
        if (!actor || g_poetterVtableAddr == 0)
            return false;
        const uintptr_t vptr = *reinterpret_cast<const uintptr_t*>(actor);
        return vptr >= g_poetterVtableAddr && vptr < g_poetterVtableAddr + kPoetterVtableSpan;
    }

} // namespace TalkatooHook

static HkTrampoline<const char16_t*, const al::LiveActor*, const al::IUseMessageSystem*, int, int> tryFindShineMessageHook = hk::hook::trampoline([](const al::LiveActor* actor, const al::IUseMessageSystem* sys, int worldId, int index) -> const char16_t* {
    const char16_t* vanilla = tryFindShineMessageHook.orig(actor, sys, worldId, index);

    if (!Archipelago::isArchipelagoActive()) {
        return vanilla;
    }
    // if (!TalkatooHook::actorIsPoetter(actor)) {
    //     return vanilla;
    // }

    // if (!Archipelago::getTalkatooMode()) {
    //     return vanilla;
    // }

    char ascii[64];
    if (!Archipelago::chooseTalkatooSpokenUtf8(worldId, index, ascii, sizeof(ascii))) {
        return vanilla;
    }
    return TalkatooHook::asciiToUtf16BufStatic(ascii);
});

// ----- Talkatoo% picker non-exhaustion -----
//
// Force-false on GameDataFile::isOpenShineName under talkatoo_mode so
// rs::calcShineIndexTableNameAvailable's pool stays at full capacity (it
// counts "indices where this getter returns false"). Without this, every
// vanilla Talkatoo visit shrinks the pool by one and after enough visits
// Poetter shows the terminal "No more hints" line and tryFindShineMessage
// is never called again. See the smo_archipelago equivalent for full
// rationale (worktree's switch-mod/src/hooks/TalkatooMenuMarkHook.cpp).
//
// Pre-approved tradeoff: under talkatoo_mode the pause-menu Power Moon list
// shows NO moons as "named" (vanilla, AP, and Hint-Toad reveals all render
// unmarked). Achievement-hint reveals also re-show next session. Both
// regressions are accepted to keep the picker non-exhausting.

static HkTrampoline<bool, const GameDataFile*, int, int> isOpenShineNameHook = hk::hook::trampoline([](const GameDataFile* self, int worldId, int index) -> bool {
    if (!Archipelago::isArchipelagoActive()) {
        return isOpenShineNameHook.orig(self, worldId, index);
    }
    // if (!Archipelago::getTalkatooMode()) {
    return isOpenShineNameHook.orig(self, worldId, index);
    // }
    // Talkatoo% mode ON: force false so the picker pool stays full.
    // return false;
});

// Pass-through trampoline on GameDataFile::tryUnlockShineName. Kept hooked
// for observability only — log the first hit per session so we can confirm
// non-Talkatoo callers (Achievement reveal, Hint-Toad) exist in this build
// of SMO. Vanilla logic runs unchanged.
static HkTrampoline<bool, GameDataFile*, int, int> tryUnlockShineNameHook = hk::hook::trampoline([](GameDataFile* self, int worldId, int index) -> bool {
    static bool s_loggedFirst = false;
    if (!s_loggedFirst) {
        s_loggedFirst = true;
        sead::FixedSafeString<80> str;
        str = "[talkatoo] first tryUnlockShineName world=";
        str.append(intToCstr(worldId));
        str.append(" idx=");
        str.append(intToCstr(index));
    }
    return tryUnlockShineNameHook.orig(self, worldId, index);
});

void InstallArchipelagoHooks() {
    hk::hook::writeBranchLinkAtMainOffset(0x512AE8, onNewGameDemoStart); // Intro demo start
    hk::hook::writeBranchLinkAtMainOffset(0x50FED4, onUnlockLost); // Beat Bowser in Cloud Check
    hk::hook::writeBranchLinkAtMainOffset(0x4C54A4, onCreditsStart); // Beat the Game Check
    hk::hook::writeBranchLinkAtMainOffset(0x209844,
        isBuyItemHook); // Shop bought items old  0x54C3A0
    hk::hook::writeBranchLinkAtMainOffset(0x38C408, skipHackCutscene); // Skip frog cutscene
    hk::hook::writeBranchAtMainOffset(0x56CC70, canEndHack); // Fix uncapture crash

    hk::hook::writeBranchLinkAtMainOffset(0x4496AC, onAddHack); // Capturesanity checks
    hk::hook::writeBranchLinkAtMainOffset(0x2089C4, getShopItemMessage); // Shop Text Replacement
    hk::hook::writeBranchLinkAtMainOffset(0x208A44, getShopItemMessage); // Shop Text Replacement
    // hk::hook::a64::assemble<"MOV W8, W28">().installAtMainOffset(0x534C58);  // Lock painting
    // order hk::hook::a64::assemble<"MOV W8, W26">().installAtMainOffset(0x534C70);  // as if lake
    // and snow hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534C80);  // are
    // always branch hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534C9C);  //
    // selections

    // Eventually replace this with updateListHook
    // UpdateListHook could also be used for world order rando and painting order rando
    // hk::hook::a64::assemble<"MOV W8, 0x2">().installAtMainOffset(0x534858);  // Lock painting
    // order hk::hook::a64::assemble<"MOV W8, 0x1">().installAtMainOffset(0x534870);  // as if lake
    // and snow hk::hook::a64::assemble<"MOV W8, 0x2">().installAtMainOffset(0x534B0C);  // are
    // always branch hk::hook::a64::assemble<"MOV W8, 0x2">().installAtMainOffset(0x534b24);  //
    // selections hk::hook::a64::assemble<"MOV W8, 0x2">().installAtMainOffset(0x5349E0);  // Always
    // unlock whole moon list hk::hook::a64::assemble<"MOV W8,
    // 0x2">().installAtMainOffset(0x5349F8); hk::hook::a64::assemble<"MOV W8,
    // 0x2">().installAtMainOffset(0x534C50); hk::hook::a64::assemble<"MOV W8,
    // 0x2">().installAtMainOffset(0x534C68);

    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534888);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x5348b0);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534b3c);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534b5c);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534a10);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534a38);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534c80);
    // hk::hook::a64::assemble<"MOV W8, 0x4">().installAtMainOffset(0x534c9c);

    hk::hook::writeBranchLinkAtMainOffset(0x52B154, calcWorldNumForShineListHook); // Always unlock whole moon list
    // getWorldIdForShineListHook.installAtSym<"_ZNK16GameProgressData22getWorldIdForShineListEi">();
    // // shine list // Also Crashes
    hk::hook::writeBranchLinkAtMainOffset(0x52b118, getWorldIdForShineListHook);
    hk::hook::writeBranchLinkAtMainOffset(0x5355c8, updateListHook);
    hk::hook::writeBranchLinkAtMainOffset(0x5354d4, updateListHook);
    hk::hook::writeBranchLinkAtMainOffset(0x535230, updateListHook);
    hk::hook::writeBranchLinkAtMainOffset(0x535168, updateListHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x502ef4, exeDemoWorldSelectTalkMessageHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x30e1a8, addPayShineHook);

    // hk::hook::writeBranchLinkAtMainOffset(0x534740, updateListHook); // Causes Crash on boot

    // Force oder of world unlock selections
    // calcNextLockedLayoutHook.installAtSym<"_ZN16GameDataFunction32calcNextLockedWorldIdForWorldMapEPKN2al11LayoutActorEi">();
    // calcNextLockedSceneHook.installAtSym<"_ZN16GameDataFunction32calcNextLockedWorldIdForWorldMapEPKN2al5SceneEi">();

    hk::hook::writeBranchLinkAtMainOffset(0x52b018, substituteSlotWorldId); // Layout
    hk::hook::writeBranchLinkAtMainOffset(0x52b05c, substituteSlotWorldId); // Scene

    // Don't auto equip cutscene awarded outfits
    // Transition to Branch Link to prevent interfering with base game when AP disabled
    hk::hook::writeBranchLinkAtMainOffset(0x4DD16C, wearCapHook);
    hk::hook::writeBranchLinkAtMainOffset(0x4DD0AC, wearCostumeHook);
    hk::hook::writeBranchLinkAtMainOffset(0x4DD0E8, wearCapHook);
    hk::hook::writeBranchLinkAtMainOffset(0x310FE4, wearCapHook);
    hk::hook::writeBranchLinkAtMainOffset(0x311440, wearCostumeHook);
    hk::hook::writeBranchLinkAtMainOffset(0x311464, wearCapHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x319EE4, wearCostumeHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x3190F4, wearCapHook);

    // Moon Data Replacement
    // Text Label
    hk::hook::writeBranchLinkAtMainOffset(0x4DC504, isReplaceShineLabel);
    hk::hook::writeBranchLinkAtMainOffset(0x4DE63C, isReplaceShineLabel);
    hk::hook::writeBranchLinkAtMainOffset(0x4DD680, isReplaceShineLabel);
    hk::hook::writeBranchLinkAtMainOffset(0x4DC52C, setShineLabel);
    hk::hook::writeBranchLinkAtMainOffset(0x4DE664, setShineLabel);
    hk::hook::writeBranchLinkAtMainOffset(0x4DD6A8, setShineLabel);

    // Color
    hk::hook::writeBranchLinkAtMainOffset(0x1CDCE4, setShineColor);
    hk::hook::writeBranchLinkAtMainOffset(0x1CDDCC, setShineColor);
    hk::hook::a64::assemble<"MOV X0, X19">().installAtMainOffset(0x1CDD2C);
    hk::hook::a64::assemble<"MOV X0, X19">().installAtMainOffset(0x1CDE14);
    hk::hook::writeBranchLinkAtMainOffset(0x1CDD3C, setShineModelColor);
    hk::hook::writeBranchLinkAtMainOffset(0x1CDE24, setShineModelColor);
    hk::hook::writeBranchLinkAtMainOffset(0x1CD94C, isPowerStarHook); // Make Moons into Stars
    hk::hook::writeBranchLinkAtMainOffset(0x1CDB94,
        isWorldPeachHook); // Overwrite default Star behavior

    hk::hook::writeBranchLinkAtMainOffset(0x1D2F08,
        onGrandShineStageChange); // Fixes multi moon soft lock exccept going to Odysseyless Cap
    hk::hook::writeBranchLinkAtMainOffset(0x52F71C, changeNextStage); // Scenario Tracking
    hk::hook::writeBranchLinkAtMainOffset(0x51DA40,
        changeNextStage); // Scenario updating via Odyssey
    // hk::hook::writeBranchLinkAtMainOffset(0x51d20c, tryFindLinkDestStageInfoOverride);  // ER
    // Stuff

    // Always active Odyssey
    // isUnlockedWorldHook.installAtSym<"_ZN16GameDataFunction15isUnlockedWorldE22GameDataHolderAccessori">();
    // isExistHomeHook.installAtSym<"_ZN16GameDataFunction11isExistHomeE22GameDataHolderAccessor">();
    // hk::hook::trampoline([]() -> bool { return true;
    // }).installAtSym<"_ZN16GameDataFunction11isExistHomeE22GameDataHolderAccessor">();
    hk::hook::writeBranchLinkAtMainOffset(0x309904, isUnlockWorldForHomeHook);
    hk::hook::writeBranchLinkAtMainOffset(0x30991c, isUnlockWorldForHomeHook);
    hk::hook::writeBranchLinkAtMainOffset(0x1f365c, isExistHomeHook);
    // Prevents shine deposit if world not unlocked
    hk::hook::writeBranchLinkAtMainOffset(0x30c37c, canDepositShinesHook);

    // Got shine replace
    setGotShineHook.installAtSym<"_ZN16GameDataFunction11setGotShineE20GameDataHolderWriterPK9ShineInfo">();

    // Grab Shine replace
    isGrabShineByShineInfoHook.installAtSym<"_ZN16GameDataFunction10isGotShineE22GameDataHolderAccessorPK9ShineInfo">();
    isGrabShineByHintInfoIdxHook.installAtSym<"_ZN16GameDataFunction10isGotShineE22GameDataHolderAccessori">();
    // isGrabShineByWorldIdHintIdxHook.installAtSym<"_ZN16GameDataFunction10isGotShineE22GameDataHolderAccessorii">();
    hk::hook::writeBranchLinkAtMainOffset(0x206E00, shineListShineCountHook); // Shine List
    // setShineCounterAndDenominatorHook.installAtSym<"_ZN2rs24setCounterAndDenominatorEPN2al11LayoutActorEii">();
    hk::hook::writeBranchLinkAtMainOffset(0x2045d0,
        setShineCounterAndDenominatorHook); // Shine List

    getUnlockShineNumHook.installAtSym<"_ZNK14GameDataHolder18findUnlockShineNumEPbi">(); // Change unlock requirements

    // Item Checks
    hk::hook::writeBranchLinkAtMainOffset(0x54CB9C, buyItemHook); // Caps
    hk::hook::writeBranchLinkAtMainOffset(0x54C604, buyItemHook); // Clothes
    hk::hook::writeBranchLinkAtMainOffset(0x54C3EC, buyItemHook); // Non typical Buy
    hk::hook::writeBranchLinkAtMainOffset(0x54C324, buyItemHook); // Buy in shop
    // Move below to branch Link to enforce game mode requirement
    hk::hook::writeBranchLinkAtMainOffset(0x318BF8,
        useCoinCollectHook); // Don't subtract regional coins

    //.installAtSym<"">();
    //.installAtSym<"_ZNK12GameDataFile30findUnlockShineNumCurrentWorldEPb">();

    // Grab Regional Coin Replace
    hk::hook::writeBranchLinkAtMainOffset(0x529B38, isGotCoinCollectHook);
    // Fix Regional Counter and Use in Shops
    hk::hook::writeBranchLinkAtMainOffset(0x5787D0,
        getCurrentWorldIdForCoinCollectHook); // Picture Font
    hk::hook::writeBranchLinkAtMainOffset(0x556588, getCurrentWorldIdForCoinCollectHook); // CoinCollectArchiveName
    hk::hook::writeBranchLinkAtMainOffset(0x5565E4, getCurrentWorldIdForCoinCollectHook); // CoinCollectEmptyArchiveName
    hk::hook::writeBranchLinkAtMainOffset(0x556640, getCurrentWorldIdForCoinCollectHook); // CoinCollect2DArchiveName
    hk::hook::writeBranchLinkAtMainOffset(0x55669C, getCurrentWorldIdForCoinCollectHook); // CoinCollect2DEmptyArchiveName
    getCoinCollectNumHook.installAtSym<"_ZN16GameDataFunction17getCoinCollectNumE22GameDataHolderAccessor">(); // Gets coin
                                                                                                               // collect num
                                                                                                               // using world id
                                                                                                               // relative to ER

    hk::hook::writeBranchLinkAtMainOffset(0x1C2bCC, getCoinCollectCheckGotNumHook); // Gets num of regional coin checks
                                                                                    // gotten for pop up on collect
    hk::hook::writeBranchLinkAtMainOffset(0x1C2BE4, getCoinCollectNumMaxHook); // Gets total regional coin count
                                                                               // for pop up on collect based on relative world

    // Instant Plant Growth QoL
    hk::hook::writeBranchLinkAtMainOffset(0x28f6d8, growOnPlant);

    // isGotCoinCollectHook.installAtSym<"_ZNK12GameDataFile16isGotCoinCollectEPKN2al11PlacementIdE">();
    // hk::hook::a64::assemble<"NOP">().installAtMainOffset(0x313334);

    // // ===== Talkatoo% mode hooks =====
    // // Three trampolines + one data-symbol lookup. All inert when
    // // ArchipelagoMode::getTalkatooMode() is false (toggled by the server
    // // side via setTalkatooMode). Symbols catalogued in syms/main.sym.

    // // Resolve Poetter's vtable address so the substitute hook can scope to
    // // Talkatoo callers only. A failure here leaves the trampoline installed
    // // but inert (substitute returns vanilla for every caller because
    // // actorIsPoetter returns false) — graceful degradation on a hypothetical
    // // future SMO patch that renames the class.
    // {
    //     const ptr vt = hk::ro::lookupSymbol("_ZTV7Poetter");
    //     if (vt == 0) {
    //         Logger::log("[talkatoo] lookupSymbol _ZTV7Poetter FAILED — substitute hook inert\n");
    //     } else {
    //         TalkatooHook::g_poetterVtableAddr = static_cast<uintptr_t>(vt);
    //         Logger::log("[talkatoo] Poetter vtable @ 0x%lx\n", static_cast<unsigned long>(vt));
    //     }
    // }
    // tryFindShineMessageHook.installAtSym<"_ZN16GameDataFunction19tryFindShineMessageEPKN2al9LiveActorEPKNS0_17IUseMessageSystemEii">();
    // isOpenShineNameHook.installAtSym<"_ZNK12GameDataFile15isOpenShineNameEii">();
    // tryUnlockShineNameHook.installAtSym<"_ZN12GameDataFile18tryUnlockShineNameEii">();

    // // ===== Cappy Messenger hooks =====
    // // Four trampolines on the per-mstxt message accessors (one System hook
    // // and one Stage hook for each of isExistLabel + getString). The hooks
    // // synthesize the kArchipelagoCappyLabel lookup against ArchipelagoMode's
    // // currently-live UTF-16 buffer. Inert unless enqueueCappyMessage has
    // // pushed at least one entry AND setCappyRsCalls below succeeded.
    // //
    // // CAVEAT: the existing shop-text BL replacements at 0x2089C4 / 0x208A44
    // // route through getShopItemMessage (which falls back to
    // // al::getSystemMessageString). The function-level trampoline below
    // // intercepts the fallback too — which is fine because non-Cappy labels
    // // pass through to Orig unchanged.
    // isExistLabelInSystemMessageHook.installAtSym<"_ZN2al27isExistLabelInSystemMessageEPKNS_17IUseMessageSystemEPKcS4_">();
    // getSystemMessageStringTrampolineHook.installAtSym<"_ZN2al22getSystemMessageStringEPKNS_17IUseMessageSystemEPKcS4_">();
    // isExistLabelInStageMessageHook.installAtSym<"_ZN2al26isExistLabelInStageMessageEPKNS_17IUseMessageSystemEPKcS4_">();
    // getStageMessageStringHook.installAtSym<"_ZN2al21getStageMessageStringEPKNS_17IUseMessageSystemEPKcS4_">();
    // hk::hook::writeBranchLinkAtMainOffset(0x3302F8, isExistCappyLabelInSystemMessageHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x3303E4, getSystemMessageCappyStringHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x330494, getStageMessageCappyStringHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x33049c, getSystemMessageCappyStringHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x330174, getSystemMessageCappyStringHook);
    // hk::hook::writeBranchLinkAtMainOffset(0x93a290, getMessageHolderHook);

    hk::hook::writeBranchLinkAtMainOffset(0x1dcb8c, isExistCappyLabelInSystemMessageHook);
    hk::hook::writeBranchLinkAtMainOffset(0x1dcbc4, getSystemMessageCappyStringHook);
    hk::hook::writeBranchLinkAtMainOffset(0x1dcb4c, isExistCappyLabelInStageMessageHook);
    hk::hook::writeBranchLinkAtMainOffset(0x1dcb84, getStageMessageCappyStringHook);

    // rs:: function-pointer wiring. tryPumpCappyMessage's dispatch path
    // skips when either pointer is null, so a lookup failure here leaves
    // the queue accumulating but never firing — visible as enqueueCappyMessage
    // logs without corresponding bubble dispatches.
    // {
    //     const ptr tryShow =
    //     hk::ro::lookupSymbol("_ZN2rs28tryShowCapMessagePriorityLowEPKN2al18IUseSceneObjHolderEPKcii");
    //     const ptr isActive =
    //     hk::ro::lookupSymbol("_ZN2rs18isActiveCapMessageEPKN2al18IUseSceneObjHolderE"); if
    //     (tryShow == 0 || isActive == 0) {
    //         Logger::log("[cappy] lookupSymbol failed tryShow=0x%lx isActive=0x%lx — pump
    //         disabled\n", static_cast<unsigned long>(tryShow),
    //                     static_cast<unsigned long>(isActive));
    //     } else {
    //         Logger::log("[cappy] tryShow @ 0x%lx isActive @ 0x%lx\n", static_cast<unsigned
    //         long>(tryShow), static_cast<unsigned long>(isActive));
    //         // setCappyRsCalls is static — safe to call at install time
    //         // before any ArchipelagoMode instance is created. The function
    //         // pointers live in class-static storage and are read by
    //         // tryPumpCappyMessage on every frame.
    //         ArchipelagoMode::setCappyRsCalls(reinterpret_cast<ArchipelagoMode::TryShowCapMessagePriorityLowFn>(tryShow),
    //                                          reinterpret_cast<ArchipelagoMode::IsActiveCapMessageFn>(isActive));
    //     }
    // }

    InstallAbilityLockHooks();
}