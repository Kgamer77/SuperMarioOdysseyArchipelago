#pragma once

#include <basis/seadTypes.h>

#include "game/System/GameDataHolderAccessor.h"

namespace ShopPriceRando {

    // Re-prices Crazy Cap's gold-coin catalogue for the shop that is about to
    // open, and puts the vanilla prices back when the setting is off.
    //
    // Called from PurpleCoinRewards' ShopInfo::createShopItemList trampoline
    // (one hook per function - see Multiplayer/main.hpp) before that hook
    // decides whether this is a purple shop, because both kinds of shop read
    // the same catalogue and only the gold-coin half is touched here.
    //
    // worldId is the kingdom the shop belongs to; -1 when it could not be
    // resolved, which prices the per-kingdom items as if they were global.
    void applyToShopCatalogue(GameDataHolderAccessor accessor, s32 worldId);

} // namespace ShopPriceRando
