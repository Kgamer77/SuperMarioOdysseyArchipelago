#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/Randomizer.h"

namespace HintSystem {

    // The kind of item a hint/purchase applies to. Each type has its own price.
    enum class HintType
    {
        Moon,
        ToadetteMedal,
        Capture,
        Ability,
    };

    namespace Prices {

        // New hint configuration uses Randomizer::HintPriceCategory.  This
        // adapter keeps the old HintSystem pricing table compiling while the
        // shop is converted, and gives that conversion one canonical mapping.
        static inline Randomizer::HintPriceCategory getCategory(HintType type,
            bool directPurchase) {
            if (directPurchase)
                return Randomizer::HintPriceCategory::Purchases;
            switch (type) {
            case HintType::Capture:
                return Randomizer::HintPriceCategory::Captures;
            case HintType::Ability:
                return Randomizer::HintPriceCategory::Abilities;
            case HintType::ToadetteMedal:
            case HintType::Moon:
            default:
                return Randomizer::HintPriceCategory::Moons;
            }
        }

        static inline Randomizer::HintPriceQuote getConfiguredPrice(HintType type,
            bool directPurchase, u32 successfulPurchaseCount) {
            return Randomizer::getHintPriceQuote(
                getCategory(type, directPurchase), successfulPurchaseCount);
        }

        static inline Randomizer::HintPriceQuote getConfiguredPathPrice(
            u32 successfulPurchaseCount) {
            return Randomizer::getHintPriceQuote(
                Randomizer::HintPriceCategory::Path, successfulPurchaseCount);
        }

        // Compatibility-only coin table used by the pre-Hint-Token shop.
        // New code should use getConfiguredPrice()/getConfiguredPathPrice().
        struct ItemCost {
            s32 hint;
            s32 buy;
        };

        // A complete set of prices for one tier, split per item type.
        struct PriceSet {
            ItemCost moon;
            ItemCost toadetteMedal;
            ItemCost capture;
            ItemCost ability;
        };

        // Indexed by Randomizer::HintPriceTier (Cheap, Normal, Expensive).
        static constexpr PriceSet cPriceSets[] = {
            // Cheap
            {
                /* moon         */ { 100, 1000 },
                /* toadetteMedal  */ { 1000, 10000 },
                /* capture      */ { 200, 2000 },
                /* ability      */ { 200, 4000 },
            },
            // Normal
            {
                /* moon         */ { 250, 2500 },
                /* toadetteMedal  */ { 2000, 10000 },
                /* capture      */ { 500, 5000 },
                /* ability      */ { 500, 5000 },
            },
            // Expensive
            {
                /* moon         */ { 500, 5000 },
                /* toadetteMedal  */ { 3000, 10000 },
                /* capture      */ { 1000, 9999 },
                /* ability      */ { 1000, 9999 },
            },
        };

        // Multi-moons grant several power moons at once, so direct purchases
        // cost extra. Their hints use the regular moon price.
        static constexpr s32 MultiMoonCostMultiplier = 3;

        // Extended Hints: after a moon hint reveals a location inside a
        // sub-area, the route to that sub-area can be bought separately. Priced
        // as a percentage of the same tier's moon-hint price, so it scales with
        // the chosen tier; 100 means "same price as the hint that revealed it".
        static constexpr s32 PathHintCostPercent = 100;

        static inline s32 getPathHintCost(const PriceSet& set) {
            const s32 cost = set.moon.hint * PathHintCostPercent / 100;
            return cost > 0 ? cost : 1;
        }

        static inline const PriceSet& getPriceSet(Randomizer::HintPriceTier tier) {
            return cPriceSets[static_cast<s32>(tier)];
        }

        static inline const ItemCost& getItemCost(const PriceSet& set, HintType type) {
            switch (type) {
            case HintType::Capture:
                return set.capture;
            case HintType::Ability:
                return set.ability;
            case HintType::ToadetteMedal:
                return set.toadetteMedal;
            case HintType::Moon:
            default:
                return set.moon;
            }
        }

    } // namespace Prices
} // namespace HintSystem
