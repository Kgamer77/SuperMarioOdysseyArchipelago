#include "Randomizer/Nuzlocke.h"

#include "Randomizer/AbilityOverlay.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/HintSystem.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/KingdomRando.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/PurpleCoinRewards.h"
#include "Randomizer/RandoSaveFile.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/StoryProgressHooks.h"
#include "Randomizer/TimerMode.h"
#include "Randomizer/TrackerOverlay.h"
#include "Randomizer/WorldMeta.h"
#include "SD/SDHooks.h"
#include "ShineDB.h"
#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/HitReactionKeeper.h"
#include "al/Library/Nerve/Nerve.h"
#include "al/Library/Nerve/NerveKeeper.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"
#include "al/Library/Se/SeFunction.h"
#include "game/Item/ShineInfo.h"
#include "game/Layout/CounterLife.h"
#include "game/Layout/CounterLifeCtrl.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/Sequence/HakoniwaStateBootLoadData.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/PlayerHitPointData.h"
#include "game/System/SaveDataAccessFunction.h"
#include "game/System/WorldList.h"

#include "nn/os.h"
#include "nn/util.h"

#include <cstddef>
#include <cstdlib>
#include <cstring>

namespace Nuzlocke {
    namespace {
        constexpr const char* cCaptureLocksPrefix = "NuzSoftCaps:";
        constexpr const char* cCaptureRetainedPrefix = "NuzSoftCapKeep:";
        constexpr const char* cAbilityLocksPrefix = "NuzSoftAbilities:";
        constexpr const char* cAbilityRetainedPrefix = "NuzSoftAbilityKeep:";
        constexpr const char* cPaintingLocksPrefix = "NuzSoftPaintings:";
        constexpr const char* cPaintingRetainedPrefix = "NuzSoftPaintingKeep:";
        // Per-kingdom carried moon counts, one fixed-width hex field per world in
        // logical world-id order.  Fixed width rather than a separated list so the
        // record has one length and the parse cannot resynchronise wrongly on a
        // truncated field; three digits leaves an enormous margin over the ~100
        // moons a single kingdom can hold even under triple redistribution.
        constexpr const char* cCarriedMoonsPrefix = "NuzSoftCarry:";
        constexpr s32 cCarryDigits = 3;
        constexpr s32 cCarryMax = 0xfff;
        // customAddHackDictionary silently truncates into FixedSafeString<128>,
        // which would corrupt the tail fields rather than fail loudly.
        static_assert(sizeof("NuzSoftCarry:") + WorldMeta::sWorldCount * cCarryDigits <= 128,
            "Carried-moon record must fit a hack dictionary entry.");
        constexpr s32 cMaxCaptures = 64;
        constexpr s32 cMaxAbilities = 32;
        constexpr s32 cMaxPaintings = 16;
        // Purple Shop moons are one offer per kingdom and cost a kingdom's worth
        // of regional coins apiece, so a run buys a handful at most; the ledger
        // record itself runs out of characters at roughly this many anyway.
        constexpr s32 cMaxPurchasedMoons = 32;
        constexpr s32 cWorldCount = WorldMeta::sWorldCount;
        constexpr ptrdiff_t cCancelWorldResourceOffset = 0x005143ec;
        constexpr ptrdiff_t cDestroyWorldResourceOffset = 0x005143fc;
        constexpr ptrdiff_t cRequestCaptureRecursiveOffset = 0x008b46c8;
        constexpr ptrdiff_t cExeLoadWorldResourceOffset = 0x0050f3c4;
        constexpr ptrdiff_t cStartFreezeActionOffset = 0x008b0bc8;
        constexpr ptrdiff_t cGetActionFrameMaxOffset = 0x008b0ce0;
        constexpr ptrdiff_t cTryStopSeOffset = 0x009e0eac;
        constexpr ptrdiff_t cHitReactionKeeperStartOffset = 0x0090c404;
        // nrvHakoniwaSequenceBootLoadData - the nerve HakoniwaSequence::init binds
        // mStateBootLoadData to, and the one exeWaitLoadData returns to the title
        // with.  Data, not code, so it is resolved as a plain pointer.
        constexpr ptrdiff_t cBootLoadDataNerveOffset = 0x01d977e0;
        // ~2s of retries before giving up on deleting the slot.
        constexpr s32 cDeleteRetryFrames = 120;

        template <typename Fn>
        Fn mainOffsetFn(ptrdiff_t offset) {
            return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
        }

        const al::Nerve* mainOffsetNerve(ptrdiff_t offset) {
            return reinterpret_cast<const al::Nerve*>(
                hk::ro::getMainModule()->range().start() + offset);
        }

        void startFreezeActionAtMainOffset(al::IUseLayoutAction* layout,
            const char* action, f32 frame, const char* pane) {
            mainOffsetFn<void (*)(al::IUseLayoutAction*, const char*, f32, const char*)>(
                cStartFreezeActionOffset)(layout, action, frame, pane);
        }

        f32 getActionFrameMaxAtMainOffset(const al::IUseLayoutAction* layout,
            const char* action, const char* pane) {
            return mainOffsetFn<f32 (*)(const al::IUseLayoutAction*, const char*, const char*)>(
                cGetActionFrameMaxOffset)(layout, action, pane);
        }

        void stopLifeAlertAtMainOffset(const al::IUseAudioKeeper* keeper) {
            if (!keeper)
                return;
            using TryStopSeFn = bool (*)(const al::IUseAudioKeeper*, const sead::SafeString&, s32,
                const char*);
            mainOffsetFn<TryStopSeFn>(cTryStopSeOffset)(keeper, "LifeAlert", -1, nullptr);
        }

        bool isExtremeNuzlockeActive() {
            return Randomizer::isNuzlockeRunMode()
                && Randomizer::getExtremeNuzlockeEnabled();
        }

        static HkTrampoline<void, const al::IUseAudioKeeper*, const sead::SafeString&>
            startSeHook = hk::hook::trampoline(
                [](const al::IUseAudioKeeper* keeper, const sead::SafeString& sound) -> void {
                    // CounterLife's warning reaction ultimately starts LifeAlert through
                    // al::startSe. Filter the sound itself so alternate warning owners or
                    // reaction names cannot reintroduce the alert in Extreme mode.
                    if (isExtremeNuzlockeActive() && sound.cstr()
                        && std::strcmp(sound.cstr(), "LifeAlert") == 0)
                        return;
                    startSeHook.orig(keeper, sound);
                });

        void forceExtremeLifeMeter(CounterLife* counter) {
            if (!counter || counter->mIsCounterUp)
                return;

            // CounterLife's Gauge action is authored as a three-health meter:
            // the one-health frame is two thirds of the action's range.  Keep
            // this on Gauge rather than Loop; Loop is the low-health warning
            // animation for the centre heart.
            const f32 gaugeMax = getActionFrameMaxAtMainOffset(counter, "Gauge", "Gauge");
            const f32 gaugeFrame = gaugeMax * (2.0f / 3.0f);
            // Also settle CounterLife's internal interpolation endpoints.  A
            // count animation can otherwise write its old frame back on the
            // following update after this action has been frozen.
            counter->mCurFrame = gaugeFrame;
            counter->mGoalFrame = gaugeFrame;
            startFreezeActionAtMainOffset(counter, "Gauge", gaugeFrame, "Gauge");

            // exeWait starts Loop whenever the native current health is one.
            // Freeze it at its first (white-heart) frame so Extreme mode does
            // not borrow the warning colour or animation.
            startFreezeActionAtMainOffset(counter, "Loop", 0.0f, "Loop");

            // The one-life hit reaction starts this alert on the normal meter.
            // Extreme mode is already permanently at one effective HP, so the
            // alert must not loop or play when the meter is refreshed.
            stopLifeAlertAtMainOffset(counter);
        }

        bool isExtremeLifeWarningReaction(const char* reaction) {
            static constexpr char cLifeWarning[] = "\xE3\x83\xA9\xE3\x82\xA4\xE3\x83\x95"
                                                   "1"
                                                   "\xE8\xAD\xA6\xE5\x91\x8A";
            return reaction && std::strcmp(reaction, cLifeWarning) == 0;
        }

        static HkTrampoline<void, const al::LayoutActor*, const char*, const char*>
            startHitReactionHook = hk::hook::trampoline(
                [](const al::LayoutActor* layout, const char* reaction, const char* pane) -> void {
                    // CounterLife uses this reaction to start the one-life warning
                    // (including LifeAlert). Extreme mode keeps a static one-life
                    // gauge instead, so suppress only that reaction.
                    if (isExtremeNuzlockeActive() && isExtremeLifeWarningReaction(reaction))
                        return;
                    startHitReactionHook.orig(layout, reaction, pane);
                });

        // Some CounterLife paths reach the keeper directly, and the keeper is
        // where the reaction's LifeAlert sound is actually started.  Filter the
        // named warning here as well as at startHitReaction so a refresh or a
        // cached layout cannot reintroduce the low-health noise.
        static HkTrampoline<void, al::HitReactionKeeper*, const char*, const sead::Vector3f*,
            const al::HitSensor*, const al::HitSensor*>
            hitReactionKeeperStartHook = hk::hook::trampoline(
                [](al::HitReactionKeeper* keeper, const char* reaction,
                    const sead::Vector3f* position, const al::HitSensor* target,
                    const al::HitSensor* source) -> void {
                    if (isExtremeNuzlockeActive() && isExtremeLifeWarningReaction(reaction))
                        return;
                    hitReactionKeeperStartHook.orig(keeper, reaction, position, target, source);
                });

        static HkTrampoline<void, CounterLife*> counterLifeWaitHook = hk::hook::trampoline([](CounterLife* counter) -> void {
            counterLifeWaitHook.orig(counter);
            if (isExtremeNuzlockeActive())
                forceExtremeLifeMeter(counter);
        });

        static HkTrampoline<void, CounterLifeCtrl*, s32> counterLifeStartCountAnimHook = hk::hook::trampoline([](CounterLifeCtrl* ctrl, s32 target) -> void {
            counterLifeStartCountAnimHook.orig(ctrl, target);
            if (!isExtremeNuzlockeActive() || !ctrl)
                return;
            forceExtremeLifeMeter(ctrl->mCounterLife);
            forceExtremeLifeMeter(ctrl->mCounterLifeKids);
        });

        // Shared by Extreme Nuzlocke and Timer Mode's damage penalty: only one
        // trampoline may own a symbol, so both live in this one hook and each
        // half checks its own run mode.
        static HkTrampoline<void, PlayerHitPointData*> playerHitPointDamageHook = hk::hook::trampoline([](PlayerHitPointData* data) -> void {
            if (data && isExtremeNuzlockeActive())
                data->mCurrentHealth = 1;
            playerHitPointDamageHook.orig(data);
            // Retail only reaches PlayerHitPointData::damage through
            // PlayerDamageKeeper, so this is one genuine hit on Mario. Void
            // falls, poison, lava and drowning take ::kill instead and are
            // therefore excluded, exactly as the penalty is specified.
            TimerMode::notifyPlayerDamage();
        });

        static HkTrampoline<s32, const PlayerHitPointData*> playerHitPointCurrentHook = hk::hook::trampoline([](const PlayerHitPointData* data) -> s32 {
            const s32 current = playerHitPointCurrentHook.orig(data);
            return isExtremeNuzlockeActive() && current > 0 ? 1 : current;
        });

        static HkTrampoline<s32, const PlayerHitPointData*> playerHitPointMaxCurrentHook = hk::hook::trampoline([](const PlayerHitPointData* data) -> s32 {
            if (isExtremeNuzlockeActive())
                return 1;
            return playerHitPointMaxCurrentHook.orig(data);
        });

        // The life-meter demo also asks for the item-adjusted and item-free
        // maxima directly, bypassing getMaxCurrent. Keep those queries at one
        // as well so a Life-Up Heart cannot briefly present a larger meter.
        static HkTrampoline<s32, const PlayerHitPointData*> playerHitPointMaxWithItemHook = hk::hook::trampoline([](const PlayerHitPointData* data) -> s32 {
            if (isExtremeNuzlockeActive())
                return 1;
            return playerHitPointMaxWithItemHook.orig(data);
        });

        static HkTrampoline<s32, const PlayerHitPointData*> playerHitPointMaxWithoutItemHook = hk::hook::trampoline([](const PlayerHitPointData* data) -> s32 {
            if (isExtremeNuzlockeActive())
                return 1;
            return playerHitPointMaxWithoutItemHook.orig(data);
        });

        void cancelWorldResource(WorldResourceLoader* loader) {
            if (loader)
                mainOffsetFn<void (*)(WorldResourceLoader*)>(cCancelWorldResourceOffset)(loader);
        }

        void destroyWorldResource(WorldResourceLoader* loader) {
            if (loader)
                mainOffsetFn<void (*)(WorldResourceLoader*)>(cDestroyWorldResourceOffset)(loader);
        }

        void requestCounterCapture(const al::LayoutActor* layout) {
            if (layout) {
                mainOffsetFn<void (*)(const al::LayoutActor*)>(cRequestCaptureRecursiveOffset)(layout);
            }
        }

        void exeNativeLoadWorldResource(HakoniwaSequence* sequence) {
            mainOffsetFn<void (*)(HakoniwaSequence*)>(cExeLoadWorldResourceOffset)(sequence);
        }

        enum class DeathPhase : u8
        {
            Idle,
            CounterAppear,
            CounterSubtract,
            CounterEnd,
            MoonPanelIn,
            MoonPanelHold,
            MoonPanelDrain,
            MoonPanelSettle,
            MoonPanelOut,
            WaitForResourceStop,
            StartSoftWorker,
            WaitSoftWorker,
            CommitSoft,
            DeleteHardSlot,
            ErrorRetry,
        };

        // Frame budgets for the death screen.  The whole sequence deliberately
        // runs slower than vanilla's ten-coin deduction: it is reporting a run
        // penalty, not a scratch, and the moon panel needs long enough to read.
        constexpr s32 cCounterAppearFrames = 50;
        constexpr s32 cCoinDrainMinFrames = 40;
        constexpr s32 cCoinDrainMaxFrames = 100;
        constexpr s32 cCounterEndFrames = 45;
        constexpr s32 cPanelFadeFrames = 24;
        constexpr s32 cPanelHoldFrames = 45;
        constexpr s32 cRowPulseFrames = 14;
        constexpr s32 cRowDrainMinFrames = 26;
        constexpr s32 cRowDrainMaxFrames = 70;
        constexpr s32 cRowSettleFrames = 22;
        constexpr s32 cPanelFinalHoldFrames = 60;
        // Frames one lost-item entry takes to travel one row of the ticker.
        constexpr s32 cLostScrollFrames = 45;

        struct SoftCarry {
            GameDataFile* file = nullptr;
            GameDataHolder* holder = nullptr;
            SeedGenerator::SoftSeedRequest request = {};
            s16 lostCaptures[cMaxCaptures] = {};
            s16 lostAbilities[cMaxAbilities] = {};
            s16 lostPaintings[cMaxPaintings] = {};
            s32 lostCaptureCount = 0;
            s32 lostAbilityCount = 0;
            s32 lostPaintingCount = 0;
            // The opener the run was started with - every piece of it, cap throw
            // included.  It is surrendered wholesale and replaced by whatever the
            // new seed picks, so it has to be snapshotted before the worker
            // rebuilds the tables under us.  Sized to the whole pool rather than
            // to today's Plentiful targets so raising those can never silently
            // leave part of an opener behind.
            s16 oldStartingCaptures[cMaxCaptures] = {};
            s16 oldStartingAbilities[cMaxAbilities] = {};
            s32 oldStartingCaptureCount = 0;
            s32 oldStartingAbilityCount = 0;
            // Rewards bought with regional coins.  The reroll resets the coin
            // economy, so these are revoked outright instead of entering the
            // retain lottery with the run's earned unlocks.
            u64 purchasedAbilities = 0;
            u64 purchasedCaptures = 0;
            s32 beforeMoons[cWorldCount] = {};
            s32 afterMoons[cWorldCount] = {};
            // The whole of what a run carries through a reroll: how many moons
            // each kingdom keeps.  There are no such moons - the new seed's board
            // starts empty - so this count is what gets persisted and added back
            // on top of it.  See Nuzlocke::getCarriedMoons.
            s32 keepMoons[cWorldCount] = {};
        };

        struct DeathState {
            bool pending = false;
            bool soft = false;
            // Set when the run ended on a Timer Mode timeout rather than a
            // Nuzlocke death.  Only the panel's headline differs; the coordinator
            // treats it as a hard death throughout.
            bool timeout = false;
            bool returningToTitle = false;
            s32 slot = -1;
            u32 seed = 0;
            GameDataFile* file = nullptr;
            s32 coinsBefore = 0;
            s32 counterStart = 0;
            s32 counterTarget = 0;
            s32 counterDuration = 0;
            s32 counterFrame = 0;
            s32 rowDuration = 0;
            DeathPhase phase = DeathPhase::Idle;
            SoftCarry softCarry = {};
        };

        DeathState sDeath;
        OverlayState sOverlay;

        class NuzlockeDeathNerve final : public al::Nerve {
        public:
            void execute(al::NerveKeeper* keeper) const override {
                updateDeathSequence(keeper->getParent<HakoniwaSequence>());
            }
        };

        class NuzlockeLoadWorldResourceNerve final : public al::Nerve {
        public:
            void execute(al::NerveKeeper* keeper) const override {
                updateLoadWorldResource(keeper->getParent<HakoniwaSequence>());
            }
        };

        NuzlockeDeathNerve sNuzlockeDeathNerve;
        NuzlockeLoadWorldResourceNerve sNuzlockeLoadWorldResourceNerve;

        u64 getLockMask(const GameDataFile* file, const char* prefix) {
            if (!file)
                return 0;
            const char* value = file->customFindHackDictionaryPrefix(prefix);
            return value ? std::strtoull(value + std::strlen(prefix), nullptr, 16) : 0;
        }

        void setLockMask(GameDataFile* file, const char* prefix, u64 mask) {
            if (!file)
                return;
            file->customRemoveHackDictionaryPrefix(prefix);
            if (!mask)
                return;
            char record[80];
            nn::util::SNPrintf(record, sizeof(record), "%s%llx", prefix,
                static_cast<unsigned long long>(mask));
            file->customAddHackDictionary(record);
        }

        // Read on every getShineNum, so it is parsed once per file rather than
        // once per call - the same lazy-by-file-pointer arrangement
        // KingdomProgress::get() uses, and dropped at the same points.
        const GameDataFile* sCarryFile = nullptr;
        s32 sCarryMoons[cWorldCount] = {};
        bool sCarryLoaded = false;

        s32 hexDigitValue(char c) {
            if (c >= '0' && c <= '9')
                return c - '0';
            if (c >= 'a' && c <= 'f')
                return c - 'a' + 10;
            if (c >= 'A' && c <= 'F')
                return c - 'A' + 10;
            return -1;
        }

        void loadCarriedMoons(const GameDataFile* file) {
            sCarryFile = file;
            sCarryLoaded = true;
            for (s32 i = 0; i < cWorldCount; i++)
                sCarryMoons[i] = 0;
            if (!file)
                return;

            const char* record = file->customFindHackDictionaryPrefix(cCarriedMoonsPrefix);
            if (!record)
                return;

            const char* cursor = record + std::strlen(cCarriedMoonsPrefix);
            for (s32 world = 0; world < cWorldCount; world++) {
                s32 value = 0;
                for (s32 digit = 0; digit < cCarryDigits; digit++) {
                    const s32 nibble = hexDigitValue(cursor[digit]);
                    // A short or malformed record is not worth guessing at: keep
                    // what parsed cleanly and leave the rest at zero.
                    if (nibble < 0)
                        return;
                    value = value * 16 + nibble;
                }
                cursor += cCarryDigits;
                sCarryMoons[world] = value;
            }
        }

        void storeCarriedMoons(GameDataFile* file, const s32* counts) {
            if (!file)
                return;

            file->customRemoveHackDictionaryPrefix(cCarriedMoonsPrefix);
            sCarryFile = file;
            sCarryLoaded = true;

            bool any = false;
            for (s32 world = 0; world < cWorldCount; world++) {
                s32 value = counts[world];
                if (value < 0)
                    value = 0;
                if (value > cCarryMax)
                    value = cCarryMax;
                sCarryMoons[world] = value;
                any = any || value > 0;
            }
            // Carrying nothing leaves no record at all, so a run that dies with an
            // empty board reads exactly like one that never died.
            if (!any)
                return;

            char record[128];
            const size_t prefixLen = std::strlen(cCarriedMoonsPrefix);
            std::memcpy(record, cCarriedMoonsPrefix, prefixLen);
            size_t at = prefixLen;
            for (s32 world = 0; world < cWorldCount; world++) {
                for (s32 shift = (cCarryDigits - 1) * 4; shift >= 0; shift -= 4)
                    record[at++] = "0123456789abcdef"[(sCarryMoons[world] >> shift) & 0xf];
            }
            record[at] = '\0';
            file->customAddHackDictionary(record);
        }

        bool isSoftMode() {
            return Randomizer::isSoftNuzlockeRunMode();
        }

        bool isPurchasedAbility(const SoftCarry& carry, s32 abilityId) {
            return abilityId >= 0 && abilityId < 64
                && (carry.purchasedAbilities & (1ull << abilityId));
        }

        bool isPurchasedCapture(const SoftCarry& carry, s32 captureIndex) {
            return captureIndex >= 0 && captureIndex < 64
                && (carry.purchasedCaptures & (1ull << captureIndex));
        }

        s32 getCaptureIndex(const CaptureRando::CaptureEntry* target) {
            if (!target)
                return -1;
            for (s32 i = 0; i < CaptureRando::getCaptureCount(); i++) {
                const auto* capture = CaptureRando::getCapture(i);
                if (capture == target || (capture && std::strcmp(capture->hackName, target->hackName) == 0))
                    return i;
            }
            return -1;
        }

        bool isStartingCapture(const CaptureRando::CaptureEntry& target) {
            for (s32 i = 0; i < CaptureRando::getStartingCaptureCount(); i++) {
                const auto* capture = CaptureRando::getStartingCapture(i);
                if (capture && std::strcmp(capture->hackName, target.hackName) == 0)
                    return true;
            }
            return false;
        }

        bool isStartingAbility(const AbilityRando::AbilityEntry& target) {
            const auto* capThrow = AbilityRando::getStartingCapThrow();
            if (capThrow && capThrow->id == target.id)
                return true;
            for (s32 i = 0; i < AbilityRando::getStartingAbilityCount(); i++) {
                const auto* ability = AbilityRando::getStartingAbility(i);
                if (ability && ability->id == target.id)
                    return true;
            }
            return false;
        }

        u32 makeFreshSeed() {
            static u32 salt = 0x4e555a53; // "NUZS"
            const u64 tick = nn::os::GetSystemTick().value;
            salt = salt * 1664525u + 1013904223u + static_cast<u32>(tick);
            const u32 seed = static_cast<u32>(tick >> 1) ^ Randomizer::getSeed() ^ salt;
            return seed ? seed : 1;
        }

        // Every percentage penalty a death levies rounds *up*, and all of them go
        // through here.  Rounding down let a dial come out to nothing on the small
        // holdings it matters most on - three moons in a kingdom at 25%, or a
        // single ability at anything under 100% - so a run could die and surrender
        // none of a category it plainly held.  Rounding up makes any non-zero dial
        // cost any non-zero holding at least one.
        s32 lossCount(s32 held, s32 percent) {
            if (held <= 0 || percent <= 0)
                return 0;
            if (percent >= 100)
                return held;
            return (held * percent + 99) / 100;
        }

        template <typename T>
        void chooseAndRemove(T* values, s32& count, s32 removeCount, Randomizer::Rng& rng,
            T* removed, s32& removedCount) {
            while (removeCount-- > 0 && count > 0) {
                const s32 index = static_cast<s32>(rng.nextModulo(static_cast<u32>(count)));
                removed[removedCount++] = values[index];
                values[index] = values[--count];
            }
        }

        // Abilities cannot be drawn from flatly, because they come in progressive
        // chains: Triple Jump stands on Double Jump, Backflip on Crouch.
        // AbilityRando::isAbilityUnlocked walks the prerequisite chain and refuses
        // an ability whose foundation is missing, so taking Double Jump while
        // leaving Triple Jump does not spare Triple Jump - it kills it silently,
        // and the run loses two moves for the price of one.
        //
        // Losses therefore come off the tip. Only an ability that nothing still
        // held depends on is eligible for the next draw, which lets Triple Jump go
        // while Double Jump stays, never the reverse. Both can still be lost - the
        // draw that takes Triple Jump is what makes Double Jump a tip for the next
        // one - so this constrains the order, not the amount.
        void chooseAndRemoveAbilities(const AbilityRando::AbilityEntry** owned, s32& count,
            s32 removeCount, Randomizer::Rng& rng,
            const AbilityRando::AbilityEntry** removed, s32& removedCount) {
            while (removeCount-- > 0 && count > 0) {
                s32 tips[cMaxAbilities];
                s32 tipCount = 0;
                for (s32 i = 0; i < count; i++) {
                    bool supportsAnotherHeldAbility = false;
                    for (s32 j = 0; j < count && !supportsAnotherHeldAbility; j++)
                        supportsAnotherHeldAbility = owned[j]->prerequisite == static_cast<s32>(owned[i]->id);
                    if (!supportsAnotherHeldAbility)
                        tips[tipCount++] = i;
                }
                // Every remaining ability props up another one, which the table
                // cannot express without a cycle. Stop rather than pick blind.
                if (tipCount == 0)
                    break;

                const s32 index = tips[static_cast<s32>(rng.nextModulo(static_cast<u32>(tipCount)))];
                removed[removedCount++] = owned[index];
                owned[index] = owned[--count];
            }
        }

        // The reroll does NOT carry the opener over.  Pinning it was tried and
        // dropped: MoonRando::rebuild clears the logic-starting override before
        // every placement attempt precisely so chooseLogicStartingEquipment can
        // rescue a tight seed, and holding the old opener in front of that both
        // constrains placement and caps how much the rescue pass can add. The old
        // opener is surrendered with everything else instead, and the new seed
        // assigns a fresh one, which keeps generation as solvable as a new file.
        void snapshotOldStartingEquipment(SoftCarry* carry) {
            for (s32 i = 0; i < CaptureRando::getStartingCaptureCount()
                 && carry->oldStartingCaptureCount < cMaxCaptures;
                 i++) {
                const s32 index = getCaptureIndex(CaptureRando::getStartingCapture(i));
                if (index >= 0)
                    carry->oldStartingCaptures[carry->oldStartingCaptureCount++] = static_cast<s16>(index);
            }

            if (const auto* capThrow = AbilityRando::getStartingCapThrow())
                carry->oldStartingAbilities[carry->oldStartingAbilityCount++] = static_cast<s16>(capThrow->id);
            for (s32 i = 0; i < AbilityRando::getStartingAbilityCount()
                 && carry->oldStartingAbilityCount < cMaxAbilities;
                 i++) {
                if (const auto* ability = AbilityRando::getStartingAbility(i))
                    carry->oldStartingAbilities[carry->oldStartingAbilityCount++] = static_cast<s16>(ability->id);
            }
            Randomizer::logPush(
                "Soft Nuzlocke: surrendering old start abilities=%d captures=%d",
                carry->oldStartingAbilityCount, carry->oldStartingCaptureCount);
        }

        void reconcileVanillaStoryProgress(GameDataFile* file) {
            if (!file || Randomizer::isMoonRandoEnabled())
                return;
            auto& scenarios = file->getMainScenarioNumArr();
            for (s32 i = 0; i < 1024; i++) {
                const auto& hint = file->getHintList()[i];
                if (!hint.isGet || hint.worldId < 0 || hint.worldId >= 16 || hint.mainScenarioNo < 0)
                    continue;
                const s32 minimum = hint.mainScenarioNo + 1;
                if (scenarios[hint.worldId] < minimum)
                    scenarios[hint.worldId] = minimum;
            }
        }

        void prepareMoonCounterTargets(SoftCarry* carry) {
            // The panel counts down to exactly what the kingdom will carry.
            for (s32 world = 0; world < cWorldCount; world++) {
                carry->afterMoons[world] = carry->keepMoons[world];
                if (carry->afterMoons[world] > carry->beforeMoons[world])
                    carry->afterMoons[world] = carry->beforeMoons[world];
            }
        }

        // A moon being held is written down twice, and only one of the two was
        // ever being cleared.
        //
        // HintInfo::isGet is the record the moon list draws from and the one
        // customRebuildShineCounters() sums into mShineNum.  The acquisition
        // GameDataFile::isGotShine(const ShineInfo*) answers from is a separate
        // pair of lists, mGotShine and mGotGrandShine, and that is the one
        // gameplay consults - including MoonHooks::isMoonAcquiredDirect, and so
        // every per-kingdom total the tracker shows.  The native setGotShine
        // writes both, which is why nothing else in the game has to think about
        // it; a reroll editing only the hint list left the dead run's moons
        // sitting in the acquisition lists, still held, while the moon list
        // showed the newly drawn set.  Two readers, two answers, and a moon the
        // player never collected in this seed.
        //
        // This is the full clear.  Both lists, and the latest-get pointer that
        // indexes into them.
        void clearAllCollectedMoons(GameDataFile* file) {
            if (!file)
                return;

            file->customClearAllShineCollected();

            // Every slot, not just the ones before the terminator, which is what
            // GameDataFile::initializeData does to these same two arrays - so all
            // 1024 pointers are known to be real ShineInfos and none of this is
            // reading into the unpopulated.  A stop-at-first-empty sweep would be
            // enough today, since the append fills the first empty slot and
            // nothing ever removes one, but it would quietly depend on that list
            // never developing a hole.
            auto emptyList = [](auto& list) {
                for (s32 i = 0; i < list.size(); i++) {
                    if (ShineInfo* shine = list[i])
                        shine->clear();
                }
            };
            emptyList(file->getGotShineList());
            emptyList(file->getGotGrandShineList());
            if (ShineInfo* latest = file->customGetLatestGetShineInfo())
                latest->clear();
        }

        bool prepareSoftPenalty(GameDataFile* file) {
            if (!file || !file->getGameDataHolder())
                return false;

            SoftCarry& carry = sDeath.softCarry;
            carry = {};
            carry.file = file;
            carry.holder = file->getGameDataHolder();
            carry.request.slot = SaveRedirect::getActiveSlot();
            carry.request.oldSeed = Randomizer::getSeed();
            carry.request.requestedSeed = makeFreshSeed();
            if (carry.request.slot < 1 || !carry.request.requestedSeed)
                return false;

            // getShineNum already includes whatever an earlier death carried
            // into this run, so a second death levies its percentage on the
            // run's whole standing rather than only on the moons found since.
            for (s32 world = 0; world < cWorldCount; world++)
                carry.beforeMoons[world] = file->getShineNum(world);

            carry.purchasedAbilities = PurpleCoinRewards::getPurchasedAbilityMask(file);
            carry.purchasedCaptures = PurpleCoinRewards::getPurchasedCaptureMask(file);

            Randomizer::Rng rng(carry.request.oldSeed ^ carry.request.requestedSeed);

            // A reroll cannot decide *which* moons survive, only how many, and it
            // does not hand any of them back.  Collection flags are keyed by the
            // *item*, and the new seed re-places every item, so a flag carried
            // across generation reappears under whatever location that item lands
            // on next: a Seaside moon kept here came back as Cascade's collected
            // story triple and floated the kingdom to world peace.  Nothing about
            // an item's identity means the same thing on both sides of a reseed.
            //
            // The per-kingdom counts do survive intact, so those are all that is
            // carried - as a number, not as moons.  The new run's board is empty
            // and every moon on it is there to be found; the kingdom just starts
            // its counter that many moons up.
            const s32 lossPercent = Randomizer::getSoftNuzlockeLossPercent();

            // Moons bought with regional coins are surrendered whole and come off
            // the top, so the percentage is levied on what the run earned.
            s32 purchasedMoons[cMaxPurchasedMoons];
            const s32 purchasedMoonCount = PurpleCoinRewards::getPurchasedMoonIndices(file, purchasedMoons, cMaxPurchasedMoons);
            s32 purchasedCredit[cWorldCount] = {};
            for (s32 i = 0; i < purchasedMoonCount; i++) {
                const auto& moon = ShineDB::sShineTable[purchasedMoons[i]];
                if (!file->customIsGotShine(moon.uid, moon.stage))
                    continue;
                // This comes off beforeMoons, which is getShineNum's figure, so
                // both the kingdom and the value have to be the ones getShineNum
                // used - the assigned kingdom and the reward-multi payout while
                // redistribution is live.  See rebuildRedistributedShineCounts.
                const bool redistributed = MoonRando::isKingdomRedistributionActive();
                const GameDataFile::HintInfo* info = file->customFindShine(moon.uid, moon.stage);
                const s32 world = redistributed
                    ? MoonRando::getAssignedKingdomForEntry(&moon)
                    : (info ? info->worldId : -1);
                if (world < 0 || world >= cWorldCount)
                    continue;
                const bool isTriple = redistributed
                    ? MoonRando::isRewardMultiEntry(&moon)
                    : (info && info->isGrand);
                purchasedCredit[world] += isTriple ? 3 : 1;
            }

            for (s32 world = 0; world < cWorldCount; world++) {
                s32 earned = carry.beforeMoons[world] - purchasedCredit[world];
                if (earned < 0)
                    earned = 0;
                carry.keepMoons[world] = earned - lossCount(earned, lossPercent);
            }

            // Some kingdoms carry nothing at all, whatever the percentage says.
            //
            // Cloud, Mushroom, Dark Side and Darker Side are the postgame's
            // reward shelf rather than places a run works through. Carrying a
            // fraction of them just hands the new run a head start on the part
            // of the game the reroll has the least business preserving.  Cloud
            // has a second reason on top of that: with Cloud moons buying
            // painting unlocks its count *is* the painting economy, and the
            // paintings themselves are surrendered whole below, so leaving the
            // moons behind would pay for them twice - the run would keep the
            // moons that already bought a link and then be charged nothing to
            // buy it again.
            carry.keepMoons[WorldMeta::WorldId_Cloud] = 0;
            carry.keepMoons[WorldMeta::WorldId_Mushroom] = 0;
            carry.keepMoons[WorldMeta::WorldId_Dark] = 0;
            carry.keepMoons[WorldMeta::WorldId_Darker] = 0;

            // Half is the historical penalty and remains the default; the Game
            // Mode Settings page can trade it for a quarter or three quarters.
            // With the moons themselves no longer individually identified, the
            // equipment penalty is that same percentage of what is held rather
            // than a tally of the reward slots on specific lost moons - the same
            // dial, applied directly.

            // The owned pools are the run's *earned* holdings: what the draw
            // takes from and what the survivors are retained out of.  The
            // opener and the regional-coin purchases are both surrendered
            // wholesale elsewhere, so neither belongs in here - leaving a
            // purchase in would let the lottery retain something the reroll has
            // already decided is gone.
            GameDataHolderAccessor accessor(carry.holder);
            const CaptureRando::CaptureEntry* ownedCaptures[cMaxCaptures] = {};
            s32 ownedCaptureCount = 0;
            for (s32 i = 0; i < CaptureRando::getCaptureCount() && ownedCaptureCount < cMaxCaptures; i++) {
                const auto* capture = CaptureRando::getCapture(i);
                if (capture && !isStartingCapture(*capture) && !isPurchasedCapture(carry, i)
                    && CaptureRando::isCaptureUnlocked(accessor, *capture))
                    ownedCaptures[ownedCaptureCount++] = capture;
            }
            const AbilityRando::AbilityEntry* ownedAbilities[cMaxAbilities] = {};
            s32 ownedAbilityCount = 0;
            for (s32 i = 0; i < AbilityRando::getAbilityCount() && ownedAbilityCount < cMaxAbilities; i++) {
                const auto* ability = AbilityRando::getAbility(i);
                // isAbilityRevocable, not just !alwaysUnlocked: a Movement Ability the
                // settings grant outright reads as owned but has no save flag to clear,
                // so surrendering it would burn a loss slot and take nothing.
                if (ability && AbilityRando::isAbilityRevocable(ability->id)
                    && !isStartingAbility(*ability)
                    && !isPurchasedAbility(carry, static_cast<s32>(ability->id))
                    && AbilityRando::isAbilityUnlocked(accessor, *ability))
                    ownedAbilities[ownedAbilityCount++] = ability;
            }
            s32 ownedPaintings[cMaxPaintings] = {};
            s32 ownedPaintingCount = 0;
            for (s32 i = 0; i < PaintingRando::getPaintingCount() && ownedPaintingCount < cMaxPaintings; i++) {
                if (!PaintingRando::isPaintingLinkFree(i) && PaintingRando::isPaintingUnlocked(file, i))
                    ownedPaintings[ownedPaintingCount++] = i;
            }

            // Captures and abilities each answer to their own dial now; only the
            // moons and the paintings follow lossPercent.
            const s32 captureLossPercent = Randomizer::getSoftNuzlockeCaptureLossPercent();
            const s32 abilityLossPercent = Randomizer::getSoftNuzlockeAbilityLossPercent();

            const CaptureRando::CaptureEntry* lostCaptures[cMaxCaptures] = {};
            chooseAndRemove(ownedCaptures, ownedCaptureCount,
                lossCount(ownedCaptureCount, captureLossPercent), rng,
                lostCaptures, carry.lostCaptureCount);
            for (s32 i = 0; i < ownedCaptureCount; i++) {
                const s32 index = getCaptureIndex(ownedCaptures[i]);
                if (index >= 0)
                    carry.request.retainedCaptures |= 1ull << index;
            }
            for (s32 i = 0; i < carry.lostCaptureCount; i++)
                carry.lostCaptures[i] = static_cast<s16>(getCaptureIndex(lostCaptures[i]));

            const AbilityRando::AbilityEntry* lostAbilities[cMaxAbilities] = {};
            chooseAndRemoveAbilities(ownedAbilities, ownedAbilityCount,
                lossCount(ownedAbilityCount, abilityLossPercent), rng,
                lostAbilities, carry.lostAbilityCount);
            for (s32 i = 0; i < ownedAbilityCount; i++)
                carry.request.retainedAbilities |= 1ull << static_cast<s32>(ownedAbilities[i]->id);
            for (s32 i = 0; i < carry.lostAbilityCount; i++)
                carry.lostAbilities[i] = static_cast<s16>(lostAbilities[i]->id);

            // Paintings only enter the retain lottery when they are won.  Once
            // Cloud moons buy them they are a purchase like any other regional
            // -coin reward, and the reroll puts the currency back - the Cloud
            // moons that paid for these links are gone above, so the links go
            // too and are re-bought with the moons the new run finds.
            s32 lostPaintings[cMaxPaintings] = {};
            const s32 paintingLossCount = Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled()
                ? ownedPaintingCount
                : lossCount(ownedPaintingCount, lossPercent);
            chooseAndRemove(ownedPaintings, ownedPaintingCount, paintingLossCount, rng,
                lostPaintings, carry.lostPaintingCount);
            for (s32 i = 0; i < ownedPaintingCount; i++)
                carry.request.retainedPaintings |= 1ull << ownedPaintings[i];
            for (s32 i = 0; i < carry.lostPaintingCount; i++)
                carry.lostPaintings[i] = static_cast<s16>(lostPaintings[i]);

            snapshotOldStartingEquipment(&carry);
            prepareMoonCounterTargets(&carry);
            return true;
        }

        void setCounter(HakoniwaSequence* sequence, s32 value) {
            if (!sequence || !sequence->mCounterMiss)
                return;
            al::setPaneStringFormat(sequence->mCounterMiss, "TxtCounter", "%04d", value);
            requestCounterCapture(sequence->mCounterMiss);
        }

        // CounterMiss's own "Subtract" action is what plays the thud at the end of
        // the coin countdown: SeDataBase's CounterMiss entry maps that action to
        // the CoinDecrease SE (and "SubtractColor" to the CoinDecreaseCount tick).
        // The SE is driven by al::ActionSeCtrl from the layout's per-frame update,
        // not fired synchronously, so the layout has to still be alive and
        // executing - which is why the moon panel hides it rather than ending it.
        void playCounterSubtractSe(HakoniwaSequence* sequence) {
            if (sequence && sequence->mCounterMiss)
                al::startAction(sequence->mCounterMiss, "Subtract", nullptr);
        }

        void setCounterVisible(HakoniwaSequence* sequence, bool visible) {
            if (!sequence || !sequence->mCounterMiss)
                return;
            if (visible)
                al::showPane(sequence->mCounterMiss, "RootPane");
            else
                al::hidePane(sequence->mCounterMiss, "RootPane");
        }

        void startCoinCounter(HakoniwaSequence* sequence) {
            sDeath.counterStart = sDeath.coinsBefore;
            sDeath.counterTarget = 0;
            sDeath.counterFrame = 0;
            sDeath.counterDuration = sDeath.coinsBefore;
            if (sDeath.counterDuration < cCoinDrainMinFrames)
                sDeath.counterDuration = cCoinDrainMinFrames;
            if (sDeath.counterDuration > cCoinDrainMaxFrames)
                sDeath.counterDuration = cCoinDrainMaxFrames;
            if (sequence && sequence->mCounterMiss)
                al::startAction(sequence->mCounterMiss, "SubtractColor", nullptr);
            sDeath.phase = DeathPhase::CounterSubtract;
        }

        // The kingdom rows are built once, before any of them drains, so the
        // player can see the whole penalty up front and then watch it apply.
        void buildMoonPanel() {
            sOverlay = {};
            sOverlay.soft = sDeath.soft;
            sOverlay.timeout = sDeath.timeout;
            sOverlay.stage = OverlayStage::MoonLoss;
            for (s32 world = 0; world < cWorldCount; world++) {
                // Soft state must display its pre-penalty snapshot: the
                // GameDataFile still holds the old counts here, and only the
                // snapshot knows what they will become.  Hard state owns no
                // snapshot and drains the live counts to zero.
                const s32 before = sDeath.soft
                    ? sDeath.softCarry.beforeMoons[world]
                    : sDeath.file ? sDeath.file->getShineNum(world)
                                  : 0;
                const s32 after = sDeath.soft ? sDeath.softCarry.afterMoons[world] : 0;
                if (before <= 0 || before == after)
                    continue;
                if (!TrackerOverlay::getWorldStyle(world).icon)
                    continue;
                const s32 row = sOverlay.rowCount++;
                sOverlay.rowWorld[row] = world;
                sOverlay.rowBefore[row] = before;
                sOverlay.rowValue[row] = before;
                sOverlay.rowAfter[row] = after;
                sOverlay.lostTotal += before - after;
            }
            // A hard wipe takes everything, so draining kingdom by kingdom would
            // just be a long way of saying "all of it".  Drain them together and
            // let the soft penalty keep the per-kingdom walk.
            sOverlay.activeRow = sDeath.soft && sOverlay.rowCount > 0 ? 0 : -1;

            // Only a soft run has a meaningful item ledger; a hard run loses the
            // save outright.  The randomly-dropped items come first, then the
            // opener, which is surrendered in full and re-rolled with the seed.
            if (!sDeath.soft)
                return;
            const SoftCarry& carry = sDeath.softCarry;
            const auto addLost = [](const char* name, LostItemKind kind) {
                if (!name || sOverlay.lostItemCount >= cMaxLostItems)
                    return;
                for (s32 i = 0; i < sOverlay.lostItemCount; i++)
                    if (sOverlay.lostItemName[i] == name)
                        return;
                sOverlay.lostItemKind[sOverlay.lostItemCount] = kind;
                sOverlay.lostItemName[sOverlay.lostItemCount++] = name;
            };
            for (s32 i = 0; i < carry.lostCaptureCount; i++) {
                if (const auto* capture = CaptureRando::getCapture(carry.lostCaptures[i]))
                    addLost(capture->displayName, LostItemKind::Capture);
            }
            for (s32 i = 0; i < carry.oldStartingCaptureCount; i++) {
                if (const auto* capture = CaptureRando::getCapture(carry.oldStartingCaptures[i]))
                    addLost(capture->displayName, LostItemKind::Capture);
            }
            for (s32 i = 0; i < carry.lostAbilityCount; i++) {
                if (const auto* ability = AbilityRando::getAbility(carry.lostAbilities[i]))
                    addLost(ability->displayName, LostItemKind::Ability);
            }
            for (s32 i = 0; i < carry.oldStartingAbilityCount; i++) {
                if (const auto* ability = AbilityRando::getAbility(carry.oldStartingAbilities[i]))
                    addLost(ability->displayName, LostItemKind::Ability);
            }
            // Purple Shop purchases are surrendered outside the draw, so they
            // never appear in the lost lists above - name them here or the
            // panel would quietly under-report the loss.
            for (s32 i = 0; i < CaptureRando::getCaptureCount() && i < 64; i++) {
                if (!isPurchasedCapture(carry, i))
                    continue;
                if (const auto* capture = CaptureRando::getCapture(i))
                    addLost(capture->displayName, LostItemKind::Capture);
            }
            for (s32 i = 0; i < AbilityRando::getAbilityCount() && i < 64; i++) {
                if (!isPurchasedAbility(carry, i))
                    continue;
                if (const auto* ability = AbilityRando::getAbility(i))
                    addLost(ability->displayName, LostItemKind::Ability);
            }

            // Open with the viewport already filled rather than with entries
            // trickling in from an empty box.  A list that fits parks there and
            // never moves; a longer one starts scrolling from that same state, so
            // the first thing on screen is always real content.
            const s32 filledRows = sOverlay.lostItemCount < cLostVisibleRows
                ? sOverlay.lostItemCount
                : cLostVisibleRows;
            sOverlay.lostScroll = static_cast<float>(filledRows > 0 ? filledRows - 1 : 0);
        }

        void advanceLostScroll() {
            if (sOverlay.stage != OverlayStage::MoonLoss
                || sOverlay.lostItemCount <= cLostVisibleRows)
                return;
            sOverlay.lostScroll += 1.0f / static_cast<float>(cLostScrollFrames);
            // Wrap a whole cycle at a time so the entry currently mid-fade keeps
            // its sub-row offset and the glide never jumps.
            const float cycle = static_cast<float>(sOverlay.lostItemCount);
            if (sOverlay.lostScroll >= cycle)
                sOverlay.lostScroll -= cycle;
        }

        s32 rowDrainFrames(s32 row) {
            s32 lost = 0;
            if (row >= 0) {
                lost = sOverlay.rowBefore[row] - sOverlay.rowAfter[row];
            } else {
                for (s32 i = 0; i < sOverlay.rowCount; i++)
                    lost += sOverlay.rowBefore[i] - sOverlay.rowAfter[i];
            }
            const s32 frames = cRowDrainMinFrames + lost * 3;
            return frames > cRowDrainMaxFrames ? cRowDrainMaxFrames : frames;
        }

        void drainRow(s32 row, s32 frame, s32 duration) {
            const s32 span = sOverlay.rowBefore[row] - sOverlay.rowAfter[row];
            const s32 progressed = (span * frame + duration - 1) / duration;
            s32 value = sOverlay.rowBefore[row] - progressed;
            if (value < sOverlay.rowAfter[row])
                value = sOverlay.rowAfter[row];
            sOverlay.rowValue[row] = value;
        }

        void restoreOldSeed() {
            if (!sDeath.soft)
                return;
            Randomizer::setSeed(sDeath.softCarry.request.oldSeed);
            Randomizer::reinit();
        }

        bool commitSoftPenalty() {
            SoftCarry& carry = sDeath.softCarry;
            GameDataFile* file = carry.file;
            if (!file || !carry.holder) {
                Randomizer::logPush("Soft Nuzlocke: commit failed missing game-data holder");
                restoreOldSeed();
                return false;
            }
            if (!RandoSaveFile::write(carry.request.slot)) {
                Randomizer::logPush("Soft Nuzlocke: commit failed writing rerolled sidecar");
                restoreOldSeed();
                return false;
            }

            GameDataHolderWriter writer(carry.holder);
            for (s32 i = 0; i < carry.lostCaptureCount; i++) {
                const s32 index = carry.lostCaptures[i];
                if (index >= 0) {
                    if (const auto* capture = CaptureRando::getCapture(index))
                        CaptureRando::setCaptureUnlocked(writer, *capture, false);
                }
            }
            for (s32 i = 0; i < carry.lostAbilityCount; i++) {
                const s32 id = carry.lostAbilities[i];
                if (id >= 0) {
                    if (const auto* ability = AbilityRando::getAbility(id))
                        AbilityRando::setAbilityUnlocked(writer, *ability, false);
                }
            }
            // The old opener goes back in the pool.  Its ownership is stored two
            // different ways - a StartingCapture flag for captures, a plain
            // ability flag for the cap throw - and neither is cleared by the lock
            // masks below, so revoke both explicitly before the new seed's opener
            // is granted.  Anything the new seed happens to start with again is
            // re-granted by applyStartingEquipment a few lines down.
            for (s32 i = 0; i < carry.oldStartingCaptureCount; i++) {
                if (const auto* capture = CaptureRando::getCapture(carry.oldStartingCaptures[i]))
                    CaptureRando::setCaptureUnlocked(writer, *capture, false);
            }
            for (s32 i = 0; i < carry.oldStartingAbilityCount; i++) {
                if (const auto* ability = AbilityRando::getAbility(carry.oldStartingAbilities[i]))
                    AbilityRando::setAbilityUnlocked(writer, *ability, false);
            }
            // Regional-coin purchases go the same way as the old opener: the
            // coins that bought them are back in the levels, so the rewards are
            // revoked outright rather than left for the lock masks to sort out.
            // The moons bought that way came off the kingdom's carried count in
            // prepareSoftPenalty, so they are already gone.
            for (s32 i = 0; i < AbilityRando::getAbilityCount() && i < 64; i++) {
                if (!isPurchasedAbility(carry, i))
                    continue;
                if (const auto* ability = AbilityRando::getAbility(i))
                    AbilityRando::setAbilityUnlocked(writer, *ability, false);
            }
            for (s32 i = 0; i < CaptureRando::getCaptureCount() && i < 64; i++) {
                if (!isPurchasedCapture(carry, i))
                    continue;
                if (const auto* capture = CaptureRando::getCapture(i))
                    CaptureRando::setCaptureUnlocked(writer, *capture, false);
            }
            PurpleCoinRewards::clearPurchaseLedger(file);
            // No moon survives a reroll - see prepareSoftPenalty.  The board is
            // wiped outright and nothing is handed back; what the kingdoms kept
            // is written down as a plain count further below.
            clearAllCollectedMoons(file);

            u64 captureLocks = 0;
            for (s32 i = 0; i < CaptureRando::getCaptureCount() && i < 64; i++) {
                const auto* capture = CaptureRando::getCapture(i);
                if (capture && !isStartingCapture(*capture)
                    && !(carry.request.retainedCaptures & (1ull << i)))
                    captureLocks |= 1ull << i;
            }
            u64 abilityLocks = 0;
            for (s32 i = 0; i < AbilityRando::getAbilityCount() && i < 64; i++) {
                const auto* ability = AbilityRando::getAbility(i);
                if (ability && !ability->alwaysUnlocked && !isStartingAbility(*ability)
                    && !(carry.request.retainedAbilities & (1ull << i)))
                    abilityLocks |= 1ull << i;
            }
            u64 paintingLocks = 0;
            for (s32 i = 0; i < PaintingRando::getPaintingCount() && i < 64; i++) {
                if (!PaintingRando::isPaintingLinkFree(i)
                    && !(carry.request.retainedPaintings & (1ull << i)))
                    paintingLocks |= 1ull << i;
            }
            setLockMask(file, cCaptureLocksPrefix, captureLocks);
            setLockMask(file, cCaptureRetainedPrefix, carry.request.retainedCaptures);
            setLockMask(file, cAbilityLocksPrefix, abilityLocks);
            setLockMask(file, cAbilityRetainedPrefix, carry.request.retainedAbilities);
            setLockMask(file, cPaintingLocksPrefix, paintingLocks);
            setLockMask(file, cPaintingRetainedPrefix, carry.request.retainedPaintings);

            // Grant the new seed's opener.  Abilities resolve their starting set
            // implicitly from the live table, but a starting capture is only owned
            // through a save flag, so without this the reroll would leave the
            // player with no starting captures at all.  It runs after the lock
            // masks because those already exempt the new starting set.
            CaptureRando::applyStartingEquipment(writer);
            AbilityRando::applyStartingEquipment(writer);
            // A reroll hands out a whole new opener, so announce it the way a
            // new file does.  Both requests are consumed once the next
            // StageScene exists (see DebugMenuHooks), which is exactly where the
            // reroll is heading - queuing now would draw over the load screen.
            AbilityOverlay::requestStartingEquipment();
            PaintingRando::requestFreeLinkAnnouncement();

            file->customResetSoftNuzlockeWorldState();
            file->customRebuildShineCounters();
            // What each kingdom kept, as a number, written straight after the
            // rebuild that zeroed the real counts so the pair is set together.
            // It replaces rather than adds to any previous carry: beforeMoons
            // already included the old one, so the percentage was levied on the
            // run's whole standing and this is what is left of all of it.
            storeCarriedMoons(file, carry.afterMoons);
            // customRebuildShineCounters only fixes the save's own mShineNum
            // table. Everything on screen reads getShineNum, which is served
            // from a separate cache while Randomize Triple Moons is on.
            MoonHooks::invalidateShineCountCache();
            HintSystem::clearSeedSpecificState(file);
            ScenarioPatcher::applyInitialKingdomStates(file);
            reconcileVanillaStoryProgress(file);
            StoryProgressHooks::healAllMainScenarios(file);
            ScenarioPatcher::healAllScenarios(file);
            Randomizer::setVisitedKingdoms(0);
            Randomizer::resetKingdomVisitOrder();
            Randomizer::applyUnlockedKingdomsMapStart();
            Randomizer::setForcedPeaceKingdoms(0);
            KingdomProgress::reset();

            // GameDataHolder::changeNextStage is latched (GameDataHolder::mIsStage):
            // it commits the first destination asked for since the last
            // StageScene::init and silently drops every later request.  A death
            // inside a sub-area spends that latch before the reroll ever runs -
            // GameDataFile::missAndRestartStage pushes MissRestartInfo through
            // changeNextStage, which an overworld death with no MissRestartArea
            // never does - so the forced start stage below was being thrown
            // away and the reroll rebuilt the sub-area the run just died in.
            // customResetSoftNuzlockeWorldState has by then set mCurrentWorldId
            // to -1, and only a *main* stage name repairs it (GameDataFile::
            // startStage resolves it through tryFindWorldIndexByMainStageName),
            // so the sub-area came up with world -1 and Shine::initAfterPlacement
            // crashed inside WorldList::getMainStageName(-1) via isMainStage.
            // Vanilla clears the same flag when it forces its own destination
            // (HakoniwaSequence's ending world warp), which is what this copies.
            // The restart info itself is already gone - the world reset above
            // drops both the flag and the pointer - so nothing downstream
            // mistakes the change below for a death restart.
            carry.holder->mIsStageChanging = false;

            if (!KingdomProgress::initializeNew(file)) {
                Randomizer::logPush("Soft Nuzlocke: commit failed initializing kingdom progress");
                restoreOldSeed();
                return false;
            }

            const char* startStageName = nullptr;
            const bool redirectExpected = Randomizer::isKingdomRandoEnabled()
                || StartPositionRando::isActive();
            const bool redirected = ProgressionHooks::prepareNewSaveStartStage(
                writer, &startStageName, nullptr);
            if (!redirected) {
                // A vanilla-seeded file has no forced start-stage redirect.  This
                // is the same expected false result handled by the file-select
                // new-file path, which sends the player to Cap Kingdom instead.
                // Only treat a missing redirect as an error when the current seed
                // actually requires Kingdom Order or a randomized start.
                if (redirectExpected) {
                    Randomizer::logPush(
                        "Soft Nuzlocke: commit failed preparing randomized start redirect");
                    restoreOldSeed();
                    return false;
                }

                ChangeStageInfo info(carry.holder, "", "CapWorldHomeStage", false, 1,
                    ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
                if (!GameDataFunction::tryChangeNextStage(writer, &info)) {
                    Randomizer::logPush(
                        "Soft Nuzlocke: commit failed applying vanilla Cap start fallback");
                    restoreOldSeed();
                    return false;
                }
                startStageName = "CapWorldHomeStage";
                Randomizer::logPush(
                    "Soft Nuzlocke: using vanilla CapWorldHomeStage start fallback");
            } else {
                Randomizer::logPush(
                    "Soft Nuzlocke: using randomized start redirect stage=%s",
                    startStageName ? startStageName : "(unknown)");
            }

            // Belt and braces for the -1 above: whatever route the stage change
            // took, no scene may be built while the current world is unset.
            // Every reader of it indexes WorldList unguarded.  The lookup is
            // WorldList::tryFindWorldIndexByMainStageName done by hand - that
            // symbol is not in syms/main.sym, and the comparison it makes is
            // just this one.
            if (file->getCurrentWorldId() < 0 && startStageName && carry.holder->mWorldList) {
                s32 startWorldId = -1;
                const s32 worldCount = carry.holder->mWorldList->mWorldList.size();
                for (s32 i = 0; i < worldCount; i++) {
                    const WorldListEntry* world = carry.holder->mWorldList->mWorldList[i];
                    if (world && world->mainStageName
                        && std::strcmp(world->mainStageName, startStageName) == 0) {
                        startWorldId = i;
                        break;
                    }
                }
                if (startWorldId >= 0) {
                    file->mCurrentWorldId = startWorldId;
                    file->mCurrentWorldIdForWrite = startWorldId;
                    file->mCurrentWorldIdForDisp = startWorldId;
                }
                Randomizer::logPush("Soft Nuzlocke: repaired current world %d from start stage %s",
                    startWorldId, startStageName);
            }

            RandoSaveFile::updateMeta(carry.request.slot, file);
            GameDataFunction::setRequireSave(writer);
            SaveDataAccessFunction::startSaveDataWriteSync(carry.holder);
            Randomizer::logPush("Soft Nuzlocke: committed retained seed=%08x", Randomizer::getSeed());
            return true;
        }

        // Both endings have to get rid of the dead run's world resources before
        // anything else is built on top of them.  Every vanilla path that leaves
        // a stage for the boot state - exeWaitLoadData, and both branches of
        // exeBootLoadData - calls WorldResourceLoader::tryDestroyWorldResource
        // first; skipping it left the previous kingdom's archive occupying the
        // world-resource heap, and TitleMenuScene then drew its sky from a model
        // whose GPU buffer was never allocated.
        void beginResourceTeardown(HakoniwaSequence* sequence) {
            if (sequence)
                cancelWorldResource(sequence->mResourceLoader);
            sDeath.counterFrame = 0;
            sDeath.phase = DeathPhase::WaitForResourceStop;
        }

        // Single exit from the moon ledger, shared by the normal end of the
        // animation and by the "nothing was collected" shortcut.
        void leaveMoonPanel(HakoniwaSequence* sequence) {
            // Nothing else drives this layout out: vanilla's exeMissEnd is its
            // only caller of end(), and the coordinator replaces that state.
            // Without this it stays drawn over the reload and the title.
            if (sequence && sequence->mCounterMiss)
                sequence->mCounterMiss->end();
            sOverlay = {};
            sDeath.counterFrame = 0;
            if (sDeath.soft) {
                sOverlay.stage = OverlayStage::Rerolling;
                sOverlay.soft = true;
                sOverlay.alpha = 1.0f;
            }
            beginResourceTeardown(sequence);
        }

        void retrySoftWorker() {
            SoftCarry& carry = sDeath.softCarry;
            carry.request.requestedSeed = makeFreshSeed();
            sDeath.counterFrame = 0;
            sOverlay.stage = OverlayStage::Rerolling;
            sDeath.phase = DeathPhase::StartSoftWorker;
        }

        void finishHardDeath(HakoniwaSequence* sequence) {
            if (!SaveRedirect::deleteSlot(sDeath.slot, sDeath.seed)) {
                // Retry: a failure here is usually a transient SD/file-handle
                // problem.  Bounded, because looping on it forever would leave the
                // player on a dead screen with no way out - better to reach the
                // title with the slot intact than to hang.
                if (++sDeath.counterFrame < cDeleteRetryFrames)
                    return;
                Randomizer::logPush("Nuzlocke: slot %d could not be deleted; returning to title anyway",
                    sDeath.slot);
            } else {
                Randomizer::logPush("Nuzlocke: deleted slot %d after Mario death", sDeath.slot);
            }

            // Vanilla's own return-to-title tail (HakoniwaSequence::exeWaitLoadData):
            // flag the boot state to skip straight to the menu, then hand the
            // sequence its real BootLoadData nerve.  Driving exeBootLoadData from a
            // custom nerve instead does not work - that function is a nerve-state
            // executor, and al::updateNerveState only ever reports completion for a
            // state the nerve keeper actually activated, so it span forever.
            //
            // Order matters: main.cpp's startLoad hook only calls through to the
            // original while isReturningToTitle() is true.
            sDeath.returningToTitle = true;
            if (sequence && sequence->mStateBootLoadData)
                sequence->mStateBootLoadData->startLoad();
            al::setNerve(sequence, mainOffsetNerve(cBootLoadDataNerveOffset));
            // The run is over and the sequence is out of our hands; clear the whole
            // coordinator so a later run in the same session can arm again.
            sDeath = {};
            sOverlay = {};
        }
    } // namespace

    void installHooks() {
        // Timer Mode's damage penalty rides this hook too, so it installs for
        // either feature even though everything below it is Nuzlocke-only.
        if (Randomizer::isNuzlockeModesFeatureEnabled() || Randomizer::isTimerModeFeatureEnabled())
            playerHitPointDamageHook.installAtSym<"_ZN18PlayerHitPointData6damageEv">();
        if (!Randomizer::isNuzlockeModesFeatureEnabled())
            return;
        startSeHook.installAtSym<"_ZN2al7startSeEPKNS_15IUseAudioKeeperERKN4sead14SafeStringBaseIcEE">();
        startHitReactionHook.installAtSym<"_ZN2al16startHitReactionEPKNS_11LayoutActorEPKcS4_">();
        hitReactionKeeperStartHook.installAtMainOffset(cHitReactionKeeperStartOffset);
        // CounterLife isn't exported in the shipped symbol table, so resolve
        // these two fixed game functions by their main-module offsets.
        counterLifeWaitHook.installAtMainOffset(0x001e58c4);
        counterLifeStartCountAnimHook.installAtMainOffset(0x001e6f44);
        playerHitPointCurrentHook.installAtSym<"_ZNK18PlayerHitPointData10getCurrentEv">();
        playerHitPointMaxCurrentHook.installAtSym<"_ZNK18PlayerHitPointData13getMaxCurrentEv">();
        playerHitPointMaxWithItemHook.installAtMainOffset(0x005388b4);
        playerHitPointMaxWithoutItemHook.installAtMainOffset(0x005388d4);
    }

    bool armMarioDeath(GameDataFile* file) {
        // A Timer Mode run only ends when its clock runs out: ordinary deaths
        // there are vanilla, so the timeout is the one that arms this.  It ends
        // the run exactly like a hard Nuzlocke death - same ledger panel, same
        // slot deletion, same return to the title.
        const bool isTimeout = TimerMode::isTimeoutDeathPending();
        if (!file || (!Randomizer::isNuzlockeRunMode() && !isTimeout) || sDeath.pending)
            return false;

        sDeath = {};
        sOverlay = {};
        sDeath.pending = true;
        sDeath.soft = !isTimeout && Randomizer::isSoftNuzlockeRunMode();
        sDeath.timeout = isTimeout;
        sDeath.file = file;
        sDeath.coinsBefore = file->getCoinNum();
        sDeath.slot = SaveRedirect::getActiveSlot();
        sDeath.seed = Randomizer::getSeed();
        if (sDeath.slot < 1 || (sDeath.soft && !prepareSoftPenalty(file))) {
            sDeath = {};
            sOverlay = {};
            return false;
        }
        // Latched only once the arm has actually succeeded, so a rejected arm
        // (no rando slot) lets the timeout keep retrying.
        if (isTimeout)
            TimerMode::notifyDeathArmed();
        Randomizer::logPush("Nuzlocke: armed death mode=%d timeout=%d slot=%d coins=%d",
            sDeath.soft ? 2 : 1, sDeath.timeout ? 1 : 0, sDeath.slot, sDeath.coinsBefore);
        return true;
    }

    // HakoniwaSequence::_100 is the sequence's "this scene is really going away"
    // flag: exePlayStage clears it only when the next stage and scenario match
    // the current ones, which is exactly what a vanilla Miss is - the player
    // respawns in the stage they died in, so exeDestroy hands
    // HakoniwaStateDeleteScene a false and deleteScene frees the SceneHeap but
    // keeps the SceneResourceHeap, still holding that kingdom's resources, for
    // exeMiss to build the replacement scene inside.
    //
    // A Nuzlocke death never takes that route: hard runs continue into the boot
    // state and soft runs respawn from the new seed's start stage.  Leaving the
    // dead kingdom's resource heap behind then starves whatever is built next -
    // al::createSceneHeap sizes the new SceneHeap from the free space that is
    // left, so the title's archives fail their decompression alloc on the file
    // load thread (sead::FrameHeap::tryAlloc halt inside al::ArchiveEntry::load).
    // Forcing the flag makes the delete-scene state release both heaps, which is
    // what every vanilla stage change and "Return to Main Menu" already does.
    void prepareSceneTeardown(HakoniwaSequence* sequence) {
        if (!sequence || !sDeath.pending || sequence->_100)
            return;
        sequence->_100 = true;
        Randomizer::logPush("Nuzlocke: releasing the dead stage's resource heap with its scene");
    }

    bool takeControlAfterSceneDestroy(HakoniwaSequence* sequence) {
        if (!sDeath.pending || !sequence || sequence->mCurrentScene)
            return false;
        al::setNerve(sequence, &sNuzlockeDeathNerve);
        return true;
    }

    void updateDeathSequence(HakoniwaSequence* sequence) {
        if (!sequence || !sDeath.pending)
            return;

        // Runs across every moon-panel phase, so the ticker keeps gliding while
        // the kingdom rows drain rather than being tied to one of them.
        advanceLostScroll();

        switch (sDeath.phase) {
        case DeathPhase::Idle:
            // The native Miss path has not run, so no ten-coin deduction has
            // happened.  Commit the all-coins penalty now, after the StageScene
            // and its frame heap are gone.
            if (sDeath.file) {
                sDeath.file->customTrySpendCoin(sDeath.file->getCoinNum());
                if (GameDataHolder* holder = sDeath.file->getGameDataHolder())
                    GameDataFunction::setRequireSave(GameDataHolderWriter(holder));
            }
            if (sequence->mCounterMiss) {
                // Undo the hide the previous death's moon panel left behind; the
                // sequence owns this layout for the whole session and reuses it.
                setCounterVisible(sequence, true);
                // Vanilla's miss path never writes this pane, but it also never
                // shows anything except coins.  The glyph has to be set here or
                // the counter appears with a bare number and no coin symbol.
                al::setPaneString(sequence->mCounterMiss, "TxtIcon", u"@", 0);
                setCounter(sequence, sDeath.coinsBefore);
                sequence->mCounterMiss->appear();
            }
            sDeath.counterFrame = 0;
            sDeath.phase = DeathPhase::CounterAppear;
            break;

        case DeathPhase::CounterAppear:
            // CounterMiss's own Appear action is intentionally reused.  Let it
            // settle a beat longer than vanilla before the first decrement.
            if (++sDeath.counterFrame >= cCounterAppearFrames)
                startCoinCounter(sequence);
            break;

        case DeathPhase::CounterSubtract: {
            const s32 nextFrame = ++sDeath.counterFrame;
            const s32 delta = sDeath.counterStart - sDeath.counterTarget;
            const s32 progressed = (delta * nextFrame + sDeath.counterDuration - 1)
                / sDeath.counterDuration;
            s32 value = sDeath.counterStart - progressed;
            if (value < sDeath.counterTarget)
                value = sDeath.counterTarget;
            setCounter(sequence, value);
            if (nextFrame >= sDeath.counterDuration) {
                playCounterSubtractSe(sequence);
                sDeath.counterFrame = 0;
                sDeath.phase = DeathPhase::CounterEnd;
            }
            break;
        }

        case DeathPhase::CounterEnd:
            // Let the emptied counter sit for a beat before the moon panel takes
            // over, so the two never overlap.
            if (++sDeath.counterFrame < cCounterEndFrames)
                break;
            buildMoonPanel();
            sDeath.counterFrame = 0;
            // Dying with nothing collected has no ledger to show; go straight on
            // rather than flashing an empty panel.
            if (sOverlay.rowCount == 0 && sOverlay.lostItemCount == 0) {
                leaveMoonPanel(sequence);
                break;
            }
            // The layout stays alive and executing behind the moon panel purely
            // so its Subtract action can keep firing the CoinDecrease SE; only
            // its panes are hidden.  leaveMoonPanel() is what finally ends it.
            setCounterVisible(sequence, false);
            sDeath.phase = DeathPhase::MoonPanelIn;
            break;

        case DeathPhase::MoonPanelIn:
            sOverlay.alpha = static_cast<float>(++sDeath.counterFrame)
                / static_cast<float>(cPanelFadeFrames);
            if (sDeath.counterFrame < cPanelFadeFrames)
                break;
            sOverlay.alpha = 1.0f;
            sDeath.counterFrame = 0;
            sDeath.phase = DeathPhase::MoonPanelHold;
            break;

        case DeathPhase::MoonPanelHold:
            // Show the untouched counts first.  The penalty reads as something
            // being taken away only if the "before" state was on screen.
            if (++sDeath.counterFrame < cPanelHoldFrames)
                break;
            sDeath.counterFrame = 0;
            sDeath.rowDuration = rowDrainFrames(sOverlay.activeRow);
            sDeath.phase = DeathPhase::MoonPanelDrain;
            break;

        case DeathPhase::MoonPanelDrain: {
            const s32 frame = ++sDeath.counterFrame;
            // The highlight leads the numbers slightly: the row lights up, then
            // its count starts falling.
            sOverlay.activePulse = frame < cRowPulseFrames
                ? static_cast<float>(frame) / static_cast<float>(cRowPulseFrames)
                : 1.0f;
            if (sOverlay.activeRow >= 0) {
                drainRow(sOverlay.activeRow, frame, sDeath.rowDuration);
            } else {
                for (s32 i = 0; i < sOverlay.rowCount; i++)
                    drainRow(i, frame, sDeath.rowDuration);
            }
            if (frame >= sDeath.rowDuration) {
                // Same beat the coin counter lands on, so a kingdom finishing
                // reads as the same kind of loss.
                playCounterSubtractSe(sequence);
                sDeath.counterFrame = 0;
                sDeath.phase = DeathPhase::MoonPanelSettle;
            }
            break;
        }

        case DeathPhase::MoonPanelSettle: {
            const s32 frame = ++sDeath.counterFrame;
            sOverlay.activePulse = 1.0f
                - static_cast<float>(frame) / static_cast<float>(cRowSettleFrames);
            if (sOverlay.activePulse < 0.0f)
                sOverlay.activePulse = 0.0f;
            if (frame < cRowSettleFrames)
                break;
            sDeath.counterFrame = 0;
            if (sOverlay.activeRow >= 0 && sOverlay.activeRow + 1 < sOverlay.rowCount) {
                sOverlay.activeRow++;
                sDeath.rowDuration = rowDrainFrames(sOverlay.activeRow);
                sDeath.phase = DeathPhase::MoonPanelDrain;
                break;
            }
            sOverlay.activeRow = -1;
            sOverlay.activePulse = 0.0f;
            sDeath.phase = DeathPhase::MoonPanelOut;
            break;
        }

        case DeathPhase::MoonPanelOut: {
            const s32 frame = ++sDeath.counterFrame;
            // Hold the finished totals before dismissing.  A soft run then hands
            // straight over to the reroll panel, which replaces this one in place;
            // a hard run has nothing to follow it, so it fades out instead.
            if (frame < cPanelFinalHoldFrames)
                break;
            if (sDeath.soft) {
                leaveMoonPanel(sequence);
                break;
            }
            const s32 fadeFrame = frame - cPanelFinalHoldFrames;
            sOverlay.alpha = 1.0f
                - static_cast<float>(fadeFrame) / static_cast<float>(cPanelFadeFrames);
            if (fadeFrame < cPanelFadeFrames)
                break;
            leaveMoonPanel(sequence);
            break;
        }

        case DeathPhase::WaitForResourceStop:
            // Destroying mid-load would free an archive the loader thread is
            // still writing into, so wait for the cancel to land first.
            if (!sequence->mResourceLoader || sequence->mResourceLoader->isEndLoadWorldResource()) {
                if (sequence->mResourceLoader)
                    destroyWorldResource(sequence->mResourceLoader);
                sDeath.phase = sDeath.soft ? DeathPhase::StartSoftWorker
                                           : DeathPhase::DeleteHardSlot;
            }
            break;

        case DeathPhase::StartSoftWorker:
            if (SeedGenerator::startSoft(sDeath.softCarry.request))
                sDeath.phase = DeathPhase::WaitSoftWorker;
            else {
                sDeath.counterFrame = 0;
                sOverlay.stage = OverlayStage::RerollFailed;
                sDeath.phase = DeathPhase::ErrorRetry;
            }
            break;

        case DeathPhase::WaitSoftWorker:
            if (!SeedGenerator::isDone())
                break;
            if (SeedGenerator::succeeded())
                sDeath.phase = DeathPhase::CommitSoft;
            else {
                sDeath.counterFrame = 0;
                sOverlay.stage = OverlayStage::RerollFailed;
                sDeath.phase = DeathPhase::ErrorRetry;
            }
            break;

        case DeathPhase::CommitSoft:
            if (commitSoftPenalty()) {
                sDeath.pending = false;
                sOverlay = {};
                al::setNerve(sequence, &sNuzlockeLoadWorldResourceNerve);
            } else {
                sDeath.counterFrame = 0;
                sOverlay.stage = OverlayStage::RerollFailed;
                sDeath.phase = DeathPhase::ErrorRetry;
            }
            break;

        case DeathPhase::DeleteHardSlot:
            finishHardDeath(sequence);
            break;

        case DeathPhase::ErrorRetry:
            // Keep WipeMiss closed.  A retry never resumes the old stage and
            // always starts from the intact slot/old randomizer tables.
            if (++sDeath.counterFrame >= 120)
                retrySoftWorker();
            break;
        }
    }

    void updateLoadWorldResource(HakoniwaSequence* sequence) {
        if (sequence)
            exeNativeLoadWorldResource(sequence);
    }

    bool isReturningToTitle() {
        return sDeath.returningToTitle;
    }

    const OverlayState& getOverlayState() {
        return sOverlay;
    }

    s32 getCarriedMoons(const GameDataFile* file, s32 worldId) {
        if (!file || worldId < 0 || worldId >= cWorldCount)
            return 0;
        if (!sCarryLoaded || sCarryFile != file)
            loadCarriedMoons(file);
        return sCarryMoons[worldId];
    }

    void dropCarriedMoonCache() {
        sCarryFile = nullptr;
        sCarryLoaded = false;
        for (s32 i = 0; i < cWorldCount; i++)
            sCarryMoons[i] = 0;
    }

    // A reroll that lands in Cascade crashes in the graphics allocator seconds
    // after arrival.  Fast travel into Cascade and starting a fresh game there
    // are both fine, so it needs the reroll path and Cascade together - this is a
    // workaround for an unresolved fault, not a fix.
    //
    // Rejecting the whole candidate seed, rather than shuffling Cascade out of
    // the route's start slot, is what makes it survive a save reload:
    // reinitDeterministic() rebuilds the route from seed+attempt alone, so a
    // route we mutated at generation time would silently rebuild *unmutated* on
    // load and no longer match the placement stored in the sidecar.  A seed we
    // simply never accepted has no such problem.
    static bool startsInCascade() {
        return Randomizer::isKingdomRandoEnabled()
            && KingdomRando::isInitialized()
            && KingdomRando::getStartingWorldId() == WorldMeta::WorldId_Cascade;
    }

    bool runSoftSeedJob(const SeedGenerator::SoftSeedRequest& request) {
        // The run's surviving captures and abilities are logic input, not just a
        // runtime grant.  Without them the placement solves for a player holding
        // nothing and spends its opener reservations re-supplying gear the run
        // already kept, which is why rerolls opened so much narrower than they
        // needed to.  These masks are final: the reroll starts the new seed with
        // an empty board, so every reward it places is still out there to be
        // earned and nothing has to be granted after the fact.
        MoonRando::setCarriedStartingEquipment(
            request.retainedCaptures, request.retainedAbilities);

        u32 candidate = request.requestedSeed;
        for (s32 attempt = 0; attempt < 16; attempt++) {
            Randomizer::setSeed(candidate ? candidate : 1);
            // Solvability and a start kingdom that is not Cascade are the only
            // bars here.  The carried moon counts constrain nothing: they are a
            // number added on top of the new seed, not moons removed from it.
            if (Randomizer::reinit() && !startsInCascade()) {
                Randomizer::logPush(
                    "Soft Nuzlocke: generated seed=%08x attempt=%d carriedCaps=%016llx carriedAbils=%016llx",
                    candidate, attempt + 1,
                    static_cast<unsigned long long>(request.retainedCaptures),
                    static_cast<unsigned long long>(request.retainedAbilities));
                MoonRando::setCarriedStartingEquipment(0, 0);
                return true;
            }
            candidate = makeFreshSeed();
        }
        // The carry comes off *before* this reinit, not after.  This path
        // re-derives the run's existing tables from the seed number alone, so any
        // generation-time input that is not part of that number would rebuild the
        // old seed into a different placement than the one the player has spent
        // the run collecting against.
        Randomizer::setSeed(request.oldSeed);
        MoonRando::setCarriedStartingEquipment(0, 0);
        Randomizer::reinit();
        Randomizer::logPush("Soft Nuzlocke: generation failed; preserved old seed");
        return false;
    }

    bool isCaptureTemporarilyLocked(const GameDataFile* file,
        const CaptureRando::CaptureEntry& capture) {
        const s32 index = getCaptureIndex(&capture);
        return isSoftMode() && index >= 0 && (getLockMask(file, cCaptureLocksPrefix) & (1ull << index));
    }

    bool isCaptureExplicitlyRetained(const GameDataFile* file,
        const CaptureRando::CaptureEntry& capture) {
        const s32 index = getCaptureIndex(&capture);
        return isSoftMode() && index >= 0
            && (getLockMask(file, cCaptureRetainedPrefix) & (1ull << index));
    }

    bool isAbilityTemporarilyLocked(const GameDataFile* file,
        const AbilityRando::AbilityEntry& ability) {
        const s32 index = static_cast<s32>(ability.id);
        return isSoftMode() && index >= 0 && index < 64
            && (getLockMask(file, cAbilityLocksPrefix) & (1ull << index));
    }

    bool isAbilityExplicitlyRetained(const GameDataFile* file,
        const AbilityRando::AbilityEntry& ability) {
        const s32 index = static_cast<s32>(ability.id);
        return isSoftMode() && index >= 0 && index < 64
            && (getLockMask(file, cAbilityRetainedPrefix) & (1ull << index));
    }

    bool isPaintingTemporarilyLocked(const GameDataFile* file, s32 paintingIndex) {
        return isSoftMode() && paintingIndex >= 0 && paintingIndex < 64
            && (getLockMask(file, cPaintingLocksPrefix) & (1ull << paintingIndex));
    }

    bool isPaintingExplicitlyRetained(const GameDataFile* file, s32 paintingIndex) {
        return isSoftMode() && paintingIndex >= 0 && paintingIndex < 64
            && (getLockMask(file, cPaintingRetainedPrefix) & (1ull << paintingIndex));
    }

    void unlockCaptureForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon) {
        if (!file || !isSoftMode())
            return;
        u64 locks = getLockMask(file, cCaptureLocksPrefix);
        u64 retained = getLockMask(file, cCaptureRetainedPrefix);
        for (s32 i = 0; i < CaptureRando::getCaptureCountForMoon(moon); i++) {
            const s32 index = getCaptureIndex(CaptureRando::getCaptureForMoon(moon, i));
            if (index >= 0) {
                locks &= ~(1ull << index);
                retained |= 1ull << index;
            }
        }
        setLockMask(file, cCaptureLocksPrefix, locks);
        setLockMask(file, cCaptureRetainedPrefix, retained);
    }

    void unlockAbilityForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon) {
        if (!file || !isSoftMode())
            return;
        u64 locks = getLockMask(file, cAbilityLocksPrefix);
        u64 retained = getLockMask(file, cAbilityRetainedPrefix);
        for (s32 i = 0; i < AbilityRando::getAbilityCountForMoon(moon); i++) {
            const auto* ability = AbilityRando::getAbilityForMoon(moon, i);
            if (ability) {
                const s32 index = static_cast<s32>(ability->id);
                locks &= ~(1ull << index);
                retained |= 1ull << index;
            }
        }
        setLockMask(file, cAbilityLocksPrefix, locks);
        setLockMask(file, cAbilityRetainedPrefix, retained);
    }

    void unlockPaintingForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon) {
        if (!file || !isSoftMode())
            return;
        u64 locks = getLockMask(file, cPaintingLocksPrefix);
        u64 retained = getLockMask(file, cPaintingRetainedPrefix);
        for (s32 i = 0; i < PaintingRando::getCloudMoonPaintingUnlockCount(moon); i++) {
            const s32 index = PaintingRando::getCloudMoonPaintingUnlockIndex(moon, i);
            if (index >= 0) {
                locks &= ~(1ull << index);
                retained |= 1ull << index;
            }
        }
        setLockMask(file, cPaintingLocksPrefix, locks);
        setLockMask(file, cPaintingRetainedPrefix, retained);
    }
} // namespace Nuzlocke
