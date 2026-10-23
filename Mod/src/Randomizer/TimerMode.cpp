#include "Randomizer/TimerMode.h"

#include "Randomizer/HudTheme.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/TrackerOverlay.h"
#include "SD/SDHooks.h"
#include "ShineDB.h"

#include "al/Library/Controller/InputFunction.h"
#include "game/Player/PlayerActorHakoniwa.h"
#include "game/Player/PlayerHackKeeper.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/DemoUtil.h"

#include "imgui.h"
#include "nn/util.h"

#include <cstdlib>
#include <cstring>

namespace TimerMode {

    namespace {

        constexpr s32 cFramesPerSecond = 60;

        // The remaining clock rides in the save file's own hack dictionary, the
        // same mechanism the Nuzlocke inventory locks use.  That keeps it inside
        // the vanilla save (so it survives a quit exactly as far as the rest of
        // the player's progress does) without touching the sidecar's fixed-size
        // header/meta layout.
        constexpr const char* cRemainingPrefix = "RandoTimerFrames=";

        // The run's own age, on the same clock: how many frames the countdown
        // has actually been running for.  It rides alongside the remaining time
        // for the same reasons, and is what the award decay is measured against.
        // It is only persisted when that decay is armed - nothing else reads it.
        constexpr const char* cElapsedPrefix = "RandoTimerElapsed=";

        // How long the clock reads as "critical" and the HUD switches to its
        // warning treatment.
        constexpr s32 cWarnFrames = 10 * cFramesPerSecond;

        // Which run the live clock belongs to.  The seed is part of the identity
        // because GameDataFile is reused across loads: starting a fresh run in a
        // slot that was just played would otherwise inherit its clock.
        s32 sBoundSlot = -1;
        u32 sBoundSeed = 0;
        GameDataFile* sBoundFile = nullptr;

        s32 sRemainingFrames = -1;
        // Whole seconds last written into the dictionary; the record is only
        // rewritten when the displayed second actually changes.
        s32 sPersistedSeconds = -1;

        s32 sElapsedFrames = 0;
        s32 sPersistedElapsedSeconds = -1;

        bool sExpired = false;
        bool sDeathArmed = false;

        // Opening lock.  sMarioControlAvailable gates the chord on the run
        // actually having handed Mario over (Bingo::notifyMarioControlAvailable
        // does the same for the board), and sStarted is what releases the lock
        // and lets the clock run.  Both are session state: a reload re-arms the
        // lock, and the clock picks up from whatever the save carried.
        bool sMarioControlAvailable = false;
        bool sStarted = false;
        bool sStartChordHeld = false;
        bool sConsumedStart = false;

        // Purely cosmetic: frames left of the flash the HUD plays when the clock
        // changes outside its own countdown, and by how much.  Positive is a
        // moon buying time (green "+30s"), negative is a penalty taking it back
        // (red "-15s"); the two share one slot because they cannot land on the
        // same frame.
        s32 sAdjustFlashFrames = 0;
        s32 sAdjustFlashSeconds = 0;
        // Drives the start prompt's breathe. Advanced by draw(), so it is tied to
        // rendered frames rather than to the (still frozen) clock.
        s32 sPromptPulseFrames = 0;

        ImU32 applyAlpha(ImU32 color, float alpha) {
            if (alpha < 0.0f)
                alpha = 0.0f;
            if (alpha > 1.0f)
                alpha = 1.0f;
            const ImU32 baseAlpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
            const ImU32 scaledAlpha = static_cast<ImU32>(baseAlpha * alpha);
            return (color & ~(0xFFu << IM_COL32_A_SHIFT)) | (scaledAlpha << IM_COL32_A_SHIFT);
        }

        bool isActiveRun() {
            return Randomizer::isTimerRunMode() && SaveRedirect::getActiveSlot() >= 1;
        }

        // -1 when the save carries no such record at all, which is what marks a
        // slot as a run this mode has not written to yet.
        s32 readRecord(const GameDataFile* file, const char* prefix) {
            if (!file)
                return -1;
            const char* record = file->customFindHackDictionaryPrefix(prefix);
            if (!record)
                return -1;
            const s32 frames = std::atoi(record + std::strlen(prefix));
            return frames < 0 ? 0 : frames;
        }

        void writeRecord(GameDataFile* file, const char* prefix, s32 frames) {
            file->customRemoveHackDictionaryPrefix(prefix);
            char record[48];
            nn::util::SNPrintf(record, sizeof(record), "%s%d", prefix, frames);
            file->customAddHackDictionary(record);
        }

        void writePersisted(GameDataFile* file, s32 frames) {
            if (!file)
                return;
            writeRecord(file, cRemainingPrefix, frames);
            sPersistedSeconds = frames / cFramesPerSecond;
        }

        void writeElapsed(GameDataFile* file, s32 frames) {
            if (!file)
                return;
            writeRecord(file, cElapsedPrefix, frames);
            sPersistedElapsedSeconds = frames / cFramesPerSecond;
        }

        // ── award decay ───────────────────────────────────────────────────────
        // With a Half Life Point selected, what a pickup pays falls off
        // exponentially with the run's own clock time: at the half-life a moon
        // is worth half of what it was worth at the start, at twice that a
        // quarter, at three times an eighth.  The curve never reaches zero, and
        // the integer result is floored at one second so a long run cannot round
        // an award away entirely.
        //
        // 2^-x is evaluated in 16.16 fixed point rather than through powf: the
        // table is 2^(-k/16) for k = 0..16 and the value between two entries is
        // linearly interpolated.  That stays within 0.03% of the true curve -
        // a hundredth of a second on the largest award this mode pays - and
        // keeps what a moon is worth a pure integer function of the clock.
        constexpr u32 cHalfCurve[17] = {
            65536,
            62757,
            60097,
            57549,
            55109,
            52773,
            50535,
            48393,
            46341,
            44376,
            42495,
            40693,
            38968,
            37316,
            35734,
            34219,
            32768,
        };
        // Past this many whole half-lives every award this mode can pay is
        // already pinned to its one-second floor, and the shift below would be
        // undefined behaviour.
        constexpr s32 cMaxWholeHalvings = 24;

        s32 decayAward(s32 baseSeconds) {
            const s32 halfLifeSeconds = Randomizer::getTimerHalfLifeSeconds();
            if (halfLifeSeconds <= 0 || baseSeconds <= 0 || sElapsedFrames <= 0)
                return baseSeconds;

            const s32 halfLifeFrames = halfLifeSeconds * cFramesPerSecond;
            const s32 wholeHalvings = sElapsedFrames / halfLifeFrames;
            if (wholeHalvings >= cMaxWholeHalvings)
                return 1;

            // 2^-(rest / halfLife) for the part of the elapsed time that does not
            // make up a whole half-life; the whole ones are the shift below.
            const s32 rest = sElapsedFrames % halfLifeFrames;
            const s64 step = static_cast<s64>(rest) * 16;
            const s32 k = static_cast<s32>(step / halfLifeFrames);
            const s64 within = step % halfLifeFrames;
            const u32 hi = cHalfCurve[k];
            const u32 lo = cHalfCurve[k + 1];
            const s64 factor = static_cast<s64>(hi)
                - (static_cast<s64>(hi - lo) * within) / halfLifeFrames;

            // Rounded to the nearest second rather than truncated, so an award
            // the curve puts at 7.5s pays 8 instead of 7 and the decay tracks
            // the curve instead of sitting consistently under it.
            const s64 half = static_cast<s64>(1) << (15 + wholeHalvings);
            const s32 awarded = static_cast<s32>(
                (static_cast<s64>(baseSeconds) * factor + half) >> (16 + wholeHalvings));
            return awarded < 1 ? 1 : awarded;
        }

        // Establishes the live clock for `file`: an existing record resumes where
        // the run left off, and a save without one is a fresh Timer run that
        // starts on the configured opening time.
        void bind(GameDataFile* file, s32 slot) {
            sBoundFile = file;
            sBoundSlot = slot;
            sBoundSeed = Randomizer::getSeed();
            sExpired = false;
            sDeathArmed = false;
            sStarted = false;
            sStartChordHeld = false;
            sAdjustFlashFrames = 0;

            // Resumed the same way the countdown is, so reloading a run does not
            // reset the decay back to full-price moons.  A save from before the
            // dial existed carries no record and starts the run's age at zero,
            // which is also what a fresh run wants.
            const s32 storedElapsed = readRecord(file, cElapsedPrefix);
            sElapsedFrames = storedElapsed > 0 ? storedElapsed : 0;
            sPersistedElapsedSeconds = sElapsedFrames / cFramesPerSecond;

            const s32 stored = readRecord(file, cRemainingPrefix);
            if (stored >= 0) {
                sRemainingFrames = stored;
                sPersistedSeconds = stored / cFramesPerSecond;
                Randomizer::logPush("TimerMode: resumed slot=%d with %ds left",
                    slot, stored / cFramesPerSecond);
                return;
            }

            sRemainingFrames = Randomizer::getTimerStartSeconds() * cFramesPerSecond;
            writePersisted(file, sRemainingFrames);
            Randomizer::logPush("TimerMode: started slot=%d with %ds",
                slot, Randomizer::getTimerStartSeconds());
        }

        // The clock is frozen for anything that is not Mario actually playing:
        // the pause menu and every menu built on top of it, and any running demo
        // (moon-get sequences, story cutscenes, the capture intro).
        bool isClockRunning(StageScene* scene, PlayerActorHakoniwa* player) {
            if (!scene || !player || !scene->mIsAlive)
                return false;
            // The opening lock: nothing ticks until the player starts the run.
            if (!sStarted)
                return false;
            if (scene->isPause())
                return false;
            return !rs::isActiveDemo(player) && !rs::isActiveDemoPlayerPuppetable(player);
        }

        // A timeout is delivered as an ordinary Mario death so the whole vanilla
        // teardown (StageScene kill -> HakoniwaSequence destroy) runs untouched;
        // Nuzlocke's coordinator then takes over from the same place a hard
        // Nuzlocke death does.  A capture would otherwise swallow the kill, so
        // it is released first.
        void requestTimeoutDeath(PlayerActorHakoniwa* player, GameDataFile* file) {
            if (!player || !file)
                return;
            if (PlayerHackKeeper* hackKeeper = player->getPlayerHackKeeper()) {
                if (hackKeeper->isHack())
                    hackKeeper->cancelHack();
            }
            GameDataHolder* holder = file->getGameDataHolder();
            if (!holder)
                return;
            GameDataFunction::killPlayer(GameDataHolderWriter(holder));
        }

        // Both penalties land here.  A penalty never sets sExpired itself: it
        // floors the clock at zero and leaves the expiry (and the death it arms)
        // to update(), which already owns that transition and only takes it from
        // a live, unpaused, demo-free frame.  A death penalty that empties the
        // clock therefore ends the run on the frame the player next has control,
        // rather than stacking a second kill onto the one already in flight.
        void applyPenalty(s32 seconds, const char* reason) {
            // Nothing to take before the clock exists, once it has run out, or
            // while the opening lock still has the run frozen - a player reading
            // the HUD has not started paying for anything yet.
            if (seconds <= 0 || !isActiveRun() || sExpired || sRemainingFrames < 0 || !sStarted)
                return;

            sRemainingFrames -= seconds * cFramesPerSecond;
            if (sRemainingFrames < 0)
                sRemainingFrames = 0;

            sAdjustFlashSeconds = -seconds;
            sAdjustFlashFrames = 90;

            writePersisted(sBoundFile, sRemainingFrames);

            Randomizer::logPush("TimerMode: -%ds (%s) -> %ds left", seconds, reason,
                sRemainingFrames / cFramesPerSecond);
        }

        void formatClock(s32 frames, char* buf, size_t bufSize) {
            if (frames < 0)
                frames = 0;
            const s32 totalSeconds = frames / cFramesPerSecond;
            nn::util::SNPrintf(buf, bufSize, "%d:%02d", totalSeconds / 60, totalSeconds % 60);
        }

    } // namespace

    s32 getRemainingFrames() { return sRemainingFrames; }

    void reset() {
        sBoundSlot = -1;
        sBoundSeed = 0;
        sBoundFile = nullptr;
        sRemainingFrames = -1;
        sPersistedSeconds = -1;
        sElapsedFrames = 0;
        sPersistedElapsedSeconds = -1;
        sExpired = false;
        sDeathArmed = false;
        sMarioControlAvailable = false;
        sStarted = false;
        sStartChordHeld = false;
        sConsumedStart = false;
        sAdjustFlashFrames = 0;
        sAdjustFlashSeconds = 0;
    }

    void notifyMarioControlAvailable() {
        if (Randomizer::isTimerRunMode())
            sMarioControlAvailable = true;
    }

    void handleInput() {
        sConsumedStart = false;
        // Both the chord and the lock wait for the clock to be bound. handleInput
        // runs before the native sequence update and update() binds after it, so
        // the very first interactive frame can slip through unlocked; the frame
        // after it the lock is up. Offering the chord earlier is not an option -
        // bind() is the new-run signal and clears sStarted, which would swallow
        // a start pressed before it.
        if (!isActiveRun() || sRemainingFrames < 0 || sStarted || !sMarioControlAvailable)
            return;

        const bool chord = al::isPadHoldZL(-1) && al::isPadHoldZR(-1);
        const bool triggered = chord && !sStartChordHeld;
        sStartChordHeld = chord;

        // Same scoping as the board's lock: the input-gate hooks also service
        // pause-menu controls, so the chord only counts while the live stage HUD
        // is up and Mario input is really being handled.
        if (!triggered || !TrackerOverlay::isHudConditionActive())
            return;

        sConsumedStart = true;
        sStarted = true;
        Randomizer::logPush("TimerMode: started by player with %ds on the clock",
            sRemainingFrames / cFramesPerSecond);
    }

    bool shouldBlockGameplayInput() {
        if (!isActiveRun() || sRemainingFrames < 0 || !sMarioControlAvailable)
            return false;
        if (!TrackerOverlay::isHudConditionActive())
            return false;
        // The frame the chord lands still swallows its own buttons, so releasing
        // the lock never leaks a ZL/ZR press into gameplay.
        return !sStarted || sConsumedStart;
    }

    bool consumeStartTrigger() { return sConsumedStart; }

    bool isStarted() { return sStarted; }

    void update(StageScene* scene, PlayerActorHakoniwa* player, GameDataFile* file) {
        if (!isActiveRun() || !file) {
            if (sBoundSlot >= 0)
                reset();
            return;
        }

        const s32 slot = SaveRedirect::getActiveSlot();
        if (file != sBoundFile || slot != sBoundSlot || Randomizer::getSeed() != sBoundSeed)
            bind(file, slot);

        if (sAdjustFlashFrames > 0)
            sAdjustFlashFrames--;

        if (sExpired) {
            // Keep asking until the death actually registers.  killPlayer is
            // idempotent, and a frame where Mario cannot die yet (a demo taking
            // over, a capture still unwinding) simply retries on the next one.
            if (!sDeathArmed)
                requestTimeoutDeath(player, file);
            return;
        }

        if (!isClockRunning(scene, player))
            return;

        if (sRemainingFrames > 0)
            sRemainingFrames--;
        sElapsedFrames++;

        const s32 seconds = sRemainingFrames / cFramesPerSecond;
        if (seconds != sPersistedSeconds)
            writePersisted(file, sRemainingFrames);

        // Only the award decay reads the run's age back, so a run without it
        // never pays for the record.
        if (Randomizer::getTimerHalfLifeSeconds() > 0) {
            const s32 elapsedSeconds = sElapsedFrames / cFramesPerSecond;
            if (elapsedSeconds != sPersistedElapsedSeconds)
                writeElapsed(file, sElapsedFrames);
        }

        if (sRemainingFrames <= 0) {
            sRemainingFrames = 0;
            sExpired = true;
            Randomizer::logPush("TimerMode: clock expired, ending the run");
            requestTimeoutDeath(player, file);
        }
    }

    void awardMoon(const ShineDB::ShineEntry* item, bool alreadyCollected) {
        // Nothing to buy time with before the clock exists, once it has run out,
        // or on a location whose reward was already banked.
        if (!isActiveRun() || alreadyCollected || sExpired || sRemainingFrames < 0 || !item)
            return;

        // A triple pays the triple bonus INSTEAD of the per-moon one, not on top
        // of it.  isRewardMultiEntry honors "Randomize Triple Moons", so the
        // award follows what the pickup actually paid out.
        const bool isTriple = MoonRando::isRewardMultiEntry(item);
        const s32 baseSeconds = isTriple ? Randomizer::getTimerTripleBonusSeconds()
                                         : Randomizer::getTimerMoonBonusSeconds();
        // Both bonuses are worth what the Half Life Point says they are worth
        // this far into the run; with the dial disabled this is the flat value.
        const s32 bonusSeconds = decayAward(baseSeconds);

        sRemainingFrames += bonusSeconds * cFramesPerSecond;
        sAdjustFlashSeconds = bonusSeconds;
        sAdjustFlashFrames = 90;

        writePersisted(sBoundFile, sRemainingFrames);

        Randomizer::logPush("TimerMode: +%ds (%s, base %ds, age %ds) -> %ds left",
            bonusSeconds, isTriple ? "triple" : "moon", baseSeconds,
            sElapsedFrames / cFramesPerSecond, sRemainingFrames / cFramesPerSecond);
    }

    void notifyPlayerDamage() {
        applyPenalty(Randomizer::getTimerDamagePenaltySeconds(), "damage");
    }

    void notifyPlayerDeath() {
        // The mode's own timeout is delivered as an ordinary Mario death, so it
        // arrives on this same signal.  applyPenalty declines once the clock has
        // expired, which is exactly that case - the run is already over and
        // there is nothing left to charge.
        applyPenalty(Randomizer::getTimerDeathPenaltySeconds(), "death");
    }

    bool isTimeoutDeathPending() { return sExpired && !sDeathArmed; }

    void notifyDeathArmed() { sDeathArmed = true; }

    void draw() {
        if (!isActiveRun() || sRemainingFrames < 0)
            return;

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
            return;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        if (!dl || !font)
            return;

        const float scale = TrackerOverlay::getHudScale();
        const auto S = [scale](float v) { return v * scale; };
        const HudTheme::Palette theme = HudTheme::get();

        char clock[16];
        formatClock(sRemainingFrames, clock, sizeof(clock));

        // While the opening lock is up the panel also carries the prompt, and
        // has to be wide enough for whichever of the two lines is longer.
        const bool waitingToStart = !sStarted && sMarioControlAvailable;
        static constexpr const char* cStartPrompt = "Hold ZL + ZR to start";

        const float clockSize = S(38.0f);
        const float promptSize = S(17.0f);
        const ImVec2 clockExtent = font->CalcTextSizeA(clockSize, 3.402823e38F, 0.0f, clock);
        const ImVec2 promptExtent = waitingToStart
            ? font->CalcTextSizeA(promptSize, 3.402823e38F, 0.0f, cStartPrompt)
            : ImVec2(0.0f, 0.0f);

        const float padX = S(18.0f);
        const float padY = S(9.0f);
        const float contentW = clockExtent.x > promptExtent.x ? clockExtent.x : promptExtent.x;
        const float panelW = contentW + padX * 2.0f;
        const float panelH = clockExtent.y + padY * 2.0f
            + (waitingToStart ? promptExtent.y + S(4.0f) : 0.0f);
        const float panelX = (io.DisplaySize.x - panelW) * 0.5f;
        const float panelY = S(24.0f);

        // The last ten seconds pulse red so the run's end is unmissable even
        // with the HUD in the corner of the player's eye.  A run that has not
        // been started yet is never critical, however little is on the clock -
        // it is not counting down, and the panel is showing a prompt instead.
        const bool critical = sStarted && sRemainingFrames <= cWarnFrames;
        float pulse = 0.0f;
        if (critical) {
            const s32 phase = sRemainingFrames % cFramesPerSecond;
            pulse = 1.0f - static_cast<float>(phase) / static_cast<float>(cFramesPerSecond);
        }

        const ImU32 panelCol = critical
            ? IM_COL32(40, 8, 10, static_cast<s32>(200 + 40 * pulse))
            : IM_COL32(8, 10, 16, 216);
        const ImU32 borderCol = critical
            ? IM_COL32(255, 86, 86, static_cast<s32>(150 + 105 * pulse))
            : theme.border;
        const ImU32 clockCol = critical ? IM_COL32(255, 140, 130, 255) : IM_COL32(255, 255, 255, 255);

        dl->AddRectFilled(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
            panelCol, S(10.0f));
        dl->AddRect(ImVec2(panelX, panelY), ImVec2(panelX + panelW, panelY + panelH),
            borderCol, S(10.0f), 0, S(2.0f));

        // Both lines are centred on the panel so the clock does not jump
        // sideways when the prompt disappears on start.
        const float centerX = panelX + panelW * 0.5f;
        const float clockX = centerX - clockExtent.x * 0.5f;
        const float clockY = panelY + padY;
        dl->AddText(font, clockSize, ImVec2(clockX + S(2.0f), clockY + S(2.0f)),
            IM_COL32(0, 0, 0, 200), clock);
        dl->AddText(font, clockSize, ImVec2(clockX, clockY), clockCol, clock);

        if (waitingToStart) {
            // Breathes rather than blinks: it sits under a static clock, so a
            // hard flash would read as the timer doing something.
            sPromptPulseFrames = (sPromptPulseFrames + 1) % 120;
            const s32 phase = sPromptPulseFrames < 60 ? sPromptPulseFrames
                                                      : 120 - sPromptPulseFrames;
            const float breathe = 0.72f + 0.28f * static_cast<float>(phase) / 60.0f;
            const float promptX = centerX - promptExtent.x * 0.5f;
            const float promptY = clockY + clockExtent.y + S(4.0f);
            dl->AddText(font, promptSize, ImVec2(promptX + S(1.0f), promptY + S(1.0f)),
                IM_COL32(0, 0, 0, 200), cStartPrompt);
            dl->AddText(font, promptSize, ImVec2(promptX, promptY),
                applyAlpha(theme.heading, breathe), cStartPrompt);
        }

        if (sAdjustFlashFrames > 0 && sAdjustFlashSeconds != 0) {
            const bool isPenalty = sAdjustFlashSeconds < 0;
            char adjust[16];
            nn::util::SNPrintf(adjust, sizeof(adjust), isPenalty ? "-%ds" : "+%ds",
                isPenalty ? -sAdjustFlashSeconds : sAdjustFlashSeconds);
            const float adjustSize = S(22.0f);
            const ImVec2 adjustExtent = font->CalcTextSizeA(adjustSize, 3.402823e38F, 0.0f, adjust);
            // Rises and fades out of the panel rather than sitting next to it, so
            // it never widens the HUD's footprint.
            const float t = static_cast<float>(sAdjustFlashFrames) / 90.0f;
            const float adjustX = panelX + panelW * 0.5f - adjustExtent.x * 0.5f;
            const float adjustY = panelY + panelH + S(4.0f) - S(14.0f) * (1.0f - t);
            const s32 adjustAlpha = static_cast<s32>(255.0f * (t > 0.5f ? 1.0f : t * 2.0f));
            // Red for a penalty against the same green the awards use, so the
            // direction reads before the number does.
            const ImU32 adjustCol = isPenalty
                ? IM_COL32(255, 108, 104, adjustAlpha)
                : IM_COL32(126, 226, 132, adjustAlpha);
            dl->AddText(font, adjustSize, ImVec2(adjustX + S(1.0f), adjustY + S(1.0f)),
                IM_COL32(0, 0, 0, adjustAlpha * 200 / 255), adjust);
            dl->AddText(font, adjustSize, ImVec2(adjustX, adjustY), adjustCol, adjust);
        }
    }

} // namespace TimerMode
