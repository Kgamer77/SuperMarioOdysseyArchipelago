#include "Randomizer/ShopPriceRando.h"

#include "Randomizer/Randomizer.h"
#include "Randomizer/Rng.h"

#include "game/Layout/ShopLayoutInfo.h"
#include "game/System/GameDataFunction.h"

#include "sead/container/seadPtrArray.h"

// ── Gold-Coin Shop Price Randomization ───────────────────────────────────────
//
// Crazy Cap sells out of one catalogue: GameDataFunction::getShopItemInfoList
// is a single sead::PtrArray<ShopItemInfo> owned by GameDataHolder, and every
// shop in the game builds its screen out of the same records
// (ShopInfo::createShopItemList filters that list per kingdom rather than
// owning its own copy). Each record carries its own price, and both the price
// the screen draws and the amount ShopStateInShop::exeBuy charges read that one
// field, so rewriting it is the whole feature.
//
// Two consequences follow from the catalogue being shared:
//
//   * A gold-coin outfit or cap is genuinely the same record everywhere, so
//     pricing it from its name alone gives it one price across the run. The
//     Life-Up Heart and the shop Power Moon are also single records, so making
//     THEM differ per kingdom means re-deriving their price on every shop open
//     with the kingdom mixed in. That is exactly what happens below, and it is
//     safe because only one shop screen exists at a time.
//
//   * The catalogue outlives a save file - it is loaded once per boot, not per
//     slot. Loading a randomized save and then a vanilla one in the same
//     session would otherwise leave the randomized prices in place, so the
//     vanilla prices are captured the first time through and restored whenever
//     the setting is off.
//
// Regional-coin items (souvenirs, stickers) are left alone: they are the purple
// shop's business, and Purple Shop Price already governs what that shop
// charges.

namespace ShopPriceRando {

    namespace {

        // The catalogue is a couple of hundred records; this is a ceiling, not
        // a size. Anything past it keeps its vanilla price rather than being
        // re-priced with no way to put it back.
        constexpr s32 kMaxTrackedItems = 512;
        constexpr s32 kMinPrice = 1;
        constexpr s32 kMaxPrice = 1000;

        static s32 sOriginalPrices[kMaxTrackedItems] = {};
        static s32 sOriginalCount = 0;
        static bool sPricesApplied = false;

        u32 hashText(const char* text) {
            u32 hash = 2166136261u;
            for (const char* cursor = text; cursor && *cursor; cursor++) {
                hash ^= static_cast<u8>(*cursor);
                hash *= 16777619u;
            }
            return hash;
        }

        // Outfits and caps are the shared half of the catalogue and keep one
        // price everywhere; the Life-Up Heart and the shop Power Moon are
        // re-rolled per kingdom so the same shelf reads differently in each
        // Crazy Cap.
        bool isPricedPerKingdom(ShopItem::ItemType type) {
            return type != ShopItem::ItemType::Cloth && type != ShopItem::ItemType::Cap;
        }

        s32 makePrice(const ShopItem::ShopItemInfo& item, s32 worldId) {
            u32 key = hashText(item.info.name);
            if (isPricedPerKingdom(item.info.type) && worldId >= 0)
                key ^= static_cast<u32>(worldId + 1) * 0x9e3779b9u;
            Randomizer::Rng rng = Randomizer::makeRng(
                Randomizer::getSeed() ^ key, Randomizer::RngStream::ShopPrice);
            return kMinPrice + static_cast<s32>(rng.nextModulo(kMaxPrice - kMinPrice + 1));
        }

        bool isGoldCoinItem(const ShopItem::ShopItemInfo& item) {
            return item.coinType == ShopItem::CoinType::Coin;
        }

    } // namespace

    void applyToShopCatalogue(GameDataHolderAccessor accessor, s32 worldId) {
        if (!accessor.mData)
            return;
        const sead::PtrArray<ShopItem::ShopItemInfo>& catalogue
            = GameDataFunction::getShopItemInfoList(accessor);
        const s32 total = catalogue.size();
        if (total <= 0)
            return;
        const s32 count = total < kMaxTrackedItems ? total : kMaxTrackedItems;

        if (sOriginalCount != count) {
            // First shop of the session, or a catalogue that changed shape
            // underneath us. Either way the prices in it are the vanilla ones
            // right now only if nothing has been applied yet.
            if (sPricesApplied)
                return;
            for (s32 i = 0; i < count; i++) {
                const ShopItem::ShopItemInfo* item = catalogue.at(i);
                sOriginalPrices[i] = item ? item->price : -1;
            }
            sOriginalCount = count;
            if (total > kMaxTrackedItems) {
                Randomizer::logPush(
                    "shop price rando: catalogue has %d items, pricing the first %d",
                    total, count);
            }
        }

        if (!Randomizer::isShopPriceRandoEnabled()) {
            if (!sPricesApplied)
                return;
            for (s32 i = 0; i < count; i++) {
                ShopItem::ShopItemInfo* item = catalogue.at(i);
                if (item && sOriginalPrices[i] >= 0)
                    item->price = sOriginalPrices[i];
            }
            sPricesApplied = false;
            return;
        }

        for (s32 i = 0; i < count; i++) {
            ShopItem::ShopItemInfo* item = catalogue.at(i);
            // A record with no vanilla price of its own is not for sale here;
            // giving it one would put it on the shelf.
            if (!item || !isGoldCoinItem(*item) || sOriginalPrices[i] < 0)
                continue;
            item->price = makePrice(*item, worldId);
        }
        sPricesApplied = true;
    }

} // namespace ShopPriceRando
