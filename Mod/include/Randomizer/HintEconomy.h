#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/Randomizer.h"

class GameDataFile;

// Runtime contract for the Hint Token wallet and the per-category progressive
// counters.  HintEconomy.cpp will persist token state in GameDataFile; coins
// remain backed by the native coin balance.  A caller must finish every begun
// transaction so failed grants can be refunded and successful ones can advance
// exactly one category counter.
namespace HintEconomy {

    enum class TransactionResult : u8
    {
        Success = 0,
        Disabled,
        InsufficientFunds,
        QuoteChanged,
        StorageFailure,
        InvalidTransaction,
    };

    struct PendingTransaction {
        Randomizer::HintPriceCategory category;
        Randomizer::HintPriceQuote quote;
        u32 successfulPurchaseCount;
        bool charged;
    };

    // Native coins, or the persisted Hint Token balance.  Null files return
    // zero.  The one-argument form answers for the run's default currency;
    // pass a quote currency when a single category overrides it.
    s32 getBalance(GameDataFile* file, Randomizer::HintCurrencyMode currency);
    s32 getBalance(GameDataFile* file);
    // The ladder position a progressive price is quoted from: this category's
    // own successful-purchase count, or - while Randomizer's global
    // progressive cost is on - the save's total across every category, so a
    // purchase of any type raises the price of all of them.  Per-category
    // counts stay the stored form either way, which is what lets the setting
    // be turned on or off mid-run without rewriting save state.
    u32 getSuccessfulPurchaseCount(GameDataFile* file,
        Randomizer::HintPriceCategory category);

    // Used for menus and confirmation prompts.  The returned quote is disabled
    // when the configured category is disabled.
    Randomizer::HintPriceQuote getQuote(GameDataFile* file,
        Randomizer::HintPriceCategory category);
    bool canAfford(GameDataFile* file, const Randomizer::HintPriceQuote& quote);

    // beginTransaction re-quotes immediately before debiting and compares its
    // currency, amount, enabled, and progressive fields with the confirmation
    // quote. QuoteChanged means the UI must refresh/reconfirm rather than
    // charging a stale progressive amount.
    // finishTransaction(false) refunds the debit; finishTransaction(true)
    // advances the category count exactly once.
    TransactionResult beginTransaction(GameDataFile* file,
        Randomizer::HintPriceCategory category,
        const Randomizer::HintPriceQuote& expectedQuote,
        PendingTransaction* outTransaction);
    TransactionResult finishTransaction(GameDataFile* file,
        PendingTransaction* transaction, bool succeeded);

    // Awarding is token-only.  It is a no-op in coin mode, returns false for a
    // bad file or persistence failure, and must be called only for a first-time
    // eligible moon collection by the moon hook.
    bool awardHintTokens(GameDataFile* file, s32 amount = 1);

    // Ensures the per-save economy record exists and verifies that it can be
    // read back.  New-save creation must call this before token awards or shop
    // transactions so a full hack dictionary is reported instead of silently
    // losing state.
    bool initializeSaveState(GameDataFile* file);

    // Used when a seed-specific hint state is intentionally cleared.  The
    // wallet is preserved; only progression counters are reset.
    bool resetProgressivePurchaseCounts(GameDataFile* file);

    // Mirrors a team-mate's completed purchase onto this save: the same debit
    // and the same one-step advance of the same category counter, so a team
    // shares one wallet and one price ladder rather than one per member.
    //
    // Deliberately NOT a begin/finish pair. There is nothing to quote against
    // (the price was settled on the buyer's save) and nothing to refund to (a
    // purchase that already happened elsewhere cannot be un-happened here), so
    // this is a straight application of a decided outcome.
    //
    // A balance too small to cover the debit is floored at zero rather than
    // refused. Balances only drift apart if someone spent coins on something
    // outside the hint system, and refusing would leave that player permanently
    // richer than the team - taking what is there keeps the pool honest.
    bool applyRemotePurchase(GameDataFile* file, Randomizer::HintPriceCategory category,
        s32 amount);

} // namespace HintEconomy
