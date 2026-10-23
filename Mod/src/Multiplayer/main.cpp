/**
 * @file main.cpp
 * @brief Core game hooks and main functionality for the multiplayer client
 */

#include "Multiplayer/main.hpp"

#include "hk/gfx/ImGuiBackendNvn.h"

#include "sead/gfx/seadColor.h"
#include "sead/prim/seadSafeString.h"
#include <sead/gfx/seadCamera.h>
#include <sead/gfx/seadPrimitiveRenderer.h>
#include <sead/gfx/seadProjection.h>
#include <sead/heap/seadHeap.h>
#include <sead/prim/seadStringUtil.h>

#include "al/Library/Bgm/BgmLineFunction.h"
#include "al/Library/Camera/CameraUtil.h"
#include "al/Library/Controller/PadRumbleDirector.h"
#include "al/Library/Controller/PadRumbleFunction.h"
#include "al/Library/Framework/GameFrameworkNx.h"
#include "al/Library/LiveActor/ActorActionFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorPoseKeeper.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Player/PlayerUtil.h"
#include "al/Library/Scene/SceneUtil.h"
#include "al/Library/Screen/ScreenFunction.h"
#include "al/Library/System/GameSystemInfo.h"

#include "agl/common/aglDrawContext.h" // IWYU pragma: keep

#include "game/Item/ShineInfo.h"
#include "game/Player/HackCap.h"
#include "game/Player/PlayerActorBase.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerAnimFrameCtrl.h"
#include "game/Player/PlayerAnimator.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Scene/StageScene.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/Application.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameSystem.h"
#include "game/Util/AchievementUtil.h"

#include "Imgui.hpp"
#include "Multiplayer/actors/PuppetActor.h"
#include "Multiplayer/layouts/PlayerEventLog.h"
#include "Multiplayer/puppets/PuppetInfo.h"
#include "Multiplayer/server/Client.hpp"
#include "Multiplayer/server/DeltaTime.hpp"
#include "imgui.h"

// ===== PLAYER INFO UPDATE FUNCTION =====

// INVARIANT: one HkTrampoline per game function, process-wide.
//
// hakkun copies a hooked function's first instruction into the trampoline slot
// without relocating it (see the TODO in hk/hook/Trampoline.h). A second
// trampoline on an already-hooked function therefore copies the first hook's
// PC-relative B, which retargets when executed from the pool -- orig() becomes
// a branch into unmapped memory, and the first hook is never reached either.
//
// This bit five functions the Randomizer already owns (al::initActorInitInfo,
// Shine::initAfterPlacement, GameDataFunction::setGotShine,
// GameDataFile::addCoinCollect, CheckpointFlag::exeGet). Multiplayer now rides
// along on those through the mpOn* bridge in Multiplayer/main.hpp. Before adding
// a hook here, check the target is not already installAtSym'd elsewhere.
void installMultiplayerHooks() {
    installSyncHooks();
    installInitHooks();
    installOtherHooks();

    hk::gfx::ImGuiBackendNvn::instance()->installHooks(false);
}
