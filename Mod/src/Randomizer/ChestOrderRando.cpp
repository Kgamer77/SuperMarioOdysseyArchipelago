#include "Randomizer/ChestOrderRando.h"

#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"

#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameSystem.h"
#include "hk/hook/Trampoline.h"

// ── Chest Order Randomization ────────────────────────────────────────────────
//
// Three rooms in the game are "open the chests in the right order" puzzles:
// ForestWorldWoodsTreasureStage (3 chests), SeaWorldCoveCaveZone (4) and
// PeachWorldCostumeStage (8). Each is a TreasureBoxSequentialDirector linked to
// its TreasureBoxSequential boxes.
//
// The director builds mBoxes[] indexed by each box's authored "Number" arg, and
// that array index alone defines the required order: control() treats the box at
// index i as a correct open only once every box below i is already open. The
// reward moon is a Shine linked to one single box (vanilla: always the last one),
// which that box pops from exeOpenWait; the other boxes instead pop a small
// "PopUpNumber" layout showing their 1-based position.
//
// So a full shuffle needs three things kept in step: the mBoxes[] order, each
// box's popup number, and the Shine — which is moved (pointer plus world
// position, keeping its authored offset from its host box) onto whichever box
// ends up last. Otherwise the moon would pop mid-puzzle and the remaining chests
// would reward nothing.
//
// Neither the director nor the boxes are decompiled, so their fields are poked
// through raw offsets taken from the retail binary; see ida_context/MapObj/
// TreasureBoxSequential{,Director}.md.

namespace ChestOrderRando {
    namespace {

        // TreasureBoxSequential (size 0x148)
        constexpr s32 cBoxShine = 0x108; // Shine* from rs::tryInitLinkShine, null on most boxes
        constexpr s32 cBoxNumber = 0x124; // authored "Number" arg, minus 1
        constexpr s32 cBoxPopUpLayout = 0x138; // al::SimpleLayoutAppearWaitEnd* ("PopUpNumber")

        // TreasureBoxSequentialDirector
        constexpr s32 cDirectorBoxCount = 0x108;
        constexpr s32 cDirectorBoxes = 0x110; // TreasureBoxSequential*[], indexed by order

        // The largest authored room has 8 chests. The cap only guards the local
        // buffer below; a room past it is left vanilla rather than truncated.
        constexpr s32 cMaxBoxes = 16;

        al::LiveActor* getShine(al::LiveActor* box) {
            return *reinterpret_cast<al::LiveActor**>(reinterpret_cast<char*>(box) + cBoxShine);
        }

        void setShine(al::LiveActor* box, al::LiveActor* shine) {
            *reinterpret_cast<al::LiveActor**>(reinterpret_cast<char*>(box) + cBoxShine) = shine;
        }

        // Rewrites the box's own number and the "1/2/3..." it pops once opened.
        void setBoxOrder(al::LiveActor* box, s32 order) {
            *reinterpret_cast<s32*>(reinterpret_cast<char*>(box) + cBoxNumber) = order;

            void* layout = *reinterpret_cast<void**>(reinterpret_cast<char*>(box) + cBoxPopUpLayout);
            if (!layout)
                return;
            // The retail disassembly of TreasureBoxSequential::init passes
            // "(char*)popUpLayout + 8" here, but that +8 is not a member offset -
            // it is the compiler's own al::LayoutActor -> al::IUseLayout base
            // adjustment (IUseNerve's vptr sits at +0, IUseLayout's at +8).
            // Adding it by hand and then letting the compiler add it again lands
            // on the IUseLayoutAction subobject at +0x10, so setPaneStringLength
            // called getLayoutActionKeeper through the wrong vtable and crashed
            // in StageScene::init. Hand over the actor itself and let the
            // conversion happen once.
            al::setPaneStringFormat(reinterpret_cast<al::LayoutActor*>(layout), "TxtNumber", "%d",
                order + 1);
        }

        const char* getCurrentStageName() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;
            HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
            if (!seq->mGameDataHolderAccessor)
                return nullptr;
            return GameDataFunction::getCurrentStageName(seq->mGameDataHolderAccessor);
        }

        // The order has to survive leaving and re-entering the room, so it is
        // derived from the seed and the room alone - never from call order.
        u32 hashStage(const char* name) {
            u32 hash = 0x811C9DC5u;
            for (const char* c = name; c && *c; c++) {
                hash ^= static_cast<u8>(*c);
                hash *= 0x01000193u;
            }
            return hash;
        }

        void shuffleChestOrder(void* director) {
            if (!director || !Randomizer::isChestOrderRandoEnabled())
                return;

            // The room's own stage keys the shuffle, so without it there is no
            // order to apply.
            const char* stage = getCurrentStageName();
            if (!stage)
                return;

            char* base = static_cast<char*>(director);
            const s32 count = *reinterpret_cast<s32*>(base + cDirectorBoxCount);
            if (count < 2 || count > cMaxBoxes)
                return;

            al::LiveActor** boxes = *reinterpret_cast<al::LiveActor***>(base + cDirectorBoxes);
            if (!boxes)
                return;

            al::LiveActor* ordered[cMaxBoxes] = {};
            al::LiveActor* shine = nullptr;
            al::LiveActor* shineHost = nullptr;
            for (s32 i = 0; i < count; i++) {
                if (!boxes[i])
                    return;
                ordered[i] = boxes[i];
                if (al::LiveActor* boxShine = getShine(boxes[i])) {
                    // More than one reward in a room would mean this room is not
                    // shaped the way the shuffle below assumes; leave it vanilla.
                    if (shine)
                        return;
                    shine = boxShine;
                    shineHost = boxes[i];
                }
            }
            if (!shine)
                return;

            // The Shine sits at an authored offset above its host box (the box's
            // own init places it there). Preserve that offset when it moves.
            const sead::Vector3f shineOffset = al::getTrans(shine) - al::getTrans(shineHost);

            Randomizer::Rng rng = Randomizer::makeRng(
                Randomizer::getSeed() ^ hashStage(stage) ^ static_cast<u32>(count),
                Randomizer::RngStream::Chest);

            for (s32 i = count - 1; i > 0; i--) {
                const s32 j = static_cast<s32>(rng.nextModulo(static_cast<u32>(i + 1)));
                al::LiveActor* tmp = ordered[i];
                ordered[i] = ordered[j];
                ordered[j] = tmp;
            }

            for (s32 i = 0; i < count; i++) {
                boxes[i] = ordered[i];
                setBoxOrder(ordered[i], i);
            }

            al::LiveActor* newHost = ordered[count - 1];
            if (newHost != shineHost) {
                setShine(shineHost, nullptr);
                setShine(newHost, shine);
                al::setTrans(shine, al::getTrans(newHost) + shineOffset);
            }
        }

        HkTrampoline<void, void*, const al::ActorInitInfo&> treasureBoxSequentialDirectorInitHook
            = hk::hook::trampoline([](void* director, const al::ActorInitInfo& initInfo) -> void {
                  // Run after the vanilla init: it is what creates the boxes, fills
                  // mBoxes[] and links the Shine, all of which the shuffle rewrites.
                  treasureBoxSequentialDirectorInitHook.orig(director, initInfo);
                  shuffleChestOrder(director);
              });

    } // namespace

    void installHooks() {
        // TreasureBoxSequentialDirector::init(al::ActorInitInfo const&). Not in
        // syms/, so it is installed by main-module offset.
        treasureBoxSequentialDirectorInitHook.installAtMainOffset(0x33fea4);
    }

} // namespace ChestOrderRando
