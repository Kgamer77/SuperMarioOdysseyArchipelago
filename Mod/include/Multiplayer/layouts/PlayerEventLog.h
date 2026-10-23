#pragma once

#include "nn/account.h"

#include "Randomizer/MoonRando.h"
#include "ShineDB.h"
#include "sead/basis/seadTypes.h"
#include "sead/heap/seadDisposer.h"
#include "sead/prim/seadSafeString.h"

constexpr s32 sNumEntries = 8;

class PlayerEventLog {
    SEAD_SINGLETON_DISPOSER(PlayerEventLog)

public:
    enum Event
    {
        CONNECT,
        DISCONNECT,
        START,
        SHINE,
        PURPLE,
        CHECKPOINT,
        MOONROCK,
        // Regular (yellow) coin pickups - same coalescing shape as PURPLE
        // (rides mNumPurples too - see addEvent/addSelfEvent), just without
        // a per-kingdom text key since the coin total is global.
        COIN,
        // An Ability or Capture bought from the Purple Shop. Purple Shop
        // Moon purchases don't need this - they already go through the
        // normal setGotShine -> mpOnSetGotShine path and log as a plain
        // SHINE event.
        PURPLE_BUY,
        // A genuine Mario death (missAndRestartStageHook in
        // EntranceHooks.cpp - the same signal Nuzlocke/TimerMode already
        // answer to). The coin loss that comes with it isn't carried on
        // this event - it already rides the COIN/SUBCOIN sync like any
        // other coin change.
        DEATH,
        PEACH_QUEST
    };

    // Per-category extras a collected moon can carry beyond its plain name -
    // each rendered in its own color (see update()'s SHINE case), separate
    // from the moon name/kingdom text which always stays the ordinary moon-
    // pickup yellow. Moon Rock Key is mutually exclusive with the other two
    // (a Moon Rock Key is never also a painting-unlock or ability/capture
    // moon); painting and reward can both be set on the same moon.
    struct ShineExtras {
        // Out-of-line ctor (defined in the .cpp) rather than a default member
        // initializer on rewardIsCapture below - a nested class's default
        // member initializer can't be evaluated while the enclosing
        // PlayerEventLog class is still being defined, which every other use
        // of ShineExtras as a default argument in this header needs.
        ShineExtras();

        sead::FixedSafeString<64> moonRockKeyText; // e.g. "(Bowser's Kingdom Moon Rock Key)" - red
        sead::FixedSafeString<96> paintingText; // e.g. "+ Painting: Metro -> Seaside" - blue
        // Holds up to THREE rewards, joined into one row: "(Jizo, Frog, Pokio)".
        // A Multi (triple) moon grants three, and both randomizers already
        // expose them by index - CaptureRando::getCaptureForMoon(uid, stage, i)
        // and AbilityRando::getLastUnlockedAbility(i). 128 rather than 48
        // because three display names plus separators do not fit the old width;
        // a Mushroom Kingdom multi is three captures, a Dark/Darker Side multi
        // is three abilities.
        sead::FixedSafeString<128> rewardText; // e.g. "(Jizo)" - orange if capture, grey if ability
        bool rewardIsCapture;

        bool any() const {
            return !moonRockKeyText.isEmpty() || !paintingText.isEmpty() || !rewardText.isEmpty();
        }
    };

    struct Entry {
        Entry();
        Entry(nn::account::Uid player, Event event, sead::FixedSafeString<128> text,
            const ShineExtras& extras = ShineExtras());
        Entry(Event event, sead::FixedSafeString<128> text, const ShineExtras& extras = ShineExtras());

        nn::account::Uid mPlayerId;
        sead::FixedSafeString<16> mPlayerName;
        bool mIsPlayerName = false;
        Event mEvent = SHINE;
        sead::FixedSafeString<128> mText;
        ShineExtras mShineExtras;
        s32 mNumPurples = 1;
        s32 mLife = -1;
    };

    PlayerEventLog();

    // amount is only used for PURPLE/COIN coalescing (how much to add to a
    // matching existing entry's running count, or to seed mNumPurples on a
    // fresh one) - every other event type ignores it.
    // Silences addEvent/addSelfEvent for as long as it is alive.
    //
    // For applying our OWN echo of a shared event. Protocol 6 stopped dropping
    // own events at the isOwn early return, because doing so left the server
    // seeing the same gap in our state vector forever and re-sending it every
    // sweep - the reconcile loop that filled the sidebar. Letting them through
    // fixed that, but the apply path also writes an event-log row, so a moon
    // you collected yourself appeared twice: once when you took it, once when
    // the server handed it back.
    //
    // Suppressing only the LOG is the right half to drop. The apply itself must
    // still run: it is idempotent by construction (see DurableSync.h) and it is
    // what converges state after a rejoin. Same shape as SuppressCoinSyncScope
    // and MoonHooks::RemoteShineGrantScope.
    // Takes a flag rather than being unconditional, the same shape as
    // LateModelDrawer::ScopedLateBuild(!isEarly) - the call site wants "suppress
    // only when this is our own echo" and a conditional object beats a
    // conditionally-destroyed one.
    class SuppressScope {
    public:
        explicit SuppressScope(bool active = true);
        ~SuppressScope();
        SuppressScope(const SuppressScope&) = delete;
        SuppressScope& operator=(const SuppressScope&) = delete;

    private:
        bool mPrev;
    };
    static bool isSuppressed();

    static void addEvent(nn::account::Uid player, Event event, sead::SafeString text,
        const ShineExtras& extras = ShineExtras(), s32 amount = 1);
    static void addSelfEvent(Event event, sead::SafeString text, const ShineExtras& extras = ShineExtras(),
        s32 amount = 1);
    void update();
    void tryUpdateNames();

    static s32 calculateLife();

    static const char* getShineMessage(sead::SafeString stage, sead::SafeString objId);
    static const char* getShineMessageWithSource(s32 uid, sead::SafeString stage, sead::SafeString objId);
    // Fills in whichever of moonRockKeyText/paintingText/rewardText (capture
    // only - see fillLastUnlockedAbility for why abilities are separate)
    // apply to this moon (leaving the rest empty) - call right after
    // getShineMessageWithSource() for the same uid/stage and pass the result
    // straight to addEvent()/addSelfEvent().
    static ShineExtras getShineExtras(s32 uid, sead::SafeString stage);
    // Local-collection-only: fills extras.rewardText from what
    // AbilityRando::unlockForMoon() actually just granted on this save (a
    // no-op if a capture already filled rewardText, or if nothing was
    // unlocked). Call immediately after applyUnlockFeedback() has run for
    // this exact moon - never for a remote player's collection.
    static void fillLastUnlockedAbility(ShineExtras& extras);
    // Remote-collection counterpart: fills extras.rewardText from an
    // AbilityId carried on the wire (ShineCollect::abilityId) rather than
    // computed locally - the receiving player's own save has no way to
    // derive what ability the SENDER's unlockForMoon call actually granted.
    // No-op if abilityId < 0 or a capture already filled rewardText.
    // extraIds is the remote sender's SECOND and THIRD abilities for a Multi
    // moon, comma separated, or empty/null. Captures need no equivalent: they
    // come from the seed, which every client has, so getShineExtras derives all
    // three of those locally for remote players too.
    static void fillAbilityById(ShineExtras& extras, s32 abilityId, const char* extraIds = nullptr);
    static const char* getCheckpointMessage(sead::SafeString objId);
    static const char* getAchievementMessage(sead::SafeString label);

    static void toggleShow() { mIsShow = !mIsShow; }
    static bool isShow() { return mIsShow; }
    static void setShow(bool isShow) { mIsShow = isShow; }

private:
    Entry mLog[8];
    static bool mIsShow;
};