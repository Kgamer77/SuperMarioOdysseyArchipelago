#include "Randomizer/TrackerOverlay.h"
#include "ShineDB.h"

#include "Randomizer/AbilityRando.h"
#include "Randomizer/BingoBoard.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/HudTheme.h"
#include "Randomizer/KingdomProgress.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/WorldMeta.h"

#include "helpers.hpp"

#include "Scene/StageScene.h"
#include "Sequence/HakoniwaSequence.h"
#include "System/GameSystem.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Library/System/GameSystemInfo.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"

#include "imgui.h"
#include "nn/util.h"

#include <basis/seadTypes.h>

namespace TrackerOverlay {

    namespace {

        namespace LD = Randomizer::LogicData;

        struct TrackedWorld {
            s32 worldId;
            const char* icon;
            const char* name;
            ImU32 color;
        };

        static const TrackedWorld kWorlds[] = {
            { WorldMeta::WorldId_Cap, "\xee\xa4\x84", "Cap", IM_COL32(255, 245, 0, 255) },
            { WorldMeta::WorldId_Cascade, "\xee\xa4\x86", "Cascade", IM_COL32(255, 153, 0, 255) },
            { WorldMeta::WorldId_Sand, "\xee\xa4\x9d", "Sand", IM_COL32(139, 241, 44, 255) },
            { WorldMeta::WorldId_Lake, "\xee\xa4\x8e", "Lake", IM_COL32(228, 121, 171, 255) },
            { WorldMeta::WorldId_Wooded, "\xee\xa4\xa7", "Wooded", IM_COL32(30, 101, 231, 255) },
            { WorldMeta::WorldId_Cloud, "\xee\xa4\x87", "Cloud", IM_COL32(255, 255, 255, 255) },
            { WorldMeta::WorldId_Lost, "\xee\xa4\x90", "Lost", IM_COL32(231, 30, 221, 255) },
            { WorldMeta::WorldId_Metro, "\xee\xa4\x92", "Metro", IM_COL32(222, 125, 94, 255) },
            { WorldMeta::WorldId_Snow, "\xee\xa4\xa0", "Snow", IM_COL32(231, 147, 10, 255) },
            { WorldMeta::WorldId_Seaside, "\xee\xa4\x9f", "Seaside", IM_COL32(179, 111, 233, 255) },
            { WorldMeta::WorldId_Luncheon, "\xee\xa4\x91", "Luncheon", IM_COL32(63, 221, 187, 255) },
            { WorldMeta::WorldId_Ruined, "\xee\xa4\x9c", "Ruined", IM_COL32(255, 215, 226, 255) },
            { WorldMeta::WorldId_Bowsers, "\xee\xa4\x83", "Bowser's", IM_COL32(221, 48, 76, 255) },
            { WorldMeta::WorldId_Moon, "\xee\xa4\x93", "Moon", IM_COL32(181, 193, 203, 255) },
            { WorldMeta::WorldId_Mushroom, "\xee\xa4\x95", "Mushroom", IM_COL32(245, 246, 114, 255) },
            { WorldMeta::WorldId_Dark, "\xee\xa4\x89", "Dark Side", IM_COL32(255, 242, 198, 255) },
            { WorldMeta::WorldId_Darker, "\xee\xa4\x88", "Darker Side", IM_COL32(255, 242, 198, 255) },
        };
        static constexpr s32 kWorldCount = sizeof(kWorlds) / sizeof(kWorlds[0]);

        s32 getSavedRequirement(GameDataFile* file, s32 logicalWorldId) {
            if (const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, logicalWorldId))
                return entry->requirement.moonCount;
            return RequirementRando::getRequirement(logicalWorldId);
        }

        // True if a kingdom could have a moon requirement under the current
        // settings, even if its randomized requirement came out 0. Every one of
        // the seventeen is opt-in on the Kingdom Moons page now, so this asks
        // RequirementRando rather than naming kingdoms - it is the same call
        // KingdomProgress fills the saved requirement from, which is what keeps
        // the tracker, the globe and the generator telling one story. A kingdom
        // set to Coins has no requirement and shows none.
        bool hasPossibleRequirement(s32 worldId) {
            return RequirementRando::isRequirementKingdom(worldId);
        }

        // Vanilla only gives these three no Moon Rock (no boss/world-peace mechanic
        // there to gate one), unlike every other tracked kingdom.
        bool hasMoonRock(s32 worldId) {
            return worldId != WorldMeta::WorldId_Mushroom
                && worldId != WorldMeta::WorldId_Dark
                && worldId != WorldMeta::WorldId_Darker;
        }

        // Captures commonly used to traverse the Moon Kingdom underground to the
        // finale. Curated prototype list — adjust as the "needed" set is finalised.
        struct TrackedCapture {
            const char* hack;
            const char* label;
            const char* icon;
        };

        static const TrackedCapture kCaveCaptures[] = {
            { "KaronWing", "Parabones", "\xee\xa4\x97" },
            { "KillerMagnum", "Banzai Bill", "\xee\xa4\x80" },
            { "ElectricWire", "Spark Pylon", "\xee\xa4\xa1" },
            { "BreedaWanwan", "Broode's Chain Chomp", "\xee\xa4\x8b" },
        };
        static constexpr s32 kCaveCaptureCount = sizeof(kCaveCaptures) / sizeof(kCaveCaptures[0]);

        static const TrackedCapture kFinalBossCaptures[] = {
            { "Koopa", "Bowser", "\xee\xa4\x82" },
        };
        static constexpr s32 kFinalBossCaptureCount = sizeof(kFinalBossCaptures) / sizeof(kFinalBossCaptures[0]);

        constexpr ImU32 kColText = IM_COL32(255, 255, 255, 255);

        // Door-kind glyphs from the expanded IcoMoon font, each with the colour
        // it is drawn in. Keep this mapping on SubareaKeyDB's semantic type
        // rather than on entrance names: entrance randomization changes
        // destinations, while a key always gates the kind of door on the
        // overworld/home side.
        //
        // The colour, not the silhouette, is what identifies a kind at row size:
        // the pipe and Moon Pipe glyphs are the same pipe mouth, and the open
        // passage and beanstalk glyphs the same plain cylinder, so hue carries
        // the identity the shape cannot. Doors are red and the scarecrow rack is
        // the wood it is built from. An open passage is a hole in the terrain
        // rather than an object with a colour of its own, so it takes the panel's
        // own default - white when owned, the same dimmed grey every other locked
        // glyph on the row uses.
        //
        // Owned and locked are authored as a pair rather than derived: a locked
        // kind still has to read as *which* kind, so locked darkens the same hue
        // instead of washing out to a shared grey.
        struct SubareaTypeStyle {
            const char* icon;
            ImU32 owned;
            ImU32 locked;
        };

        SubareaTypeStyle getSubareaTypeStyle(SubareaKeyDB::SubareaType type) {
            switch (type) {
            case EntranceDB2::SubareaType_WarpPipe: // E919 pipe
                return { "\xee\xa4\x99", IM_COL32(35, 139, 42, 255),
                    IM_COL32(28, 79, 31, 255) };
            case EntranceDB2::SubareaType_MoonPipe: // E914 moonpipe
                return { "\xee\xa4\x94", IM_COL32(133, 117, 118, 255),
                    IM_COL32(77, 70, 71, 255) };
            // Locked is an opaque dark grey rather than the half-alpha dim the
            // lock/Bowser/key glyphs on the same row use. Those are outlines; this
            // one is a solid block, so at the same alpha it covers enough area to
            // come out brighter than an *owned* Moon Pipe and invert the signal.
            case EntranceDB2::SubareaType_OpenPassage: // E916 otherzone
                return { "\xee\xa4\x96", kColText, IM_COL32(96, 101, 112, 255) };
            case EntranceDB2::SubareaType_Doorway: // E90A door
                return { "\xee\xa4\x8a", IM_COL32(163, 31, 33, 255),
                    IM_COL32(94, 28, 29, 255) };
            // Scarecrow door. DoorAreaChangeCap is the only unit in the game
            // that owns a Links.CapRack, and every one of its five placements
            // sits ~200-320 units from one, so this type is exactly the rack you
            // hang Cappy on before a Cappy-less room - not a door Cappy opens.
            // The E91E rack glyph says that; the E905 cap-on-a-door glyph read as
            // just another door, which is also the shape it collides with.
            case EntranceDB2::SubareaType_CappyDoor: // E91E scarecrowdoor
                return { "\xee\xa4\x9e", IM_COL32(201, 138, 69, 255),
                    IM_COL32(96, 66, 31, 255) };
            // Wooded and Bowser's carry a beanstalk *and* a warp pipe, so the two
            // greens have to stay apart: the stalk is a bright yellow-green
            // against the pipe's deep green, and locked drops well below the
            // pipe's owned value rather than landing beside it.
            case EntranceDB2::SubareaType_Beanstalk: // E925 vine
                return { "\xee\xa4\xa5", IM_COL32(168, 220, 62, 255),
                    IM_COL32(68, 89, 28, 255) };
            default:
                return { nullptr, 0, 0 };
            }
        }

        // Per Kingdom mode collapses every door kind behind a single key, so that
        // row has no kind to colour. It gets its own gold rather than the white
        // the Moon Rock Key column uses: the two keys sit on the same row and
        // would otherwise be the same glyph in the same colour twice.
        constexpr ImU32 kColSubareaKeyOwned = IM_COL32(240, 198, 88, 255);
        constexpr ImU32 kColSubareaKeyLocked = IM_COL32(109, 85, 32, 255);

        // Whether a glyph exists is an ASSIGNMENT question, not a data one.
        // SubareaKeyDB::kingdomHasType only says the kingdom has doors of that
        // kind; whether a key was actually minted for them depends on the seed.
        // MoonRando::hasSubareaKey deliberately falls open when it cannot tell,
        // so keying an icon off the static matrix alone would draw an owned
        // (green) key for a pair that has no carrier at all - which is exactly
        // how Mushroom and Dark Side used to read.
        bool hasAssignedSubareaKey(s32 kingdom, SubareaKeyDB::SubareaType type) {
            return MoonRando::hasSubareaKeyAssignment(kingdom, static_cast<s32>(type));
        }

        bool kingdomHasAssignedSubareaKey(s32 kingdom) {
            for (s32 i = 0; i < SubareaKeyDB::sGatedTypeCount; i++) {
                if (hasAssignedSubareaKey(kingdom, SubareaKeyDB::sGatedTypes[i]))
                    return true;
            }
            return false;
        }

        constexpr ImU32 kColPanel = IM_COL32(8, 10, 16, 160);
        constexpr ImU32 kColDim = IM_COL32(150, 156, 170, 255);
        // Border and heading colors are run-mode dependent; see HudTheme::get().
        constexpr ImU32 kColOn = IM_COL32(90, 210, 110, 255); // peace / capture owned
        constexpr ImU32 kColOff = IM_COL32(60, 64, 74, 255);
        constexpr ImU32 kColCubeOn = IM_COL32(245, 205, 70, 255);
        constexpr ImU32 kColTime = IM_COL32(140, 205, 255, 255); // playtime accent
        // +Hints mode: per-kingdom "what's worth doing here" dot.
        constexpr ImU32 kColHintRed = IM_COL32(215, 70, 70, 255);
        constexpr ImU32 kColHintOrange = IM_COL32(235, 155, 60, 255);
        constexpr ImU32 kColHintGreen = IM_COL32(90, 210, 110, 255);
        constexpr ImU32 kColHintUnavailable = IM_COL32(90, 94, 104, 255);
        // Moon Limits: "inside the possible requirement range" (Full mode only).
        constexpr ImU32 kColLimitMid = IM_COL32(240, 205, 70, 255);

        float sVisibilityAlpha = 0.0f;
        float sLastPanelBottom = -1.0f;

        // Collecting a moon is the one tracker value that changes mid-frame while
        // the player is watching, and a randomized moon is often credited to a
        // kingdom other than the one they are standing in. Flashing the row that
        // moved is what makes that legible.
        constexpr ImU32 kColRowFlash = IM_COL32(255, 236, 140, 255);
        constexpr float kRowFlashSeconds = 1.1f;
        s32 sLastShineNum[kWorldCount] = {};
        float sRowFlash[kWorldCount] = {};
        bool sShineSnapshotValid = false;

        void resetRowFlash() {
            sShineSnapshotValid = false;
            for (s32 i = 0; i < kWorldCount; i++)
                sRowFlash[i] = 0.0f;
        }

        // Must run every frame the file is reachable, including while the panel is
        // faded out: the count ticks up before the moon-get cutscene has finished
        // fading the tracker back in, and a transition missed there never flashes.
        void updateRowFlash(GameDataFile* file, float deltaTime) {
            const float decay = deltaTime / kRowFlashSeconds;
            for (s32 i = 0; i < kWorldCount; i++) {
                const s32 have = file->getShineNum(kWorlds[i].worldId);
                // A jump larger than a grand moon is a wholesale rebuild (save
                // load, Nuzlocke reroll), not a pickup, so it must not flash.
                if (sShineSnapshotValid && have > sLastShineNum[i]
                    && have - sLastShineNum[i] <= 3)
                    sRowFlash[i] = 1.0f;
                else if (sRowFlash[i] > 0.0f)
                    sRowFlash[i] = sRowFlash[i] > decay ? sRowFlash[i] - decay : 0.0f;
                sLastShineNum[i] = have;
            }
            sShineSnapshotValid = true;
        }

        ImU32 applyAlpha(ImU32 color, float alpha) {
            const ImU32 baseAlpha = (color >> IM_COL32_A_SHIFT) & 0xFFu;
            const ImU32 scaledAlpha = static_cast<ImU32>(baseAlpha * alpha);
            return (color & ~(0xFFu << IM_COL32_A_SHIFT))
                | (scaledAlpha << IM_COL32_A_SHIFT);
        }

        float approach(float value, float target, float amount) {
            if (value < target) {
                value += amount;
                return value < target ? value : target;
            }
            if (value > target) {
                value -= amount;
                return value > target ? value : target;
            }
            return value;
        }

        bool isKingdomMoonCounterVisible(const StageScene* scene) {
            if (!scene || !scene->stageSceneLayout || !scene->stageSceneLayout->mShineCountLyt)
                return false;

            // ShineCounter derives from LayoutActor. It remains alive during its
            // End animation, so begin our fade as soon as that action starts.
            const al::LayoutActor* counter = reinterpret_cast<const al::LayoutActor*>(scene->stageSceneLayout->mShineCountLyt);
            return counter->isAlive() && !al::isActionPlaying(counter, "End");
        }

        bool isNerveStateActive(const void* state) {
            return state
                && !reinterpret_cast<const al::NerveStateBase*>(state)->isDead();
        }

        bool isMoonGetCutsceneActive(const StageScene* scene) {
            return scene
                && (isNerveStateActive(scene->mStateGetShine)
                    || isNerveStateActive(scene->mStateGetShineMain)
                    || isNerveStateActive(scene->mStateGetShineGrand)
                    || isNerveStateActive(scene->mStateGetShineMainSandWorld));
        }

        bool isCaptureOwned(GameDataHolderAccessor accessor, GameDataFile* file,
            bool captureRando, const TrackedCapture& capture) {
            return captureRando
                ? CaptureRando::isCaptureInGallery(accessor, capture.hack)
                : file->isExistInHackDictionary(capture.hack);
        }

        void addText(ImDrawList* dl, ImFont* font, float size, float x, float y, ImU32 col, const char* text) {
            dl->AddText(font, size, ImVec2(x, y), col, text);
        }

        void addTextRight(ImDrawList* dl, ImFont* font, float size, float rightX, float y, ImU32 col, const char* text) {
            const ImVec2 ts = font->CalcTextSizeA(size, 3.402823e38F, 0.0f, text);
            dl->AddText(font, size, ImVec2(rightX - ts.x, y), col, text);
        }

        void addTextCentered(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImU32 col, const char* text) {
            const ImVec2 ts = font->CalcTextSizeA(size, 3.402823e38F, 0.0f, text);
            dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f + 1.0f, y + 1.0f), applyAlpha(IM_COL32_BLACK, sVisibilityAlpha * 0.8f), text);
            dl->AddText(font, size, ImVec2(cx - ts.x * 0.5f, y), col, text);
        }

        void addIconCentered(ImDrawList* dl, ImFont* font, float size, float cx, float y, ImU32 col, const char* icon) {
            dl->AddText(font, size, ImVec2(cx - size * 0.5f + 1.0f, y + 1.0f), applyAlpha(IM_COL32_BLACK, sVisibilityAlpha * 0.8f), icon);
            dl->AddText(font, size, ImVec2(cx - size * 0.5f, y), col, icon);
        }

        // Door-kind glyph in its authored colour. Owned briefly also drew an
        // oversized copy of itself behind as a bloom; that works for a glyph with
        // internal detail but several of these are solid blocks, and a 1.3x solid
        // block reads as a second hard-edged rectangle framing the first rather
        // than as a glow. Owned/locked is carried by the colour pair instead.
        void addSubareaIcon(ImDrawList* dl, ImFont* font, float size, float cx, float y,
            ImU32 ownedColor, ImU32 lockedColor, bool owned, const char* icon) {
            addIconCentered(dl, font, size, cx, y,
                applyAlpha(owned ? ownedColor : lockedColor, sVisibilityAlpha), icon);
        }

        void addTextCol(ImDrawList* dl, ImFont* font, float size, float leftX, float y, ImU32 col, const char* text) {
            dl->AddText(font, size, ImVec2(leftX + 1.0f, y + 1.0f), applyAlpha(IM_COL32_BLACK, sVisibilityAlpha * 0.8f), text);
            dl->AddText(font, size, ImVec2(leftX, y), col, text);
        }

        // GameDataFile::getPlayTimeTotal() counts one tick per frame the engine's
        // own StageScene::control() runs, which it already skips while paused; it
        // also lives on the save file itself, so it survives stage loads without
        // resetting. The engine ticks at 60Hz, not 100Hz, so hundredths below are
        // that tick counter rescaled to a 1/100s unit -- a smooth, live-updating
        // display, not a lab-grade frame count.
        void formatPlayTime(s64 frames, char* buf, size_t bufSize) {
            const s64 totalCentiseconds = (frames > 0 ? frames : 0) * 100 / 60;
            const s32 hours = static_cast<s32>(totalCentiseconds / 360000);
            const s32 minutes = static_cast<s32>((totalCentiseconds / 6000) % 60);
            const s32 seconds = static_cast<s32>((totalCentiseconds / 100) % 60);
            const s32 centiseconds = static_cast<s32>(totalCentiseconds % 100);
            if (hours > 0)
                nn::util::SNPrintf(buf, bufSize, "%d:%02d:%02d:%02d", hours, minutes, seconds, centiseconds);
            else
                nn::util::SNPrintf(buf, bufSize, "%d:%02d:%02d", minutes, seconds, centiseconds);
        }

    } // namespace

    WorldStyle getWorldStyle(s32 logicalWorldId) {
        for (s32 i = 0; i < kWorldCount; i++) {
            if (kWorlds[i].worldId == logicalWorldId)
                return { kWorlds[i].icon, kWorlds[i].name,
                    static_cast<u32>(kWorlds[i].color) };
        }
        return { nullptr, nullptr, 0 };
    }

    float getHudScale() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        const al::DrawSystemInfo* drawSystemInfo = system && system->mSystemInfo
            ? system->mSystemInfo->drawSystemInfo
            : nullptr;
        return drawSystemInfo && !drawSystemInfo->isDocked
            ? (720.0f / 900.0f)
            : 1.0f;
    }

    float getPanelTop() {
        return 216.0f * getHudScale();
    }

    float getLastPanelBottom() {
        return sLastPanelBottom;
    }

    bool isHudConditionActive() {
        StageScene* scene = getStageScene();
        if (scene && StartPositionRando::shouldHideOpeningHud(scene->mStageName.cstr()))
            return false;
        return isKingdomMoonCounterVisible(scene)
            || isMoonGetCutsceneActive(scene);
    }

    void draw() {
        if (Bingo::shouldSuppressTracker()) {
            sVisibilityAlpha = 0.0f;
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }
        if (!Randomizer::isTrackerOverlayEnabled()) {
            sVisibilityAlpha = 0.0f;
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }

        StageScene* scene = getStageScene();
        if (!scene) {
            sVisibilityAlpha = 0.0f;
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }
        if (StartPositionRando::shouldHideOpeningHud(scene->mStageName.cstr())) {
            sVisibilityAlpha = 0.0f;
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }

        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f) {
            sLastPanelBottom = -1.0f;
            return;
        }

        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence) {
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }

        HakoniwaSequence* sequence = static_cast<HakoniwaSequence*>(system->mSequence);
        GameDataHolderAccessor accessor = sequence->mGameDataHolderAccessor;
        if (!accessor.mData) {
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }

        GameDataFile* file = accessor->getGameDataFile();
        if (!file) {
            sLastPanelBottom = -1.0f;
            resetRowFlash();
            return;
        }

        // The save is resolved before the visibility gate purely so the moon-count
        // snapshot keeps ticking while the panel is hidden.
        const float deltaTime = io.DeltaTime > 0.0f ? io.DeltaTime : (1.0f / 60.0f);
        updateRowFlash(file, deltaTime);

        const bool shouldShow = isHudConditionActive();
        const float targetAlpha = shouldShow ? 1.0f : 0.0f;
        sVisibilityAlpha = approach(sVisibilityAlpha, targetAlpha, deltaTime * 5.0f);
        if (sVisibilityAlpha <= 0.001f) {
            sLastPanelBottom = -1.0f;
            return;
        }

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();

        // Every position/size below is a literal pixel value tuned by eye against
        // docked output, so it renders too large in handheld without a rescale.
        // io.DisplaySize isn't a reliable basis for that scale - it reflects
        // whatever the emulator's internal render/resolution-scale setting
        // produces, not the console's native docked-vs-handheld ratio. mSystemInfo
        // ->drawSystemInfo->isDocked is the actual console-mode flag, so branch on
        // that instead. Odyssey's real native render resolution is 900p docked /
        // 720p handheld (not the console's max output of 1080p/720p), so the
        // correct ratio is 720/900. Docked stays exactly 1.0 (unscaled).
        //
        // S() scales a literal at the point of use rather than uniformly
        // rescaling the finished vertex buffer after the fact (an earlier version
        // did that; some glyph quads came out degenerate at non-1:1 scale - text
        // silently vanished while its row's space was still reserved, likely a
        // sub-pixel/rounding issue in how the backend tessellates already-baked
        // quads). Passing already-scaled sizes/positions into ImGui's normal
        // AddText/AddRectFilled/etc. calls is the standard, well-exercised path
        // (ImGui always samples the same baked atlas bitmap at whatever size you
        // ask for), so this sidesteps that entirely at the cost of every literal
        // in the layout below needing to be wrapped in S(...).
        const float trackerScale = getHudScale();
        const auto S = [trackerScale](float v) { return v * trackerScale; };

        const Randomizer::TrackerOverlayMode trackerMode = Randomizer::getTrackerOverlayMode();
        const bool guidedMode = trackerMode == Randomizer::TrackerOverlayMode::Guided;
        const bool showHintDot = trackerMode == Randomizer::TrackerOverlayMode::Hints || guidedMode;

        // Medals-only wins bypass the chapel/final-boss path entirely, so the
        // total moon requirement and endgame checklists are moot noise on this
        // tracker - only the Medals counter matters.
        const bool isMedalsOnlyMode = Randomizer::getBowserWinCondition()
            == Randomizer::BowserWinCondition_ToadetteMedals;
        const bool captureLockEnabled = Randomizer::isCaptureRandoEnabled() && !isMedalsOnlyMode;
        const Randomizer::TrackerMoonLimitMode moonLimitMode = Randomizer::getTrackerMoonLimitMode();
        const Randomizer::TrackerCaveSkipMode caveSkipMode = Randomizer::getTrackerCaveSkipMode();
        const s32 caveSkipRequirementCount = caveSkipMode == Randomizer::TrackerCaveSkipMode::Easy
            ? 3
            : caveSkipMode == Randomizer::TrackerCaveSkipMode::Hard ? 1
                                                                    : 0;
        const bool showCaveSkip = !isMedalsOnlyMode && caveSkipRequirementCount > 0;
        const bool caveSectionsSideBySide = false; // Always stack vertically to fit Bowser on right
        const Randomizer::SubareaUnlockMode subareaUnlockMode
            = Randomizer::getSubareaUnlockMode();
        const bool showGlobalSubareaKeys = Randomizer::isSubareaUnlockEnabled()
            && subareaUnlockMode == Randomizer::SubareaUnlockMode_PerType;
        const bool showKingdomSubareaKeys = Randomizer::isSubareaUnlockEnabled()
            && (subareaUnlockMode == Randomizer::SubareaUnlockMode_PerKingdom
                || subareaUnlockMode
                    == Randomizer::SubareaUnlockMode_PerKingdomAndType);

        const float panelTop = getPanelTop();

        // Row column metrics. Hoisted above the panel width because the width is
        // measured from them.
        //
        // Every column here is narrower than the glyph size it carries, because
        // the IcoMoon glyphs are drawn well inside their em box: at 22px the
        // widest door kind inks only 16.5px, leaving ~2.75px of padding a side.
        // So a 20px pitch still shows a 3.5px gap between neighbouring glyphs
        // while the glyphs themselves stay the size they were. Same for the
        // 16px lock/Bowser/key glyphs in a 20px column, and the 20px kingdom
        // icon in a 27px one.
        const float colW = S(20.0f);
        const float kColW = S(27.0f);
        const float kingdomIconSize = S(20.0f);
        const float kingdomTextSize = S(20.0f);
        const float hintCountSize = S(16.0f);
        const float shiftCols = Randomizer::isMoonRockKeyEnabled() ? 3.0f : 2.0f;
        // Content edge to the start of the kingdom name.
        const float nameOffset = colW * shiftCols + kColW + S(4.0f);
        const float subareaIconSize = S(22.0f);
        const float subareaColW = S(20.0f);
        // Gap the door-kind glyphs keep from the name to their left.
        const float subareaNameGap = S(8.0f);
        // The "have / req" readout every row ends with, plus the gap before it.
        const float countsSpan = S(52.0f) + S(8.0f);

        const float padX = S(14.0f);
        const float basePanelW = S(280.0f);

        // Cave Requirements and Cave Skip use compact labels at the bottom rather
        // than widening every kingdom row, so only the Subarea Key column can
        // push the panel past its base width.
        //
        // It is sized to the widest row actually drawn, not to the product of
        // independent worst cases - those are never the same row. The longest
        // kingdom names (Mushroom, Darker Side) lock three door kinds or none,
        // while every kingdom locking four has a short name, so reserving
        // "longest name" plus "most glyphs" left a band of dead centre no row
        // ever reached into.
        //
        // The hint count's width is reserved for every kingdom whenever
        // Hints/Guided is on, including ones Mario has not visited and so are not
        // printing one yet: measuring only what is on screen this frame would
        // widen the whole panel the moment a kingdom is first entered.
        float panelW = basePanelW;
        if (showKingdomSubareaKeys) {
            const float hintSpan = showHintDot
                ? font->CalcTextSizeA(hintCountSize, FLT_MAX, 0.0f, " (000)").x
                : 0.0f;
            float widestRow = 0.0f;
            for (s32 i = 0; i < kWorldCount; i++) {
                const s32 worldId = kWorlds[i].worldId;
                s32 types = 0;
                if (subareaUnlockMode == Randomizer::SubareaUnlockMode_PerKingdom) {
                    types = kingdomHasAssignedSubareaKey(worldId) ? 1 : 0;
                } else {
                    for (s32 t = 0; t < SubareaKeyDB::sGatedTypeCount; t++) {
                        if (hasAssignedSubareaKey(worldId, SubareaKeyDB::sGatedTypes[t]))
                            types++;
                    }
                }
                float span = font->CalcTextSizeA(
                                     kingdomTextSize, FLT_MAX, 0.0f, kWorlds[i].name)
                                 .x
                    + hintSpan;
                if (types > 0)
                    span += subareaNameGap + subareaColW * static_cast<float>(types);
                if (span > widestRow)
                    widestRow = span;
            }
            const float measured = padX * 2.0f + nameOffset + widestRow + countsSpan;
            if (measured > panelW)
                panelW = measured;
        }

        const float panelMargin = S(24.0f);
        const float panelX = Randomizer::isTrackerRightAligned()
            ? io.DisplaySize.x - panelW - panelMargin
            : panelMargin;
        const float padTop = S(4.0f);
        const float rowH = S(28.0f);
        const float headSize = S(19.0f);
        const float rowSize = S(16.0f);
        const float headerH = S(26.0f);
        const float capHeaderH = S(24.0f);
        const float caveHeaderSize = caveSectionsSideBySide ? S(15.0f) : headSize - S(2.0f);
        const float caveRowSize = caveSectionsSideBySide ? S(14.0f) : rowSize;
        const float sectionGap = S(6.0f);
        const float timeRowH = S(32.0f);
        const float timeValueSize = S(24.0f);
        const float timeDividerGap = S(10.0f);

        const float contentX = panelX + padX;
        const float rightX = panelX + panelW - padX;

        // Only list kingdoms that actually carry a moon requirement. Moon joins
        // the list when Moon Kingdom requirements are enabled.
        const s32 currentWorldId = file->getCurrentWorldId();
        s32 globalSubareaTypeIndex[SubareaKeyDB::sGatedTypeCount];
        s32 globalSubareaTypeKingdom[SubareaKeyDB::sGatedTypeCount];
        s32 globalSubareaTypeCount = 0;
        if (showGlobalSubareaKeys) {
            for (s32 i = 0; i < SubareaKeyDB::sGatedTypeCount; i++) {
                for (s32 w = 0; w < WorldMeta::sWorldCount; w++) {
                    if (!hasAssignedSubareaKey(w, SubareaKeyDB::sGatedTypes[i]))
                        continue;
                    globalSubareaTypeIndex[globalSubareaTypeCount] = i;
                    globalSubareaTypeKingdom[globalSubareaTypeCount++] = w;
                    break;
                }
            }
        }
        s32 totalMoons = 0;
        s32 shown[kWorldCount];
        s32 shownCount = 0;
        for (s32 i = 0; i < kWorldCount; i++) {
            s32 req = hasPossibleRequirement(kWorlds[i].worldId)
                ? getSavedRequirement(file, kWorlds[i].worldId)
                : 0;
            // Hints/Guided also list kingdoms with no moon requirement of their
            // own (Mushroom/Dark/Darker, anything set to Coins, or any kingdom
            // before Moon Kingdom Requirements assigns one) so their hint dot is
            // visible too.
            const bool possibleReq = hasPossibleRequirement(kWorlds[i].worldId);
            const bool showAll = trackerMode == Randomizer::TrackerOverlayMode::Extended || showHintDot;
            const bool hasMoonRockKey = Randomizer::isMoonRockKeyEnabled() && hasMoonRock(kWorlds[i].worldId);
            const bool hasSubareaKey = showKingdomSubareaKeys
                && kingdomHasAssignedSubareaKey(kWorlds[i].worldId);
            if (possibleReq || showAll || hasMoonRockKey || hasSubareaKey) {
                shown[shownCount++] = i;
                const s32 logicalWorldId = kWorlds[i].worldId;
                const s32 nativeWorldId = logicalWorldId;
                const bool persistedVisited = (Randomizer::getVisitedKingdoms()
                                                  & (1u << logicalWorldId))
                    != 0;
                bool visited = currentWorldId == kWorlds[i].worldId
                    || persistedVisited;
                if (visited) {
                    Randomizer::setVisitedKingdoms(Randomizer::getVisitedKingdoms() | (1u << kWorlds[i].worldId));
                }
                // This total tracks progress toward Randomizer::getMoonRequirement();
                // kingdoms with no requirement of their own (only listed here because
                // of Hints/Guided) never contribute to it.
                if (possibleReq) {
                    s32 have = file->getShineNum(nativeWorldId);
                    if (visited && have > req) {
                        have = req;
                    }
                    totalMoons += have;
                }
            }
        }

        // Hints/Guided are supersets of Complete: they keep the playtime row too,
        // subject to the user's Tracker Timer toggle.
        const bool showTime = Randomizer::isTrackerTimerEnabled();

        if (showHintDot)
            MoonRando::refreshKingdomHintStatus(accessor);

        float panelH = padTop + (showTime ? timeRowH + timeDividerGap : 0.0f)
            + shownCount * rowH + padTop;
        if (!isMedalsOnlyMode)
            panelH += headerH + S(10.0f);
        if (Randomizer::isToadetteMedalModeEnabled())
            panelH += headerH + S(10.0f);
        if (globalSubareaTypeCount > 0)
            panelH += sectionGap + capHeaderH + S(26.0f) + S(8.0f);
        if (captureLockEnabled || showCaveSkip) {
            panelH += sectionGap;
            const float caveIconSize = S(26.0f);
            if (captureLockEnabled) {
                panelH += capHeaderH + caveIconSize + S(8.0f);
            }
            if (showCaveSkip) {
                panelH += capHeaderH + caveIconSize + S(8.0f);
            }
        }
        // Boss fits on the right side, so no extra height needed
        sLastPanelBottom = panelTop + panelH;

        const HudTheme::Palette theme = HudTheme::get();
        const ImU32 colPanel = applyAlpha(kColPanel, sVisibilityAlpha);
        const ImU32 colBorder = applyAlpha(theme.border, sVisibilityAlpha);
        const ImU32 colText = applyAlpha(kColText, sVisibilityAlpha);
        const ImU32 colDim = applyAlpha(kColDim, sVisibilityAlpha);
        const ImU32 colHead = applyAlpha(theme.heading, sVisibilityAlpha);
        const ImU32 colOn = applyAlpha(kColOn, sVisibilityAlpha);
        const ImU32 colOff = applyAlpha(kColOff, sVisibilityAlpha);
        const ImU32 colCubeOn = applyAlpha(kColCubeOn, sVisibilityAlpha);
        const ImU32 colTime = applyAlpha(kColTime, sVisibilityAlpha);
        const ImU32 colHintRed = applyAlpha(kColHintRed, sVisibilityAlpha);
        const ImU32 colHintOrange = applyAlpha(kColHintOrange, sVisibilityAlpha);
        const ImU32 colHintGreen = applyAlpha(kColHintGreen, sVisibilityAlpha);
        const ImU32 colHintUnavailable = applyAlpha(kColHintUnavailable, sVisibilityAlpha);
        const ImU32 colLimitMid = applyAlpha(kColLimitMid, sVisibilityAlpha);

        dl->AddRectFilled(ImVec2(panelX, panelTop), ImVec2(panelX + panelW, panelTop + panelH), colPanel, S(10.0f));
        dl->AddRect(ImVec2(panelX, panelTop), ImVec2(panelX + panelW, panelTop + panelH), colBorder, S(10.0f), 0, S(2.0f));

        float y = panelTop + padTop;

        if (showTime) {
            // Stopwatch icon: circle face + a static hour/minute hand pair. Sized
            // up and placed first so playtime reads as the panel's headline stat.
            const float midY = y + timeRowH * 0.5f;
            const float labelY = midY - headSize * 0.5f;
            const float valueY = midY - timeValueSize * 0.5f;
            const float clockCx = contentX + S(8.0f);
            const float clockCy = midY;
            dl->AddCircle(ImVec2(clockCx, clockCy), S(8.0f), colTime, 0, S(1.8f));
            dl->AddLine(ImVec2(clockCx, clockCy - S(5.0f)), ImVec2(clockCx, clockCy), colTime, S(1.8f));
            dl->AddLine(ImVec2(clockCx, clockCy), ImVec2(clockCx + S(4.0f), clockCy + S(2.5f)), colTime, S(1.8f));

            addText(dl, font, headSize, contentX + S(22.0f), labelY, colHead, "Time");

            u64 frames = file->getPlayTimeTotal();
            u64 s = frames / 60;
            u64 cs = (frames % 60) * 100 / 60;
            u64 m = s / 60;
            s = s % 60;
            u64 h = m / 60;
            m = m % 60;

            char dBuf[2] = { 0, 0 };

            float curX = rightX;
            const float digitWidth = S(12.0f);
            const float colonWidth = S(8.0f);

            // cs
            curX -= digitWidth;
            dBuf[0] = '0' + (cs % 10);
            addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);
            curX -= digitWidth;
            dBuf[0] = '0' + (cs / 10);
            addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);

            // colon
            curX -= colonWidth;
            addTextCentered(dl, font, timeValueSize, curX + colonWidth * 0.5f, valueY, colTime, ":");

            // s
            curX -= digitWidth;
            dBuf[0] = '0' + (s % 10);
            addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);
            curX -= digitWidth;
            dBuf[0] = '0' + (s / 10);
            addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);

            // colon
            curX -= colonWidth;
            addTextCentered(dl, font, timeValueSize, curX + colonWidth * 0.5f, valueY, colTime, ":");

            // m
            char mStr[16];
            if (h > 0) {
                nn::util::SNPrintf(mStr, sizeof(mStr), "%02d", m);
            } else {
                nn::util::SNPrintf(mStr, sizeof(mStr), "%d", m);
            }
            s32 mLen = strlen(mStr);
            for (s32 i = mLen - 1; i >= 0; i--) {
                curX -= digitWidth;
                dBuf[0] = mStr[i];
                addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);
            }

            // h
            if (h > 0) {
                // colon
                curX -= colonWidth;
                addTextCentered(dl, font, timeValueSize, curX + colonWidth * 0.5f, valueY, colTime, ":");

                char hStr[16];
                nn::util::SNPrintf(hStr, sizeof(hStr), "%d", h);
                s32 hLen = strlen(hStr);
                for (s32 i = hLen - 1; i >= 0; i--) {
                    curX -= digitWidth;
                    dBuf[0] = hStr[i];
                    addTextCentered(dl, font, timeValueSize, curX + digitWidth * 0.5f, valueY, colTime, dBuf);
                }
            }

            y += timeRowH;
            dl->AddLine(ImVec2(contentX, y + timeDividerGap * 0.5f), ImVec2(rightX, y + timeDividerGap * 0.5f), colOff, S(1.0f));
            y += timeDividerGap;
        }

        char buf[64];
        if (!isMedalsOnlyMode) {
            y += S(10.0f); // More margin above Moons
            nn::util::SNPrintf(buf, sizeof(buf), "Moons  %d / %u",
                totalMoons, Randomizer::getMoonRequirement());
            addText(dl, font, headSize, contentX, y, colHead, buf);
            y += headerH;
        }

        if (Randomizer::isToadetteMedalModeEnabled()) {
            y += S(10.0f);
            char medalBuf[64];
            nn::util::SNPrintf(medalBuf, sizeof(medalBuf), "\xee\xa4\xa2 Toadette Medals  %d / %d",
                MoonRando::getToadetteMedalCollectedCount(file), Randomizer::getToadetteMedalRatio().required);
            addText(dl, font, headSize, contentX, y, colHead, medalBuf);
            y += headerH;
        }

        const float textX = contentX + colW * shiftCols;
        const float nameX = contentX + nameOffset;

        // Kingdom-scoped Subarea Keys hang off the RIGHT, anchored to the moon
        // counts every row ends with. Those counts are the only fixed landmark on
        // that side of a row, so pinning the glyphs to them keeps the block
        // reading as part of that readout instead of floating in the gap after
        // short kingdom names. Each row also packs only its own door kinds at a
        // tight pitch, so a kingdom that locks one kind no longer reserves the
        // width of the kingdom that locks four.
        const float subareaRightX = rightX - countsSpan;

        for (s32 s = 0; s < shownCount; s++) {
            const s32 i = shown[s];
            const s32 w = kWorlds[i].worldId;
            const float midY = y + rowH * 0.5f;
            const float textY = midY - rowSize * 0.5f;

            // Discovery is tied only to physically visiting the kingdom. The
            // save's already-go flag persists after leaving, while randomized
            // moons credited to an unvisited kingdom do not reveal it.
            bool visited = currentWorldId == w || ((Randomizer::getVisitedKingdoms() & (1u << w)) != 0);
            const s32 nativeWorldId = w;

            // Check visibility mode
            bool hasReq = hasPossibleRequirement(w);
            bool showRow = hasReq; // Basic mode shows only requirement kingdoms
            const bool hasMoonRockKey = Randomizer::isMoonRockKeyEnabled() && hasMoonRock(w);
            const bool hasSubareaKey = showKingdomSubareaKeys
                && kingdomHasAssignedSubareaKey(w);
            if (trackerMode == Randomizer::TrackerOverlayMode::Extended || showHintDot
                || hasMoonRockKey || hasSubareaKey) {
                showRow = true; // Show all
            }
            if (!showRow)
                continue;

            // Recently-changed rows get a brief wash of their kingdom colour and a
            // highlighted count, so a moon credited to a kingdom the player is not
            // standing in still announces itself.
            const float rowFlash = sRowFlash[i] * sVisibilityAlpha;
            if (rowFlash > 0.0f) {
                const float ease = rowFlash * rowFlash;
                dl->AddRectFilled(ImVec2(contentX - S(6.0f), y + S(1.0f)),
                    ImVec2(rightX + S(6.0f), y + rowH - S(1.0f)),
                    applyAlpha(kWorlds[i].color, ease * 0.28f), S(5.0f));
            }

            const s32 peaceScenario = nativeWorldId >= 0 && nativeWorldId < WorldMeta::sWorldCount
                ? LD::sPeaceScenarioByKingdom[nativeWorldId]
                : -1;
            const bool peace = peaceScenario > 0 && file->getScenarioNo(nativeWorldId) >= peaceScenario;
            const bool showsMoonRock = hasMoonRock(nativeWorldId);
            const bool cube = showsMoonRock && file->isOpenMoonRock(nativeWorldId);

            const char* ICON_LOCK = "\xee\xa4\x8f"; // E90F
            const char* ICON_UNLOCK = "\xee\xa4\xa3"; // E923
            const char* ICON_PEACE = "\xee\xa4\x98"; // E918
            const char* ICON_ROCK_LOCKED = "\xee\xa4\x9a"; // E91A
            const char* ICON_KEY = "\xee\xa4\x8d"; // E90D
            const char* ICON_BOWSER_HEAD = "\xee\xa4\x81"; // E901 (Bowser)

            // 1. Determine Lock Icon
            s32 req = hasPossibleRequirement(w) ? getSavedRequirement(file, w) : 0;
            s32 have = file->getShineNum(nativeWorldId);

            ImU32 lockColor = colDim;
            const char* lockIcon = ICON_LOCK;

            const KingdomProgress::KingdomProgressEntry* entry = KingdomProgress::findEntry(file, w);
            const KingdomProgress::KingdomProgressList* list = KingdomProgress::get(file);
            const s32 frontier = KingdomProgress::getFirstIncompleteIndex(file);

            if (entry && entry->completeStatus) {
                lockColor = colText; // White: previously completed
                lockIcon = ICON_UNLOCK;
            } else if (list && frontier >= 0 && frontier < KingdomProgress::cKingdomCount && list->entries[frontier].kingdomId == w) {
                lockColor = colHintGreen; // Green: current kingdom
                lockIcon = ICON_UNLOCK;
            } else if (Randomizer::isVisitedKingdomsMapEnabled() && visited) {
                lockColor = colHintOrange; // Orange: visited out of sequence
                lockIcon = ICON_UNLOCK;
            } else if (visited) {
                // Not on world map, but unlocked via painting
                lockIcon = ICON_UNLOCK;
                lockColor = colDim; // Grey
            }
            addIconCentered(dl, font, rowSize, contentX + colW * 0.5f, textY, lockColor, lockIcon);

            // 2. Determine Kingdom State Icon
            bool statePeace = peace;
            bool stateCube = false;
            if (hasMoonRock(w)) {
                stateCube = file->isOpenMoonRock(nativeWorldId);
            }
            // A kingdom's state is unknown until Mario has physically visited it.
            // Keep the Bowser icon as the greyed-out unknown marker rather than
            // leaking the randomized peace/Cube state through the tracker.
            const char* stateIcon = !visited
                ? ICON_BOWSER_HEAD
                : (stateCube ? ICON_ROCK_LOCKED : (statePeace ? ICON_PEACE : ICON_BOWSER_HEAD));
            ImU32 stateColor = visited ? colText : applyAlpha(colDim, 0.5f);

            addIconCentered(dl, font, rowSize, contentX + colW * 1.5f, textY, stateColor, stateIcon);

            // 3. Determine Moon Rock Key Icon
            if (Randomizer::isMoonRockKeyEnabled() && showsMoonRock) {
                bool gotKey = false;
                const ShineDB::ShineEntry* keyEntry = MoonRando::getMoonRockKeyForKingdom(nativeWorldId);
                if (keyEntry) {
                    gotKey = file->customIsGotShine(keyEntry->uid, keyEntry->stage);
                }
                ImU32 keyColor = gotKey ? colText : applyAlpha(colDim, 0.5f);
                addIconCentered(dl, font, rowSize, contentX + colW * 2.5f, textY, keyColor, ICON_KEY);
            }

            const float kingdomIconY = midY - kingdomIconSize * 0.5f;
            const float kingdomTextY = midY - kingdomTextSize * 0.5f - S(3.0f);

            // Draw the colored icon
            float kIconCx = textX + kColW * 0.5f;
            if (w == WorldMeta::WorldId_Bowsers)
                kIconCx += S(3.0f); // Tweak to horizontally center Bowser's narrower icon
            addIconCentered(dl, font, kingdomIconSize, kIconCx, kingdomIconY, applyAlpha(kWorlds[i].color, sVisibilityAlpha), kWorlds[i].icon);

            // Draw the white kingdom name
            addText(dl, font, kingdomTextSize, nameX, kingdomTextY, colText, kWorlds[i].name);

            // Draw hint count next to kingdom name
            if (showHintDot && visited) {
                s32 count = 0;
                if (guidedMode) {
                    count = MoonRando::getKingdomHintCount(w);
                } else {
                    count = MoonRando::getKingdomHintTotalCount(w);
                }
                char countBuf[16];
                nn::util::SNPrintf(countBuf, sizeof(countBuf), " (%d)", count);

                ImVec2 textSize = font->CalcTextSizeA(kingdomTextSize, FLT_MAX, 0.0f, kWorlds[i].name);
                float hintCountX = nameX + textSize.x;
                float hintCountY = midY - hintCountSize * 0.5f;
                addText(dl, font, hintCountSize, hintCountX, hintCountY, colText, countBuf);
            }

            // Kingdom-scoped modes live on kingdom rows. Per Kingdom has one
            // shared key, while the fine mode has one independently colored
            // glyph for every door kind that kingdom actually locks.
            if (hasSubareaKey) {
                const float subareaIconY = midY - subareaIconSize * 0.5f;

                if (subareaUnlockMode == Randomizer::SubareaUnlockMode_PerKingdom) {
                    for (s32 typeIndex = 0;
                         typeIndex < SubareaKeyDB::sGatedTypeCount; typeIndex++) {
                        const SubareaKeyDB::SubareaType type
                            = SubareaKeyDB::sGatedTypes[typeIndex];
                        if (!hasAssignedSubareaKey(w, type))
                            continue;
                        const bool unlocked = MoonRando::hasSubareaKey(
                            file, w, static_cast<s32>(type));
                        addSubareaIcon(dl, font, subareaIconSize,
                            subareaRightX - subareaColW * 0.5f, subareaIconY,
                            kColSubareaKeyOwned, kColSubareaKeyLocked, unlocked, ICON_KEY);
                        break;
                    }
                } else {
                    // Right-anchored, so this row's kind count has to be known
                    // before the first glyph can be placed.
                    s32 typeCount = 0;
                    for (s32 typeIndex = 0;
                         typeIndex < SubareaKeyDB::sGatedTypeCount; typeIndex++) {
                        const SubareaKeyDB::SubareaType type
                            = SubareaKeyDB::sGatedTypes[typeIndex];
                        if (hasAssignedSubareaKey(w, type)
                            && getSubareaTypeStyle(type).icon)
                            typeCount++;
                    }
                    const float startX = subareaRightX - subareaColW * typeCount;

                    s32 drawn = 0;
                    for (s32 typeIndex = 0;
                         typeIndex < SubareaKeyDB::sGatedTypeCount; typeIndex++) {
                        const SubareaKeyDB::SubareaType type
                            = SubareaKeyDB::sGatedTypes[typeIndex];
                        if (!hasAssignedSubareaKey(w, type))
                            continue;
                        const SubareaTypeStyle style = getSubareaTypeStyle(type);
                        if (!style.icon)
                            continue;
                        const bool unlocked = MoonRando::hasSubareaKey(
                            file, w, static_cast<s32>(type));
                        addSubareaIcon(dl, font, subareaIconSize,
                            startX + subareaColW * (drawn + 0.5f), subareaIconY,
                            style.owned, style.locked, unlocked, style.icon);
                        drawn++;
                    }
                }
            }

            // Moon Kingdom earns its count row even with no moon-use
            // requirement of its own when low gravity is behind a moon count:
            // that mode gives the player a number to work towards, and this is
            // where they read it.
            const bool showsMoonGravityCount = w == WorldMeta::WorldId_Moon
                && Randomizer::getMoonGravityMode()
                    == Randomizer::MoonGravityMode::ExtraMoonRequirements;
            if (hasPossibleRequirement(w) || showsMoonGravityCount) {
                char haveBuf[8];
                char reqBuf[8];
                if (visited) {
                    if (req > 0 && have > req)
                        have = req;
                    nn::util::SNPrintf(haveBuf, sizeof(haveBuf), "%d", have);
                    nn::util::SNPrintf(reqBuf, sizeof(reqBuf), "%d", req);
                } else {
                    nn::util::SNPrintf(haveBuf, sizeof(haveBuf), "%d", have);
                    nn::util::SNPrintf(reqBuf, sizeof(reqBuf), "?");
                }

                // Moon Limits: while the requirement is still "?", color the count
                // against the range this kingdom's requirement could fall in. Once
                // it is visited the real number is on screen anyway, so the count
                // goes back to white rather than restating it in color.
                ImU32 haveCol = colText;
                s32 limitMin = 0;
                s32 limitMax = 0;
                if (moonLimitMode != Randomizer::TrackerMoonLimitMode::None && !visited
                    && RequirementRando::getTheoreticalRange(w, &limitMin, &limitMax)) {
                    if (have >= limitMax)
                        haveCol = colHintGreen;
                    else if (moonLimitMode == Randomizer::TrackerMoonLimitMode::Full)
                        haveCol = have < limitMin ? colHintRed : colLimitMid;
                }

                // The count itself carries the flash: it is the value that moved.
                if (rowFlash > 0.0f)
                    haveCol = applyAlpha(kColRowFlash, sVisibilityAlpha);

                const float numColW = S(24.0f);
                const float slashColW = S(12.0f);

                // Moon low gravity's Extra Moon Requirements mode adds a third
                // field to this one row: collected / kingdom requirement / moons
                // needed for low gravity (the kingdom's own requirement plus the
                // seed's 5-10 surcharge). It stays "?" until Moon Kingdom has been
                // visited, exactly like the requirement beside it - the surcharge
                // is something the player discovers by going there.
                char gravityBuf[8];
                const bool showGravityReq = showsMoonGravityCount;
                if (showGravityReq) {
                    if (visited) {
                        nn::util::SNPrintf(gravityBuf, sizeof(gravityBuf), "%d",
                            Randomizer::getMoonGravityMoonRequirement());
                    } else {
                        nn::util::SNPrintf(gravityBuf, sizeof(gravityBuf), "?");
                    }
                }

                // Right-anchored, so the extra field extends the row leftward
                // rather than pushing anything past the panel edge.
                float rX = rightX + S(8.0f) - numColW * 0.5f;
                if (showGravityReq) {
                    addTextCentered(
                        dl, font, kingdomTextSize, rX, kingdomTextY, colText, gravityBuf);
                    rX -= (numColW * 0.5f + slashColW * 0.5f);
                    addTextCentered(dl, font, kingdomTextSize, rX, kingdomTextY, colText, "/");
                    rX -= (slashColW * 0.5f + numColW * 0.5f);
                }
                addTextCentered(dl, font, kingdomTextSize, rX, kingdomTextY, colText, reqBuf);
                rX -= (numColW * 0.5f + slashColW * 0.5f);
                addTextCentered(dl, font, kingdomTextSize, rX, kingdomTextY, colText, "/");
                rX -= (slashColW * 0.5f + numColW * 0.5f);
                addTextCentered(dl, font, kingdomTextSize, rX, kingdomTextY, haveCol, haveBuf);
            }
            y += rowH;
        }

        // Per Type has exactly one global key per door kind, so render each slot
        // once rather than repeating the same state on every kingdom row.
        if (globalSubareaTypeCount > 0) {
            y += sectionGap;
            addText(dl, font, caveHeaderSize, contentX, y, colHead, "Subarea Keys");
            y += capHeaderH;

            const float iconSize = S(26.0f);
            const float iconColW = S(34.0f);
            for (s32 i = 0; i < globalSubareaTypeCount; i++) {
                const SubareaKeyDB::SubareaType type
                    = SubareaKeyDB::sGatedTypes[globalSubareaTypeIndex[i]];
                const SubareaTypeStyle style = getSubareaTypeStyle(type);
                if (!style.icon)
                    continue;

                const bool unlocked = MoonRando::hasSubareaKey(
                    file, globalSubareaTypeKingdom[i], static_cast<s32>(type));
                const float cx = contentX + iconColW * (i + 0.5f);
                addSubareaIcon(dl, font, iconSize, cx, y, style.owned, style.locked,
                    unlocked, style.icon);
            }
            y += iconSize + S(8.0f);
        }

        if (captureLockEnabled || showCaveSkip) {
            y += sectionGap;
            const float caveIconSize = S(26.0f);

            if (captureLockEnabled) {
                addText(dl, font, caveHeaderSize, contentX, y, colHead, "Moon Cave Requirements");
            }
            if (showCaveSkip) {
                float skipX = contentX;
                float skipY = y + (captureLockEnabled ? capHeaderH + caveIconSize + S(8.0f) : 0.0f);
                addText(dl, font, caveHeaderSize, skipX, skipY, colHead,
                    caveSkipMode == Randomizer::TrackerCaveSkipMode::Easy ? "Moon Cave Skip: Easy" : "Moon Cave Skip: Hard");
            }
            if (captureLockEnabled || showCaveSkip) {
                y += capHeaderH;
            }
            const float caveColW = S(34.0f);

            if (captureLockEnabled) {
                s32 drawnCount = 0;
                for (s32 i = 0; i < kCaveCaptureCount; i++) {
                    const bool owned = isCaptureOwned(accessor, file, captureLockEnabled, kCaveCaptures[i]);
                    const char* iconToDraw = kCaveCaptures[i].icon;
                    if (!iconToDraw)
                        continue;

                    float cx = contentX + drawnCount * caveColW + caveColW * 0.5f;
                    if (i == 3)
                        cx -= S(6.0f); // Chain Chomp visual offset
                    addIconCentered(dl, font, caveIconSize, cx, y, owned ? colText : applyAlpha(colDim, 0.5f), iconToDraw);
                    drawnCount++;
                }
                if (kCaveCaptureCount > 0) {
                    y += caveIconSize + S(8.0f);
                }
            }

            if (showCaveSkip) {
                float skipY = y + capHeaderH;
                s32 drawnCount = 0;
                for (s32 i = 0; i < caveSkipRequirementCount; i++) {
                    const char* iconToDraw = "\xee\xa4\xa6"; // Wall Jump
                    bool unlocked = false;
                    if (caveSkipMode == Randomizer::TrackerCaveSkipMode::Easy) {
                        switch (i) {
                        case 0:
                            iconToDraw = "\xee\xa4\x8c"; // High Jump
                            unlocked = AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_GroundPoundJump)
                                || AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_Backflip);
                            break;
                        case 1:
                            unlocked = AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_WallJump);
                            break;
                        case 2:
                            iconToDraw = "\xee\xa4\xa4"; // Dive (Vault)
                            unlocked = AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_Vault);
                            break;
                        }
                    } else if (caveSkipMode == Randomizer::TrackerCaveSkipMode::Hard) {
                        unlocked = AbilityRando::isAbilityUnlocked(accessor, AbilityRando::AbilityId_WallJump);
                    }

                    float cx = contentX + drawnCount * caveColW + caveColW * 0.5f;
                    addIconCentered(dl, font, caveIconSize, cx, skipY, unlocked ? colText : applyAlpha(colDim, 0.5f), iconToDraw);
                    drawnCount++;
                }
            }
        }

        if (captureLockEnabled) {
            const char* ICON_BOWSER_HEAD = "\xee\xa4\x82"; // E902 (BowserCapture)
            const float bowserSize = S(60.0f); // Double size
            float bossX = rightX - bowserSize - S(10.0f); // Tucked slightly in from the edge
            float bossY = sLastPanelBottom - S(10.0f) - padTop - bowserSize; // Align to bottom of panel
            const bool owned = isCaptureOwned(accessor, file, captureLockEnabled, kFinalBossCaptures[0]);
            addText(dl, font, bowserSize, bossX, bossY, owned ? colText : applyAlpha(colDim, 0.5f), ICON_BOWSER_HEAD);
        }
    }

} // namespace TrackerOverlay
