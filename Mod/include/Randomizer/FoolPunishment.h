#pragma once

#include <basis/seadTypes.h>

namespace Randomizer {

    enum class FoolPunishment : u8
    {
        None,
        Lose100Coins,
        Lose500Coins,
        Lose1000Coins,
        LoseHalfCoins,
        GainOneCoin,
        Jackpot,
        Kill,
    };

    // Every punishment except None, so the draw below can be "1 + hash % count".
    // It must be kept in step with the list above: the draw covers the whole
    // range, so a count larger than the list produces a value nothing handles.
    // (A TeleportToStart punishment used to sit between GainOneCoin and Jackpot;
    // it was removed because warping into the run's start kingdom crashes.)
    static constexpr u32 kFoolPunishmentCount = 7;

    // Kill is the last entry on purpose: dropping the top of the range is what
    // takes it out of the draw.
    static_assert(static_cast<u32>(FoolPunishment::Kill) == kFoolPunishmentCount,
        "Kill must stay last for rollFoolPunishment's exclusion to mean anything.");

    // A fool moon is a joke.  Under a Nuzlocke it would not be one: killing the
    // player ends the run outright on hard, and burns the whole reroll penalty on
    // soft, off a moon the player had no way to identify beforehand.  Every other
    // punishment costs coins or a walk back.
    inline FoolPunishment rollFoolPunishment(u32 hash, bool isNuzlockeRun) {
        const u32 count = isNuzlockeRun ? kFoolPunishmentCount - 1 : kFoolPunishmentCount;
        return static_cast<FoolPunishment>(1 + hash % count);
    }

    inline const char* getFoolPunishmentText(FoolPunishment punishment) {
        switch (punishment) {
        case FoolPunishment::Lose100Coins:
            return "You are a fool (and just lost 100 coins)";
        case FoolPunishment::Lose500Coins:
            return "You are a fool (and just lost 500 coins)";
        case FoolPunishment::Lose1000Coins:
            return "You are a fool (and just lost 1000 coins)";
        case FoolPunishment::LoseHalfCoins:
            return "You are a fool (and just lost half your coins)";
        case FoolPunishment::GainOneCoin:
            return "You are a fool (but you got 1 coin)";
        case FoolPunishment::Jackpot:
            return "You are a fool (but hit the jackpot!)";
        case FoolPunishment::Kill:
            return "You are a fool (and just died)";
        case FoolPunishment::None:
        default:
            return "You are a fool";
        }
    }

} // namespace Randomizer
