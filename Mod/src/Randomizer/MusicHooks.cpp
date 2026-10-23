#include "Randomizer/MusicHooks.h"

#include "Randomizer/MusicRando.h"
#include "Randomizer/Randomizer.h"

#include "../audio/CustomBgmManager.h"
#include "al/Library/Base/StringUtil.h"
#include "al/Library/Bgm/BgmLineFunction.h"
#include "al/Library/Bgm/BgmPlayingRequest.h"
#include "game/Sequence/HakoniwaSequence.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameSystem.h"
#include "hk/hook/Trampoline.h"
#include "imgui.h"
#include "nn/util.h"

#include <cstdint>
#include <cstring>

namespace al {
    class BgmDirector;
    class BgmLine {
    public:
        void startBgm(const BgmPlayingRequest&);
        const char* getResourceName(const char*) const;
    };
    bool isPadTriggerL(int port);
    bool isPadHoldL(int port);
} // namespace al

namespace MusicHooks {

    static constexpr s32 kMaxRemapLogs = 48;
    static constexpr s32 kMaxSkipLogs = 48;
    static constexpr s32 kNowPlayingFrames = 300;
    static constexpr s32 kMaxIntroContinuations = 16;
    static constexpr uintptr_t kBgmLineResetOffset = 0x0081ee58;
    static constexpr uintptr_t kBgmLineUpdateOffset = 0x0081f31c;
    static constexpr uintptr_t kBgmLineStopAllOffset = 0x00820aa8;
    static constexpr uintptr_t kBgmLineIsRunningOffset = 0x008211b8;
    static constexpr uintptr_t kBgmLineIsPauseOffset = 0x008211c8;
    static constexpr uintptr_t kBgmLineGetCurPlayNameOffset = 0x0082134c;
    static constexpr uintptr_t kBgmLineGetCurResourceNameOffset = 0x00821364;
    // Custom music silences the vanilla stream, never the line. Stopping a line
    // is not an option: BgmMultiPlayingController::update restarts any line it
    // owns that reports "not playing", every frame, which in the Crazy Cap shop
    // meant ~60 stream loads a second until the audio heap wedged.
    //
    // al::Bgm::update re-applies the line's fader to the stream through this
    // every frame, so clamping here is enough and nothing has to be restored.
    static constexpr uintptr_t kBasicSoundSetVolumeOffset = 0x00aa3d00;
    // Once per frame, before al::BgmLineKeeper::update runs the lines.
    static constexpr uintptr_t kBgmDirectorUpdateOffset = 0x00819678;
    // al::BgmLine holds its two al::Bgm players in an array at +0x48, and each
    // player reaches its nn::atk sound through the fader controller at +0x68
    // (see al::Bgm::changeFader).
    static constexpr size_t kBgmLinePlayerArrayOffset = 0x48;
    static constexpr size_t kBgmPlayerFaderCtrlOffset = 0x68;
    static constexpr uintptr_t kBgmDirectorStartOffset = 0x00819860;
    static constexpr uintptr_t kBgmDirectorStartSituationOffset = 0x0081a538;
    static constexpr s32 kMollusqueIntroFrames = 300;
    static constexpr const char* kMollusqueIntroResource = "StmRsBgmBossHaikai_A";
    static constexpr const char* kMollusqueSustainedResource = "StmRsBgmBossHaikai_B";
    static constexpr const char* kMollusqueSustained8BitResource = "StmRsBgmBossHaikai_B_8Bit";
    // Selects the boss's sustained layer within StmRsBgmBossHaikai_B. It is not
    // a 2D/3D arrangement selector, so the 8-bit battle needs this situation AND
    // In2DArea - see bgmDirectorStartSituationHook, which exempts it from the
    // 8-bit rewrite for exactly that reason. The game ships no 8-bit arrangement
    // of the intro (only "Mollusque-Lanceur Battle (8-Bit)" exists), which is why
    // sBossResourceTable has BossHaikai_B_8Bit but no _A_8Bit.
    static constexpr const char* kMollusqueSustainedSituation = "CollectBgmGiantWanderBoss";

    static s32 sRemapLogCount = 0;
    static s32 sSkipLogCount = 0;
    static s32 sNowPlayingFrames = 0;
    static char sNowPlayingTitle[96] = {};
    static char sLastShownResource[80] = {};
    static bool sDiagnosticBannerLogged = false;
    static char sLastMaxSourceResource[80] = {};
    static bool sLastMaxSourceResourceValid = false;

    struct IntroContinuation {
        al::BgmLine* line = nullptr;
        al::BgmDirector* director = nullptr;
        al::BgmPlayingRequest request { nullptr };
        bool sawIntroPlaying = false;
        // Whether the intro was drawn as an 8-bit variant. Snapshotted at arm
        // time because sForce8Bit/sForce3D are per-request globals that any
        // other track playing during the ~5s intro will have moved on.
        bool force8Bit = false;
        s32 activeFrames = 0;
    };

    static IntroContinuation sIntroContinuations[kMaxIntroContinuations];
    static al::BgmDirector* sStartingBgmDirector = nullptr;
    static bool sStartMollusqueSustainedSituation = false;

    // Music Toad / the pause-menu Music List: line currently playing the
    // player's selected "CollectBgm" track, if any. Vanilla can fail to ever
    // stop it once a different track is meant to take over, leaving it
    // it looping forever underneath. Remembered here so startBgmHook can force
    // it off the moment anything else starts, instead of blocking it outright
    // (which would also stall the EventFlow gates that wait for it to play).
    static al::BgmLine* sCollectBgmLine = nullptr;

    static bool sForce8Bit = false;
    static bool sForce3D = false;
    static char sActualResource[80] = {};

    template <typename Fn>
    static Fn mainOffsetFunction(uintptr_t offset) {
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
    }

    static bool isReady() {
        return Randomizer::isMusicRandoEnabled() && MusicRando::isInitialized();
    }

    static const char* getCurrentStageName() {
        GameSystem* system = GameSystemFunction::getGameSystem();
        if (!system || !system->mSequence)
            return nullptr;
        HakoniwaSequence* seq = (HakoniwaSequence*)system->mSequence;
        if (!seq->mGameDataHolderAccessor)
            return nullptr;
        return GameDataFunction::getCurrentStageName(seq->mGameDataHolderAccessor);
    }

    static bool isEqual(const char* a, const char* b) {
        return a && b && al::isEqualString(a, b);
    }

    static bool startsWith(const char* value, const char* prefix) {
        if (!value || !prefix)
            return false;
        return std::strncmp(value, prefix, std::strlen(prefix)) == 0;
    }

    static bool contains(const char* value, const char* needle) {
        return value && needle && std::strstr(value, needle) != nullptr;
    }

    static bool isBgmLineRunning(const al::BgmLine* line) {
        if (!line)
            return false;

        using Fn = bool (*)(const al::BgmLine*);
        return mainOffsetFunction<Fn>(kBgmLineIsRunningOffset)(line);
    }

    static bool isBgmLinePaused(const al::BgmLine* line) {
        if (!line)
            return false;

        using Fn = bool (*)(const al::BgmLine*);
        return mainOffsetFunction<Fn>(kBgmLineIsPauseOffset)(line);
    }

    // ── music source ─────────────────────────────────────────────────────────
    //
    // The game owns the audio: it drives every line's fader and re-applies the
    // result to the stream through nn::atk every frame. This layer never writes
    // that state. It records one decision per line, derives from those records
    // once a frame which line the player should be hearing, and -- if that line
    // is meant to be replaced by a custom track -- clamps the vanilla streams'
    // volume to zero *after* the game has set it.
    //
    // Clamping the sound rather than the fader is what keeps this stable.
    // al::Bgm::isRunning reads the fader value itself, so writing it made idle
    // players report as running (crashing the stop recorder), stopped lines
    // never finish stopping, and released lines come back at full volume. None
    // of that can happen to a value we only ever overwrite downstream, and
    // "un-silence" costs nothing: stop clamping and the game's own value lands
    // on the very next frame.

    static void setNowPlaying(const char* resourceName);

    // Cues that never take a custom track and must stay audible while one
    // plays. The moon-get jingle is short, plays over the area theme, and would
    // otherwise cost a multi-megabyte load per moon. Add play names here.
    static constexpr const char* kNoCustomMusicCues[] = { "GetShine" };

    static bool isCueExemptFromCustomMusic(const char* playName) {
        if (!playName || !playName[0])
            return false;
        for (const char* cue : kNoCustomMusicCues) {
            if (isEqual(playName, cue))
                return true;
        }
        return false;
    }

    // One record per al::BgmLine that has started a cue. This is the whole of
    // the feature's game-side state: everything else is derived from it each
    // frame, so there is no ownership to keep in sync across hooks.
    static constexpr int kMaxBgmLineRecords = 24;

    struct BgmLineRecord {
        al::BgmLine* line = nullptr;
        char cueKey[192] = {};
        // Empty means this line's cue resolved to the vanilla side.
        char track[128] = {};
        bool exempt = false;
        // Monotonic, so "the line the player should be hearing" is simply the
        // most recently started one that is still running.
        u32 startSeq = 0;
    };

    static BgmLineRecord sLineRecords[kMaxBgmLineRecords];
    static u32 sNextStartSeq = 1;

    // Derived once a frame by resolveCustomMusic, consumed by the per-line hook.
    static bool sSilenceVanilla = false;
    // Latches once the backend has proved it can make sound, so the game's music
    // is never silenced for a custom track that turns out never to play, and a
    // gap between two custom tracks does not let vanilla back in.
    static bool sCustomEverAudible = false;
    // Grace period before a track with no winning line is stopped outright, so
    // crossing a loading zone ducks it instead of cutting it.
    static s32 sNoWinnerFrames = 0;
    static constexpr s32 kNoWinnerGraceFrames = 120;

    // Safety net: if silencing somehow makes a line restart in a loop, back out
    // rather than thrash the stream loader.
    static s32 sStartCount = 0;
    static s32 sStartWindow = 0;
    static bool sTakeoverDisabled = false;

    static BgmLineRecord* findRecord(const al::BgmLine* line) {
        for (BgmLineRecord& record : sLineRecords) {
            if (record.line == line)
                return &record;
        }
        return nullptr;
    }

    static BgmLineRecord* acquireRecord(al::BgmLine* line) {
        if (BgmLineRecord* existing = findRecord(line))
            return existing;
        for (BgmLineRecord& record : sLineRecords) {
            if (!record.line) {
                record = BgmLineRecord {};
                record.line = line;
                return &record;
            }
        }

        static bool sReportedFull = false;
        if (!sReportedFull) {
            sReportedFull = true;
            Randomizer::logPush("[CustomBgm] line records exhausted");
        }
        return nullptr;
    }

    static void releaseRecord(const al::BgmLine* line) {
        if (BgmLineRecord* record = findRecord(line))
            *record = BgmLineRecord {};
    }

    static void** getBgmLinePlayers(al::BgmLine* line) {
        if (!line)
            return nullptr;
        return *reinterpret_cast<void***>(reinterpret_cast<u8*>(line) + kBgmLinePlayerArrayOffset);
    }

    // The nn::atk sound behind one al::Bgm, reached exactly as al::Bgm::changeFader
    // reaches it.
    static void* getBgmPlayerSound(void* bgm) {
        if (!bgm)
            return nullptr;
        void* ctrl = *reinterpret_cast<void**>(reinterpret_cast<u8*>(bgm) + kBgmPlayerFaderCtrlOffset);
        if (!ctrl)
            return nullptr;
        void* inner = *reinterpret_cast<void**>(ctrl);
        if (!inner)
            return nullptr;
        return *reinterpret_cast<void**>(reinterpret_cast<u8*>(inner) + 8);
    }

    // Applied after al::Bgm::update has written the line's own volume, so this
    // is the value that reaches the mixer. Nothing has to be undone: the moment
    // this stops being called the game's value stands again.
    static void silenceBgmLine(al::BgmLine* line) {
        void** players = getBgmLinePlayers(line);
        if (!players)
            return;

        using Fn = void (*)(void*, f32, s32);
        Fn setVolume = mainOffsetFunction<Fn>(kBasicSoundSetVolumeOffset);
        for (int i = 0; i < 2; i++) {
            if (void* sound = getBgmPlayerSound(players[i]))
                setVolume(sound, 0.0f, 0);
        }
    }

    // The whole decision, recomputed from the records once a frame. Ownership is
    // derived here rather than stored and mutated from every hook, which is what
    // let a stale line keep the custom voice after its area had been left.
    static void resolveCustomMusic() {
        CustomBgmManager& custom = CustomBgmManager::instance();
        const Randomizer::MusicSourceMode mode = Randomizer::getMusicSourceMode();

        if (sStartWindow > 0)
            sStartWindow--;

        if (mode == Randomizer::MusicSourceMode::Off) {
            sSilenceVanilla = true;
            custom.stop();
            return;
        }

        if (!Randomizer::isCustomMusicPossible() || sTakeoverDisabled || custom.isFaulted()) {
            sSilenceVanilla = false;
            custom.stop();
            return;
        }

        BgmLineRecord* winner = nullptr;
        for (BgmLineRecord& record : sLineRecords) {
            if (!record.line || record.exempt || !isBgmLineRunning(record.line))
                continue;
            if (!winner || record.startSeq > winner->startSeq)
                winner = &record;
        }

        if (!winner || !winner->track[0]) {
            // Either nothing is playing, or the line the player should be
            // hearing chose the vanilla side.
            sSilenceVanilla = false;
            if (winner) {
                // A definite vanilla cue: stop rather than linger.
                sNoWinnerFrames = 0;
                custom.stop();
            } else if (++sNoWinnerFrames > kNoWinnerGraceFrames) {
                custom.stop();
            } else {
                // Probably mid-transition. Duck so crossing a loading zone
                // fades the track out and can fade the same one back in.
                custom.setVolume(0.0f);
            }
            return;
        }

        sNoWinnerFrames = 0;
        custom.setVolume(1.0f);
        custom.play(winner->track);
        setNowPlaying(winner->track);
        custom.setPaused(isBgmLinePaused(winner->line));

        if (custom.isAudible())
            sCustomEverAudible = true;
        sSilenceVanilla = sCustomEverAudible;
    }

    static const char* getBgmLineCurPlayName(const al::BgmLine* line) {
        if (!line)
            return nullptr;

        using Fn = const char* (*)(const al::BgmLine*);
        return mainOffsetFunction<Fn>(kBgmLineGetCurPlayNameOffset)(line);
    }

    static const char* getBgmLineCurResourceName(const al::BgmLine* line) {
        if (!line)
            return nullptr;

        using Fn = const char* (*)(const al::BgmLine*);
        return mainOffsetFunction<Fn>(kBgmLineGetCurResourceNameOffset)(line);
    }

    static void startMollusqueSustainedSituation(al::BgmDirector* director, const char* reason) {
        if (!director) {
            Randomizer::logPush("Music ML situation FAILED no director (%s)", reason);
            return;
        }

        using Fn = void (*)(al::BgmDirector*, const char*, bool, bool, bool);
        mainOffsetFunction<Fn>(kBgmDirectorStartSituationOffset)(
            director, kMollusqueSustainedSituation, true, false, true);
        Randomizer::logPush("Music ML situation started %s director=%p (%s)",
            kMollusqueSustainedSituation, director, reason);
    }

    static IntroContinuation* findIntroContinuation(const al::BgmLine* line) {
        for (IntroContinuation& continuation : sIntroContinuations)
            if (continuation.line == line)
                return &continuation;

        return nullptr;
    }

    static void clearIntroContinuation(const al::BgmLine* line, const char* reason = nullptr) {
        IntroContinuation* continuation = findIntroContinuation(line);
        if (!continuation)
            return;

        if (reason) {
            Randomizer::logPush("Music ML cancel %s line=%p frame=%d cur=%s",
                reason, line, continuation->activeFrames,
                getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)");
        }
        continuation->line = nullptr;
        continuation->director = nullptr;
        continuation->sawIntroPlaying = false;
        continuation->force8Bit = false;
        continuation->activeFrames = 0;
    }

    static void armMollusqueContinuation(al::BgmLine* line, const al::BgmPlayingRequest& introRequest) {
        IntroContinuation* continuation = nullptr;
        for (IntroContinuation& candidate : sIntroContinuations) {
            if (!candidate.line) {
                continuation = &candidate;
                break;
            }
        }

        if (!continuation) {
            Randomizer::logPush("Music ML arm FAILED no slot line=%p play=%s",
                line, introRequest.bgmName ? introRequest.bgmName : "(none)");
            return;
        }

        continuation->request = introRequest;
        continuation->director = sStartingBgmDirector;
        continuation->request._8 = nullptr;
        continuation->request._10 = kMollusqueSustainedResource;
        // Force BgmLine to replace the completed intro even though this continuation uses the
        // same logical play name. Without this, its same-request guard can leave the silent
        // one-shot selected instead of starting the sustained resource.
        continuation->request._2c = true;
        continuation->sawIntroPlaying = isBgmLineRunning(line);
        continuation->force8Bit = sForce8Bit;
        continuation->activeFrames = 0;
        continuation->line = line;
        Randomizer::logPush("Music ML armed line=%p play=%s running=%d 8bit=%d cur=%s",
            line, introRequest.bgmName ? introRequest.bgmName : "(none)",
            continuation->sawIntroPlaying, continuation->force8Bit,
            getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)");
    }

    static void logRemap(const char* playName, const char* originalResource, const char* remappedResource) {
        if (sRemapLogCount >= kMaxRemapLogs || al::isEqualString(originalResource, remappedResource))
            return;

        Randomizer::logPush("Music %s: %s -> %s", playName, originalResource, remappedResource);
        sRemapLogCount++;
    }

    static bool shouldLogSkippedBgm(const char* playName, const char* resourceName) {
        if (!playName || !playName[0])
            return false;
        if (!resourceName || !resourceName[0])
            return false;
        return startsWith(resourceName, "StmRsBgm");
    }

    static void logSkippedBgm(const char* reason, const char* playName, const char* resourceName) {
        if (sSkipLogCount >= kMaxSkipLogs || !shouldLogSkippedBgm(playName, resourceName))
            return;

        Randomizer::logPush("Music skip %s: %s (%s)", playName,
            resourceName && resourceName[0] ? resourceName : "(none)", reason);
        sSkipLogCount++;
    }

    static const char* getTrackDisplayName(const char* resourceName) {
        struct TrackName {
            const char* resourceName;
            const char* displayName;
        };

        static constexpr TrackName sTrackNames[] = {
            { "StmRsBgmHat", "Bonneton" },
            { "StmRsBgmFall", "Fossil Falls" },
            { "StmRsBgmDinosaur", "Fossil Falls: Dinosaur" },
            { "StmRsBgmDesertMain", "Tostarena: Ruins" },
            { "StmRsBgmDesertNight", "Tostarena: Night" },
            { "StmRsBgmDesertTown", "Tostarena: Town" },
            { "StmRsBgmDesertTownClct", "Tostarena: Town" },
            { "StmRsBgmJaguar", "Tostarena: Jaxi" },
            { "StmRsBgmForest", "Steam Gardens" },
            { "StmRsBgmBeforeGK", "Steam Gardens: Sherm" },
            { "StmRsBgmLakeScenario1", "Lake Lamode 1" },
            { "StmRsBgmLakeScenario2", "Lake Lamode 2" },
            { "StmRsBgmLakeTunnel", "Lake Lamode: Underwater Passage" },
            { "StmRsBgmClashFirst", "Forgotten Isle 1" },
            { "StmRsBgmClashSecond", "Forgotten Isle 2" },
            { "StmRsBgmCityScenario01", "New Donk City: Night 1" },
            { "StmRsBgmCityScenario01Strain", "New Donk City: Night 2" },
            { "StmRsBgmCityCafe01", "New Donk City: Cafe" },
            { "StmRsBgmCitySession", "New Donk City: Band Performance" },
            { "StmRsBgmCityScenario02", "New Donk City: Daytime" },
            { "StmRsBgmCityScenario03", "NDC Festival" },
            { "StmRsBgmCityScenario03Jp", "NDC Festival (Japanese)" },
            { "StmRsBgmCityScenario03Special", "NDC Festival (8-Bit)" },
            { "StmRsBgmCityScenario03Clct", "NDC Festival (8-Bit)" },
            { "StmRsBgmCityScenario04", "New Donk City: Rain" },
            { "StmRsBgmSea01", "Bubblaine: Underwater" },
            { "StmRsBgmSea01beach", "Bubblaine" },
            { "StmRsBgmSea02", "Bubblaine" },
            { "StmRsBgmSnowTown", "Shiveria: Town" },
            { "StmRsBgmSnowTownJingleBell", "Shiveria: Town (Bells)" },
            { "StmRsBgmSnowRaceEntrance", "Shiveria: Race-Course Entrance" },
            { "StmRsBgmLava", "Mount Volbono" },
            { "StmRsBgmLavaTown", "Mount Volbono: Town" },
            { "StmRsBgmSky01", "Bowser's Castle 1" },
            { "StmRsBgmSky02", "Bowser's Castle 2" },
            { "StmRsBgmMoon", "Honeylune Ridge" },
            { "StmRsBgmMoonDungeon", "Honeylune Ridge: Caves" },
            { "StmRsBgmMoonChurch", "Honeylune Ridge: Wedding Hall" },
            { "StmRsBgmMoonEscape", "Honeylune Ridge: Collapse" },
            { "StmRsBgmEndRock", "Honeylune Ridge: Escape" },
            { "StmRsBgmEndRockJp", "Honeylune Ridge: Escape (Japanese)" },
            { "StmRsBgmEndRockClct", "Honeylune Ridge: Escape (8-Bit)" },
            { "StmRsBgmPeachCastle", "Peach's Castle" },
            { "StmRsBgmExAthletic01", "Run, Jump, Throw! 1" },
            { "StmRsBgmExAthletic02", "Run, Jump, Throw! 2" },
            { "StmRsBgmExChika", "Subterranean 1" },
            { "StmRsBgmExChika2", "Subterranean 2" },
            { "StmRsBgmExHat", "Caves" },
            { "StmRsBgmExCold", "Ice" },
            { "StmRsBgmExPsyche", "Another World" },
            { "StmRsBgmExRuin", "Ruins" },
            { "StmRsBgmExBonus", "Above the Clouds" },
            { "StmRsBgmM1PL", "Projection Room: Above Ground" },
            { "StmRsBgmM1UG", "Projection Room: Underground" },
            { "StmRsBgmKinopioBrigade", "Toad Brigade Member" },
            { "StmRsBgmDemoWorldMap", "To the Next Kingdom" },
            { "StmRsBgmShop", "Shop" },
            { "StmRsBgmShopClct", "Shop" },
            { "StmRsBgmStaffRoll", "Staff Roll" },
            { "StmRsBgmTitle", "Title Screen" },
            { "StmRsBgmE3Title", "E3 Trailer" },
            { "StmRsBgmE3Title2", "E3 Trailer 2" },
            { "StmRsBgmDemoOpening", "Opening Cutscene" },
            { "StmRsBgmDesertMainParade", "Tostarena: Parade" },
            { "StmRsBgmMoonParade", "Honeylune Ridge: Parade" },
            { "StmRsBgmRace", "Race" },
            { "StmRsBgmRaceHurry", "Race: Final Lap" },
            { "StmRsBgmRaceResult", "Race: Results" },
            { "StmRsBgmRadioControlCar", "RC Car" },
            { "StmRsBgmSlotRoom", "Spinning Slots" },
            { "StmRsBgmExFantasy", "Climactic Duel!" },
            { "StmRsBgmMiniGame01", "Minigame" },
            { "StmRsBgmRaidWorld", "Ruined Kingdom" },
            { "StmRsBgmBossGateKeeper", "Boss: Broodals Battle" },
            { "StmRsBgmBossBreeda", "Boss: Madame Broode Battle" },
            { "StmRsBgmBossKnuckle", "Boss: Knucklotec Battle" },
            { "StmRsBgmBossForest", "Boss: Torkdrift Battle" },
            { "StmRsBgmBossMofumofu", "Boss: Mechawiggler Battle" },
            { "StmRsBgmBossHaikai_A", "Boss: Mollusque-Lanceur Intro" },
            { "StmRsBgmBossHaikai_B", "Boss: Mollusque-Lanceur Battle" },
            { "StmRsBgmBossMagma", "Boss: Cookatiel Battle" },
            { "StmRsBgmBossDragon", "Boss: Ruined Dragon Battle" },
            { "StmRsBgmBossGolem", "Boss: RoboBrood Battle" },
            { "StmRsBgmBossKoopa01", "Boss: Bowser Battle 1" },
            { "StmRsBgmBossKoopa02", "Boss: Bowser Battle 2" },
        };

        for (const TrackName& track : sTrackNames)
            if (al::isEqualString(track.resourceName, resourceName))
                return track.displayName;

        return resourceName;
    }

    static void setNowPlaying(const char* resourceName) {
        if (!resourceName || !resourceName[0])
            return;
        if (isEqual(resourceName, sLastShownResource))
            return;

        nn::util::SNPrintf(sLastShownResource, sizeof(sLastShownResource), "%s", resourceName);

        bool is8Bit = false;
        char baseResource[80];
        const char* suffix = std::strstr(resourceName, "_8Bit");
        if (suffix) {
            is8Bit = true;
            size_t len = suffix - resourceName;
            if (len >= sizeof(baseResource))
                len = sizeof(baseResource) - 1;
            std::strncpy(baseResource, resourceName, len);
            baseResource[len] = '\0';
            resourceName = baseResource;
        }

        const char* displayName = getTrackDisplayName(resourceName);
        if (is8Bit) {
            nn::util::SNPrintf(sNowPlayingTitle, sizeof(sNowPlayingTitle), "Music: %s (8-Bit)", displayName);
        } else {
            nn::util::SNPrintf(sNowPlayingTitle, sizeof(sNowPlayingTitle), "Music: %s", displayName);
        }
        sNowPlayingFrames = kNowPlayingFrames;
    }

    enum class RequestRemapResult : u8
    {
        NotRemapped,
        Remapped,
        Suppressed,
    };

    static RequestRemapResult applyRequestRemap(al::BgmLine* line, al::BgmPlayingRequest* request) {
        if (!line || !request || !isReady())
            return RequestRemapResult::NotRemapped;

        const char* playName = request->bgmName;
        if (!playName || !playName[0])
            return RequestRemapResult::NotRemapped;
        const char* originalResource = request->_10;
        if (!originalResource)
            originalResource = line->getResourceName(playName);

        // Music Toad and the pause-menu Music List play the selected resource through
        // this dedicated line. Preserve the player's explicit selection.
        if (isEqual(playName, "CollectBgm")) {
            sForce8Bit = false;
            sForce3D = false;
            return RequestRemapResult::NotRemapped;
        }

        if (!originalResource || !originalResource[0]) {
            // Many "Ex" challenge rooms and other subareas define no Stage BGM at
            // all in vanilla. Fill that silence with a shuffled track (picked
            // deterministically from the current stage) instead of dead air.
            const char* substitute = isEqual(playName, "Stage")
                ? MusicRando::pickResourceForSilentStage(getCurrentStageName())
                : nullptr;
            if (!substitute) {
                logSkippedBgm("no resource", playName, originalResource);
                return RequestRemapResult::NotRemapped;
            }
            originalResource = substitute;
        }

        sForce8Bit = false;
        sForce3D = false;

        const Randomizer::MusicRandoMode mode = Randomizer::getMusicRandoMode();
        const char* fixedRemap = MusicRando::remapResourceName(playName, originalResource);
        // Preserve the existing source-pool boundary for Standard and Max.
        // Extended gets a chance to replace a source only through its
        // stage-aware resolver below.
        if (!fixedRemap
            || (mode != Randomizer::MusicRandoMode::Extended
                && isEqual(originalResource, fixedRemap))) {
            logSkippedBgm("not randomized", playName, originalResource);
            return RequestRemapResult::NotRemapped;
        }

        const char* remappedResource = fixedRemap;
        if (mode == Randomizer::MusicRandoMode::Extended) {
            // Extended mode keys the assignment by the physical stage and the
            // vanilla source cue. This separates subareas that happen to share
            // a resource while keeping each cue stable on every revisit.
            const char* areaRemap = MusicRando::remapResourceNameForStage(
                getCurrentStageName(), originalResource);
            if (areaRemap && !isEqual(originalResource, areaRemap))
                remappedResource = areaRemap;
        }

        if (!remappedResource
            || (isEqual(originalResource, remappedResource)
                && mode != Randomizer::MusicRandoMode::Max)) {
            logSkippedBgm("not randomized", playName, originalResource);
            return RequestRemapResult::NotRemapped;
        }

        if (mode == Randomizer::MusicRandoMode::Max) {
            // Only suppress a duplicate-source re-pick while that pick is still audibly
            // playing. sLastMaxSourceResource is never reset on a stage/zone change, so
            // without the isBgmLineRunning check, walking through a loading zone into an
            // area whose vanilla theme happens to share the same source as wherever we
            // just were would leave this line permanently silent (nothing plays).
            if (sLastMaxSourceResourceValid
                && isEqual(sLastMaxSourceResource, originalResource)
                && isBgmLineRunning(line)) {
                logSkippedBgm("Max duplicate source", playName, originalResource);
                return RequestRemapResult::Suppressed;
            }

            remappedResource = MusicRando::pickRuntimeRandomResource();
            if (!remappedResource) {
                logSkippedBgm("Max empty pool", playName, originalResource);
                return RequestRemapResult::NotRemapped;
            }
            nn::util::SNPrintf(sLastMaxSourceResource, sizeof(sLastMaxSourceResource), "%s", originalResource);
            sLastMaxSourceResourceValid = true;
        }

        if (!remappedResource || isEqual(originalResource, remappedResource)) {
            // Max intentionally permits the random draw to match the source;
            // it was still a fresh draw and must replace this request.
            if (mode != Randomizer::MusicRandoMode::Max) {
                logSkippedBgm("not randomized", playName, originalResource);
                return RequestRemapResult::NotRemapped;
            }
        }

        if (contains(remappedResource, "_8Bit")) {
            sForce8Bit = true;
            const char* suffix = std::strstr(remappedResource, "_8Bit");
            size_t len = suffix - remappedResource;
            if (len >= sizeof(sActualResource))
                len = sizeof(sActualResource) - 1;
            std::strncpy(sActualResource, remappedResource, len);
            sActualResource[len] = '\0';
            request->_10 = sActualResource;
        } else {
            sForce3D = true;
            request->_10 = remappedResource;
        }

        if (contains(originalResource, "BossHaikai") || contains(remappedResource, "BossHaikai")) {
            Randomizer::logPush("Music ML remap play=%s original=%s target=%s",
                playName, originalResource, remappedResource);
        }
        logRemap(playName, originalResource, remappedResource);
        setNowPlaying(remappedResource);
        return RequestRemapResult::Remapped;
    }

    static HkTrampoline<void*, al::BgmDirector*, const al::BgmPlayingRequest&, bool>
        bgmDirectorStartHook = hk::hook::trampoline([](al::BgmDirector* director,
                                                        const al::BgmPlayingRequest& request,
                                                        bool holdSituation) -> void* {
            al::BgmDirector* previousDirector = sStartingBgmDirector;
            const bool previousStartSituation = sStartMollusqueSustainedSituation;
            sStartingBgmDirector = director;
            sStartMollusqueSustainedSituation = false;

            void* result = bgmDirectorStartHook.orig(director, request, holdSituation);
            const bool startSituation = sStartMollusqueSustainedSituation;

            sStartingBgmDirector = previousDirector;
            sStartMollusqueSustainedSituation = previousStartSituation;

            if (startSituation)
                startMollusqueSustainedSituation(director, "randomized direct");

            if (sForce8Bit) {
                using Fn = void (*)(al::BgmDirector*, const char*, bool, bool, bool);
                mainOffsetFunction<Fn>(kBgmDirectorStartSituationOffset)(
                    director, "In2DArea", true, true, false);
            }
            return result;
        });

    static HkTrampoline<void, al::BgmDirector*, const char*, bool, bool, bool>
        bgmDirectorStartSituationHook = hk::hook::trampoline([](al::BgmDirector* director, const char* situationName, bool unk1, bool unk2, bool unk3) -> void {
            Randomizer::logPush("BGM Situation Triggered: %s (args: %d, %d, %d)", situationName ? situationName : "NULL", unk1, unk2, unk3);
            if (sForce3D && isEqual(situationName, "In2DArea")) {
                // Block the 2D audio situation to keep the 3D track playing
                return;
            }
            if (sForce8Bit && situationName && !isEqual(situationName, "In2DArea")
                && !isEqual(situationName, kMollusqueSustainedSituation)) {
                // We want to force the 8-bit track to play, so we change the situation to In2DArea
                situationName = "In2DArea";
            }
            bgmDirectorStartSituationHook.orig(director, situationName, unk1, unk2, unk3);
        });

    static HkTrampoline<void, al::BgmLine*, s32, bool> bgmLineStopAllHook = hk::hook::trampoline([](al::BgmLine* line, s32 fadeFrames, bool immediately) -> void {
        clearIntroContinuation(line, "stop-all");
        if (sCollectBgmLine == line)
            sCollectBgmLine = nullptr;
        // The line is done with whatever it was playing. Dropping its record is
        // all that is needed: resolveCustomMusic will pick the next-newest
        // running line on the following frame, which is what uncovers a theme
        // that a layer on top of it had been replacing.
        releaseRecord(line);
        bgmLineStopAllHook.orig(line, fadeFrames, immediately);
    });

    static HkTrampoline<void, al::BgmLine*, const al::BgmPlayingRequest&>
        startBgmHook = hk::hook::trampoline([](al::BgmLine* line, const al::BgmPlayingRequest& request) -> void {
            if (!sDiagnosticBannerLogged) {
                Randomizer::logPush("Music ML diagnostics active v4");
                sDiagnosticBannerLogged = true;
            }

            if (isEqual(request.bgmName, "CollectBgm")) {
                // Remember which line Music Toad / the pause-menu Music List is
                // using so the next *different* track (below) can force it off.
                sCollectBgmLine = line;
            } else if (sCollectBgmLine && sCollectBgmLine != line) {
                // Music Toad / pause-menu Music List: vanilla can fail to ever
                // call CollectBgmPlayer::stop() once a track is selected, leaving
                // it looping forever underneath. Force it off the moment anything
                // else is about to play, rather than blocking it outright (that
                // also stalled the EventFlow gates that wait for it to start).
                bgmLineStopAllHook.orig(sCollectBgmLine, 0, true);
                sCollectBgmLine = nullptr;
            } else if (sCollectBgmLine == line) {
                // That line has moved on to ordinary music, so it is no longer
                // the Music List's. Leaving the pointer set kept it permanently
                // exempt from silencing, which is how Captain Toad's theme and
                // the Odyssey map could still be heard over a custom track.
                sCollectBgmLine = nullptr;
            }

            clearIntroContinuation(line, "new-start");

            // This hook only *records* what this line was asked to play and
            // which side its cue falls on. Which line the player actually hears,
            // and what the backend does about it, is derived once a frame by
            // resolveCustomMusic -- so there is no ownership to get stale here.
            const Randomizer::MusicSourceMode sourceMode = Randomizer::getMusicSourceMode();
            const bool cueExempt = isCueExemptFromCustomMusic(request.bgmName);
            const bool cueEligible = request.bgmName && request.bgmName[0]
                && !isEqual(request.bgmName, "CollectBgm") && !cueExempt;

            if (Randomizer::isCustomMusicPossible() && !sTakeoverDisabled
                && !CustomBgmManager::instance().isFaulted()) {
                CustomBgmManager::instance().init();

                if (BgmLineRecord* record = acquireRecord(line)) {
                    record->exempt = cueExempt;

                    if (cueEligible) {
                        // Keyed the way MusicRando keys the vanilla shuffle, so
                        // a custom track changes exactly when the vanilla one
                        // would rather than on every area change.
                        const char* originalResource = request._10;
                        if (!originalResource && request.bgmName)
                            originalResource = line->getResourceName(request.bgmName);

                        char cueKey[192];
                        const char* stageName = getCurrentStageName();
                        if (!originalResource || !originalResource[0]) {
                            nn::util::SNPrintf(cueKey, sizeof(cueKey), "%s|%s",
                                stageName ? stageName : "", request.bgmName);
                        } else if (Randomizer::getMusicRandoMode()
                            == Randomizer::MusicRandoMode::Extended) {
                            nn::util::SNPrintf(cueKey, sizeof(cueKey), "%s|%s",
                                stageName ? stageName : "", originalResource);
                        } else {
                            nn::util::SNPrintf(cueKey, sizeof(cueKey), "%s", originalResource);
                        }

                        // Only a genuinely new cue re-draws. A line that simply
                        // re-requests what it already has -- the shop radio does
                        // this constantly -- keeps its answer.
                        if (!isEqual(cueKey, record->cueKey)) {
                            nn::util::SNPrintf(
                                record->cueKey, sizeof(record->cueKey), "%s", cueKey);

                            const bool maxMode = Randomizer::getMusicRandoMode()
                                == Randomizer::MusicRandoMode::Max;
                            // Max redraws per request, including which side the
                            // cue lands on; the stable modes keep it per cue.
                            const bool wantCustom
                                = sourceMode == Randomizer::MusicSourceMode::Custom
                                || (maxMode ? CustomBgmManager::instance().rollPrefersCustom()
                                            : CustomBgmManager::instance().cuePrefersCustom(cueKey));

                            const char* track = nullptr;
                            if (wantCustom) {
                                track = maxMode
                                    ? CustomBgmManager::instance().getRuntimeRandomCustomMusic(cueKey)
                                    : CustomBgmManager::instance().getCustomMusicForCue(cueKey);
                            }

                            if (track) {
                                nn::util::SNPrintf(
                                    record->track, sizeof(record->track), "%s", track);
                            } else {
                                record->track[0] = '\0';
                                Randomizer::logPush("[CustomBgm] cue %s -> vanilla", cueKey);
                            }
                        }

                        record->startSeq = sNextStartSeq++;

                        if (sStartWindow <= 0) {
                            sStartWindow = 180;
                            sStartCount = 0;
                        }
                        if (++sStartCount > 40) {
                            // A line is restarting every frame despite being
                            // silenced; hand the audio back rather than thrash
                            // the stream loader.
                            sTakeoverDisabled = true;
                            Randomizer::logPush("[CustomBgm] restart storm, custom music disabled");
                        }
                    }
                }
            }

            al::BgmPlayingRequest remappedRequest = request;
            const RequestRemapResult remapResult = applyRequestRemap(line, &remappedRequest);
            if (remapResult == RequestRemapResult::Suppressed)
                return;
            const bool wasRemapped = remapResult == RequestRemapResult::Remapped;
            const char* requestedResource = request._10;
            if (!requestedResource && request.bgmName)
                requestedResource = line->getResourceName(request.bgmName);
            if (contains(requestedResource, "BossHaikai")
                || contains(remappedRequest._10, "BossHaikai")
                || contains(request.bgmName, "Haikai")) {
                Randomizer::logPush("Music ML pre line=%p play=%s requested=%s override=%s",
                    line,
                    request.bgmName ? request.bgmName : "(none)",
                    requestedResource ? requestedResource : "(none)",
                    remappedRequest._10 ? remappedRequest._10 : "(none)");
            }

            startBgmHook.orig(line, remappedRequest);

            const char* effectiveResource = remappedRequest._10;
            if (!effectiveResource && remappedRequest.bgmName)
                effectiveResource = line->getResourceName(remappedRequest.bgmName);
            if (contains(effectiveResource, "BossHaikai")
                || contains(getBgmLineCurResourceName(line), "BossHaikai")) {
                Randomizer::logPush("Music ML post line=%p effective=%s cur=%s running=%d",
                    line,
                    effectiveResource ? effectiveResource : "(none)",
                    getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)",
                    isBgmLineRunning(line));
            }
            if (wasRemapped && isEqual(effectiveResource, kMollusqueSustainedResource))
                sStartMollusqueSustainedSituation = true;

            if (wasRemapped && isEqual(effectiveResource, kMollusqueIntroResource)) {
                Randomizer::logPush("Music ML start detected remapped=%d req=%s effective=%s",
                    wasRemapped,
                    request.bgmName ? request.bgmName : "(none)",
                    effectiveResource);
                armMollusqueContinuation(line, remappedRequest);
            }
        });

    static HkTrampoline<void, al::BgmLine*> bgmLineUpdateHook = hk::hook::trampoline([](al::BgmLine* line) -> void {
        // Clamp after the game has written this line's volume for the frame.
        // Applied to every line, not just ones with a record: a door or loading
        // zone fades its theme out on a line the takeover never saw. The
        // CollectBgm line is left alone so Music Toad and the pause-menu Music
        // List still work.
        if (sSilenceVanilla && line != sCollectBgmLine) {
            BgmLineRecord* record = findRecord(line);
            // The exemption follows what the line is *currently* playing rather
            // than what it was last asked to start: a jingle line falls back to
            // the theme underneath it without another startBgm.
            if (record && record->exempt
                && !isCueExemptFromCustomMusic(getBgmLineCurPlayName(line)))
                record->exempt = false;

            // Off silences everything; the exemptions exist only so a jingle can
            // be heard over a custom track.
            const bool exempt = record && record->exempt
                && Randomizer::getMusicSourceMode() != Randomizer::MusicSourceMode::Off;
            if (!exempt)
                silenceBgmLine(line);
        }

        static s32 sSkipTriggerCount = 0;
        static s32 sSkipTriggerTimer = 0;
        static al::BgmLine* sMainLine = nullptr;

        if (sMainLine == nullptr || !isBgmLineRunning(sMainLine)) {
            if (isBgmLineRunning(line)) {
                sMainLine = line;
            }
        }

        if (sMainLine == line) {
            static bool sWasLDown = false;
            bool isLDown = al::isPadHoldL(-1);
            if (isLDown && !sWasLDown) {
                sSkipTriggerCount++;
                sSkipTriggerTimer = 45;
            } else if (sSkipTriggerTimer > 0) {
                sSkipTriggerTimer--;
                if (sSkipTriggerTimer == 0)
                    sSkipTriggerCount = 0;
            }
            sWasLDown = isLDown;

            if (sSkipTriggerCount >= 3) {
                sSkipTriggerCount = 0;
                sSkipTriggerTimer = 0;

                if (isReady() && isBgmLineRunning(line) && !isBgmLinePaused(line)) {
                    const char* playName = getBgmLineCurPlayName(line);
                    if (playName && playName[0]) {
                        clearIntroContinuation(line, "triple-l");
                        bgmLineStopAllHook.orig(line, 0, true);

                        al::BgmPlayingRequest req { nullptr };
                        req.bgmName = playName;
                        req._2c = true;
                        startBgmHook.orig(line, req);
                        return;
                    }
                }
            }
        }

        IntroContinuation* continuation = findIntroContinuation(line);
        if (!continuation)
            return;

        const bool isRunning = isBgmLineRunning(line);
        if (isRunning)
            continuation->sawIntroPlaying = true;

        if (isRunning && !isBgmLinePaused(line)) {
            continuation->activeFrames++;
            if (continuation->activeFrames == 1 || continuation->activeFrames == 240) {
                Randomizer::logPush("Music ML tick line=%p frame=%d running=%d cur=%s",
                    line, continuation->activeFrames, isRunning,
                    getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)");
            }
            if (continuation->activeFrames < kMollusqueIntroFrames)
                return;
        } else if (!continuation->sawIntroPlaying || isRunning) {
            return;
        }

        const al::BgmPlayingRequest sustainedRequest = continuation->request;
        al::BgmDirector* director = continuation->director;
        const bool sustained8Bit = continuation->force8Bit;
        clearIntroContinuation(line);
        Randomizer::logPush("Music ML handoff line=%p play=%s before=%s running=%d 8bit=%d",
            line,
            sustainedRequest.bgmName ? sustainedRequest.bgmName : "(none)",
            getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)",
            isRunning, sustained8Bit);
        // This starts the line directly, so bgmDirectorStartHook - the only place
        // sForce8Bit is turned into a real In2DArea situation - never runs for the
        // handoff. Restore this chain's own 2D/3D intent (whatever else played
        // during the intro has moved the globals on) and apply the situations here.
        sForce8Bit = sustained8Bit;
        sForce3D = !sustained8Bit;
        startBgmHook.orig(line, sustainedRequest);
        startMollusqueSustainedSituation(director, "intro handoff");
        if (sustained8Bit && director) {
            using Fn = void (*)(al::BgmDirector*, const char*, bool, bool, bool);
            mainOffsetFunction<Fn>(kBgmDirectorStartSituationOffset)(
                director, "In2DArea", true, true, false);
        }
        Randomizer::logPush("Music ML result line=%p play=%s cur=%s running=%d",
            line,
            getBgmLineCurPlayName(line) ? getBgmLineCurPlayName(line) : "(none)",
            getBgmLineCurResourceName(line) ? getBgmLineCurResourceName(line) : "(none)",
            isBgmLineRunning(line));
        setNowPlaying(sustained8Bit ? kMollusqueSustained8BitResource : kMollusqueSustainedResource);
    });

    static HkTrampoline<void, al::BgmLine*, const char*, s32> bgmLineResetHook = hk::hook::trampoline([](al::BgmLine* line, const char* resourceName, s32 situationIndex) -> void {
        clearIntroContinuation(line, "reset");
        // A reset rebuilds the line's play info, so its record is stale. The
        // custom track is not stopped here: with no running line to win,
        // resolveCustomMusic ducks it for a grace period, so crossing a loading
        // zone fades out and can fade the same track back in.
        releaseRecord(line);
        sTakeoverDisabled = false;
        sStartCount = 0;
        sStartWindow = 0;
        bgmLineResetHook.orig(line, resourceName, situationIndex);
    });

    // The one place the custom-music decision is made, once a frame, before the
    // line keeper runs the lines -- so the clamps applied during orig() below
    // act on a decision taken this frame rather than the last one.
    static HkTrampoline<void, al::BgmDirector*> bgmDirectorUpdateHook
        = hk::hook::trampoline([](al::BgmDirector* director) -> void {
              resolveCustomMusic();
              bgmDirectorUpdateHook.orig(director);
          });

    static HkTrampoline<bool, const char*, const char*> isEqualStringHook = hk::hook::trampoline([](const char* lhs, const char* rhs) -> bool {
        if (!lhs || !rhs)
            return lhs == rhs;

        return isEqualStringHook.orig(lhs, rhs);
    });

    static HkTrampoline<bool, GameDataFile*, const char*, const char*> trySetCollectedBgmHook = hk::hook::trampoline([](GameDataFile* dataFile, const char* name, const char* situationName) -> bool {
        return trySetCollectedBgmHook.orig(dataFile, name, situationName);
    });

    static HkTrampoline<bool, const GameDataFile*, const char*, const char*>
        isCollectedBgmHook = hk::hook::trampoline([](const GameDataFile* dataFile,
                                                      const char* name,
                                                      const char* situationName) -> bool {
            // Expose the complete Music List from a fresh save. The list has a fixed
            // 82-entry catalogue and uses this predicate to decide whether each row is
            // selectable; no save-data mutation is needed.
            if (dataFile && name && name[0])
                return true;
            return isCollectedBgmHook.orig(dataFile, name, situationName);
        });

    static HkTrampoline<bool, void*> isGameClearHook = hk::hook::trampoline([](void* accessor) -> bool {
        uintptr_t lr = reinterpret_cast<uintptr_t>(__builtin_return_address(0));
        uintptr_t moduleBase = hk::ro::getMainModule()->range().start();
        uintptr_t offset = lr - moduleBase;

        // StageSceneStateCollectionList offsets are around 0x4da000 to 0x4db000
        // (e.g. cStageSceneStateCollectionListExeViewBgmListOffset = 0x4dad60)
        // If the call comes from this range, it's checking if the Music List tab should be shown.
        if (offset >= 0x4d9000 && offset <= 0x4dc000) {
            return true;
        }

        return isGameClearHook.orig(accessor);
    });

    void installHooks() {
        isEqualStringHook.installAtSym<"_ZN2al13isEqualStringEPKcS1_">();
        // GameDataFile::isCollectedBgm is not exported in main.sym on 1.0.0.
        isCollectedBgmHook.installAtMainOffset(0x00526434);
        trySetCollectedBgmHook.installAtSym<"_ZN12GameDataFile18trySetCollectedBgmEPKcS1_">();
        bgmDirectorStartHook.installAtMainOffset(kBgmDirectorStartOffset);
        bgmDirectorStartSituationHook.installAtMainOffset(kBgmDirectorStartSituationOffset);
        startBgmHook.installAtSym<"_ZN2al7BgmLine8startBgmERKNS_17BgmPlayingRequestE">();
        bgmLineResetHook.installAtMainOffset(kBgmLineResetOffset);
        bgmLineUpdateHook.installAtMainOffset(kBgmLineUpdateOffset);
        bgmDirectorUpdateHook.installAtMainOffset(kBgmDirectorUpdateOffset);
        bgmLineStopAllHook.installAtMainOffset(kBgmLineStopAllOffset);
        isGameClearHook.installAtSym<"_ZN16GameDataFunction11isGameClearE22GameDataHolderAccessor">();
        Randomizer::logPush("Music ML hooks installed v6");
    }

    void resetMaxRequestState() {
        sLastMaxSourceResource[0] = '\0';
        sLastMaxSourceResourceValid = false;
    }

    void drawNowPlayingOverlay() {
        if (!Randomizer::isMusicOverlayEnabled()) {
            sNowPlayingFrames = 0;
            return;
        }

        if (sNowPlayingFrames <= 0)
            return;

        const f32 alpha = sNowPlayingFrames < 45 ? (f32)sNowPlayingFrames / 45.0f : 1.0f;
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 24.0f, io.DisplaySize.y - 24.0f),
            ImGuiCond_Always, ImVec2(1.0f, 1.0f));
        ImGui::SetNextWindowBgAlpha(0.72f * alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14.0f, 10.0f));
        ImGui::Begin("Music Rando Now Playing", nullptr,
            ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoInputs);
        ImGui::TextUnformatted(sNowPlayingTitle);
        ImGui::End();
        ImGui::PopStyleVar(3);

        sNowPlayingFrames--;
    }

} // namespace MusicHooks

const char* MusicRando::getTrackDisplayName(const char* resourceName) {
    return MusicHooks::getTrackDisplayName(resourceName);
}
