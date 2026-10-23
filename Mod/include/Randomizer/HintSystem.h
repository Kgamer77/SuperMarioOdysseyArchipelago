#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/Randomizer.h"

namespace al {
    class IUseLayout;
    class IUseSceneObjHolder;
    struct ActorInitInfo;
    class LayoutActor;
}

class CommonVerticalList;
class GameDataHolderAccessor;
class GameDataHolderWriter;
class GameDataFile;

namespace ShineDB {
    struct ShineEntry;
}

namespace HintSystem {

    enum class PurchaseResult
    {
        Success,
        Disabled,
        InvalidSelection,
        AlreadyAcquired,
        AlreadyUnlocked,
        NotEnoughFunds,
        CannotBuyProgression,
        StorageFailure,
        QuoteChanged,
    };

    void observeShineListQuery(const al::LayoutActor* layout, s32 worldId, s32 index);
    void observeShineListLayout(const al::LayoutActor* layout);
    bool isMoonListActive();
    bool isDirectShineListViewActive();
    // True for a non-moon informational collection-list page (Unlocked
    // Paintings or Moon Rock Keys).  Used by the shared moon-message hook to
    // avoid asking the native list for a nonexistent Dark/Darker Side row.
    bool isCustomShineListInfoPage(s32 worldId);
    // Drops the cached Unlocked Paintings rows. Call wherever a shine actually
    // reaches the save file, alongside PaintingRando::invalidateUnlockCache:
    // a peer's Cloud moon can unlock a painting while the page is open.
    void invalidatePaintingListRows();
    // Builds the collection list's hint-list-derived indices ahead of time.
    // Call from the game thread while a load is already on screen (scene init);
    // it is a no-op once they are current, and skips a file whose hint list has
    // not been allocated yet.
    void warmShineListCaches(GameDataFile* file);
    // Identifies a ShineList layout by its own name, independent of whether the
    // player currently "has it in direct view" - ShineListLayout populates its
    // per-world name cache once, at construction (see
    // GameDataFunction::tryGetWorldName's callers), well before
    // isDirectShineListViewActive() would ever read true for it.
    bool isLikelyShineListLayout(const al::LayoutActor* layout);
    bool shouldRevealShineListName(const al::LayoutActor* layout, s32 worldId, s32 index);
    bool shouldShowActionPrompt(const al::LayoutActor* layout, s32 worldId, s32 index);
    bool tryHandleListBuyInput();
    bool tryHandleListHintInput();
    // Consumes the collection list's cancel press when the route panel is
    // open, so B dismisses the panel instead of closing the list out
    // from under it. False (do nothing) whenever the panel isn't showing.
    bool tryHandleListCancelInput();
    bool isConfirmationDialogActive();
    bool isPollingConfirmationDialogInput();
    bool shouldPulseCollectionListCancel();
    void observeShineActorInitInfo(const al::ActorInitInfo& info);
    void resetStageContext();
    // Removes hint purchases that describe a particular randomized placement.
    // Used by Soft Nuzlocke before rebuilding a fresh seed.
    void clearSeedSpecificState(GameDataFile* file);

    // ── shared team hint economy ────────────────────────────────────────────
    //
    // Which storage a purchase touched, so the same purchase can be reproduced
    // on a team-mate's save. Sent on the wire, so the numbers are fixed.
    enum class SharedUnlock : s32
    {
        // The wallet moved but the thing it bought syncs through its own
        // channel already - a bought moon rides ShineCollect, a bought
        // capture/ability rides its own unlock packet. Only the debit and the
        // price ladder need mirroring here.
        EconomyOnly = 0,
        MoonHint = 1, // a moon's location, keyed by ShineDB index
        RouteHint = 2, // a route to a location, keyed by ShineDB index
        FlagHint = 3, // capture/ability hint, keyed by its hack-dictionary flag
    };

    // Applies a purchase a team-mate completed: the debit and the ladder
    // advance always, and the unlock itself when this save does not have it.
    //
    // The two are separated on purpose. The debit belongs to the EVENT, which
    // the server delivers exactly once, so it applies whether or not the unlock
    // was already set locally; the unlock is idempotent state.
    // Returns false only when the event is invalid or its economy state could
    // not be persisted. An unlock that was already present is a successful,
    // idempotent application: the event's debit still belongs to this save.
    bool applyRemoteHintUnlock(GameDataFile* file, SharedUnlock unlock, s32 shineIndex,
        const char* flagKey, Randomizer::HintPriceCategory category, s32 amount);
    const char16_t* tryMakeStaticFooterPrompt();
    const char16_t* tryMakeFooterPrompt();
    const char16_t* tryGetSystemMessageOverride(const char* fileName, const char* key);
    bool isToadetteMedalHuntMessage(const char* fileName, const char* key);
    bool showToadetteMedalTombstoneNotReadyMessage(const al::IUseSceneObjHolder* holder);
    const char16_t* tryMakeCaptureGuidePaneString(const al::IUseLayout* layout,
        const char* paneName);
    void installHooks();

    PurchaseResult buyHint(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    PurchaseResult buyMoon(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);

    const char16_t* tryMakeHintMessage(GameDataHolderAccessor accessor, s32 worldId, s32 index,
        bool allowLocked = false);
    // Compressed row text for a Moon Rock Keys page row ("Cap Kingdom", plus
    // " - <location> (<kingdom>)" once hinted or collected). Returns null for
    // any other page; consumed by the shared tryFindShineMessage hook so the
    // native ShineList page update renders the rows itself.
    const char16_t* tryMakeMoonRockKeyRowText(GameDataHolderAccessor accessor, s32 worldId, s32 index);
    const char16_t* tryMakeSubareaKeyRowText(GameDataHolderAccessor accessor, s32 worldId, s32 index);
    const char16_t* tryMakeActionPrompt(const al::LayoutActor* layout,
        GameDataHolderAccessor accessor, s32 worldId, s32 index);
    const ShineDB::ShineEntry* tryFindOriginalMoon(GameDataFile* file, s32 worldId, s32 index);
    const ShineDB::ShineEntry* tryFindRandomizedLocation(GameDataFile* file, s32 worldId, s32 index);

    // The Movement Abilities' Ability List icons. Their art was added to
    // TextureSouvenir for this mod, so no vanilla record builds it and it has to be
    // created here. Call prepare from StageScene layout init (a sead heap has to be
    // current - al::createTextureInfo allocates) and invalidate when the scene is
    // torn down: the textures live on that scene's heap.
    void prepareAbilityIconTextures();
    void invalidateAbilityIconTextures();

} // namespace HintSystem
