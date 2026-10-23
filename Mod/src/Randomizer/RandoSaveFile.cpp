#include "Randomizer/RandoSaveFile.h"

#include "EntranceDB.h"
#include "Library/Base/StringUtil.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/RequirementRando.h"
#include "Randomizer/SpoilerLog.hpp"
#include "Randomizer/StartPositionRando.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/GameProgressData.h"

#include "Multiplayer/algorithms/crc32.h"

#include "nn/fs.h"

#include "SDRedirect/SDPaths.h"
#include <cstring>

namespace RandoSaveFile {
    namespace {
        namespace LD = Randomizer::LogicData;

        enum SettingsFlag : u32
        {
            SettingsFlag_MoonRando = 1u << 0,
            SettingsFlag_EntranceRando = 1u << 1,
            SettingsFlag_PaintingRando = 1u << 2,
            SettingsFlag_KingdomRando = 1u << 3,
            SettingsFlag_LogicRando = 1u << 4,
            SettingsFlag_SkyboxRando = 1u << 5,
            SettingsFlag_LockedCaptures = 1u << 6,
            SettingsFlag_SphinxRando = 1u << 7,
            // Retired: the spoiler log is always written now. The bit stays
            // reserved so an older build reading a new slot still parses the
            // flags it does understand.
            SettingsFlag_SpoilerLogRetired = 1u << 8,
            SettingsFlag_LockedAbilities = 1u << 9,
            SettingsFlag_MusicRando = 1u << 10,
            SettingsFlag_MusicOverlay = 1u << 11,
            SettingsFlag_MoonKingdomReq = 1u << 12,
            SettingsFlag_WorldPeaceReq = 1u << 13,
            SettingsFlag_CloudPaintings = 1u << 14,
            SettingsFlag_ExtendedOptions = 1u << 15,
            SettingsFlag_SubAreaSkybox = 1u << 16,
            SettingsFlag_FastMoonDemos = 1u << 17,
            SettingsFlag_MotionFreeControls = 1u << 18,
            SettingsFlag_SkipCutsceneCameras = 1u << 19,
            // Bit 20 held the retired Costume Doors Open toggle (costume doors
            // are always open now). Reserved so the bit is never reused.
            SettingsFlag_TrackerOverlay = 1u << 21,
            SettingsFlag_OneSheepHerding = 1u << 22,
            SettingsFlag_TripleMoonsAnywhere = 1u << 23,
            SettingsFlag_EasyMinigames = 1u << 24,
            SettingsFlag_AreaNameOverlay = 1u << 25,
            SettingsFlag_VisitedKingdomsMap = 1u << 26,
            SettingsFlag_MoonRockKey = 1u << 27,
            SettingsFlag_EnemySwap = 1u << 28,
            SettingsFlag_NpcSwap = 1u << 29,
            // Bit 30 held the retired Keep Critical Captures toggle.
            // Reserved so the bit is never reused.
            SettingsFlag_MoonPurchases = 1u << 31,
        };

        constexpr u8 cBowserWinConditionMask = 0x1F;
        constexpr u8 cStreamerModeBit = 0x20;

        // The hint-price byte only ever held a 0-2 tier, so its spare high bits
        // are free for other hint-shop settings. Bits 4-5 store Hint Mode;
        // bit 7 remains a legacy Extended Hints compatibility bit.
        constexpr u8 cHintPriceTierMask = 0x0F;
        constexpr u8 cHintModeShift = 4;
        constexpr u8 cHintModeMask = 0x30;
        constexpr u8 cExtendedHintsBit = 0x80;

        enum HeaderReservedIndex : s32
        {
            // Low three bits are the EntranceRandoMode value; bits 3-4 carry the
            // Shop Locations mode (see cShopLocationShift).
            Reserved_EntranceMode = 0,
            // Low two bits are the StartingEquipment value; bits 2-3 carry the
            // Starting Position mode (see cStartPositionShift); bit 7 carries the
            // Chest Order setting (see cChestOrderBit).
            Reserved_StartingEquipment = 1,
            Reserved_CaptureAbilityDifficulty = 2,
            // Stored as mode+1 so 0 means "not written" (legacy save) and we fall
            // back to the SkyboxRando/SubAreaSkybox bits below.
            Reserved_SkyboxMode = 3,
            // Stored as value+1 so 0 means "not written" (legacy save); since this
            // setting defaults on, a legacy save reads back as on.
            Reserved_StabilizeShaders = 4,
            Reserved_SpecialMoonFormat = 5,
            Reserved_CapMoonUse = 6,
            Reserved_CloudMoonUse = 7,
            Reserved_MoonMoonUse = 8,
            Reserved_MoonTotalLo = 9,
            Reserved_MoonTotalHi = 10,
            // Stored as mode+1 so 0 means "not written" (legacy save) and we fall
            // back to the SettingsFlag_WorldPeaceReq bit below. That only ever
            // holds 1-3, so bits 2-5 carry the two Subarea Unlock settings (see
            // cSubareaUnlock*/cSubareaHost*).
            Reserved_WorldPeaceMode = 11,
            // Stored packed-value+1 across two bytes so 0 means a legacy save.
            Reserved_SeedStyleLo = 12,
            Reserved_SeedStyleHi = 13,
            // Low nibble holds mode+1, so old headers with zeroed reserved
            // bytes use their original settings-flag fallback. Bits 4-5 carry
            // the two music pool filters (see cMusicDisable*Bit).
            Reserved_MusicMode = 14,
            // Four Purple Shop settings: 2 ability bits, 2 capture bits, one
            // moon-toggle bit, and three price-selector bits.
            Reserved_PurpleShopSettings = 15,
        };
        constexpr u8 cSpecialMoonFormat = 0x53;
        // The special-moon-format byte is otherwise a fixed format marker. Its
        // high bit is free and keeps the new seed setting inside the existing
        // 0x40-byte header without invalidating older saves.
        constexpr u8 cStartKingdomStateBit = 0x80;
        // StartingEquipment only ever holds 0-2, so the rest of its byte is free.
        // Bit 7 carries Chest Order; it is only trusted on cChestOrderSettingsFormat
        // headers, and the mask keeps older saves reading their value unchanged.
        constexpr u8 cStartingEquipmentMask = 0x03;
        constexpr u8 cChestOrderBit = 0x80;
        // Bits 2-3 of the same byte hold the 3-valued Starting Position mode. Only
        // trusted on cStartPositionSettingsFormat headers, so older saves keep
        // reading their StartingEquipment value unchanged.
        constexpr u8 cStartPositionShift = 2;
        constexpr u8 cStartPositionMask = 0x0C;
        constexpr u8 cBowserSettingsFormat = 0x42;
        constexpr u8 cMoonPurchaseSettingsFormat = 0x43;
        constexpr u8 cStreamerModeSettingsFormat = 0x44;
        constexpr u8 cPurpleShopSettingsFormat = 0x46;
        constexpr u8 cChaosSettingsFormat = 0x47;
        constexpr u8 cPropChaosSettingsFormat = 0x48;
        constexpr u8 cHintModeSettingsFormat = 0x49;
        constexpr u8 cChestOrderSettingsFormat = 0x4A;
        constexpr u8 cStartPositionSettingsFormat = 0x4B;
        constexpr u8 cMusicFilterSettingsFormat = 0x4C;
        constexpr u8 cSubareaUnlockSettingsFormat = 0x4D;
        constexpr u8 cCoinRandoSettingsFormat = 0x4E;
        constexpr u8 cShopLocationSettingsFormat = 0x4F;
        constexpr u8 cShopPricingSettingsFormat = 0x50;
        // Seed Style became three independent axes (Unlock Pacing / Seed
        // Openness / Seed Difficulty) packed into the SAME two reserved bytes.
        // A header at this format stores the tagged three-axis value; anything
        // older stores a bare SeedStyle index and is migrated on read.
        // This follows the shop-pricing format, which already occupies 0x50.
        constexpr u8 cSeedAxisSettingsFormat = 0x51;
        // The Subarea Unlock mode (0-3) and host (0-2) ride in the free upper
        // bits of the World Peace byte. They are only trusted on
        // cSubareaUnlockSettingsFormat headers; an older save reads back as
        // zero, which is the Unlocked default with sub-areas open as before.
        constexpr u8 cWorldPeaceModeMask = 0x03;
        constexpr u8 cSubareaUnlockShift = 2;
        constexpr u8 cSubareaUnlockMask = 0x0C;
        constexpr u8 cSubareaHostShift = 4;
        constexpr u8 cSubareaHostMask = 0x30;
        constexpr u8 cCoinRandoBit = 0x40;
        // EntranceRandoMode only ever holds 0-5, so bits 3-4 of its byte are
        // free for the Shop Locations mode. Only trusted on
        // cShopLocationSettingsFormat headers; an older save reads back as
        // zero, which is the Randomized default those saves actually meant.
        constexpr u8 cEntranceModeMask = 0x07;
        constexpr u8 cShopLocationShift = 3;
        constexpr u8 cShopLocationMask = 0x18;
        // Bits 5-6 of the same byte carry the two shop-pricing settings, only
        // trusted on cShopPricingSettingsFormat headers. Both default off,
        // which is exactly how an older save behaved.
        constexpr u8 cShopPriceRandoBit = 0x20;
        constexpr u8 cProportionalKingdomPricingBit = 0x40;
        // Reserved_MusicMode only ever held a 0-4 mode+1 value, so the rest of
        // its byte is free. These bits are only trusted on
        // cMusicFilterSettingsFormat headers; older saves read back as unset,
        // which is each filter's default (off).
        constexpr u8 cMusicModeMask = 0x0F;
        constexpr u8 cMusicDisable2DBit = 0x10;
        constexpr u8 cMusicDisableExtraBit = 0x20;
        constexpr u8 cMusicDisableBossBit = 0x40;
        // Bit 7 briefly held a "use custom music" flag before it grew into the
        // four-way music source below. Left reserved rather than reused.
        // Reserved_MusicMode is full, so the music *source* lives in the spare
        // bits of Reserved_SkyboxMode: that byte only ever held a skybox mode+1
        // of 1-5, which fits in the low three bits, and everything above them
        // reads back as zero on every save written so far -- zero meaning "not
        // written", i.e. the Vanilla default.
        constexpr u8 cSkyboxModeMask = 0x07;
        constexpr u8 cMusicSourceShift = 3;
        constexpr u8 cMusicSourceMask = 0x38;
        // CaptureAbilityDifficulty only ever holds 0-2, so the upper six bits
        // of its byte are free. The two chaos swap modes live there, stored
        // directly: they are a new setting, so a header that predates them
        // carries zero, which is Off -- exactly what those saves meant.
        constexpr u8 cCaptureAbilityDifficultyMask = 0x03;
        constexpr u8 cNpcSwapModeShift = 2;
        constexpr u8 cNpcSwapModeMask = 0x0C;
        constexpr u8 cPropSwapModeShift = 4;
        constexpr u8 cPropSwapModeMask = 0x30;
        // Bit 6 held the retired Log Planned Swaps diagnostic, whose output
        // now lives in the spoiler log. Reserved rather than reused.
        // Bit 7, the last free bit of this byte. If another chaos setting is
        // needed after this one it will have to find a different home.
        constexpr u8 cTwoDSwapBit = 0x80;

        constexpr u8 cStabilizeShadersMask = 0x03;
        constexpr u8 cPropSwapCodeShift = 2;
        constexpr u8 cPropSwapCodeMask = 0x1C;
        // Bit 5 of the Reserved_StabilizeShaders byte. RETIRED: the setting it
        // carried folded into Seed Difficulty (a set bit means Hard). Still
        // READ on pre-cSeedAxisSettingsFormat headers so old slots migrate;
        // never written again, and permanently reserved so nothing reuses it.
        constexpr u8 cPrioritizeDifficultMovementBit = 0x20;
        // Bit 6 of the Reserved_StabilizeShaders byte. Legacy saves carry 0
        // there, which reads back as the setting's default (off).
        constexpr u8 cRandomizeTripleMoonsBit = 0x40;
        // Bit 7 of the Reserved_StabilizeShaders byte: promotes the
        // SettingsFlag_VisitedKingdomsMap bit from the Visited mode to the
        // Unlocked one. Legacy saves carry 0 there, so a save written before
        // the mode existed still reads back as plain Visited.
        constexpr u8 cVisitedKingdomsUnlockedBit = 0x80;

        bool hasBowserSettings(const Header& header) {
            return header.settingsFormat == cBowserSettingsFormat
                || header.settingsFormat == cMoonPurchaseSettingsFormat
                || header.settingsFormat == cStreamerModeSettingsFormat
                || header.settingsFormat == cPurpleShopSettingsFormat
                || header.settingsFormat == cChaosSettingsFormat
                || header.settingsFormat == cPropChaosSettingsFormat
                || header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasMoonPurchaseSetting(const Header& header) {
            return header.settingsFormat == cMoonPurchaseSettingsFormat
                || header.settingsFormat == cStreamerModeSettingsFormat
                || header.settingsFormat == cPurpleShopSettingsFormat
                || header.settingsFormat == cChaosSettingsFormat
                || header.settingsFormat == cPropChaosSettingsFormat
                || header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasStreamerModeSetting(const Header& header) {
            return header.settingsFormat == cStreamerModeSettingsFormat
                || header.settingsFormat == cPurpleShopSettingsFormat
                || header.settingsFormat == cChaosSettingsFormat
                || header.settingsFormat == cPropChaosSettingsFormat
                || header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasPurpleShopSettings(const Header& header) {
            return header.settingsFormat == cPurpleShopSettingsFormat
                || header.settingsFormat == cChaosSettingsFormat
                || header.settingsFormat == cPropChaosSettingsFormat
                || header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasPropChaosSettings(const Header& header) {
            return header.settingsFormat == cPropChaosSettingsFormat
                || header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasChestOrderSetting(const Header& header) {
            return header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasHintModeSetting(const Header& header) {
            return header.settingsFormat == cHintModeSettingsFormat
                || header.settingsFormat == cChestOrderSettingsFormat
                || header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasStartPositionSetting(const Header& header) {
            return header.settingsFormat == cStartPositionSettingsFormat
                || header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasMusicFilterSettings(const Header& header) {
            return header.settingsFormat == cMusicFilterSettingsFormat
                || header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasSubareaUnlockSettings(const Header& header) {
            return header.settingsFormat == cSubareaUnlockSettingsFormat
                || header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasCoinRandoSetting(const Header& header) {
            return header.settingsFormat == cCoinRandoSettingsFormat
                || header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasShopLocationSetting(const Header& header) {
            return header.settingsFormat == cShopLocationSettingsFormat
                || header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        bool hasShopPricingSettings(const Header& header) {
            return header.settingsFormat == cShopPricingSettingsFormat
                || header.settingsFormat == cSeedAxisSettingsFormat;
        }

        // Only a header at this format stores the tagged three-axis value in
        // Reserved_SeedStyleLo/Hi. Everything older stores a bare SeedStyle.
        bool hasSeedAxisSettings(const Header& header) {
            return header.settingsFormat == cSeedAxisSettingsFormat;
        }

        Randomizer::ShopLocationMode readShopLocationMode(const Header& header) {
            if (!hasShopLocationSetting(header))
                return Randomizer::ShopLocationMode_Randomized;
            const u8 enc = static_cast<u8>(
                (header.reserved[Reserved_EntranceMode] & cShopLocationMask) >> cShopLocationShift);
            return enc <= static_cast<u8>(Randomizer::ShopLocationMode_Vanilla)
                ? static_cast<Randomizer::ShopLocationMode>(enc)
                : Randomizer::ShopLocationMode_Randomized;
        }

        Randomizer::HintMode readHintMode(const Header& header) {
            if (hasHintModeSetting(header)) {
                const u8 encoded = static_cast<u8>(
                    (header.hintPriceTier & cHintModeMask) >> cHintModeShift);
                if (encoded <= static_cast<u8>(Randomizer::HintMode::Purchases))
                    return static_cast<Randomizer::HintMode>(encoded);
                return Randomizer::HintMode::Basic;
            }

            // Before the merged selector existed, saves had independent
            // purchase and Extended Hints bits. Older formats without the
            // purchase setting used the established direct-purchase default.
            if (!hasMoonPurchaseSetting(header))
                return Randomizer::HintMode::Purchases;
            if ((header.settingsFlags & SettingsFlag_MoonPurchases) != 0)
                return Randomizer::HintMode::Purchases;
            if ((header.hintPriceTier & cExtendedHintsBit) != 0)
                return Randomizer::HintMode::Extended;
            return Randomizer::HintMode::Basic;
        }

        struct ConnRecord {
            s16 link;
            u8 kind;
            u8 pad;
        };

        static_assert(sizeof(ConnRecord) == 4, "Serialized entrance connection must be 4 bytes");
        constexpr u32 cConnectionFormatVersion = 2;
        constexpr u32 cLogicDirectedStartFormatVersion = 5;

        struct LogicDirectedStartRecord {
            s16 abilities[2];
            s16 captures[4];
            u8 abilityCount;
            u8 captureCount;
        };
        static_assert(sizeof(LogicDirectedStartRecord) == 14,
            "Serialized logic-directed starts must stay compact.");

        // Version 6 carries the cap-throw half of a Soft Nuzlocke starting
        // inventory.  Earlier records only represented optional extra
        // abilities, so they cannot preserve a non-default cap throw.
        struct NuzlockeStartRecord {
            s16 capThrow;
        };
        static_assert(sizeof(NuzlockeStartRecord) == 2,
            "Serialized Nuzlocke cap throw must stay compact.");
        constexpr u32 cNuzlockeStartFormatVersion = 6;

        // Version 7 carries the run-mode-specific settings that the "Game Mode
        // Settings" page owns.  Older saves simply keep whatever the defaults
        // resolve to (half-loss Soft Nuzlocke, Extreme off, and the fixed medal
        // ratio table), which is exactly the behavior they were generated with.
        struct GameModeSettingsRecord {
            u8 timerStartIdx;
            u8 timerMoonBonusIdx;
            u8 timerTripleBonusIdx;
            u8 softNuzlockeLoss;
            u8 customMedalCounts;
            u8 customMedalAvailable;
            u8 customMedalRequired;
            // Both Soft Nuzlocke equipment dials, packed into the byte that used
            // to be padding so the record keeps its size and its version.  Each
            // nibble stores value+1, which leaves 0 meaning "a save written
            // before these existed" - those keep the defaults, which is the
            // half-loss the moon dial used to impose on them anyway.
            u8 softNuzlockeEquipLoss;
        };
        static_assert(sizeof(GameModeSettingsRecord) == 8,
            "Serialized game mode settings must stay compact.");
        constexpr u32 cGameModeSettingsFormatVersion = 7;
        // SoftNuzlockeLoss currently uses values 0..2. Keep Extreme in the
        // unused high bit so version 7 records remain the same size. Strip
        // only that bit on load; the existing setter remains responsible for
        // validating the loss enum exactly as it did before Extreme existed.
        constexpr u8 cExtremeNuzlockeBit = 0x80;
        constexpr u8 cSoftNuzlockeLossMask = 0x7f;

        struct MoonAllocationSettingsRecord {
            u8 flags; // bit 0 master, bit 1 randomize child
            u8 difficulty; // MoonAllocationDifficulty
            u8 coinPolicy; // MoonCoinPolicy
            u8 reserved; // always zero
        };
        static_assert(sizeof(MoonAllocationSettingsRecord) == 4,
            "Serialized moon allocation settings must stay compact.");
        constexpr u32 cMoonAllocationSettingsFormatVersion = 8;

        // Version 9 carries Timer Mode's two penalty dials.  All three fields are
        // stored as plain roll indices whose zero entry is "Off"/"Disabled", so
        // an older save that stops before this record - and therefore leaves the
        // loader's defaults standing - plays exactly as it was generated to.
        //
        // halfLifeIdx (the award decay) landed after version 9 shipped and took
        // one of this record's reserved bytes rather than appending a new record:
        // every save that carries the record wrote those bytes as zero, which is
        // exactly the "Disabled" the dial has to decode to, so the record's size
        // - and with it the payload layout and the format version - is unchanged.
        struct TimerPenaltySettingsRecord {
            u8 damagePenaltyIdx;
            u8 deathPenaltyIdx;
            u8 halfLifeIdx;
            u8 reserved; // always zero
        };
        static_assert(sizeof(TimerPenaltySettingsRecord) == 4,
            "Serialized timer penalty settings must stay compact.");
        constexpr u32 cTimerPenaltySettingsFormatVersion = 9;

        // Version 10 stores both profile banks.  Currency is deliberately not
        // serialized: it is derived from the three special moon selections in
        // the existing fixed header.
        struct HintConfigurationSettingsRecord {
            u32 coinProfile;
            u32 tokenProfile;
        };
        static_assert(sizeof(HintConfigurationSettingsRecord) == 8,
            "Serialized hint configuration must stay compact.");
        constexpr u32 cHintConfigurationSettingsFormatVersion = 10;

        struct MultiplayerSettingsRecord {
            u8 version;
            u8 flags;
            u8 reserved[6];
        };
        static_assert(sizeof(MultiplayerSettingsRecord) == 8,
            "Serialized multiplayer settings must stay extensible and compact.");
        constexpr u32 cMultiplayerSettingsFormatVersion = 11;
        // 2 adds the cross-team sync bit. A record still written at version 1
        // has that bit clear anyway, so the version gate below is about intent
        // rather than safety: a v1 record genuinely did not have an opinion.
        constexpr u8 cMultiplayerSettingsRecordVersion = 2;
        constexpr u8 cDeathLinkBit = 1u << 0;
        // Room-owned like death link: the host sets it, the server mirrors it
        // to everyone. The player's own TEAM is deliberately not stored here -
        // see Randomizer::getSelectedTeam for why putting it in the seed would
        // put every guest on the host's team.
        constexpr u8 cSyncOtherTeamsBit = 1u << 1;

        // Version 12: the Ability Lock page's movement-ability dials, in the same
        // packed layout Randomizer::packMovementAbilitySettings produces (bits 0-1
        // Moon gravity mode, then one bit per dial from bit 2 up - see the comment
        // on that function for the current order). They select which abilities enter
        // the reward pool, so they have to travel with the seed rather than live in
        // the global config. A pre-12 slot has no record and decodes to 0 - every
        // dial granted, exactly how it was generated; so does a slot written before
        // a later dial was appended, since its bit reads back as 0.
        struct MovementAbilitySettingsRecord {
            u32 packed;
            u32 reserved; // always zero
        };
        static_assert(sizeof(MovementAbilitySettingsRecord) == 8,
            "Serialized movement ability settings must stay compact.");
        constexpr u32 cMovementAbilitySettingsFormatVersion = 12;
        // Version 13 appends the optional per-kingdom moon and triple-moon
        // targets. -1 is the auto sentinel for both arrays, so a pre-13 save
        // naturally decodes to the same automatic distribution as before.
        struct KingdomTargetSettingsRecord {
            s16 moonTargets[WorldMeta::sWorldCount];
            s8 tripleMoonTargets[WorldMeta::sWorldCount];
            u8 reserved; // always zero; makes the packed array size explicit
        };
        static_assert(sizeof(KingdomTargetSettingsRecord)
                == sizeof(s16) * WorldMeta::sWorldCount
                    + sizeof(s8) * WorldMeta::sWorldCount + sizeof(u8),
            "Serialized kingdom target settings must stay append-only.");
        constexpr u32 cKingdomTargetSettingsFormatVersion = 13;

        // Version 14 carries the Wave C settings that cannot fit in the legacy
        // scalar/header projections: special-moon masks, key scopes, the exact
        // Custom map mode and its kingdom mask, plus signed requirement offsets.
        // Keep this record append-only and fixed-size so v13 readers continue to
        // parse their payload unchanged.
        struct WaveCSettingsRecord {
            u32 startKingdomsMask;
            u8 capMoonUseMask;
            u8 cloudMoonUseMask;
            u8 moonMoonUseMask;
            u8 moonRockKeyScope;
            u8 subareaKeyScope;
            u8 visitedKingdomsMapMode;
            u8 reserved;
            s8 moonRequirementDeviations[WorldMeta::sWorldCount];
        };
        static_assert(sizeof(WaveCSettingsRecord) == 28,
            "Wave C settings must stay compact and append-only.");
        constexpr u32 cWaveCSettingsFormatVersion = 14;

        // Version 15 carries Wave B's pool masks and starting-equipment
        // overrides. It is appended after the current Wave C record so the
        // already-shipped v14 sidecar remains byte-for-byte readable.
        struct AdvancedWaveBSettingsRecord {
            u64 capturePoolLo;
            u64 capturePoolHi;
            u64 abilityPool;
            s16 startingCaptures; // -1 means the selected preset value
            s16 startingThrows;
            s16 startingAbilities;
            u16 purpleShopCustomPrice; // 0 means the price is not Custom
            // reserved[0] carries the ability-progression flag, stored as
            // value+1 so 0 still means "not written" on an older slot and
            // reads back as the Progressive default. The struct keeps its
            // size; the remaining bytes stay free.
            u8 reserved[8];
        };
        static_assert(sizeof(AdvancedWaveBSettingsRecord) == 40,
            "Wave B settings must stay compact and append-only.");
        constexpr u32 cAdvancedWaveBSettingsFormatVersion = 15;

// Version 16 carries Wave D's values that are not represented by the
// legacy header or the fixed roll indices: custom Timer/Nuzlocke
// values, per-entry hint overrides, the music theme mask, and the
// Easy Minigames/Sheep Herding companions. The masks make a zero value
// distinguish "not custom" from a legitimate custom number.
#pragma pack(push, 1)
        struct AdvancedWaveDSettingsRecord {
            u8 timerCustomMask; // bits 0..5, one for each Timer Values row
            u8 nuzlockeCustomMask; // bits 0..2, moons/abilities/captures
            u8 hintCustomMask; // one bit per HintPriceCategory
            u8 hintCoinOverrideMask; // one bit per HintPriceCategory
            s32 timerCustomSeconds[6];
            s16 nuzlockeCustomPercent[3];
            u16 hintCustomPrice[Randomizer::kHintPriceCategoryCount];
            u64 musicThemeMask;
            u16 easyMinigameLow;
            u16 easyMinigameHigh;
            u8 sheepHerdingCount;
            u8 reserved[7];
        };
        static_assert(sizeof(AdvancedWaveDSettingsRecord) == 64,
            "Wave D settings must stay compact and append-only.");
#pragma pack(pop)
        constexpr u32 cAdvancedWaveDSettingsFormatVersion = 16;

// Version 17 carries the generation safety net. itemId is a LogicData
// moon-table index (the same stable index used by itemForLocation), so
// loading never depends on a pointer or a reordered ShineDB scan.
#pragma pack(push, 1)
        struct AutoGrant {
            u16 itemKind;
            u16 itemId;
        };

        struct AutoGrantBlock {
            u16 count;
            u8 notified;
            u8 reserved;
            AutoGrant grants[MoonRando::kMaxAutoGrants];
        };
#pragma pack(pop)
        static_assert(sizeof(AutoGrant) == 4,
            "Auto-grant records must stay compact.");
        static_assert(sizeof(AutoGrantBlock)
                == 4 + sizeof(AutoGrant) * MoonRando::kMaxAutoGrants,
            "Auto-grant block must remain fixed-size.");
        constexpr u16 cAutoGrantMoonItemKind = 0;
        constexpr u32 cAutoGrantFormatVersion = 17;

        // Version 18 carries what every kingdom moons hand out. Before this,
        // six kingdoms projected onto the Wave C record and the other eleven
        // had no setting at all - they always gated progress. A pre-18 slot
        // therefore decodes to its Wave C values for Cap/Cloud/Moon and to the
        // compiled defaults (Progress, plus Captures/Abilities on the story
        // kingdoms) everywhere else, which is what it was generated with.
        //
        // Written BEFORE the auto-grant block, not after it: that block has to
        // stay last, because commitAutoGrantsNotified() rewrites it in place
        // at payloadSize - sizeof(AutoGrantBlock).
        struct KingdomMoonUseRecord {
            u8 masks[WorldMeta::sWorldCount];
            u8 reserved[3]; // always zero
        };
        static_assert(sizeof(KingdomMoonUseRecord) == WorldMeta::sWorldCount + 3,
            "Kingdom moon-use masks must stay compact and append-only.");
        constexpr u32 cKingdomMoonUseFormatVersion = 18;

        // Per-kingdom moon requirement bases typed on the Moon Requirement
        // advanced page. -1 means the player never touched that kingdom and the
        // distribution decides it, which is why this is signed and cannot be
        // folded into a zero-default array.
        //
        // Same placement rule as the record above: before the auto-grant block,
        // which has to stay last.
        struct RequirementBaseRecord {
            s16 bases[WorldMeta::sWorldCount];
            u8 reserved[2]; // always zero
        };
        static_assert(
            sizeof(RequirementBaseRecord) == WorldMeta::sWorldCount * 2 + 2,
            "Requirement bases must stay compact and append-only.");
        constexpr u32 cRequirementBaseFormatVersion = 19;

        // Per-kingdom Req Deviation. Its own record rather than a wider version
        // of the one above, because that one has already shipped in a v19 slot
        // and the format is append-only.
        struct RequirementDeviationRecord {
            s16 deviations[WorldMeta::sWorldCount];
            u8 reserved[2]; // always zero
        };
        static_assert(
            sizeof(RequirementDeviationRecord) == WorldMeta::sWorldCount * 2 + 2,
            "Requirement deviations must stay compact and append-only.");
        constexpr u32 cRequirementDeviationFormatVersion = 20;

        constexpr u32 cLegacyVersion19 = 19;
        constexpr u32 cLegacyVersion18 = 18;
        constexpr u32 cLegacyVersion17 = 17;

        constexpr u32 cLegacyVersion16 = 16;
        constexpr u32 cLegacyVersion15 = 15;
        constexpr u32 cLegacyVersion14 = 14;
        constexpr u32 cLegacyVersion13 = 13;
        constexpr u32 cLegacyVersion12 = 12;
        constexpr u32 cLegacyVersion11 = 11;
        constexpr u32 cLegacyVersion10 = 10;
        constexpr u32 cLegacyVersion9 = 9;
        constexpr u32 cLegacyVersion7 = 7;
        constexpr u32 cLegacyVersion8 = 8;
        constexpr u8 cMoonAllocationMasterBit = 1u << 0;
        constexpr u8 cMoonAllocationRandomizeBit = 1u << 1;

        constexpr size_t cMaxPayloadSize = sizeof(s32)
            + sizeof(s32) * LD::sMoonCount
            + sizeof(MoonRando::SerializedPlacement) * ShineDB::sShineTableSize
            + sizeof(s16) * LD::sPaintingLinkCount
            + sizeof(LogicDirectedStartRecord)
            + sizeof(NuzlockeStartRecord)
            + sizeof(GameModeSettingsRecord)
            + sizeof(MoonAllocationSettingsRecord)
            + sizeof(TimerPenaltySettingsRecord)
            + sizeof(HintConfigurationSettingsRecord)
            + sizeof(MultiplayerSettingsRecord)
            // Every appended record has to be counted here as well as in
            // expectedPayloadSize: this is what sizes the write buffer, and a
            // record that does not fit makes appendBytes refuse, which fails the
            // whole write rather than truncating it.
            + sizeof(MovementAbilitySettingsRecord)
            + sizeof(KingdomTargetSettingsRecord)
            + sizeof(WaveCSettingsRecord)
            + sizeof(AdvancedWaveBSettingsRecord)
            + sizeof(AdvancedWaveDSettingsRecord)
            + sizeof(ConnRecord) * EntranceDB::sLinkCount * 4
            + sizeof(KingdomMoonUseRecord)
            + sizeof(RequirementBaseRecord)
            + sizeof(RequirementDeviationRecord)
            + sizeof(AutoGrantBlock);

        static u8 sPayload[cMaxPayloadSize];
        static u8 sValidationPayload[cMaxPayloadSize];
        static EntranceRando::Conn sPipeConn[EntranceDB::sLinkCount];
        static EntranceRando::Conn sHomeOutConn[EntranceDB::sLinkCount];
        static EntranceRando::Conn sSubNearConn[EntranceDB::sLinkCount];
        static EntranceRando::Conn sSubFarConn[EntranceDB::sLinkCount];
        static s32 sAutoGrantItems[MoonRando::kMaxAutoGrants] = {};
        static s32 sAutoGrantCount = 0;
        static bool sAutoGrantNotified = false;
        static bool sAutoGrantNotificationDirty = false;
        static u32 sAutoGrantSeed = 0;
        static bool sAutoGrantSeedValid = false;

        void clearAutoGrantState() {
            std::memset(sAutoGrantItems, 0, sizeof(sAutoGrantItems));
            sAutoGrantCount = 0;
            sAutoGrantNotified = false;
            sAutoGrantNotificationDirty = false;
            sAutoGrantSeed = 0;
            sAutoGrantSeedValid = false;
        }

        bool sameAutoGrantList(s32 count) {
            if (count != sAutoGrantCount)
                return false;
            for (s32 i = 0; i < count; i++) {
                if (sAutoGrantItems[i] != MoonRando::getAutoGrantItemIndex(i))
                    return false;
            }
            return true;
        }

        bool isValidSlot(s32 slot) {
            return slot >= 1 && slot <= 10;
        }

        const char* categoryRoot(SaveRedirect::Category category) {
            switch (category) {
            case SaveRedirect::Category::Multiplayer:
                return "MPSaves";
            case SaveRedirect::Category::MultiplayerGuest:
                return "MPSaves/_guest";
            default:
                return "Saves";
            }
        }

        al::StringTmp<128> makeSlotDir(s32 slot, SaveRedirect::Category category) {
            return al::StringTmp<128>("sd:/SMO-Randomizer/%s/Slot%d", categoryRoot(category), slot);
        }

        al::StringTmp<128> makePath(s32 slot, SaveRedirect::Category category) {
            return al::StringTmp<128>(
                "sd:/SMO-Randomizer/%s/Slot%d/rando.bin", categoryRoot(category), slot);
        }

        al::StringTmp<128> makeTempPath(s32 slot, SaveRedirect::Category category) {
            return al::StringTmp<128>(
                "sd:/SMO-Randomizer/%s/Slot%d/rando.bin.pending", categoryRoot(category), slot);
        }

        al::StringTmp<128> makeBackupPath(s32 slot, SaveRedirect::Category category) {
            return al::StringTmp<128>(
                "sd:/SMO-Randomizer/%s/Slot%d/rando.bin.backup", categoryRoot(category), slot);
        }

        bool ensureSlotDir(s32 slot, SaveRedirect::Category category) {
            if (!isValidSlot(slot))
                return false;

            SDPaths::ensureRandomizerRoot();
            al::StringTmp<64> root = { "sd:/SMO-Randomizer/%s", categoryRoot(category) };
            nn::fs::CreateDirectory(root.cstr());
            nn::fs::CreateDirectory(makeSlotDir(slot, category).cstr());
            return true;
        }

        u32 calcCrc32(const void* data, size_t size) {
            const u8* bytes = static_cast<const u8*>(data);
            u32 crc = 0xFFFFFFFF;
            for (size_t i = 0; i < size; i++)
                crc = (crc >> 8) ^ crc32::s_Table[(crc ^ bytes[i]) & 0xFF];
            return crc ^ 0xFFFFFFFF;
        }

        bool appendBytes(u8*& cursor, const u8* end, const void* data, size_t size) {
            if (!data || cursor + size > end)
                return false;
            std::memcpy(cursor, data, size);
            cursor += size;
            return true;
        }

        bool takeBytes(const u8*& cursor, const u8* end, const void** out, size_t size) {
            if (!out || cursor + size > end)
                return false;
            *out = cursor;
            cursor += size;
            return true;
        }

        size_t expectedPayloadSize(s32 placementCount, s32 entranceLinkCount, u32 version) {
            if (placementCount < 0 || placementCount > ShineDB::sShineTableSize)
                return 0;
            if (entranceLinkCount <= 0 || entranceLinkCount > EntranceDB::sLinkCount)
                return 0;
            return sizeof(s32)
                + sizeof(s32) * LD::sMoonCount
                + sizeof(MoonRando::SerializedPlacement) * placementCount
                + (version >= cLogicDirectedStartFormatVersion
                        ? sizeof(s16) * LD::sPaintingLinkCount
                            + sizeof(LogicDirectedStartRecord)
                        : 0)
                + (version >= cNuzlockeStartFormatVersion ? sizeof(NuzlockeStartRecord) : 0)
                + (version >= cGameModeSettingsFormatVersion ? sizeof(GameModeSettingsRecord) : 0)
                + (version >= cMoonAllocationSettingsFormatVersion
                        ? sizeof(MoonAllocationSettingsRecord)
                        : 0)
                + (version >= cTimerPenaltySettingsFormatVersion
                        ? sizeof(TimerPenaltySettingsRecord)
                        : 0)
                + (version >= cHintConfigurationSettingsFormatVersion
                        ? sizeof(HintConfigurationSettingsRecord)
                        : 0)
                + (version >= cMultiplayerSettingsFormatVersion
                        ? sizeof(MultiplayerSettingsRecord)
                        : 0)
                + (version >= cMovementAbilitySettingsFormatVersion
                        ? sizeof(MovementAbilitySettingsRecord)
                        : 0)
                + (version >= cKingdomTargetSettingsFormatVersion
                        ? sizeof(KingdomTargetSettingsRecord)
                        : 0)
                + (version >= cWaveCSettingsFormatVersion
                        ? sizeof(WaveCSettingsRecord)
                        : 0)
                + (version >= cAdvancedWaveBSettingsFormatVersion
                        ? sizeof(AdvancedWaveBSettingsRecord)
                        : 0)
                + (version >= cAdvancedWaveDSettingsFormatVersion
                        ? sizeof(AdvancedWaveDSettingsRecord)
                        : 0)
                + sizeof(ConnRecord) * entranceLinkCount * (version >= cConnectionFormatVersion ? 4 : 3)
                + (version >= cKingdomMoonUseFormatVersion
                        ? sizeof(KingdomMoonUseRecord)
                        : 0)
                + (version >= cRequirementBaseFormatVersion
                        ? sizeof(RequirementBaseRecord)
                        : 0)
                + (version >= cRequirementDeviationFormatVersion
                        ? sizeof(RequirementDeviationRecord)
                        : 0)
                + (version >= cAutoGrantFormatVersion ? sizeof(AutoGrantBlock) : 0);
        }

        u32 makeSettingsFlags() {
            u32 flags = 0;
            if (Randomizer::isMoonRandoEnabled())
                flags |= SettingsFlag_MoonRando;
            if (Randomizer::isEntranceRandoEnabled())
                flags |= SettingsFlag_EntranceRando;
            if (Randomizer::isPaintingRandoEnabled())
                flags |= SettingsFlag_PaintingRando;
            if (Randomizer::isKingdomRandoEnabled())
                flags |= SettingsFlag_KingdomRando;
            if (Randomizer::isLogicRandoEnabled())
                flags |= SettingsFlag_LogicRando;
            if (Randomizer::isKingdomSkyboxRandoEnabled())
                flags |= SettingsFlag_SkyboxRando;
            if (Randomizer::isSubAreaSkyboxRandoEnabled())
                flags |= SettingsFlag_SubAreaSkybox;
            if (Randomizer::isCaptureRandoEnabled())
                flags |= SettingsFlag_LockedCaptures;
            if (Randomizer::isSphinxRandoEnabled())
                flags |= SettingsFlag_SphinxRando;
            if (Randomizer::isLockedAbilitiesEnabled())
                flags |= SettingsFlag_LockedAbilities;
            if (Randomizer::isMusicRandoEnabled())
                flags |= SettingsFlag_MusicRando;
            if (Randomizer::isMusicOverlayEnabled())
                flags |= SettingsFlag_MusicOverlay;
            if (Randomizer::isMoonKingdomRequirementsEnabled())
                flags |= SettingsFlag_MoonKingdomReq;
            if (Randomizer::isWorldPeaceRequirementEnabled())
                flags |= SettingsFlag_WorldPeaceReq;
            if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
                flags |= SettingsFlag_CloudPaintings;
            if (Randomizer::isFastMoonDemosEnabled())
                flags |= SettingsFlag_FastMoonDemos;
            if (Randomizer::isMotionFreeControlsEnabled())
                flags |= SettingsFlag_MotionFreeControls;
            if (Randomizer::isSkipCutsceneCamerasEnabled())
                flags |= SettingsFlag_SkipCutsceneCameras;
            if (Randomizer::isTrackerOverlayEnabled())
                flags |= SettingsFlag_TrackerOverlay;
            if (Randomizer::isOneSheepHerdingEnabled())
                flags |= SettingsFlag_OneSheepHerding;
            if (Randomizer::isTripleMoonsAnywhereEnabled())
                flags |= SettingsFlag_TripleMoonsAnywhere;
            if (Randomizer::isEasyMinigamesEnabled())
                flags |= SettingsFlag_EasyMinigames;
            if (Randomizer::isAreaNameOverlayEnabled())
                flags |= SettingsFlag_AreaNameOverlay;
            if (Randomizer::isVisitedKingdomsMapEnabled())
                flags |= SettingsFlag_VisitedKingdomsMap;
            if (Randomizer::isMoonRockKeyEnabled())
                flags |= SettingsFlag_MoonRockKey;
            if (Randomizer::getHintMode() == Randomizer::HintMode::Purchases)
                flags |= SettingsFlag_MoonPurchases;
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
            if (Randomizer::isEnemyNpcSwapRandoEnabled())
                flags |= SettingsFlag_EnemySwap | SettingsFlag_NpcSwap;
#endif
            flags |= SettingsFlag_ExtendedOptions;
            return flags;
        }

        void applySettingsFlags(u32 flags) {
            Randomizer::setMoonRandoEnabled((flags & SettingsFlag_MoonRando) != 0);
            Randomizer::setPaintingRandoEnabled((flags & SettingsFlag_PaintingRando) != 0);
            Randomizer::setKingdomRandoEnabled((flags & SettingsFlag_KingdomRando) != 0);
            Randomizer::setLogicRandoEnabled((flags & SettingsFlag_LogicRando) != 0);
            // Skybox mode is applied separately (reserved byte, with bit fallback).
            Randomizer::setCaptureRandoEnabled((flags & SettingsFlag_LockedCaptures) != 0);
            Randomizer::setSphinxRandoEnabled((flags & SettingsFlag_SphinxRando) != 0);
            Randomizer::setLockedAbilitiesEnabled((flags & SettingsFlag_LockedAbilities) != 0);
            Randomizer::setMusicRandoEnabled((flags & SettingsFlag_MusicRando) != 0);
            Randomizer::setMusicOverlayEnabled((flags & SettingsFlag_MusicOverlay) != 0);
            Randomizer::setStreamerModeEnabled(false);
            Randomizer::setMoonKingdomRequirementsEnabled((flags & SettingsFlag_MoonKingdomReq) != 0);
            // World Peace Requirement mode is applied separately (reserved byte,
            // with bit fallback) via readWorldPeaceMode().
            Randomizer::setCloudKingdomMoonsUnlockPaintingsEnabled((flags & SettingsFlag_CloudPaintings) != 0);
            Randomizer::setVisitedKingdomsMapEnabled((flags & SettingsFlag_VisitedKingdomsMap) != 0);
            Randomizer::setMoonRockKeyEnabled((flags & SettingsFlag_MoonRockKey) != 0);
            Randomizer::setTripleMoonsAnywhereEnabled(
                (flags & SettingsFlag_TripleMoonsAnywhere) != 0);
            // Seed-affecting: the slot's own Chaos setting must win over
            // whatever is currently toggled. Old saves carry 0 (off).
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
            // The current UI exposes one combined Enemy/NPC switch. Treat
            // either legacy bit as enabling the combined behavior when loading
            // an older slot, then write both bits on the next save.
            Randomizer::setEnemyNpcSwapRandoEnabled(
                (flags & (SettingsFlag_EnemySwap | SettingsFlag_NpcSwap)) != 0);
#endif
            // QoL is global rather than seed-specific. Loading another slot must not
            // replace the user's current QoL preferences with that slot's old flags.
        }

        u8 makePropSwapCode() {
#if SMOR_FEATURE_PROP_RANDOMIZATION
            if (!Randomizer::isPropSwapRandoEnabled())
                return 0;
            switch (Randomizer::getPropSwapChancePercent()) {
            case 50:
                return 2;
            case 75:
                return 3;
            case 100:
                return 4;
            case 25:
            default:
                return 1;
            }
#else
            return 0;
#endif
        }

        // Reads the two chaos swap modes from the spare bits of the
        // CaptureAbilityDifficulty byte. Runs after the settings-flag pass and
        // applyPropChaosSettings so that it is the authority on these two
        // categories: those passes still set them from the older single-bit
        // encoding, which cannot express the narrow modes.
        void applyChaosSwapModes(const Header& header) {
#if SMOR_FEATURE_CHAOS_ANY
            const u8 raw = header.reserved[Reserved_CaptureAbilityDifficulty];
#if SMOR_FEATURE_ENEMY_RANDOMIZATION
            Randomizer::setNpcSwapMode(static_cast<Randomizer::ChaosSwapMode>(
                (raw & cNpcSwapModeMask) >> cNpcSwapModeShift));
            Randomizer::setTwoDSwapRandoEnabled((raw & cTwoDSwapBit) != 0);
#endif
#if SMOR_FEATURE_PROP_RANDOMIZATION
            Randomizer::setPropSwapMode(static_cast<Randomizer::ChaosSwapMode>(
                (raw & cPropSwapModeMask) >> cPropSwapModeShift));
#endif
#else
            (void)header;
#endif
        }

        void applyPropChaosSettings(const Header& header) {
#if SMOR_FEATURE_PROP_RANDOMIZATION
            const u8 code = hasPropChaosSettings(header)
                ? static_cast<u8>((header.reserved[Reserved_StabilizeShaders] & cPropSwapCodeMask)
                      >> cPropSwapCodeShift)
                : 0;
            Randomizer::setPropSwapRandoEnabled(code != 0);
            switch (code) {
            case 2:
                Randomizer::setPropSwapChancePercent(50);
                break;
            case 3:
                Randomizer::setPropSwapChancePercent(75);
                break;
            case 4:
                Randomizer::setPropSwapChancePercent(100);
                break;
            case 1:
            default:
                Randomizer::setPropSwapChancePercent(25);
                break;
            }
#else
            (void)header;
#endif
        }

        void fillHintConfigurationHeader(Header* header) {
            if (!header)
                return;
            const Randomizer::HintMode hintMode = Randomizer::getHintMode();
            // Keep the legacy tier projection populated for older readers. The
            // authoritative per-category profiles live in the version-10
            // payload record.
            header->hintPriceTier = (static_cast<u8>(Randomizer::getHintPriceTier()) & cHintPriceTierMask)
                | static_cast<u8>((static_cast<u8>(hintMode) << cHintModeShift) & cHintModeMask)
                | (hintMode != Randomizer::HintMode::Basic ? cExtendedHintsBit : 0);
        }

        void fillHeaderSettings(Header* header) {
            header->seed = Randomizer::getSeed();
            header->settingsFlags = makeSettingsFlags();
            header->moonRequirement = static_cast<u8>(Randomizer::getMoonRequirementLevel());
            header->moonDifficulty = static_cast<u8>(Randomizer::getMoonDifficulty());
            header->logicDifficulty = static_cast<u8>(Randomizer::getLogicDifficulty());
            fillHintConfigurationHeader(header);
            header->reserved[Reserved_EntranceMode]
                = (static_cast<u8>(Randomizer::getEntranceRandoMode()) & cEntranceModeMask)
                | static_cast<u8>((static_cast<u8>(Randomizer::getShopLocationMode())
                                      << cShopLocationShift)
                    & cShopLocationMask)
                | (Randomizer::isShopPriceRandoEnabled() ? cShopPriceRandoBit : 0)
                | (Randomizer::isProportionalKingdomPricingEnabled()
                        ? cProportionalKingdomPricingBit
                        : 0);
            header->reserved[Reserved_StartingEquipment]
                = (static_cast<u8>(Randomizer::getStartingEquipment()) & cStartingEquipmentMask)
                | static_cast<u8>((static_cast<u8>(Randomizer::getStartPositionMode())
                                      << cStartPositionShift)
                    & cStartPositionMask)
                | (Randomizer::isChestOrderRandoEnabled() ? cChestOrderBit : 0);
            header->reserved[Reserved_CaptureAbilityDifficulty] = (static_cast<u8>(Randomizer::getCaptureAbilityDifficulty())
                                                                      & cCaptureAbilityDifficultyMask)
                | static_cast<u8>(
                    static_cast<u8>(Randomizer::getNpcSwapMode())
                    << cNpcSwapModeShift)
                | static_cast<u8>(
                    static_cast<u8>(Randomizer::getPropSwapMode())
                    << cPropSwapModeShift)
                | static_cast<u8>(Randomizer::isTwoDSwapRandoEnabled()
                        ? cTwoDSwapBit
                        : 0);
            header->reserved[Reserved_SkyboxMode]
                = ((static_cast<u8>(Randomizer::getSkyboxRandoMode()) + 1) & cSkyboxModeMask)
                | static_cast<u8>(
                    ((static_cast<u8>(Randomizer::getMusicSourceMode()) + 1) << cMusicSourceShift)
                    & cMusicSourceMask);
            header->reserved[Reserved_StabilizeShaders] = (static_cast<u8>(Randomizer::isStabilizeShadersEnabled()) + 1)
                | static_cast<u8>(makePropSwapCode() << cPropSwapCodeShift)
                | (Randomizer::isRandomizeTripleMoonsEnabled() ? cRandomizeTripleMoonsBit : 0)
                | (Randomizer::getVisitedKingdomsMapMode()
                            == Randomizer::VisitedKingdomsMapMode_Unlocked
                        ? cVisitedKingdomsUnlockedBit
                        : 0);
            header->reserved[Reserved_SpecialMoonFormat] = cSpecialMoonFormat
                | (Randomizer::isStartKingdomStateRandoEnabled()
                        ? cStartKingdomStateBit
                        : 0);
            header->reserved[Reserved_CapMoonUse] = static_cast<u8>(Randomizer::getCapKingdomMoonUse());
            header->reserved[Reserved_CloudMoonUse] = static_cast<u8>(Randomizer::getCloudKingdomMoonUse());
            header->reserved[Reserved_MoonMoonUse] = static_cast<u8>(Randomizer::getMoonKingdomMoonUse());
            // Stored as value+1 so 0 still means "not written" for the legacy
            // readers; the tag bit inside the value distinguishes the format.
            const u16 seedAxes = Randomizer::getSeedAxesPacked() + 1;
            header->reserved[Reserved_SeedStyleLo] = static_cast<u8>(seedAxes);
            header->reserved[Reserved_SeedStyleHi] = static_cast<u8>(seedAxes >> 8);
            const u32 moonTotal = Randomizer::getMoonRequirement();
            header->reserved[Reserved_MoonTotalLo] = static_cast<u8>(moonTotal & 0xFF);
            header->reserved[Reserved_MoonTotalHi] = static_cast<u8>((moonTotal >> 8) & 0xFF);
            header->reserved[Reserved_WorldPeaceMode]
                = (static_cast<u8>(Randomizer::getWorldPeaceRequirementMode()) + 1)
                | static_cast<u8>((static_cast<u8>(Randomizer::getSubareaUnlockMode())
                                      << cSubareaUnlockShift)
                    & cSubareaUnlockMask)
                | static_cast<u8>((static_cast<u8>(Randomizer::getSubareaUnlockHost())
                                      << cSubareaHostShift)
                    & cSubareaHostMask)
                | (Randomizer::isCoinRandoEnabled() ? cCoinRandoBit : 0);
            header->reserved[Reserved_MusicMode]
                = ((static_cast<u8>(Randomizer::getMusicRandoMode()) + 1) & cMusicModeMask)
                | (Randomizer::isMusic2DThemesDisabled() ? cMusicDisable2DBit : 0)
                | (Randomizer::isMusicExtraThemesDisabled() ? cMusicDisableExtraBit : 0)
                | (Randomizer::isMusicBossThemesDisabled() ? cMusicDisableBossBit : 0);
            header->reserved[Reserved_PurpleShopSettings] = static_cast<u8>(Randomizer::getPurpleShopAbilityCount())
                | static_cast<u8>(Randomizer::getPurpleShopCaptureCount() << 2)
                | static_cast<u8>(Randomizer::arePurpleShopMoonsEnabled() ? 0x10 : 0)
                | static_cast<u8>(static_cast<u8>(Randomizer::getPurpleShopPrice()) << 5);
            header->bowserWinCondition = (static_cast<u8>(Randomizer::getBowserWinCondition()) & cBowserWinConditionMask)
                | (static_cast<u8>(Randomizer::getRunMode()) << 6);
            if (Randomizer::isStreamerModeEnabled())
                header->bowserWinCondition |= cStreamerModeBit;
            header->toadetteMedalRatio = static_cast<u8>(Randomizer::getToadetteMedalRatioIdx());
            header->settingsFormat = cSeedAxisSettingsFormat;
        }

        // Resolve the skybox mode from a loaded header: prefer the explicit reserved
        // byte (mode+1); fall back to the legacy SkyboxRando/SubAreaSkybox bits.
        Randomizer::SkyboxRandoMode readSkyboxMode(const Header& header) {
            // Masked: the upper bits of this byte now carry the music source.
            const u8 enc = header.reserved[Reserved_SkyboxMode] & cSkyboxModeMask;
            if (enc != 0)
                return static_cast<Randomizer::SkyboxRandoMode>(enc - 1);

            const bool kingdom = (header.settingsFlags & SettingsFlag_SkyboxRando) != 0;
            const bool subarea = (header.settingsFlags & SettingsFlag_SubAreaSkybox) != 0;
            if (kingdom && subarea)
                return Randomizer::SkyboxRandoMode_Max;
            if (kingdom)
                return Randomizer::SkyboxRandoMode_Kingdoms;
            if (subarea)
                return Randomizer::SkyboxRandoMode_Subareas;
            return Randomizer::SkyboxRandoMode_None;
        }

        // Resolve the World Peace Requirement mode from a loaded header: prefer
        // the explicit reserved byte; fall back to the legacy WorldPeaceReq bit
        // (old saves can only resolve to StoryProgression or Required, never
        // OnKingdomComplete, which didn't exist yet).
        Randomizer::WorldPeaceRequirementMode readWorldPeaceMode(const Header& header) {
            // Only the low two bits are the world-peace mode+1; the rest of the
            // byte now carries the Subarea Unlock settings. Legacy saves stored
            // a bare 1-3 here, so masking reads them back unchanged.
            const u8 enc = header.reserved[Reserved_WorldPeaceMode] & cWorldPeaceModeMask;
            if (enc != 0)
                return static_cast<Randomizer::WorldPeaceRequirementMode>(enc - 1);

            return (header.settingsFlags & SettingsFlag_WorldPeaceReq) != 0
                ? Randomizer::WorldPeaceRequirementMode_Required
                : Randomizer::WorldPeaceRequirementMode_StoryProgression;
        }

        Randomizer::SubareaUnlockMode readSubareaUnlockMode(const Header& header) {
            if (!hasSubareaUnlockSettings(header))
                return Randomizer::SubareaUnlockMode_Unlocked;
            return static_cast<Randomizer::SubareaUnlockMode>(
                (header.reserved[Reserved_WorldPeaceMode] & cSubareaUnlockMask)
                >> cSubareaUnlockShift);
        }

        Randomizer::SubareaUnlockHost readSubareaUnlockHost(const Header& header) {
            if (!hasSubareaUnlockSettings(header))
                return Randomizer::SubareaUnlockHost_CapAndMoon;
            const u8 enc = static_cast<u8>(
                (header.reserved[Reserved_WorldPeaceMode] & cSubareaHostMask) >> cSubareaHostShift);
            return enc <= static_cast<u8>(Randomizer::SubareaUnlockHost_CapAndMoon)
                ? static_cast<Randomizer::SubareaUnlockHost>(enc)
                : Randomizer::SubareaUnlockHost_CapAndMoon;
        }

        // Stored as mode+1, so an unwritten (zero) field means Vanilla -- which
        // is what every save from before this setting existed played.
        Randomizer::MusicSourceMode readMusicSourceMode(const Header& header) {
            const u8 enc
                = (header.reserved[Reserved_SkyboxMode] & cMusicSourceMask) >> cMusicSourceShift;
            if (enc == 0)
                return Randomizer::MusicSourceMode::Vanilla;
            return static_cast<Randomizer::MusicSourceMode>(enc - 1);
        }

        Randomizer::MusicRandoMode readMusicMode(const Header& header) {
            const u8 enc = header.reserved[Reserved_MusicMode] & cMusicModeMask;
            if (enc != 0)
                return static_cast<Randomizer::MusicRandoMode>(enc - 1);
            return (header.settingsFlags & SettingsFlag_MusicRando) != 0
                ? Randomizer::MusicRandoMode::On
                : Randomizer::MusicRandoMode::Off;
        }

        void applyPurpleShopSettings(const Header& header) {
            if (!hasPurpleShopSettings(header)) {
                Randomizer::setPurpleShopAbilityCount(1);
                Randomizer::setPurpleShopCaptureCount(1);
                Randomizer::setPurpleShopMoonsEnabled(true);
                Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Purple50);
                return;
            }
            const u8 packed = header.reserved[Reserved_PurpleShopSettings];
            Randomizer::setPurpleShopAbilityCount(packed & 0x3);
            Randomizer::setPurpleShopCaptureCount((packed >> 2) & 0x3);
            Randomizer::setPurpleShopMoonsEnabled((packed & 0x10) != 0);
            Randomizer::setPurpleShopPrice(
                static_cast<Randomizer::PurpleShopPrice>((packed >> 5) & 0x7));
        }

        void applyBowserSettings(const Header& header) {
            if (hasBowserSettings(header)) {
                Randomizer::setBowserWinCondition(
                    static_cast<Randomizer::BowserWinCondition>(header.bowserWinCondition & cBowserWinConditionMask));
                Randomizer::setToadetteMedalRatioIdx(header.toadetteMedalRatio);
                return;
            }

            Randomizer::setBowserWinCondition(Randomizer::BowserWinCondition_DefeatBowser);
            Randomizer::setToadetteMedalRatioIdx(0);
        }

        bool isCompatible(const Header& header) {
            return header.magic == cMagic
                && isVersionSupported(header.version)
                && header.ldMoonCount == LD::sMoonCount
                && header.shineTableSize == ShineDB::sShineTableSize
                && header.entranceLinkCount > 0
                && header.entranceLinkCount <= EntranceDB::sLinkCount;
        }

        bool sidecarFileExists(const char* path) {
            if (!path)
                return false;
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;
            nn::fs::CloseFile(handle);
            return true;
        }

        bool validateSidecarFile(const char* path, Header* headerOut = nullptr,
            Meta* metaOut = nullptr) {
            if (!path)
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;

            long actualFileSize = 0;
            Header header = {};
            Meta meta = {};
            bool ok = nn::fs::GetFileSize(&actualFileSize, handle).IsSuccess()
                && actualFileSize >= static_cast<long>(sizeof(Header) + sizeof(Meta))
                && nn::fs::ReadFile(handle, 0, &header, sizeof(header)).IsSuccess()
                && nn::fs::ReadFile(handle, sizeof(Header), &meta, sizeof(meta)).IsSuccess()
                && isCompatible(header)
                && header.fileSize == static_cast<u32>(actualFileSize);

            const size_t payloadSize = ok
                ? static_cast<size_t>(actualFileSize) - sizeof(Header) - sizeof(Meta)
                : 0;
            ok = ok && payloadSize > 0 && payloadSize <= cMaxPayloadSize
                && nn::fs::ReadFile(handle, sizeof(Header) + sizeof(Meta),
                    sValidationPayload, payloadSize)
                       .IsSuccess()
                && calcCrc32(sValidationPayload, payloadSize) == header.payloadCrc32;
            nn::fs::CloseFile(handle);

            if (!ok)
                return false;
            if (headerOut)
                *headerOut = header;
            if (metaOut)
                *metaOut = meta;
            return true;
        }

        bool recoverSidecarIfNeeded(
            s32 slot, SaveRedirect::Category category, bool verbose,
            bool inspectExisting = false) {
            const auto primary = makePath(slot, category);
            // Recovery is normally for the rename window, where the primary
            // path is absent. Callers that found transaction artifacts request
            // validation of an existing primary too: it may be a torn/corrupt
            // new copy while a complete backup or pending copy is recoverable.
            if (sidecarFileExists(primary.cstr())) {
                if (!inspectExisting || validateSidecarFile(primary.cstr()))
                    return true;
            }

            const auto temp = makeTempPath(slot, category);
            const auto backup = makeBackupPath(slot, category);
            const bool tempValid = validateSidecarFile(temp.cstr());
            const bool backupValid = validateSidecarFile(backup.cstr());

            // A validated temp represents a fully flushed transaction that was
            // interrupted between the two renames. Prefer completing it; the
            // previous primary remains available as the backup.
            if (tempValid) {
                if (sidecarFileExists(primary.cstr())
                    && nn::fs::DeleteFile(primary.cstr()).IsFailure()) {
                    return false;
                }
                if (nn::fs::RenameFile(temp.cstr(), primary.cstr()).IsSuccess()
                    && validateSidecarFile(primary.cstr())) {
                    if (verbose)
                        Randomizer::logPush("SIDECAR recovered slot=%d source=temp", slot);
                    return true;
                }
            }

            // If the new file cannot be promoted, restore the last known-good
            // primary. An invalid/missing primary may be removed only while a
            // validated backup is still present.
            if (backupValid) {
                if (sidecarFileExists(primary.cstr())
                    && nn::fs::DeleteFile(primary.cstr()).IsFailure()) {
                    return false;
                }
                if (nn::fs::RenameFile(backup.cstr(), primary.cstr()).IsSuccess()
                    && validateSidecarFile(primary.cstr())) {
                    if (verbose)
                        Randomizer::logPush("SIDECAR recovered slot=%d source=backup", slot);
                    return true;
                }
            }
            return false;
        }

        bool replaceSidecarTransaction(s32 slot, SaveRedirect::Category category,
            const Header& header, const Meta& meta, const u8* payload,
            size_t payloadSize) {
            if (!payload || payloadSize == 0 || payloadSize > cMaxPayloadSize
                || header.fileSize != sizeof(Header) + sizeof(Meta) + payloadSize
                || header.payloadCrc32 != calcCrc32(payload, payloadSize)) {
                return false;
            }

            // Resolve a previously interrupted swap before reusing its temp or
            // backup names. A brand-new slot legitimately has nothing to
            // recover, so the return value is intentionally advisory here.
            recoverSidecarIfNeeded(slot, category, true, true);

            const auto primary = makePath(slot, category);
            const auto temp = makeTempPath(slot, category);
            const auto backup = makeBackupPath(slot, category);
            const bool hadValidPrimary = validateSidecarFile(primary.cstr());
            if (!hadValidPrimary && validateSidecarFile(temp.cstr())
                && !validateSidecarFile(backup.cstr())) {
                // Recovery could not promote the only valid on-disk copy. Do
                // not remove it merely to reuse the temp filename.
                return false;
            }

            // Recovery promotes any valid temp when the primary is unavailable.
            // Consequently, anything still at the temp path is stale or invalid
            // and can be removed before creating the next complete candidate.
            if (sidecarFileExists(temp.cstr())
                && nn::fs::DeleteFile(temp.cstr()).IsFailure()) {
                return false;
            }
            if (nn::fs::CreateFile(temp.cstr(), header.fileSize).IsFailure())
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, temp.cstr(), nn::fs::OpenMode_Write).IsFailure()) {
                nn::fs::DeleteFile(temp.cstr());
                return false;
            }
            const auto flush = nn::fs::WriteOption::CreateOption(
                nn::fs::WriteOptionFlag_Flush);
            bool ok = nn::fs::WriteFile(handle, 0, &header, sizeof(header), flush).IsSuccess();
            ok = ok && nn::fs::WriteFile(handle, sizeof(Header), &meta, sizeof(meta), flush).IsSuccess();
            ok = ok && nn::fs::WriteFile(handle, sizeof(Header) + sizeof(Meta), payload, payloadSize, flush).IsSuccess();
            nn::fs::CloseFile(handle);
            if (!ok || !validateSidecarFile(temp.cstr())) {
                // The candidate was never valid, so removing it cannot discard
                // a recoverable sidecar.
                nn::fs::DeleteFile(temp.cstr());
                return false;
            }

            if (hadValidPrimary) {
                // Both primary and temp are valid at this point. It is safe to
                // retire a stale backup before moving the primary into its
                // recoverable position.
                if (sidecarFileExists(backup.cstr())
                    && nn::fs::DeleteFile(backup.cstr()).IsFailure()) {
                    return false;
                }
                if (nn::fs::RenameFile(primary.cstr(), backup.cstr()).IsFailure())
                    return false;
            } else if (sidecarFileExists(primary.cstr())) {
                // The temp has been validated, so an incompatible/corrupt
                // primary is no longer the only possible recovery source.
                if (nn::fs::DeleteFile(primary.cstr()).IsFailure())
                    return false;
            }

            if (nn::fs::RenameFile(temp.cstr(), primary.cstr()).IsFailure()) {
                // Restore the previous primary when possible. If this rename is
                // also interrupted, the read boundary can recover from either
                // the still-valid temp or backup.
                if (hadValidPrimary)
                    nn::fs::RenameFile(backup.cstr(), primary.cstr());
                return false;
            }

            if (!validateSidecarFile(primary.cstr())) {
                if (hadValidPrimary && validateSidecarFile(backup.cstr())) {
                    nn::fs::DeleteFile(primary.cstr());
                    nn::fs::RenameFile(backup.cstr(), primary.cstr());
                }
                return false;
            }
            return true;
        }

        constexpr u8 cVisitOrderUnranked = 0xFF;

        u8 getMetaVisitOrder(const Meta& meta, s32 worldId) {
            if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
                return cVisitOrderUnranked;
            return worldId < 8 ? meta.visitOrderLo[worldId] : meta.visitOrderHi[worldId - 8];
        }

        void setMetaVisitOrder(Meta* meta, s32 worldId, u8 order) {
            if (!meta || worldId < 0 || worldId >= WorldMeta::sWorldCount)
                return;
            if (worldId < 8)
                meta->visitOrderLo[worldId] = order;
            else
                meta->visitOrderHi[worldId - 8] = order;
        }

        // Packed into the low 17 bits of Meta::reserved[0x3], one per WorldId.
        // reserved[2]'s high two bits carry RunMode bits 0-1 and bit 1 carries
        // bit 2.  The forced-peace mask continues to own bit 0.
        u32 getMetaForcedPeaceKingdoms(const Meta& meta) {
            return (static_cast<u32>(meta.reserved[0])
                       | (static_cast<u32>(meta.reserved[1]) << 8)
                       | (static_cast<u32>(meta.reserved[2] & 0x01) << 16))
                & 0x1FFFFu;
        }

        void setMetaForcedPeaceKingdoms(Meta* meta, u32 mask) {
            if (!meta)
                return;
            meta->reserved[0] = static_cast<u8>(mask & 0xFF);
            meta->reserved[1] = static_cast<u8>((mask >> 8) & 0xFF);
            const u8 runMode = static_cast<u8>(Randomizer::getRunMode());
            meta->reserved[2] = static_cast<u8>(((mask >> 16) & 0x01)
                | ((runMode & 0x04) >> 1)
                | ((runMode & 0x03) << 6));
        }

        void fillMeta(Meta* meta) {
            if (!meta)
                return;

            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
                const s8 order = Randomizer::getKingdomVisitOrder(worldId);
                setMetaVisitOrder(meta, worldId, order < 0 ? cVisitOrderUnranked : static_cast<u8>(order));
            }
            setMetaForcedPeaceKingdoms(meta, Randomizer::getForcedPeaceKingdoms());
        }

        void fillMeta(Meta* meta, GameDataHolderAccessor accessor) {
            fillMeta(meta);
            if (!meta || !accessor.mData || !accessor.mData->getGameDataFile())
                return;

            GameDataFile* file = accessor.mData->getGameDataFile();
            meta->totalMoons = file->getTotalShineNum();
            meta->currentWorldId = file->getCurrentWorldId();
            meta->visitedKingdoms = Randomizer::getVisitedKingdoms();
        }

        void fillMeta(Meta* meta, const GameDataFile* file) {
            fillMeta(meta);
            if (!meta || !file)
                return;

            meta->totalMoons = file->getTotalShineNum();
            meta->currentWorldId = file->getCurrentWorldId();
            meta->visitedKingdoms = Randomizer::getVisitedKingdoms();
        }

        void writeConnRecord(ConnRecord* out, EntranceRando::Conn conn) {
            out->link = conn.link;
            out->kind = static_cast<u8>(conn.kind);
            out->pad = 0;
        }

        EntranceRando::Conn readConnRecord(const ConnRecord& record) {
            return { record.link, static_cast<EntranceRando::ConnKind>(record.kind) };
        }

        bool writeHeaderAt(s32 slot, const Header& header, SaveRedirect::Category category) {
            nn::fs::FileHandle handle;
            const auto path = makePath(slot, category);
            if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_ReadWrite).IsFailure())
                return false;
            const bool ok = nn::fs::WriteFile(handle, 0, &header, sizeof(Header),
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            return ok;
        }

        bool writeMetaAt(
            s32 slot, const Meta& meta, SaveRedirect::Category category, bool verbose = true) {
            nn::fs::FileHandle handle;
            const auto path = makePath(slot, category);
            if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_ReadWrite).IsFailure())
                return false;
            const bool ok = nn::fs::WriteFile(handle, sizeof(Header), &meta, sizeof(Meta),
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            if (verbose) {
                Randomizer::logPush(
                    "SIDECAR meta-write slot=%d ok=%d moons=%d curN=%d visited=%08x",
                    slot, ok, meta.totalMoons, meta.currentWorldId,
                    meta.visitedKingdoms);
            }
            return ok;
        }

        bool readHeaderImpl(
            s32 slot, Header* header, Meta* meta, bool verbose, SaveRedirect::Category category) {
            if (!isValidSlot(slot) || !header) {
                if (verbose) {
                    Randomizer::logPush("SIDECAR read reject slot=%d header=%d",
                        slot, header ? 1 : 0);
                }
                return false;
            }
            nn::fs::FileHandle handle;
            const auto path = makePath(slot, category);
            const auto temp = makeTempPath(slot, category);
            const auto backup = makeBackupPath(slot, category);
            // Avoid a full payload/CRC scan on the ordinary read path, but do
            // validate the primary before trusting it when a transactional
            // artifact remains. This lets recovery promote a valid backup or
            // pending sidecar when the existing primary was torn.
            const bool inspectExisting = sidecarFileExists(temp.cstr())
                || sidecarFileExists(backup.cstr());
            recoverSidecarIfNeeded(slot, category, verbose, inspectExisting);
            if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure()) {
                if (verbose) {
                    Randomizer::logPush("SIDECAR read open-fail slot=%d path=%s",
                        slot, path.cstr());
                }
                return false;
            }

            const bool headerOk = nn::fs::ReadFile(handle, 0, header, sizeof(Header)).IsSuccess();
            bool metaOk = true;
            if (meta)
                metaOk = nn::fs::ReadFile(handle, sizeof(Header), meta, sizeof(Meta)).IsSuccess();
            nn::fs::CloseFile(handle);
            if (verbose) {
                Randomizer::logPush(
                    "SIDECAR read slot=%d hdr=%d meta=%d magic=%08x ver=%u size=%u crc=%08x seed=%u links=%d",
                    slot, headerOk, metaOk, header->magic, header->version,
                    header->fileSize, header->payloadCrc32, header->seed,
                    header->entranceLinkCount);
            }
            return headerOk && metaOk;
        }
    } // namespace

    bool isVersionSupported(u32 version) {
        // Each newer version only appends payload records the reader gates on, so
        // older slots stay loadable rather than being orphaned by a format bump.
        return version == cVersion
            || version == cLegacyVersion19
            || version == cLegacyVersion18
            || version == cLegacyVersion17
            || version == cLegacyVersion16
            || version == cLegacyVersion15
            || version == cLegacyVersion14
            || version == cLegacyVersion13
            || version == cLegacyVersion12
            || version == cLegacyVersion11
            || version == cLegacyVersion10
            || version == cLegacyVersion9
            || version == cLegacyVersion8
            || version == cLegacyVersion7
            || version == cNuzlockeStartFormatVersion
            || version == cLogicDirectedStartFormatVersion
            || version == cLogicDirectedStartFormatVersion - 1;
    }

    void resetAutoGrantState() {
        clearAutoGrantState();
    }

    s32 getAutoGrantCount() {
        return sAutoGrantCount;
    }

    s32 applyAutoGrants(GameDataHolderWriter writer) {
        if (sAutoGrantCount <= 0 || sAutoGrantNotified)
            return 0;
        if (!writer.mData || !writer->getGameDataFile())
            return -1;

        s32 applied = 0;
        for (s32 i = 0; i < sAutoGrantCount; i++) {
            const s32 item = sAutoGrantItems[i];
            const ShineDB::ShineEntry* entry = MoonRando::getMoonEntry(item);
            if (!entry || !MoonHooks::grantMoonAuto(writer, entry, entry)) {
                Randomizer::logPush(
                    "auto-grant failed index=%d item=%d applied=%d/%d",
                    i, item, applied, sAutoGrantCount);
                return -1;
            }
            applied++;
        }

        sAutoGrantNotified = true;
        sAutoGrantNotificationDirty = true;
        Randomizer::logPush("auto-grants applied count=%d", applied);
        return applied;
    }

    bool commitAutoGrantsNotified(s32 slot, SaveRedirect::Category category) {
        if (!sAutoGrantNotificationDirty || sAutoGrantCount <= 0)
            return true;
        if (!isValidSlot(slot))
            return false;

        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category)
            || header.version < cAutoGrantFormatVersion)
            return false;
        if (!sAutoGrantSeedValid || header.seed != sAutoGrantSeed)
            return false;

        const size_t payloadSize = header.fileSize >= sizeof(Header) + sizeof(Meta)
            ? header.fileSize - sizeof(Header) - sizeof(Meta)
            : 0;
        if (payloadSize == 0 || payloadSize > cMaxPayloadSize)
            return false;

        nn::fs::FileHandle handle;
        const auto path = makePath(slot, category);
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false;
        const bool readOk = nn::fs::ReadFile(handle,
            sizeof(Header) + sizeof(Meta), sPayload, payloadSize)
                                .IsSuccess();
        nn::fs::CloseFile(handle);
        if (!readOk || calcCrc32(sPayload, payloadSize) != header.payloadCrc32)
            return false;

        const s32 placementCount = *reinterpret_cast<const s32*>(sPayload);
        if (expectedPayloadSize(placementCount, header.entranceLinkCount, header.version)
                != payloadSize
            || payloadSize < sizeof(AutoGrantBlock))
            return false;

        AutoGrantBlock block = {};
        std::memcpy(&block, sPayload + payloadSize - sizeof(block), sizeof(block));
        if (block.count != sAutoGrantCount || block.notified > 1)
            return false;
        for (s32 i = 0; i < sAutoGrantCount; i++) {
            if (block.grants[i].itemKind != cAutoGrantMoonItemKind
                || block.grants[i].itemId != sAutoGrantItems[i])
                return false;
        }
        if (block.notified != 0) {
            sAutoGrantNotificationDirty = false;
            return true;
        }

        block.notified = 1;
        std::memcpy(sPayload + payloadSize - sizeof(block), &block, sizeof(block));
        header.payloadCrc32 = calcCrc32(sPayload, payloadSize);
        const bool ok = replaceSidecarTransaction(
            slot, category, header, meta, sPayload, payloadSize);
        if (ok)
            sAutoGrantNotificationDirty = false;
        Randomizer::logPush(
            "SIDECAR auto-grant notification slot=%d ok=%d count=%d",
            slot, ok ? 1 : 0, sAutoGrantCount);
        return ok;
    }

    bool readHeader(s32 slot, Header* header, Meta* meta, SaveRedirect::Category category) {
        return readHeaderImpl(slot, header, meta, true, category);
    }

    bool write(s32 slot, SaveRedirect::Category category) {
        if (!ensureSlotDir(slot, category))
            return false;

        if (Randomizer::isMoonRandoEnabled()
            && (!MoonRando::isInitialized() || !MoonRando::logicSucceeded()))
            return false;

        Header header = {};
        Meta meta = {};
        header.magic = cMagic;
        header.version = cVersion;
        fillHeaderSettings(&header);
        header.entranceAttempt = MoonRando::getLastEntranceAttempt();
        header.logicSucceeded = MoonRando::logicSucceeded() ? 1 : 0;
        header.ldMoonCount = LD::sMoonCount;
        header.shineTableSize = ShineDB::sShineTableSize;
        header.entranceLinkCount = EntranceDB::sLinkCount;
        fillMeta(&meta);

        const s32 autoGrantCount = Randomizer::isMoonRandoEnabled()
                && MoonRando::isInitialized()
            ? MoonRando::getAutoGrantCount()
            : 0;
        if (autoGrantCount < 0 || autoGrantCount > MoonRando::kMaxAutoGrants)
            return false;
        AutoGrantBlock autoGrantBlock = {};
        autoGrantBlock.count = static_cast<u16>(autoGrantCount);
        const bool preserveAutoGrantState = sAutoGrantSeedValid
            && sAutoGrantSeed == header.seed
            && sameAutoGrantList(autoGrantCount);
        autoGrantBlock.notified = preserveAutoGrantState && sAutoGrantNotified ? 1 : 0;
        for (s32 i = 0; i < autoGrantCount; i++) {
            const s32 item = MoonRando::getAutoGrantItemIndex(i);
            if (item < 0 || item > 0xFFFF)
                return false;
            autoGrantBlock.grants[i] = {
                cAutoGrantMoonItemKind,
                static_cast<u16>(item),
            };
        }

        u8* cursor = sPayload;
        u8* end = sPayload + cMaxPayloadSize;
        const s32 placementCount = MoonRando::isInitialized() ? MoonRando::getPlacementCount() : 0;
        if (!appendBytes(cursor, end, &placementCount, sizeof(placementCount)))
            return false;

        s32 itemForLocation[LD::sMoonCount];
        for (s32 i = 0; i < LD::sMoonCount; i++)
            itemForLocation[i] = MoonRando::isInitialized() ? MoonRando::getPlacementItemForLocation(i) : -1;
        if (!appendBytes(cursor, end, itemForLocation, sizeof(itemForLocation)))
            return false;

        for (s32 i = 0; i < placementCount; i++) {
            const MoonRando::Placement* placement = MoonRando::getPlacement(i);
            MoonRando::SerializedPlacement saved = {
                placement ? ShineDB::findIndex(placement->location) : -1,
                placement ? ShineDB::findIndex(placement->item) : -1,
            };
            if (!appendBytes(cursor, end, &saved, sizeof(saved)))
                return false;
        }

        s16 cloudPaintingLinkOrder[LD::sPaintingLinkCount] = {};
        if (MoonRando::getCloudPaintingLinkOrder(
                cloudPaintingLinkOrder, LD::sPaintingLinkCount)
                != LD::sPaintingLinkCount
            || !appendBytes(cursor, end, cloudPaintingLinkOrder,
                sizeof(cloudPaintingLinkOrder)))
            return false;

        LogicDirectedStartRecord logicStarts = {};
        AbilityRando::AbilityId abilityOverrides[2] = {};
        const s32 abilityOverrideCount = AbilityRando::getLogicStartingOverride(
            abilityOverrides, 2);
        if (abilityOverrideCount < 0 || abilityOverrideCount > 2)
            return false;
        logicStarts.abilityCount = static_cast<u8>(abilityOverrideCount);
        for (s32 i = 0; i < abilityOverrideCount; i++)
            logicStarts.abilities[i] = static_cast<s16>(abilityOverrides[i]);

        const s32 captureOverrideCount = CaptureRando::getLogicStartingOverride(
            logicStarts.captures, 4);
        if (captureOverrideCount < 0 || captureOverrideCount > 4)
            return false;
        logicStarts.captureCount = static_cast<u8>(captureOverrideCount);
        if (!appendBytes(cursor, end, &logicStarts, sizeof(logicStarts)))
            return false;

        const AbilityRando::AbilityId capThrowOverride = AbilityRando::getLogicStartingCapThrowOverride();
        const NuzlockeStartRecord nuzlockeStart = {
            capThrowOverride == AbilityRando::AbilityId_Count
                ? static_cast<s16>(-1)
                : static_cast<s16>(capThrowOverride),
        };
        if (!appendBytes(cursor, end, &nuzlockeStart, sizeof(nuzlockeStart)))
            return false;

        const GameModeSettingsRecord gameModeSettings = {
            static_cast<u8>(Randomizer::getTimerStartIdx()),
            static_cast<u8>(Randomizer::getTimerMoonBonusIdx()),
            static_cast<u8>(Randomizer::getTimerTripleBonusIdx()),
            static_cast<u8>(static_cast<u8>(Randomizer::getSoftNuzlockeLoss())
                | (Randomizer::getExtremeNuzlockeEnabled() ? cExtremeNuzlockeBit : 0)),
            static_cast<u8>(Randomizer::isCustomToadetteMedalCountsEnabled() ? 1 : 0),
            static_cast<u8>(Randomizer::getCustomToadetteMedalAvailable()),
            static_cast<u8>(Randomizer::getCustomToadetteMedalRequired()),
            static_cast<u8>(
                ((static_cast<u8>(Randomizer::getSoftNuzlockeAbilityLoss()) + 1) & 0x0f)
                | (((static_cast<u8>(Randomizer::getSoftNuzlockeCaptureLoss()) + 1) & 0x0f) << 4)),
        };
        if (!appendBytes(cursor, end, &gameModeSettings, sizeof(gameModeSettings)))
            return false;

        const MoonAllocationSettingsRecord moonAllocationSettings = {
            static_cast<u8>((Randomizer::isMoonReallocationEnabled()
                                    ? cMoonAllocationMasterBit
                                    : 0)
                | (Randomizer::isRandomizeMoonAllocationEnabled()
                        ? cMoonAllocationRandomizeBit
                        : 0)),
            static_cast<u8>(Randomizer::getMoonAllocationDifficulty()),
            static_cast<u8>(Randomizer::getMoonCoinPolicy()),
            0,
        };
        if (!appendBytes(cursor, end, &moonAllocationSettings,
                sizeof(moonAllocationSettings)))
            return false;

        const TimerPenaltySettingsRecord timerPenaltySettings = {
            static_cast<u8>(Randomizer::getTimerDamagePenaltyIdx()),
            static_cast<u8>(Randomizer::getTimerDeathPenaltyIdx()),
            static_cast<u8>(Randomizer::getTimerHalfLifeIdx()),
            0,
        };
        if (!appendBytes(cursor, end, &timerPenaltySettings,
                sizeof(timerPenaltySettings)))
            return false;

        const HintConfigurationSettingsRecord hintConfiguration = {
            Randomizer::packHintPriceProfile(Randomizer::getHintCoinPriceProfile()),
            Randomizer::packHintPriceProfile(Randomizer::getHintTokenPriceProfile()),
        };
        if (!appendBytes(cursor, end, &hintConfiguration,
                sizeof(hintConfiguration)))
            return false;

        const MultiplayerSettingsRecord multiplayerSettings = {
            cMultiplayerSettingsRecordVersion,
            static_cast<u8>((Randomizer::isDeathLinkEnabled() ? cDeathLinkBit : 0)
                | (Randomizer::isSyncOtherTeamsEnabled() ? cSyncOtherTeamsBit : 0)),
            {},
        };
        if (!appendBytes(cursor, end, &multiplayerSettings,
                sizeof(multiplayerSettings)))
            return false;

        const MovementAbilitySettingsRecord movementAbilities = {
            Randomizer::packMovementAbilitySettings(),
            0,
        };
        if (!appendBytes(cursor, end, &movementAbilities,
                sizeof(movementAbilities)))
            return false;

        KingdomTargetSettingsRecord kingdomTargets = {};
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            kingdomTargets.moonTargets[worldId]
                = Randomizer::getKingdomMoonTarget(worldId);
            kingdomTargets.tripleMoonTargets[worldId]
                = Randomizer::getKingdomTripleMoonTarget(worldId);
        }
        if (!appendBytes(cursor, end, &kingdomTargets, sizeof(kingdomTargets)))
            return false;

        WaveCSettingsRecord waveC = {};
        waveC.startKingdomsMask = Randomizer::getStartKingdomsMask();
        waveC.capMoonUseMask = Randomizer::getCapKingdomMoonUseMask();
        waveC.cloudMoonUseMask = Randomizer::getCloudKingdomMoonUseMask();
        waveC.moonMoonUseMask = Randomizer::getMoonKingdomMoonUseMask();
        waveC.moonRockKeyScope = static_cast<u8>(Randomizer::getMoonRockKeyScope());
        waveC.subareaKeyScope = static_cast<u8>(Randomizer::getSubareaKeyScope());
        waveC.visitedKingdomsMapMode = static_cast<u8>(
            Randomizer::getVisitedKingdomsMapMode());
        s16 dev = Randomizer::getMoonRequirementDeviation();
        waveC.moonRequirementDeviations[0] = static_cast<s8>(dev & 0xFF);
        waveC.moonRequirementDeviations[1] = static_cast<s8>((dev >> 8) & 0xFF);
        for (s32 i = 2; i < WorldMeta::sWorldCount; i++)
            waveC.moonRequirementDeviations[i] = 0;
        if (!appendBytes(cursor, end, &waveC, sizeof(waveC)))
            return false;

        const AdvancedWaveBSettingsRecord advancedWaveB = {
            Randomizer::getCaptureLockPoolMask(0),
            Randomizer::getCaptureLockPoolMask(1),
            Randomizer::getAbilityLockPoolMask(),
            static_cast<s16>(Randomizer::isStartingEquipmentCustom()
                    ? Randomizer::getStartingCaptureCount()
                    : -1),
            static_cast<s16>(Randomizer::isStartingEquipmentCustom()
                    ? Randomizer::getStartingThrowCount()
                    : -1),
            static_cast<s16>(Randomizer::isStartingEquipmentCustom()
                    ? Randomizer::getStartingAbilityCount()
                    : -1),
            static_cast<u16>(Randomizer::isPurpleShopPriceCustom()
                    ? Randomizer::getPurpleShopCustomPrice()
                    : 0),
            { static_cast<u8>(Randomizer::isProgressiveAbilitiesEnabled() ? 2 : 1) },
        };
        if (!appendBytes(cursor, end, &advancedWaveB, sizeof(advancedWaveB)))
            return false;

        AdvancedWaveDSettingsRecord advancedWaveD = {};
        if (Randomizer::isTimerStartCustom())
            advancedWaveD.timerCustomMask |= 1u << 0;
        if (Randomizer::isTimerMoonBonusCustom())
            advancedWaveD.timerCustomMask |= 1u << 1;
        if (Randomizer::isTimerTripleBonusCustom())
            advancedWaveD.timerCustomMask |= 1u << 2;
        if (Randomizer::isTimerDamagePenaltyCustom())
            advancedWaveD.timerCustomMask |= 1u << 3;
        if (Randomizer::isTimerDeathPenaltyCustom())
            advancedWaveD.timerCustomMask |= 1u << 4;
        if (Randomizer::isTimerHalfLifeCustom())
            advancedWaveD.timerCustomMask |= 1u << 5;
        if (Randomizer::isSoftNuzlockeLossCustom())
            advancedWaveD.nuzlockeCustomMask |= 1u << 0;
        if (Randomizer::isSoftNuzlockeAbilityLossCustom())
            advancedWaveD.nuzlockeCustomMask |= 1u << 1;
        if (Randomizer::isSoftNuzlockeCaptureLossCustom())
            advancedWaveD.nuzlockeCustomMask |= 1u << 2;

        advancedWaveD.timerCustomSeconds[0] = Randomizer::getTimerStartCustomSeconds();
        advancedWaveD.timerCustomSeconds[1] = Randomizer::getTimerMoonBonusCustomSeconds();
        advancedWaveD.timerCustomSeconds[2] = Randomizer::getTimerTripleBonusCustomSeconds();
        advancedWaveD.timerCustomSeconds[3] = Randomizer::getTimerDamagePenaltyCustomSeconds();
        advancedWaveD.timerCustomSeconds[4] = Randomizer::getTimerDeathPenaltyCustomSeconds();
        advancedWaveD.timerCustomSeconds[5] = Randomizer::getTimerHalfLifeCustomSeconds();
        advancedWaveD.nuzlockeCustomPercent[0] = static_cast<s16>(Randomizer::getSoftNuzlockeLossCustomPercent());
        advancedWaveD.nuzlockeCustomPercent[1] = static_cast<s16>(Randomizer::getSoftNuzlockeAbilityLossCustomPercent());
        advancedWaveD.nuzlockeCustomPercent[2] = static_cast<s16>(Randomizer::getSoftNuzlockeCaptureLossCustomPercent());
        for (u8 category = 0; category < Randomizer::kHintPriceCategoryCount;
             category++) {
            const Randomizer::HintPriceCategory hintCategory = static_cast<Randomizer::HintPriceCategory>(category);
            if (Randomizer::isHintPriceCustom(hintCategory))
                advancedWaveD.hintCustomMask |= static_cast<u8>(1u << category);
            if (Randomizer::isHintCoinOverride(hintCategory))
                advancedWaveD.hintCoinOverrideMask |= static_cast<u8>(1u << category);
            advancedWaveD.hintCustomPrice[category] = Randomizer::getHintCustomPrice(hintCategory);
        }
        advancedWaveD.musicThemeMask = Randomizer::getMusicThemeMask();
        advancedWaveD.easyMinigameLow = static_cast<u16>(
            Randomizer::getEasyMinigameLow());
        advancedWaveD.easyMinigameHigh = static_cast<u16>(
            Randomizer::getEasyMinigameHigh());
        advancedWaveD.sheepHerdingCount = static_cast<u8>(
            Randomizer::getSheepHerdingCount());
        if (!appendBytes(cursor, end, &advancedWaveD, sizeof(advancedWaveD)))
            return false;

        ConnRecord conn = {};
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            writeConnRecord(&conn, EntranceRando::getPipeConn(i));
            if (!appendBytes(cursor, end, &conn, sizeof(conn)))
                return false;
        }
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            writeConnRecord(&conn, EntranceRando::getHomeDoorConn(i, true));
            if (!appendBytes(cursor, end, &conn, sizeof(conn)))
                return false;
        }
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            writeConnRecord(&conn, EntranceRando::getSubDoorConn(i, false));
            if (!appendBytes(cursor, end, &conn, sizeof(conn)))
                return false;
        }
        for (s32 i = 0; i < EntranceDB::sLinkCount; i++) {
            writeConnRecord(&conn, EntranceRando::getSubDoorConn(i, true));
            if (!appendBytes(cursor, end, &conn, sizeof(conn)))
                return false;
        }
        KingdomMoonUseRecord kingdomMoonUse = {};
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            kingdomMoonUse.masks[worldId] = Randomizer::getKingdomMoonUseMask(worldId);
        if (!appendBytes(cursor, end, &kingdomMoonUse, sizeof(kingdomMoonUse)))
            return false;

        RequirementBaseRecord requirementBases = {};
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            requirementBases.bases[worldId] = static_cast<s16>(
                RequirementRando::getAuthoredRequirementBaseRaw(worldId));
        if (!appendBytes(cursor, end, &requirementBases, sizeof(requirementBases)))
            return false;

        RequirementDeviationRecord requirementDeviations = {};
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            requirementDeviations.deviations[worldId] = static_cast<s16>(
                RequirementRando::getAuthoredRequirementDeviationRaw(worldId));
        if (!appendBytes(cursor, end, &requirementDeviations,
                sizeof(requirementDeviations)))
            return false;

        if (!appendBytes(cursor, end, &autoGrantBlock, sizeof(autoGrantBlock)))
            return false;

        const size_t payloadSize = static_cast<size_t>(cursor - sPayload);
        header.fileSize = static_cast<u32>(sizeof(Header) + sizeof(Meta) + payloadSize);
        header.payloadCrc32 = calcCrc32(sPayload, payloadSize);

        const bool ok = replaceSidecarTransaction(
            slot, category, header, meta, sPayload, payloadSize);
        if (ok) {
            sAutoGrantCount = autoGrantCount;
            for (s32 i = 0; i < autoGrantCount; i++)
                sAutoGrantItems[i] = MoonRando::getAutoGrantItemIndex(i);
            sAutoGrantNotified = autoGrantBlock.notified != 0;
            sAutoGrantNotificationDirty = false;
            sAutoGrantSeed = header.seed;
            sAutoGrantSeedValid = true;
        }
        Randomizer::logPush(
            "SIDECAR write slot=%d ok=%d ver=%u size=%u crc=%08x seed=%u placements=%d autoGrants=%d",
            slot, ok, header.version, header.fileSize, header.payloadCrc32,
            header.seed, placementCount, autoGrantCount);
        return ok;
    }

    bool loadAndApply(s32 slot, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category) || !isCompatible(header))
            return false;
        clearAutoGrantState();

        const size_t payloadSize = header.fileSize >= sizeof(Header) + sizeof(Meta)
            ? header.fileSize - sizeof(Header) - sizeof(Meta)
            : 0;
        if (payloadSize == 0 || payloadSize > cMaxPayloadSize)
            return false;

        nn::fs::FileHandle handle;
        const auto path = makePath(slot, category);
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false;

        const bool readOk = nn::fs::ReadFile(handle, sizeof(Header) + sizeof(Meta), sPayload, payloadSize).IsSuccess();
        nn::fs::CloseFile(handle);
        if (!readOk || calcCrc32(sPayload, payloadSize) != header.payloadCrc32)
            return false;

        const u8* cursor = sPayload;
        const u8* end = sPayload + payloadSize;
        const void* ptr = nullptr;
        const bool sheepHerdingEnabled = Randomizer::isOneSheepHerdingEnabled();
        const s32 sheepHerdingCount = Randomizer::getSheepHerdingCount();

        // Allocation settings were introduced in version 8. Reset all four
        // values before reading any optional versioned payload records so an
        // older slot cannot inherit them from a previously loaded slot.
        Randomizer::setMoonReallocationEnabled(false);
        Randomizer::setRandomizeMoonAllocationEnabled(false);
        Randomizer::setMoonAllocationDifficulty(
            Randomizer::MoonAllocationDifficulty::Vanilla);
        Randomizer::setMoonCoinPolicy(Randomizer::MoonCoinPolicy::Vanilla);
        // Wave C is optional. Establish its defaults before applying the
        // header's legacy projections so a v13-or-older slot cannot inherit a
        // previous slot's masks, scopes, custom map list, or deviations.
        Randomizer::setCapKingdomMoonUseMask(
            static_cast<u8>(1u << Randomizer::SpecialMoonUse_Coins));
        Randomizer::setCloudKingdomMoonUseMask(
            static_cast<u8>(1u << Randomizer::SpecialMoonUse_Coins));
        Randomizer::setMoonKingdomMoonUseMask(
            static_cast<u8>(1u << Randomizer::SpecialMoonUse_Coins));
        Randomizer::setMoonRockKeyScope(Randomizer::KeyScope::WithinKingdom);
        Randomizer::setSubareaKeyScope(Randomizer::KeyScope::WithinKingdom);
        Randomizer::setStartKingdomsMask(1u << WorldMeta::WorldId_Cap);
        Randomizer::clearMoonRequirementDeviations();
        Randomizer::clearKingdomMoonTargets();
        Randomizer::clearKingdomTripleMoonTargets();
        // Wave D settings are optional too. Clear them before reading the
        // versioned record so loading an older slot cannot inherit custom
        // values, per-entry hint overrides, a filtered music pool, or the
        // previous run's QoL thresholds.
        Randomizer::clearHintPriceOverrides();
        Randomizer::setMusicThemeMask(~static_cast<u64>(0));
        Randomizer::setTimerStartCustomSeconds(3 * 60);
        Randomizer::setTimerMoonBonusCustomSeconds(45);
        Randomizer::setTimerTripleBonusCustomSeconds(60);
        Randomizer::setTimerDamagePenaltyCustomSeconds(0);
        Randomizer::setTimerDeathPenaltyCustomSeconds(0);
        Randomizer::setTimerHalfLifeCustomSeconds(0);
        Randomizer::setSoftNuzlockeLossCustomPercent(50);
        Randomizer::setSoftNuzlockeAbilityLossCustomPercent(50);
        Randomizer::setSoftNuzlockeCaptureLossCustomPercent(50);
        Randomizer::setEasyMinigameLow(Randomizer::kEasyMinigameDefaultLow);
        Randomizer::setEasyMinigameHigh(Randomizer::kEasyMinigameDefaultHigh);
        Randomizer::setSheepHerdingCount(Randomizer::kSheepHerdingDefaultCount);
        Randomizer::setSheepHerdingCount(sheepHerdingCount);
        Randomizer::setOneSheepHerdingEnabled(sheepHerdingEnabled);

        if (!takeBytes(cursor, end, &ptr, sizeof(s32)))
            return false;
        const s32 placementCount = *static_cast<const s32*>(ptr);
        const s32 savedEntranceLinkCount = header.entranceLinkCount;
        const size_t expectedSize = expectedPayloadSize(placementCount, savedEntranceLinkCount, header.version);
        if (expectedSize == 0 || expectedSize != payloadSize)
            return false;

        if (!takeBytes(cursor, end, &ptr, sizeof(s32) * LD::sMoonCount))
            return false;
        const s32* itemForLocation = static_cast<const s32*>(ptr);

        if (!takeBytes(cursor, end, &ptr, sizeof(MoonRando::SerializedPlacement) * placementCount))
            return false;
        const MoonRando::SerializedPlacement* placements = static_cast<const MoonRando::SerializedPlacement*>(ptr);

        const s16* cloudPaintingLinkOrder = nullptr;
        const LogicDirectedStartRecord* logicStarts = nullptr;
        const NuzlockeStartRecord* nuzlockeStart = nullptr;
        const GameModeSettingsRecord* gameModeSettings = nullptr;
        const MoonAllocationSettingsRecord* moonAllocationSettings = nullptr;
        const TimerPenaltySettingsRecord* timerPenaltySettings = nullptr;
        const HintConfigurationSettingsRecord* hintConfiguration = nullptr;
        const MultiplayerSettingsRecord* multiplayerSettings = nullptr;
        const MovementAbilitySettingsRecord* movementAbilities = nullptr;
        const KingdomTargetSettingsRecord* kingdomTargets = nullptr;
        const WaveCSettingsRecord* waveCSettings = nullptr;
        const AdvancedWaveBSettingsRecord* advancedWaveBSettings = nullptr;
        const AdvancedWaveDSettingsRecord* advancedWaveDSettings = nullptr;
        if (header.version >= cLogicDirectedStartFormatVersion) {
            if (!takeBytes(cursor, end, &ptr,
                    sizeof(s16) * LD::sPaintingLinkCount))
                return false;
            cloudPaintingLinkOrder = static_cast<const s16*>(ptr);
            if (!takeBytes(cursor, end, &ptr, sizeof(LogicDirectedStartRecord)))
                return false;
            logicStarts = static_cast<const LogicDirectedStartRecord*>(ptr);
            if (header.version >= cNuzlockeStartFormatVersion) {
                if (!takeBytes(cursor, end, &ptr, sizeof(NuzlockeStartRecord)))
                    return false;
                nuzlockeStart = static_cast<const NuzlockeStartRecord*>(ptr);
            }
            if (header.version >= cGameModeSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr, sizeof(GameModeSettingsRecord)))
                    return false;
                gameModeSettings = static_cast<const GameModeSettingsRecord*>(ptr);
            }
            if (header.version >= cMoonAllocationSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(MoonAllocationSettingsRecord)))
                    return false;
                moonAllocationSettings = static_cast<const MoonAllocationSettingsRecord*>(ptr);
            }
            if (header.version >= cTimerPenaltySettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(TimerPenaltySettingsRecord)))
                    return false;
                timerPenaltySettings = static_cast<const TimerPenaltySettingsRecord*>(ptr);
            }
            if (header.version >= cHintConfigurationSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(HintConfigurationSettingsRecord)))
                    return false;
                hintConfiguration = static_cast<const HintConfigurationSettingsRecord*>(ptr);
            }
            if (header.version >= cMultiplayerSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(MultiplayerSettingsRecord)))
                    return false;
                multiplayerSettings = static_cast<const MultiplayerSettingsRecord*>(ptr);
            }
            if (header.version >= cMovementAbilitySettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(MovementAbilitySettingsRecord)))
                    return false;
                movementAbilities = static_cast<const MovementAbilitySettingsRecord*>(ptr);
            }
            if (header.version >= cKingdomTargetSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(KingdomTargetSettingsRecord)))
                    return false;
                kingdomTargets = static_cast<const KingdomTargetSettingsRecord*>(ptr);
            }
            if (header.version >= cWaveCSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr, sizeof(WaveCSettingsRecord)))
                    return false;
                waveCSettings = static_cast<const WaveCSettingsRecord*>(ptr);
            }
            if (header.version >= cAdvancedWaveBSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(AdvancedWaveBSettingsRecord)))
                    return false;
                advancedWaveBSettings = static_cast<const AdvancedWaveBSettingsRecord*>(ptr);
            }
            if (header.version >= cAdvancedWaveDSettingsFormatVersion) {
                if (!takeBytes(cursor, end, &ptr,
                        sizeof(AdvancedWaveDSettingsRecord)))
                    return false;
                advancedWaveDSettings = static_cast<const AdvancedWaveDSettingsRecord*>(ptr);
            }
        }

        if (!takeBytes(cursor, end, &ptr, sizeof(ConnRecord) * savedEntranceLinkCount))
            return false;
        const ConnRecord* pipeRecords = static_cast<const ConnRecord*>(ptr);

        const ConnRecord* homeOutRecords = nullptr;
        const ConnRecord* subNearRecords = nullptr;
        const ConnRecord* subFarRecords = nullptr;
        s32 loadedAutoGrantItems[MoonRando::kMaxAutoGrants] = {};
        s32 loadedAutoGrantCount = 0;
        bool loadedAutoGrantNotified = false;
        if (header.version >= cConnectionFormatVersion) {
            if (!takeBytes(cursor, end, &ptr, sizeof(ConnRecord) * savedEntranceLinkCount))
                return false;
            homeOutRecords = static_cast<const ConnRecord*>(ptr);
        }
        if (!takeBytes(cursor, end, &ptr, sizeof(ConnRecord) * savedEntranceLinkCount))
            return false;
        subNearRecords = static_cast<const ConnRecord*>(ptr);

        if (!takeBytes(cursor, end, &ptr, sizeof(ConnRecord) * savedEntranceLinkCount))
            return false;
        subFarRecords = static_cast<const ConnRecord*>(ptr);

        const KingdomMoonUseRecord* kingdomMoonUse = nullptr;
        if (header.version >= cKingdomMoonUseFormatVersion) {
            if (!takeBytes(cursor, end, &ptr, sizeof(KingdomMoonUseRecord)))
                return false;
            kingdomMoonUse = static_cast<const KingdomMoonUseRecord*>(ptr);
        }

        const RequirementBaseRecord* requirementBases = nullptr;
        if (header.version >= cRequirementBaseFormatVersion) {
            if (!takeBytes(cursor, end, &ptr, sizeof(RequirementBaseRecord)))
                return false;
            requirementBases = static_cast<const RequirementBaseRecord*>(ptr);
        }

        const RequirementDeviationRecord* requirementDeviations = nullptr;
        if (header.version >= cRequirementDeviationFormatVersion) {
            if (!takeBytes(cursor, end, &ptr, sizeof(RequirementDeviationRecord)))
                return false;
            requirementDeviations = static_cast<const RequirementDeviationRecord*>(ptr);
        }

        if (header.version >= cAutoGrantFormatVersion) {
            if (!takeBytes(cursor, end, &ptr, sizeof(AutoGrantBlock)))
                return false;
            const AutoGrantBlock* autoGrantBlock = static_cast<const AutoGrantBlock*>(ptr);
            if (autoGrantBlock->count > MoonRando::kMaxAutoGrants
                || autoGrantBlock->notified > 1)
                return false;
            loadedAutoGrantCount = autoGrantBlock->count;
            loadedAutoGrantNotified = autoGrantBlock->notified != 0;
            for (s32 i = 0; i < loadedAutoGrantCount; i++) {
                if (autoGrantBlock->grants[i].itemKind != cAutoGrantMoonItemKind
                    || autoGrantBlock->grants[i].itemId >= LD::sMoonCount)
                    return false;
                const s32 item = autoGrantBlock->grants[i].itemId;
                for (s32 previous = 0; previous < i; previous++) {
                    if (loadedAutoGrantItems[previous] == item)
                        return false;
                }
                loadedAutoGrantItems[i] = item;
            }
        }
        // The host uploads its rando.bin after its own native save has been
        // initialized, so the shared seed may already carry notified=1. A
        // multiplayer guest has a different, blank native save and must run
        // the same one-time grants locally; notification state is per save,
        // not part of the shared seed identity.
        if (category == SaveRedirect::Category::MultiplayerGuest)
            loadedAutoGrantNotified = false;

        Randomizer::setSeed(header.seed);
        Randomizer::setDeathLinkEnabled(multiplayerSettings
            && multiplayerSettings->version >= 1
            && (multiplayerSettings->flags & cDeathLinkBit) != 0);
        Randomizer::setSyncOtherTeamsEnabled(multiplayerSettings
            && multiplayerSettings->version >= 2
            && (multiplayerSettings->flags & cSyncOtherTeamsBit) != 0);
        Randomizer::applyMovementAbilitySettings(
            movementAbilities ? movementAbilities->packed : 0u);
        Randomizer::setRunMode(getRunMode(header, &meta));
        applySettingsFlags(header.settingsFlags);
        applyPropChaosSettings(header);
        applyChaosSwapModes(header);
        Randomizer::setStreamerModeEnabled(hasStreamerModeSetting(header)
            && (header.bowserWinCondition & cStreamerModeBit) != 0);
        Randomizer::setWorldPeaceRequirementMode(readWorldPeaceMode(header));
        // Mode first: it re-settles the host, so the stored host has to land on
        // top of it rather than under it.
        Randomizer::setSubareaUnlockMode(readSubareaUnlockMode(header));
        Randomizer::setSubareaUnlockHost(readSubareaUnlockHost(header));
        Randomizer::setCoinRandoEnabled(hasCoinRandoSetting(header)
            && (header.reserved[Reserved_WorldPeaceMode] & cCoinRandoBit) != 0);
        Randomizer::setShopPriceRandoEnabled(hasShopPricingSettings(header)
            && (header.reserved[Reserved_EntranceMode] & cShopPriceRandoBit) != 0);
        Randomizer::setProportionalKingdomPricingEnabled(hasShopPricingSettings(header)
            && (header.reserved[Reserved_EntranceMode] & cProportionalKingdomPricingBit) != 0);
        Randomizer::setMusicRandoMode(readMusicMode(header));
        Randomizer::setMusic2DThemesDisabled(hasMusicFilterSettings(header)
            && (header.reserved[Reserved_MusicMode] & cMusicDisable2DBit) != 0);
        Randomizer::setMusicExtraThemesDisabled(hasMusicFilterSettings(header)
            && (header.reserved[Reserved_MusicMode] & cMusicDisableExtraBit) != 0);
        Randomizer::setMusicBossThemesDisabled(hasMusicFilterSettings(header)
            && (header.reserved[Reserved_MusicMode] & cMusicDisableBossBit) != 0);
        Randomizer::setMusicSourceMode(readMusicSourceMode(header));
        applyPurpleShopSettings(header);
        const bool hasSpecialMoonFormat = (header.reserved[Reserved_SpecialMoonFormat]
                                              & static_cast<u8>(~cStartKingdomStateBit))
            == cSpecialMoonFormat;
        Randomizer::setStartKingdomStateRandoEnabled(
            hasSpecialMoonFormat
            && (header.reserved[Reserved_SpecialMoonFormat] & cStartKingdomStateBit) != 0);
        Randomizer::setChestOrderRandoEnabled(
            hasChestOrderSetting(header)
            && (header.reserved[Reserved_StartingEquipment] & cChestOrderBit) != 0);
        Randomizer::setStartPositionMode(
            hasStartPositionSetting(header)
                ? static_cast<Randomizer::StartPositionMode>(
                      (header.reserved[Reserved_StartingEquipment] & cStartPositionMask)
                      >> cStartPositionShift)
                : Randomizer::StartPositionMode_Vanilla);
        // Avoid inheriting a previous slot's configuration when loading an old
        // sidecar without the special-moon fields. Such a sidecar predates
        // Hint Tokens, so its historical Cap reward is Coins.
        Randomizer::setCapKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
        Randomizer::setCloudKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
        Randomizer::setMoonKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
        if (hasSpecialMoonFormat) {
            Randomizer::setCapKingdomMoonUse(static_cast<Randomizer::SpecialMoonUse>(
                header.reserved[Reserved_CapMoonUse]));
            Randomizer::setCloudKingdomMoonUse(static_cast<Randomizer::SpecialMoonUse>(
                header.reserved[Reserved_CloudMoonUse]));
            Randomizer::setMoonKingdomMoonUse(static_cast<Randomizer::SpecialMoonUse>(
                header.reserved[Reserved_MoonMoonUse]));
        } else if ((header.reserved[Reserved_CaptureAbilityDifficulty]
                       & cCaptureAbilityDifficultyMask)
            == Randomizer::CaptureAbilityDifficulty_Plentiful) {
            // Old Plentiful used otherwise-unused Cap/Cloud/Moon items for duplicate
            // captures and abilities. Preserve that intent unless one of the old
            // dedicated toggles already claimed Cloud or Moon.
            Randomizer::setCapKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
            if (!Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
                Randomizer::setCloudKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
            if (!Randomizer::isMoonKingdomRequirementsEnabled())
                Randomizer::setMoonKingdomMoonUse(Randomizer::SpecialMoonUse_AbilitiesOrCaptures);
        }
        // Saves before the game-mode-settings record have no Extreme bit. Set
        // the default explicitly so loading one after a newer save cannot
        // leak the previous run's setting.
        Randomizer::setExtremeNuzlockeEnabled(false);
        if (gameModeSettings) {
            Randomizer::setTimerStartIdx(gameModeSettings->timerStartIdx);
            Randomizer::setTimerMoonBonusIdx(gameModeSettings->timerMoonBonusIdx);
            Randomizer::setTimerTripleBonusIdx(gameModeSettings->timerTripleBonusIdx);
            Randomizer::setExtremeNuzlockeEnabled(
                (gameModeSettings->softNuzlockeLoss & cExtremeNuzlockeBit) != 0);
            Randomizer::setSoftNuzlockeLoss(
                static_cast<Randomizer::SoftNuzlockeLoss>(
                    gameModeSettings->softNuzlockeLoss & cSoftNuzlockeLossMask));
            // A zero nibble is a save from before the dial existed; leave the
            // default standing rather than reading it as "lose nothing".
            const u8 abilityNibble = gameModeSettings->softNuzlockeEquipLoss & 0x0f;
            const u8 captureNibble = (gameModeSettings->softNuzlockeEquipLoss >> 4) & 0x0f;
            if (abilityNibble)
                Randomizer::setSoftNuzlockeAbilityLoss(
                    static_cast<Randomizer::SoftNuzlockeEquipLoss>(abilityNibble - 1));
            if (captureNibble)
                Randomizer::setSoftNuzlockeCaptureLoss(
                    static_cast<Randomizer::SoftNuzlockeEquipLoss>(captureNibble - 1));
            // Available first: the setters keep required <= available by pushing
            // the other value, and the pool is the one that bounds the goal.
            Randomizer::setCustomToadetteMedalAvailable(gameModeSettings->customMedalAvailable);
            Randomizer::setCustomToadetteMedalRequired(gameModeSettings->customMedalRequired);
            Randomizer::setCustomToadetteMedalCountsEnabled(
                gameModeSettings->customMedalCounts != 0);
        }
        // Saves from before the penalty record carry no dials at all. Set the
        // "Off" defaults explicitly rather than leaving them where a previously
        // loaded run put them, exactly as the Extreme bit above does.
        Randomizer::setTimerDamagePenaltyIdx(0);
        Randomizer::setTimerDeathPenaltyIdx(0);
        Randomizer::setTimerHalfLifeIdx(0);
        if (timerPenaltySettings) {
            Randomizer::setTimerDamagePenaltyIdx(timerPenaltySettings->damagePenaltyIdx);
            Randomizer::setTimerDeathPenaltyIdx(timerPenaltySettings->deathPenaltyIdx);
            // Zero on every save written before the decay dial existed, which is
            // the "Disabled" those runs were generated with.
            Randomizer::setTimerHalfLifeIdx(timerPenaltySettings->halfLifeIdx);
        }
        if (moonAllocationSettings) {
            Randomizer::setMoonReallocationEnabled(
                (moonAllocationSettings->flags & cMoonAllocationMasterBit) != 0);
            Randomizer::setRandomizeMoonAllocationEnabled(
                (moonAllocationSettings->flags & cMoonAllocationRandomizeBit) != 0);
            Randomizer::setMoonAllocationDifficulty(
                static_cast<Randomizer::MoonAllocationDifficulty>(
                    moonAllocationSettings->difficulty));
            Randomizer::setMoonCoinPolicy(
                static_cast<Randomizer::MoonCoinPolicy>(
                    moonAllocationSettings->coinPolicy));
            // The reserved byte is intentionally ignored for forward
            // compatibility.
        }
        if (kingdomTargets) {
            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
                Randomizer::setKingdomMoonTarget(
                    worldId, kingdomTargets->moonTargets[worldId]);
                Randomizer::setKingdomTripleMoonTarget(
                    worldId, kingdomTargets->tripleMoonTargets[worldId]);
            }
            if (Randomizer::isKingdomMoonTargetCustom()) {
                Randomizer::setMoonReallocationEnabled(true);
                Randomizer::setRandomizeMoonAllocationEnabled(true);
            }
            if (Randomizer::isKingdomTripleMoonTargetCustom())
                Randomizer::setRandomizeTripleMoonsEnabled(true);
        }
        if (kingdomMoonUse) {
            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
                Randomizer::setKingdomMoonUseMask(worldId, kingdomMoonUse->masks[worldId]);
        }
        // A slot saved before this record existed has no authored bases, so the
        // distribution decides them - which is what it did on that slot anyway.
        RequirementRando::clearRequirementBases();
        if (requirementBases) {
            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
                RequirementRando::setAuthoredRequirementBaseRaw(
                    worldId, requirementBases->bases[worldId]);
        }
        RequirementRando::clearRequirementDeviations();
        if (requirementDeviations) {
            for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
                RequirementRando::setAuthoredRequirementDeviationRaw(
                    worldId, requirementDeviations->deviations[worldId]);
        }
        if (waveCSettings) {
            if (!kingdomMoonUse) {
                Randomizer::setCapKingdomMoonUseMask(waveCSettings->capMoonUseMask);
                Randomizer::setCloudKingdomMoonUseMask(waveCSettings->cloudMoonUseMask);
                Randomizer::setMoonKingdomMoonUseMask(waveCSettings->moonMoonUseMask);
            }
            Randomizer::setMoonRockKeyScope(static_cast<Randomizer::KeyScope>(
                waveCSettings->moonRockKeyScope));
            Randomizer::setSubareaKeyScope(static_cast<Randomizer::KeyScope>(
                waveCSettings->subareaKeyScope));
            Randomizer::setVisitedKingdomsMapMode(
                static_cast<Randomizer::VisitedKingdomsMapMode>(
                    waveCSettings->visitedKingdomsMapMode));
            Randomizer::setStartKingdomsMask(waveCSettings->startKingdomsMask);
            s16 dev = static_cast<s16>(
                static_cast<u8>(waveCSettings->moonRequirementDeviations[0]) | (static_cast<u8>(waveCSettings->moonRequirementDeviations[1]) << 8));

            // Check if it's an old save (sum up the values if there are multiple non-zero, or if they are reasonable small numbers)
            // Wait, actually, if it's an old save, the sum of array might be better.
            // But let's just use the new format. If it's old, it will be garbled and they can just change it.
            // Actually, if it's an old save, they probably want the sum.
            s32 sum = 0;
            bool isOld = false;
            for (s32 i = 2; i < WorldMeta::sWorldCount; i++) {
                if (waveCSettings->moonRequirementDeviations[i] != 0)
                    isOld = true;
            }
            if (isOld) {
                for (s32 i = 0; i < WorldMeta::sWorldCount; i++)
                    sum += waveCSettings->moonRequirementDeviations[i];
                dev = static_cast<s16>(sum);
            }

            Randomizer::setMoonRequirementDeviation(dev);
        }
        Randomizer::setSkyboxRandoMode(readSkyboxMode(header));
        // Reserved byte stored as value+1; 0 = legacy save -> default on.
        Randomizer::setStabilizeShadersEnabled(
            (header.reserved[Reserved_StabilizeShaders] & cStabilizeShadersMask) != 1);
        Randomizer::setRandomizeTripleMoonsEnabled(
            (header.reserved[Reserved_StabilizeShaders] & cRandomizeTripleMoonsBit) != 0
            || Randomizer::isKingdomTripleMoonTargetCustom());
        // applySettingsFlags() above already resolved the flag bit to
        // Vanilla/Visited; only the promotion to Unlocked lives here.
        if (Randomizer::isVisitedKingdomsMapEnabled()
            && (header.reserved[Reserved_StabilizeShaders] & cVisitedKingdomsUnlockedBit) != 0) {
            Randomizer::setVisitedKingdomsMapMode(
                Randomizer::VisitedKingdomsMapMode_Unlocked);
        }
        if (header.settingsFlags & SettingsFlag_ExtendedOptions) {
            Randomizer::setEntranceRandoMode(static_cast<Randomizer::EntranceRandoMode>(
                header.reserved[Reserved_EntranceMode] & cEntranceModeMask));
            Randomizer::setShopLocationMode(readShopLocationMode(header));
            Randomizer::setStartingEquipment(static_cast<Randomizer::StartingEquipment>(
                header.reserved[Reserved_StartingEquipment] & cStartingEquipmentMask));
            Randomizer::setCaptureAbilityDifficulty(
                static_cast<Randomizer::CaptureAbilityDifficulty>(
                    header.reserved[Reserved_CaptureAbilityDifficulty]
                    & cCaptureAbilityDifficultyMask));
        } else {
            Randomizer::setEntranceRandoEnabled((header.settingsFlags & SettingsFlag_EntranceRando) != 0);
            Randomizer::setShopLocationMode(Randomizer::ShopLocationMode_Randomized);
            Randomizer::setStartingEquipment(Randomizer::StartingEquipment_Normal);
            Randomizer::setCaptureAbilityDifficulty(Randomizer::CaptureAbilityDifficulty_Normal);
        }
        if (advancedWaveBSettings) {
            Randomizer::setCaptureLockPoolMask(0, advancedWaveBSettings->capturePoolLo);
            Randomizer::setCaptureLockPoolMask(1, advancedWaveBSettings->capturePoolHi);
            Randomizer::setAbilityLockPoolMask(advancedWaveBSettings->abilityPool);
            // 0 is a slot written before this setting existed; those seeds were
            // all progressive, which is also the default.
            if (advancedWaveBSettings->reserved[0] != 0) {
                Randomizer::setProgressiveAbilitiesEnabled(
                    advancedWaveBSettings->reserved[0] != 1);
            }
            Randomizer::setStartingEquipmentOverrides(
                advancedWaveBSettings->startingCaptures,
                advancedWaveBSettings->startingThrows,
                advancedWaveBSettings->startingAbilities);
            if (advancedWaveBSettings->purpleShopCustomPrice > 0) {
                Randomizer::setPurpleShopCustomPrice(
                    advancedWaveBSettings->purpleShopCustomPrice);
                Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Custom);
            } else {
                Randomizer::setPurpleShopCustomPrice(50);
                if (Randomizer::isPurpleShopPriceCustom())
                    Randomizer::setPurpleShopPrice(
                        Randomizer::PurpleShopPrice::Purple50);
            }
        } else {
            // Pre-Wave-B sidecars had no opinion on the item subsets or custom
            // numeric values. Defaults are all items in the pool and the
            // existing preset quantities/price.
            Randomizer::resetLockPools();
            Randomizer::setStartingEquipmentOverrides(-1, -1, -1);
            Randomizer::setPurpleShopCustomPrice(50);
            if (Randomizer::isPurpleShopPriceCustom())
                Randomizer::setPurpleShopPrice(
                    Randomizer::PurpleShopPrice::Purple50);
        }
        if (hasSpecialMoonFormat) {
            const u32 total = header.reserved[Reserved_MoonTotalLo]
                | (static_cast<u32>(header.reserved[Reserved_MoonTotalHi]) << 8);
            Randomizer::setMoonRequirement(total);
        } else {
            Randomizer::setMoonRequirementLevel(static_cast<Randomizer::MoonRequirement>(header.moonRequirement));
        }
        Randomizer::setMoonDifficulty(static_cast<Randomizer::MoonDifficulty>(header.moonDifficulty));
        Randomizer::setLogicDifficulty(static_cast<Randomizer::LogicDifficulty>(header.logicDifficulty));
        // Reserved_SeedStyleLo/Hi holds value+1; 0 means the field was never
        // written. setSeedAxesPacked() reads the tag bit and falls back to the
        // legacy SeedStyle -> three-axis migration on its own, so a header from
        // before cSeedAxisSettingsFormat needs no special case here beyond the
        // Prioritize Difficult Movement bit below.
        const u16 storedSeedAxes = header.reserved[Reserved_SeedStyleLo]
            | (static_cast<u16>(header.reserved[Reserved_SeedStyleHi]) << 8);
        if (storedSeedAxes != 0)
            Randomizer::setSeedAxesPacked(storedSeedAxes - 1);
        else
            Randomizer::applyLegacySeedStyle(Randomizer::SeedStyle_Standard);
        // Runs after the axes are settled: the two retired chain-length
        // Loading Zones modes carry a Seed Openness meaning, and that is a more
        // specific statement of intent than whatever the Seed Style implied.
        // A no-op for every mode that is still selectable.
        Randomizer::applyLegacyEntranceRandoMode(Randomizer::getEntranceRandoMode());
        if (!hasSeedAxisSettings(header)) {
            // Pre-axis slot: the retired checkbox is the only other input to
            // Seed Difficulty, and it means Hard. Runs after the style
            // migration above so it can promote whatever that produced.
            Randomizer::applyLegacyPrioritizeDifficultMovement(
                (header.reserved[Reserved_StabilizeShaders] & cPrioritizeDifficultMovementBit) != 0);
        }
        // Version 10 supplies both banks. Keep old slots deterministic by
        // projecting their tier into the coin bank and resetting the newly
        // introduced token bank to its original all-one defaults.
        if (hintConfiguration) {
            Randomizer::setHintCoinPriceProfile(
                Randomizer::unpackHintCoinPriceProfile(hintConfiguration->coinProfile));
            Randomizer::setHintTokenPriceProfile(
                Randomizer::unpackHintTokenPriceProfile(hintConfiguration->tokenProfile));
        } else {
            Randomizer::setHintPriceTier(
                static_cast<Randomizer::HintPriceTier>(header.hintPriceTier & cHintPriceTierMask));
            Randomizer::HintPriceProfile tokenDefaults = { {
                                                               static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                               static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                               static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                               static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                               static_cast<u8>(Randomizer::HintTokenPrice::Disabled),
                                                           },
                static_cast<u8>(Randomizer::HintTokenProgressiveIncrement::Disabled), false };
            Randomizer::setHintTokenPriceProfile(tokenDefaults);
        }
        Randomizer::setHintMode(readHintMode(header));
        if (advancedWaveDSettings) {
            const u8 timerMask = advancedWaveDSettings->timerCustomMask;
            if (timerMask & (1u << 0))
                Randomizer::setTimerStartCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[0]);
            if (timerMask & (1u << 1))
                Randomizer::setTimerMoonBonusCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[1]);
            if (timerMask & (1u << 2))
                Randomizer::setTimerTripleBonusCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[2]);
            if (timerMask & (1u << 3))
                Randomizer::setTimerDamagePenaltyCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[3]);
            if (timerMask & (1u << 4))
                Randomizer::setTimerDeathPenaltyCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[4]);
            if (timerMask & (1u << 5))
                Randomizer::setTimerHalfLifeCustomSeconds(
                    advancedWaveDSettings->timerCustomSeconds[5]);

            const u8 nuzlockeMask = advancedWaveDSettings->nuzlockeCustomMask;
            if (nuzlockeMask & (1u << 0))
                Randomizer::setSoftNuzlockeLossCustomPercent(
                    advancedWaveDSettings->nuzlockeCustomPercent[0]);
            if (nuzlockeMask & (1u << 1))
                Randomizer::setSoftNuzlockeAbilityLossCustomPercent(
                    advancedWaveDSettings->nuzlockeCustomPercent[1]);
            if (nuzlockeMask & (1u << 2))
                Randomizer::setSoftNuzlockeCaptureLossCustomPercent(
                    advancedWaveDSettings->nuzlockeCustomPercent[2]);

            for (u8 category = 0;
                 category < Randomizer::kHintPriceCategoryCount; category++) {
                const Randomizer::HintPriceCategory hintCategory = static_cast<Randomizer::HintPriceCategory>(category);
                const u8 bit = static_cast<u8>(1u << category);
                if (advancedWaveDSettings->hintCoinOverrideMask & bit)
                    Randomizer::setHintCoinOverride(hintCategory, true);
                if ((advancedWaveDSettings->hintCustomMask
                        | advancedWaveDSettings->hintCoinOverrideMask)
                    & bit) {
                    const u16 price = advancedWaveDSettings->hintCustomPrice[category];
                    Randomizer::setHintCustomPrice(hintCategory, price > 0 ? price : 1);
                }
            }

            Randomizer::setMusicThemeMask(advancedWaveDSettings->musicThemeMask);
            const s32 low = advancedWaveDSettings->easyMinigameLow > 0
                ? advancedWaveDSettings->easyMinigameLow
                : Randomizer::kEasyMinigameDefaultLow;
            const s32 high = advancedWaveDSettings->easyMinigameHigh > 0
                ? advancedWaveDSettings->easyMinigameHigh
                : Randomizer::kEasyMinigameDefaultHigh;
            Randomizer::setEasyMinigameLow(low);
            Randomizer::setEasyMinigameHigh(high);
            Randomizer::setSheepHerdingCount(
                advancedWaveDSettings->sheepHerdingCount);
            // setSheepHerdingCount intentionally enables the replacement UI;
            // the global QoL setting remains authoritative for whether the
            // feature itself is on. This also prevents an older sidecar's
            // legacy settings bit from changing the current global setting.
            Randomizer::setOneSheepHerdingEnabled(sheepHerdingEnabled);
        }
        applyBowserSettings(header);
        Randomizer::setVisitedKingdoms(meta.visitedKingdoms);
        Randomizer::setForcedPeaceKingdoms(getMetaForcedPeaceKingdoms(meta));
        Randomizer::resetKingdomVisitOrder();
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            if ((meta.visitedKingdoms & (1u << worldId)) == 0)
                continue;
            const u8 order = getMetaVisitOrder(meta, worldId);
            Randomizer::setKingdomVisitOrder(worldId, order == cVisitOrderUnranked ? -1 : static_cast<s8>(order));
        }
        if (!Randomizer::isEntranceRandoEnabled())
            EntranceRando::reset();
        if (loadedAutoGrantCount > 0 && !Randomizer::isMoonRandoEnabled())
            return false;
        if (!Randomizer::isMoonRandoEnabled())
            MoonRando::clear();

        AbilityRando::clearLogicStartingOverride();
        CaptureRando::clearLogicStartingOverride();
        if (logicStarts) {
            if (logicStarts->abilityCount > 2 || logicStarts->captureCount > 4)
                return false;

            AbilityRando::AbilityId abilityOverrides[2] = {};
            for (s32 i = 0; i < logicStarts->abilityCount; i++)
                abilityOverrides[i] = static_cast<AbilityRando::AbilityId>(
                    logicStarts->abilities[i]);
            if (!AbilityRando::setLogicStartingOverride(
                    abilityOverrides, logicStarts->abilityCount)
                || !CaptureRando::setLogicStartingOverride(
                    logicStarts->captures, logicStarts->captureCount))
                return false;
        }
        if (nuzlockeStart && nuzlockeStart->capThrow >= 0)
            AbilityRando::setLogicStartingCapThrowOverride(
                static_cast<AbilityRando::AbilityId>(nuzlockeStart->capThrow));

        if (!Randomizer::reinitDeterministic(header.entranceAttempt))
            return false;
        // Needs the rebuilt kingdom route (for the starting kingdom), and must
        // run after the saved visit record above so it can only ever add to it.
        // Idempotent, so re-applying it on every load also repairs a slot whose
        // meta block predates the Unlocked mode.
        Randomizer::applyUnlockedKingdomsMapStart();

        if (Randomizer::isEntranceRandoEnabled()) {
            for (s32 i = 0; i < savedEntranceLinkCount; i++) {
                sPipeConn[i] = readConnRecord(pipeRecords[i]);
                sHomeOutConn[i] = homeOutRecords ? readConnRecord(homeOutRecords[i]) : sPipeConn[i];
                sSubNearConn[i] = readConnRecord(subNearRecords[i]);
                sSubFarConn[i] = readConnRecord(subFarRecords[i]);
            }
            EntranceRando::restore(sPipeConn, sHomeOutConn, sSubNearConn, sSubFarConn, savedEntranceLinkCount, header.seed, header.entranceAttempt, header.logicDifficulty);
        }

        // Re-derived rather than stored: the walk-back proof needs the restored
        // entrance shuffle and the restored starting-equipment overrides, both of
        // which are in place by now. Same (seed, attempt) reproduces the same node.
        StartPositionRando::rebuild(header.seed, header.entranceAttempt);

        if (Randomizer::isMoonRandoEnabled()
            && !MoonRando::restorePlacements(itemForLocation, LD::sMoonCount,
                placements, placementCount, header.seed, header.entranceAttempt,
                header.logicSucceeded != 0, cloudPaintingLinkOrder,
                cloudPaintingLinkOrder ? LD::sPaintingLinkCount : 0,
                loadedAutoGrantItems, loadedAutoGrantCount))
            return false;

        sAutoGrantCount = loadedAutoGrantCount;
        for (s32 i = 0; i < loadedAutoGrantCount; i++)
            sAutoGrantItems[i] = loadedAutoGrantItems[i];
        sAutoGrantNotified = loadedAutoGrantNotified;
        sAutoGrantNotificationDirty = false;
        sAutoGrantSeed = header.seed;
        sAutoGrantSeedValid = true;

        if (Randomizer::isMoonRandoEnabled() && MoonRando::isInitialized())
            SpoilerLog::generate();

        return true;
    }

    bool updateMeta(s32 slot, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category) || !isCompatible(header))
            return false;
        fillMeta(&meta);
        return writeMetaAt(slot, meta, category);
    }

    bool updateMeta(s32 slot, GameDataHolderAccessor accessor, SaveRedirect::Category category) {
        return updateMeta(slot, accessor, true, category);
    }

    bool updateMeta(
        s32 slot, GameDataHolderAccessor accessor, bool verbose, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeaderImpl(slot, &header, &meta, verbose, category) || !isCompatible(header))
            return false;
        fillMeta(&meta, accessor);
        if (verbose) {
            Randomizer::logPush("rando meta update slot=%d moons=%d world=%d",
                slot, meta.totalMoons, meta.currentWorldId);
        }
        return writeMetaAt(slot, meta, category, verbose);
    }

    bool updateMeta(s32 slot, const GameDataFile* file, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category) || !isCompatible(header))
            return false;
        fillMeta(&meta, file);
        Randomizer::logPush("rando meta update slot=%d moons=%d world=%d (file)",
            slot, meta.totalMoons, meta.currentWorldId);
        return writeMetaAt(slot, meta, category);
    }

    bool updateSettingsFlags(s32 slot, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category) || !isCompatible(header))
            return false;
        fillHeaderSettings(&header);
        return writeHeaderAt(slot, header, category);
    }

    bool updateHintConfiguration(s32 slot, SaveRedirect::Category category) {
        Header header = {};
        Meta meta = {};
        if (!readHeader(slot, &header, &meta, category) || !isCompatible(header))
            return false;
        if (header.version != cHintConfigurationSettingsFormatVersion) {
            // An older payload has no gap where an in-place profile record can
            // be written. The normal full-sidecar writer is the safe upgrade
            // path once a loaded run is active; it retains every restored
            // placement/connection record and emits the current full-sidecar
            // format through the established save path.
            return write(slot, category);
        }

        const size_t payloadSize = header.fileSize >= sizeof(Header) + sizeof(Meta)
            ? header.fileSize - sizeof(Header) - sizeof(Meta)
            : 0;
        if (payloadSize == 0 || payloadSize > cMaxPayloadSize)
            return false;

        const auto path = makePath(slot, category);
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false;
        const bool readOk = nn::fs::ReadFile(handle, sizeof(Header) + sizeof(Meta),
            sPayload, payloadSize)
                                .IsSuccess();
        nn::fs::CloseFile(handle);
        if (!readOk || calcCrc32(sPayload, payloadSize) != header.payloadCrc32)
            return false;

        const u8* cursor = sPayload;
        const u8* end = sPayload + payloadSize;
        const void* ptr = nullptr;
        if (!takeBytes(cursor, end, &ptr, sizeof(s32)))
            return false;
        const s32 placementCount = *static_cast<const s32*>(ptr);
        if (expectedPayloadSize(placementCount, header.entranceLinkCount, header.version)
            != payloadSize)
            return false;
        if (!takeBytes(cursor, end, &ptr, sizeof(s32) * LD::sMoonCount)
            || !takeBytes(cursor, end, &ptr,
                sizeof(MoonRando::SerializedPlacement) * placementCount)
            || !takeBytes(cursor, end, &ptr, sizeof(s16) * LD::sPaintingLinkCount)
            || !takeBytes(cursor, end, &ptr, sizeof(LogicDirectedStartRecord))
            || !takeBytes(cursor, end, &ptr, sizeof(NuzlockeStartRecord))
            || !takeBytes(cursor, end, &ptr, sizeof(GameModeSettingsRecord))
            || !takeBytes(cursor, end, &ptr, sizeof(MoonAllocationSettingsRecord))
            || !takeBytes(cursor, end, &ptr, sizeof(TimerPenaltySettingsRecord))) {
            return false;
        }
        const size_t recordOffset = static_cast<size_t>(cursor - sPayload);
        if (recordOffset + sizeof(HintConfigurationSettingsRecord) > payloadSize)
            return false;

        const HintConfigurationSettingsRecord newRecord = {
            Randomizer::packHintPriceProfile(Randomizer::getHintCoinPriceProfile()),
            Randomizer::packHintPriceProfile(Randomizer::getHintTokenPriceProfile()),
        };
        std::memcpy(sPayload + recordOffset, &newRecord, sizeof(newRecord));
        fillHintConfigurationHeader(&header);
        header.payloadCrc32 = calcCrc32(sPayload, payloadSize);

        const bool ok = replaceSidecarTransaction(
            slot, category, header, meta, sPayload, payloadSize);
        Randomizer::logPush("SIDECAR hint-config slot=%d ok=%d", slot, ok);
        return ok;
    }

    Randomizer::RunMode getRunMode(const Header&, const Meta* meta) {
        if (!Randomizer::isGameModesFeatureEnabled())
            return Randomizer::RunMode::Standard;
        if (!meta)
            return Randomizer::RunMode::Standard;
        const u8 encoded = static_cast<u8>(((meta->reserved[2] >> 6) & 0x03)
            | ((meta->reserved[2] & 0x02) << 1));
        if (encoded == static_cast<u8>(Randomizer::RunMode::ToadetteMedals))
            return Randomizer::RunMode::ToadetteMedals;
        if (encoded == static_cast<u8>(Randomizer::RunMode::Bingo))
            return Randomizer::RunMode::Bingo;
        if (encoded == static_cast<u8>(Randomizer::RunMode::Ascend))
            return Randomizer::RunMode::Ascend;
        if (Randomizer::isNuzlockeModesFeatureEnabled()) {
            if (encoded == static_cast<u8>(Randomizer::RunMode::Nuzlocke))
                return Randomizer::RunMode::Nuzlocke;
            if (encoded == static_cast<u8>(Randomizer::RunMode::SoftNuzlocke))
                return Randomizer::RunMode::SoftNuzlocke;
        }
        if (Randomizer::isTimerModeFeatureEnabled()
            && encoded == static_cast<u8>(Randomizer::RunMode::Timer))
            return Randomizer::RunMode::Timer;
        return Randomizer::RunMode::Standard;
    }
} // namespace RandoSaveFile
