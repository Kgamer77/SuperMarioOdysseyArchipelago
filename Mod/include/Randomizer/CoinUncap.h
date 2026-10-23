#pragma once

#include "sead/basis/seadTypes.h"

class StageScene;

// Lifts vanilla's 9999 gold-coin ceiling when Randomizer::isUncappedCoinsEnabled().
namespace CoinUncap {

    // The ceiling the uncapped mode enforces instead of 9999.
    //
    // Six digits, not s32's ten, and the reason is the HUD rather than the
    // save: CounterCoin.szs's TxtCounter pane is a text pane with a
    // maxTextLength, so the counter can only draw as many characters as that
    // pane was allocated. Vanilla allocates 10 bytes - four UTF-16 characters
    // plus a terminator - which IS the 9999 cap, expressed in the layout.
    //
    // Six is also what makes a scientific-notation fallback fit later without
    // a second layout change: "1.00e7" is exactly six characters, so one pane
    // width covers everything from 0 up to s32's maximum.
    // Nine digits. Scientific notation fits any magnitude in the five
    // characters the counter can draw ("9.9e8" is the same width as "1.0e5"),
    // so the display stopped being the constraint once the notation went in.
    // Kept a round number well clear of the s32 edge rather than pushed to
    // INT_MAX, so the running total plus an incoming grant cannot approach
    // overflow inside GameDataFile::addCoin's own arithmetic.
    inline constexpr s32 cMaxCoins = 999999999;

    void installHooks();

    // Keeps the purple-coin counter clear of a widened gold counter.
    //
    // The two HUD counters sit side by side and vanilla's gold one is always
    // four characters, so the purple one's position assumes that width. Once
    // coins run past 9999 the gold digits grow rightwards and draw straight
    // over the purple counter - visible the moment uncapped coins was first
    // tested.
    //
    // Done in code rather than by moving the pane in CounterCollectCoin.szs
    // because that layout ships in romfs for EVERY player, including those with
    // the toggle off: a static shift would move the purple counter for people
    // who never asked for uncapped coins. This only displaces it while the gold
    // counter is actually wider than vanilla, and puts it straight back
    // otherwise.
    //
    // Safe to call every frame; cheap and idempotent.
    void updateCounterLayout(StageScene* scene);

} // namespace CoinUncap
