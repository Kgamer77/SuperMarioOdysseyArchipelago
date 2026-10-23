#include "Randomizer/CoinUncap.h"

#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "al/Library/Layout/IUseLayout.h"
#include "al/Library/Layout/LayoutActorUtil.h"

#include "game/Layout/StageSceneLayout.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderAccessor.h"

#include "Multiplayer/MpDebugLog.h"
#include "Randomizer/Randomizer.h"

// ── Uncapping the gold-coin counter ──────────────────────────────────────────
//
// Everything here is derived from the retail binary, not from a header. The
// decomp's GameDataFile.h static_asserts only the STRUCT'S TOTAL SIZE
// (sizeof(GameDataFile) == 0xb68) and pins no individual field offset - which
// is exactly the trap that made PlayerHackKeeper::isHack() read the wrong byte
// earlier in this project. So the offsets below were read out of
// `Odyssey data/main` (NSO, LZ4-decompressed, symbols from its own .dynsym)
// rather than trusted from the struct layout.
//
// GameDataFunction::addCoin @ 0x529c10 is a two-instruction thunk:
//
//     ldr  x0, [x0, #32]        ; GameDataHolder -> GameDataFile
//     b    GameDataFile::addCoin
//
// GameDataFunction::subCoin @ 0x529c18 is the same thunk with `neg w1, w1`
// spliced in - subtracting IS adding a negative. That single fact is why this
// one hook covers shops: ShopStateInShop::exeBuy charges through subCoin, so a
// purchase arrives here as a negative count and takes the same path.
//
// GameDataFile::addCoin @ 0x520fcc, in full:
//
//     ldr  w9,  [x0, #1496]     ; mCoinNum
//     ldr  w8,  [x0, #1500]     ; mTotalCoinNum
//     mov  w10, #9999
//     add  w9,  w9, w1          ; current + count
//     cmp  w9,  w10
//     csel w9,  w9, w10, lt     ; <- THE CAP: min(sum, 9999)
//     cmp  w1,  #1
//     str  w9,  [x0, #1496]
//     b.lt ...                  ; a negative/zero count skips the total update
//     ...                       ; mTotalCoinNum, saturated at 0x7ffffffe
//
// Two things follow. The cap is a plain min() on the RUNNING TOTAL, so an
// already-uncapped value passing through vanilla's code would be dragged back
// down to 9999 - which is why this hook rewrites mCoinNum AFTER calling orig()
// rather than trying to pre-adjust the argument. And mTotalCoinNum has its own,
// far larger saturation that orig() handles correctly, so it is deliberately
// left alone: the lifetime-total statistic never wanted a 9999 ceiling and does
// not have one.

namespace CoinUncap {

    namespace {

        // Confirmed above. Both are relative to GameDataFile, NOT to
        // GameDataHolder - the thunk has already dereferenced +0x20 by the time
        // this hook runs.
        constexpr ptrdiff_t cCoinNumOffset = 1496; // 0x5D8
        constexpr uintptr_t cGameDataFileAddCoinOffset = 0x520fcc;

        s32* coinNumPtr(void* gameDataFile) {
            return reinterpret_cast<s32*>(
                reinterpret_cast<uintptr_t>(gameDataFile) + cCoinNumOffset);
        }

        // Logged once per transition so a session shows the feature engaging
        // without a line per coin.
        bool sLoggedActive = false;

        // GameDataFile::addCoin(this, count). Not symbolised - installed at the
        // measured offset, the same way PuppetCapturePool hooks
        // al::LiveActorKit::endInit.
        //
        // NOTE this is deliberately NOT GameDataFunction::addCoin: that one is
        // already trampolined by syncHooks.cpp's addCoinHook for multiplayer
        // coin sync, and hakkun allows exactly ONE trampoline per function (see
        // the sync-bridge note in Multiplayer/main.hpp - a second one copies the
        // first's un-relocated branch and orig() jumps into unmapped memory).
        // Hooking the callee instead keeps both features working and, as a
        // bonus, catches subCoin too.
        static HkTrampoline<void, void*, s32> addCoinHook
            = hk::hook::trampoline([](void* gameDataFile, s32 count) -> void {
                  if (!gameDataFile || !Randomizer::isUncappedCoinsEnabled()) {
                      addCoinHook.orig(gameDataFile, count);
                      return;
                  }

                  s32* coins = coinNumPtr(gameDataFile);
                  const s32 before = *coins;

                  // Let the original run first: it owns mTotalCoinNum, the
                  // lifetime statistic, and gets its saturation right. It also
                  // writes a clamped mCoinNum, which is corrected below.
                  addCoinHook.orig(gameDataFile, count);

                  // Recompute from the value we saw BEFORE the call, because
                  // what orig() left behind is min(before + count, 9999) and is
                  // useless above the cap.
                  s64 wanted = static_cast<s64>(before) + static_cast<s64>(count);
                  if (wanted < 0)
                      wanted = 0;
                  if (wanted > cMaxCoins)
                      wanted = cMaxCoins;

                  *coins = static_cast<s32>(wanted);

                  if (!sLoggedActive && before <= 9999 && wanted > 9999) {
                      sLoggedActive = true;
                      MpDebugLog::write("[CoinUncap] past the vanilla cap: %d + %d -> %d",
                          before, count, static_cast<s32>(wanted));
                  }
              });

        // CoinCounter::updatePanel(this, coinCount, numDigits) - not symbolised,
        // found by scanning for reads of mNumDigits (+312) and following the
        // shared call target. Its whole body is:
        //
        //     if (numDigits != 3 && numDigits != 4) return;   // draws nothing
        //     setPaneStringFormat(this, "TxtCounter",   "%04d"/"%03d", count);
        //     setPaneStringFormat(this, "TxtCounterSh", "%04d"/"%03d", count);
        //
        // so it pads to four and never truncates. The five-character ceiling is
        // the PANE's, not the format's: raising the bflyt maxTextLength from 10
        // to 14 and then 24 changed nothing on screen, which is what ruled the
        // layout out as the lever. Six digits therefore cannot be drawn, and
        // 909999 was rendering as "90999".
        //
        // So past what fits, print scientific instead: "1.0e5" is exactly five
        // characters, and so is every value up to "9.9e9". Below 100000 this
        // hands straight back to the original and vanilla is untouched.
        constexpr uintptr_t cUpdatePanelOffset = 0x1de4f8;
        constexpr s32 cMaxPlainCoins = 99999;

        // The exponent marker of scientific notation.
        //
        // This is a real 'e' only because the counter's FONT was changed to one
        // that has letters. Vanilla draws it with NumberFont26 - digits and
        // punctuation, no letters at all - which is why an earlier attempt at
        // "9.9e5" rendered as "9.9?5". tools/widen_coin_counter.py now swaps
        // that pane's font to HeadFont72 in place, which it can do without
        // moving a single offset because the new name is shorter.
        //
        // If that layout edit is ever reverted, this must go back to a
        // punctuation mark - '+' was the previous choice, since real scientific
        // notation writes 9.9e+5. The glyph set was measured, not assumed: all
        // of ",:/-+" render, no letter does.
        constexpr char cExponentSeparator = 'e';

        // The IUseLayout the pane calls take is at `this + 8`, NOT `this` -
        // CoinCounter derives from al::LayoutActor and that is where the
        // IUseLayout subobject sits. The original spells it out:
        //
        //     add  x8, x19, #8
        //     cmp  x19, #0
        //     csel x21, xzr, x8, eq     ; (this == 0) ? null : this + 8
        //     mov  x0, x21              ; <- what setPaneStringFormat receives
        //
        // Passing `this` instead crashed on the first HUD draw, inside
        // al::setPaneStringLength off a null dereference (crash 01788401384,
        // reached through CoinCounter::updateCountImmidiate). The null check is
        // kept for the same reason the original has one.
        static HkTrampoline<void, void*, u32, s32> updatePanelHook
            = hk::hook::trampoline([](void* self, u32 coinCount, s32 numDigits) -> void {
                  al::IUseLayout* counter = self
                      ? reinterpret_cast<al::IUseLayout*>(reinterpret_cast<uintptr_t>(self) + 8)
                      : nullptr;
                  // numDigits 3 is the purple counter, which cannot overflow -
                  // and anything other than 3 or 4 the original ignores anyway.
                  if (!counter || numDigits != 4 || coinCount <= cMaxPlainCoins) {
                      updatePanelHook.orig(self, coinCount, numDigits);
                      return;
                  }

                  // Integer maths on purpose: one leading digit, one decimal,
                  // and the exponent. Truncated rather than rounded so 999999
                  // reads "9.9e5" instead of rounding up into "10.0e5", which
                  // would not fit.
                  s32 exponent = 0;
                  u32 scaled = coinCount;
                  while (scaled >= 10) {
                      scaled /= 10;
                      exponent++;
                  }
                  u32 divisor = 1;
                  for (s32 i = 0; i < exponent - 1; i++)
                      divisor *= 10;
                  const u32 second = divisor ? (coinCount / divisor) % 10 : 0;

                  // Exactly five characters for every magnitude this can reach:
                  // "1.0+5" through "9.9+8".
                  al::setPaneStringFormat(counter, "TxtCounter", "%d.%d%c%d",
                      static_cast<s32>(scaled), static_cast<s32>(second),
                      cExponentSeparator, exponent);
                  al::setPaneStringFormat(counter, "TxtCounterSh", "%d.%d%c%d",
                      static_cast<s32>(scaled), static_cast<s32>(second),
                      cExponentSeparator, exponent);
              });

    } // namespace

    void installHooks() {
        addCoinHook.installAtMainOffset(cGameDataFileAddCoinOffset);
        updatePanelHook.installAtMainOffset(cUpdatePanelOffset);
    }

    void updateCounterLayout(StageScene* scene) {
        // The gold counter's text pane is 84 units wide for four characters -
        // read out of CounterCoin.bflyt rather than guessed - so one digit is
        // 21. Widening the pane to six characters lets the number grow by two
        // digits, and the purple counter has to move by exactly that much.
        constexpr f32 cDigitWidth = 84.0f / 4.0f;

        if (!scene || !scene->stageSceneLayout)
            return;

        // Forward-declared in the custom StageSceneLayout header, and a
        // CoinCounter is an al::LayoutActor - same cast TrackerOverlay uses on
        // mShineCountLyt.
        al::LayoutActor* purple
            = reinterpret_cast<al::LayoutActor*>(scene->stageSceneLayout->mCoinCollectLyt);
        if (!purple)
            return;

        // The base position has to be sampled from the layout itself, once,
        // before we have moved it - hardcoding it would break the moment the
        // layout changed. Re-sampled whenever the scene changes, because the
        // counter is rebuilt with the scene and the old pointer means nothing.
        static StageScene* sBaseScene = nullptr;
        static al::LayoutActor* sBaseActor = nullptr;
        static sead::Vector2f sBaseTrans = sead::Vector2f(0.0f, 0.0f);
        if (sBaseScene != scene || sBaseActor != purple) {
            sBaseScene = scene;
            sBaseActor = purple;
            sBaseTrans = al::getLocalTrans(purple);
        }

        // A FIXED shift, not one computed from the current total.
        //
        // Scientific notation bounds the gold counter at five characters for
        // every value it can now hold - "99999" and "9.9e8" are the same width -
        // so there is nothing left to track per frame. One digit of clearance
        // covers it, always.
        //
        // This also removes the failure mode the value-driven version had: it
        // re-sampled the purple counter's base position whenever the scene or
        // the counter pointer changed, and any path that rebuilt the counter
        // without changing either would have re-sampled an ALREADY-SHIFTED base
        // and crept further right each time. A constant offset cannot drift.
        //
        // Still gated on the toggle, so a player with uncapped coins off sees
        // the vanilla HUD exactly as it has always been.
        const f32 offset = Randomizer::isUncappedCoinsEnabled() ? cDigitWidth : 0.0f;
        const sead::Vector2f wanted(sBaseTrans.x + offset, sBaseTrans.y);

        // Only write when it actually differs: this runs every frame, and
        // setLocalTrans on an unchanged value is pure churn.
        const sead::Vector2f current = al::getLocalTrans(purple);
        if (current.x != wanted.x || current.y != wanted.y)
            al::setLocalTrans(purple, wanted);
    }

} // namespace CoinUncap
