#include "Randomizer/Presets.h"

#include "Randomizer/Randomizer.h"

namespace Presets {
    namespace {
        // Settings shared by every preset; individual presets override what differs.
        void applyCommon() {
            Randomizer::setMoonRandoEnabled(true);
            Randomizer::setMoonReallocationEnabled(false);
            Randomizer::setRandomizeMoonAllocationEnabled(false);
            Randomizer::setMoonAllocationDifficulty(
                Randomizer::MoonAllocationDifficulty::Vanilla);
            Randomizer::setMoonCoinPolicy(Randomizer::MoonCoinPolicy::Vanilla);
            Randomizer::clearKingdomMoonTargets();
            Randomizer::clearKingdomTripleMoonTargets();
            Randomizer::setTripleMoonsAnywhereEnabled(false);
            Randomizer::setRandomizeTripleMoonsEnabled(false);
            Randomizer::setEntranceRandoMode(Randomizer::EntranceRandoMode_Standard);
            Randomizer::setShopLocationMode(Randomizer::ShopLocationMode_Randomized);
            Randomizer::setPaintingRandoEnabled(true);
            Randomizer::setKingdomRandoEnabled(false);
            Randomizer::setStartKingdomStateRandoEnabled(false);
            Randomizer::setChestOrderRandoEnabled(false);
            Randomizer::setCoinRandoEnabled(false);
            Randomizer::setShopPriceRandoEnabled(false);
            Randomizer::setStartPositionMode(Randomizer::StartPositionMode_Vanilla);
            Randomizer::setCapKingdomMoonUse(Randomizer::SpecialMoonUse_HintTokens);
            Randomizer::setCloudKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
            Randomizer::setMoonKingdomMoonUse(Randomizer::SpecialMoonUse_Coins);
            Randomizer::setWorldPeaceRequirementEnabled(false);
            Randomizer::setCaptureRandoEnabled(true);
            Randomizer::setLockedAbilitiesEnabled(true);
            Randomizer::setStartingEquipment(Randomizer::StartingEquipment_Normal);
            Randomizer::setCaptureAbilityDifficulty(Randomizer::CaptureAbilityDifficulty_Normal);
            Randomizer::setMoonRequirementLevel(Randomizer::MoonRequirement_124);
            Randomizer::setMoonDifficulty(Randomizer::MoonDifficulty_Medium);
            Randomizer::setUnlockPacing(Randomizer::UnlockPacing_Standard);
            Randomizer::setSeedOpenness(Randomizer::SeedOpenness_Standard);
            Randomizer::setSeedDifficulty(Randomizer::SeedDifficulty_Standard);
            Randomizer::setSphinxRandoEnabled(true);
            Randomizer::setSkyboxRandoMode(Randomizer::SkyboxRandoMode_Kingdoms);
            Randomizer::setMusicRandoEnabled(true);
            Randomizer::setStreamerModeEnabled(false);
            Randomizer::setMusicOverlayEnabled(true);
            Randomizer::setMusic2DThemesDisabled(false);
            Randomizer::setMusicExtraThemesDisabled(false);
            Randomizer::setMusicBossThemesDisabled(false);
            Randomizer::HintPriceProfile coinHintDefaults = { {
                                                                  static_cast<u8>(Randomizer::HintCoinPrice::Coins250),
                                                                  static_cast<u8>(Randomizer::HintCoinPrice::Coins500),
                                                                  static_cast<u8>(Randomizer::HintCoinPrice::Coins500),
                                                                  static_cast<u8>(Randomizer::HintCoinPrice::Coins250),
                                                                  static_cast<u8>(Randomizer::HintCoinPrice::Disabled),
                                                              },
                static_cast<u8>(Randomizer::HintCoinProgressiveIncrement::Disabled), false };
            Randomizer::HintPriceProfile tokenHintDefaults = { {
                                                                   static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                                   static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                                   static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                                   static_cast<u8>(Randomizer::HintTokenPrice::Tokens1),
                                                                   static_cast<u8>(Randomizer::HintTokenPrice::Disabled),
                                                               },
                static_cast<u8>(Randomizer::HintTokenProgressiveIncrement::Disabled), false };
            Randomizer::setHintCoinPriceProfile(coinHintDefaults);
            Randomizer::setHintTokenPriceProfile(tokenHintDefaults);
            Randomizer::setHintMode(Randomizer::HintMode::Purchases);
            Randomizer::setPurpleShopAbilityCount(1);
            Randomizer::setPurpleShopCaptureCount(1);
            Randomizer::setPurpleShopMoonsEnabled(true);
            Randomizer::setPurpleShopPrice(Randomizer::PurpleShopPrice::Purple50);
            Randomizer::setProportionalKingdomPricingEnabled(false);
        }

        void applyStandard() {
            applyCommon();
            Randomizer::setLogicRandoEnabled(true);
            Randomizer::setLogicDifficulty(Randomizer::LogicDifficulty_Casual);
        }

        void applyNoLogic() {
            applyCommon();
            Randomizer::setLogicRandoEnabled(false);
        }

        constexpr Preset sPresets[] = {
            { u"Standard",
                "Recommended settings with casual logic. The seed is beatable.",
                &applyStandard },
            { u"No Logic",
                "Fully random placement. The seed may not be beatable.",
                &applyNoLogic },
        };

        constexpr s32 sPresetCount = sizeof(sPresets) / sizeof(sPresets[0]);
    } // namespace

    s32 count() { return sPresetCount; }

    const Preset& get(s32 index) {
        if (index < 0)
            index = 0;
        if (index >= sPresetCount)
            index = sPresetCount - 1;
        return sPresets[index];
    }

    void apply(s32 index) {
        const Preset& preset = get(index);
        if (preset.apply)
            preset.apply();
    }
} // namespace Presets
