#include "Randomizer/HintEconomy.h"

#include "Randomizer/FeatureFlags.h"

#if SMOR_FEATURE_MULTIPLAYER
#include "Multiplayer/main.hpp"
#endif

#include <climits>
#include <cstdlib>
#include <cstring>

#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderWriter.h"
#include "nn/util.h"

namespace HintEconomy {

    namespace {

        constexpr char kRecordKey[] = "RandoHintEconomy";
        constexpr char kRecordPrefix[] = "RandoHintEconomy:";
        constexpr char kRecordVersion[] = "1:";
        constexpr s32 kRecordValueCount = 1 + Randomizer::kHintPriceCategoryCount;

        struct SaveState {
            s32 tokenBalance;
            u32 successfulPurchaseCounts[Randomizer::kHintPriceCategoryCount];
        };

        static bool isValidCategory(Randomizer::HintPriceCategory category) {
            return Randomizer::isValidHintPriceCategory(category);
        }

        // A completed hint purchase has its own durable HintUnlock event,
        // which already carries the debit and the price-ladder step. Letting
        // the generic yellow-coin hooks publish these internal mutations as
        // well would charge peers twice (and publish a refund for a purchase
        // that never completed). Remote HintUnlock application must be
        // suppressed for the same reason or it echoes another SubCoin event.
        static void addHintCoins(GameDataFile* file, s32 amount) {
#if SMOR_FEATURE_MULTIPLAYER
            SuppressCoinSyncScope suppressCoinSync;
#endif
            GameDataFunction::addCoin(
                GameDataHolderWriter(file->getGameDataHolder()), amount);
        }

        static void subtractHintCoins(GameDataFile* file, s32 amount) {
#if SMOR_FEATURE_MULTIPLAYER
            SuppressCoinSyncScope suppressCoinSync;
#endif
            GameDataFunction::subCoin(
                GameDataHolderWriter(file->getGameDataHolder()), amount);
        }

        static u8 categoryIndex(Randomizer::HintPriceCategory category) {
            return static_cast<u8>(category);
        }

        // Where on the price ladder the next purchase in this category sits.
        // The global setting only changes which counter is read, never how the
        // counters are stored or advanced, so toggling it re-prices the shop
        // without invalidating a save's existing history.
        static u32 ladderPosition(const SaveState& state,
            Randomizer::HintPriceCategory category) {
            if (!Randomizer::isHintCategoryProgressiveGlobal(category))
                return state.successfulPurchaseCounts[categoryIndex(category)];
            u64 total = 0;
            for (u8 i = 0; i < Randomizer::kHintPriceCategoryCount; i++)
                total += state.successfulPurchaseCounts[i];
            return total > UINT_MAX ? UINT_MAX : static_cast<u32>(total);
        }

        static bool parseNonNegative(const char*& text, u32 maxValue, u32* outValue) {
            if (!text || !outValue || *text < '0' || *text > '9')
                return false;
            char* end = nullptr;
            const unsigned long value = std::strtoul(text, &end, 10);
            if (end == text || value > static_cast<unsigned long>(maxValue))
                return false;
            *outValue = static_cast<u32>(value);
            text = end;
            return true;
        }

        static bool readState(const GameDataFile* file, SaveState* outState) {
            if (!file || !outState)
                return false;
            *outState = {};
            const char* record = file->customFindHackDictionaryKey(kRecordKey);
            if (!record)
                return false;

            const size_t prefixLen = sizeof(kRecordPrefix) - 1;
            const size_t versionLen = sizeof(kRecordVersion) - 1;
            if (std::strncmp(record + prefixLen, kRecordVersion, versionLen) != 0)
                return false;

            const char* cursor = record + prefixLen + versionLen;
            u32 values[kRecordValueCount] = {};
            for (s32 i = 0; i < kRecordValueCount; i++) {
                const u32 maxValue = i == 0 ? static_cast<u32>(INT_MAX) : UINT_MAX;
                if (!parseNonNegative(cursor, maxValue, &values[i]))
                    return false;
                if (i + 1 == kRecordValueCount) {
                    if (*cursor != '\0')
                        return false;
                } else if (*cursor++ != ',') {
                    return false;
                }
            }

            outState->tokenBalance = static_cast<s32>(values[0]);
            for (u8 i = 0; i < Randomizer::kHintPriceCategoryCount; i++)
                outState->successfulPurchaseCounts[i] = values[i + 1];
            return true;
        }

        static void markRequireSave(GameDataFile* file) {
            if (file && file->getGameDataHolder())
                GameDataFunction::setRequireSave(GameDataHolderWriter(file->getGameDataHolder()));
        }

        static bool writeState(GameDataFile* file, const SaveState& state) {
            if (!file || !file->getGameDataHolder() || state.tokenBalance < 0)
                return false;
            char record[128] = {};
            const s32 written = nn::util::SNPrintf(record, sizeof(record),
                "%s%s%d,%u,%u,%u,%u,%u", kRecordPrefix, kRecordVersion,
                state.tokenBalance, state.successfulPurchaseCounts[0],
                state.successfulPurchaseCounts[1], state.successfulPurchaseCounts[2],
                state.successfulPurchaseCounts[3], state.successfulPurchaseCounts[4]);
            if (written < 0 || written >= static_cast<s32>(sizeof(record))
                || !file->customUpsertHackDictionaryKey(kRecordKey, record))
                return false;

            SaveState verified = {};
            if (!readState(file, &verified)
                || verified.tokenBalance != state.tokenBalance
                || std::memcmp(verified.successfulPurchaseCounts,
                       state.successfulPurchaseCounts,
                       sizeof(state.successfulPurchaseCounts))
                    != 0)
                return false;
            markRequireSave(file);
            return true;
        }

        static SaveState readSanitizedState(GameDataFile* file) {
            SaveState state = {};
            readState(file, &state);
            return state;
        }

        static bool quotesEqual(const Randomizer::HintPriceQuote& left,
            const Randomizer::HintPriceQuote& right) {
            return left.currency == right.currency && left.amount == right.amount
                && left.enabled == right.enabled && left.progressive == right.progressive;
        }

        static TransactionResult refundTransaction(GameDataFile* file,
            PendingTransaction* transaction) {
            if (!file || !transaction || !transaction->charged
                || transaction->quote.amount <= 0)
                return TransactionResult::InvalidTransaction;

            if (transaction->quote.currency == Randomizer::HintCurrencyMode::Coins) {
                addHintCoins(file, transaction->quote.amount);
                markRequireSave(file);
            } else {
                SaveState state = {};
                if (!readState(file, &state)
                    || state.tokenBalance > INT_MAX - transaction->quote.amount)
                    return TransactionResult::StorageFailure;
                state.tokenBalance += transaction->quote.amount;
                if (!writeState(file, state))
                    return TransactionResult::StorageFailure;
            }

            transaction->charged = false;
            return TransactionResult::Success;
        }

    } // namespace

    s32 getBalance(GameDataFile* file, Randomizer::HintCurrencyMode currency) {
        if (!file)
            return 0;
        if (currency == Randomizer::HintCurrencyMode::Coins)
            return file->getCoinNum();
        return readSanitizedState(file).tokenBalance;
    }

    s32 getBalance(GameDataFile* file) {
        return getBalance(file, Randomizer::getHintCurrencyMode());
    }

    u32 getSuccessfulPurchaseCount(GameDataFile* file,
        Randomizer::HintPriceCategory category) {
        if (!file || !isValidCategory(category))
            return 0;
        return ladderPosition(readSanitizedState(file), category);
    }

    Randomizer::HintPriceQuote getQuote(GameDataFile* file,
        Randomizer::HintPriceCategory category) {
        return Randomizer::getHintPriceQuote(category,
            getSuccessfulPurchaseCount(file, category));
    }

    bool canAfford(GameDataFile* file, const Randomizer::HintPriceQuote& quote) {
        // Against the wallet the quote is actually drawn from: a coin-priced
        // category is affordable out of coins even in a Hint Token run.
        return file && quote.enabled && quote.amount > 0
            && quote.amount <= getBalance(file, quote.currency);
    }

    TransactionResult beginTransaction(GameDataFile* file,
        Randomizer::HintPriceCategory category,
        const Randomizer::HintPriceQuote& expectedQuote,
        PendingTransaction* outTransaction) {
        if (!file || !outTransaction || !isValidCategory(category))
            return TransactionResult::InvalidTransaction;
        *outTransaction = {};

        // Create/repair the counter record before either currency is debited.
        // Coin transactions also need it: a later successful purchase advances
        // the shared progressive counter.
        if (!initializeSaveState(file))
            return TransactionResult::StorageFailure;

        const u32 count = getSuccessfulPurchaseCount(file, category);
        const Randomizer::HintPriceQuote current = Randomizer::getHintPriceQuote(category, count);
        if (!quotesEqual(current, expectedQuote))
            return TransactionResult::QuoteChanged;
        if (!current.enabled || current.amount <= 0)
            return TransactionResult::Disabled;
        if (!canAfford(file, current))
            return TransactionResult::InsufficientFunds;

        if (current.currency == Randomizer::HintCurrencyMode::Coins) {
            subtractHintCoins(file, current.amount);
            markRequireSave(file);
        } else {
            SaveState state = readSanitizedState(file);
            if (state.tokenBalance < current.amount)
                return TransactionResult::InsufficientFunds;
            state.tokenBalance -= current.amount;
            if (!writeState(file, state))
                return TransactionResult::StorageFailure;
        }

        *outTransaction = { category, current, count, true };
        return TransactionResult::Success;
    }

    TransactionResult finishTransaction(GameDataFile* file,
        PendingTransaction* transaction, bool succeeded) {
        if (!file || !transaction || !transaction->charged
            || !isValidCategory(transaction->category) || !transaction->quote.enabled
            || transaction->quote.amount <= 0)
            return TransactionResult::InvalidTransaction;

        if (!succeeded)
            return refundTransaction(file, transaction);

        SaveState state = {};
        if (!readState(file, &state)) {
            const TransactionResult refundResult = refundTransaction(file, transaction);
            return refundResult == TransactionResult::Success
                ? TransactionResult::StorageFailure
                : refundResult;
        }

        const u8 index = categoryIndex(transaction->category);
        // Compared as a ladder position, not as a raw counter: under the
        // global setting a team-mate's purchase in another category is exactly
        // the kind of drift this check exists to catch.
        if (ladderPosition(state, transaction->category)
            != transaction->successfulPurchaseCount) {
            const TransactionResult refundResult = refundTransaction(file, transaction);
            return refundResult == TransactionResult::Success
                ? TransactionResult::QuoteChanged
                : refundResult;
        }

        // The final representable count is already a stable saturated state.
        // The purchase succeeded and stays paid; there is no further counter
        // value to persist, and the transaction must not remain live.
        if (state.successfulPurchaseCounts[index] == UINT_MAX) {
            transaction->charged = false;
            return TransactionResult::Success;
        }

        state.successfulPurchaseCounts[index]++;
        if (!writeState(file, state)) {
            const TransactionResult refundResult = refundTransaction(file, transaction);
            return refundResult == TransactionResult::Success
                ? TransactionResult::StorageFailure
                : refundResult;
        }
        transaction->charged = false;
        return TransactionResult::Success;
    }

    bool awardHintTokens(GameDataFile* file, s32 amount) {
        if (!file || amount <= 0
            || Randomizer::getHintCurrencyMode() != Randomizer::HintCurrencyMode::HintTokens)
            return false;
        SaveState state = readSanitizedState(file);
        if (state.tokenBalance > INT_MAX - amount)
            return false;
        state.tokenBalance += amount;
        if (!writeState(file, state))
            return false;
        // The debug log is also surfaced by the mod's in-game debug overlay;
        // use it instead of competing with an active moon-get presentation.
        Randomizer::logPush("Hint Token +%d (balance %d)", amount, state.tokenBalance);
        return true;
    }

    bool initializeSaveState(GameDataFile* file) {
        if (!file)
            return false;
        SaveState state = {};
        if (readState(file, &state))
            return true;
        return writeState(file, state);
    }

    bool resetProgressivePurchaseCounts(GameDataFile* file) {
        if (!file)
            return false;
        SaveState state = readSanitizedState(file);
        std::memset(state.successfulPurchaseCounts, 0,
            sizeof(state.successfulPurchaseCounts));
        return writeState(file, state);
    }

    bool applyRemotePurchase(GameDataFile* file, Randomizer::HintPriceCategory category,
        s32 amount) {
        if (!file || !isValidCategory(category) || amount <= 0)
            return false;
        if (!initializeSaveState(file))
            return false;

        SaveState state = readSanitizedState(file);

        const bool usesCoins = Randomizer::getHintCurrencyMode()
            == Randomizer::HintCurrencyMode::Coins;
        if (!usesCoins)
            state.tokenBalance -= amount < state.tokenBalance ? amount : state.tokenBalance;

        // Saturating, exactly as finishTransaction treats UINT_MAX: the top of
        // the ladder is a stable state, not an error.
        const u8 index = categoryIndex(category);
        if (state.successfulPurchaseCounts[index] != UINT_MAX)
            state.successfulPurchaseCounts[index]++;

        // Persist the ladder (and token debit) before touching native coins.
        // The economy record already exists after initializeSaveState(), but a
        // failed verified write must still leave the event wholly unapplied so
        // it cannot take coins without recording the matching ladder advance.
        if (!writeState(file, state))
            return false;
        if (usesCoins) {
            const s32 balance = file->getCoinNum();
            const s32 charge = amount < balance ? amount : balance;
            if (charge > 0) {
                subtractHintCoins(file, charge);
                markRequireSave(file);
            }
        }
        Randomizer::logPush("Hint purchase mirrored from team-mate: category=%d amount=%d",
            (int)index, (int)amount);
        return true;
    }

} // namespace HintEconomy
