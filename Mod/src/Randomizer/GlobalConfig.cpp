#include "Randomizer/GlobalConfig.h"

#include "Randomizer/Randomizer.h"

#include "nn/fs.h"

#include "SDRedirect/SDPaths.h"
#include <cstring>

namespace GlobalConfig {
    namespace {
        constexpr u32 cMagic = 0x47464353; // 'SCFG'
        constexpr u32 cVersion = 1;
        constexpr u32 cFlagMultiplayerEnabled = 1u << 0;
        constexpr u32 cFlagQolInitialized = 1u << 1;
        constexpr u32 cFlagFastMoonDemos = 1u << 2;
        constexpr u32 cFlagMotionFreeControls = 1u << 3;
        constexpr u32 cFlagSkipCutsceneCameras = 1u << 4;
        // Bit 5 held the retired Costume Doors Open toggle. Costume doors are
        // now always open, so the bit is reserved and never read or written.
        constexpr u32 cFlagOneSheepHerding = 1u << 6;
        constexpr u32 cFlagTrackerOverlay = 1u << 7;
        constexpr u32 cFlagEasyMinigames = 1u << 8;
        constexpr u32 cFlagAreaNameOverlay = 1u << 9;
        // Tracker mode is packed across flags for backward compatibility:
        // cFlagTrackerOverlay alone (old configs) means "on" -> Standard; adding
        // cFlagTrackerOverlayTime means Complete (Standard + playtime); adding
        // cFlagTrackerOverlayHints on top of that means Hints (Complete + a plain
        // per-kingdom reachability dot); adding cFlagTrackerOverlayGuided on top
        // of THAT means Guided (Hints, but the dot/count also judge importance).
        constexpr u32 cFlagTrackerOverlayTime = 1u << 10;
        constexpr u32 cFlagTrackerOverlayHints = 1u << 11;
        constexpr u32 cFlagTrackerOverlayGuided = 1u << 12;
        // 00 remains Medium so existing configs keep the established board size.
        constexpr u32 cFlagBoardScaleSmall = 1u << 13;
        constexpr u32 cFlagBoardScaleLarge = 1u << 14;
        constexpr u32 cFlagNoClip = 1u << 15;
        constexpr u32 cFlagFreeCostumeMoon = 1u << 16;
        constexpr u32 cFlagTrackerRightAligned = 1u << 17;
        constexpr u32 cFlagTrackerCaveSkipEasy = 1u << 18;
        constexpr u32 cFlagTrackerCaveSkipHard = 1u << 19;
        constexpr u32 cFlagCompletedKingdomMoonCollected = 1u << 20;
        constexpr u32 cFlagRandomizeOutfits = 1u << 21;
        // 00 is None, so existing configs keep uncolored tracker counters.
        constexpr u32 cFlagTrackerMoonLimitUpper = 1u << 22;
        constexpr u32 cFlagTrackerMoonLimitFull = 1u << 23;
        constexpr u32 cFlagAlwaysNeutralCapThrow = 1u << 24;
        // Not a seed setting: the seed's chaos categories, and the swap
        // assignment built from them, are untouched -- this only stops the
        // runtime from applying that assignment. It lives here so a player who
        // turned it on to get past a crash does not have to turn it on again
        // after a reboot.
        constexpr u32 cFlagChaosForceDisabled = 1u << 25;
        constexpr u32 cFlagUnlockHintArt = 1u << 26;
        // Inverted on purpose (set = classic, not set = Tabbed): every
        // existing config.bin already on someone's SD card reads this new
        // bit as 0 regardless of what setDefaults() says (setDefaults()
        // only runs for a config file that doesn't exist yet) - "unset
        // means Tabs" keeps everyone already running the Tab rework on
        // Tabs with zero behavior change from this update, while a normal
        // (non-inverted) "Tabbed" flag would have silently reverted every
        // existing install back to the classic list. The community poll on
        // Tabs vs. the classic layout is still open; this is the explicit
        // opt-out for testers who want the classic list back.
        // Retired with the Tab system. The bit stays reserved rather than
        // being reused: an old config file still has it set, and a new
        // setting inheriting the bit would read that as its own value.
        // constexpr u32 cFlagClassicSettingsUI = 1u << 27;
        constexpr u32 cFlagFastDog = 1u << 28;
        constexpr u32 cFlagUncappedCoins = 1u << 29;

        constexpr u16 cDefaultServerPort = 1027;
        constexpr const char* cDefaultServerIP = "176.189.251.231";
        constexpr const char* cConfigPath = "sd:/SMO-Randomizer/config.bin";

        // 0 means "not set" (what every existing config's zero-filled reserved bytes
        // already read as) so old configs pick up the real defaults below instead of
        // silently mapping to Randomizer::ControllerButton::A. Real button values are
        // offset by +1 from Randomizer::ControllerButton's own enumerators.
        constexpr u8 cButtonUnset = 0;

        struct ConfigData {
            u32 magic;
            u32 version;
            u32 flags;
            u16 serverPort;
            u8 pad[2];
            char serverIP[0x60];
            u8 upThrowButton;
            u8 downThrowJumpButton;
            u8 spinShakeButton;
            // Multiplayer team, 0-based, taken out of the reserved block. No
            // version bump needed and none of the button fields' +1 "unset"
            // trick either: an existing config's zero-filled reserved bytes
            // read as team 0, which is Team 1 - exactly the default a config
            // written before teams existed should get.
            u8 team;
            // Wave D QoL values use the remaining bytes of the fixed-size
            // config record. Zero means an older config, so applyLoaded()
            // falls back to the established 10/25/1 defaults in that case.
            u16 easyMinigameLow;
            u16 easyMinigameHigh;
            u8 sheepHerdingCount;
            u8 reserved[7];
        };

        static_assert(sizeof(ConfigData) == 0x80, "Global config must stay fixed-size");

        ConfigData sConfig = {};
        bool sLoaded = false;

        void copyString(char* dst, size_t dstSize, const char* src) {
            if (!dst || dstSize == 0)
                return;
            dst[0] = '\0';
            if (!src)
                return;

            std::strncpy(dst, src, dstSize - 1);
            dst[dstSize - 1] = '\0';
        }

        void setDefaults() {
            std::memset(&sConfig, 0, sizeof(sConfig));
            sConfig.magic = cMagic;
            sConfig.version = cVersion;
            sConfig.serverPort = cDefaultServerPort;
            copyString(sConfig.serverIP, sizeof(sConfig.serverIP), cDefaultServerIP);
            sConfig.easyMinigameLow = Randomizer::kEasyMinigameDefaultLow;
            sConfig.easyMinigameHigh = Randomizer::kEasyMinigameDefaultHigh;
            sConfig.sheepHerdingCount = Randomizer::kSheepHerdingDefaultCount;
            sConfig.flags = cFlagQolInitialized
                | cFlagSkipCutsceneCameras
                | cFlagAreaNameOverlay;
        }

        bool isValid(const ConfigData& config) {
            return config.magic == cMagic
                && config.version == cVersion
                && config.serverPort != 0
                && config.serverIP[0] != '\0';
        }

        bool readConfig(ConfigData* out) {
            if (!out)
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, cConfigPath, nn::fs::OpenMode_Read).IsFailure())
                return false;

            long fileSize = 0;
            const bool sizeOk = nn::fs::GetFileSize(&fileSize, handle).IsSuccess() && fileSize == sizeof(ConfigData);
            const bool readOk = sizeOk && nn::fs::ReadFile(handle, 0, out, sizeof(ConfigData)).IsSuccess();
            nn::fs::CloseFile(handle);
            if (!readOk)
                return false;

            out->serverIP[sizeof(out->serverIP) - 1] = '\0';
            return isValid(*out);
        }

        bool writeConfig(const ConfigData& config) {
            SDPaths::ensureRandomizerRoot();
            nn::fs::DeleteFile(cConfigPath);
            if (nn::fs::CreateFile(cConfigPath, sizeof(ConfigData)).IsFailure())
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, cConfigPath, nn::fs::OpenMode_Write).IsFailure())
                return false;

            const bool ok = nn::fs::WriteFile(handle, 0, &config, sizeof(ConfigData),
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            return ok;
        }

        void setFlag(u32 flag, bool enabled) {
            if (enabled)
                sConfig.flags |= flag;
            else
                sConfig.flags &= ~flag;
        }

        u8 toStoredButton(Randomizer::ControllerButton button) {
            return static_cast<u8>(button) + 1;
        }

        // Returns cButtonUnset for anything out of range, including a genuinely-unset
        // (zero) field - callers fall back to the real default in that case.
        u8 clampStoredButton(u8 raw) {
            return raw >= 1 && raw <= Randomizer::cControllerButtonCount ? raw : cButtonUnset;
        }

        void captureQol() {
            sConfig.flags |= cFlagQolInitialized;
            setFlag(cFlagFastMoonDemos, Randomizer::isFastMoonDemosEnabled());
            setFlag(cFlagMotionFreeControls, Randomizer::isMotionFreeControlsEnabled());
            setFlag(cFlagSkipCutsceneCameras, Randomizer::isSkipCutsceneCamerasEnabled());
            setFlag(cFlagFreeCostumeMoon, Randomizer::isFreeCostumeMoonEnabled());
            setFlag(cFlagOneSheepHerding, Randomizer::isOneSheepHerdingEnabled());
            const Randomizer::TrackerOverlayMode trackerMode = Randomizer::getTrackerOverlayMode();
            setFlag(cFlagTrackerOverlay, trackerMode != Randomizer::TrackerOverlayMode::None);
            setFlag(cFlagTrackerOverlayTime,
                trackerMode == Randomizer::TrackerOverlayMode::Extended
                    || trackerMode == Randomizer::TrackerOverlayMode::Hints
                    || trackerMode == Randomizer::TrackerOverlayMode::Guided);
            setFlag(cFlagTrackerOverlayHints,
                trackerMode == Randomizer::TrackerOverlayMode::Hints
                    || trackerMode == Randomizer::TrackerOverlayMode::Guided);
            setFlag(cFlagTrackerOverlayGuided, trackerMode == Randomizer::TrackerOverlayMode::Guided);
            const Randomizer::BoardScale boardScale = Randomizer::getBoardScale();
            setFlag(cFlagBoardScaleSmall, boardScale == Randomizer::BoardScale::Small);
            setFlag(cFlagBoardScaleLarge, boardScale == Randomizer::BoardScale::Large);
            setFlag(cFlagEasyMinigames, Randomizer::isEasyMinigamesEnabled());
            setFlag(cFlagFastDog, Randomizer::isFastDogEnabled());
            setFlag(cFlagUncappedCoins, Randomizer::isUncappedCoinsEnabled());
            setFlag(cFlagUnlockHintArt, Randomizer::isUnlockHintArtEnabled());
            setFlag(cFlagAreaNameOverlay, Randomizer::isAreaNameOverlayEnabled());
            setFlag(cFlagNoClip, Randomizer::isNoClipEnabled());
            setFlag(cFlagTrackerRightAligned, Randomizer::isTrackerRightAligned());
            setFlag(cFlagCompletedKingdomMoonCollected,
                Randomizer::isCompletedKingdomMoonCollectedEnabled());
            setFlag(cFlagRandomizeOutfits,
                Randomizer::isRandomizeOutfitsEnabled());
            setFlag(cFlagAlwaysNeutralCapThrow,
                Randomizer::isAlwaysNeutralCapThrowEnabled());
            setFlag(cFlagChaosForceDisabled, Randomizer::isChaosForceDisabled());

            sConfig.upThrowButton = toStoredButton(Randomizer::getUpThrowButton());
            sConfig.downThrowJumpButton = toStoredButton(Randomizer::getDownThrowJumpButton());
            sConfig.spinShakeButton = toStoredButton(Randomizer::getSpinShakeButton());
            sConfig.team = Randomizer::getSelectedTeam();
            sConfig.easyMinigameLow = static_cast<u16>(Randomizer::getEasyMinigameLow());
            sConfig.easyMinigameHigh = static_cast<u16>(Randomizer::getEasyMinigameHigh());
            sConfig.sheepHerdingCount = static_cast<u8>(Randomizer::getSheepHerdingCount());

            const Randomizer::TrackerCaveSkipMode caveSkipMode = Randomizer::getTrackerCaveSkipMode();
            setFlag(cFlagTrackerCaveSkipEasy, caveSkipMode == Randomizer::TrackerCaveSkipMode::Easy);
            setFlag(cFlagTrackerCaveSkipHard, caveSkipMode == Randomizer::TrackerCaveSkipMode::Hard);

            const Randomizer::TrackerMoonLimitMode moonLimitMode = Randomizer::getTrackerMoonLimitMode();
            setFlag(cFlagTrackerMoonLimitUpper, moonLimitMode == Randomizer::TrackerMoonLimitMode::Upper);
            setFlag(cFlagTrackerMoonLimitFull, moonLimitMode == Randomizer::TrackerMoonLimitMode::Full);
        }
    } // namespace

    void applyLoaded() {
        if (!sLoaded)
            return;

        Randomizer::setFastMoonDemosEnabled((sConfig.flags & cFlagFastMoonDemos) != 0);
        Randomizer::setMotionFreeControlsEnabled((sConfig.flags & cFlagMotionFreeControls) != 0);
        Randomizer::setSkipCutsceneCamerasEnabled((sConfig.flags & cFlagSkipCutsceneCameras) != 0);
        Randomizer::setFreeCostumeMoonEnabled((sConfig.flags & cFlagFreeCostumeMoon) != 0);
        Randomizer::setOneSheepHerdingEnabled((sConfig.flags & cFlagOneSheepHerding) != 0);
        Randomizer::TrackerOverlayMode trackerMode = Randomizer::TrackerOverlayMode::None;
        if (sConfig.flags & cFlagTrackerOverlay) {
            if (sConfig.flags & cFlagTrackerOverlayHints)
                trackerMode = (sConfig.flags & cFlagTrackerOverlayGuided)
                    ? Randomizer::TrackerOverlayMode::Guided
                    : Randomizer::TrackerOverlayMode::Hints;
            else if (sConfig.flags & cFlagTrackerOverlayTime)
                trackerMode = Randomizer::TrackerOverlayMode::Extended;
            else
                trackerMode = Randomizer::TrackerOverlayMode::Basic;
        }
        Randomizer::setTrackerOverlayMode(trackerMode);
        Randomizer::BoardScale boardScale = Randomizer::BoardScale::Medium;
        if (sConfig.flags & cFlagBoardScaleSmall)
            boardScale = Randomizer::BoardScale::Small;
        else if (sConfig.flags & cFlagBoardScaleLarge)
            boardScale = Randomizer::BoardScale::Large;
        Randomizer::setBoardScale(boardScale);
        Randomizer::setEasyMinigamesEnabled((sConfig.flags & cFlagEasyMinigames) != 0);
        Randomizer::setFastDogEnabled((sConfig.flags & cFlagFastDog) != 0);
        Randomizer::setUncappedCoinsEnabled((sConfig.flags & cFlagUncappedCoins) != 0);
        if (sConfig.easyMinigameLow != 0 && sConfig.easyMinigameHigh != 0) {
            Randomizer::setEasyMinigameLow(sConfig.easyMinigameLow);
            Randomizer::setEasyMinigameHigh(sConfig.easyMinigameHigh);
        } else {
            Randomizer::setEasyMinigameLow(Randomizer::kEasyMinigameDefaultLow);
            Randomizer::setEasyMinigameHigh(Randomizer::kEasyMinigameDefaultHigh);
        }
        Randomizer::setSheepHerdingCount(sConfig.sheepHerdingCount != 0
                ? sConfig.sheepHerdingCount
                : Randomizer::kSheepHerdingDefaultCount);
        // The legacy enable bit remains authoritative for old configs; loading
        // the count above must not turn the feature on just because the new
        // count slot was populated.
        Randomizer::setOneSheepHerdingEnabled(
            (sConfig.flags & cFlagOneSheepHerding) != 0);
        Randomizer::setUnlockHintArtEnabled((sConfig.flags & cFlagUnlockHintArt) != 0);
        Randomizer::setAreaNameOverlayEnabled((sConfig.flags & cFlagAreaNameOverlay) != 0);
        Randomizer::setNoClipEnabled((sConfig.flags & cFlagNoClip) != 0);
        Randomizer::setTrackerRightAligned((sConfig.flags & cFlagTrackerRightAligned) != 0);
        Randomizer::setCompletedKingdomMoonCollectedEnabled(
            (sConfig.flags & cFlagCompletedKingdomMoonCollected) != 0);
        Randomizer::setRandomizeOutfitsEnabled(
            (sConfig.flags & cFlagRandomizeOutfits) != 0);
        Randomizer::setAlwaysNeutralCapThrowEnabled(
            (sConfig.flags & cFlagAlwaysNeutralCapThrow) != 0);
        Randomizer::setChaosForceDisabled(
            (sConfig.flags & cFlagChaosForceDisabled) != 0);

        const u8 upThrowRaw = clampStoredButton(sConfig.upThrowButton);
        if (upThrowRaw != cButtonUnset)
            Randomizer::setUpThrowButton(static_cast<Randomizer::ControllerButton>(upThrowRaw - 1));
        const u8 downThrowJumpRaw = clampStoredButton(sConfig.downThrowJumpButton);
        if (downThrowJumpRaw != cButtonUnset)
            Randomizer::setDownThrowJumpButton(static_cast<Randomizer::ControllerButton>(downThrowJumpRaw - 1));
        const u8 spinShakeRaw = clampStoredButton(sConfig.spinShakeButton);
        if (spinShakeRaw != cButtonUnset)
            Randomizer::setSpinShakeButton(static_cast<Randomizer::ControllerButton>(spinShakeRaw - 1));

        // setSelectedTeam clamps, so a corrupt byte lands on Team 1 rather
        // than a team index nothing else in the room agrees exists.
        Randomizer::setSelectedTeam(sConfig.team);

        Randomizer::TrackerCaveSkipMode caveSkipMode = Randomizer::TrackerCaveSkipMode::None;
        if (sConfig.flags & cFlagTrackerCaveSkipHard)
            caveSkipMode = Randomizer::TrackerCaveSkipMode::Hard;
        else if (sConfig.flags & cFlagTrackerCaveSkipEasy)
            caveSkipMode = Randomizer::TrackerCaveSkipMode::Easy;
        Randomizer::setTrackerCaveSkipMode(caveSkipMode);

        Randomizer::TrackerMoonLimitMode moonLimitMode = Randomizer::TrackerMoonLimitMode::None;
        if (sConfig.flags & cFlagTrackerMoonLimitFull)
            moonLimitMode = Randomizer::TrackerMoonLimitMode::Full;
        else if (sConfig.flags & cFlagTrackerMoonLimitUpper)
            moonLimitMode = Randomizer::TrackerMoonLimitMode::Upper;
        Randomizer::setTrackerMoonLimitMode(moonLimitMode);
    }

    void load() {
        if (!readConfig(&sConfig)) {
            setDefaults();
            writeConfig(sConfig);
        } else if ((sConfig.flags & cFlagQolInitialized) == 0) {
            // Upgrade an existing v1 multiplayer-only config in place, preserving
            // its server settings while seeding QoL with the established defaults.
            captureQol();
            writeConfig(sConfig);
        }

        sLoaded = true;
        applyLoaded();
    }

    bool save() {
        if (!sLoaded)
            setDefaults();

        captureQol();
        sConfig.magic = cMagic;
        sConfig.version = cVersion;
        sLoaded = true;
        return writeConfig(sConfig);
    }
} // namespace GlobalConfig
