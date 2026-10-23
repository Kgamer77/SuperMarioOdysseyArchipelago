#include "Randomizer/CreditsRoll.h"

#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EntranceHooks.h"

#include "al/Library/Base/StringUtil.h"
#include "al/Library/Nerve/Nerve.h"
#include "al/Library/Nerve/NerveUtil.h"

#include "game/MapObj/ChangeStageInfo.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameSystem.h"

#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"
#include "nn/util.h"

#include <cstring>

namespace al {
    class IUseMessageSystem;
}

namespace CreditsRoll {
    namespace {

        // ---------------------------------------------------------------------
        // Retail 1.0.0 offsets.  Everything here is a plain main-module offset,
        // matching how the rest of the mod reaches un-exported game code.
        // ---------------------------------------------------------------------

        // StaffRollScene::initRollLineData() / ::updateRollLine()
        constexpr ptrdiff_t cInitRollLineDataOffset = 0x004c5fa8;
        constexpr ptrdiff_t cUpdateRollLineOffset = 0x004c693c;
        // HakoniwaStateDemoEnding::exeLoad().  Waiting until this nerve is
        // reached lets the ending state finish loading the Moon-world resources
        // StaffRollScene expects when the jump starts in another kingdom.
        constexpr ptrdiff_t cDemoEndingLoadOffset = 0x00511eb8;
        // HakoniwaStateDemoEnding's LoadStaffRoll nerve (.data): builds the
        // StaffRollScene on "StaffRollStage".  Everything before it in that
        // state only exists to play the ending demo first.
        constexpr ptrdiff_t cNrvDemoEndingLoadStaffRollOffset = 0x01d97e38;
        // StaffRollScene field offsets, from initRollLineData/updateRollLine.
        constexpr size_t cSceneLineCountOffset = 0x21a0; // s32
        constexpr size_t cSceneLineArrayOffset = 0x21a8; // RollLine**
        constexpr size_t cSceneStartOffsetOffset = 0x61b0; // f32
        constexpr size_t cSceneTotalLengthOffset = 0x61b4; // f32
        constexpr size_t cSceneScrollSpeedOffset = 0x61b8; // f32
        constexpr size_t cSceneScrollPosOffset = 0x61bc; // f32
        constexpr size_t cSceneRollPlayingFrameOffset = 0x623c; // s32
        // Per-line: cumulative Y of the line within the roll.
        constexpr size_t cLinePositionOffset = 0x110; // f32

        const al::Nerve* mainOffsetNerve(ptrdiff_t offset) {
            return reinterpret_cast<const al::Nerve*>(
                hk::ro::getMainModule()->range().start() + offset);
        }

        // ---------------------------------------------------------------------
        // Tuning
        // ---------------------------------------------------------------------

        // How much faster Nintendo's own roll runs than our sections.  The total
        // length of the roll is held at whatever the game asked for, so raising
        // this slows our sections down as well as speeding theirs up.
        constexpr float cNintendoSpeedMultiplier = 2.5f;
        // Sanity rails, in roll units per frame.  Vanilla sits near 1.0.
        constexpr float cMinScrollSpeed = 0.15f;
        constexpr float cMaxScrollSpeed = 6.0f;

        // ---------------------------------------------------------------------
        // Run summary
        // ---------------------------------------------------------------------

        // Written as a plain label line followed by a plain value line.  Using
        // the vanilla "@Role_x" pair would render the label above the value in
        // smaller type, but that needs extra message labels published into the
        // StaffRoll archive, and enumerating that archive is not something to
        // reach into until the rest of the roll is known good.
        constexpr const char16_t* cStatLabels[] = {
            u"Final Time",
            u"Moons Collected",
            u"Captures Unlocked",
            u"Abilities Unlocked",
        };

        // ---------------------------------------------------------------------
        // The roll itself.  A null name ends a section.
        // ---------------------------------------------------------------------

        struct Section {
            const char16_t* title;
            const char16_t* names[8];
        };

        constexpr Section cSections[] = {
            { u"CREATED AND DEVELOPED BY", { u"MaliciousMajora", u"SecretDev", nullptr } },
            { u"GRAPHIC DESIGN", { u"1tsAJ", u"BaconFearMC", nullptr } },
            { u"LOGIC DESIGN AND DEVELOPMENT", { u"MaliciousMajora", nullptr } },
            { u"LOGIC GRAPHS", { u"Karateman", u"GamesJam82", nullptr } },
            { u"IN-GAME TRACKER", { u"MaliciousMajora", nullptr } },
            { u"EXTERNAL TRACKER", { u"FireRising", u"Onijuiku", nullptr } },
            { u"MULTIPLAYER", { u"Onijuiku", u"MrKatzenGaming", u"egg_guy", u"Zyric02", u"MaliciousMajora", u"SecretDev", nullptr } },
            { u"SPECIAL THANKS",
                { u"CraftyBoss - Super Mario Odyssey Online", u"Sanae6 - SMO Online Server",
                    u"fruityloops1 - exlaunch", u"The Odyssey Decompilation Project", nullptr } },
        };
        constexpr s32 cSectionCount = sizeof(cSections) / sizeof(cSections[0]);

        // ---------------------------------------------------------------------
        // State
        // ---------------------------------------------------------------------

        // Vanilla's own text is ~7.5k characters; ours adds a few hundred.
        char16_t sRollBuffer[0x4000] = {};
        // Lines emitted ahead of the game's own text.  Doubles as the index of
        // the first vanilla line, which is where the roll changes speed.
        s32 sCustomLineCount = 0;

        float sSlowSpeed = 0.0f;
        float sFastSpeed = 0.0f;
        float sBoundaryY = -1.0f;
        bool sSpeedSplitReady = false;

        bool sJumpPending = false;
        bool sJumpArmed = false;

        // ---------------------------------------------------------------------
        // Helpers
        // ---------------------------------------------------------------------

        GameDataFile* tryGetGameDataFile() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;
            if (!al::isEqualString(system->mSequence->mName.cstr(), "HakoniwaSequence"))
                return nullptr;
            GameDataHolder* holder = ((HakoniwaSequence*)system->mSequence)->mGameDataHolderAccessor.mData;
            if (!holder)
                return nullptr;
            return holder->getGameDataFile();
        }

        GameDataHolder* tryGetGameDataHolder() {
            GameSystem* system = GameSystemFunction::getGameSystem();
            if (!system || !system->mSequence)
                return nullptr;
            if (!al::isEqualString(system->mSequence->mName.cstr(), "HakoniwaSequence"))
                return nullptr;
            return ((HakoniwaSequence*)system->mSequence)->mGameDataHolderAccessor.mData;
        }

        struct Writer {
            char16_t* out;
            s32 used;
            s32 capacity;
            s32 lines;

            void ch(char16_t c) {
                if (used + 1 < capacity)
                    out[used++] = c;
            }
            void str(const char16_t* s) {
                if (!s)
                    return;
                while (*s)
                    ch(*s++);
            }
            void ascii(const char* s) {
                if (!s)
                    return;
                while (*s)
                    ch(static_cast<char16_t>(*s++));
            }
            void newline() {
                ch(u'\n');
                lines++;
            }
        };

        void formatPlayTime(char* buf, s32 size, s64 centiseconds) {
            if (centiseconds < 0)
                centiseconds = 0;
            const s32 hours = static_cast<s32>(centiseconds / 360000);
            const s32 minutes = static_cast<s32>((centiseconds / 6000) % 60);
            const s32 seconds = static_cast<s32>((centiseconds / 100) % 60);
            nn::util::SNPrintf(buf, size, "%d:%02d:%02d", hours, minutes, seconds);
        }

        void writeStatLine(Writer& w, s32 index, const char* value) {
            w.str(cStatLabels[index]);
            w.newline();
            w.ascii(value);
            w.newline();
            w.newline();
        }

        void writeRunSummary(Writer& w) {
            GameDataFile* file = tryGetGameDataFile();
            GameDataHolder* holder = tryGetGameDataHolder();
            if (!file)
                return;

            w.str(u"@title YOUR RUN");
            w.newline();
            w.newline();

            char buf[64] = {};
            formatPlayTime(buf, sizeof(buf), file->getPlayTimeTotal());
            writeStatLine(w, 0, buf);

            nn::util::SNPrintf(buf, sizeof(buf), "%d", file->getTotalShineNum());
            writeStatLine(w, 1, buf);

            if (holder && CaptureRando::isInitialized()) {
                GameDataHolderAccessor accessor(holder);
                const s32 total = CaptureRando::getCaptureCount();
                s32 unlocked = 0;
                for (s32 i = 0; i < total; i++) {
                    const CaptureRando::CaptureEntry* entry = CaptureRando::getCapture(i);
                    if (entry && CaptureRando::isCaptureUnlocked(accessor, *entry))
                        unlocked++;
                }
                nn::util::SNPrintf(buf, sizeof(buf), "%d / %d", unlocked, total);
                writeStatLine(w, 2, buf);
            }

            if (holder && AbilityRando::isInitialized()) {
                GameDataHolderAccessor accessor(holder);
                const s32 total = AbilityRando::getDisplayAbilityCount();
                s32 unlocked = 0;
                for (s32 i = 0; i < total; i++) {
                    const AbilityRando::AbilityEntry* entry = AbilityRando::getDisplayAbility(i);
                    if (entry && AbilityRando::isAbilityUnlocked(accessor, *entry))
                        unlocked++;
                }
                nn::util::SNPrintf(buf, sizeof(buf), "%d / %d", unlocked, total);
                writeStatLine(w, 3, buf);
            }

            w.newline();
            w.newline();
        }

        void writeSections(Writer& w) {
            for (s32 s = 0; s < cSectionCount; s++) {
                w.str(u"@title ");
                w.str(cSections[s].title);
                w.newline();
                w.newline();
                for (s32 n = 0; n < 8 && cSections[s].names[n]; n++) {
                    w.str(cSections[s].names[n]);
                    w.newline();
                }
                w.newline();
                w.newline();
            }
        }

        // Builds our roll and appends the game's own text unchanged.
        const char16_t* buildRoll(const char16_t* vanilla) {
            Writer w { sRollBuffer, 0, static_cast<s32>(sizeof(sRollBuffer) / sizeof(char16_t)), 0 };

            writeRunSummary(w);
            writeSections(w);

            sCustomLineCount = w.lines;

            if (vanilla) {
                while (*vanilla && w.used + 1 < w.capacity)
                    w.out[w.used++] = *vanilla++;
            }
            w.out[w.used] = u'\0';
            return sRollBuffer;
        }

        bool isStaffRollArchive(const char* archive) {
            return archive && al::isEqualString(archive, "StaffRoll");
        }

        // ---------------------------------------------------------------------
        // Two-speed scroll
        // ---------------------------------------------------------------------

        static HkTrampoline<void, void*> initRollLineDataHook = hk::hook::trampoline([](void* scene) -> void {
            initRollLineDataHook.orig(scene);

            sSpeedSplitReady = false;
            sBoundaryY = -1.0f;
            if (!scene || sCustomLineCount <= 0)
                return;

            u8* base = reinterpret_cast<u8*>(scene);
            const s32 lineCount = *reinterpret_cast<s32*>(base + cSceneLineCountOffset);
            void** lines = *reinterpret_cast<void***>(base + cSceneLineArrayOffset);
            if (!lines || sCustomLineCount >= lineCount)
                return;

            void* firstVanillaLine = lines[sCustomLineCount];
            if (!firstVanillaLine)
                return;

            const float boundary = *reinterpret_cast<float*>(
                reinterpret_cast<u8*>(firstVanillaLine) + cLinePositionOffset);
            const float startOffset = *reinterpret_cast<float*>(base + cSceneStartOffsetOffset);
            const float totalLength = *reinterpret_cast<float*>(base + cSceneTotalLengthOffset);
            const s32 frames = *reinterpret_cast<s32*>(base + cSceneRollPlayingFrameOffset);
            if (frames <= 0 || totalLength <= 0.0f || boundary <= 0.0f || boundary >= totalLength)
                return;

            // The scene scrolls a fixed distance (startOffset + totalLength)
            // in a fixed number of frames.  Splitting that budget between a
            // slow pass over our lines and a pass k times faster over the
            // game's own leaves the roll exactly as long as it was.
            const float ours = startOffset + boundary;
            const float theirs = totalLength - boundary;
            const float slow = (ours + theirs / cNintendoSpeedMultiplier) / static_cast<float>(frames);
            if (slow < cMinScrollSpeed || slow > cMaxScrollSpeed)
                return;

            sSlowSpeed = slow;
            sFastSpeed = slow * cNintendoSpeedMultiplier;
            sBoundaryY = boundary;
            sSpeedSplitReady = true;
            *reinterpret_cast<float*>(base + cSceneScrollSpeedOffset) = sSlowSpeed;
        });

        static HkTrampoline<void, void*> updateRollLineHook = hk::hook::trampoline([](void* scene) -> void {
            if (scene && sSpeedSplitReady) {
                u8* base = reinterpret_cast<u8*>(scene);
                const float pos = *reinterpret_cast<float*>(base + cSceneScrollPosOffset);
                *reinterpret_cast<float*>(base + cSceneScrollSpeedOffset) = pos >= sBoundaryY ? sFastSpeed : sSlowSpeed;
            }
            updateRollLineHook.orig(scene);
        });

        // ---------------------------------------------------------------------
        // Jump to credits (temporary test aid)
        // ---------------------------------------------------------------------

        // exeDestroy has disposed of the gameplay scene by this point, and the
        // ending state has completed any Moon-world resource request selected by
        // appear(). Skip only EndingScene itself; jumping from appear() directly
        // to LoadStaffRoll leaves non-Moon resources resident and stalls the
        // StaffRollScene initialization behind a permanently black wipe.
        static HkTrampoline<void, void*> demoEndingLoadHook = hk::hook::trampoline([](void* state) -> void {
            if (!sJumpArmed || !state) {
                demoEndingLoadHook.orig(state);
                return;
            }
            sJumpArmed = false;
            al::setNerve(reinterpret_cast<al::IUseNerve*>(state),
                mainOffsetNerve(cNrvDemoEndingLoadStaffRollOffset));
        });

    } // namespace

    const char16_t* tryBuildMessage(const char* archive, const char* label,
        const char16_t* vanilla) {
        if (!isStaffRollArchive(archive) || !label
            || !al::isEqualString(label, "StaffRoll"))
            return nullptr;
        return buildRoll(vanilla);
    }

    void requestJumpToCredits() {
        if (!sJumpArmed)
            sJumpPending = true;
    }

    bool isJumpRequested() {
        return sJumpPending || sJumpArmed;
    }

    void updateSequence(HakoniwaSequence* sequence) {
        if (!sJumpPending || !sequence)
            return;

        GameDataHolder* holder = sequence->mGameDataHolderAccessor.mData;
        if (!holder)
            return;

        // Let exePlayStage take its authored DemoEndingStage branch.  Besides
        // selecting the correct destroy nerve, that branch prepares the save,
        // world-resource loader, audio situation and next-stage information the
        // ending state relies on.  Setting the nerve directly omitted those
        // prerequisites and left StaffRollScene waiting behind a black wipe.
        ChangeStageInfo info(holder, "", "DemoEndingStage", false, -1,
            ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
        info.mPlacementString.copy("");
        info.setWipeType("FadeBlack");

        EntranceHooks::bypassNextTransition();
        const bool changed = GameDataFunction::tryChangeNextStage(
            GameDataHolderWriter(holder), &info);
        if (!changed)
            EntranceHooks::resetBypass();

        sJumpPending = false;
        sJumpArmed = changed;
    }

    void installHooks() {
        initRollLineDataHook.installAtMainOffset(cInitRollLineDataOffset);
        updateRollLineHook.installAtMainOffset(cUpdateRollLineOffset);
        demoEndingLoadHook.installAtMainOffset(cDemoEndingLoadOffset);
    }

} // namespace CreditsRoll
