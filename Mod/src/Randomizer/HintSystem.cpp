#include "Randomizer/HintSystem.h"

#include "game/Layout/ShopLayoutInfo.h"

#include "Multiplayer/MpDebugLog.h"
#if SMOR_FEATURE_MULTIPLAYER
#include "Multiplayer/server/Client.hpp"
#endif
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EnemySwapHooks.h"
#include "Randomizer/HintEconomy.h"
#include "Randomizer/MoonHooks.h"
#include "Randomizer/MoonPurchaseOverlay.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/PaintingRando.h"
#include "Randomizer/PathHintOverlay.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/SubareaKeyDB.h"
#include "Randomizer/WorldMeta.h"
#include "ShineDB.h"
#include "helpers.hpp"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActor.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/LiveActor/ActorAnimFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/Message/MessageHolder.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Play/Layout/RollParts.h"
#include "al/Library/Play/Layout/WindowConfirm.h"
#include "al/Library/Scene/IUseSceneObjHolder.h"
#include "game/Demo/ProjectDemoDirector.h"
#include "game/Item/Shine.h"
#include "game/Item/ShineInfo.h"
#include "game/Layout/CommonVerticalList.h"
#include "game/Layout/FooterParts.h"
#include "game/Layout/MenuSelectParts.h"
#include "game/MapObj/CapMessageShowInfo.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/Scene/StageScene.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/Util/StageInputFunction.h"
#include "hk/hook/Trampoline.h"
#include "hk/hook/a64/Assembler.h"
#include "hk/ro/RoUtil.h"
#include "nn/util.h"
#include "prim/seadSafeString.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace al {
    class ActionPadAndCameraCtrl;
    class ActionPadAndCameraCtrlInfo;
    class IUseMessageSystem;
    class IUseLayoutAction;
}

namespace HintSystem {
    static constexpr s32 cMaxCustomShineListRows = 64;
    // Medal-message diagnostics run after every moon's native collection
    // callback. Keep them opt-in so they do not add synchronous debug I/O to a
    // normal pickup.
    static constexpr bool cLogToadetteMedalDiagnostics = false;

    // The ShineList queries collection state repeatedly while it animates and
    // scrolls. The paintings page is immutable while the collection list is
    // open, so keep its row state cached for that visit instead of redoing
    // unlock lookups every frame. (The Moon Rock Keys page has no such cache:
    // it renders through the native per-page update, which only queries its
    // rows on page changes and cursor moves.)
    static bool sPaintingListRowsDirty = true;
    static bool sPaintingListRowsValid = false;
    static u32 sPaintingListRowsRoleLayoutHash = 0;
    static bool sPaintingListUnlockedRows[cMaxCustomShineListRows] {};

    // "Immutable while the list is open" holds for a solo pickup - the player
    // cannot collect anything from inside the pause menu. A peer's moon can
    // still land mid-visit though, and that is exactly when a Cloud moon
    // unlocks a painting, so the same commit that drops PaintingRando's unlock
    // cache has to drop these rows too or the open page keeps showing the
    // pre-grant row text and count.
    void invalidatePaintingListRows() {
        sPaintingListRowsDirty = true;
        sPaintingListRowsValid = false;
    }

    static const al::LayoutActor* sObservedShineListLayout = nullptr;
    static s32 sObservedWorldId = -1;
    static s32 sObservedIndex = -1;
    static const al::LayoutActor* sLatestActionLayout = nullptr;
    static s32 sLatestActionWorldId = -1;
    static s32 sLatestActionIndex = -1;
    static const al::LayoutActor* sActiveDirectShineListLayout = nullptr;
    static CommonVerticalList* sActiveShineList = nullptr;
    static al::LayoutActor* sActiveDirectHackListLayout = nullptr;
    static al::LayoutActor* sActiveDirectSouvenirListLayout = nullptr;
    static MenuSelectParts* sActiveMoonMenuSelect = nullptr;
    static const char* sLoggedListNames[24] = {};
    static s32 sLoggedListNameCount = 0;
    static const char* sLoggedMenuSelectNames[24] = {};
    static s32 sLoggedMenuSelectNameCount = 0;
    static FooterParts* sLatestFooter = nullptr;
    static bool sLoggedDirectShineList = false;
    static bool sIsDirectShineListView = false;
    // updateWorldInfo can run while the collection-state transition has already
    // cleared the broader direct-view flag. Keep the exact world scoped over
    // that native call so every per-row hook still resolves the page's roster
    // instead of falling through to GameDataFile::findShine. This includes the
    // synthetic pages which borrow a real world id: without the exact scope, a
    // transition-time rebuild of the Subarea Keys page can repopulate its Cap or
    // Moon slot with the kingdom's reallocated moons.
    static s32 sUpdatingShineListWorld = -1;
    static bool sIsDirectHackListView = false;
    static bool sIsDirectSouvenirListView = false;
    static bool sLoggedDirectHackList = false;
    static bool sLoggedDirectSouvenirList = false;
    static bool sIsPollingConfirmInput = false;
    static bool sConfirmIsClosing = false;
    static s32 sConfirmInputCooldown = 0;
    // Set whenever setDirectShineListViewActive(false) fires, so a decide/buy
    // press thrown right after a close can't open a fresh confirmation on a
    // selection that's already gone. Counted down once per frame in
    // stageSceneControlHook, which runs unconditionally every frame.
    static s32 sSuppressNewConfirmationFrames = 0;
    // One A press reaches tryHandleListHintInput more than once on the Capture
    // and Ability lists: pollHackListActionInput / pollSouvenirListActionInput
    // each run from both the list's own control hook and the collection-list
    // state hook, and isTriggerUiDecide can add another. Hint and Buy absorb the
    // repeats through hasPendingConfirmation, but opening the route panel is a
    // toggle - a second call in the same frame closes what the first opened. Own
    // the press for a couple of frames instead. Counted down in
    // stageSceneControlHook alongside sSuppressNewConfirmationFrames.
    static s32 sRouteViewInputCooldown = 0;
    static constexpr s32 cRouteViewInputCooldownFrames = 2;

    static bool takeRouteViewInput() {
        if (sRouteViewInputCooldown > 0)
            return false;
        sRouteViewInputCooldown = cRouteViewInputCooldownFrames;
        return true;
    }

    static s32 sPendingCollectionListCloseFrames = 0;
    static s32 sPendingShineListRefreshFrames = 0;
    static const al::LayoutActor* sPendingShineListRefreshLayout = nullptr;
    static s32 sPendingShineListRefreshWorldId = -1;
    static s32 sPendingShineListRefreshIndex = -1;
    static s32 sPendingShineListRefreshTop = -1;
    struct PendingMoonListRestore {
        bool pending = false;
        s32 worldId = -1;
        s32 index = -1;
        s32 top = -1;
    };
    static PendingMoonListRestore sPendingMoonListRestore;
    static s32 sPendingShineCountAnimFrames = 0;
    static const ShineDB::ShineEntry* sPendingBoughtMoonDemoEntry = nullptr;
    static bool sPendingBoughtMoonOverlay = false;
    static al::ActorInitInfo sLatestShineActorInitInfo;
    static bool sHasLatestShineActorInitInfo = false;
    static Shine* sBoughtMoonDemoShine = nullptr;
    static al::LiveActor* sBoughtMoonDemoGetActor = nullptr;
    static al::LiveActor* sBoughtMoonDemoVisualActor = nullptr;
    static ShineInfo sBoughtMoonDemoShineInfo;
    static bool sSkippedBoughtPowerStarDemoRumble = false;
    static s32 sBoughtPowerStarDemoGetFirstFrames = 0;
    struct BoughtMoonDemoShineState {
        bool valid = false;
        Shine* shine = nullptr;
        bool isGotShine = false;
        ShineInfo* curShineInfo = nullptr;
        bool hadCurShineInfo = false;
        ShineInfo curShineInfoCopy;
        ShineType modelType = Normal;
        bool isPowerStar = false;
        bool isHintPhoto = false;
        ChangeStageInfo* changeStageInfo = nullptr;
        void* demoCamera = nullptr;
        bool isUseDemoCam = false;
        bool isUseAppearDemoForce = false;
    };

    static BoughtMoonDemoShineState sBoughtMoonDemoOriginalState;

    static constexpr uintptr_t cGetWorldIdForShineListOffset = 0x52b0e4;
    static constexpr uintptr_t cGameDataFileFindShineOffset = 0x523d54;
    static constexpr uintptr_t cGameDataFileIsGotShineWorldIndexOffset = 0x523e10;
    static constexpr uintptr_t cGameDataFileGetWorldTotalShineNumMaxOffset = 0x5241d8;
    static constexpr uintptr_t cFindShineGetTimeOffset = 0x52c0b4;
    static constexpr uintptr_t cCheckMoonRockShineForShineListOffset = 0x52c0fc;
    static constexpr uintptr_t cCheckUnlockHintByHintNpcForShineListOffset = 0x52c144;
    static constexpr uintptr_t cCheckUnlockHintByAmiiboForShineListOffset = 0x52c188;
    static constexpr uintptr_t cCheckShineSeaOfTreeForShineListOffset = 0x52c1cc;
    static constexpr uintptr_t cCalcShineNumInOneShineOffset = 0x52bd98;
    static constexpr uintptr_t cIsOpenShineNameOffset = 0x52bcf0;
    static constexpr uintptr_t cGetWorldTotalShineNumOffset = 0x52c268;
    static constexpr uintptr_t cCalcWorldNumForShineListOffset = 0x52b128;
    static constexpr uintptr_t cUnlockHintAddByMoonRockOffset = 0x522050;
    // CommonVerticalList::initData(int). This is not available through the
    // symbol map, so custom list pages call the game's exact reset sequence
    // through its main-module offset.
    static constexpr uintptr_t cCommonVerticalListInitDataOffset = 0x1e25cc;
    // CommonVerticalList::addGroupAnimData(const sead::FixedSafeString<64>*, const char*).
    // Declared in CommonVerticalList.h (matching the real class layout) but, like
    // tryChangeNextStageWithDemoWorldWarp above, not in syms/main.sym, so
    // installAtSym fails to link; called by raw offset instead. Confirmed via
    // ida_context/Layout/CommonVerticalList.md.
    static constexpr uintptr_t cCommonVerticalListAddGroupAnimDataOffset = 0x1e2920;
    static constexpr uintptr_t cShineListUpdateWorldInfoOffset = 0x203ffc;
    static constexpr uintptr_t cShineListControlOffset = 0x2052f8;
    static constexpr uintptr_t cShineListIsEnableInputOffset = 0x2057d4;
    static constexpr uintptr_t cShineListDecideOffset = 0x206a4c;
    static constexpr uintptr_t cShineListCancelOffset = 0x206a94;
    static constexpr ptrdiff_t cShineListCommonVerticalListOffset = 0x130;
    static constexpr ptrdiff_t cShineListSelectedWorldIdxOffset = 0x1ac;
    static constexpr ptrdiff_t cShineListTopIndexArrayOffset = 0x1b8;
    static constexpr ptrdiff_t cShineListSelectedIndexArrayOffset = 0x1c0;
    // The page's two header counters, moons then regional coins, immediately
    // before the footer - updateWorldInfo drives both through
    // rs::setCounterAndDenominator.
    static constexpr ptrdiff_t cShineListShineCounterLayoutOffset = 0x1e8;
    static constexpr ptrdiff_t cShineListFooterLayoutOffset = 0x1f8;
    static constexpr uintptr_t cHackListControlOffset = 0x1ed2cc;
    static constexpr uintptr_t cHackListUpdatePartsDataOffset = 0x1ec648;
    static constexpr uintptr_t cHackListCancelOffset = 0x1ed754;
    static constexpr ptrdiff_t cHackListCommonHorizontalListOffset = 0x130;
    static constexpr ptrdiff_t cHackListEntryArrayOffset = 0x138;
    static constexpr ptrdiff_t cHackListCountOffset = 0x140;
    static constexpr ptrdiff_t cHackListNameDataOffset = 0x150;
    static constexpr ptrdiff_t cHackListTutorialDataOffset = 0x158;
    static constexpr ptrdiff_t cHackListEnableDataOffset = 0x160;
    static constexpr ptrdiff_t cHackListGuideLayoutOffset = 0x188;
    static constexpr ptrdiff_t cHackListFooterLayoutOffset = 0x190;
    static constexpr ptrdiff_t cHackListSceneObjHolderOffset = 0x38;
    static constexpr ptrdiff_t cCommonHorizontalListSelectedIdxOffset = 0x3c;
    static constexpr ptrdiff_t cCommonHorizontalListStartIdxOffset = 0x40;
    static constexpr ptrdiff_t cCommonHorizontalListPartsNumOffset = 0x18;
    static constexpr ptrdiff_t cCommonHorizontalListPartsArrayOffset = 0x20;
    static constexpr ptrdiff_t cCollectionStateShineListLayoutOffset = 0x20;
    static constexpr ptrdiff_t cCollectionStateHackListLayoutOffset = 0x28;
    static constexpr ptrdiff_t cCollectionStateSouvenirListLayoutOffset = 0x30;
    // ShineListLayout::exeAppear only calls updateWorldInfo() (the function
    // that recomputes a kingdom's row data, e.g. post-Moon-Rock rows) on its
    // first tick if this byte is already set; exeWorldRoll (paging between
    // kingdoms) calls updateWorldInfo() unconditionally instead. Left false,
    // entering a kingdom's list fresh from the map shows whatever row data
    // was last computed (often stale/short), which only "fixes itself" once
    // the player pages away and back and hits the unconditional WorldRoll
    // path. Confirmed via ShineListLayout::exeAppear's disassembly.
    static constexpr ptrdiff_t cShineListForceWorldInfoUpdateFlagOffset = 0x1e1;
    static constexpr uintptr_t cSouvenirListUpdatePartsDataOffset = 0x20b134;
    static constexpr uintptr_t cSouvenirListControlOffset = 0x20be10;
    static constexpr uintptr_t cSouvenirListCancelOffset = 0x20c28c;
    static constexpr ptrdiff_t cSouvenirListCommonHorizontalListOffset = 0x130;
    static constexpr ptrdiff_t cSouvenirListCountOffset = 0x138;
    static constexpr ptrdiff_t cSouvenirListNumberDataOffset = 0x140;
    static constexpr ptrdiff_t cSouvenirListNameDataOffset = 0x148;
    static constexpr ptrdiff_t cSouvenirListTutorialDataOffset = 0x150;
    static constexpr ptrdiff_t cSouvenirListEnableDataOffset = 0x158;
    static constexpr ptrdiff_t cSouvenirListTextureDataOffset = 0x160;
    // al::createTextureInfo(archiveDir, archiveName, textureName). Not in
    // syms/main.sym. It ends in operator new, so it may only be called where a
    // sead heap is current - see prepareAbilityIconTextures.
    static constexpr uintptr_t cCreateTextureInfoOffset = 0x008b48cc;
    static constexpr ptrdiff_t cSouvenirListCounterLayoutOffset = 0x178;
    static constexpr ptrdiff_t cSouvenirListGuideLayoutOffset = 0x180;
    static constexpr ptrdiff_t cSouvenirListFooterLayoutOffset = 0x188;
    static constexpr ptrdiff_t cSouvenirListSceneObjHolderOffset = 0x38;
    static constexpr uintptr_t cStageSceneStateCollectionListExeViewStageMapOffset = 0x4da2ac;
    static constexpr uintptr_t cStageSceneStateCollectionListExeViewShineListOffset = 0x4da460;
    static constexpr uintptr_t cStageSceneStateCollectionListExeViewHackListOffset = 0x4da880;
    static constexpr uintptr_t cStageSceneStateCollectionListExeViewSouvenirListOffset = 0x4daaf0;
    static constexpr uintptr_t cStageSceneStateCollectionListExeViewBgmListOffset = 0x4dad60;
    static constexpr uintptr_t cStageSceneControlOffset = 0x4cc348;
    static constexpr uintptr_t cStageSceneExeCollectionListOffset = 0x4d07b4;
    static constexpr uintptr_t cStageSceneExeDemoShineGetOffset = 0x4cf1ac;
    static constexpr uintptr_t cStageSceneStateGetShineExeDemoGetFirstOffset = 0x4dbd7c;
    static constexpr uintptr_t cStageSceneNrvCollectionListOffset = 0x1d8cb70;
    static constexpr uintptr_t cStageSceneNrvDemoShineGetOffset = 0x1d8cb90;
    static constexpr uintptr_t cStageSceneNrvDemoShineGrandGetOffset = 0x1d8cbd8;
    static constexpr uintptr_t cStageSceneStateGetShineNrvDemoGetOffset = 0x1d8dc38;
    static constexpr ptrdiff_t cStageSceneStateGetShinePowerStarActorOffset = 0x48;
    static constexpr uintptr_t cRsGetDemoShineActorOffset = 0x4d3404;
    static constexpr uintptr_t cRsRequestStartDemoShineGetOffset = 0x54feb8;
    static constexpr uintptr_t cRsRequestStartDemoShineGrandGetOffset = 0x550168;
    static constexpr uintptr_t cRsSetCounterAndDenominatorOffset = 0x2012d8;
    // These root-pane helpers are present in the game binary but absent from
    // syms/main.sym, so call them by their verified main-module offsets.
    //
    // Deliberately the NoRecursive pair. al::showPaneRoot forwards to
    // al::showPaneRecursive (ida_context/Library/Layout/LayoutActorUtil.md),
    // which force-shows EVERY pane in the layout - including panes the .bflyt
    // authored as hidden. Re-showing a counter that way revealed a leftover
    // hidden text pane inside the counter parts (a constant "オデッセイ号"
    // drawn ahead of the real coin/moon glyph on every kingdom page). The
    // NoRecursive variants only flip the root pane's own visibility bit, so a
    // hide/show round-trip leaves every child's authored visibility intact -
    // and hiding the root still hides the whole subtree visually.
    static constexpr uintptr_t cShowPaneRootOffset = 0x8b2be4;
    static constexpr uintptr_t cHidePaneRootOffset = 0x8b2c14;
    static constexpr uintptr_t cUpdateLayoutPaneRecursiveOffset = 0x8b4a60;
    static constexpr uintptr_t cCommonHorizontalListInitDataNoResetSelectedOffset = 0x1df704;
    static constexpr uintptr_t cCommonHorizontalListAddStringDataOffset = 0x1df8a4;
    static constexpr uintptr_t cCommonHorizontalListSetImageDataOffset = 0x1df900;
    static constexpr uintptr_t cCommonHorizontalListSetEnableDataOffset = 0x1df928;
    static constexpr uintptr_t cCommonHorizontalListUpdatePartsOffset = 0x1dff3c;
    static constexpr uintptr_t cSetPaneLocalAlphaOffset = 0x8b2830;
    static constexpr uintptr_t cLayoutTryStartActionOffset = 0x8b0dc0;
    static constexpr uintptr_t cActionPadAndCameraStartPadRumbleOffset = 0xa3888c;
    static constexpr s32 cPendingCollectionListCloseFrameMax = 90;
    static constexpr s32 cPendingShineListRefreshFrameMax = 4;
    static constexpr s32 cSuppressNewConfirmationFrameMax = 10;
    static constexpr s32 cPendingShineCountAnimFrameMax = 300;
    static constexpr s32 cBoughtPowerStarDemoGetFirstFrameMax = 180;
    static constexpr s32 cAbilityLockedIconAlpha = 88;
    static constexpr s32 cAbilityUnlockedIconAlpha = 255;
    static constexpr uintptr_t cWindowConfirmSetTxtMessageOffset = 0x99b3b0;
    static constexpr uintptr_t cWindowConfirmSetTxtListOffset = 0x99b3cc;
    static constexpr uintptr_t cWindowConfirmSetListNumOffset = 0x99b404;
    static constexpr uintptr_t cWindowConfirmSetCancelIdxOffset = 0x99b42c;
    static constexpr uintptr_t cWindowConfirmAppearWithChoicingCancelOffset = 0x99b6c0;
    static constexpr uintptr_t cWindowConfirmIsNerveEndOffset = 0x99b788;
    static constexpr uintptr_t cWindowConfirmTryUpOffset = 0x99b870;
    static constexpr uintptr_t cWindowConfirmTryDownOffset = 0x99b8d8;
    static constexpr uintptr_t cWindowConfirmTryDecideOffset = 0x99b944;
    static constexpr uintptr_t cWindowConfirmTryCancelOffset = 0x99ba54;
    static constexpr uintptr_t cRsIsTriggerUiDecideOffset = 0x575ba8;
    static constexpr uintptr_t cRsIsTriggerUiCancelOffset = 0x575c40;
    static constexpr uintptr_t cRsIsTriggerUiUpOffset = 0x575ee0;
    static constexpr uintptr_t cRsIsTriggerUiDownOffset = 0x575f5c;

    static void refreshFooterPrompt();
    static void applyShineListFooterText(const al::LayoutActor* layout);
    static void applyHackListFooterText(const al::LayoutActor* layout);
    static void applySouvenirListFooterText(const al::LayoutActor* layout);
    static s32 inferCurrentShineListWorld(const al::LayoutActor* layout);
    static void resetCustomShineListViewCaches(al::LayoutActor* layout);
    static void setDirectShineListViewActive(bool active);
    static void setDirectHackListViewActive(bool active, al::LayoutActor* layout = nullptr);
    static void setDirectSouvenirListViewActive(bool active, al::LayoutActor* layout = nullptr);
    static bool tryGetCurrentSelection(const al::LayoutActor** outLayout, s32* outWorldId,
        s32* outIndex, CommonVerticalList** outList);
    static bool tryGetCurrentCaptureSelection(const al::LayoutActor** outLayout, s32* outIndex,
        const CaptureRando::CaptureEntry** outCapture);
    static bool tryGetCurrentAbilitySelection(const al::LayoutActor** outLayout, s32* outIndex,
        const AbilityRando::AbilityEntry** outAbility);
    static bool hasPendingConfirmation();
    static void hideRouteHintView();
    static void updateRouteHintView();
    static void updateConfirmationDialog();
    static void updatePendingShineListRefresh(const al::LayoutActor* layout);
    static void restorePendingMoonListSelection(const al::LayoutActor* layout);
    static bool tryStartBoughtMoonShineGetDemo(StageScene* scene,
        const ShineDB::ShineEntry* entry);
    static const ShineDB::ShineEntry* findCaptureUnlockMoon(
        const CaptureRando::CaptureEntry& capture);
    static const ShineDB::ShineEntry* findAbilityUnlockMoon(
        const AbilityRando::AbilityEntry& ability);
    static const ShineDB::ShineEntry* findAbilityUnlockMoon(
        const al::LayoutActor* layout, const AbilityRando::AbilityEntry& ability);
    static PurchaseResult buyRouteHint(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyCaptureRouteHint(const al::LayoutActor* layout, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyAbilityRouteHint(const al::LayoutActor* layout, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyCaptureHint(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyCapture(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyAbilityHint(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);
    static PurchaseResult buyAbility(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote = nullptr);

    struct HackListEntry {
        sead::FixedSafeString<64>* hackName;
        bool unlocked;
        u8 padding[7];
    };

    static_assert(sizeof(HackListEntry) == 0x10, "HackListEntry size");

    static bool isBoughtMoonDemoActor(const al::LiveActor* actor) {
        if (!actor || !sBoughtMoonDemoShine)
            return false;

        return actor == sBoughtMoonDemoGetActor || actor == sBoughtMoonDemoVisualActor || actor == sBoughtMoonDemoShine || actor == sBoughtMoonDemoShine->mModelEmpty || actor == sBoughtMoonDemoShine->mModelShine;
    }

    static void saveBoughtMoonDemoShineState(Shine* shine) {
        if (!shine)
            return;

        sBoughtMoonDemoOriginalState.valid = true;
        sBoughtMoonDemoOriginalState.shine = shine;
        sBoughtMoonDemoOriginalState.isGotShine = shine->mIsGotShine;
        sBoughtMoonDemoOriginalState.curShineInfo = shine->curShineInfo;
        sBoughtMoonDemoOriginalState.hadCurShineInfo = shine->curShineInfo != nullptr;
        if (shine->curShineInfo)
            sBoughtMoonDemoOriginalState.curShineInfoCopy = *shine->curShineInfo;
        sBoughtMoonDemoOriginalState.modelType = shine->mModelType;
        sBoughtMoonDemoOriginalState.isPowerStar = shine->mIsPowerStar;
        sBoughtMoonDemoOriginalState.isHintPhoto = shine->mIsHintPhoto;
        sBoughtMoonDemoOriginalState.changeStageInfo = shine->mChangeStageInfo;
        sBoughtMoonDemoOriginalState.demoCamera = shine->_300;
        sBoughtMoonDemoOriginalState.isUseDemoCam = shine->mIsUseDemoCam;
        sBoughtMoonDemoOriginalState.isUseAppearDemoForce = shine->mIsUseAppearDemoForce;
    }

    static void restoreBoughtMoonDemoShineState() {
        if (!sBoughtMoonDemoOriginalState.valid)
            return;

        Shine* shine = sBoughtMoonDemoOriginalState.shine;
        if (shine) {
            shine->mIsGotShine = sBoughtMoonDemoOriginalState.isGotShine;
            if (sBoughtMoonDemoOriginalState.hadCurShineInfo && sBoughtMoonDemoOriginalState.curShineInfo)
                *sBoughtMoonDemoOriginalState.curShineInfo = sBoughtMoonDemoOriginalState.curShineInfoCopy;
            shine->curShineInfo = sBoughtMoonDemoOriginalState.curShineInfo;
            shine->mModelType = sBoughtMoonDemoOriginalState.modelType;
            shine->mIsPowerStar = sBoughtMoonDemoOriginalState.isPowerStar;
            shine->mIsHintPhoto = sBoughtMoonDemoOriginalState.isHintPhoto;
            shine->mChangeStageInfo = sBoughtMoonDemoOriginalState.changeStageInfo;
            shine->_300 = sBoughtMoonDemoOriginalState.demoCamera;
            shine->mIsUseDemoCam = sBoughtMoonDemoOriginalState.isUseDemoCam;
            shine->mIsUseAppearDemoForce = sBoughtMoonDemoOriginalState.isUseAppearDemoForce;
        }

        sBoughtMoonDemoOriginalState = {};
    }

    static void forgetBoughtMoonDemoShineState() {
        sBoughtMoonDemoOriginalState = {};
    }

    static void clearBoughtMoonDemoState(bool restoreShineState) {
        if (restoreShineState)
            restoreBoughtMoonDemoShineState();
        else
            forgetBoughtMoonDemoShineState();

        sBoughtMoonDemoShine = nullptr;
        sBoughtMoonDemoGetActor = nullptr;
        sBoughtMoonDemoVisualActor = nullptr;
        sSkippedBoughtPowerStarDemoRumble = false;
        sBoughtPowerStarDemoGetFirstFrames = 0;
    }

    // ChangeStageInfo's string members are sead::FixedSafeString<0x80>, each laid out as
    //   +0x00 vtable, +0x08 mStringTop, +0x10 mBufferSize, +0x14 inline buffer (0x80 bytes),
    // i.e. 0x98 bytes per string. A well-formed string points mStringTop into its own inline
    // buffer. `stringOffset` is the offset of the FixedSafeString member itself.
    static bool isRawChangeStageFixedStringStorageSane(
        const ChangeStageInfo* info, ptrdiff_t stringOffset) {
        static constexpr ptrdiff_t cFixedSafeStringSize = 0x98;
        static constexpr ptrdiff_t cStringTopFieldOffset = 0x8;

        const uintptr_t objectStart = reinterpret_cast<uintptr_t>(info) + stringOffset;
        const uintptr_t objectEnd = objectStart + cFixedSafeStringSize;
        const uintptr_t stringTop = *reinterpret_cast<const uintptr_t*>(objectStart + cStringTopFieldOffset);
        return stringTop >= objectStart && stringTop < objectEnd;
    }

    static bool isChangeStageInfoStorageSane(const ChangeStageInfo* info) {
        if (!info)
            return false;

        return isRawChangeStageFixedStringStorageSane(info, 0x0) && // mChangeStageId
            isRawChangeStageFixedStringStorageSane(info, 0x98) && // mChangeStageName
            isRawChangeStageFixedStringStorageSane(info, 0x130) && // mPlacementString
            isRawChangeStageFixedStringStorageSane(info, 0x1d8); // mWipeType
    }

    enum class SelectionActionState
    {
        None,
        HintAndBuy,
        BuyOnly,
        HintOnly,
        CannotBuy,
    };

    // Extended Hints layer over the row above. A route only ever becomes
    // relevant once the location hint has already been bought, so it takes over
    // the A button exactly when the hint action has finished using it - the
    // player never has to pick between the two.
    enum class PathActionState
    {
        None,
        Buy,
        View,
    };

    enum class ConfirmAction
    {
        None,
        Hint,
        Buy,
        Path,
        Message,
    };

    enum class ConfirmTarget
    {
        None,
        Shine,
        Capture,
        Ability,
    };

    struct PendingConfirmation {
        ConfirmAction action = ConfirmAction::None;
        ConfirmTarget target = ConfirmTarget::None;
        const al::LayoutActor* layout = nullptr;
        s32 worldId = -1;
        s32 index = -1;
        Randomizer::HintPriceCategory category = Randomizer::HintPriceCategory::Moons;
        Randomizer::HintPriceQuote quote {};
    };

    static PendingConfirmation sPendingConfirmation;

    // The collection list normally exposes only the 14 main kingdoms.  Its
    // hidden slots are reused for the optional Toadette Medals and Unlocked
    // Paintings pages. Keep the enabled special pages contiguous so either
    // setting can be enabled without creating a blank page before it.
    static constexpr s32 cNormalShineListWorldCount = 14;

    static s32 getSpecialShineListWorldId(s32 pageIndex) {
        if (pageIndex < cNormalShineListWorldCount)
            return -1;

        s32 nextPage = cNormalShineListWorldCount;
        if (Randomizer::isToadetteMedalModeEnabled() && pageIndex == nextPage++)
            return WorldMeta::WorldId_Mushroom;
        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled() && pageIndex == nextPage++)
            return WorldMeta::WorldId_Dark;
        if (Randomizer::isMoonRockKeyEnabled() && pageIndex == nextPage++)
            return WorldMeta::WorldId_Darker;
        return -1;
    }

    static s32 getShineListWorldCount() {
        s32 count = cNormalShineListWorldCount;
        if (Randomizer::isToadetteMedalModeEnabled())
            count++;
        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            count++;
        if (Randomizer::isMoonRockKeyEnabled())
            count++;
        return count;
    }

    // Whether a per-row native query is being asked about a page this file is
    // currently driving. The direct-view flag covers the list once it is the
    // active view; sUpdatingShineListWorld covers the updateWorldInfo passes
    // that run outside that window - the one ShineListLayout's constructor
    // makes, and the ones a collection-state transition makes after the flag
    // has already been cleared. Every per-row hook must use the same test,
    // because getWorldTotalShineNumHook sizes the page with it: a hook that
    // disagrees gets handed row indices the native data cannot resolve (see
    // isOpenShineNameHook).
    static bool isShineListPageContextActive(s32 worldId) {
        return sIsDirectShineListView || sUpdatingShineListWorld == worldId;
    }

    static bool isPaintingListWorld(s32 worldId) {
        return worldId == WorldMeta::WorldId_Dark
            && Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled();
    }

    static bool isMoonRockKeyListWorld(s32 worldId) {
        return worldId == WorldMeta::WorldId_Darker
            && Randomizer::isMoonRockKeyEnabled();
    }

    // Subarea Keys ride on Cap and/or Moon Kingdom moons, and all three hidden
    // world slots are already spoken for (Toadette Medals, Unlocked Paintings,
    // Moon Rock Keys). Rather than invent a fourth, the key list takes over one
    // host kingdom's own page and every leftover moon - that kingdom's and the
    // co-host's - is listed together on the other one. With a single host the
    // co-host page simply gains the leftovers, so one rule covers all three
    // host settings.
    //
    // Both pages therefore piggyback a real, visible kingdom slot exactly like
    // the Toadette Medals page does with Mushroom; see the warning above
    // isToadetteMedalListWorld and keep every worldId-keyed native query in
    // step with it.
    static s32 subareaKeyListWorldId() {
        // No slots means nothing to list: the feature is off, or moon rando is
        // not built, or the host cannot carry this mode.
        if (!Randomizer::isSubareaUnlockEnabled() || MoonRando::getSubareaKeySlotCount() <= 0)
            return -1;
        return Randomizer::getSubareaUnlockHost() == Randomizer::SubareaUnlockHost_Cap
            ? WorldMeta::WorldId_Cap
            : WorldMeta::WorldId_Moon;
    }

    static s32 subareaMergedMoonWorldId() {
        const s32 keysWorld = subareaKeyListWorldId();
        if (keysWorld < 0)
            return -1;
        return keysWorld == WorldMeta::WorldId_Cap ? WorldMeta::WorldId_Moon
                                                   : WorldMeta::WorldId_Cap;
    }

    static bool isSubareaKeyListWorld(s32 worldId) {
        const s32 keysWorld = subareaKeyListWorldId();
        return keysWorld >= 0 && worldId == keysWorld;
    }

    static bool isSubareaMergedMoonWorld(s32 worldId) {
        const s32 mergedWorld = subareaMergedMoonWorldId();
        return mergedWorld >= 0 && worldId == mergedWorld;
    }

    static s32 getSubareaKeyCount() {
        const s32 count = MoonRando::getSubareaKeySlotCount();
        return count > 0 ? count : 0;
    }

    // The Toadette Medals page piggybacks on the real Mushroom Kingdom world
    // slot (see getSpecialShineListWorldId / findHintByWorldIndex), so unlike
    // the Dark/Darker Side pages it shares a worldId with a real, otherwise
    // unrelated vanilla kingdom. Every native per-row query that keys off
    // worldId must treat it the same as the other custom list pages while
    // this page is showing, or it falls through to vanilla data sized for
    // Mushroom's single real HintInfo entry (the Power Star) and null-derefs
    // once the cursor reaches a medal index beyond that.
    static bool isToadetteMedalListWorld(s32 worldId) {
        return worldId == WorldMeta::WorldId_Mushroom
            && Randomizer::isToadetteMedalModeEnabled();
    }

    static bool isRandomizedKingdomMoonListWorld(s32 worldId) {
        return worldId >= WorldMeta::WorldId_Cap
            && worldId <= WorldMeta::WorldId_Moon;
    }

    static bool shouldExposeFullRandomizedMoonList(s32 worldId) {
        // The Subarea Key page occupies a real kingdom slot but is not a moon
        // list at all. Excluding it here keeps every caller below (row lookup,
        // totals, get-time, hint state) off the randomized-moon path in one
        // place, instead of each having to test for it before that path wins.
        if (isSubareaKeyListWorld(worldId))
            return false;
        return isShineListPageContextActive(worldId)
            && Randomizer::isMoonRandoEnabled()
            && MoonRando::isInitialized()
            && isRandomizedKingdomMoonListWorld(worldId);
    }

    static s32 getMoonRockKeyKingdomAt(s32 index) {
        if (index < 0)
            return -1;

        s32 found = 0;
        // These are deliberately listed in vanilla kingdom order (Cap through
        // Moon), rather than in the randomized location order.
        for (s32 kingdom = WorldMeta::WorldId_Cap; kingdom <= WorldMeta::WorldId_Moon; kingdom++) {
            if (!MoonRando::kingdomHasMoonRockKey(kingdom))
                continue;
            if (found++ == index)
                return kingdom;
        }
        return -1;
    }

    static s32 getMoonRockKeyCount() {
        s32 count = 0;
        while (getMoonRockKeyKingdomAt(count) >= 0)
            count++;
        return count;
    }

    // The Toadette Medals page piggybacks on the real (but otherwise hidden)
    // Mushroom Kingdom world slot (see calcWorldNumForShineListHook). Its rows
    // don't correspond to Mushroom's own real hints - each row index maps to
    // the index-th medal (MoonRando::getToadetteMedalAt), and its persistent
    // hint-purchase state is the medal's own real HintInfo entry (found by
    // identity, wherever it actually lives - mirrors findShopHintForStage's
    // linear scan in MoonHooks.cpp), never Mushroom's real array content.
    //
    // Every custom page resolves one of these per row per frame, so the old
    // 0x400-slot strcmp walk was ~65k string compares a frame on a full page.
    // The slot a shine identity lives in is fixed for the life of a file
    // (initializeHintList writes it once), so it is indexed alongside the
    // kingdom cache below and shares its invalidation.
    static void ensureHintKingdomCache(const GameDataFile* file);
    static s16 sHintSlotByShineTableIndex[ShineDB::sShineTableSize];

    // ShineDB::findEntry / findIndex both walk the 775-entry table with a
    // strcmp per row. Everything below asks for the same handful of identities
    // over and over, so index them once. Open addressing on the uid, with the
    // stage still compared on a hit, so a uid shared by two stages resolves the
    // same way findEntry does (lowest table index wins).
    static constexpr s32 cShineTableHashCapacity = 2048;
    static_assert((cShineTableHashCapacity & (cShineTableHashCapacity - 1)) == 0,
        "shine table hash capacity must be a power of two");
    static_assert(cShineTableHashCapacity >= ShineDB::sShineTableSize * 2,
        "shine table hash must stay well under half full");
    static s16 sShineTableIndexByUidSlot[cShineTableHashCapacity];
    static bool sShineTableIndexHashReady = false;

    static u32 hashShineUid(s32 uid) {
        const u32 hash = static_cast<u32>(uid) * 2654435761u;
        return hash ^ (hash >> 15);
    }

    static void ensureShineTableIndexHash() {
        if (sShineTableIndexHashReady)
            return;
        for (s32 i = 0; i < cShineTableHashCapacity; i++)
            sShineTableIndexByUidSlot[i] = -1;
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++) {
            s32 slot = static_cast<s32>(hashShineUid(ShineDB::sShineTable[i].uid)
                & static_cast<u32>(cShineTableHashCapacity - 1));
            while (sShineTableIndexByUidSlot[slot] >= 0)
                slot = (slot + 1) & (cShineTableHashCapacity - 1);
            sShineTableIndexByUidSlot[slot] = static_cast<s16>(i);
        }
        sShineTableIndexHashReady = true;
    }

    static s32 findShineTableIndexByIdentity(s32 uid, const char* stage) {
        if (!stage)
            return -1;
        ensureShineTableIndexHash();
        s32 slot = static_cast<s32>(hashShineUid(uid)
            & static_cast<u32>(cShineTableHashCapacity - 1));
        for (s32 probe = 0; probe < cShineTableHashCapacity; probe++) {
            const s32 index = sShineTableIndexByUidSlot[slot];
            if (index < 0)
                return -1;
            const ShineDB::ShineEntry& candidate = ShineDB::sShineTable[index];
            if (candidate.uid == uid && std::strcmp(candidate.stage, stage) == 0)
                return index;
            slot = (slot + 1) & (cShineTableHashCapacity - 1);
        }
        return -1;
    }

    static s32 shineTableIndexOf(const ShineDB::ShineEntry* entry) {
        if (!entry)
            return -1;
        // sShineTable is `static constexpr` in a header, so every translation
        // unit holds its own copy. A pointer minted here is just an offset; one
        // handed over from MoonRando (medals, keys) points into that unit's
        // copy instead and has to come back through the identity index.
        const uintptr_t tableBegin = reinterpret_cast<uintptr_t>(&ShineDB::sShineTable[0]);
        const uintptr_t address = reinterpret_cast<uintptr_t>(entry);
        if (address >= tableBegin) {
            const uintptr_t byteOffset = address - tableBegin;
            if (byteOffset < sizeof(ShineDB::sShineTable)
                && byteOffset % sizeof(ShineDB::ShineEntry) == 0)
                return static_cast<s32>(byteOffset / sizeof(ShineDB::ShineEntry));
        }
        return findShineTableIndexByIdentity(entry->uid, entry->stage);
    }

    static GameDataFile::HintInfo* findHintInfoForEntry(GameDataFile* file, const ShineDB::ShineEntry* entry) {
        if (!file || !entry)
            return nullptr;

        ensureHintKingdomCache(file);
        s32 tableIndex = shineTableIndexOf(entry);
        if (tableIndex >= 0 && sHintSlotByShineTableIndex[tableIndex] < 0) {
            // Slots are recorded against the lowest-index entry for an
            // identity. That is this same entry unless the table ever grows an
            // exact (uid, stage) duplicate, in which case a pointer to the
            // second copy has to resolve through the first rather than report
            // an existing hint record as missing.
            tableIndex = findShineTableIndexByIdentity(entry->uid, entry->stage);
        }
        if (tableIndex < 0 || sHintSlotByShineTableIndex[tableIndex] < 0)
            return nullptr;
        return &file->getHintList()[sHintSlotByShineTableIndex[tableIndex]];
    }

    // GameDataFile::customIsGotShine resolves the same record by walking all
    // 0x400 slots with a strcmp each. The custom pages ask it once per row per
    // frame, so route them through the indexed lookup instead; the answer is
    // the identical HintInfo::isGet read.
    static bool isEntryHintGot(const GameDataFile* file, const ShineDB::ShineEntry* entry) {
        const GameDataFile::HintInfo* hint
            = findHintInfoForEntry(const_cast<GameDataFile*>(file), entry);
        return hint && hint->isGet;
    }

    // GameDataFile::findShine filters post-Moon-Rock entries until the active
    // scenario is that kingdom's Moon Rock scenario. Once the collection list
    // intentionally includes the full randomized pool, resolve its row index
    // against every real HintInfo record in the normal native ordering instead.
    // Kingdom each hint-list slot belongs to after redistribution (the moon->kingdom
    // reshuffle reassigns which kingdom a moon is listed/counted under). Cached for
    // the open shine-list view: getAssignedKingdom is a linear moon-table scan, so
    // recomputing it for all 0x400 hints on every navigation query was the list
    // slowdown. Built once per view-open (keyed on the file and role layout), then
    // O(1) per lookup.
    static s8 sHintKingdomCache[0x400];
    // Row -> hint-slot index for every page, laid out back to back in
    // sHintPageSlots and sliced by sHintPageStart/sHintPageCount. Resolving a
    // row used to rescan all 0x400 slots, and the native ShineListLayout asks
    // several times per visible row per frame (row text, get-time, hint state,
    // acquisition, the page total), so an open redistributed kingdom page was
    // paying six-figure slot visits a frame. Built in the same pass as the
    // kingdom cache above and read O(1) afterwards.
    static s16 sHintPageSlots[0x400];
    static s8 sHintSlotPage[0x400];
    static s16 sHintPageStart[WorldMeta::sWorldCount];
    static s16 sHintPageCount[WorldMeta::sWorldCount];
    static const GameDataFile* sHintKingdomCacheFile = nullptr;
    static u32 sHintKingdomCacheRoleLayoutHash = 0;
    // The page a slot lands on also depends on which kingdom currently hosts
    // the Subarea Key list, and that host is a setting rather than part of the
    // role layout, so it has to be part of the cache key too.
    static s32 sHintKingdomCacheKeysWorld = -2;

    static void invalidateHintKingdomCache() {
        sHintKingdomCacheFile = nullptr;
        sHintKingdomCacheRoleLayoutHash = 0;
        sHintKingdomCacheKeysWorld = -2;
        for (s32 i = 0; i < WorldMeta::sWorldCount; i++) {
            sHintPageStart[i] = 0;
            sHintPageCount[i] = 0;
        }
    }

    static bool hintSlotBelongsToPage(const GameDataFile* file, s32 slotIndex, s32 worldId);

    static void ensureHintKingdomCache(const GameDataFile* file) {
        const u32 roleLayoutHash = MoonRando::getMoonRoleLayoutHash();
        const s32 keysWorld = subareaKeyListWorldId();
        // The hint list is heap-allocated by the file's own setup. Building
        // against it before that has run would both crash and stamp a wrong
        // cache under this file pointer, so leave it cold until it exists -
        // this is reachable now that the warm-up below runs at scene init.
        if (file && !file->getHintList().begin())
            return;
        if (!file
            || (sHintKingdomCacheFile == file
                && sHintKingdomCacheRoleLayoutHash == roleLayoutHash
                && sHintKingdomCacheKeysWorld == keysWorld))
            return;
        const bool active = MoonRando::isKingdomRedistributionActive();
        for (s32 i = 0; i < ShineDB::sShineTableSize; i++)
            sHintSlotByShineTableIndex[i] = -1;
        s32 populatedSlots = 0;
        for (s32 i = 0; i < 0x400; i++) {
            const GameDataFile::HintInfo* hint = &file->getHintList()[i];
            if (hint->stageName.isEmpty()) {
                sHintKingdomCache[i] = -1;
                continue;
            }
            populatedSlots++;
            // A collection-list row represents a moon ITEM, not the physical
            // location currently holding an item.  The hint record is therefore
            // deliberately kept as the item's own identity here; the row-text
            // and hint actions use its placement to show where it can be found.
            // Resolving location -> reward at this point instead mixed those two
            // identities, so a Sand row could display (and buy a hint for) an
            // unrelated Cap reward's location.
            // Same resolution ShineDB::findEntry performs (lowest table index
            // for the identity), off the index rather than a 775-row strcmp
            // walk repeated for all 0x400 slots.
            const s32 tableIndex = findShineTableIndexByIdentity(
                hint->uniqueId, hint->stageName.cstr());
            const ShineDB::ShineEntry* item
                = tableIndex >= 0 ? &ShineDB::sShineTable[tableIndex] : nullptr;
            // Identity -> slot, for findHintInfoForEntry. First slot wins, the
            // same one its old scan would have stopped on.
            if (tableIndex >= 0 && sHintSlotByShineTableIndex[tableIndex] < 0)
                sHintSlotByShineTableIndex[tableIndex] = static_cast<s16>(i);
            s32 kingdom = active && item
                ? MoonRando::getAssignedKingdomForEntry(item)
                : hint->worldId;
            if (kingdom < 0)
                kingdom = hint->worldId;
            sHintKingdomCache[i] = static_cast<s8>(kingdom);
        }

        // Bucket the slots per page. Decided through the same predicate the row
        // lookups used to run inline, so the index cannot drift from it, but
        // asked only about the pages a slot can actually land on: its own
        // kingdom's, or - once Subarea Keys are rearranging pages - the merged
        // one. Every other page is a guaranteed no, and testing all 17 per slot
        // made this build 8x more expensive than it needed to be.
        const s32 mergedWorld = subareaMergedMoonWorldId();
        for (s32 i = 0; i < 0x400; i++) {
            sHintSlotPage[i] = -1;
            const s32 slotKingdom = sHintKingdomCache[i];
            if (slotKingdom >= 0 && slotKingdom < WorldMeta::sWorldCount
                && hintSlotBelongsToPage(file, i, slotKingdom)) {
                sHintSlotPage[i] = static_cast<s8>(slotKingdom);
                continue;
            }
            if (mergedWorld >= 0 && mergedWorld != slotKingdom
                && hintSlotBelongsToPage(file, i, mergedWorld))
                sHintSlotPage[i] = static_cast<s8>(mergedWorld);
        }

        // Counting sort into ascending-slot order per page, which is the row
        // numbering the old scan produced.
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++)
            sHintPageCount[worldId] = 0;
        for (s32 i = 0; i < 0x400; i++)
            if (sHintSlotPage[i] >= 0)
                sHintPageCount[sHintSlotPage[i]]++;
        s16 running = 0;
        s16 fill[WorldMeta::sWorldCount];
        for (s32 worldId = 0; worldId < WorldMeta::sWorldCount; worldId++) {
            sHintPageStart[worldId] = running;
            fill[worldId] = running;
            running += sHintPageCount[worldId];
        }
        for (s32 i = 0; i < 0x400; i++)
            if (sHintSlotPage[i] >= 0)
                sHintPageSlots[fill[sHintSlotPage[i]]++] = static_cast<s16>(i);

        // An allocated but not-yet-filled hint list builds a correct-but-empty
        // index. Stamping the key on that would freeze the empty answer in
        // place, since nothing in the key changes once the save fills the list
        // in. Leave it unstamped so the next call rebuilds; the tables above
        // are still valid to read in the meantime.
        if (populatedSlots == 0)
            return;

        sHintKingdomCacheFile = file;
        sHintKingdomCacheRoleLayoutHash = roleLayoutHash;
        sHintKingdomCacheKeysWorld = keysWorld;
    }

    // Which page a hint slot's moon is listed on. Normally its own kingdom's,
    // but Subarea Keys move things around: a key carrier belongs to the key
    // list instead of any kingdom page, the host kingdom's page stops showing
    // moons at all, and the merged page picks up both kingdoms' leftovers.
    static bool hintSlotBelongsToPage(const GameDataFile* file, s32 slotIndex, s32 worldId) {
        const s32 slotKingdom = sHintKingdomCache[slotIndex];
        const s32 keysWorld = subareaKeyListWorldId();
        if (keysWorld < 0)
            return slotKingdom == worldId;

        const GameDataFile::HintInfo* hint = &file->getHintList()[slotIndex];
        if (!hint->stageName.isEmpty()
            && MoonRando::isSubareaKeyItem(hint->uniqueId, hint->stageName.cstr()))
            return false;

        if (worldId == keysWorld)
            return false;
        if (worldId == subareaMergedMoonWorldId())
            return slotKingdom == worldId || slotKingdom == keysWorld;
        return slotKingdom == worldId;
    }

    static const GameDataFile::HintInfo* findFullRandomizedKingdomHint(
        const GameDataFile* file, s32 worldId, s32 index) {
        if (!file || index < 0 || index >= MoonRando::kMaxShineListRows)
            return nullptr;
        if (worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return nullptr;
        ensureHintKingdomCache(file);
        if (index >= sHintPageCount[worldId])
            return nullptr;
        return &file->getHintList()[sHintPageSlots[sHintPageStart[worldId] + index]];
    }

    // Number of moons listed under a kingdom after redistribution -- the row count
    // its page shows. Matches findFullRandomizedKingdomHint's filter exactly.
    static s32 countFullRandomizedKingdomMoons(const GameDataFile* file, s32 worldId) {
        if (!file || worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return 0;
        ensureHintKingdomCache(file);
        return sHintPageCount[worldId];
    }

    // Seed generation runs on its own worker thread and is explicitly barred
    // from touching the GameDataFile, so the hint-list-derived indices cannot be
    // built there. Build them here instead: scene init is on the game thread,
    // the file is live, and a loading screen is already covering the cost - so
    // the first collection-list open no longer pays for it. Re-running is free
    // once the cache key matches.
    void warmShineListCaches(GameDataFile* file) {
        if (!file)
            return;
        ensureShineTableIndexHash();
        ensureHintKingdomCache(file);
    }

    static s32 clampNativeShineListRows(s32 count, s32 worldId) {
        if (count <= MoonRando::kMaxShineListRows)
            return count > 0 ? count : 0;
        static bool sLoggedCapacityClamp = false;
        if (!sLoggedCapacityClamp) {
            sLoggedCapacityClamp = true;
            Randomizer::logPush(
                "shine list row capacity clamp: world=%d rows=%d capacity=%d",
                worldId, count, MoonRando::kMaxShineListRows);
        }
        return MoonRando::kMaxShineListRows;
    }

    static GameDataFile::HintInfo* findHintByWorldIndex(GameDataFile* file, s32 worldId, s32 index) {
        if (!file || worldId < 0 || index < 0)
            return nullptr;

        if (shouldExposeFullRandomizedMoonList(worldId)) {
            return const_cast<GameDataFile::HintInfo*>(
                findFullRandomizedKingdomHint(file, worldId, index));
        }

        if (worldId == WorldMeta::WorldId_Mushroom && Randomizer::isToadetteMedalModeEnabled()) {
            const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(index);
            return medal ? findHintInfoForEntry(file, medal) : nullptr;
        }

        if (isMoonRockKeyListWorld(worldId)) {
            const s32 kingdom = getMoonRockKeyKingdomAt(index);
            const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
            return key ? findHintInfoForEntry(file, key) : nullptr;
        }

        // Subarea Key rows resolve to the carrier moon's own hint record, found
        // by identity wherever it lives, so hinting one behaves like hinting any
        // other moon.
        if (isSubareaKeyListWorld(worldId)) {
            const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(index);
            return key ? findHintInfoForEntry(file, key) : nullptr;
        }

        // Painting rows are pure status information: unlike moon rows they
        // have no hint record and intentionally cannot be purchased or hinted.
        if (isPaintingListWorld(worldId))
            return nullptr;

        using Fn = GameDataFile::HintInfo* (*)(GameDataFile*, s32, s32);
        GameDataFile::HintInfo* hint = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + cGameDataFileFindShineOffset)(file, worldId, index);
        if (!hint || hint->stageName.isEmpty())
            return nullptr;
        return hint;
    }

    // Compact location kingdom names for the Moon Rock Keys page rows: the
    // list column only spans half the screen, so the constant " Kingdom"
    // suffix would push most hinted locations past the pane. The footer
    // detail line carries the full wording for the selected row.
    static const char16_t* getShortWorldName16(s32 worldId) {
        switch (worldId) {
        case WorldMeta::WorldId_Cap:
            return u"Cap";
        case WorldMeta::WorldId_Cascade:
            return u"Cascade";
        case WorldMeta::WorldId_Sand:
            return u"Sand";
        case WorldMeta::WorldId_Wooded:
            return u"Wooded";
        case WorldMeta::WorldId_Lake:
            return u"Lake";
        case WorldMeta::WorldId_Cloud:
            return u"Cloud";
        case WorldMeta::WorldId_Lost:
            return u"Lost";
        case WorldMeta::WorldId_Metro:
            return u"Metro";
        case WorldMeta::WorldId_Seaside:
            return u"Seaside";
        case WorldMeta::WorldId_Snow:
            return u"Snow";
        case WorldMeta::WorldId_Luncheon:
            return u"Luncheon";
        case WorldMeta::WorldId_Ruined:
            return u"Ruined";
        case WorldMeta::WorldId_Bowsers:
            return u"Bowser's";
        case WorldMeta::WorldId_Moon:
            return u"Moon";
        case WorldMeta::WorldId_Mushroom:
            return u"Mushroom";
        case WorldMeta::WorldId_Dark:
            return u"Dark Side";
        case WorldMeta::WorldId_Darker:
            return u"Darker Side";
        default:
            return u"???";
        }
    }

    // Row text for the Moon Rock Keys page, consumed by MoonHooks.cpp's
    // tryFindShineMessage hook so the NATIVE per-page update writes it (the
    // page title already reads "Moon Rock Keys", so rows omit the suffix):
    //   unrevealed: "Cap Kingdom"                     (faded by the row state)
    //   revealed:   "Cap Kingdom - Frog Pond (Lake)"  (hinted or collected)
    const char16_t* tryMakeMoonRockKeyRowText(GameDataHolderAccessor accessor, s32 worldId, s32 index) {
        if (!isMoonRockKeyListWorld(worldId))
            return nullptr;
        const s32 kingdom = getMoonRockKeyKingdomAt(index);
        if (kingdom < 0)
            return nullptr;

        static sead::WFixedSafeString<0x200> sRowText;
        sRowText = u"";
        sRowText.append(WorldMeta::getDisplayName16ForWorldId(kingdom));

        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
        if (!file || !key)
            return sRowText.cstr();

        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        const bool collected = isEntryHintGot(file, key);
        const bool revealed = collected
            || (hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc);
        if (!revealed)
            return sRowText.cstr();

        const MoonRando::Placement* placement = MoonRando::getPlacementForItem(key->uid, key->stage);
        const ShineDB::ShineEntry* location = placement ? placement->location : nullptr;
        if (!location || !location->name)
            return sRowText.cstr();

        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));
        sRowText.appendWithFormat(u" - %s (%s)", locationName.cstr(),
            getShortWorldName16(location->worldId));
        return sRowText.cstr();
    }

    // Row label for a Subarea Key. The mode decides how much of the pair a slot
    // names: both halves in the finest mode, one half in the coarser ones.
    const char16_t* tryMakeSubareaKeyRowText(GameDataHolderAccessor accessor, s32 worldId, s32 index) {
        if (!isSubareaKeyListWorld(worldId))
            return nullptr;

        s32 kingdom = -1;
        SubareaKeyDB::SubareaType type = EntranceDB2::SubareaType_None;
        if (!SubareaKeyDB::keySlotAt(index, &kingdom, &type))
            return nullptr;

        static sead::WFixedSafeString<0x200> sRowText;
        sRowText = u"";
        if (kingdom >= 0)
            sRowText.append(getShortWorldName16(kingdom));
        if (kingdom >= 0 && type != EntranceDB2::SubareaType_Count)
            sRowText.append(u" - ");
        if (type != EntranceDB2::SubareaType_Count)
            sRowText.append(SubareaKeyDB::typeNamePlural(type));
        else
            sRowText.append(u" Subareas");

        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(index);
        if (!file || !key)
            return sRowText.cstr();

        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        const bool collected = isEntryHintGot(file, key);
        const bool revealed = collected
            || (hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc);
        if (!revealed)
            return sRowText.cstr();

        const MoonRando::Placement* placement = MoonRando::getPlacementForItem(key->uid, key->stage);
        const ShineDB::ShineEntry* location = placement ? placement->location : nullptr;
        if (!location || !location->name)
            return sRowText.cstr();

        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));
        sRowText.appendWithFormat(u" @ %s (%s)", locationName.cstr(),
            getShortWorldName16(location->worldId));
        return sRowText.cstr();
    }

    static HkTrampoline<const GameDataFile::HintInfo*, const GameDataFile*, s32, s32>
        findShineWorldIndexHook = hk::hook::trampoline(
            [](const GameDataFile* file, s32 worldId, s32 index) -> const GameDataFile::HintInfo* {
                if (shouldExposeFullRandomizedMoonList(worldId))
                    return findFullRandomizedKingdomHint(file, worldId, index);
                // Moon Rock Key rows are backed by the key's own real HintInfo
                // record, wherever it lives. Native ShineListLayout helpers
                // (tryFindShineMessage, findShineGetTime, the map-dot pos calc)
                // dereference this result without a null check, so resolving it
                // here is what makes the native page render safe at all.
                if (isShineListPageContextActive(worldId)
                    && isMoonRockKeyListWorld(worldId)) {
                    const s32 kingdom = getMoonRockKeyKingdomAt(index);
                    const ShineDB::ShineEntry* key = kingdom >= 0
                        ? MoonRando::getMoonRockKeyForKingdom(kingdom)
                        : nullptr;
                    return key ? findHintInfoForEntry(const_cast<GameDataFile*>(file), key) : nullptr;
                }
                // Subarea Keys need exactly the same treatment, and doubly so:
                // their page borrows a REAL kingdom slot, so without this the
                // fall-through below returns that kingdom's own hint array,
                // which is shorter than the key count and hands the native
                // updateWorldInfo a null to dereference.
                if (isShineListPageContextActive(worldId)
                    && isSubareaKeyListWorld(worldId)) {
                    const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(index);
                    return key ? findHintInfoForEntry(const_cast<GameDataFile*>(file), key) : nullptr;
                }
                if (isShineListPageContextActive(worldId)
                    && isToadetteMedalListWorld(worldId)) {
                    const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(index);
                    return medal ? findHintInfoForEntry(const_cast<GameDataFile*>(file), medal) : nullptr;
                }
                return findShineWorldIndexHook.orig(file, worldId, index);
            });

    // ShineListLayout gets its collected-row styling from GameDataFile's
    // world/index query, not merely from the list's State animation data. The
    // medal page reuses the Mushroom world slot, so teach that native query to
    // resolve its fake rows to the real medal items while the direct list is
    // active. This makes the layout itself apply the normal Shine state,
    // opacity, and collection-date panes to the selected medal row.
    static HkTrampoline<bool, GameDataFile*, s32, s32> isGotShineWorldIndexHook = hk::hook::trampoline(
        [](GameDataFile* file, s32 worldId, s32 index) -> bool {
            if (shouldExposeFullRandomizedMoonList(worldId)) {
                const GameDataFile::HintInfo* hint = findFullRandomizedKingdomHint(file, worldId, index);
                if (!hint)
                    return false;

                // The full randomized list is an item roster.  Its HintInfo is
                // the item's original save identity, while its row text points
                // at the physical placement.  Test that item directly; mapping
                // this HintInfo as though it were a location makes a checked row
                // belong to whichever unrelated reward happens to be there.
                const ShineDB::ShineEntry* item = ShineDB::findEntry(
                    hint->uniqueId, hint->stageName.cstr());
                return item ? MoonHooks::isMoonAcquiredDirect(file, item) : hint->isGet;
            }
            if (isShineListPageContextActive(worldId)
                && Randomizer::isToadetteMedalModeEnabled()
                && worldId == WorldMeta::WorldId_Mushroom) {
                const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(index);
                return file && medal && MoonHooks::isMoonAcquiredDirect(file, medal);
            }
            if (isShineListPageContextActive(worldId)
                && isPaintingListWorld(worldId)) {
                if (index >= 0 && index < cMaxCustomShineListRows && sPaintingListRowsValid)
                    return sPaintingListUnlockedRows[index];
                return file && PaintingRando::isPaintingUnlocked(file, index);
            }
            if (isShineListPageContextActive(worldId)
                && isMoonRockKeyListWorld(worldId)) {
                const s32 kingdom = getMoonRockKeyKingdomAt(index);
                const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
                return file && key && isEntryHintGot(file, key);
            }
            if (isShineListPageContextActive(worldId)
                && isSubareaKeyListWorld(worldId)) {
                const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(index);
                return file && key && isEntryHintGot(file, key);
            }
            return isGotShineWorldIndexHook.orig(file, worldId, index);
        });

    // Vanilla reports only the pre-Moon-Rock row count until the matching
    // kingdom's rock is opened. The maximum count is the full pool, including
    // post-rock moons. In an active randomized moon list, expose that complete
    // pool so every item remains eligible for hints regardless of Moon Rock
    // progress; preserve the native count everywhere else.
    static HkTrampoline<s32, GameDataHolderAccessor, s32> getWorldTotalShineNumHook = hk::hook::trampoline(
        [](GameDataHolderAccessor accessor, s32 worldId) -> s32 {
            if (shouldExposeFullRandomizedMoonList(worldId)) {
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
                if (file) {
                    // With redistribution active a kingdom's row count is its
                    // reassigned pool, not the vanilla world total. Count it the
                    // same way findFullRandomizedKingdomHint lists it so the total
                    // and the rows always agree.
                    //
                    // The same applies once Subarea Keys are rearranging pages:
                    // the merged page carries two kingdoms' leftovers and every
                    // key carrier has moved to the key list, so the native total
                    // is wrong in both directions. Rows come from
                    // findFullRandomizedKingdomHint either way, and a total that
                    // disagrees with them desyncs scrolling straight into the
                    // native null-deref.
                    // This function is present in the game binary but absent
                    // from syms/main.sym, so call its verified offset directly.
                    using Fn = s32 (*)(const GameDataFile*, s32);
                    if (MoonRando::isKingdomRedistributionActive()
                        || subareaKeyListWorldId() >= 0) {
                        // The redistributed count can exceed this kingdom's real
                        // native capacity - kMaxShineListRows is a flat global
                        // cap, not a per-kingdom one, and some kingdoms natively
                        // hold far fewer rows (Darker Side, Cloud, Ruined, etc.).
                        // Reporting more rows than the kingdom can actually hold
                        // lets the player scroll to an index that's valid for
                        // this mod's own bookkeeping but out of bounds for
                        // GameDataFile::isOpenShineName's native indexing, which
                        // crashes (confirmed via crash reports, reproducible by
                        // scrolling the shine list with Kingdom Redistribution
                        // active). Bound the reported total by the kingdom's
                        // real native capacity too - same accessor as the
                        // non-redistribution path below. If the native call ever
                        // returns something non-positive (shouldn't happen for a
                        // real kingdom, but this is unverified game-binary
                        // territory), fall back to the unclamped redistributed
                        // count rather than truncating the list to nothing.
                        const s32 redistributedCount = countFullRandomizedKingdomMoons(file, worldId);
                        const s32 nativeMax = reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start()
                            + cGameDataFileGetWorldTotalShineNumMaxOffset)(file, worldId);
                        const s32 boundedCount = (nativeMax > 0 && nativeMax < redistributedCount)
                            ? nativeMax
                            : redistributedCount;
                        return clampNativeShineListRows(boundedCount, worldId);
                    }
                    return clampNativeShineListRows(
                        reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start()
                            + cGameDataFileGetWorldTotalShineNumMaxOffset)(file, worldId),
                        worldId);
                }
            }
            // The Moon Rock Keys page renders through the native per-page
            // update: this total is what sizes its CommonVerticalList, clamps
            // its scrolling, and feeds the top-right progress counter, exactly
            // like a real kingdom page. Without it the list stays sized to
            // Darker Side's real (1-moon) data and scrolling/row rendering
            // fall apart.
            if (isShineListPageContextActive(worldId)
                && isMoonRockKeyListWorld(worldId))
                return clampNativeShineListRows(getMoonRockKeyCount(), worldId);
            // Same for the Subarea Keys page, which likewise renders through the
            // native per-page update on a real kingdom slot.
            if (isShineListPageContextActive(worldId)
                && isSubareaKeyListWorld(worldId))
                return clampNativeShineListRows(getSubareaKeyCount(), worldId);
            // Same reasoning for the Toadette Medals page: without this the
            // native total stays sized to Mushroom's real (1-entry) data
            // while applyToadetteMedalListView has already populated up to
            // getToadetteMedalTotalCount() custom rows directly on the
            // CommonVerticalList, so the cursor can reach an index the native
            // world-info update (calcShineNumInOneShine) has no real data
            // for and null-derefs.
            if (isShineListPageContextActive(worldId)
                && isToadetteMedalListWorld(worldId))
                return clampNativeShineListRows(
                    MoonRando::getToadetteMedalTotalCount(), worldId);
            return getWorldTotalShineNumHook.orig(accessor, worldId);
        });

    // ShineListLayout runs several native per-row queries whose implementations
    // dereference GameDataFile::findShine without a null check (the reported
    // crash occurred in checkUnlockHintByHintNpcForShineList). The paintings
    // page has no HintInfo behind its rows at all, so its queries must stay
    // neutral. Moon Rock Key rows DO resolve to the key's real HintInfo (see
    // findShineWorldIndexHook), so their get-time lookup is safe and supplies
    // the real collection date for collected keys.
    static HkTrampoline<u64, const al::LayoutActor*, s32, s32> findShineGetTimeHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> u64 {
            if (isPaintingListWorld(worldId))
                return 0;
            if (shouldExposeFullRandomizedMoonList(worldId)) {
                GameDataHolderAccessor accessor(layout);
                GameDataFile* file = accessor.mData
                    ? accessor.mData->getGameDataFile()
                    : nullptr;
                if (!findFullRandomizedKingdomHint(file, worldId, index))
                    return 0;
            }
            return findShineGetTimeHook.orig(layout, worldId, index);
        });

    static HkTrampoline<bool, const al::LayoutActor*, s32, s32> checkMoonRockShineForShineListHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> bool {
            if (isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId))
                return false;

            // The full randomized pool is listed above, so post-Moon-Rock
            // entries should use the normal hint-row state rather than the
            // native locked Rock state.
            if (shouldExposeFullRandomizedMoonList(worldId))
                return false;

            return checkMoonRockShineForShineListHook.orig(layout, worldId, index);
        });

    static HkTrampoline<bool, const al::LayoutActor*, s32, s32> checkUnlockHintByHintNpcForShineListHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> bool {
            if (isPaintingListWorld(worldId))
                return false;
            if (shouldExposeFullRandomizedMoonList(worldId)
                || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId)) {
                GameDataHolderAccessor accessor(layout);
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
                GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
                return hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc;
            }
            return checkUnlockHintByHintNpcForShineListHook.orig(layout, worldId, index);
        });

    static HkTrampoline<bool, const al::LayoutActor*, s32, s32> checkUnlockHintByAmiiboForShineListHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> bool {
            if (isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId))
                return false;
            if (shouldExposeFullRandomizedMoonList(worldId)) {
                GameDataHolderAccessor accessor(layout);
                GameDataFile* file = accessor.mData
                    ? accessor.mData->getGameDataFile()
                    : nullptr;
                GameDataFile::HintInfo* hint = findHintByWorldIndex(
                    file, worldId, index);
                return hint
                    && hint->hintStatus == GameDataFile::HintStatus_UnlockByAmiibo;
            }
            return checkUnlockHintByAmiiboForShineListHook.orig(layout, worldId, index);
        });

    static HkTrampoline<bool, const al::LayoutActor*, s32, s32> checkShineSeaOfTreeForShineListHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> bool {
            if (isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId))
                return false;
            if (shouldExposeFullRandomizedMoonList(worldId))
                return false;
            return checkShineSeaOfTreeForShineListHook.orig(layout, worldId, index);
        });

    static HkTrampoline<s32, const al::LayoutActor*, s32, s32> calcShineNumInOneShineHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 worldId, s32 index) -> s32 {
            // Full randomized kingdom pages deliberately include post-Moon-Rock
            // rows. The native calculation follows their Moon Rock metadata
            // through the stage WorldList, which is not valid for these
            // collection-list-only rows and crashes while rolling the page.
            // Each randomized item is represented as one list row, so the
            // neutral one-shine display is the correct value here.
            if (shouldExposeFullRandomizedMoonList(worldId)
                || isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId)
                || isToadetteMedalListWorld(worldId))
                return 1;
            return calcShineNumInOneShineHook.orig(layout, worldId, index);
        });

    static const ShineDB::ShineEntry* findEntryFromHint(const GameDataFile::HintInfo* hint) {
        if (!hint || hint->stageName.isEmpty())
            return nullptr;
        return ShineDB::findEntry(hint->uniqueId, hint->stageName.cstr());
    }

    static bool initShineInfoFromEntry(ShineInfo* shineInfo, const ShineDB::ShineEntry* entry) {
        if (!shineInfo || !entry || entry->objId < 0)
            return false;

        shineInfo->mStageName.copy(entry->stage);
        shineInfo->mObjId.format("obj%d", entry->objId);
        shineInfo->mLabel.format("ScenarioName_obj%d", entry->objId);
        shineInfo->setType(MoonRando::isRewardMultiEntry(entry)
                ? ShineInfo::Type_GrandShine
                : ShineInfo::Type_Shine);
        return true;
    }

    static bool isMultiMoonEntry(const ShineDB::ShineEntry* entry) {
        // Reward-multi (honors Randomize Triple Moons) so reshuffled grands are
        // treated the same as vanilla grands for purchase-selection blocking.
        return MoonRando::isRewardMultiEntry(entry);
    }

    static bool isStoryMoonEntry(const ShineDB::ShineEntry* entry) {
        return entry && (entry->flags & ShineDB::ShineFlag_Story);
    }

    static const ShineDB::ShineEntry* findRandomizedLocationForItem(
        const ShineDB::ShineEntry* item) {
        if (!item)
            return nullptr;

        const MoonRando::Placement* placement = MoonRando::getPlacementForItem(item->uid, item->stage);
        return placement ? placement->location : nullptr;
    }

    static bool isPurchaseBlockedSelection(const ShineDB::ShineEntry* item) {
        const ShineDB::ShineEntry* location = findRandomizedLocationForItem(item);
        return isMultiMoonEntry(item) || isMultiMoonEntry(location) || isStoryMoonEntry(location)
            || (item && MoonRando::isToadetteMedalItem(item->uid, item->stage))
            || (item && MoonRando::isMoonRockKeyItem(item->uid, item->stage))
            || (item && MoonRando::isSubareaKeyItem(item->uid, item->stage));
    }

    static bool isPowerStarEntry(const ShineDB::ShineEntry* entry) {
        const s32 kingdom = entry ? MoonRando::getAssignedKingdomForEntry(entry) : -1;
        return entry && (kingdom >= 0 ? kingdom : entry->worldId) == WorldMeta::WorldId_Mushroom
            && !(entry->flags & ShineDB::ShineFlag_Multi);
    }

    static Randomizer::HintPriceCategory getHintCategoryForShineEntry(
        const ShineDB::ShineEntry*) {
        // Moon, Toadette Medal, Moon Rock Key, and Subarea Key locations all
        // intentionally use the single Moons selector.
        return Randomizer::HintPriceCategory::Moons;
    }

    static Randomizer::HintPriceCategory getHintCategoryForCapture() {
        return Randomizer::HintPriceCategory::Captures;
    }

    static Randomizer::HintPriceCategory getHintCategoryForAbility() {
        return Randomizer::HintPriceCategory::Abilities;
    }

    static Randomizer::HintPriceCategory getPurchaseCategory() {
        return Randomizer::HintPriceCategory::Purchases;
    }

    static Randomizer::HintPriceCategory getPathCategory() {
        return Randomizer::HintPriceCategory::Path;
    }

    static Randomizer::HintPriceQuote getPriceQuote(GameDataFile* file,
        Randomizer::HintPriceCategory category) {
        return HintEconomy::getQuote(file, category);
    }

    static bool isPriceEnabled(GameDataFile* file, Randomizer::HintPriceCategory category) {
        return getPriceQuote(file, category).enabled;
    }

    static PurchaseResult toPurchaseResult(HintEconomy::TransactionResult result) {
        switch (result) {
        case HintEconomy::TransactionResult::Success:
            return PurchaseResult::Success;
        case HintEconomy::TransactionResult::Disabled:
            return PurchaseResult::Disabled;
        case HintEconomy::TransactionResult::InsufficientFunds:
            return PurchaseResult::NotEnoughFunds;
        case HintEconomy::TransactionResult::QuoteChanged:
            return PurchaseResult::QuoteChanged;
        case HintEconomy::TransactionResult::StorageFailure:
            return PurchaseResult::StorageFailure;
        case HintEconomy::TransactionResult::InvalidTransaction:
        default:
            return PurchaseResult::InvalidSelection;
        }
    }

    static PurchaseResult beginPayment(GameDataFile* file,
        Randomizer::HintPriceCategory category,
        const Randomizer::HintPriceQuote* expectedQuote,
        HintEconomy::PendingTransaction* transaction) {
        const Randomizer::HintPriceQuote quote = expectedQuote
            ? *expectedQuote
            : getPriceQuote(file, category);
        return toPurchaseResult(HintEconomy::beginTransaction(file, category, quote, transaction));
    }

    // Publishes a completed purchase to the rest of the team, so the wallet and
    // the price ladder stay one shared thing rather than one per player - and
    // so a hint the team paid for is legible to all of them, not only whoever
    // happened to press the button.
    //
    // Every spend path funnels through finishPayment below, which is why the
    // broadcast lives there rather than at seven separate call sites: a new
    // purchase kind cannot forget to pay into the shared pool.
    static void broadcastHintPurchase(const HintEconomy::PendingTransaction& transaction,
        SharedUnlock unlock, s32 shineIndex, const char* flagKey) {
#if SMOR_FEATURE_MULTIPLAYER
        Client::sendHintUnlockPacket(static_cast<s32>(unlock), shineIndex, flagKey,
            static_cast<s32>(transaction.category), transaction.quote.amount,
            transaction.successfulPurchaseCount);
#else
        (void)transaction;
        (void)unlock;
        (void)shineIndex;
        (void)flagKey;
#endif
    }

    static PurchaseResult finishPayment(GameDataFile* file,
        HintEconomy::PendingTransaction* transaction, bool succeeded,
        SharedUnlock unlock = SharedUnlock::EconomyOnly, s32 shineIndex = -1,
        const char* flagKey = nullptr) {
        // Captured before finishTransaction, which clears `charged` and is free
        // to leave the rest of the record in whatever state it likes.
        const HintEconomy::PendingTransaction spent = transaction ? *transaction
                                                                  : HintEconomy::PendingTransaction {};
        const PurchaseResult result = toPurchaseResult(
            HintEconomy::finishTransaction(file, transaction, succeeded));
        if (result != PurchaseResult::Success || !succeeded)
            return result;
        broadcastHintPurchase(spent, unlock, shineIndex, flagKey);
        return PurchaseResult::Success;
    }

    static const char16_t* getCurrencyWord(const Randomizer::HintPriceQuote& quote) {
        if (quote.currency == Randomizer::HintCurrencyMode::HintTokens)
            return quote.amount == 1 ? u"Hint Token" : u"Hint Tokens";
        return quote.amount == 1 ? u"Coin" : u"Coins";
    }

    constexpr char16_t kMessageTagMark = 0x000e;
    constexpr char16_t kPadStyleGroup = 0x0006;
    constexpr char16_t kAButtonPadStyle = 0x0002;
    constexpr char16_t kBButtonPadStyle = 0x0003;
    constexpr char16_t kYButtonPadStyle = 0x0005;

    static s32 getFooterTextLength(const char16_t* text) {
        s32 length = 0;
        while (text && text[length])
            length++;
        return length;
    }

    template <s32 N>
    static void appendPadStyleLabel(sead::WFixedSafeString<N>& text, const char16_t* label) {
        const s32 labelLength = getFooterTextLength(label);
        text.append(static_cast<char16_t>(labelLength * sizeof(char16_t)));

        for (s32 i = 0; i < labelLength; i++)
            text.append(label[i]);
    }

    template <s32 N>
    static void appendPadStyleTag(
        sead::WFixedSafeString<N>& text, char16_t padStyleTag,
        const char16_t* dualLabel, const char16_t* handheldLabel,
        const char16_t* fullKeyLabel, const char16_t* leftLabel,
        const char16_t* rightLabel) {
        const s32 payloadWordCount = 5
            + getFooterTextLength(dualLabel)
            + getFooterTextLength(handheldLabel)
            + getFooterTextLength(fullKeyLabel)
            + getFooterTextLength(leftLabel)
            + getFooterTextLength(rightLabel);

        text.append(kMessageTagMark);
        text.append(kPadStyleGroup);
        text.append(padStyleTag);
        text.append(static_cast<char16_t>(payloadWordCount * sizeof(char16_t)));
        appendPadStyleLabel(text, dualLabel);
        appendPadStyleLabel(text, handheldLabel);
        appendPadStyleLabel(text, fullKeyLabel);
        appendPadStyleLabel(text, leftLabel);
        appendPadStyleLabel(text, rightLabel);
    }

    template <s32 N>
    static void appendAButtonIcon(sead::WFixedSafeString<N>& text) {
        appendPadStyleTag(text, kAButtonPadStyle,
            u"DualButtonA", u"HandheldButtonA", u"FullKeyButtonA", u"LeftButtonA", u"RightButtonA");
    }

    template <s32 N>
    static void appendBButtonIcon(sead::WFixedSafeString<N>& text) {
        appendPadStyleTag(text, kBButtonPadStyle,
            u"DualButtonB", u"HandheldButtonB", u"FullKeyButtonB", u"LeftButtonB", u"RightButtonB");
    }

    template <s32 N>
    static void appendYButtonIcon(sead::WFixedSafeString<N>& text) {
        appendPadStyleTag(text, kYButtonPadStyle,
            u"DualButtonY", u"HandheldButtonY", u"FullKeyButtonY", u"LeftButtonY", u"RightButtonY");
    }

    template <s32 N>
    static void appendPrice(sead::WFixedSafeString<N>& text,
        const Randomizer::HintPriceQuote& quote) {
        text.appendWithFormat(u"%d%s", quote.amount,
            quote.currency == Randomizer::HintCurrencyMode::HintTokens ? u"T" : u"C");
    }

    template <s32 N>
    static void appendHintAction(sead::WFixedSafeString<N>& text,
        const Randomizer::HintPriceQuote& quote) {
        appendAButtonIcon(text);
        text.append(u" Hint ");
        appendPrice(text, quote);
    }

    template <s32 N>
    static void appendBuyAction(sead::WFixedSafeString<N>& text,
        const Randomizer::HintPriceQuote& quote) {
        appendYButtonIcon(text);
        text.append(u" Buy ");
        appendPrice(text, quote);
    }

    template <s32 N>
    static void appendPathBuyAction(sead::WFixedSafeString<N>& text,
        const Randomizer::HintPriceQuote& quote) {
        appendAButtonIcon(text);
        text.append(u" Path ");
        appendPrice(text, quote);
    }

    template <s32 N>
    static void appendPathViewAction(sead::WFixedSafeString<N>& text) {
        appendAButtonIcon(text);
        text.append(u" View Path");
    }

    template <s32 N>
    static void appendCloseAction(sead::WFixedSafeString<N>& text) {
        appendBButtonIcon(text);
        text.append(u" Close");
    }

    template <s32 N>
    static void appendFooterSeparator(sead::WFixedSafeString<N>& text) {
        text.append(u"   ");
    }

    template <s32 N>
    static void appendPlayerActionGap(sead::WFixedSafeString<N>& text) {
        text.append(u"      ");
    }

    template <s32 N>
    static const char16_t* makeListFooterPrompt(
        sead::WFixedSafeString<N>& text, SelectionActionState state,
        s32 balance, const Randomizer::HintPriceQuote& hintQuote,
        const Randomizer::HintPriceQuote& buyQuote,
        PathActionState path = PathActionState::None,
        const Randomizer::HintPriceQuote& pathQuote = {}) {
        text = u"";
        text.appendWithFormat(Randomizer::getHintCurrencyMode()
                    == Randomizer::HintCurrencyMode::HintTokens
                ? u"Hint Tokens:%d"
                : u"Coins:%d",
            balance);

        // The A slot carries the hint until it has been bought, then the route.
        // getPathActionState only reports anything once the hint is spent, so
        // these two never contend for the same button.
        const bool hasPathAction = path != PathActionState::None;
        bool hasHintOrBuyAction = false;
        switch (state) {
        case SelectionActionState::HintAndBuy:
            appendPlayerActionGap(text);
            appendHintAction(text, hintQuote);
            appendFooterSeparator(text);
            appendBuyAction(text, buyQuote);
            hasHintOrBuyAction = true;
            break;
        case SelectionActionState::HintOnly:
            appendPlayerActionGap(text);
            appendHintAction(text, hintQuote);
            hasHintOrBuyAction = true;
            break;
        case SelectionActionState::BuyOnly:
            appendPlayerActionGap(text);
            if (hasPathAction) {
                if (path == PathActionState::Buy)
                    appendPathBuyAction(text, pathQuote);
                else
                    appendPathViewAction(text);
                appendFooterSeparator(text);
            }
            appendBuyAction(text, buyQuote);
            hasHintOrBuyAction = true;
            break;
        default:
            if (hasPathAction) {
                appendPlayerActionGap(text);
                if (path == PathActionState::Buy)
                    appendPathBuyAction(text, pathQuote);
                else
                    appendPathViewAction(text);
                hasHintOrBuyAction = true;
            }
            break;
        }

        if (hasHintOrBuyAction)
            appendFooterSeparator(text);
        else
            appendPlayerActionGap(text);
        appendCloseAction(text);
        return text.cstr();
    }

    static bool isOriginalMoonAcquired(GameDataFile* file, const ShineDB::ShineEntry* entry) {
        if (!file || !entry)
            return false;

        return MoonHooks::isMoonAcquiredDirect(file, entry);
    }

    static bool canUseHintSystem() {
        return Randomizer::isHintSystemEnabled() && Randomizer::isMoonRandoEnabled() && MoonRando::isInitialized();
    }

    template <typename Fn>
    static Fn mainOffsetFunction(uintptr_t offset) {
        return reinterpret_cast<Fn>(hk::ro::getMainModule()->range().start() + offset);
    }

    static const al::Nerve* mainOffsetNerve(uintptr_t offset) {
        return reinterpret_cast<const al::Nerve*>(hk::ro::getMainModule()->range().start() + offset);
    }

    static bool requestStartDemoShineGet(Shine* shine) {
        using Fn = bool (*)(Shine*);
        return mainOffsetFunction<Fn>(cRsRequestStartDemoShineGetOffset)(shine);
    }

    static bool requestStartDemoShineGrandGet(Shine* shine) {
        using Fn = bool (*)(Shine*);
        return mainOffsetFunction<Fn>(cRsRequestStartDemoShineGrandGetOffset)(shine);
    }

    static Shine* getDemoShineActor(StageScene* scene) {
        if (!scene)
            return nullptr;

        using Fn = Shine* (*)(const al::Scene*);
        return mainOffsetFunction<Fn>(cRsGetDemoShineActorOffset)(scene);
    }

    static bool isBoughtMoonDemoSceneActive(StageScene* scene) {
        return sBoughtMoonDemoShine && scene && (al::isNerve(scene, mainOffsetNerve(cStageSceneNrvDemoShineGetOffset)) || al::isNerve(scene, mainOffsetNerve(cStageSceneNrvDemoShineGrandGetOffset)));
    }

    static bool isBoughtMoonDemoSceneActive() {
        if (!sBoughtMoonDemoShine)
            return false;
        return isBoughtMoonDemoSceneActive(getStageScene());
    }

    static bool isBoughtPowerStarDemoActive() {
        return sBoughtMoonDemoShine && sBoughtMoonDemoShine->mIsPowerStar;
    }

    static al::IUseLayout* getLayoutInterface(al::LayoutActor* layout) {
        return layout ? reinterpret_cast<al::IUseLayout*>(reinterpret_cast<u8*>(layout) + 8) : nullptr;
    }

    static al::IUseLayoutAction* getLayoutActionInterface(al::LayoutActor* layout) {
        return layout ? reinterpret_cast<al::IUseLayoutAction*>(reinterpret_cast<u8*>(layout) + 16) : nullptr;
    }

    static bool tryStartLayoutAction(
        al::IUseLayoutAction* layout, const char* actionName, const char* paneName = nullptr) {
        if (!layout || !actionName)
            return false;

        using Fn = bool (*)(al::IUseLayoutAction*, const char*, const char*);
        return mainOffsetFunction<Fn>(cLayoutTryStartActionOffset)(layout, actionName, paneName);
    }

    static const al::IUseSceneObjHolder* getHackListSceneObjHolder(
        const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return reinterpret_cast<const al::IUseSceneObjHolder*>(
            reinterpret_cast<const u8*>(layout) + cHackListSceneObjHolderOffset);
    }

    static GameDataHolderAccessor getHackListAccessor(const al::LayoutActor* layout) {
        const al::IUseSceneObjHolder* holder = getHackListSceneObjHolder(layout);
        return holder ? GameDataHolderAccessor(holder) : GameDataHolderAccessor(static_cast<GameDataHolder*>(nullptr));
    }

    static GameDataHolderWriter getHackListWriter(const al::LayoutActor* layout) {
        const al::IUseSceneObjHolder* holder = getHackListSceneObjHolder(layout);
        return holder ? GameDataHolderWriter(holder) : GameDataHolderWriter(static_cast<GameDataHolder*>(nullptr));
    }

    static al::LiveActor* getStageScenePowerStarDemoActor(StageScene* scene) {
        if (!scene || !scene->mStateGetShine)
            return nullptr;
        return *reinterpret_cast<al::LiveActor**>(
            reinterpret_cast<u8*>(scene->mStateGetShine) + cStageSceneStateGetShinePowerStarActorOffset);
    }

    static void commonHorizontalListInitDataNoResetSelected(void* list, s32 count) {
        if (!list)
            return;

        using Fn = void (*)(void*, s32);
        mainOffsetFunction<Fn>(cCommonHorizontalListInitDataNoResetSelectedOffset)(list, count);
    }

    static void commonHorizontalListAddStringData(
        void* list, const sead::WFixedSafeString<0x100>* data, const char* paneName) {
        if (!list || !data || !paneName)
            return;

        using Fn = void (*)(void*, const sead::WFixedSafeString<0x100>*, const char*);
        mainOffsetFunction<Fn>(cCommonHorizontalListAddStringDataOffset)(list, data, paneName);
    }

    static void commonHorizontalListSetImageData(void* list, void* const* data,
        const char* paneName) {
        if (!list || !paneName)
            return;

        using Fn = void (*)(void*, void* const*, const char*);
        mainOffsetFunction<Fn>(cCommonHorizontalListSetImageDataOffset)(list, data, paneName);
    }

    static void commonHorizontalListSetEnableData(void* list, const bool* data) {
        if (!list || !data)
            return;

        using Fn = void (*)(void*, const bool*);
        mainOffsetFunction<Fn>(cCommonHorizontalListSetEnableDataOffset)(list, data);
    }

    static void commonHorizontalListUpdateParts(void* list) {
        if (!list)
            return;

        using Fn = void (*)(void*);
        mainOffsetFunction<Fn>(cCommonHorizontalListUpdatePartsOffset)(list);
    }

    static void setCounterAndDenominator(al::LayoutActor* layout, s32 current, s32 denominator) {
        if (!layout)
            return;

        using Fn = void (*)(al::LayoutActor*, s32, s32);
        mainOffsetFunction<Fn>(cRsSetCounterAndDenominatorOffset)(layout, current, denominator);
    }

    static void setPaneLocalAlphaByOffset(al::IUseLayout* layout, const char* paneName, f32 alpha) {
        if (!layout || !paneName)
            return;

        using Fn = void (*)(al::IUseLayout*, const char*, f32);
        mainOffsetFunction<Fn>(cSetPaneLocalAlphaOffset)(layout, paneName, alpha);
    }

    static al::WindowConfirm* getStageWindowConfirm() {
        StageScene* scene = getStageScene();
        return scene ? scene->mWindowConfirm : nullptr;
    }

    static void windowConfirmSetTxtMessage(al::WindowConfirm* window, const char16_t* text) {
        using Fn = void (*)(al::WindowConfirm*, const char16_t*);
        mainOffsetFunction<Fn>(cWindowConfirmSetTxtMessageOffset)(window, text);
    }

    static void windowConfirmSetTxtList(al::WindowConfirm* window, s32 index, const char16_t* text) {
        using Fn = void (*)(al::WindowConfirm*, s32, const char16_t*);
        mainOffsetFunction<Fn>(cWindowConfirmSetTxtListOffset)(window, index, text);
    }

    static void windowConfirmSetListNum(al::WindowConfirm* window, s32 count) {
        using Fn = void (*)(al::WindowConfirm*, s32);
        mainOffsetFunction<Fn>(cWindowConfirmSetListNumOffset)(window, count);
    }

    static void windowConfirmSetCancelIdx(al::WindowConfirm* window, s32 index) {
        using Fn = void (*)(al::WindowConfirm*, s32);
        mainOffsetFunction<Fn>(cWindowConfirmSetCancelIdxOffset)(window, index);
    }

    static void windowConfirmAppearWithChoicingCancel(al::WindowConfirm* window) {
        using Fn = void (*)(al::WindowConfirm*);
        mainOffsetFunction<Fn>(cWindowConfirmAppearWithChoicingCancelOffset)(window);
    }

    static bool windowConfirmIsNerveEnd(al::WindowConfirm* window) {
        using Fn = bool (*)(al::WindowConfirm*);
        return mainOffsetFunction<Fn>(cWindowConfirmIsNerveEndOffset)(window);
    }

    static bool windowConfirmTryUp(al::WindowConfirm* window) {
        using Fn = bool (*)(al::WindowConfirm*);
        return mainOffsetFunction<Fn>(cWindowConfirmTryUpOffset)(window);
    }

    static bool windowConfirmTryDown(al::WindowConfirm* window) {
        using Fn = bool (*)(al::WindowConfirm*);
        return mainOffsetFunction<Fn>(cWindowConfirmTryDownOffset)(window);
    }

    static bool windowConfirmTryDecide(al::WindowConfirm* window) {
        using Fn = bool (*)(al::WindowConfirm*);
        return mainOffsetFunction<Fn>(cWindowConfirmTryDecideOffset)(window);
    }

    static bool windowConfirmTryCancel(al::WindowConfirm* window) {
        using Fn = bool (*)(al::WindowConfirm*);
        return mainOffsetFunction<Fn>(cWindowConfirmTryCancelOffset)(window);
    }

    static s32 windowConfirmGetSelectedIndex(const al::WindowConfirm* window) {
        return *reinterpret_cast<const s32*>(reinterpret_cast<const u8*>(window) + 0x134);
    }

    static s32 windowConfirmGetCancelIndex(const al::WindowConfirm* window) {
        return *reinterpret_cast<const s32*>(reinterpret_cast<const u8*>(window) + 0x138);
    }

    static bool windowConfirmIsCancelSelected(const al::WindowConfirm* window) {
        return windowConfirmGetSelectedIndex(window) == windowConfirmGetCancelIndex(window);
    }

    static bool isTriggerUiDecide(const al::IUseSceneObjHolder* holder) {
        using Fn = bool (*)(const al::IUseSceneObjHolder*);
        return mainOffsetFunction<Fn>(cRsIsTriggerUiDecideOffset)(holder);
    }

    static bool isTriggerUiCancel(const al::IUseSceneObjHolder* holder) {
        using Fn = bool (*)(const al::IUseSceneObjHolder*);
        return mainOffsetFunction<Fn>(cRsIsTriggerUiCancelOffset)(holder);
    }

    static bool isTriggerUiUp(const al::IUseSceneObjHolder* holder) {
        using Fn = bool (*)(const al::IUseSceneObjHolder*);
        return mainOffsetFunction<Fn>(cRsIsTriggerUiUpOffset)(holder);
    }

    static bool isTriggerUiDown(const al::IUseSceneObjHolder* holder) {
        using Fn = bool (*)(const al::IUseSceneObjHolder*);
        return mainOffsetFunction<Fn>(cRsIsTriggerUiDownOffset)(holder);
    }

    static CommonVerticalList* getShineListCommonVerticalList(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<CommonVerticalList* const*>(
            reinterpret_cast<const u8*>(layout) + cShineListCommonVerticalListOffset);
    }

    static al::LayoutActor* getShineListFooterLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cShineListFooterLayoutOffset);
    }

    static s32 getShineListSelectedWorldIndex(const al::LayoutActor* layout) {
        if (!layout)
            return -1;
        return *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(layout) + cShineListSelectedWorldIdxOffset);
    }

    static s32 getWorldIdForShineList(const al::LayoutActor* layout, s32 worldIndex) {
        if (!layout || worldIndex < 0)
            return -1;

        using Fn = s32 (*)(const al::LayoutActor*, s32);
        return mainOffsetFunction<Fn>(cGetWorldIdForShineListOffset)(layout, worldIndex);
    }

    static s32 getSelectedShineListWorldId(const al::LayoutActor* layout) {
        const s32 worldId = getWorldIdForShineList(layout, getShineListSelectedWorldIndex(layout));
        return worldId >= 0 ? worldId : inferCurrentShineListWorld(layout);
    }

    static void* getHackListCommonHorizontalList(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<void* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListCommonHorizontalListOffset);
    }

    static HackListEntry* getHackListEntries(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<HackListEntry* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListEntryArrayOffset);
    }

    static s32 getHackListCount(const al::LayoutActor* layout) {
        if (!layout)
            return 0;
        return *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(layout) + cHackListCountOffset);
    }

    static sead::WFixedSafeString<0x100>* getHackListNameData(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<sead::WFixedSafeString<0x100>* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListNameDataOffset);
    }

    static sead::WFixedSafeString<0x200>* getHackListTutorialData(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<sead::WFixedSafeString<0x200>* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListTutorialDataOffset);
    }

    static bool* getHackListEnableData(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<bool* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListEnableDataOffset);
    }

    static al::LayoutActor* getHackListGuideLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListGuideLayoutOffset);
    }

    static al::LayoutActor* getHackListFooterLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cHackListFooterLayoutOffset);
    }

    static s32 getHackListSelectedIndex(const al::LayoutActor* layout) {
        void* list = getHackListCommonHorizontalList(layout);
        if (!list)
            return -1;
        const s32 index = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListSelectedIdxOffset);
        if (index < 0 && getHackListCount(layout) > 0)
            return 0;
        return index;
    }

    static void closeHackList(const al::LayoutActor* layout) {
        if (!layout)
            return;

        using Fn = void (*)(al::LayoutActor*);
        mainOffsetFunction<Fn>(cHackListCancelOffset)(const_cast<al::LayoutActor*>(layout));
    }

    static al::LayoutActor* getShineListLayoutFromCollectionState(void* state) {
        if (!state)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor**>(
            reinterpret_cast<u8*>(state) + cCollectionStateShineListLayoutOffset);
    }

    static al::LayoutActor* getHackListLayoutFromCollectionState(void* state) {
        if (!state)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor**>(
            reinterpret_cast<u8*>(state) + cCollectionStateHackListLayoutOffset);
    }

    static const CaptureRando::CaptureEntry* getHackListCaptureAt(
        const al::LayoutActor* layout, s32 index) {
        if (!layout || index < 0 || index >= getHackListCount(layout))
            return nullptr;

        HackListEntry* entries = getHackListEntries(layout);
        if (entries && entries[index].hackName && !entries[index].hackName->isEmpty()) {
            if (const CaptureRando::CaptureEntry* capture = CaptureRando::findCaptureByHackName(entries[index].hackName->cstr()))
                return capture;
        }

        return CaptureRando::getCapture(index);
    }

    static const al::IUseSceneObjHolder* getSouvenirListSceneObjHolder(
        const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return reinterpret_cast<const al::IUseSceneObjHolder*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListSceneObjHolderOffset);
    }

    static GameDataHolderAccessor getSouvenirListAccessor(const al::LayoutActor* layout) {
        const al::IUseSceneObjHolder* holder = getSouvenirListSceneObjHolder(layout);
        return holder ? GameDataHolderAccessor(holder) : GameDataHolderAccessor(static_cast<GameDataHolder*>(nullptr));
    }

    static GameDataHolderWriter getSouvenirListWriter(const al::LayoutActor* layout) {
        const al::IUseSceneObjHolder* holder = getSouvenirListSceneObjHolder(layout);
        return holder ? GameDataHolderWriter(holder) : GameDataHolderWriter(static_cast<GameDataHolder*>(nullptr));
    }

    static void* getSouvenirListCommonHorizontalList(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<void* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListCommonHorizontalListOffset);
    }

    static s32 getSouvenirListCount(const al::LayoutActor* layout) {
        if (!layout)
            return 0;
        return *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListCountOffset);
    }

    static void setSouvenirListCount(al::LayoutActor* layout, s32 count) {
        if (!layout)
            return;
        *reinterpret_cast<s32*>(reinterpret_cast<u8*>(layout) + cSouvenirListCountOffset) = count;
    }

    static sead::WFixedSafeString<0x100>* getSouvenirListNumberData(
        const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<sead::WFixedSafeString<0x100>* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListNumberDataOffset);
    }

    static sead::WFixedSafeString<0x100>* getSouvenirListNameData(
        const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<sead::WFixedSafeString<0x100>* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListNameDataOffset);
    }

    static sead::WFixedSafeString<0x100>* getSouvenirListTutorialData(
        const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<sead::WFixedSafeString<0x100>* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListTutorialDataOffset);
    }

    static bool* getSouvenirListEnableData(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<bool* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListEnableDataOffset);
    }

    static void** getSouvenirListTextureData(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<void** const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListTextureDataOffset);
    }

    static al::LayoutActor* getSouvenirListCounterLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListCounterLayoutOffset);
    }

    static al::LayoutActor* getSouvenirListGuideLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListGuideLayoutOffset);
    }

    static al::LayoutActor* getSouvenirListFooterLayout(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor* const*>(
            reinterpret_cast<const u8*>(layout) + cSouvenirListFooterLayoutOffset);
    }

    static s32 getSouvenirListSelectedIndex(const al::LayoutActor* layout) {
        void* list = getSouvenirListCommonHorizontalList(layout);
        if (!list)
            return -1;
        const s32 index = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListSelectedIdxOffset);
        if (index < 0 && getSouvenirListCount(layout) > 0)
            return 0;
        return index;
    }

    static void closeSouvenirList(const al::LayoutActor* layout) {
        if (!layout)
            return;

        using Fn = void (*)(al::LayoutActor*);
        mainOffsetFunction<Fn>(cSouvenirListCancelOffset)(const_cast<al::LayoutActor*>(layout));
    }

    static al::LayoutActor* getSouvenirListLayoutFromCollectionState(void* state) {
        if (!state)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor**>(
            reinterpret_cast<u8*>(state) + cCollectionStateSouvenirListLayoutOffset);
    }

    static bool isShineListInputEnabled(const al::LayoutActor* layout) {
        if (!layout || !layout->isAlive())
            return false;

        using Fn = bool (*)(const al::LayoutActor*);
        return mainOffsetFunction<Fn>(cShineListIsEnableInputOffset)(layout);
    }

    static void closeShineList(const al::LayoutActor* layout) {
        if (!layout)
            return;

        using Fn = void (*)(al::LayoutActor*);
        mainOffsetFunction<Fn>(cShineListCancelOffset)(const_cast<al::LayoutActor*>(layout));
    }

    static void updateShineListWorldInfo(const al::LayoutActor* layout) {
        if (!layout)
            return;

        using Fn = void (*)(al::LayoutActor*);
        mainOffsetFunction<Fn>(cShineListUpdateWorldInfoOffset)(
            const_cast<al::LayoutActor*>(layout));
    }

    static s32 getListSelectedDataIndex(const CommonVerticalList* list) {
        if (!list || list->mCurSelected < 0)
            return -1;
        return list->mCurSelected;
    }

    static s32 getListTopSelectedIdx(const CommonVerticalList* list) {
        return list ? list->mTopSelectableIdx : -1;
    }

    static s32 calcStableListTopIndex(const CommonVerticalList* list, s32 selectedIndex, s32 preferredTop) {
        if (!list || selectedIndex < 0)
            return preferredTop >= 0 ? preferredTop : 0;

        // CommonVerticalList::updateCursorPos draws the cursor on list part
        // (selectedIndex - top + 1), which must stay inside mListPartsArr, i.e.
        //   0 <= selectedIndex - top + 1 < mListPartsArraySize.
        // The game keeps the selected row a few parts off the bottom: CommonVerticalList::down
        // snaps top to selectedIndex - mListPartsNum + 4, and setSelectedIdx caps an auto-top at
        // mDataCount - mListPartsNum + 3. The previous maxTop of mDataCount - mListPartsNum was
        // three rows too tight, so buying a moon near the bottom of a long kingdom list clamped
        // top down until the cursor part index landed one past the end of mListPartsArr, feeding
        // a null pane to al::calcPaneTrans and crashing. Mirror the game's margins instead.
        const s32 visibleCount = list->mListPartsNum > 0 ? list->mListPartsNum : 1;
        const s32 partsCount = list->mListPartsArraySize > 0 ? list->mListPartsArraySize : visibleCount;
        const s32 bottomMargin = visibleCount >= 4 ? 4 : 1;

        s32 top = preferredTop >= 0 ? preferredTop : getListTopSelectedIdx(list);
        if (top < 0)
            top = 0;
        if (top > selectedIndex)
            top = selectedIndex;
        if (selectedIndex - top > visibleCount - bottomMargin)
            top = selectedIndex - (visibleCount - bottomMargin);

        const s32 maxTop = list->mDataCount - visibleCount + (bottomMargin - 1);
        if (maxTop >= 0 && top > maxTop)
            top = maxTop;
        if (top < 0)
            top = 0;

        // Hard guarantee the cursor part index stays within mListPartsArr regardless of the
        // heuristics above (partsCount is always >= 2, so this keeps top <= selectedIndex too).
        if (selectedIndex - top + 1 >= partsCount)
            top = selectedIndex - partsCount + 2;
        return top < 0 ? 0 : top;
    }

    static void writeRememberedShineListSelection(const al::LayoutActor* layout, s32 worldId,
        s32 index, s32 topIndex) {
        if (!layout || worldId < 0 || worldId >= WorldMeta::sWorldCount)
            return;

        s32* topIndices = *reinterpret_cast<s32* const*>(
            reinterpret_cast<const u8*>(layout) + cShineListTopIndexArrayOffset);
        s32* selectedIndices = *reinterpret_cast<s32* const*>(
            reinterpret_cast<const u8*>(layout) + cShineListSelectedIndexArrayOffset);
        if (topIndices)
            topIndices[worldId] = topIndex;
        if (selectedIndices)
            selectedIndices[worldId] = index;
    }

    static void rememberMoonListSelection(const al::LayoutActor* layout, s32 worldId,
        s32 index, s32 topIndex) {
        if (!layout || worldId < 0 || index < 0)
            return;

        CommonVerticalList* list = getShineListCommonVerticalList(layout);
        const s32 stableTop = calcStableListTopIndex(list, index, topIndex);
        writeRememberedShineListSelection(layout, worldId, index, stableTop);
        if (list)
            list->setSelectedIdx(index, stableTop);

        sPendingMoonListRestore.pending = true;
        sPendingMoonListRestore.worldId = worldId;
        sPendingMoonListRestore.index = index;
        sPendingMoonListRestore.top = stableTop;
        Randomizer::logPush(
            "hint system saved bought moon list cursor world=%d index=%d top=%d",
            worldId, index, stableTop);
    }

    static void restorePendingMoonListSelection(const al::LayoutActor* layout) {
        if (!sPendingMoonListRestore.pending || !layout || !sIsDirectShineListView)
            return;
        if (getSelectedShineListWorldId(layout) != sPendingMoonListRestore.worldId)
            return;

        CommonVerticalList* list = getShineListCommonVerticalList(layout);
        if (!list || sPendingMoonListRestore.index < 0
            || sPendingMoonListRestore.index >= list->mDataCount)
            return;

        const s32 topIndex = calcStableListTopIndex(
            list, sPendingMoonListRestore.index, sPendingMoonListRestore.top);
        writeRememberedShineListSelection(layout, sPendingMoonListRestore.worldId,
            sPendingMoonListRestore.index, topIndex);
        list->setSelectedIdx(sPendingMoonListRestore.index, topIndex);
        sObservedIndex = sPendingMoonListRestore.index;
        Randomizer::logPush(
            "hint system restored bought moon list cursor world=%d index=%d top=%d",
            sPendingMoonListRestore.worldId, sPendingMoonListRestore.index, topIndex);
        sPendingMoonListRestore = {};
    }

    static bool writeHintMessageToListData(const al::LayoutActor* layout, CommonVerticalList* list,
        s32 worldId, s32 index) {
        if (!layout || !list || !list->mStringDataArr || list->mStringDataCount <= 0 || index < 0 || index >= list->mDataCount)
            return false;

        sead::WFixedSafeString<0x200>* contentData = list->mStringDataArr[0];
        if (!contentData)
            return false;

        const char16_t* message = tryMakeHintMessage(GameDataHolderAccessor(layout), worldId, index, true);
        if (!message)
            return false;

        contentData[index].copy(sead::WSafeString(message));
        return true;
    }

    static void requestShineListDeferredRefresh(const al::LayoutActor* layout, s32 worldId,
        s32 index, s32 topIndex) {
        sPendingShineListRefreshLayout = layout;
        sPendingShineListRefreshWorldId = worldId;
        sPendingShineListRefreshIndex = index;
        sPendingShineListRefreshTop = topIndex;
        sPendingShineListRefreshFrames = cPendingShineListRefreshFrameMax;
        Randomizer::logPush("hint system scheduled ShineList refresh world=%d index=%d top=%d",
            worldId, index, topIndex);
    }

    static void clearPendingShineListRefresh() {
        sPendingShineListRefreshLayout = nullptr;
        sPendingShineListRefreshWorldId = -1;
        sPendingShineListRefreshIndex = -1;
        sPendingShineListRefreshTop = -1;
        sPendingShineListRefreshFrames = 0;
    }

    static void updatePendingShineListRefresh(const al::LayoutActor* layout) {
        if (!sPendingShineListRefreshLayout || layout != sPendingShineListRefreshLayout)
            return;
        if (hasPendingConfirmation())
            return;
        if (sPendingShineListRefreshFrames > 0) {
            sPendingShineListRefreshFrames--;
            return;
        }
        if (!sIsDirectShineListView || !isShineListInputEnabled(layout))
            return;

        CommonVerticalList* list = getShineListCommonVerticalList(layout);
        if (!list || !list->mListPartsArr || list->mListPartsNum <= 0)
            return;

        const s32 selectedIndex = sPendingShineListRefreshIndex;
        if (selectedIndex < 0 || selectedIndex >= list->mDataCount) {
            clearPendingShineListRefresh();
            return;
        }

        const s32 topIndex = calcStableListTopIndex(list, selectedIndex, sPendingShineListRefreshTop);

        updateShineListWorldInfo(layout);
        list = getShineListCommonVerticalList(layout);
        if (!list) {
            clearPendingShineListRefresh();
            return;
        }

        // Moon Rock Key rows already got their revealed text from the native
        // rebuild above (tryFindShineMessage hook); the long-form hint string
        // would clobber that compressed row text.
        const bool wroteHint = !isMoonRockKeyListWorld(sPendingShineListRefreshWorldId)
            && !isSubareaKeyListWorld(sPendingShineListRefreshWorldId)
            && writeHintMessageToListData(
                layout, list, sPendingShineListRefreshWorldId, selectedIndex);
        list->setSelectedIdx(selectedIndex, topIndex);
        sObservedIndex = selectedIndex;

        applyShineListFooterText(layout);
        refreshFooterPrompt();
        Randomizer::logPush("hint system refreshed ShineList after hint purchase world=%d index=%d top=%d wrote=%d",
            sPendingShineListRefreshWorldId, selectedIndex, topIndex, wroteHint);
        clearPendingShineListRefresh();
    }

    static bool isActionableUnacquiredSelection(GameDataFile* file, s32 worldId, s32 index,
        GameDataFile::HintInfo** outHint, const ShineDB::ShineEntry** outEntry) {
        if (outHint)
            *outHint = nullptr;
        if (outEntry)
            *outEntry = nullptr;

        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        const ShineDB::ShineEntry* entry = findEntryFromHint(hint);
        if (!hint || !entry)
            return false;
        if (outHint)
            *outHint = hint;
        if (outEntry)
            *outEntry = entry;
        if (isOriginalMoonAcquired(file, entry))
            return false;
        return true;
    }

    static bool isPromptableSelection(GameDataFile* file, s32 worldId, s32 index) {
        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        if (!isActionableUnacquiredSelection(file, worldId, index, &hint, &entry))
            return false;
        return hint && hint->hintStatus != GameDataFile::HintStatus_UnlockByNpc;
    }

    static SelectionActionState getSelectionActionState(GameDataFile* file, s32 worldId, s32 index,
        GameDataFile::HintInfo** outHint = nullptr, const ShineDB::ShineEntry** outEntry = nullptr) {
        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        if (!isActionableUnacquiredSelection(file, worldId, index, &hint, &entry)) {
            if (outHint)
                *outHint = hint;
            if (outEntry)
                *outEntry = entry;
            return SelectionActionState::None;
        }

        if (outHint)
            *outHint = hint;
        if (outEntry)
            *outEntry = entry;

        const bool isHinted = hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc;
        const bool hintEnabled = isPriceEnabled(file, getHintCategoryForShineEntry(entry));
        const bool purchaseEnabled = isPriceEnabled(file, getPurchaseCategory());
        // The Darker Side page is a key tracker, not a moon shop: its only
        // available action is the one-time location hint.  (The same item is
        // also protected from direct purchase everywhere by
        // isPurchaseBlockedSelection above.)
        if (isMoonRockKeyListWorld(worldId) || isSubareaKeyListWorld(worldId))
            return !isHinted && hintEnabled ? SelectionActionState::HintOnly : SelectionActionState::None;
        if (isPurchaseBlockedSelection(entry))
            return isHinted   ? purchaseEnabled ? SelectionActionState::CannotBuy : SelectionActionState::None
                : hintEnabled ? SelectionActionState::HintOnly
                              : SelectionActionState::None;

        if (isHinted)
            return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
        if (hintEnabled && purchaseEnabled)
            return SelectionActionState::HintAndBuy;
        if (hintEnabled)
            return SelectionActionState::HintOnly;
        return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
    }

    static GameDataFile* getFile(GameDataHolderAccessor accessor) {
        return accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
    }

    // ── Extended Hints: which routes the player has bought ──────────────────
    //
    // One bit per ShineDB location, hex-encoded into a fixed pair of
    // hack-dictionary records (the same trick KingdomProgress uses to persist
    // its own state). One key per purchase would be simpler, but that array is
    // 160 fixed slots shared with the captures the player has actually used and
    // with the capture/ability hint keys, and it silently drops writes once
    // full - a run can buy far more routes than would fit. A two-record bitset
    // costs the same two slots whether one route has been bought or all of them.
    static constexpr s32 cRouteHintBitCount = ShineDB::sShineTableSize;
    static constexpr s32 cRouteHintByteCount = (cRouteHintBitCount + 7) / 8;
    static constexpr s32 cRouteHintHexPerChunk = 100; // must stay even
    static constexpr s32 cRouteHintBytesPerChunk = cRouteHintHexPerChunk / 2;
    static constexpr s32 cRouteHintChunkCount
        = (cRouteHintByteCount + cRouteHintBytesPerChunk - 1) / cRouteHintBytesPerChunk;

    static u8 sRouteHintBits[cRouteHintByteCount];
    static const GameDataFile* sRouteHintBitsFile = nullptr;

    static void makeRouteHintChunkKey(char* out, size_t outSize, s32 chunk) {
        nn::util::SNPrintf(out, outSize, "RPH%d", chunk);
    }

    static s32 parseRouteHintHexDigit(char c) {
        if (c >= '0' && c <= '9')
            return c - '0';
        if (c >= 'A' && c <= 'F')
            return c - 'A' + 10;
        if (c >= 'a' && c <= 'f')
            return c - 'a' + 10;
        return -1;
    }

    static void loadRouteHintBits(const GameDataFile* file) {
        std::memset(sRouteHintBits, 0, sizeof(sRouteHintBits));
        if (!file)
            return;

        for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
            char key[16];
            makeRouteHintChunkKey(key, sizeof(key), chunk);
            const char* record = file->customFindHackDictionaryKey(key);
            if (!record)
                continue;

            const char* hex = record + std::strlen(key) + 1;
            for (s32 i = 0; hex[i] && hex[i + 1]; i += 2) {
                const s32 byteIndex = chunk * cRouteHintBytesPerChunk + i / 2;
                if (byteIndex >= cRouteHintByteCount)
                    break;
                const s32 high = parseRouteHintHexDigit(hex[i]);
                const s32 low = parseRouteHintHexDigit(hex[i + 1]);
                if (high < 0 || low < 0)
                    break;
                sRouteHintBits[byteIndex] = static_cast<u8>((high << 4) | low);
            }
        }
    }

    static bool storeRouteHintBits(GameDataFile* file, const u8* bits) {
        if (!file || !bits)
            return false;

        static constexpr char cHexDigits[] = "0123456789ABCDEF";
        char keys[cRouteHintChunkCount][16] = {};
        char records[cRouteHintChunkCount][128] = {};
        char previousRecords[cRouteHintChunkCount][128] = {};
        bool hadPreviousRecord[cRouteHintChunkCount] = {};
        s32 neededNewRecords = 0;
        for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
            makeRouteHintChunkKey(keys[chunk], sizeof(keys[chunk]), chunk);
            s32 length = 0;
            for (const char* cursor = keys[chunk]; *cursor; cursor++)
                records[chunk][length++] = *cursor;
            if (length + 1 + cRouteHintHexPerChunk >= static_cast<s32>(sizeof(records[chunk])))
                return false;
            records[chunk][length++] = ':';
            for (s32 i = 0; i < cRouteHintBytesPerChunk; i++) {
                const s32 byteIndex = chunk * cRouteHintBytesPerChunk + i;
                const u8 value = byteIndex < cRouteHintByteCount ? bits[byteIndex] : 0;
                records[chunk][length++] = cHexDigits[value >> 4];
                records[chunk][length++] = cHexDigits[value & 0xF];
            }
            records[chunk][length] = '\0';

            if (!file->customCanUpsertHackDictionaryKey(keys[chunk]))
                return false;
            if (const char* previous = file->customFindHackDictionaryKey(keys[chunk])) {
                std::strncpy(previousRecords[chunk], previous,
                    sizeof(previousRecords[chunk]) - 1);
                hadPreviousRecord[chunk] = true;
            } else
                neededNewRecords++;
        }
        if (file->customGetHackDictionaryFreeCount() < neededNewRecords)
            return false;

        // Preflight the shared dictionary before writing anything. If a write
        // or readback fails anyway, restore every chunk so a failed route
        // purchase does not leave a partly-persisted bitset behind.
        auto rollback = [&]() {
            bool restored = true;
            for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
                if (hadPreviousRecord[chunk])
                    restored = file->customUpsertHackDictionaryKey(
                                   keys[chunk], previousRecords[chunk])
                        && restored;
                else if (file->customFindHackDictionaryKey(keys[chunk]))
                    restored = file->customRemoveHackDictionaryKey(keys[chunk]) && restored;
            }
            return restored;
        };

        for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
            if (!file->customUpsertHackDictionaryKey(keys[chunk], records[chunk])) {
                const bool restored = rollback();
                Randomizer::logPush("hint system route state write failed chunk=%d rollback=%d", chunk, restored);
                return false;
            }
        }
        for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
            const char* stored = file->customFindHackDictionaryKey(keys[chunk]);
            if (!stored || std::strcmp(stored, records[chunk]) != 0) {
                const bool restored = rollback();
                Randomizer::logPush("hint system route state readback failed chunk=%d rollback=%d", chunk, restored);
                return false;
            }
        }
        return true;
    }

    static void invalidateRouteActionCache();

    static void invalidateRouteHintBits() {
        sRouteHintBitsFile = nullptr;
        invalidateRouteActionCache();
    }

    static void ensureRouteHintBits(const GameDataFile* file) {
        if (!file || sRouteHintBitsFile == file)
            return;
        loadRouteHintBits(file);
        sRouteHintBitsFile = file;
    }

    static s32 routeHintBitIndex(const ShineDB::ShineEntry* location) {
        if (!location || !location->stage)
            return -1;
        return ShineDB::findIndex(location->uid, location->stage);
    }

    static bool isRouteHintBought(const GameDataFile* file, const ShineDB::ShineEntry* location) {
        const s32 index = routeHintBitIndex(location);
        if (!file || index < 0 || index >= cRouteHintBitCount)
            return false;
        ensureRouteHintBits(file);
        return (sRouteHintBits[index >> 3] & (1u << (index & 7))) != 0;
    }

    static bool setRouteHintBought(GameDataFile* file, const ShineDB::ShineEntry* location) {
        const s32 index = routeHintBitIndex(location);
        if (!file || index < 0 || index >= cRouteHintBitCount)
            return false;
        ensureRouteHintBits(file);
        u8 updatedBits[cRouteHintByteCount];
        std::memcpy(updatedBits, sRouteHintBits, sizeof(updatedBits));
        updatedBits[index >> 3] |= static_cast<u8>(1u << (index & 7));
        if (!storeRouteHintBits(file, updatedBits))
            return false;
        std::memcpy(sRouteHintBits, updatedBits, sizeof(sRouteHintBits));
        sRouteHintBitsFile = file;
        return true;
    }

    // Null unless this row's hint has already been bought AND the location it
    // points at is inside a shuffled sub-area, which is the only case where a
    // route is more than the kingdom name the hint already gave away.
    static const ShineDB::ShineEntry* findRouteHintLocation(
        GameDataFile* file, s32 worldId, s32 index) {
        if (!canUseHintSystem()
            || !isPriceEnabled(file, getPathCategory()))
            return nullptr;

        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        if (!isActionableUnacquiredSelection(file, worldId, index, &hint, &entry))
            return nullptr;
        if (!hint || hint->hintStatus != GameDataFile::HintStatus_UnlockByNpc)
            return nullptr;

        const ShineDB::ShineEntry* location = tryFindRandomizedLocation(file, worldId, index);
        return MoonRando::isLocationInSubArea(location) ? location : nullptr;
    }

    // observeShineListLayout rebuilds the footer every frame the list is open,
    // and resolving a row's route state means several full ShineDB/placement
    // scans. Memoize the highlighted row; only a purchase or a change of list
    // can move it, and both invalidate explicitly.
    struct RouteActionCache {
        const GameDataFile* file;
        s32 worldId;
        s32 index;
        const ShineDB::ShineEntry* location;
        PathActionState state;
        bool valid;
    };
    static RouteActionCache sRouteActionCache = {};

    // The Capture and Ability lists key their rows on the static table entry
    // rather than a (world, index) pair, but need memoizing for exactly the same
    // reason: their footers are rebuilt every frame too.
    struct ItemRouteActionCache {
        const GameDataFile* file;
        const void* entry;
        const ShineDB::ShineEntry* location;
        PathActionState state;
        bool valid;
    };
    static ItemRouteActionCache sCaptureRouteActionCache = {};
    static ItemRouteActionCache sAbilityRouteActionCache = {};

    static void invalidateRouteActionCache() {
        sRouteActionCache.valid = false;
        sCaptureRouteActionCache.valid = false;
        sAbilityRouteActionCache.valid = false;
    }

    static PathActionState getPathActionState(GameDataFile* file, s32 worldId, s32 index,
        const ShineDB::ShineEntry** outLocation = nullptr) {
        if (outLocation)
            *outLocation = nullptr;

        if (sRouteActionCache.valid && sRouteActionCache.file == file
            && sRouteActionCache.worldId == worldId && sRouteActionCache.index == index) {
            if (outLocation)
                *outLocation = sRouteActionCache.location;
            return sRouteActionCache.state;
        }

        const ShineDB::ShineEntry* location = findRouteHintLocation(file, worldId, index);
        const PathActionState state = !location ? PathActionState::None
            : isRouteHintBought(file, location) ? PathActionState::View
                                                : PathActionState::Buy;

        sRouteActionCache = { file, worldId, index, location, state, true };
        if (outLocation)
            *outLocation = location;
        return state;
    }

    static bool canUseAbilityHintSystem() {
        if (!Randomizer::isLockedAbilitiesEnabled())
            return false;
        if (!AbilityRando::isInitialized())
            AbilityRando::buildRemapTable(Randomizer::getSeed());
        return AbilityRando::isInitialized();
    }

    const char16_t* tryMakeToadetteMedalHuntMessage(const char* fileName, const char* key);

    const char16_t* tryGetSystemMessageOverride(const char* fileName, const char* key) {
        if (canUseAbilityHintSystem() && fileName && key && std::strcmp(fileName, "CollectList") == 0 && std::strcmp(key, "CollectList_SouvenirList") == 0)
            return u"Ability List";

        if (const char16_t* message = tryMakeToadetteMedalHuntMessage(fileName, key))
            return message;

        return nullptr;
    }

    static s32 getAbilityListCount() {
        // Show the full acquirable-ability list, including starting abilities (they render
        // as already-acquired); only the reward placement uses the narrower unlock pool.
        return canUseAbilityHintSystem() ? AbilityRando::getDisplayAbilityCount() : 0;
    }

    static const AbilityRando::AbilityEntry* getAbilityListEntryAt(s32 index) {
        if (!canUseAbilityHintSystem() || index < 0)
            return nullptr;
        return AbilityRando::getDisplayAbility(index);
    }

    static const char* getAbilityListDisplayName(const AbilityRando::AbilityEntry& ability) {
        return AbilityRando::getProgressiveDisplayName(ability);
    }

    static const AbilityRando::AbilityEntry* getRevealedAbilityForListIndex(
        const al::LayoutActor* layout, s32 index,
        const AbilityRando::AbilityEntry& fallbackAbility) {
        if (layout) {
            if (const AbilityRando::AbilityEntry* revealed = AbilityRando::getRevealedAbilityForListIndex(
                    getSouvenirListAccessor(layout), index))
                return revealed;

            if (AbilityRando::isAbilityUnlocked(getSouvenirListAccessor(layout),
                    fallbackAbility))
                return &fallbackAbility;
        }
        return nullptr;
    }

    static void writeAbilityName(sead::WFixedSafeString<0x100>* name,
        const AbilityRando::AbilityEntry& ability,
        const AbilityRando::AbilityEntry* revealedAbility = nullptr) {
        if (!name)
            return;

        const char* displayName = revealedAbility
            ? (revealedAbility->displayName ? revealedAbility->displayName : revealedAbility->key)
            : getAbilityListDisplayName(ability);
        name->convertFromMultiByteString(displayName, std::strlen(displayName));
    }

    static void makeAbilityHintKey(sead::FixedSafeString<128>* key,
        const AbilityRando::AbilityEntry& ability) {
        if (!key)
            return;
        key->format("HintAbility_%s", ability.key ? ability.key : "");
    }

    static bool hasHintUnlockFlag(const GameDataFile* file, const char* key) {
        if (!file || !key || !key[0])
            return false;
        // Accept the old exact flag too so existing in-progress saves retain
        // their bought hints. New writes use the checked key:value form.
        return file->customFindHackDictionaryKey(key)
            || file->customIsExistInHackDictionary(key);
    }

    static bool setHintUnlockFlag(GameDataFile* file, const char* key) {
        if (!file || !key || !key[0])
            return false;
        char record[128] = {};
        const s32 written = nn::util::SNPrintf(record, sizeof(record), "%s:1", key);
        if (written < 0 || written >= static_cast<s32>(sizeof(record))
            || !file->customUpsertHackDictionaryKey(key, record))
            return false;
        const char* verified = file->customFindHackDictionaryKey(key);
        return verified && std::strcmp(verified, record) == 0;
    }

    static const ShineDB::ShineEntry* findAbilityUnlockMoon(
        const AbilityRando::AbilityEntry& ability) {
        return AbilityRando::getMoonForAbility(ability);
    }

    static const ShineDB::ShineEntry* findAbilityUnlockMoon(
        const al::LayoutActor* layout, const AbilityRando::AbilityEntry& ability) {
        if (layout) {
            if (const ShineDB::ShineEntry* moon = AbilityRando::getMoonForAbility(getSouvenirListAccessor(layout), ability))
                return moon;
        }
        return findAbilityUnlockMoon(ability);
    }

    static const ShineDB::ShineEntry* findAbilityDisplayLocation(
        const al::LayoutActor* layout, const AbilityRando::AbilityEntry& ability) {
        const ShineDB::ShineEntry* moon = findAbilityUnlockMoon(layout, ability);
        const ShineDB::ShineEntry* location = findRandomizedLocationForItem(moon);
        return location ? location : moon;
    }

    static bool isAbilityHintUnlocked(GameDataFile* file,
        const AbilityRando::AbilityEntry& ability) {
        if (!file)
            return false;

        sead::FixedSafeString<128> key;
        makeAbilityHintKey(&key, ability);
        if (!key.isEmpty() && hasHintUnlockFlag(file, key.cstr()))
            return true;

        const ShineDB::ShineEntry* moon = findAbilityUnlockMoon(ability);
        GameDataFile::HintInfo* hint = moon ? file->customFindShine(moon->uid, moon->stage) : nullptr;
        return hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc;
    }

    static bool setAbilityHintUnlocked(GameDataFile* file,
        const AbilityRando::AbilityEntry& ability, const ShineDB::ShineEntry* moon = nullptr) {
        if (!file)
            return false;

        sead::FixedSafeString<128> key;
        makeAbilityHintKey(&key, ability);
        if (key.isEmpty())
            return false;

        if (!setHintUnlockFlag(file, key.cstr()))
            return false;
        if (!moon)
            moon = findAbilityUnlockMoon(ability);
        if (moon) {
            if (GameDataFile::HintInfo* hint = file->customFindShine(moon->uid, moon->stage))
                hint->hintStatus = GameDataFile::HintStatus_UnlockByNpc;
        }
        return true;
    }

    static bool isAbilityUnlockedForList(const al::LayoutActor* layout,
        const AbilityRando::AbilityEntry& ability) {
        return AbilityRando::isAbilityUnlocked(getSouvenirListAccessor(layout), ability);
    }

    static const char16_t* tryMakeAbilityHintMessage(const al::LayoutActor* layout,
        const AbilityRando::AbilityEntry& ability, bool allowLocked = false) {
        GameDataFile* file = getFile(getSouvenirListAccessor(layout));
        if (!allowLocked && !isAbilityHintUnlocked(file, ability))
            return nullptr;

        const ShineDB::ShineEntry* location = findAbilityDisplayLocation(layout, ability);
        if (!location || !location->name)
            return nullptr;

        static sead::WFixedSafeString<0x200> sHintMessage;
        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));

        sHintMessage = u"";
        sHintMessage.appendWithFormat(
            u"%s in %s",
            locationName.cstr(),
            ShineDB::getWorldNameFromId(location->worldId));
        return sHintMessage.cstr();
    }

    static SelectionActionState getAbilitySelectionActionState(const al::LayoutActor* layout,
        s32 index, const AbilityRando::AbilityEntry** outAbility = nullptr,
        const ShineDB::ShineEntry** outMoon = nullptr) {
        if (outAbility)
            *outAbility = nullptr;
        if (outMoon)
            *outMoon = nullptr;
        if (!canUseAbilityHintSystem() || !layout || index < 0)
            return SelectionActionState::None;

        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!ability)
            return SelectionActionState::None;

        const ShineDB::ShineEntry* moon = findAbilityUnlockMoon(layout, *ability);
        if (outAbility)
            *outAbility = ability;
        if (outMoon)
            *outMoon = moon;
        if (!moon)
            return SelectionActionState::None;
        if (isAbilityUnlockedForList(layout, *ability))
            return SelectionActionState::None;

        GameDataFile* file = getFile(getSouvenirListAccessor(layout));
        const bool isHinted = isAbilityHintUnlocked(file, *ability);
        const bool hintEnabled = isPriceEnabled(file, getHintCategoryForAbility());
        const bool purchaseEnabled = isPriceEnabled(file, getPurchaseCategory());
        if (isPurchaseBlockedSelection(moon))
            return isHinted   ? purchaseEnabled ? SelectionActionState::CannotBuy : SelectionActionState::None
                : hintEnabled ? SelectionActionState::HintOnly
                              : SelectionActionState::None;

        if (isHinted)
            return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
        if (hintEnabled && purchaseEnabled)
            return SelectionActionState::HintAndBuy;
        if (hintEnabled)
            return SelectionActionState::HintOnly;
        return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
    }

    static bool canUseCaptureHintSystem() {
        if (!Randomizer::isCaptureRandoEnabled())
            return false;
        if (!CaptureRando::isInitialized())
            CaptureRando::buildRemapTable(Randomizer::getSeed());
        return CaptureRando::isInitialized();
    }

    static void makeCaptureHintKey(sead::FixedSafeString<128>* key,
        const CaptureRando::CaptureEntry& capture) {
        if (!key)
            return;
        key->format("HintCapture_%s", capture.hackName ? capture.hackName : "");
    }

    static bool isCaptureHintUnlocked(GameDataFile* file,
        const CaptureRando::CaptureEntry& capture) {
        if (!file)
            return false;

        sead::FixedSafeString<128> key;
        makeCaptureHintKey(&key, capture);
        if (!key.isEmpty() && hasHintUnlockFlag(file, key.cstr()))
            return true;

        const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(capture);
        GameDataFile::HintInfo* hint = moon ? file->customFindShine(moon->uid, moon->stage) : nullptr;
        return hint && hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc;
    }

    static bool setCaptureHintUnlocked(GameDataFile* file,
        const CaptureRando::CaptureEntry& capture) {
        if (!file)
            return false;

        sead::FixedSafeString<128> key;
        makeCaptureHintKey(&key, capture);
        if (key.isEmpty())
            return false;
        if (!setHintUnlockFlag(file, key.cstr()))
            return false;
        if (const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(capture)) {
            if (GameDataFile::HintInfo* hint = file->customFindShine(moon->uid, moon->stage))
                hint->hintStatus = GameDataFile::HintStatus_UnlockByNpc;
        }
        return true;
    }

    static const ShineDB::ShineEntry* findCaptureUnlockMoon(
        const CaptureRando::CaptureEntry& capture) {
        return CaptureRando::getMoonForCapture(capture);
    }

    static const ShineDB::ShineEntry* findCaptureDisplayLocation(
        const CaptureRando::CaptureEntry& capture) {
        const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(capture);
        const ShineDB::ShineEntry* location = findRandomizedLocationForItem(moon);
        return location ? location : moon;
    }

    static bool isCaptureUnlockedForList(const al::LayoutActor* layout,
        const CaptureRando::CaptureEntry& capture) {
        return CaptureRando::isCaptureUnlocked(getHackListAccessor(layout), capture);
    }

    static const char16_t* tryMakeCaptureHintMessage(const al::LayoutActor* layout,
        const CaptureRando::CaptureEntry& capture, bool allowLocked = false) {
        GameDataFile* file = getFile(getHackListAccessor(layout));
        if (!allowLocked && !isCaptureHintUnlocked(file, capture))
            return nullptr;

        const ShineDB::ShineEntry* location = findCaptureDisplayLocation(capture);
        if (!location || !location->name)
            return nullptr;

        static sead::WFixedSafeString<0x200> sHintMessage;
        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));

        sHintMessage = u"";
        sHintMessage.appendWithFormat(
            u"%s in %s",
            locationName.cstr(),
            ShineDB::getWorldNameFromId(location->worldId));
        return sHintMessage.cstr();
    }

    static SelectionActionState getCaptureSelectionActionState(const al::LayoutActor* layout,
        s32 index, const CaptureRando::CaptureEntry** outCapture = nullptr,
        const ShineDB::ShineEntry** outMoon = nullptr) {
        if (outCapture)
            *outCapture = nullptr;
        if (outMoon)
            *outMoon = nullptr;
        if (!canUseCaptureHintSystem() || !layout || index < 0)
            return SelectionActionState::None;

        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!capture)
            return SelectionActionState::None;
        const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(*capture);
        if (outCapture)
            *outCapture = capture;
        if (outMoon)
            *outMoon = moon;
        if (!moon)
            return SelectionActionState::None;
        if (isCaptureUnlockedForList(layout, *capture))
            return SelectionActionState::None;

        GameDataFile* file = getFile(getHackListAccessor(layout));
        const bool isHinted = isCaptureHintUnlocked(file, *capture);
        const bool hintEnabled = isPriceEnabled(file, getHintCategoryForCapture());
        const bool purchaseEnabled = isPriceEnabled(file, getPurchaseCategory());
        if (isPurchaseBlockedSelection(moon))
            return isHinted   ? purchaseEnabled ? SelectionActionState::CannotBuy : SelectionActionState::None
                : hintEnabled ? SelectionActionState::HintOnly
                              : SelectionActionState::None;

        if (isHinted)
            return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
        if (hintEnabled && purchaseEnabled)
            return SelectionActionState::HintAndBuy;
        if (hintEnabled)
            return SelectionActionState::HintOnly;
        return purchaseEnabled ? SelectionActionState::BuyOnly : SelectionActionState::None;
    }

    // Route support for the Capture and Ability lists, on the same terms as the
    // moon list (findRouteHintLocation): a route is only for sale once the
    // location hint has been bought, and only when that location sits inside a
    // shuffled sub-area, where the kingdom name the hint gave away is not enough
    // to find it. The bought-route bits are keyed on the location itself, so a
    // route paid for from either list is already paid for in the other.
    static const ShineDB::ShineEntry* findCaptureRouteHintLocation(
        const al::LayoutActor* layout, s32 index) {
        if (!canUseCaptureHintSystem() || !layout || index < 0
            || !isPriceEnabled(getFile(getHackListAccessor(layout)), getPathCategory()))
            return nullptr;

        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!capture || isCaptureUnlockedForList(layout, *capture))
            return nullptr;
        if (!isCaptureHintUnlocked(getFile(getHackListAccessor(layout)), *capture))
            return nullptr;

        const ShineDB::ShineEntry* location = findCaptureDisplayLocation(*capture);
        return MoonRando::isLocationInSubArea(location) ? location : nullptr;
    }

    static const ShineDB::ShineEntry* findAbilityRouteHintLocation(
        const al::LayoutActor* layout, s32 index) {
        if (!canUseAbilityHintSystem() || !layout || index < 0
            || !isPriceEnabled(getFile(getSouvenirListAccessor(layout)), getPathCategory()))
            return nullptr;

        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!ability || isAbilityUnlockedForList(layout, *ability))
            return nullptr;
        if (!isAbilityHintUnlocked(getFile(getSouvenirListAccessor(layout)), *ability))
            return nullptr;

        const ShineDB::ShineEntry* location = findAbilityDisplayLocation(layout, *ability);
        return MoonRando::isLocationInSubArea(location) ? location : nullptr;
    }

    static PathActionState getCapturePathActionState(const al::LayoutActor* layout, s32 index,
        const ShineDB::ShineEntry** outLocation = nullptr) {
        if (outLocation)
            *outLocation = nullptr;

        GameDataFile* file = getFile(getHackListAccessor(layout));
        // Cheap row identity first: resolving the route location itself means a
        // full placement scan, which is what the cache is here to avoid.
        const CaptureRando::CaptureEntry* row = layout && index >= 0 ? getHackListCaptureAt(layout, index) : nullptr;

        if (sCaptureRouteActionCache.valid && sCaptureRouteActionCache.file == file
            && sCaptureRouteActionCache.entry == row) {
            if (outLocation)
                *outLocation = sCaptureRouteActionCache.location;
            return sCaptureRouteActionCache.state;
        }

        const ShineDB::ShineEntry* location = findCaptureRouteHintLocation(layout, index);
        const PathActionState state = !location ? PathActionState::None
            : isRouteHintBought(file, location) ? PathActionState::View
                                                : PathActionState::Buy;

        sCaptureRouteActionCache = { file, row, location, state, true };
        if (outLocation)
            *outLocation = location;
        return state;
    }

    static PathActionState getAbilityPathActionState(const al::LayoutActor* layout, s32 index,
        const ShineDB::ShineEntry** outLocation = nullptr) {
        if (outLocation)
            *outLocation = nullptr;

        GameDataFile* file = getFile(getSouvenirListAccessor(layout));
        // Same reason as getCapturePathActionState: key the cache on the cheap
        // row lookup, not on the scan it exists to avoid.
        const AbilityRando::AbilityEntry* row = index >= 0 ? getAbilityListEntryAt(index) : nullptr;

        if (sAbilityRouteActionCache.valid && sAbilityRouteActionCache.file == file
            && sAbilityRouteActionCache.entry == row) {
            if (outLocation)
                *outLocation = sAbilityRouteActionCache.location;
            return sAbilityRouteActionCache.state;
        }

        const ShineDB::ShineEntry* location = findAbilityRouteHintLocation(layout, index);
        const PathActionState state = !location ? PathActionState::None
            : isRouteHintBought(file, location) ? PathActionState::View
                                                : PathActionState::Buy;

        sAbilityRouteActionCache = { file, row, location, state, true };
        if (outLocation)
            *outLocation = location;
        return state;
    }

    static void writeCaptureName(
        sead::WFixedSafeString<0x100>* name, const CaptureRando::CaptureEntry& capture) {
        if (!name)
            return;
        name->convertFromMultiByteString(
            capture.displayName ? capture.displayName : capture.hackName,
            std::strlen(capture.displayName ? capture.displayName : capture.hackName));
    }

    static void applyHackListIconVisualState(al::LayoutActor* layout) {
        if (!canUseCaptureHintSystem() || !layout)
            return;

        void* list = getHackListCommonHorizontalList(layout);
        if (!list)
            return;

        const s32 partsNum = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListPartsNumOffset);
        if (partsNum <= 0 || partsNum > 64)
            return;

        al::LayoutActor** parts = *reinterpret_cast<al::LayoutActor***>(
            reinterpret_cast<u8*>(list) + cCommonHorizontalListPartsArrayOffset);
        if (!parts)
            return;

        const s32 startIndex = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListStartIdxOffset);
        const s32 count = getHackListCount(layout);

        for (s32 i = 0; i < partsNum; i++) {
            al::IUseLayout* partLayout = getLayoutInterface(parts[i]);
            if (!partLayout)
                continue;

            const s32 dataIndex = startIndex + i;
            bool isLocked = false;
            if (dataIndex >= 0 && dataIndex < count) {
                const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, dataIndex);
                if (capture && !isCaptureUnlockedForList(layout, *capture))
                    isLocked = true;
            }

            setPaneLocalAlphaByOffset(partLayout, "PicDummy", isLocked ? 96.0f : 255.0f);
        }
    }

    static void refreshHackListData(al::LayoutActor* layout) {
        if (!canUseCaptureHintSystem() || !layout)
            return;

        sead::WFixedSafeString<0x100>* names = getHackListNameData(layout);
        sead::WFixedSafeString<0x200>* tutorials = getHackListTutorialData(layout);
        bool* enableData = getHackListEnableData(layout);
        const s32 count = getHackListCount(layout);
        for (s32 i = 0; i < count; i++) {
            const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, i);
            if (!capture)
                continue;
            const bool isUnlocked = isCaptureUnlockedForList(layout, *capture);
            if (enableData)
                enableData[i] = true;
            if (isUnlocked)
                continue;

            writeCaptureName(names ? &names[i] : nullptr, *capture);
            if (tutorials) {
                const char16_t* hint = tryMakeCaptureHintMessage(layout, *capture);
                tutorials[i] = u"";
                if (hint)
                    tutorials[i].copy(sead::WSafeString(hint));
            }
        }

        if (enableData) {
            void* list = getHackListCommonHorizontalList(layout);
            commonHorizontalListSetEnableData(list, enableData);
            commonHorizontalListUpdateParts(list);
            applyHackListIconVisualState(layout);
        }
    }

    static void applyHackListGuideText(al::LayoutActor* layout) {
        if (!canUseCaptureHintSystem() || !layout)
            return;

        const s32 index = getHackListSelectedIndex(layout);
        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!capture || isCaptureUnlockedForList(layout, *capture))
            return;

        al::LayoutActor* guide = getHackListGuideLayout(layout);
        if (!guide)
            return;

        static sead::WFixedSafeString<0x100> sCaptureName;
        writeCaptureName(&sCaptureName, *capture);
        al::IUseLayout* guideLayout = getLayoutInterface(guide);
        if (!guideLayout)
            return;

        al::showPane(guideLayout, "TxtContent00");
        al::showPane(guideLayout, "TxtContent01");
        tryStartLayoutAction(getLayoutActionInterface(guide), "On", "OnOff");
        al::setPaneString(guideLayout, "TxtContent00", sCaptureName.cstr(), 0);

        const char16_t* hint = tryMakeCaptureHintMessage(layout, *capture);
        al::setPaneString(guideLayout, "TxtContent01", hint ? hint : u"", 0);
    }

    static void applySouvenirListIconVisualState(al::LayoutActor* layout) {
        if (!canUseAbilityHintSystem() || !layout)
            return;

        void* list = getSouvenirListCommonHorizontalList(layout);
        if (!list)
            return;

        const s32 partsNum = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListPartsNumOffset);
        if (partsNum <= 0 || partsNum > 64)
            return;

        al::LayoutActor** parts = *reinterpret_cast<al::LayoutActor***>(
            reinterpret_cast<u8*>(list) + cCommonHorizontalListPartsArrayOffset);
        if (!parts)
            return;

        const s32 startIndex = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(list) + cCommonHorizontalListStartIdxOffset);
        GameDataHolderAccessor accessor = getSouvenirListAccessor(layout);
        const s32 count = getSouvenirListCount(layout);

        for (s32 i = 0; i < partsNum; i++) {
            al::IUseLayout* partLayout = getLayoutInterface(parts[i]);
            if (!partLayout)
                continue;

            const s32 dataIndex = startIndex + i;
            bool isLocked = false;
            if (dataIndex >= 0 && dataIndex < count) {
                const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(dataIndex);
                if (ability && !AbilityRando::isAbilityUnlocked(accessor, *ability))
                    isLocked = true;
            }

            al::showPane(partLayout, "PicDummy");
            setPaneLocalAlphaByOffset(partLayout, "PicDummy",
                isLocked ? cAbilityLockedIconAlpha : cAbilityUnlockedIconAlpha);
        }
    }

    // The five Movement Abilities are appended to the Ability List, and their icons
    // are textures added to TextureSouvenir for this mod - no vanilla gift record
    // names them, so unlike every other row there is nothing already built to reuse.
    //
    // They are therefore built here, once per StageScene, from
    // prepareAbilityIconTextures below. NOT from refreshSouvenirListData: that runs
    // inside StageScene::exeCollectionList, a nerve execute with no current sead
    // heap, and al::createTextureInfo ends in operator new - which faults there
    // (observed as a crash in SharcArchiveRes/GetTlsValue on opening the list).
    //
    // The TextureInfos come out of the scene heap, so the cache is cleared - never
    // deleted - when the scene is torn down, exactly like the notification layouts.
    static void* sAbilityIconTextures[AbilityRando::AbilityId_Count] = {};

    void prepareAbilityIconTextures() {
        for (s32 i = 0; i < AbilityRando::AbilityId_Count; i++) {
            sAbilityIconTextures[i] = nullptr;
            const char* textureName = AbilityRando::getSouvenirTextureName(
                static_cast<AbilityRando::AbilityId>(i));
            if (!textureName)
                continue;
            using Fn = void* (*)(const char*, const char*, const char*);
            sAbilityIconTextures[i] = mainOffsetFunction<Fn>(cCreateTextureInfoOffset)(
                "ObjectData/TextureSouvenir", "TextureSouvenir", textureName);
        }
    }

    void invalidateAbilityIconTextures() {
        for (s32 i = 0; i < AbilityRando::AbilityId_Count; i++)
            sAbilityIconTextures[i] = nullptr;
    }

    static void* tryGetAbilityIconTexture(const AbilityRando::AbilityEntry& ability) {
        if (ability.id < 0 || ability.id >= AbilityRando::AbilityId_Count)
            return nullptr;
        return sAbilityIconTextures[ability.id];
    }

    static void refreshSouvenirListData(al::LayoutActor* layout) {
        if (!canUseAbilityHintSystem() || !layout)
            return;

        void* list = getSouvenirListCommonHorizontalList(layout);
        sead::WFixedSafeString<0x100>* numbers = getSouvenirListNumberData(layout);
        sead::WFixedSafeString<0x100>* names = getSouvenirListNameData(layout);
        sead::WFixedSafeString<0x100>* tutorials = getSouvenirListTutorialData(layout);
        bool* enableData = getSouvenirListEnableData(layout);
        void** textureData = getSouvenirListTextureData(layout);
        s32 count = getAbilityListCount();
        const s32 nativeCapacity = getSouvenirListCount(layout);
        if (nativeCapacity > 0 && nativeCapacity < count)
            count = nativeCapacity;
        if (!list || count <= 0 || !numbers || !names || !tutorials || !enableData)
            return;

        GameDataHolderAccessor accessor = getSouvenirListAccessor(layout);
        s32 unlockedCount = 0;
        for (s32 i = 0; i < count; i++) {
            const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(i);
            char numberText[8];
            nn::util::SNPrintf(numberText, sizeof(numberText), "%02d", i + 1);
            numbers[i].convertFromMultiByteString(numberText, std::strlen(numberText));
            names[i] = u"";
            tutorials[i] = u"";
            enableData[i] = ability != nullptr;
            if (!ability)
                continue;

            const AbilityRando::AbilityEntry* revealed = getRevealedAbilityForListIndex(layout, i, *ability);
            writeAbilityName(&names[i], *ability, revealed);
            if (textureData) {
                if (void* icon = tryGetAbilityIconTexture(*ability))
                    textureData[i] = icon;
            }
            const bool isUnlocked = AbilityRando::isAbilityUnlocked(accessor, *ability);
            if (isUnlocked) {
                unlockedCount++;
                continue;
            }

            if (const char16_t* hint = tryMakeAbilityHintMessage(layout, *ability))
                tutorials[i].copy(sead::WSafeString(hint));
        }

        setSouvenirListCount(layout, count);
        setCounterAndDenominator(getSouvenirListCounterLayout(layout), unlockedCount, count);
        commonHorizontalListInitDataNoResetSelected(list, count);
        commonHorizontalListSetEnableData(list, enableData);
        commonHorizontalListAddStringData(list, numbers, "TxtNumber");
        if (textureData)
            commonHorizontalListSetImageData(list, textureData, "PicDummy");
        commonHorizontalListUpdateParts(list);
        applySouvenirListIconVisualState(layout);
    }

    static void applySouvenirListGuideText(al::LayoutActor* layout) {
        if (!canUseAbilityHintSystem() || !layout)
            return;

        const s32 index = getSouvenirListSelectedIndex(layout);
        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!ability)
            return;

        al::LayoutActor* guide = getSouvenirListGuideLayout(layout);
        if (!guide)
            return;

        al::IUseLayout* guideLayout = getLayoutInterface(guide);
        if (!guideLayout)
            return;

        static sead::WFixedSafeString<0x100> sAbilityName;
        writeAbilityName(&sAbilityName, *ability,
            getRevealedAbilityForListIndex(layout, index, *ability));

        al::showPane(guideLayout, "TxtContent00");
        al::showPane(guideLayout, "TxtContent01");
        tryStartLayoutAction(getLayoutActionInterface(guide), "On", "OnOff");
        al::setPaneString(guideLayout, "TxtContent00", sAbilityName.cstr(), 0);

        const char16_t* hint = nullptr;
        if (!isAbilityUnlockedForList(layout, *ability))
            hint = tryMakeAbilityHintMessage(layout, *ability);
        al::setPaneString(guideLayout, "TxtContent01", hint ? hint : u"", 0);
    }

    static bool containsString(const char* haystack, const char* needle) {
        return haystack && needle && std::strstr(haystack, needle);
    }

    static bool isLikelyShineListName(const char* name) {
        return containsString(name, "Collection") || containsString(name, "Shine") || containsString(name, "PowerMoon") || containsString(name, "MoonList");
    }

    static s32 inferCurrentShineListWorld(GameDataFile* file) {
        if (!file)
            return -1;
        const s32 displayWorldId = file->getCurrentWorldIdForDisp();
        if (displayWorldId >= 0)
            return displayWorldId;
        return file->getCurrentWorldId();
    }

    static s32 inferCurrentShineListWorld(const al::LayoutActor* layout) {
        return inferCurrentShineListWorld(getFile(GameDataHolderAccessor(layout)));
    }

    static void logListNameOnce(CommonVerticalList* list) {
        if (!list || !list->mRootActor)
            return;

        const char* name = list->mRootActor->getName();
        if (!name)
            name = "<null>";

        for (s32 i = 0; i < sLoggedListNameCount; i++)
            if (sLoggedListNames[i] == name || std::strcmp(sLoggedListNames[i], name) == 0)
                return;

        if (sLoggedListNameCount < (s32)(sizeof(sLoggedListNames) / sizeof(sLoggedListNames[0])))
            sLoggedListNames[sLoggedListNameCount++] = name;

        Randomizer::logPush("hint system saw list name=%s data=%d parts=%d selected=%d",
            name, list->mDataCount, list->mListPartsNum, list->mCurSelected);
    }

    static void logMenuSelectNameOnce(MenuSelectParts* menu) {
        if (!menu || !menu->mLayoutActor)
            return;

        const char* name = menu->mLayoutActor->getName();
        if (!name)
            name = "<null>";

        for (s32 i = 0; i < sLoggedMenuSelectNameCount; i++)
            if (sLoggedMenuSelectNames[i] == name || std::strcmp(sLoggedMenuSelectNames[i], name) == 0)
                return;

        if (sLoggedMenuSelectNameCount < (s32)(sizeof(sLoggedMenuSelectNames) / sizeof(sLoggedMenuSelectNames[0])))
            sLoggedMenuSelectNames[sLoggedMenuSelectNameCount++] = name;

        Randomizer::logPush("hint system saw menu select name=%s amount=%d max=%d cursor=%d default=%d",
            name, menu->mMenuItemAmount, menu->mMax, menu->mCursorItemIndex, menu->mDefaultIndex);
    }

    static bool isShineList(CommonVerticalList* list) {
        if (!list || !list->mRootActor)
            return false;
        if (list->mRootActor == sObservedShineListLayout)
            return true;
        return isLikelyShineListName(list->mRootActor->getName());
    }

    static bool isLikelyMoonListMenu(MenuSelectParts* menu) {
        if (!menu || !menu->mLayoutActor)
            return false;
        if (isLikelyShineListName(menu->mLayoutActor->getName()))
            return true;
        return menu->mMenuItemAmount > 8 || menu->mMax > 8;
    }

    static void observeShineList(CommonVerticalList* list) {
        logListNameOnce(list);
        if (!isShineList(list))
            return;

        sActiveShineList = list;
        sObservedShineListLayout = list->mRootActor;
        if (sObservedWorldId < 0)
            sObservedWorldId = inferCurrentShineListWorld(list->mRootActor);
        refreshFooterPrompt();
    }

    static void observeMoonMenuSelect(MenuSelectParts* menu) {
        logMenuSelectNameOnce(menu);
        if (!isLikelyMoonListMenu(menu))
            return;

        sActiveMoonMenuSelect = menu;
        sObservedShineListLayout = menu->mLayoutActor;
        sObservedWorldId = inferCurrentShineListWorld(menu->mLayoutActor);
        sObservedIndex = menu->mCursorItemIndex;
        refreshFooterPrompt();
    }

    static bool isObservedShineList(CommonVerticalList* list) {
        return list && list->mRootActor && list == sActiveShineList && isShineList(list);
    }

    static void logResult(const char* action, PurchaseResult result) {
        switch (result) {
        case PurchaseResult::Success:
            Randomizer::logPush("hint system %s ok", action);
            break;
        case PurchaseResult::AlreadyUnlocked:
            Randomizer::logPush("hint system %s skipped: already unlocked", action);
            break;
        case PurchaseResult::AlreadyAcquired:
            Randomizer::logPush("hint system %s skipped: already acquired", action);
            break;
        case PurchaseResult::NotEnoughFunds:
            Randomizer::logPush("hint system %s skipped: insufficient funds", action);
            break;
        case PurchaseResult::CannotBuyProgression:
            Randomizer::logPush("hint system %s skipped: story progression moon", action);
            break;
        case PurchaseResult::Disabled:
            Randomizer::logPush("hint system %s skipped: disabled", action);
            break;
        case PurchaseResult::StorageFailure:
            Randomizer::logPush("hint system %s failed: economy storage failure", action);
            break;
        case PurchaseResult::QuoteChanged:
            Randomizer::logPush("hint system %s skipped: price changed before confirmation", action);
            break;
        case PurchaseResult::InvalidSelection:
        default:
            Randomizer::logPush("hint system %s skipped: invalid selection", action);
            break;
        }
    }

    static bool hasPendingConfirmation() {
        return sPendingConfirmation.action != ConfirmAction::None;
    }

    bool isConfirmationDialogActive() {
        return hasPendingConfirmation();
    }

    bool isPollingConfirmationDialogInput() {
        return sIsPollingConfirmInput;
    }

    static void clearPendingConfirmation() {
        sPendingConfirmation = {};
        sConfirmIsClosing = false;
        sConfirmInputCooldown = 0;
    }

    // The list views (shine/hack/souvenir) can be force-closed by native code
    // out from under a confirmation dialog that just opened this same frame -
    // e.g. the player presses A (open a hint/buy confirmation) and B (leave
    // the page) close enough together that both register before the list's
    // own "input enabled" state catches up with the cancel. clearPendingConfirmation
    // alone only forgets our bookkeeping; it never told the native
    // al::WindowConfirm widget to close, so a dialog that had already called
    // windowConfirmAppearWithChoicingCancel stays visibly on screen, orphaned,
    // with the moon list already gone behind it. If the dialog has already
    // started closing on its own (the player chose an option), leave that
    // alone - only force it shut when it's still just sitting there awaiting
    // input.
    static void abortPendingConfirmationDialog() {
        if (!hasPendingConfirmation())
            return;

        if (!sConfirmIsClosing) {
            al::WindowConfirm* window = getStageWindowConfirm();
            if (window)
                windowConfirmTryCancel(window);
        }

        clearPendingConfirmation();
    }

    static void setDirectShineListViewActive(bool active) {
        if (sIsDirectShineListView == active)
            return;

        sIsDirectShineListView = active;
        // Rebuild the per-hint kingdom cache for this view (the seed/save may have
        // changed since it was last open). The bought-route bitset is cached off
        // the same save and reloaded on the same terms.
        invalidateHintKingdomCache();
        invalidateRouteHintBits();
        hideRouteHintView();
        if (!active) {
            resetCustomShineListViewCaches(const_cast<al::LayoutActor*>(sActiveDirectShineListLayout));
            sActiveDirectShineListLayout = nullptr;
            sActiveShineList = nullptr;
            sActiveMoonMenuSelect = nullptr;
            sLatestActionLayout = nullptr;
            sLatestActionWorldId = -1;
            sLatestActionIndex = -1;
            abortPendingConfirmationDialog();
            // A buy/hint input thrown in the same or next frame as this close
            // could otherwise still read the about-to-be-stale selection and
            // open (or even complete) a confirmation for a row the player is
            // no longer looking at.
            sSuppressNewConfirmationFrames = cSuppressNewConfirmationFrameMax;
            clearPendingShineListRefresh();
            sPaintingListRowsDirty = true;
            sPaintingListRowsValid = false;
            sPaintingListRowsRoleLayoutHash = 0;
        }
    }

    bool applyRemoteHintUnlock(GameDataFile* file, SharedUnlock unlock, s32 shineIndex,
        const char* flagKey, Randomizer::HintPriceCategory category, s32 amount) {
        if (!file)
            return false;

        if (unlock < SharedUnlock::EconomyOnly || unlock > SharedUnlock::FlagHint
            || !Randomizer::isValidHintPriceCategory(category) || amount <= 0)
            return false;

        const ShineDB::ShineEntry* sharedEntry = nullptr;
        switch (unlock) {
        case SharedUnlock::EconomyOnly:
            if (category != Randomizer::HintPriceCategory::Purchases)
                return false;
            break;
        case SharedUnlock::MoonHint:
            if (category != Randomizer::HintPriceCategory::Moons
                || !(sharedEntry = ShineDB::entryAt(shineIndex)))
                return false;
            break;
        case SharedUnlock::RouteHint:
            if (category != Randomizer::HintPriceCategory::Path
                || !(sharedEntry = ShineDB::entryAt(shineIndex)))
                return false;
            break;
        case SharedUnlock::FlagHint:
            if (!flagKey || !(sharedEntry = ShineDB::entryAt(shineIndex)))
                return false;
            if (category == Randomizer::HintPriceCategory::Captures) {
                if (std::strncmp(flagKey, "HintCapture_", 12) != 0 || !flagKey[12])
                    return false;
            } else if (category == Randomizer::HintPriceCategory::Abilities) {
                if (std::strncmp(flagKey, "HintAbility_", 12) != 0 || !flagKey[12])
                    return false;
            } else {
                return false;
            }
            break;
        }

        // The debit first and unconditionally. It belongs to the EVENT, which
        // the server hands out exactly once, so it must not be skipped just
        // because this save already happens to hold the unlock - otherwise a
        // team-mate buying something you had already bought yourself would cost
        // the shared pool nothing.
        if (!HintEconomy::applyRemotePurchase(file, category, amount))
            return false;

        bool applied = true;
        switch (unlock) {
        case SharedUnlock::EconomyOnly:
            // A bought moon/capture/ability - the thing itself arrives on its
            // own channel, so there is nothing more to do here.
            break;
        case SharedUnlock::MoonHint: {
            GameDataFile::HintInfo* hint = findHintInfoForEntry(file, sharedEntry);
            if (!hint) {
                applied = false;
            } else if (hint->hintStatus != GameDataFile::HintStatus_UnlockByNpc) {
                hint->hintStatus = GameDataFile::HintStatus_UnlockByNpc;
            }
            break;
        }
        case SharedUnlock::RouteHint: {
            if (!isRouteHintBought(file, sharedEntry)
                && !setRouteHintBought(file, sharedEntry))
                applied = false;
            break;
        }
        case SharedUnlock::FlagHint: {
            if (!hasHintUnlockFlag(file, flagKey) && !setHintUnlockFlag(file, flagKey)) {
                applied = false;
                break;
            }
            // Local capture/ability hint purchases also reveal the moon that
            // carries that unlock. Mirror that secondary state so the moon
            // list and route action agree on every team member's save.
            if (GameDataFile::HintInfo* hint = findHintInfoForEntry(file, sharedEntry))
                hint->hintStatus = GameDataFile::HintStatus_UnlockByNpc;
            break;
        }
        }

        if (applied) {
            // A revealed location is exactly when a route becomes purchasable,
            // same as on the buying side.
            invalidateRouteActionCache();
            GameDataFunction::setRequireSave(GameDataHolderWriter(file->getGameDataHolder()));
        }
        return applied;
    }

    void clearSeedSpecificState(GameDataFile* file) {
        if (!file)
            return;

        file->customRemoveHackDictionaryPrefix("HintAbility_");
        file->customRemoveHackDictionaryPrefix("HintCapture_");
        for (s32 chunk = 0; chunk < cRouteHintChunkCount; chunk++) {
            char key[16];
            makeRouteHintChunkKey(key, sizeof(key), chunk);
            file->customRemoveHackDictionaryKey(key);
        }
        for (s32 i = 0; i < 1024; i++)
            file->getHintList()[i].hintStatus = GameDataFile::HintStatus_Lock;

        if (!HintEconomy::resetProgressivePurchaseCounts(file))
            Randomizer::logPush("hint system failed to reset progressive purchase counters during seed reset");

        invalidateHintKingdomCache();
        invalidateRouteHintBits();
        hideRouteHintView();
        sPaintingListRowsDirty = true;
        sPaintingListRowsValid = false;
        sPaintingListRowsRoleLayoutHash = 0;
    }

    static void setDirectHackListViewActive(bool active, al::LayoutActor* layout) {
        if (active) {
            sIsDirectHackListView = true;
            if (layout)
                sActiveDirectHackListLayout = layout;
            return;
        }

        if (!sIsDirectHackListView)
            return;

        sIsDirectHackListView = false;
        sActiveDirectHackListLayout = nullptr;
        if (sPendingConfirmation.target == ConfirmTarget::Capture)
            abortPendingConfirmationDialog();
    }

    static void setDirectSouvenirListViewActive(bool active, al::LayoutActor* layout) {
        if (active) {
            sIsDirectSouvenirListView = true;
            if (layout)
                sActiveDirectSouvenirListLayout = layout;
            return;
        }

        if (!sIsDirectSouvenirListView)
            return;

        sIsDirectSouvenirListView = false;
        sActiveDirectSouvenirListLayout = nullptr;
        if (sPendingConfirmation.target == ConfirmTarget::Ability)
            abortPendingConfirmationDialog();
    }

    static void requestCollectionListCloseToGame() {
        sPendingCollectionListCloseFrames = cPendingCollectionListCloseFrameMax;
        setDirectShineListViewActive(false);
        setDirectHackListViewActive(false);
        setDirectSouvenirListViewActive(false);
        Randomizer::logPush("hint system requested collection list cancel pulse");
    }

    static void requestBoughtMoonPostCloseEffects(s32 worldId, s32 index,
        const ShineDB::ShineEntry* entry, bool showMoonOverlay) {
        sPendingBoughtMoonDemoEntry = entry;
        sPendingBoughtMoonOverlay = showMoonOverlay;
        sPendingShineCountAnimFrames = cPendingShineCountAnimFrameMax;

        StageScene* scene = getStageScene();
        Randomizer::logPush("hint system bought moon post effects world=%d index=%d entry=%s stageDemo=%p dot=%p",
            worldId, index, entry && entry->name ? entry->name : "<null>",
            scene ? scene->mDemoShine : nullptr,
            scene ? scene->mDemoDotShine : nullptr);
    }

    static void updatePendingCollectionListClose(StageScene* scene) {
        if (sPendingCollectionListCloseFrames <= 0)
            return;

        if (!scene || !al::isNerve(scene, mainOffsetNerve(cStageSceneNrvCollectionListOffset))) {
            sPendingCollectionListCloseFrames = 0;
            return;
        }

        sPendingCollectionListCloseFrames--;
    }

    static void updatePendingShineCountAnim(StageScene* scene) {
        if (sPendingShineCountAnimFrames <= 0)
            return;

        if (!scene) {
            sPendingBoughtMoonDemoEntry = nullptr;
            sPendingBoughtMoonOverlay = false;
            sPendingShineCountAnimFrames = 0;
            return;
        }

        const ShineDB::ShineEntry* demoEntry = sPendingBoughtMoonDemoEntry;
        if (al::isNerve(scene, mainOffsetNerve(cStageSceneNrvCollectionListOffset))) {
            sPendingShineCountAnimFrames--;
            return;
        }

        // Bought moons always use the shine-counter animation, never a shine-get cutscene.
        // A hint-bought moon is usually a remote moon (often a different kingdom) with no Shine
        // actor in the current scene to drive the demo. The only ways to play the cutscene are to
        // either borrow an actor that isn't a demo-ready Shine (crashes, see the StageScene fallback
        // that was removed) or construct a fully-initialized Shine actor by hand outside the normal
        // placement pipeline (high risk). Playing the demo only "sometimes" is a worse, inconsistent
        // experience, so we deliberately disable it and rely on the always-reliable counter path.
        static constexpr bool cEnableBoughtMoonShineGetDemo = false;
        if (cEnableBoughtMoonShineGetDemo && demoEntry && tryStartBoughtMoonShineGetDemo(scene, demoEntry)) {
            sPendingBoughtMoonDemoEntry = nullptr;
            sPendingBoughtMoonOverlay = false;
            sPendingShineCountAnimFrames = 0;
            return;
        }

        if (!scene->stageSceneLayout) {
            sPendingShineCountAnimFrames--;
            if (sPendingShineCountAnimFrames <= 0) {
                sPendingBoughtMoonDemoEntry = nullptr;
                sPendingBoughtMoonOverlay = false;
                Randomizer::logPush("hint system skipped shine count anim: missing layout");
            }
            return;
        }

        sPendingBoughtMoonDemoEntry = nullptr;
        scene->stageSceneLayout->updateCounterParts();
        scene->stageSceneLayout->startShineCountAnim(false);
        if (sPendingBoughtMoonOverlay && demoEntry) {
            const bool hasCompanionOverlay = CaptureRando::getCaptureCountForMoon(*demoEntry) > 0
                || AbilityRando::getAbilityCountForMoon(*demoEntry) > 0
                || CaptureRando::isFoolMoon(*demoEntry)
                || AbilityRando::isFoolMoon(*demoEntry);
            const ShineDB::ShineEntry* purchasedLocation = findRandomizedLocationForItem(demoEntry);
            MoonPurchaseOverlay::queue(*demoEntry, purchasedLocation, hasCompanionOverlay);
        }
        sPendingBoughtMoonOverlay = false;
        sPendingShineCountAnimFrames = 0;
        Randomizer::logPush("hint system started shine count anim");
    }

    bool shouldPulseCollectionListCancel() {
        if (sPendingCollectionListCloseFrames <= 0 || hasPendingConfirmation())
            return false;

        StageScene* scene = getStageScene();
        if (!scene || !al::isNerve(scene, mainOffsetNerve(cStageSceneNrvCollectionListOffset))) {
            sPendingCollectionListCloseFrames = 0;
            return false;
        }

        return true;
    }

    void observeShineActorInitInfo(const al::ActorInitInfo& info) {
        if (!info.placementInfo || !info.actorSceneInfo.demoDirector || !info.actorResourceHolder)
            return;

        sLatestShineActorInitInfo = info;
        sHasLatestShineActorInitInfo = true;
    }

    void resetStageContext() {
        sHasLatestShineActorInitInfo = false;
        clearBoughtMoonDemoState(false);
        sPendingBoughtMoonDemoEntry = nullptr;
        sPendingBoughtMoonOverlay = false;
        MoonPurchaseOverlay::clear();
        sActiveDirectHackListLayout = nullptr;
        sActiveDirectSouvenirListLayout = nullptr;
        sIsDirectHackListView = false;
        sIsDirectSouvenirListView = false;
        sPendingMoonListRestore = {};
        clearPendingShineListRefresh();
    }

    static bool tryStartBoughtMoonShineGetDemo(StageScene* scene,
        const ShineDB::ShineEntry* entry) {
        if (!scene || !entry)
            return false;

        const bool isMultiMoon = isMultiMoonEntry(entry);
        if (isMultiMoon) {
            Randomizer::logPush("hint system skipped shine get demo for bought multi moon: using count anim fallback entry=%s",
                entry->name ? entry->name : "<null>");
            return false;
        }
        if (!sHasLatestShineActorInitInfo) {
            Randomizer::logPush("hint system skipped shine get demo: missing actor init info");
            return false;
        }

        ProjectDemoDirector* director = reinterpret_cast<ProjectDemoDirector*>(sLatestShineActorInitInfo.actorSceneInfo.demoDirector);
        if (!director) {
            Randomizer::logPush("hint system skipped shine get demo: missing demo director");
            return false;
        }

        // Only use a genuine, demo-ready Shine. getDemoShineActor() applies the game's own
        // validity check, and director->mDemoShine is a real Shine*. scene->mDemoShine is typed
        // al::LiveActor* and may be a stale/uninitialized actor that is NOT a ready demo shine
        // (getDemoShineActor() returns null for it); reinterpret-casting it to Shine* and running
        // the native shine-get demo against it crashes inside StageScene::exeDemoShineGet.
        Shine* shine = getDemoShineActor(scene);
        if (!shine && director->mDemoShine) {
            shine = director->mDemoShine;
            Randomizer::logPush("hint system using ProjectDemoDirector demo shine fallback shine=%p",
                shine);
        }
        if (!shine) {
            Randomizer::logPush("hint system skipped shine get demo: missing native demo shine actor");
            return false;
        }

        Randomizer::logPush("hint system preparing shine get demo actor entry=%s director=%p shine=%p visual=%p",
            entry->name ? entry->name : "<null>", director, shine, scene->mDemoShine);

        saveBoughtMoonDemoShineState(shine);
        initShineInfoFromEntry(&sBoughtMoonDemoShineInfo, entry);
        const bool useGrandDemo = false;
        if (shine->curShineInfo)
            *shine->curShineInfo = sBoughtMoonDemoShineInfo;
        else
            shine->curShineInfo = &sBoughtMoonDemoShineInfo;

        const bool usePowerStarDemo = isPowerStarEntry(entry);
        shine->mIsGotShine = false;
        shine->mModelType = useGrandDemo ? Grand : Normal;
        // The native bought-list flow has no real in-world PowerStar actor to hand off to.
        // Reuse the stable normal ShineGet actor path for PowerStar-backed purchases instead
        // of the PowerStar GetFirst branch, which leaves Mario waiting on an invisible actor.
        const bool useNormalDemoFallbackForPowerStar = usePowerStarDemo;
        shine->mIsPowerStar = usePowerStarDemo && !useNormalDemoFallbackForPowerStar;
        shine->mIsHintPhoto = false;
        shine->mChangeStageInfo = nullptr;
        shine->_300 = nullptr;
        shine->mIsUseDemoCam = true;
        shine->mIsUseAppearDemoForce = false;

        bool requestedDemo = false;
        sBoughtMoonDemoShine = shine;
        sBoughtMoonDemoVisualActor = scene->mDemoShine;
        if (useNormalDemoFallbackForPowerStar)
            Randomizer::logPush(
                "hint system using normal ShineGet fallback for bought PowerStar entry=%s",
                entry->name ? entry->name : "<null>");
        if (useGrandDemo) {
            sBoughtMoonDemoGetActor = scene->mDemoShine;
            requestedDemo = requestStartDemoShineGrandGet(shine);
        } else {
            sBoughtMoonDemoGetActor = scene->mDemoShine;
            requestedDemo = requestStartDemoShineGet(shine);
        }
        if (!requestedDemo) {
            Randomizer::logPush("hint system skipped shine get demo: native demo request failed multi=%d grandDemo=%d",
                isMultiMoon, useGrandDemo);
            clearBoughtMoonDemoState(true);
            return false;
        }

        if (!sBoughtMoonDemoGetActor)
            sBoughtMoonDemoGetActor = scene->mDemoShine ? scene->mDemoShine : static_cast<al::LiveActor*>(shine);
        sSkippedBoughtPowerStarDemoRumble = false;
        sBoughtPowerStarDemoGetFirstFrames = 0;
        Randomizer::logPush("hint system requested native shine get demo multi=%d grandDemo=%d powerStar=%d actorCam=%p useDemoCam=%d getActor=%p visual=%p",
            isMultiMoon, useGrandDemo, usePowerStarDemo, shine->_300, shine->mIsUseDemoCam,
            sBoughtMoonDemoGetActor, sBoughtMoonDemoVisualActor);

        al::setNerve(scene, mainOffsetNerve(useGrandDemo ? cStageSceneNrvDemoShineGrandGetOffset : cStageSceneNrvDemoShineGetOffset));
        Randomizer::logPush("hint system set StageScene shine get nerve multi=%d grandDemo=%d",
            isMultiMoon, useGrandDemo);
        return true;
    }

    void observeShineListQuery(const al::LayoutActor* layout, s32 worldId, s32 index) {
        if (!layout || worldId < 0 || index < 0)
            return;

        sObservedShineListLayout = layout;
        sObservedWorldId = worldId;
        sObservedIndex = index;
    }

    // Toadette Medals page display override: the vanilla ShineList's per-world
    // population (updateShineListWorldInfo) has already filled this page with
    // Mushroom Kingdom's real rows/title by the time this runs; overwrite them
    // with the medal set. Row/title pane names ("TxtContent"/"TxtOption") mirror
    // this project's own CommonVerticalList/layout conventions elsewhere (e.g.
    // StageSceneStateFileSelect's settings list) - confirm against the real
    // ShineList archive if the page doesn't render as expected.
    static constexpr s32 kMaxToadetteMedalListRows = 64;
    static al::LayoutActor* sToadetteMedalListAppliedLayout = nullptr;
    static s32 sToadetteMedalListAppliedWorldId = -1;
    static s32 sToadetteMedalListAppliedCollectedCount = -1;

    // ShineListLayout owns the two top-right counters and its map as child
    // layouts. Neither counter means anything on the custom information pages
    // (they are not kingdoms: they have no regional coins, and their row count
    // is not a moon total), so both are hidden there along with the map.
    // Offsets confirmed against ShineListLayout::updateWorldInfo in
    // ida_context/Layout/ShineListLayout.md: this+61 (0x1E8) takes the
    // "TxtShine" picture font and the moon count, this+62 (0x1F0) takes
    // "TxtIcon" and the regional-coin count, this+59 (0x1D8) is MapTerrainLayout.
    static constexpr ptrdiff_t cShineListMoonCounterLayoutOffset = 0x1E8;
    static constexpr ptrdiff_t cShineListPurpleCoinCounterLayoutOffset = 0x1F0;
    static constexpr ptrdiff_t cShineListMapLayoutOffset = 0x1D8;
    // Map moon markers: ShineListLayout's constructor builds 110 "ShineListPos"
    // actors into the buffer at this+48 (0x180), counting them in this+94
    // (0x178), plus two "ShineListPosSelect" cursor markers at this+50 (0x190)
    // counted in this+98 (0x188). updateWorldInfo hides them all with
    // al::startAction(actor + 16, "Hide", nullptr) before re-showing the ones
    // its per-world data says exist.
    //
    // That "+ 16" in the disassembly is the al::IUseLayoutAction sub-object
    // inside al::LayoutActor, which C++ applies on its own when an
    // al::LayoutActor* is passed to al::startAction. Adding it by hand as well
    // lands 16 bytes past the sub-object and the vtable dispatch inside
    // startAction jumps through garbage - so these markers are passed as plain
    // al::LayoutActor* at offset 0, exactly like the row parts elsewhere here.
    static constexpr ptrdiff_t cShineListPosMarkerCountOffset = 0x178;
    static constexpr ptrdiff_t cShineListPosMarkerArrayOffset = 0x180;
    static constexpr ptrdiff_t cShineListPosSelectMarkerCountOffset = 0x188;
    static constexpr ptrdiff_t cShineListPosSelectMarkerArrayOffset = 0x190;
    // Cached CommonVerticalList::mCurSelected value used by
    // ShineListLayout::updatePosLayout to skip an unchanged selection.
    static constexpr ptrdiff_t cShineListLastSelectedIdxOffset = 0x204;

    static al::LayoutActor* getShineListChildLayout(al::LayoutActor* layout, ptrdiff_t offset) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::LayoutActor**>(
            reinterpret_cast<u8*>(layout) + offset);
    }

    static void setShineListChildVisible(al::LayoutActor* child, bool visible) {
        if (!child)
            return;

        using Fn = void (*)(al::IUseLayout*);
        Fn changeVisibility = mainOffsetFunction<Fn>(
            visible ? cShowPaneRootOffset : cHidePaneRootOffset);
        changeVisibility(static_cast<al::IUseLayout*>(child));
    }

    static void setShineListMapVisible(al::LayoutActor* layout, bool visible) {
        if (!layout)
            return;

        if (visible)
            al::showPane(layout, "ParMap");
        else
            al::hidePane(layout, "ParMap");
        setShineListChildVisible(
            getShineListChildLayout(layout, cShineListMapLayoutOffset), visible);
    }

    static void setShineListPurpleCoinCounterVisible(al::LayoutActor* layout, bool visible) {
        setShineListChildVisible(
            getShineListChildLayout(layout, cShineListPurpleCoinCounterLayoutOffset), visible);
    }

    static void setShineListMoonCounterVisible(al::LayoutActor* layout, bool visible) {
        setShineListChildVisible(
            getShineListChildLayout(layout, cShineListMoonCounterLayoutOffset), visible);
    }

    // Both counters and the map back on. Every exit path out of a custom page
    // has to restore all three, or the next kingdom page renders without them.
    static void restoreShineListKingdomPanes(al::LayoutActor* layout) {
        setShineListMapVisible(layout, true);
        setShineListPurpleCoinCounterVisible(layout, true);
        setShineListMoonCounterVisible(layout, true);
    }

    static void hideShineListCustomPagePanes(al::LayoutActor* layout) {
        setShineListMapVisible(layout, false);
        setShineListPurpleCoinCounterVisible(layout, false);
        setShineListMoonCounterVisible(layout, false);
    }

    static void hideShineListMarkerArray(al::LayoutActor* layout, ptrdiff_t countOffset,
        ptrdiff_t arrayOffset) {
        if (!layout)
            return;
        // sead::PtrArrayImpl { s32 count; s32 capacity; void** buffer; } inline
        // in the layout, hence count and buffer being 8 bytes apart.
        const s32 count = *reinterpret_cast<const s32*>(
            reinterpret_cast<const u8*>(layout) + countOffset);
        void** const* array
            = reinterpret_cast<void** const*>(reinterpret_cast<u8*>(layout) + arrayOffset);
        if (count <= 0 || !array || !*array)
            return;
        for (s32 i = 0; i < count; i++) {
            al::LayoutActor* marker = reinterpret_cast<al::LayoutActor*>((*array)[i]);
            if (!marker)
                continue;
            al::startAction(marker, "Hide", nullptr);
        }
    }

    // With moon rando on, the map's per-moon markers point at the PHYSICAL moon
    // slots while the list rows next to them name the randomized rewards, so
    // they are both meaningless and a spoiler. Play the same "Hide" action
    // updateWorldInfo itself uses on every marker. This has to be reasserted
    // per frame: updateWorldInfo (page change) and updatePosLayout (cursor
    // move) both re-show the markers their per-world data says exist.
    static void hideShineListMapMoonMarkers(al::LayoutActor* layout) {
        hideShineListMarkerArray(
            layout, cShineListPosMarkerCountOffset, cShineListPosMarkerArrayOffset);
        hideShineListMarkerArray(layout, cShineListPosSelectMarkerCountOffset,
            cShineListPosSelectMarkerArrayOffset);
    }

    // ShineListLayout's own per-row string/anim arrays, in the order
    // updateWorldInfo registers them: this+39 row text (TxtContent), +40/+41 the
    // two row-number panes, +42 TxtValue, +43 TxtIcon, +44 the "OnOff" anim
    // group, +45 the "State" anim group (ida_context/Layout/ShineListLayout.md).
    // Registrations are consumed positionally by CommonVerticalList::updateParts,
    // so a custom page that resets the registration counters and re-adds only its
    // own two arrays silently drops the other four. The row-number panes then
    // freeze on whatever the last native refresh left - which is why a scrolled
    // medal page still numbered its rows 01..13 while the scrollbar was halfway
    // down. Re-register the native arrays alongside ours so every pane the
    // vanilla page fills keeps tracking the scroll window; only TxtContent and
    // "State" are substituted.
    static constexpr ptrdiff_t cShineListRowNumber00ArrayOffset = 40 * 8;
    static constexpr ptrdiff_t cShineListRowNumber01ArrayOffset = 41 * 8;
    static constexpr ptrdiff_t cShineListRowValueArrayOffset = 42 * 8;
    static constexpr ptrdiff_t cShineListRowIconArrayOffset = 43 * 8;
    static constexpr ptrdiff_t cShineListRowOnOffAnimArrayOffset = 44 * 8;

    static const sead::WFixedSafeString<0x200>* getShineListNativeRowStrings(
        const al::LayoutActor* layout, ptrdiff_t offset) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<const sead::WFixedSafeString<0x200>* const*>(
            reinterpret_cast<const u8*>(layout) + offset);
    }

    static void addShineListGroupAnimData(
        CommonVerticalList* list, const sead::FixedSafeString<64>* data, const char* group) {
        if (!list || !data)
            return;
        using Fn = CommonVerticalList* (*)(CommonVerticalList*, const sead::FixedSafeString<64>*, const char*);
        mainOffsetFunction<Fn>(cCommonVerticalListAddGroupAnimDataOffset)(list, data, group);
    }

    // Scroll-state diagnostic. CommonVerticalList::down (ida_context/Layout/
    // CommonVerticalList.md) advances mTopSelectableIdx only when
    // top + mListPartsNum - 4 < mCurSelected, and refuses to scroll at all once
    // top == mDataCount + 3 - mListPartsNum; updateParts then binds part i to
    // data row (top + i - 1) and hides any part whose row falls outside
    // 1..mDataCount. Every custom page symptom (rows not advancing, blank rows,
    // rows past the end) is one of those four fields disagreeing with rowCount,
    // so log them whenever they change.
    static s32 sLoggedListScrollCur = -1;
    static s32 sLoggedListScrollTop = -1;

    static void logCustomShineListScrollState(const CommonVerticalList* list, s32 rowCount) {
        if (!list)
            return;
        if (list->mCurSelected == sLoggedListScrollCur
            && list->mTopSelectableIdx == sLoggedListScrollTop)
            return;
        sLoggedListScrollCur = list->mCurSelected;
        sLoggedListScrollTop = list->mTopSelectableIdx;
        Randomizer::logPush(
            "hint system custom list scroll cur=%d top=%d partsNum=%d partsSize=%d dataCount=%d "
            "rows=%d",
            list->mCurSelected, list->mTopSelectableIdx, list->mListPartsNum,
            list->mListPartsArraySize, list->mDataCount, rowCount);
    }

    static void registerCustomShineListRowData(al::LayoutActor* layout, CommonVerticalList* list,
        const sead::WFixedSafeString<0x200>* rowText, const sead::FixedSafeString<64>* rowState) {
        if (!list)
            return;

        list->mStringDataCount = 0;
        list->field_b0 = 0;
        list->addStringData(rowText, "TxtContent");
        if (const sead::WFixedSafeString<0x200>* numbers
            = getShineListNativeRowStrings(layout, cShineListRowNumber00ArrayOffset))
            list->addStringData(numbers, "TxtNumber00");
        if (const sead::WFixedSafeString<0x200>* numbers
            = getShineListNativeRowStrings(layout, cShineListRowNumber01ArrayOffset))
            list->addStringData(numbers, "TxtNumber01");
        if (const sead::WFixedSafeString<0x200>* values
            = getShineListNativeRowStrings(layout, cShineListRowValueArrayOffset))
            list->addStringData(values, "TxtValue");
        if (const sead::WFixedSafeString<0x200>* icons
            = getShineListNativeRowStrings(layout, cShineListRowIconArrayOffset))
            list->addStringData(icons, "TxtIcon");
        if (layout) {
            const sead::FixedSafeString<64>* onOff
                = *reinterpret_cast<const sead::FixedSafeString<64>* const*>(
                    reinterpret_cast<const u8*>(layout) + cShineListRowOnOffAnimArrayOffset);
            addShineListGroupAnimData(list, onOff, "OnOff");
        }
        addShineListGroupAnimData(list, rowState, "State");
        list->mStringsDirty = true;
        list->updateParts();
    }

    static void initCustomShineListData(CommonVerticalList* list, s32 rowCount) {
        if (!list)
            return;

        // initData clears both the live and cached selection/top pairs before
        // rebuilding the rows. Calling it by offset avoids hand-reproducing
        // those four adjacent fields (the public class declaration exposes
        // only three of them accurately).
        using Fn = void (*)(CommonVerticalList*, s32);
        mainOffsetFunction<Fn>(cCommonVerticalListInitDataOffset)(list, rowCount);
    }

    // ShineListLayout owns its kingdom-title al::RollParts as a child part at
    // this byte offset (this+51*8, ida_context/Layout/ShineListLayout.md's
    // constructor: "*((_QWORD *)this + 51) = v81;" right before
    // RollParts::RollParts(v81, this, ..., "ParRoll")). A previous attempt
    // wrote the "TxtRoll" pane directly on the ShineListLayout itself and had
    // no visible effect - that pane belongs to this RollParts CHILD actor, not
    // the parent, so the earlier call likely just silently found no such pane.
    static constexpr ptrdiff_t cShineListRollPartsOffset = 51 * 8;

    static al::RollParts* getShineListRollParts(const al::LayoutActor* layout) {
        if (!layout)
            return nullptr;
        return *reinterpret_cast<al::RollParts* const*>(
            reinterpret_cast<const u8*>(layout) + cShineListRollPartsOffset);
    }

    // ShineListLayout builds its title RollParts with the normal 14-page
    // message array. Optional list pages extend the page count later, so the
    // RollParts animation can otherwise read past that original allocation
    // while wrapping. Keep a permanent backing array large enough for every
    // world slot, copying the game's localized normal titles on first use.
    static const char16_t* sShineListTitleMessages[WorldMeta::sWorldCount] = {};

    static void ensureShineListTitleMessageCapacity(al::LayoutActor* layout) {
        const s32 pageCount = getShineListWorldCount();
        if (pageCount <= cNormalShineListWorldCount)
            return;

        al::RollParts* rollParts = getShineListRollParts(layout);
        if (!rollParts || rollParts->mMessages == sShineListTitleMessages)
            return;

        const char16_t** sourceMessages = rollParts->mMessages;
        const s32 sourceCount = rollParts->mMessageCount;
        for (s32 page = 0; page < cNormalShineListWorldCount; page++) {
            const bool hasSourceTitle = sourceMessages && page < sourceCount && sourceMessages[page];
            sShineListTitleMessages[page] = hasSourceTitle
                ? sourceMessages[page]
                : WorldMeta::getDisplayName16ForWorldId(page);
        }

        s32 page = cNormalShineListWorldCount;
        if (Randomizer::isToadetteMedalModeEnabled())
            sShineListTitleMessages[page++] = u"Toadette Medal Hunt";
        if (Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled())
            sShineListTitleMessages[page++] = u"Unlocked Paintings";
        if (Randomizer::isMoonRockKeyEnabled())
            sShineListTitleMessages[page++] = u"Moon Rock Keys";
        while (page < WorldMeta::sWorldCount)
            sShineListTitleMessages[page++] = u"";

        rollParts->mMessages = sShineListTitleMessages;
        rollParts->mMessageCount = pageCount;
    }

    static void applyToadetteMedalListView(al::LayoutActor* layout, CommonVerticalList* list, s32 worldId) {
        if (!layout)
            return;

        if (worldId != WorldMeta::WorldId_Mushroom || !Randomizer::isToadetteMedalModeEnabled()) {
            if (sToadetteMedalListAppliedLayout == layout) {
                restoreShineListKingdomPanes(layout);
                sToadetteMedalListAppliedLayout = nullptr;
            }
            return;
        }

        hideShineListCustomPagePanes(layout);

        // Title. The other two custom pages already write "TxtRoll" directly
        // (applyCustomShineListRows), and this page needs it for the same
        // reason: the RollParts title array is built ONCE in ShineListLayout's
        // constructor from tryGetWorldName(worldId) per page
        // (ida_context/Layout/ShineListLayout.md), and the constructor also
        // preselects the page whose worldId matches the CURRENT world. Opening
        // the list while standing in Mushroom Kingdom therefore renders this
        // page's header straight from that constructor-time cache - before
        // ensureShineListTitleMessageCapacity has swapped in our own titles -
        // so it read "Mushroom Kingdom" whenever the tryGetWorldName override
        // did not win at construction time. Writing the pane here is
        // independent of both the cache and that override.
        if (al::RollParts* rollParts = getShineListRollParts(layout))
            al::setPaneString(rollParts, "TxtRoll", u"Toadette Medal Hunt", 0);

        if (!list) {
            Randomizer::logPush("hint system Toadette Medal Hunt page: no CommonVerticalList found");
            return;
        }

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        const s32 collectedCount = file ? MoonRando::getToadetteMedalCollectedCount(file) : -1;

        // ShineListLayout::updatePosLayout (ida_context/Layout/ShineListLayout.md)
        // re-derives each visible row's "State" anim from Mushroom Kingdom's REAL
        // (near-empty) per-world hint data whenever the list cursor moves,
        // clobbering whatever we set below - so the row string/anim data and the
        // map-terrain hide have to be reasserted every call (every frame this
        // page is active), not just once when the page is (re)selected, or
        // moving the cursor after the initial apply silently reverts a collected
        // medal's row back to "Hint". Only the heavier structural setup
        // (initCustomShineListData's row-count/selection reset, and the visible
        // Hide/Wait transition it plays on every part) is gated to first-apply,
        // to avoid replaying that transition 60 times a second.
        const bool isFirstApply = sToadetteMedalListAppliedLayout != layout
            || sToadetteMedalListAppliedWorldId != worldId;

        static sead::WFixedSafeString<0x200> sRowText[kMaxToadetteMedalListRows];
        // Per-row visual state, bound via CommonVerticalList::addGroupAnimData's
        // "State" anim group - this is the real native mechanism the vanilla
        // ShineList uses to recolor a row when its moon is collected ("Shine"),
        // vs a revealed-but-uncollected hint ("Hint"), vs unrevealed ("Normal").
        // Confirmed via ida_context/Layout/ShineListLayout.md's per-row refresh
        // function: al::startAction(row, isGet ? "Shine" : (hinted ? "HintRock" :
        // "Hint"), "State"). Not a text suffix - appending "[Collected]" to the
        // row text was wrong.
        static sead::FixedSafeString<64> sRowState[kMaxToadetteMedalListRows];
        // One row per pool ENTRY. The medal progress itself is not shown here:
        // the shared moon counter is hidden on this page like the regional-coin
        // one, since neither is a moon or coin total. Medal progress lives on
        // the tracker overlay and in Cappy's progress message.
        const s32 entryCount = MoonRando::getToadetteMedalTotalCount();
        const s32 medalTotal = MoonRando::getToadetteMedalTotalValue();
        const s32 rowCount
            = entryCount < kMaxToadetteMedalListRows ? entryCount : kMaxToadetteMedalListRows;
        for (s32 i = 0; i < rowCount; i++) {
            const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(i);
            const MoonRando::Placement* placement = medal
                ? MoonRando::getPlacementForItem(medal->uid, medal->stage)
                : nullptr;
            const ShineDB::ShineEntry* location = placement ? placement->location : nullptr;
            const bool collected = file && medal && isOriginalMoonAcquired(file, medal);
            // Mirrors shouldRevealShineListName's own reveal gate (hint bought or
            // already collected) - findHintByWorldIndex already resolves this
            // (worldId, index) pair to the medal's own real hint entry.
            const bool revealed = collected || shouldRevealShineListName(layout, worldId, i);
            const s32 medalValue = MoonRando::getToadetteMedalValueAt(i);

            if (revealed && location && location->name) {
                sead::WFixedSafeString<0x100> locationName;
                locationName.convertFromMultiByteString(location->name, std::strlen(location->name));
                if (medalValue > 1) {
                    sRowText[i].format(u"%s (%s) x%d", locationName.cstr(),
                        WorldMeta::getDisplayName16ForWorldId(location->worldId), medalValue);
                } else {
                    sRowText[i].format(u"%s (%s)", locationName.cstr(),
                        WorldMeta::getDisplayName16ForWorldId(location->worldId));
                }
                sRowState[i].copy(collected ? "Shine" : "Hint");
            } else {
                sRowText[i].copy(u"???");
                sRowState[i].copy("Normal");
            }
        }

        if (isFirstApply)
            initCustomShineListData(list, rowCount);

        // addStringData/addGroupAnimData both append via an auto-incrementing
        // registration counter (CommonVerticalList::mStringDataCount / field_b0,
        // confirmed via their disassembly in ida_context/Layout/
        // CommonVerticalList.md) that only initDataNoResetSelected resets. Since
        // that call is skipped on repeat frames above, reset the two counters
        // directly instead - otherwise repeated calls would walk past the end of
        // the list's fixed-size internal registration arrays.
        list->mStringDataCount = 0;
        registerCustomShineListRowData(layout, list, sRowText, sRowState);
        logCustomShineListScrollState(list, rowCount);

        // Belt-and-suspenders direct per-row "State" anim: logging confirmed
        // the collected/state data fed into addGroupAnimData is correct (e.g.
        // a genuinely-collected medal computes collected=1/state="Shine"), yet
        // the row still rendered as not-collected - so updateParts()'s own
        // internal consumption of that registered data (ida_context/Layout/
        // CommonVerticalList.md's updateParts, offsets +0xA8/+0xB0) isn't
        // taking visual effect for some reason not yet identified. Apply the
        // exact same al::startAction(part, value, "State") call directly
        // ourselves, mirroring the proven-working pattern in
        // StageSceneStateFileSelect.cpp's applySettingsRowStyles.
        //
        // mListPartsArr is a virtualized window with a leading non-data part.
        // updateParts() maps part i to (top + i - 1), so using (top + i) here
        // shifted every state onto the following row and made collected medals
        // render as uncollected. Mirror the native mapping exactly.
        s32 visiblePartCount = list->mListPartsArr ? list->mListPartsNum : 0;
        if (visiblePartCount > list->mListPartsArraySize)
            visiblePartCount = list->mListPartsArraySize;
        for (s32 i = 0; i < visiblePartCount; i++) {
            const s32 dataRow = list->mTopSelectableIdx + i - 1;
            if (dataRow < 0 || dataRow >= rowCount)
                continue;
            if (al::LayoutActor* part = list->mListPartsArr[i]) {
                al::startAction(part, sRowState[dataRow].cstr(), "State");
                // updateParts() has already flushed the native row state by
                // this point. Flush our replacement action as well; otherwise
                // its pane changes wait until the next frame, where
                // ShineListLayout's Mushroom-data refresh overwrites them.
                using Fn = void (*)(al::LayoutActor*);
                mainOffsetFunction<Fn>(cUpdateLayoutPaneRecursiveOffset)(part);
            }
        }

        const bool changed = isFirstApply || sToadetteMedalListAppliedCollectedCount != collectedCount;
        if (changed) {
            // The native layout only reapplies its full collected-row treatment
            // when the selected index changes. Invalidate that cache so its
            // next control pass consults isGotShineWorldIndexHook above and
            // renders the medal's authentic Shine state immediately.
            *reinterpret_cast<s32*>(reinterpret_cast<u8*>(layout)
                + cShineListLastSelectedIdxOffset)
                = -1;
        }
        sToadetteMedalListAppliedLayout = layout;
        sToadetteMedalListAppliedWorldId = worldId;
        sToadetteMedalListAppliedCollectedCount = collectedCount;
        if (changed) {
            Randomizer::logPush(
                "hint system applied Toadette Medal Hunt page rows=%d/%d medals=%d/%d "
                "partsSize=%d topIdx=%d",
                rowCount, entryCount, collectedCount, medalTotal, list->mListPartsArraySize,
                list->mTopSelectableIdx);
            // Diagnostic: title/collected-color/map-hide were reported still
            // broken after the per-frame reassert fix. Dump every row's
            // computed collected/state so the next test can tell whether the
            // "collected" boolean itself is wrong (customIsGotShine mismatch)
            // vs. the anim-group mechanism not visually applying despite
            // correct data - two very different bugs that look identical
            // in-game.
            for (s32 i = 0; i < rowCount; i++) {
                const ShineDB::ShineEntry* medal = MoonRando::getToadetteMedalAt(i);
                const bool collected = file && medal && isOriginalMoonAcquired(file, medal);
                Randomizer::logPush("hint system  row=%d uid=%d stage=%s collected=%d state=%s",
                    i, medal ? medal->uid : -1, medal ? medal->stage : "<null>", collected,
                    sRowState[i].cstr());
            }
        }
    }

    struct CustomShineListViewCache {
        al::LayoutActor* layout = nullptr;
        s32 worldId = -1;
        s32 rowCount = -1;
        s32 currentCount = -1;
    };

    // The paintings page uses the same collection-list mechanics as Bowser
    // Medals: native updateWorldInfo first fills the layout with the hidden
    // world's normal rows, so we replace its row data and visual State group
    // every frame while the page is selected. (The Moon Rock Keys page does
    // NOT go through here - see applyMoonRockKeyListView below.)
    static void applyCustomShineListRows(al::LayoutActor* layout, CommonVerticalList* list,
        s32 worldId, s32 targetWorldId, const char16_t* title,
        sead::WFixedSafeString<0x200>* rowText, sead::FixedSafeString<64>* rowState,
        s32 rowCount, s32 currentCount, CustomShineListViewCache* cache) {
        if (!layout || !cache)
            return;

        if (worldId != targetWorldId) {
            if (cache->layout == layout) {
                restoreShineListKingdomPanes(layout);
                *cache = {};
            }
            return;
        }

        hideShineListCustomPagePanes(layout);
        if (!list || !rowText || !rowState)
            return;

        if (rowCount < 0)
            rowCount = 0;
        if (rowCount > kMaxToadetteMedalListRows)
            rowCount = kMaxToadetteMedalListRows;
        const bool isFirstApply = cache->layout != layout || cache->worldId != worldId
            || cache->rowCount != rowCount;

        if (isFirstApply) {
            // These pages reuse different hidden worlds. Keeping the previous
            // world's selected/top indices makes CommonVerticalList bind its
            // virtual parts to the wrong data rows during the transition.
            initCustomShineListData(list, rowCount);
        }

        registerCustomShineListRowData(layout, list, rowText, rowState);
        logCustomShineListScrollState(list, rowCount);

        s32 visiblePartCount = list->mListPartsArr ? list->mListPartsNum : 0;
        if (visiblePartCount > list->mListPartsArraySize)
            visiblePartCount = list->mListPartsArraySize;
        for (s32 i = 0; i < visiblePartCount; i++) {
            const s32 dataRow = list->mTopSelectableIdx + i - 1;
            if (dataRow < 0 || dataRow >= rowCount)
                continue;
            if (al::LayoutActor* part = list->mListPartsArr[i]) {
                al::startAction(part, rowState[dataRow].cstr(), "State");
                using Fn = void (*)(al::LayoutActor*);
                mainOffsetFunction<Fn>(cUpdateLayoutPaneRecursiveOffset)(part);
            }
        }

        if (title) {
            if (al::RollParts* rollParts = getShineListRollParts(layout))
                al::setPaneString(rollParts, "TxtRoll", title, 0);
        }

        const bool changed = isFirstApply || cache->currentCount != currentCount;
        if (changed) {
            *reinterpret_cast<s32*>(reinterpret_cast<u8*>(layout)
                + cShineListLastSelectedIdxOffset)
                = -1;
        }
        cache->layout = layout;
        cache->worldId = worldId;
        cache->rowCount = rowCount;
        cache->currentCount = currentCount;
    }

    static CustomShineListViewCache sPaintingListViewCache;
    static CustomShineListViewCache sMoonRockKeyListViewCache;
    static CustomShineListViewCache sSubareaKeyListViewCache;
    static s32 sPaintingListRowCount = 0;
    static s32 sPaintingListUnlockedCount = 0;

    static void resetCustomShineListViewCaches(al::LayoutActor* layout) {
        // The collection layout can be reused after closing. Restore these
        // panes and discard every page-specific binding so a later page starts
        // with a fresh row-to-part mapping.
        if (layout)
            restoreShineListKingdomPanes(layout);
        sToadetteMedalListAppliedLayout = nullptr;
        sToadetteMedalListAppliedWorldId = -1;
        sToadetteMedalListAppliedCollectedCount = -1;
        sPaintingListViewCache = {};
        sMoonRockKeyListViewCache = {};
        sSubareaKeyListViewCache = {};
        sPaintingListRowCount = 0;
        sPaintingListUnlockedCount = 0;
        sPaintingListRowsRoleLayoutHash = 0;
    }

    static void applyPaintingListView(al::LayoutActor* layout, CommonVerticalList* list, s32 worldId) {
        static sead::WFixedSafeString<0x200> sRowText[kMaxToadetteMedalListRows];
        static sead::FixedSafeString<64> sRowState[kMaxToadetteMedalListRows];

        if (isPaintingListWorld(worldId)) {
            const u32 roleLayoutHash = MoonRando::getMoonRoleLayoutHash();
            if (sPaintingListRowsRoleLayoutHash != roleLayoutHash) {
                sPaintingListRowsDirty = true;
                sPaintingListRowsValid = false;
                sPaintingListRowsRoleLayoutHash = roleLayoutHash;
            }
            GameDataFile* file = getFile(GameDataHolderAccessor(layout));
            if (sPaintingListRowsDirty) {
                const s32 total = PaintingRando::getPaintingCount();
                sPaintingListRowCount = total < kMaxToadetteMedalListRows
                    ? total
                    : kMaxToadetteMedalListRows;
                sPaintingListUnlockedCount = 0;
                for (s32 i = 0; i < sPaintingListRowCount; i++) {
                    const bool unlocked = file && PaintingRando::isPaintingUnlocked(file, i);
                    sPaintingListUnlockedRows[i] = unlocked;
                    if (unlocked)
                        sPaintingListUnlockedCount++;

                    const char* sourceKingdom = nullptr;
                    const char* destinationKingdom = nullptr;
                    if (unlocked && PaintingRando::getPaintingUnlockKingdomNames(i, &sourceKingdom, &destinationKingdom)) {
                        sead::WFixedSafeString<0x80> sourceName;
                        sead::WFixedSafeString<0x80> destinationName;
                        sourceName.convertFromMultiByteString(sourceKingdom, std::strlen(sourceKingdom));
                        destinationName.convertFromMultiByteString(destinationKingdom, std::strlen(destinationKingdom));
                        sRowText[i].format(u"%s - %s", sourceName.cstr(), destinationName.cstr());
                    } else {
                        sRowText[i].copy(u"???");
                    }
                    sRowState[i].copy(unlocked ? "Shine" : "Normal");
                }
                sPaintingListRowsValid = file != nullptr;
                sPaintingListRowsDirty = file == nullptr;
            }
        }

        applyCustomShineListRows(layout, list, worldId, WorldMeta::WorldId_Dark,
            u"Unlocked Paintings", sRowText, sRowState, sPaintingListRowCount,
            sPaintingListUnlockedCount, &sPaintingListViewCache);
    }

    // Moon Rock Keys reuses Darker Side's world slot. Its native update writes
    // Darker Side's fallback regional-coin icon into the shared counter before
    // hiding it, and that text can leak into the next kingdom page. Treat it
    // like the other special-information pages instead: supply its rows here
    // and keep that counter hidden for the whole visit.
    static void applyMoonRockKeyListView(al::LayoutActor* layout, CommonVerticalList* list, s32 worldId) {
        static sead::WFixedSafeString<0x200> sRowText[kMaxToadetteMedalListRows];
        static sead::FixedSafeString<64> sRowState[kMaxToadetteMedalListRows];

        s32 rowCount = 0;
        s32 collectedCount = 0;
        if (isMoonRockKeyListWorld(worldId)) {
            const s32 total = getMoonRockKeyCount();
            rowCount = total < kMaxToadetteMedalListRows ? total : kMaxToadetteMedalListRows;

            GameDataFile* file = getFile(GameDataHolderAccessor(layout));
            for (s32 i = 0; i < rowCount; i++) {
                const s32 kingdom = getMoonRockKeyKingdomAt(i);
                const ShineDB::ShineEntry* key = MoonRando::getMoonRockKeyForKingdom(kingdom);
                const bool collected = file && key && isEntryHintGot(file, key);
                const bool revealed = collected || shouldRevealShineListName(layout, worldId, i);
                if (collected)
                    collectedCount++;

                const char16_t* text = tryMakeMoonRockKeyRowText(
                    GameDataHolderAccessor(layout), worldId, i);
                if (text)
                    sRowText[i].copy(text);
                else if (kingdom >= 0)
                    sRowText[i].format(u"%s Moon Rock Key",
                        WorldMeta::getDisplayName16ForWorldId(kingdom));
                else
                    sRowText[i].copy(u"Moon Rock Key");

                sRowState[i].copy(collected ? "Shine" : (revealed ? "Hint" : "Normal"));
            }
        }

        applyCustomShineListRows(layout, list, worldId, WorldMeta::WorldId_Darker,
            u"Moon Rock Keys", sRowText, sRowState, rowCount, collectedCount,
            &sMoonRockKeyListViewCache);
    }

    static void applySubareaKeyListView(al::LayoutActor* layout, CommonVerticalList* list, s32 worldId) {
        static sead::WFixedSafeString<0x200> sRowText[kMaxToadetteMedalListRows];
        static sead::FixedSafeString<64> sRowState[kMaxToadetteMedalListRows];

        s32 rowCount = 0;
        s32 collectedCount = 0;
        if (isSubareaKeyListWorld(worldId)) {
            const s32 total = getSubareaKeyCount();
            rowCount = total < kMaxToadetteMedalListRows ? total : kMaxToadetteMedalListRows;

            GameDataFile* file = getFile(GameDataHolderAccessor(layout));
            for (s32 i = 0; i < rowCount; i++) {
                const ShineDB::ShineEntry* key = MoonRando::getSubareaKeyForSlot(i);
                const bool collected = file && key && isEntryHintGot(file, key);
                const bool revealed = collected || shouldRevealShineListName(layout, worldId, i);
                if (collected)
                    collectedCount++;

                const char16_t* text = tryMakeSubareaKeyRowText(
                    GameDataHolderAccessor(layout), worldId, i);
                if (text)
                    sRowText[i].copy(text);
                else
                    sRowText[i].copy(u"Subarea Key");

                sRowState[i].copy(collected ? "Shine" : (revealed ? "Hint" : "Normal"));
            }
        }

        // The host world id is a setting, not a constant, so unlike the other
        // custom pages the target is resolved rather than hardcoded. Callers
        // pass worldId = -1 to mean "clear this page", so with the feature off
        // the target must be something no worldId and no sentinel can equal -
        // otherwise -1 == -1 would read as "apply" and blank the live page.
        const s32 target = subareaKeyListWorldId();
        applyCustomShineListRows(layout, list, worldId, target >= 0 ? target : -2,
            u"Subarea Keys", sRowText, sRowState, rowCount, collectedCount,
            &sSubareaKeyListViewCache);
    }

    void observeShineListLayout(const al::LayoutActor* layout) {
        if (!sIsDirectShineListView || !layout)
            return;

        al::LayoutActor* mutableLayout = const_cast<al::LayoutActor*>(layout);
        ensureShineListTitleMessageCapacity(mutableLayout);
        CommonVerticalList* list = getShineListCommonVerticalList(layout);
        sActiveDirectShineListLayout = layout;
        sActiveShineList = list;
        sObservedShineListLayout = layout;
        sObservedWorldId = getSelectedShineListWorldId(layout);
        if (list && getListSelectedDataIndex(list) >= 0)
            sObservedIndex = getListSelectedDataIndex(list);
        restorePendingMoonListSelection(layout);
        // Clear a previously-applied custom page before applying the selected
        // one, so its hidden map/counter panes cannot leak into the next page.
        if (sObservedWorldId == WorldMeta::WorldId_Mushroom && Randomizer::isToadetteMedalModeEnabled()) {
            applyPaintingListView(mutableLayout, list, -1);
            applyMoonRockKeyListView(mutableLayout, list, -1);
            applySubareaKeyListView(mutableLayout, list, -1);
            applyToadetteMedalListView(mutableLayout, list, sObservedWorldId);
        } else if (isPaintingListWorld(sObservedWorldId)) {
            applyToadetteMedalListView(mutableLayout, list, -1);
            applyMoonRockKeyListView(mutableLayout, list, -1);
            applySubareaKeyListView(mutableLayout, list, -1);
            applyPaintingListView(mutableLayout, list, sObservedWorldId);
        } else if (isMoonRockKeyListWorld(sObservedWorldId)) {
            applyToadetteMedalListView(mutableLayout, list, -1);
            applyPaintingListView(mutableLayout, list, -1);
            applySubareaKeyListView(mutableLayout, list, -1);
            applyMoonRockKeyListView(mutableLayout, list, sObservedWorldId);
        } else if (isSubareaKeyListWorld(sObservedWorldId)) {
            applyToadetteMedalListView(mutableLayout, list, -1);
            applyPaintingListView(mutableLayout, list, -1);
            applyMoonRockKeyListView(mutableLayout, list, -1);
            applySubareaKeyListView(mutableLayout, list, sObservedWorldId);
        } else {
            applyToadetteMedalListView(mutableLayout, list, -1);
            applyPaintingListView(mutableLayout, list, -1);
            applyMoonRockKeyListView(mutableLayout, list, -1);
            applySubareaKeyListView(mutableLayout, list, -1);
        }

        // After the page-specific work, since the custom pages hide the map
        // wholesale and the kingdom pages keep the terrain but not the markers.
        hideShineListMapMoonMarkers(mutableLayout);

        if (!sLoggedDirectShineList) {
            const char* name = layout->getName();
            Randomizer::logPush("hint system saw ShineList layout=%s world=%d index=%d data=%d parts=%d",
                name ? name : "<null>", sObservedWorldId, sObservedIndex,
                list ? list->mDataCount : -1, list ? list->mListPartsNum : -1);
            sLoggedDirectShineList = true;
        }

        applyShineListFooterText(layout);
        refreshFooterPrompt();
    }

    static void observeHackListLayout(al::LayoutActor* layout) {
        if (!sIsDirectHackListView || !layout)
            return;

        sActiveDirectHackListLayout = layout;
        if (!sLoggedDirectHackList) {
            const char* name = layout->getName();
            Randomizer::logPush("hint system saw HackList layout=%s index=%d count=%d",
                name ? name : "<null>", getHackListSelectedIndex(layout), getHackListCount(layout));
            sLoggedDirectHackList = true;
        }

        applyHackListGuideText(layout);
        applyHackListFooterText(layout);
        refreshFooterPrompt();
    }

    static void observeSouvenirListLayout(al::LayoutActor* layout) {
        if (!sIsDirectSouvenirListView || !layout)
            return;

        sActiveDirectSouvenirListLayout = layout;
        if (!sLoggedDirectSouvenirList) {
            const char* name = layout->getName();
            Randomizer::logPush("hint system saw SouvenirList layout=%s index=%d count=%d abilities=%d",
                name ? name : "<null>", getSouvenirListSelectedIndex(layout),
                getSouvenirListCount(layout), getAbilityListCount());
            sLoggedDirectSouvenirList = true;
        }

        applySouvenirListGuideText(layout);
        applySouvenirListFooterText(layout);
        refreshFooterPrompt();
    }

    bool isMoonListActive() {
        if (sIsDirectShineListView && sActiveDirectShineListLayout && isShineListInputEnabled(sActiveDirectShineListLayout))
            return true;
        if (sIsDirectShineListView && isObservedShineList(sActiveShineList))
            return true;
        return sIsDirectShineListView && sActiveMoonMenuSelect && sActiveMoonMenuSelect->mLayoutActor && sActiveMoonMenuSelect->mLayoutActor->isAlive();
    }

    static bool isCaptureListActive() {
        if (!canUseCaptureHintSystem() || !sIsDirectHackListView || !sActiveDirectHackListLayout)
            return false;
        if (!sActiveDirectHackListLayout->isAlive())
            return false;
        const s32 index = getHackListSelectedIndex(sActiveDirectHackListLayout);
        return index >= 0 && index < getHackListCount(sActiveDirectHackListLayout);
    }

    static bool isAbilityListActive() {
        if (!canUseAbilityHintSystem() || !sIsDirectSouvenirListView || !sActiveDirectSouvenirListLayout)
            return false;
        if (!sActiveDirectSouvenirListLayout->isAlive())
            return false;
        const s32 index = getSouvenirListSelectedIndex(sActiveDirectSouvenirListLayout);
        return index >= 0 && index < getAbilityListCount();
    }

    const char16_t* tryMakeCaptureGuidePaneString(const al::IUseLayout* layout,
        const char* paneName) {
        if (!layout || !paneName || !sIsDirectHackListView || !sActiveDirectHackListLayout)
            return nullptr;
        if (!canUseCaptureHintSystem())
            return nullptr;

        const bool isGuidePane = std::strcmp(paneName, "TxtContent00") == 0 || std::strcmp(paneName, "TxtContent01") == 0;
        if (!isGuidePane)
            return nullptr;

        const s32 index = getHackListSelectedIndex(sActiveDirectHackListLayout);
        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(sActiveDirectHackListLayout, index);
        if (!capture || isCaptureUnlockedForList(sActiveDirectHackListLayout, *capture))
            return nullptr;

        if (std::strcmp(paneName, "TxtContent00") == 0) {
            static sead::WFixedSafeString<0x100> sName;
            writeCaptureName(&sName, *capture);
            return sName.cstr();
        }

        if (std::strcmp(paneName, "TxtContent01") == 0) {
            if (const char16_t* hint = tryMakeCaptureHintMessage(sActiveDirectHackListLayout, *capture))
                return hint;
            return u"";
        }

        return nullptr;
    }

    static void recordActionPromptTarget(const al::LayoutActor* layout, s32 worldId, s32 index) {
        sLatestActionLayout = layout;
        sLatestActionWorldId = worldId;
        sLatestActionIndex = index;
    }

    bool shouldShowActionPrompt(const al::LayoutActor* layout, s32 worldId, s32 index) {
        if (!canUseHintSystem() || !sIsDirectShineListView || !layout)
            return false;

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        if (!isPromptableSelection(file, worldId, index))
            return false;
        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        const ShineDB::ShineEntry* entry = findEntryFromHint(hint);
        if (!entry || !isPriceEnabled(file, getHintCategoryForShineEntry(entry)))
            return false;

        observeShineListQuery(layout, worldId, index);
        recordActionPromptTarget(layout, worldId, index);
        return true;
    }

    bool shouldRevealShineListName(const al::LayoutActor* layout, s32 worldId, s32 index) {
        if (!layout || worldId < 0 || index < 0)
            return false;

        if (sIsDirectShineListView) {
            sActiveDirectShineListLayout = layout;
            sActiveShineList = getShineListCommonVerticalList(layout);
            observeShineListQuery(layout, worldId, index);
        }

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        if (!hint)
            return false;

        const ShineDB::ShineEntry* entry = findEntryFromHint(hint);
        return hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc || isOriginalMoonAcquired(file, entry);
    }

    const ShineDB::ShineEntry* tryFindOriginalMoon(GameDataFile* file, s32 worldId, s32 index) {
        return findEntryFromHint(findHintByWorldIndex(file, worldId, index));
    }

    const ShineDB::ShineEntry* tryFindRandomizedLocation(GameDataFile* file, s32 worldId, s32 index) {
        const ShineDB::ShineEntry* original = tryFindOriginalMoon(file, worldId, index);
        return findRandomizedLocationForItem(original);
    }

    static void logSelectionMapping(GameDataFile* file, s32 worldId, s32 index, const char* action) {
        const ShineDB::ShineEntry* entry = tryFindOriginalMoon(file, worldId, index);
        const ShineDB::ShineEntry* location = tryFindRandomizedLocation(file, worldId, index);
        Randomizer::logPush(
            "hint system %s mapping world=%d index=%d display=%d item=%s uid=%d stage=%s -> location=%s uid=%d stage=%s",
            action ? action : "selection",
            worldId, index, index + 1,
            entry && entry->name ? entry->name : "<none>",
            entry ? entry->uid : -1,
            entry && entry->stage ? entry->stage : "<none>",
            location && location->name ? location->name : "<none>",
            location ? location->uid : -1,
            location && location->stage ? location->stage : "<none>");
    }

    const char16_t* tryMakeHintMessage(GameDataHolderAccessor accessor, s32 worldId, s32 index,
        bool allowLocked) {
        if (!Randomizer::isMoonRandoEnabled() || !MoonRando::isInitialized())
            return nullptr;

        GameDataFile* file = getFile(accessor);
        GameDataFile::HintInfo* hint = findHintByWorldIndex(file, worldId, index);
        if (!hint)
            return nullptr;
        const ShineDB::ShineEntry* item = findEntryFromHint(hint);
        const bool acquired = isOriginalMoonAcquired(file, item);
        // A newly collected reward may not have a bought-hint flag (or have
        // become the native "latest hint") yet.  Its row is already revealed,
        // so show its randomized location on the first list open rather than
        // falling back to only the original moon name until the list is reopened.
        if (!allowLocked && hint->hintStatus != GameDataFile::HintStatus_UnlockByNpc
            && file && file->getLatestHint() != hint && !acquired)
            return nullptr;

        const ShineDB::ShineEntry* location = tryFindRandomizedLocation(file, worldId, index);
        if (!location || !location->name)
            return nullptr;

        static sead::WFixedSafeString<0x200> sHintMessage;
        sead::WFixedSafeString<0x100> locationName;
        locationName.convertFromMultiByteString(location->name, std::strlen(location->name));

        sHintMessage = u"";
        if (isMoonRockKeyListWorld(worldId)) {
            const s32 kingdom = getMoonRockKeyKingdomAt(index);
            if (kingdom < 0)
                return nullptr;
            // Only reached for the footer detail line; the row itself already
            // names the exact location moon, so the footer keeps just the
            // kingdom-level summary.
            sHintMessage.appendWithFormat(u"%s Moon Rock Key in %s",
                WorldMeta::getDisplayName16ForWorldId(kingdom),
                WorldMeta::getDisplayName16ForWorldId(location->worldId));
        } else if (isSubareaKeyListWorld(worldId)) {
            s32 keyKingdom = -1;
            SubareaKeyDB::SubareaType type = EntranceDB2::SubareaType_None;
            if (!SubareaKeyDB::keySlotAt(index, &keyKingdom, &type))
                return nullptr;
            // Same reasoning as the Moon Rock Key footer: the row already names
            // the exact location, so this line stays at kingdom level.
            if (keyKingdom >= 0 && type != EntranceDB2::SubareaType_Count)
                sHintMessage.appendWithFormat(u"%s %s key in %s",
                    WorldMeta::getDisplayName16ForWorldId(keyKingdom),
                    SubareaKeyDB::typeName(type),
                    WorldMeta::getDisplayName16ForWorldId(location->worldId));
            else if (keyKingdom >= 0)
                sHintMessage.appendWithFormat(u"%s Subarea key in %s",
                    WorldMeta::getDisplayName16ForWorldId(keyKingdom),
                    WorldMeta::getDisplayName16ForWorldId(location->worldId));
            else
                sHintMessage.appendWithFormat(u"%s key in %s",
                    SubareaKeyDB::typeName(type),
                    WorldMeta::getDisplayName16ForWorldId(location->worldId));
        } else {
            sHintMessage.appendWithFormat(
                u"%s in %s",
                locationName.cstr(),
                ShineDB::getWorldNameFromId(location->worldId));
        }
        return sHintMessage.cstr();
    }

    const char16_t* tryMakeActionPrompt(const al::LayoutActor* layout,
        GameDataHolderAccessor accessor, s32 worldId, s32 index) {
        (void)layout;
        (void)accessor;
        (void)worldId;
        (void)index;
        return nullptr;
    }

    const char16_t* tryMakeStaticFooterPrompt() {
        return tryMakeFooterPrompt();
    }

    static const char16_t* tryMakeCaptureFooterPrompt() {
        const al::LayoutActor* layout = nullptr;
        s32 index = -1;
        if (!tryGetCurrentCaptureSelection(&layout, &index, nullptr))
            return nullptr;

        GameDataFile* file = getFile(getHackListAccessor(layout));
        const ShineDB::ShineEntry* moon = nullptr;
        const SelectionActionState state = getCaptureSelectionActionState(layout, index, nullptr, &moon);

        static sead::WFixedSafeString<0x200> sPrompt;
        const s32 balance = HintEconomy::getBalance(file);
        const Randomizer::HintPriceQuote hintQuote = getPriceQuote(file, getHintCategoryForCapture());
        const Randomizer::HintPriceQuote buyQuote = getPriceQuote(file, getPurchaseCategory());
        const PathActionState path = getCapturePathActionState(layout, index);
        return makeListFooterPrompt(
            sPrompt, state, balance, hintQuote, buyQuote, path,
            getPriceQuote(file, getPathCategory()));
    }

    static const char16_t* tryMakeAbilityFooterPrompt() {
        const al::LayoutActor* layout = nullptr;
        s32 index = -1;
        if (!tryGetCurrentAbilitySelection(&layout, &index, nullptr))
            return nullptr;

        GameDataFile* file = getFile(getSouvenirListAccessor(layout));
        const ShineDB::ShineEntry* moon = nullptr;
        const SelectionActionState state = getAbilitySelectionActionState(layout, index, nullptr, &moon);

        static sead::WFixedSafeString<0x200> sPrompt;
        const s32 balance = HintEconomy::getBalance(file);
        const Randomizer::HintPriceQuote hintQuote = getPriceQuote(file, getHintCategoryForAbility());
        const Randomizer::HintPriceQuote buyQuote = getPriceQuote(file, getPurchaseCategory());
        const PathActionState path = getAbilityPathActionState(layout, index);
        return makeListFooterPrompt(
            sPrompt, state, balance, hintQuote, buyQuote, path,
            getPriceQuote(file, getPathCategory()));
    }

    const char16_t* tryMakeFooterPrompt() {
        if (isAbilityListActive())
            return tryMakeAbilityFooterPrompt();

        if (isCaptureListActive())
            return tryMakeCaptureFooterPrompt();

        if (!isMoonListActive())
            return nullptr;

        const al::LayoutActor* layout = nullptr;
        s32 worldId = -1;
        s32 index = -1;
        if (!tryGetCurrentSelection(&layout, &worldId, &index, nullptr))
            return nullptr;

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        const ShineDB::ShineEntry* entry = nullptr;
        const SelectionActionState state = getSelectionActionState(file, worldId, index, nullptr, &entry);

        static sead::WFixedSafeString<0x200> sPrompt;
        const s32 balance = HintEconomy::getBalance(file);
        const Randomizer::HintPriceQuote hintQuote = getPriceQuote(
            file, getHintCategoryForShineEntry(entry));
        const Randomizer::HintPriceQuote buyQuote = getPriceQuote(file, getPurchaseCategory());
        const PathActionState path = getPathActionState(file, worldId, index);
        return makeListFooterPrompt(
            sPrompt, state, balance, hintQuote, buyQuote, path,
            getPriceQuote(file, getPathCategory()));
    }

    static void setConfirmationText(al::WindowConfirm* window, ConfirmAction action,
        ConfirmTarget target, const Randomizer::HintPriceQuote& quote) {
        if (!window)
            return;

        static sead::WFixedSafeString<0x100> sConfirmMessage;
        sConfirmMessage = u"";
        windowConfirmSetListNum(window, 2);
        windowConfirmSetCancelIdx(window, 1);
        if (action == ConfirmAction::Path) {
            sConfirmMessage.appendWithFormat(
                u"Spend %d %s to learn the way there?", quote.amount, getCurrencyWord(quote));
            windowConfirmSetTxtList(window, 0, u"Path");
        } else if (action == ConfirmAction::Buy) {
            const char16_t* message = u"Spend %d %s to buy this Power Moon?";
            if (target == ConfirmTarget::Capture)
                message = u"Spend %d %s to buy this capture?";
            else if (target == ConfirmTarget::Ability)
                message = u"Spend %d %s to buy this ability?";
            sConfirmMessage.appendWithFormat(message, quote.amount, getCurrencyWord(quote));
            windowConfirmSetTxtList(window, 0, u"Buy");
        } else {
            const char16_t* message = u"Spend %d %s for a hint?";
            if (target == ConfirmTarget::Capture)
                message = u"Spend %d %s for a capture hint?";
            else if (target == ConfirmTarget::Ability)
                message = u"Spend %d %s for an ability hint?";
            sConfirmMessage.appendWithFormat(message, quote.amount, getCurrencyWord(quote));
            windowConfirmSetTxtList(window, 0, u"Hint");
        }

        windowConfirmSetTxtMessage(window, sConfirmMessage.cstr());
        windowConfirmSetTxtList(window, 1, u"Cancel");
    }

    static bool beginMessageDialog(const char16_t* message) {
        if (hasPendingConfirmation())
            return true;

        al::WindowConfirm* window = getStageWindowConfirm();
        if (!window)
            return false;

        windowConfirmSetListNum(window, 1);
        windowConfirmSetCancelIdx(window, 0);
        windowConfirmSetTxtMessage(window, message);
        windowConfirmSetTxtList(window, 0, u"OK");
        windowConfirmAppearWithChoicingCancel(window);

        sPendingConfirmation.action = ConfirmAction::Message;
        sPendingConfirmation.target = ConfirmTarget::None;
        sPendingConfirmation.layout = nullptr;
        sPendingConfirmation.worldId = -1;
        sPendingConfirmation.index = -1;
        sConfirmIsClosing = false;
        sConfirmInputCooldown = 2;
        return true;
    }

    static bool beginInsufficientFundsDialog(const char* actionName,
        const Randomizer::HintPriceQuote& quote, s32 balance) {
        static sead::WFixedSafeString<0x100> sMessage;
        sMessage = u"";
        sMessage.appendWithFormat(u"Need %d %s. You have %d.",
            quote.amount, getCurrencyWord(quote), balance);

        Randomizer::logPush("hint system %s skipped: insufficient funds need=%d have=%d currency=%s",
            actionName, quote.amount, balance,
            Randomizer::getHintCurrencyModeName(quote.currency));
        return beginMessageDialog(sMessage.cstr());
    }

    static bool beginProgressionMoonBlockedDialog(const ShineDB::ShineEntry* entry) {
        const ShineDB::ShineEntry* location = findRandomizedLocationForItem(entry);
        Randomizer::logPush(
            "hint system buy skipped: story progression moon item=%s flags=%d location=%s flags=%d",
            entry && entry->name ? entry->name : "<none>",
            entry ? entry->flags : -1,
            location && location->name ? location->name : "<none>",
            location ? location->flags : -1);
        return beginMessageDialog(u"You cannot buy story progression moons.");
    }

    static bool beginConfirmation(ConfirmAction action, const al::LayoutActor* layout,
        s32 worldId, s32 index) {
        if (hasPendingConfirmation())
            return true;

        al::WindowConfirm* window = getStageWindowConfirm();
        if (!window)
            return false;

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        const ShineDB::ShineEntry* entry = nullptr;
        getSelectionActionState(file, worldId, index, nullptr, &entry);
        const Randomizer::HintPriceCategory category = action == ConfirmAction::Path
            ? getPathCategory()
            : action == ConfirmAction::Buy
            ? getPurchaseCategory()
            : getHintCategoryForShineEntry(entry);
        const Randomizer::HintPriceQuote quote = getPriceQuote(file, category);
        if (!quote.enabled)
            return false;
        setConfirmationText(window, action, ConfirmTarget::Shine, quote);
        windowConfirmAppearWithChoicingCancel(window);

        sPendingConfirmation.action = action;
        sPendingConfirmation.target = ConfirmTarget::Shine;
        sPendingConfirmation.layout = layout;
        sPendingConfirmation.worldId = worldId;
        sPendingConfirmation.index = index;
        sPendingConfirmation.category = category;
        sPendingConfirmation.quote = quote;
        sConfirmIsClosing = false;
        sConfirmInputCooldown = 2;

        CommonVerticalList* list = layout == sActiveDirectShineListLayout
            ? getShineListCommonVerticalList(layout)
            : nullptr;
        Randomizer::logPush(
            "hint system confirmation opened action=%d world=%d index=%d display=%d raw=%d top=%d entry=%s uid=%d stage=%s",
            static_cast<s32>(action), worldId, index, index + 1,
            list ? list->mCurSelected : -1,
            getListTopSelectedIdx(list),
            entry && entry->name ? entry->name : "<none>",
            entry ? entry->uid : -1,
            entry && entry->stage ? entry->stage : "<none>");
        return true;
    }

    static bool beginCaptureConfirmation(ConfirmAction action, const al::LayoutActor* layout,
        s32 index) {
        if (hasPendingConfirmation())
            return true;

        al::WindowConfirm* window = getStageWindowConfirm();
        if (!window)
            return false;

        const ShineDB::ShineEntry* moon = nullptr;
        const CaptureRando::CaptureEntry* capture = nullptr;
        getCaptureSelectionActionState(layout, index, &capture, &moon);
        const Randomizer::HintPriceCategory category = action == ConfirmAction::Path
            ? getPathCategory()
            : action == ConfirmAction::Buy
            ? getPurchaseCategory()
            : getHintCategoryForCapture();
        GameDataFile* file = getFile(getHackListAccessor(layout));
        const Randomizer::HintPriceQuote quote = getPriceQuote(file, category);
        if (!quote.enabled)
            return false;
        setConfirmationText(window, action, ConfirmTarget::Capture, quote);
        windowConfirmAppearWithChoicingCancel(window);

        sPendingConfirmation.action = action;
        sPendingConfirmation.target = ConfirmTarget::Capture;
        sPendingConfirmation.layout = layout;
        sPendingConfirmation.worldId = -1;
        sPendingConfirmation.index = index;
        sPendingConfirmation.category = category;
        sPendingConfirmation.quote = quote;
        sConfirmIsClosing = false;
        sConfirmInputCooldown = 2;

        Randomizer::logPush(
            "hint system capture confirmation opened action=%d index=%d capture=%s moon=%s uid=%d stage=%s",
            static_cast<s32>(action), index,
            capture && capture->displayName ? capture->displayName : "<none>",
            moon && moon->name ? moon->name : "<none>",
            moon ? moon->uid : -1,
            moon && moon->stage ? moon->stage : "<none>");
        return true;
    }

    static bool beginAbilityConfirmation(ConfirmAction action, const al::LayoutActor* layout,
        s32 index) {
        if (hasPendingConfirmation())
            return true;

        al::WindowConfirm* window = getStageWindowConfirm();
        if (!window)
            return false;

        const ShineDB::ShineEntry* moon = nullptr;
        const AbilityRando::AbilityEntry* ability = nullptr;
        getAbilitySelectionActionState(layout, index, &ability, &moon);
        const Randomizer::HintPriceCategory category = action == ConfirmAction::Path
            ? getPathCategory()
            : action == ConfirmAction::Buy
            ? getPurchaseCategory()
            : getHintCategoryForAbility();
        GameDataFile* file = getFile(getSouvenirListAccessor(layout));
        const Randomizer::HintPriceQuote quote = getPriceQuote(file, category);
        if (!quote.enabled)
            return false;
        setConfirmationText(window, action, ConfirmTarget::Ability, quote);
        windowConfirmAppearWithChoicingCancel(window);

        sPendingConfirmation.action = action;
        sPendingConfirmation.target = ConfirmTarget::Ability;
        sPendingConfirmation.layout = layout;
        sPendingConfirmation.worldId = -1;
        sPendingConfirmation.index = index;
        sPendingConfirmation.category = category;
        sPendingConfirmation.quote = quote;
        sConfirmIsClosing = false;
        sConfirmInputCooldown = 2;

        Randomizer::logPush(
            "hint system ability confirmation opened action=%d index=%d ability=%s moon=%s uid=%d stage=%s",
            static_cast<s32>(action), index,
            ability && ability->displayName ? ability->displayName : "<none>",
            moon && moon->name ? moon->name : "<none>",
            moon ? moon->uid : -1,
            moon && moon->stage ? moon->stage : "<none>");
        return true;
    }

    static void executePendingConfirmation() {
        const PendingConfirmation pending = sPendingConfirmation;
        clearPendingConfirmation();

        if (pending.action == ConfirmAction::Message)
            return;

        if (!pending.layout || pending.index < 0)
            return;

        const char* actionName = pending.action == ConfirmAction::Buy ? "buy"
            : pending.action == ConfirmAction::Path                   ? "route"
                                                                      : "hint";
        PurchaseResult result = PurchaseResult::InvalidSelection;
        const ShineDB::ShineEntry* shineBuyDemoEntry = nullptr;
        if (pending.action == ConfirmAction::Buy && pending.target == ConfirmTarget::Shine) {
            shineBuyDemoEntry = tryFindOriginalMoon(getFile(GameDataHolderAccessor(pending.layout)),
                pending.worldId, pending.index);
        }

        const ShineDB::ShineEntry* abilityBuyDemoEntry = nullptr;
        if (pending.action == ConfirmAction::Buy && pending.target == ConfirmTarget::Ability) {
            const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(pending.index);
            abilityBuyDemoEntry = ability ? findAbilityUnlockMoon(pending.layout, *ability) : nullptr;
        }

        const ShineDB::ShineEntry* captureBuyDemoEntry = nullptr;
        if (pending.action == ConfirmAction::Buy && pending.target == ConfirmTarget::Capture) {
            const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(pending.layout, pending.index);
            captureBuyDemoEntry = capture ? findCaptureUnlockMoon(*capture) : nullptr;
        }

        if (pending.target == ConfirmTarget::Ability) {
            result = pending.action == ConfirmAction::Path
                ? buyAbilityRouteHint(pending.layout, pending.index, &pending.quote)
                : pending.action == ConfirmAction::Buy
                ? buyAbility(getSouvenirListWriter(pending.layout), pending.index, &pending.quote)
                : buyAbilityHint(getSouvenirListWriter(pending.layout), pending.index, &pending.quote);
        } else if (pending.target == ConfirmTarget::Capture) {
            result = pending.action == ConfirmAction::Path
                ? buyCaptureRouteHint(pending.layout, pending.index, &pending.quote)
                : pending.action == ConfirmAction::Buy
                ? buyCapture(getHackListWriter(pending.layout), pending.index, &pending.quote)
                : buyCaptureHint(getHackListWriter(pending.layout), pending.index, &pending.quote);
        } else if (pending.worldId >= 0) {
            if (pending.action == ConfirmAction::Buy)
                result = buyMoon(GameDataHolderWriter(pending.layout), pending.worldId, pending.index,
                    &pending.quote);
            else if (pending.action == ConfirmAction::Path)
                result = buyRouteHint(GameDataHolderWriter(pending.layout), pending.worldId, pending.index,
                    &pending.quote);
            else
                result = buyHint(GameDataHolderWriter(pending.layout), pending.worldId, pending.index,
                    &pending.quote);
        }

        logResult(actionName, result);
        Randomizer::logPush("hint system confirmed %s target=%d world=%d index=%d",
            actionName, static_cast<s32>(pending.target), pending.worldId, pending.index);

        if (result != PurchaseResult::Success) {
            if (result == PurchaseResult::QuoteChanged)
                beginMessageDialog(u"The price changed. Please try again.");
            else if (result == PurchaseResult::StorageFailure)
                beginMessageDialog(u"Could not update hint purchase data. Please try again.");
            return;
        }

        if (pending.action == ConfirmAction::Buy) {
            const ShineDB::ShineEntry* demoEntry = nullptr;
            if (pending.target == ConfirmTarget::Ability) {
                const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(pending.index);
                demoEntry = abilityBuyDemoEntry ? abilityBuyDemoEntry : (ability ? findAbilityUnlockMoon(pending.layout, *ability) : nullptr);
                closeSouvenirList(pending.layout);
            } else if (pending.target == ConfirmTarget::Capture) {
                const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(pending.layout, pending.index);
                demoEntry = captureBuyDemoEntry ? captureBuyDemoEntry : (capture ? findCaptureUnlockMoon(*capture) : nullptr);
                closeHackList(pending.layout);
            } else {
                demoEntry = shineBuyDemoEntry ? shineBuyDemoEntry : tryFindOriginalMoon(getFile(GameDataHolderAccessor(pending.layout)), pending.worldId, pending.index);
                CommonVerticalList* list = getShineListCommonVerticalList(pending.layout);
                rememberMoonListSelection(pending.layout, pending.worldId, pending.index,
                    getListTopSelectedIdx(list));
                closeShineList(pending.layout);
            }
            requestBoughtMoonPostCloseEffects(pending.worldId, pending.index, demoEntry,
                pending.target == ConfirmTarget::Shine);
            requestCollectionListCloseToGame();
        } else {
            if (pending.target == ConfirmTarget::Ability) {
                al::LayoutActor* souvenirListLayout = const_cast<al::LayoutActor*>(pending.layout);
                refreshSouvenirListData(souvenirListLayout);
                applySouvenirListGuideText(souvenirListLayout);
                applySouvenirListFooterText(pending.layout);
            } else if (pending.target == ConfirmTarget::Capture) {
                al::LayoutActor* hackListLayout = const_cast<al::LayoutActor*>(pending.layout);
                refreshHackListData(hackListLayout);
                applyHackListGuideText(hackListLayout);
                applyHackListFooterText(pending.layout);
            } else {
                CommonVerticalList* list = pending.layout == sActiveDirectShineListLayout
                    ? getShineListCommonVerticalList(pending.layout)
                    : nullptr;
                requestShineListDeferredRefresh(
                    pending.layout, pending.worldId, pending.index, getListTopSelectedIdx(list));
                applyShineListFooterText(pending.layout);
            }
            refreshFooterPrompt();
        }
    }

    static void finishPendingConfirmation(bool accepted) {
        if (sPendingConfirmation.action == ConfirmAction::Message) {
            clearPendingConfirmation();
            refreshFooterPrompt();
            return;
        }

        if (!accepted) {
            clearPendingConfirmation();
            refreshFooterPrompt();
            return;
        }

        executePendingConfirmation();
    }

    static void updateConfirmationDialog() {
        if (!hasPendingConfirmation())
            return;

        StageScene* scene = getStageScene();
        al::WindowConfirm* window = scene ? scene->mWindowConfirm : nullptr;
        if (!scene || !window) {
            clearPendingConfirmation();
            return;
        }

        window->movement();

        if (sConfirmIsClosing) {
            if (!windowConfirmIsNerveEnd(window))
                return;

            const bool accepted = !windowConfirmIsCancelSelected(window);
            finishPendingConfirmation(accepted);
            return;
        }

        if (sConfirmInputCooldown > 0) {
            sConfirmInputCooldown--;
            return;
        }

        sIsPollingConfirmInput = true;
        if (isTriggerUiUp(scene)) {
            windowConfirmTryUp(window);
        } else if (isTriggerUiDown(scene)) {
            windowConfirmTryDown(window);
        } else if (isTriggerUiDecide(scene)) {
            const bool didClose = windowConfirmIsCancelSelected(window)
                ? windowConfirmTryCancel(window)
                : windowConfirmTryDecide(window);
            if (didClose)
                sConfirmIsClosing = true;
        } else if (isTriggerUiCancel(scene)) {
            if (windowConfirmTryCancel(window))
                sConfirmIsClosing = true;
        }
        sIsPollingConfirmInput = false;
    }

    PurchaseResult buyHint(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseHintSystem())
            return PurchaseResult::Disabled;

        GameDataFile* file = getFile(writer);
        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        if (!isActionableUnacquiredSelection(file, worldId, index, &hint, &entry))
            return entry ? PurchaseResult::AlreadyAcquired : PurchaseResult::InvalidSelection;
        if (hint->hintStatus == GameDataFile::HintStatus_UnlockByNpc)
            return PurchaseResult::AlreadyUnlocked;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getHintCategoryForShineEntry(entry),
            expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        hint->hintStatus = GameDataFile::HintStatus_UnlockByNpc;
        // A revealed location is exactly when a route becomes purchasable, so
        // the row's cached route state is now stale.
        invalidateRouteActionCache();
        // (Moon Rock Key rows need no extra invalidation here: the deferred
        // refresh re-runs the native page update, whose hooked row text
        // already reflects the new hint status.)
        logSelectionMapping(file, worldId, index, "hint");
        GameDataFunction::setRequireSave(writer);
        // Keyed by ShineDB index rather than the (worldId, index) row that was
        // clicked: a row number depends on how each player's list was built,
        // the table index does not.
        return finishPayment(file, &transaction, true, SharedUnlock::MoonHint,
            ShineDB::findIndex(entry));
    }

    // The purchase itself, once a list has resolved which location the player is
    // buying a way to. Shared by the moon, capture, and ability rows; the bought
    // bit is keyed on the location, so paying from one list covers all of them.
    static PurchaseResult buyRouteHintToLocation(GameDataHolderWriter writer, GameDataFile* file,
        const ShineDB::ShineEntry* location, PathActionState path, const char* listName,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (path == PathActionState::None || !location || !file)
            return PurchaseResult::InvalidSelection;
        if (path == PathActionState::View)
            return PurchaseResult::AlreadyUnlocked;

        // Prove there is something to sell before taking payment. A stage can
        // sit outside the overworld yet have no loading-zone route into it at
        // all (an Odyssey interior, a cutscene-only room); charging for "no
        // route through loading zones leads here" would be a straight rip-off.
        static MoonRando::AreaRouteSet sRoutes;
        if (!MoonRando::buildSeedRoutesToLocation(location, &sRoutes))
            return PurchaseResult::InvalidSelection;

        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getPathCategory(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        if (!setRouteHintBought(file, location)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::StorageFailure : refund;
        }
        invalidateRouteActionCache();

        Randomizer::logPush("hint system route bought list=%s location=%s stage=%s",
            listName,
            location->name ? location->name : "<none>",
            location->stage ? location->stage : "<none>");
        GameDataFunction::setRequireSave(writer);
        return finishPayment(file, &transaction, true, SharedUnlock::RouteHint,
            ShineDB::findIndex(location));
    }

    static PurchaseResult buyRouteHint(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseHintSystem()
            || !isPriceEnabled(getFile(writer), getPathCategory()))
            return PurchaseResult::Disabled;

        GameDataFile* file = getFile(writer);
        const ShineDB::ShineEntry* location = nullptr;
        const PathActionState path = getPathActionState(file, worldId, index, &location);
        return buyRouteHintToLocation(writer, file, location, path, "moon", expectedQuote);
    }

    static PurchaseResult buyCaptureRouteHint(const al::LayoutActor* layout, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseCaptureHintSystem())
            return PurchaseResult::Disabled;

        GameDataHolderWriter writer = getHackListWriter(layout);
        GameDataFile* file = getFile(writer);
        const ShineDB::ShineEntry* location = nullptr;
        const PathActionState path = getCapturePathActionState(layout, index, &location);
        return buyRouteHintToLocation(writer, file, location, path, "capture", expectedQuote);
    }

    static PurchaseResult buyAbilityRouteHint(const al::LayoutActor* layout, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseAbilityHintSystem())
            return PurchaseResult::Disabled;

        GameDataHolderWriter writer = getSouvenirListWriter(layout);
        GameDataFile* file = getFile(writer);
        const ShineDB::ShineEntry* location = nullptr;
        const PathActionState path = getAbilityPathActionState(layout, index, &location);
        return buyRouteHintToLocation(writer, file, location, path, "ability", expectedQuote);
    }

    // The route panel is informational, not a modal: it is dismissed by B, by
    // moving to another row, or by leaving the list (see updateRouteHintView).
    // Which list opened it decides how "another row" is spelled - the moon list
    // identifies a row by (world, index), the capture and ability lists by their
    // index alone - so the panel remembers its origin.
    enum class RouteViewSource
    {
        None,
        Moon,
        Capture,
        Ability,
    };
    static RouteViewSource sRouteViewSource = RouteViewSource::None;
    static s32 sRouteViewWorldId = -1;
    static s32 sRouteViewIndex = -1;

    // The overlay is shared with the "Kingdom Checks" menu, so only ever take it
    // down when this system is the one that put it up.
    static void hideRouteHintView() {
        if (sRouteViewSource == RouteViewSource::None)
            return;
        sRouteViewSource = RouteViewSource::None;
        sRouteViewWorldId = -1;
        sRouteViewIndex = -1;
        PathHintOverlay::hide();
    }

    // Seed routes, not save-state routes: a bought hint describes how the
    // shuffle wired this seed, so it reads the same whenever it is opened and
    // does not change with where Mario is standing or what he can do.
    static void showRouteHintPanel(const ShineDB::ShineEntry* location, const char* fallbackName) {
        char title[128];
        nn::util::SNPrintf(title, sizeof(title), "Route to %s",
            location->name && location->name[0] ? location->name : fallbackName);

        static MoonRando::AreaRouteSet sRoutes;
        if (MoonRando::buildSeedRoutesToLocation(location, &sRoutes))
            PathHintOverlay::show(title, sRoutes);
        else
            PathHintOverlay::showMessage(title, "No route through loading zones leads here.");
    }

    static void showRouteHintView(const al::LayoutActor* layout, s32 worldId, s32 index) {
        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        const ShineDB::ShineEntry* location = nullptr;
        if (getPathActionState(file, worldId, index, &location) != PathActionState::View || !location)
            return;

        showRouteHintPanel(location, "this Power Moon");
        sRouteViewSource = RouteViewSource::Moon;
        sRouteViewWorldId = worldId;
        sRouteViewIndex = index;
    }

    static void showCaptureRouteHintView(const al::LayoutActor* layout, s32 index) {
        const ShineDB::ShineEntry* location = nullptr;
        if (getCapturePathActionState(layout, index, &location) != PathActionState::View || !location)
            return;

        showRouteHintPanel(location, "this capture");
        sRouteViewSource = RouteViewSource::Capture;
        sRouteViewWorldId = -1;
        sRouteViewIndex = index;
    }

    static void showAbilityRouteHintView(const al::LayoutActor* layout, s32 index) {
        const ShineDB::ShineEntry* location = nullptr;
        if (getAbilityPathActionState(layout, index, &location) != PathActionState::View || !location)
            return;

        showRouteHintPanel(location, "this ability");
        sRouteViewSource = RouteViewSource::Ability;
        sRouteViewWorldId = -1;
        sRouteViewIndex = index;
    }

    // Keeps the panel honest about what it is describing: the moment the player
    // scrolls to another row or leaves the collection list, the route on screen
    // stops belonging to what is selected.
    static void updateRouteHintView() {
        if (sRouteViewSource == RouteViewSource::None || !PathHintOverlay::isVisible())
            return;
        if (hasPendingConfirmation()) {
            hideRouteHintView();
            return;
        }

        s32 index = -1;
        switch (sRouteViewSource) {
        case RouteViewSource::Capture:
            if (!isCaptureListActive() || !tryGetCurrentCaptureSelection(nullptr, &index, nullptr)
                || index != sRouteViewIndex)
                hideRouteHintView();
            return;
        case RouteViewSource::Ability:
            if (!isAbilityListActive() || !tryGetCurrentAbilitySelection(nullptr, &index, nullptr)
                || index != sRouteViewIndex)
                hideRouteHintView();
            return;
        default:
            break;
        }

        if (!isMoonListActive()) {
            hideRouteHintView();
            return;
        }

        s32 worldId = -1;
        if (!tryGetCurrentSelection(nullptr, &worldId, &index, nullptr)
            || worldId != sRouteViewWorldId || index != sRouteViewIndex)
            hideRouteHintView();
    }

    PurchaseResult buyMoon(GameDataHolderWriter writer, s32 worldId, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseHintSystem())
            return PurchaseResult::Disabled;

        GameDataFile* file = getFile(writer);
        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        if (!isActionableUnacquiredSelection(file, worldId, index, &hint, &entry))
            return entry ? PurchaseResult::AlreadyAcquired : PurchaseResult::InvalidSelection;
        if (isPurchaseBlockedSelection(entry))
            return PurchaseResult::CannotBuyProgression;
        const ShineDB::ShineEntry* displayLocation = tryFindRandomizedLocation(file, worldId, index);
        if (!displayLocation)
            return PurchaseResult::InvalidSelection;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getPurchaseCategory(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        logSelectionMapping(file, worldId, index, "buy");
        Randomizer::logPush("hint system buy resolved world=%d index=%d display=%d entry=%s uid=%d stage=%s location=%s",
            worldId, index, index + 1,
            entry && entry->name ? entry->name : "<none>",
            entry ? entry->uid : -1,
            entry && entry->stage ? entry->stage : "<none>",
            displayLocation && displayLocation->name ? displayLocation->name : "<none>");
        if (!MoonHooks::grantMoonDirect(writer, entry, displayLocation)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::InvalidSelection : refund;
        }

        GameDataFunction::setRequireSave(writer);
        return finishPayment(file, &transaction, true);
    }

    static PurchaseResult buyCaptureHint(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseCaptureHintSystem())
            return PurchaseResult::Disabled;

        const al::LayoutActor* layout = sActiveDirectHackListLayout;
        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!layout || !capture)
            return PurchaseResult::InvalidSelection;
        if (isCaptureUnlockedForList(layout, *capture))
            return PurchaseResult::AlreadyAcquired;
        GameDataFile* file = getFile(writer);
        if (isCaptureHintUnlocked(file, *capture))
            return PurchaseResult::AlreadyUnlocked;

        const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(*capture);
        if (!moon)
            return PurchaseResult::InvalidSelection;
        if (!file)
            return PurchaseResult::InvalidSelection;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getHintCategoryForCapture(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        if (!setCaptureHintUnlocked(file, *capture)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::StorageFailure : refund;
        }
        // A revealed location is exactly when a route becomes purchasable.
        invalidateRouteActionCache();
        const ShineDB::ShineEntry* location = findCaptureDisplayLocation(*capture);
        Randomizer::logPush(
            "hint system capture hint index=%d capture=%s moon=%s uid=%d stage=%s location=%s uid=%d stage=%s",
            index,
            capture->displayName ? capture->displayName : capture->hackName,
            moon->name ? moon->name : "<none>",
            moon->uid,
            moon->stage ? moon->stage : "<none>",
            location && location->name ? location->name : "<none>",
            location ? location->uid : -1,
            location && location->stage ? location->stage : "<none>");
        GameDataFunction::setRequireSave(writer);
        // Capture and ability hints are stored as a hack-dictionary flag rather
        // than a table index, so the flag key IS the portable identifier.
        sead::FixedSafeString<128> sharedKey;
        makeCaptureHintKey(&sharedKey, *capture);
        return finishPayment(file, &transaction, true, SharedUnlock::FlagHint,
            ShineDB::findIndex(moon),
            sharedKey.cstr());
    }

    static PurchaseResult buyCapture(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseCaptureHintSystem())
            return PurchaseResult::Disabled;

        const al::LayoutActor* layout = sActiveDirectHackListLayout;
        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!layout || !capture)
            return PurchaseResult::InvalidSelection;
        if (isCaptureUnlockedForList(layout, *capture))
            return PurchaseResult::AlreadyAcquired;

        const ShineDB::ShineEntry* moon = findCaptureUnlockMoon(*capture);
        if (!moon)
            return PurchaseResult::InvalidSelection;
        if (isPurchaseBlockedSelection(moon))
            return PurchaseResult::CannotBuyProgression;

        GameDataFile* file = getFile(writer);
        const ShineDB::ShineEntry* displayLocation = findCaptureDisplayLocation(*capture);
        if (!file || !displayLocation)
            return PurchaseResult::InvalidSelection;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getPurchaseCategory(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        Randomizer::logPush(
            "hint system capture buy index=%d capture=%s moon=%s uid=%d stage=%s location=%s uid=%d stage=%s",
            index,
            capture->displayName ? capture->displayName : capture->hackName,
            moon->name ? moon->name : "<none>",
            moon->uid,
            moon->stage ? moon->stage : "<none>",
            displayLocation && displayLocation->name ? displayLocation->name : "<none>",
            displayLocation ? displayLocation->uid : -1,
            displayLocation && displayLocation->stage ? displayLocation->stage : "<none>");
        if (!MoonHooks::grantMoonDirect(writer, moon, displayLocation)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::InvalidSelection : refund;
        }

        CaptureRando::setCaptureUnlocked(writer, *capture, true);
        GameDataFunction::setRequireSave(writer);
        return finishPayment(file, &transaction, true);
    }

    static void setAbilityUnlockedRecursive(GameDataHolderWriter writer,
        const AbilityRando::AbilityEntry& ability) {
        if (ability.prerequisite >= 0) {
            if (const AbilityRando::AbilityEntry* prerequisite = AbilityRando::getAbility(ability.prerequisite))
                setAbilityUnlockedRecursive(writer, *prerequisite);
        }

        AbilityRando::setAbilityUnlocked(writer, ability, true);
    }

    static PurchaseResult buyAbilityHint(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseAbilityHintSystem())
            return PurchaseResult::Disabled;

        const al::LayoutActor* layout = sActiveDirectSouvenirListLayout;
        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!layout || !ability)
            return PurchaseResult::InvalidSelection;
        if (isAbilityUnlockedForList(layout, *ability))
            return PurchaseResult::AlreadyAcquired;

        GameDataFile* file = getFile(writer);
        if (isAbilityHintUnlocked(file, *ability))
            return PurchaseResult::AlreadyUnlocked;

        const ShineDB::ShineEntry* moon = findAbilityUnlockMoon(layout, *ability);
        if (!moon)
            return PurchaseResult::InvalidSelection;

        if (!file)
            return PurchaseResult::InvalidSelection;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getHintCategoryForAbility(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        if (!setAbilityHintUnlocked(file, *ability, moon)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::StorageFailure : refund;
        }
        // A revealed location is exactly when a route becomes purchasable.
        invalidateRouteActionCache();
        const ShineDB::ShineEntry* location = findAbilityDisplayLocation(layout, *ability);
        Randomizer::logPush(
            "hint system ability hint index=%d ability=%s moon=%s uid=%d stage=%s location=%s uid=%d stage=%s",
            index,
            ability->displayName ? ability->displayName : ability->key,
            moon->name ? moon->name : "<none>",
            moon->uid,
            moon->stage ? moon->stage : "<none>",
            location && location->name ? location->name : "<none>",
            location ? location->uid : -1,
            location && location->stage ? location->stage : "<none>");
        GameDataFunction::setRequireSave(writer);
        sead::FixedSafeString<128> sharedKey;
        makeAbilityHintKey(&sharedKey, *ability);
        return finishPayment(file, &transaction, true, SharedUnlock::FlagHint,
            ShineDB::findIndex(moon),
            sharedKey.cstr());
    }

    static PurchaseResult buyAbility(GameDataHolderWriter writer, s32 index,
        const Randomizer::HintPriceQuote* expectedQuote) {
        if (!canUseAbilityHintSystem())
            return PurchaseResult::Disabled;

        const al::LayoutActor* layout = sActiveDirectSouvenirListLayout;
        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!layout || !ability)
            return PurchaseResult::InvalidSelection;
        if (isAbilityUnlockedForList(layout, *ability))
            return PurchaseResult::AlreadyAcquired;

        const ShineDB::ShineEntry* moon = findAbilityUnlockMoon(layout, *ability);
        if (!moon)
            return PurchaseResult::InvalidSelection;
        if (isPurchaseBlockedSelection(moon))
            return PurchaseResult::CannotBuyProgression;

        GameDataFile* file = getFile(writer);
        const ShineDB::ShineEntry* displayLocation = findAbilityDisplayLocation(layout, *ability);
        if (!file || !displayLocation)
            return PurchaseResult::InvalidSelection;
        HintEconomy::PendingTransaction transaction {};
        const PurchaseResult payment = beginPayment(file, getPurchaseCategory(), expectedQuote, &transaction);
        if (payment != PurchaseResult::Success)
            return payment;
        Randomizer::logPush(
            "hint system ability buy index=%d ability=%s moon=%s uid=%d stage=%s location=%s uid=%d stage=%s",
            index,
            ability->displayName ? ability->displayName : ability->key,
            moon->name ? moon->name : "<none>",
            moon->uid,
            moon->stage ? moon->stage : "<none>",
            displayLocation && displayLocation->name ? displayLocation->name : "<none>",
            displayLocation ? displayLocation->uid : -1,
            displayLocation && displayLocation->stage ? displayLocation->stage : "<none>");
        if (!MoonHooks::grantMoonDirect(writer, moon, displayLocation)) {
            const PurchaseResult refund = finishPayment(file, &transaction, false);
            return refund == PurchaseResult::Success ? PurchaseResult::InvalidSelection : refund;
        }

        setAbilityUnlockedRecursive(writer, *ability);
        AbilityRando::recordAbilityReveal(writer, index, *ability);
        GameDataFunction::setRequireSave(writer);
        return finishPayment(file, &transaction, true);
    }

    static bool tryGetCurrentSelection(const al::LayoutActor** outLayout, s32* outWorldId,
        s32* outIndex, CommonVerticalList** outList) {
        if (outLayout)
            *outLayout = nullptr;
        if (outWorldId)
            *outWorldId = -1;
        if (outIndex)
            *outIndex = -1;
        if (outList)
            *outList = nullptr;

        if (!sIsDirectShineListView)
            return false;

        if (sActiveDirectShineListLayout && isShineListInputEnabled(sActiveDirectShineListLayout)) {
            CommonVerticalList* list = getShineListCommonVerticalList(sActiveDirectShineListLayout);
            const s32 worldId = getSelectedShineListWorldId(sActiveDirectShineListLayout);
            const s32 index = list ? getListSelectedDataIndex(list) : sObservedIndex;
            if (outLayout)
                *outLayout = sActiveDirectShineListLayout;
            if (outWorldId)
                *outWorldId = worldId;
            if (outIndex)
                *outIndex = index;
            if (outList)
                *outList = list;
            return worldId >= 0 && index >= 0;
        }

        CommonVerticalList* list = sActiveShineList;
        if (isObservedShineList(list)) {
            const s32 inferredWorldId = inferCurrentShineListWorld(list->mRootActor);
            const s32 worldId = inferredWorldId >= 0 ? inferredWorldId : sObservedWorldId;
            if (outLayout)
                *outLayout = list->mRootActor;
            if (outWorldId)
                *outWorldId = worldId;
            if (outIndex)
                *outIndex = getListSelectedDataIndex(list);
            if (outList)
                *outList = list;
            return worldId >= 0 && getListSelectedDataIndex(list) >= 0;
        }

        MenuSelectParts* menu = sActiveMoonMenuSelect;
        if (menu && menu->mLayoutActor && menu->mLayoutActor->isAlive()) {
            const s32 worldId = inferCurrentShineListWorld(menu->mLayoutActor);
            if (outLayout)
                *outLayout = menu->mLayoutActor;
            if (outWorldId)
                *outWorldId = worldId;
            if (outIndex)
                *outIndex = menu->mCursorItemIndex;
            return worldId >= 0 && menu->mCursorItemIndex >= 0;
        }

        if (!sLatestActionLayout || sLatestActionWorldId < 0 || sLatestActionIndex < 0)
            return false;

        if (outLayout)
            *outLayout = sLatestActionLayout;
        if (outWorldId)
            *outWorldId = sLatestActionWorldId;
        if (outIndex)
            *outIndex = sLatestActionIndex;
        return true;
    }

    static bool tryGetCurrentCaptureSelection(const al::LayoutActor** outLayout, s32* outIndex,
        const CaptureRando::CaptureEntry** outCapture) {
        if (outLayout)
            *outLayout = nullptr;
        if (outIndex)
            *outIndex = -1;
        if (outCapture)
            *outCapture = nullptr;

        if (!isCaptureListActive())
            return false;

        al::LayoutActor* layout = sActiveDirectHackListLayout;
        const s32 index = getHackListSelectedIndex(layout);
        const CaptureRando::CaptureEntry* capture = getHackListCaptureAt(layout, index);
        if (!capture || index < 0)
            return false;

        if (outLayout)
            *outLayout = layout;
        if (outIndex)
            *outIndex = index;
        if (outCapture)
            *outCapture = capture;
        return true;
    }

    static bool tryGetCurrentAbilitySelection(const al::LayoutActor** outLayout, s32* outIndex,
        const AbilityRando::AbilityEntry** outAbility) {
        if (outLayout)
            *outLayout = nullptr;
        if (outIndex)
            *outIndex = -1;
        if (outAbility)
            *outAbility = nullptr;

        if (!isAbilityListActive())
            return false;

        al::LayoutActor* layout = sActiveDirectSouvenirListLayout;
        const s32 index = getSouvenirListSelectedIndex(layout);
        const AbilityRando::AbilityEntry* ability = getAbilityListEntryAt(index);
        if (!ability || index < 0)
            return false;

        if (outLayout)
            *outLayout = layout;
        if (outIndex)
            *outIndex = index;
        if (outAbility)
            *outAbility = ability;
        return true;
    }

    bool tryHandleListBuyInput() {
        if (sIsPollingConfirmInput)
            return false;
        if (hasPendingConfirmation())
            return true;
        // Report "consumed" during the post-cancel suppression window, for the
        // same reason as tryHandleListHintInput below.
        if (sSuppressNewConfirmationFrames > 0)
            return true;
        const al::LayoutActor* abilityLayout = nullptr;
        s32 abilityIndex = -1;
        if (tryGetCurrentAbilitySelection(&abilityLayout, &abilityIndex, nullptr)) {
            GameDataFile* file = getFile(getSouvenirListAccessor(abilityLayout));
            const ShineDB::ShineEntry* moon = nullptr;
            const SelectionActionState state = getAbilitySelectionActionState(abilityLayout, abilityIndex, nullptr, &moon);
            if (state == SelectionActionState::None)
                return false;
            if (state == SelectionActionState::CannotBuy)
                return beginProgressionMoonBlockedDialog(moon);
            if (state == SelectionActionState::HintOnly)
                return true;

            const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPurchaseCategory());
            if (!HintEconomy::canAfford(file, quote))
                return beginInsufficientFundsDialog("ability buy", quote, HintEconomy::getBalance(file));

            return beginAbilityConfirmation(ConfirmAction::Buy, abilityLayout, abilityIndex);
        }

        const al::LayoutActor* captureLayout = nullptr;
        s32 captureIndex = -1;
        if (tryGetCurrentCaptureSelection(&captureLayout, &captureIndex, nullptr)) {
            GameDataFile* file = getFile(getHackListAccessor(captureLayout));
            const ShineDB::ShineEntry* moon = nullptr;
            const SelectionActionState state = getCaptureSelectionActionState(captureLayout, captureIndex, nullptr, &moon);
            if (state == SelectionActionState::None)
                return false;
            if (state == SelectionActionState::CannotBuy)
                return beginProgressionMoonBlockedDialog(moon);
            if (state == SelectionActionState::HintOnly)
                return true;

            const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPurchaseCategory());
            if (!HintEconomy::canAfford(file, quote))
                return beginInsufficientFundsDialog("capture buy", quote, HintEconomy::getBalance(file));

            return beginCaptureConfirmation(ConfirmAction::Buy, captureLayout, captureIndex);
        }

        const al::LayoutActor* layout = nullptr;
        s32 worldId = -1;
        s32 index = -1;
        CommonVerticalList* list = nullptr;
        if (!tryGetCurrentSelection(&layout, &worldId, &index, &list))
            return false;

        // Unlocked Paintings are information-only, and Moon Rock Keys and
        // Subarea Keys are deliberately hint-only.  Consume Y on those pages so
        // it cannot open a moon-purchase flow for the borrowed world's native rows.
        if (isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
            || isSubareaKeyListWorld(worldId))
            return true;

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));
        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        const SelectionActionState state = getSelectionActionState(file, worldId, index, &hint, &entry);
        if (state == SelectionActionState::None)
            return false;
        if (state == SelectionActionState::CannotBuy)
            return beginProgressionMoonBlockedDialog(entry);
        if (state == SelectionActionState::HintOnly)
            return true;

        const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPurchaseCategory());
        if (!HintEconomy::canAfford(file, quote))
            return beginInsufficientFundsDialog("buy", quote, HintEconomy::getBalance(file));

        (void)list;
        return beginConfirmation(ConfirmAction::Buy, layout, worldId, index);
    }

    bool tryHandleListHintInput() {
        if (sIsPollingConfirmInput)
            return false;
        if (hasPendingConfirmation())
            return true;
        // A disabled whole hint system does not own any collection-list input;
        // leave the native lists untouched in that configuration.
        if (!Randomizer::isHintSystemEnabled())
            return false;
        // Must report "consumed" (true), not "declined" (false), here.
        // shineListDecideHook only calls this because it wants to fall through to
        // vanilla's own native decide/hint handling when we report false (that's
        // the normal path when the hint system doesn't apply, e.g. it's disabled
        // entirely). During the brief post-cancel suppression window we're not
        // declining because the hint system doesn't apply - we're deliberately
        // blocking a request against a selection that's about to go stale. Report
        // false here and vanilla's own decide() would run instead, opening its own
        // native hint dialog on that same stale row - a second, completely
        // separate dialog system none of the guards above this point track at all.
        if (sSuppressNewConfirmationFrames > 0)
            return true;

        const al::LayoutActor* abilityLayout = nullptr;
        s32 abilityIndex = -1;
        if (tryGetCurrentAbilitySelection(&abilityLayout, &abilityIndex, nullptr)) {
            GameDataFile* file = getFile(getSouvenirListAccessor(abilityLayout));

            // Same handover as the moon list: once the location hint is paid
            // for, A stops being the hint button and becomes the route button.
            const PathActionState abilityPath = getAbilityPathActionState(abilityLayout, abilityIndex);
            if (abilityPath == PathActionState::View) {
                if (!takeRouteViewInput())
                    return true;
                if (PathHintOverlay::isVisible() && sRouteViewSource == RouteViewSource::Ability
                    && abilityIndex == sRouteViewIndex)
                    hideRouteHintView();
                else
                    showAbilityRouteHintView(abilityLayout, abilityIndex);
                return true;
            }
            if (abilityPath == PathActionState::Buy) {
                const ShineDB::ShineEntry* location = nullptr;
                getAbilityPathActionState(abilityLayout, abilityIndex, &location);
                static MoonRando::AreaRouteSet sProbeRoutes;
                if (!location || !MoonRando::buildSeedRoutesToLocation(location, &sProbeRoutes))
                    return beginMessageDialog(u"No route through loading zones leads there.");

                const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPathCategory());
                if (!HintEconomy::canAfford(file, quote))
                    return beginInsufficientFundsDialog("route", quote, HintEconomy::getBalance(file));
                return beginAbilityConfirmation(ConfirmAction::Path, abilityLayout, abilityIndex);
            }

            const AbilityRando::AbilityEntry* ability = nullptr;
            const ShineDB::ShineEntry* moon = nullptr;
            const SelectionActionState state = getAbilitySelectionActionState(
                abilityLayout, abilityIndex, &ability, &moon);
            if (state != SelectionActionState::HintAndBuy
                && state != SelectionActionState::HintOnly) {
                // This row still belongs to the custom ability list even when
                // its hint price is Disabled (or its only action is on Y).
                // Own A so no native collection-list action can leak through.
                return ability && moon && !isAbilityUnlockedForList(abilityLayout, *ability);
            }
            const Randomizer::HintPriceQuote quote = getPriceQuote(file, getHintCategoryForAbility());
            if (!HintEconomy::canAfford(file, quote))
                return beginInsufficientFundsDialog("ability hint", quote, HintEconomy::getBalance(file));

            return beginAbilityConfirmation(ConfirmAction::Hint, abilityLayout, abilityIndex);
        }

        const al::LayoutActor* captureLayout = nullptr;
        s32 captureIndex = -1;
        if (tryGetCurrentCaptureSelection(&captureLayout, &captureIndex, nullptr)) {
            GameDataFile* file = getFile(getHackListAccessor(captureLayout));

            const PathActionState capturePath = getCapturePathActionState(captureLayout, captureIndex);
            if (capturePath == PathActionState::View) {
                if (!takeRouteViewInput())
                    return true;
                if (PathHintOverlay::isVisible() && sRouteViewSource == RouteViewSource::Capture
                    && captureIndex == sRouteViewIndex)
                    hideRouteHintView();
                else
                    showCaptureRouteHintView(captureLayout, captureIndex);
                return true;
            }
            if (capturePath == PathActionState::Buy) {
                const ShineDB::ShineEntry* location = nullptr;
                getCapturePathActionState(captureLayout, captureIndex, &location);
                static MoonRando::AreaRouteSet sProbeRoutes;
                if (!location || !MoonRando::buildSeedRoutesToLocation(location, &sProbeRoutes))
                    return beginMessageDialog(u"No route through loading zones leads there.");

                const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPathCategory());
                if (!HintEconomy::canAfford(file, quote))
                    return beginInsufficientFundsDialog("route", quote, HintEconomy::getBalance(file));
                return beginCaptureConfirmation(ConfirmAction::Path, captureLayout, captureIndex);
            }

            const CaptureRando::CaptureEntry* capture = nullptr;
            const ShineDB::ShineEntry* moon = nullptr;
            const SelectionActionState state = getCaptureSelectionActionState(
                captureLayout, captureIndex, &capture, &moon);
            if (state != SelectionActionState::HintAndBuy
                && state != SelectionActionState::HintOnly) {
                // Capture rows have no native A action to delegate to. Keep the
                // ownership rule parallel to moons and abilities for every
                // valid, still-locked custom row.
                return capture && moon && !isCaptureUnlockedForList(captureLayout, *capture);
            }
            const Randomizer::HintPriceQuote quote = getPriceQuote(file, getHintCategoryForCapture());
            if (!HintEconomy::canAfford(file, quote))
                return beginInsufficientFundsDialog("capture hint", quote, HintEconomy::getBalance(file));

            return beginCaptureConfirmation(ConfirmAction::Hint, captureLayout, captureIndex);
        }

        const al::LayoutActor* layout = nullptr;
        s32 worldId = -1;
        s32 index = -1;
        CommonVerticalList* list = nullptr;
        if (!tryGetCurrentSelection(&layout, &worldId, &index, &list))
            return false;
        if (!canUseHintSystem())
            return false;

        GameDataFile* file = getFile(GameDataHolderAccessor(layout));

        // Extended Hints takes the A button over once the location hint has been
        // paid for; before that getPathActionState reports None and the ordinary
        // hint flow below runs untouched.
        const PathActionState path = getPathActionState(file, worldId, index);
        if (path == PathActionState::View) {
            if (!takeRouteViewInput())
                return true;
            if (PathHintOverlay::isVisible() && sRouteViewSource == RouteViewSource::Moon
                && worldId == sRouteViewWorldId && index == sRouteViewIndex)
                hideRouteHintView();
            else
                showRouteHintView(layout, worldId, index);
            return true;
        }
        if (path == PathActionState::Buy) {
            // Say so before the confirm dialog rather than after the coins are
            // gone: a handful of stages sit outside the overworld with no
            // loading-zone route into them at all.
            const ShineDB::ShineEntry* location = nullptr;
            getPathActionState(file, worldId, index, &location);
            static MoonRando::AreaRouteSet sProbeRoutes;
            if (!location || !MoonRando::buildSeedRoutesToLocation(location, &sProbeRoutes))
                return beginMessageDialog(u"No route through loading zones leads there.");

            const Randomizer::HintPriceQuote quote = getPriceQuote(file, getPathCategory());
            if (!HintEconomy::canAfford(file, quote))
                return beginInsufficientFundsDialog("route", quote, HintEconomy::getBalance(file));
            return beginConfirmation(ConfirmAction::Path, layout, worldId, index);
        }

        GameDataFile::HintInfo* hint = nullptr;
        const ShineDB::ShineEntry* entry = nullptr;
        const SelectionActionState state = getSelectionActionState(file, worldId, index, &hint, &entry);
        if (state != SelectionActionState::HintAndBuy
            && state != SelectionActionState::HintOnly) {
            // Once a row resolves to one of our unacquired shuffled entries,
            // the direct ShineList's A button is ours even when its hint price
            // is Disabled. Returning false here delegates to vanilla decide(),
            // which can open the native coin-purchase path for the custom row.
            // Non-applicable, acquired, and stale rows still return false.
            return isActionableUnacquiredSelection(file, worldId, index, nullptr, nullptr);
        }
        const Randomizer::HintPriceQuote quote = getPriceQuote(file, getHintCategoryForShineEntry(entry));
        if (!HintEconomy::canAfford(file, quote))
            return beginInsufficientFundsDialog("hint", quote, HintEconomy::getBalance(file));

        (void)list;
        return beginConfirmation(ConfirmAction::Hint, layout, worldId, index);
    }

    bool tryHandleListCancelInput() {
        if (sIsPollingConfirmInput || hasPendingConfirmation())
            return false;
        // Only the panel this system opened, from the list it belongs to. The
        // "Kingdom Checks" menu shares the overlay but runs its own input, and
        // must keep its own B press.
        if (sRouteViewSource == RouteViewSource::None || !PathHintOverlay::isVisible())
            return false;
        const bool isOwningListActive = sRouteViewSource == RouteViewSource::Capture
            ? isCaptureListActive()
            : sRouteViewSource == RouteViewSource::Ability ? isAbilityListActive()
                                                           : isMoonListActive();
        if (!isOwningListActive)
            return false;
        hideRouteHintView();
        return true;
    }

    static void pollHackListActionInput() {
        if (!isCaptureListActive() || sIsPollingConfirmInput || hasPendingConfirmation())
            return;

        if (al::isPadTriggerY(-1)) {
            tryHandleListBuyInput();
            return;
        }

        if (al::isPadTriggerA(-1))
            tryHandleListHintInput();
    }

    static void pollSouvenirListActionInput() {
        if (!isAbilityListActive() || sIsPollingConfirmInput || hasPendingConfirmation())
            return;

        if (al::isPadTriggerY(-1)) {
            tryHandleListBuyInput();
            return;
        }

        if (al::isPadTriggerA(-1))
            tryHandleListHintInput();
    }

    bool isDirectShineListViewActive() {
        return sIsDirectShineListView;
    }

    bool isCustomShineListInfoPage(s32 worldId) {
        return isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
            || isSubareaKeyListWorld(worldId);
    }

    bool isLikelyShineListLayout(const al::LayoutActor* layout) {
        return layout && isLikelyShineListName(layout->getName());
    }

    static HkTrampoline<void, al::LayoutActor*> shineListControlHook = hk::hook::trampoline([](al::LayoutActor* layout) -> void {
        shineListControlHook.orig(layout);
        observeShineListLayout(layout);
        updateConfirmationDialog();
        updatePendingShineListRefresh(layout);
    });

    static HkTrampoline<bool, al::LayoutActor*> shineListDecideHook = hk::hook::trampoline([](al::LayoutActor* layout) -> bool {
        observeShineListLayout(layout);
        if (tryHandleListHintInput())
            return true;
        // tryHandleListHintInput can decline for reasons that have nothing to
        // do with "vanilla should handle this instead" - e.g. a decide input
        // thrown after we've already backed out to the kingdom map, where
        // sIsDirectShineListView is correctly false and tryGetCurrentSelection
        // correctly finds nothing. The suppression window above only covers a
        // short window right after a close; a decide arriving after it has
        // expired (a slower but still close-then-hint press) hits this same
        // fallthrough. Letting vanilla's own decide()/hint handling run on a
        // list our own state no longer considers active reliably crashes
        // (NerveKeeper::update() on an unrelated layout actor). Only let the
        // fallthrough happen while the hint system genuinely isn't managing
        // this view at all (e.g. disabled outright) - never on a stale one.
        if (Randomizer::isHintSystemEnabled() && !sIsDirectShineListView)
            return true;
        return shineListDecideHook.orig(layout);
    });

    // ShineListLayout::cancel() cannot be hooked with a trampoline: it's only
    // 16 bytes (ADRP/ADD/ADD/B - a tail call into al::setNerve with a
    // PC-relative literal load), and this codebase's trampoline mechanism
    // only backs up and relocates the function's single first instruction to
    // let orig() replay it (see the "TODO: Relocate instruction, or at least
    // abort if instruction needs to be relocated" note in
    // hk/hook/Trampoline.h). Replaying a PC-relative ADRP from the backup
    // pool's address computes a garbage pointer, which orig() then passes to
    // al::setNerve() as this list's new current nerve - deterministically
    // (the pool address is fixed), corrupting the SAME way every single
    // close, dialog or no dialog. That was the cause of a
    // NerveKeeper::update() crash reproducible on every close - hooking this
    // function was tried and reverted; the suppression window is armed from
    // setDirectShineListViewActive(false) instead (below), which is a frame
    // later but doesn't touch this function at all.

    // Switched to origin/main's fix for a real, previously-known crash here
    // (this fork used to run a narrower isOpenShineNameBoundsGuardHook
    // instead, with the richer hook below left disabled after it reproduced
    // a StageScene::init boot crash requiring RCM recovery). Dev's version
    // below is a comprehensive rewrite that specifically covers the
    // multi-page (Subarea Key/Moon Rock Key/Painting) redistribution case
    // this fork's earlier crash came from - NEEDS REAL ON-HARDWARE BOOT
    // TESTING before trusting this over the guard hook it replaced, given
    // that exact history.
    //
    // orig() here is an UNBOUNDED scan. GameDataFile::isOpenShineName (0x523ec0)
    // walks the save's shine array counting the entries tagged with worldId and
    // stops only once it has counted `index` of them - there is no array-length
    // check. GameDataFile::findShine and ::isGotShine share that shape, but both
    // are hooked at the GameDataFile level, so they are answered from our own
    // roster wherever they are called from; this one is hooked at the
    // GameDataFunction wrapper, so anything it declines to answer reaches the
    // raw scan. Handed a row index the native data cannot reach, that scan runs
    // off the end of the array and keeps walking foreign memory until a garbage
    // entry happens to match and sends it down that entry's Moon Rock branch.
    // That is the reported scene-init crash: a fault at
    // WorldList::getMoonRockScenarioNo with the scan already 2,962,421 entries
    // (0x644b4798 / 0x238) past the array base, while building a 64-row
    // redistributed Lost Kingdom page whose native world-6 data is far shorter.
    //
    // So every page whose row count getWorldTotalShineNumHook overrides has to
    // be answered here too, over the same isShineListPageContextActive window
    // that hook uses - otherwise the page gets sized from our roster while this
    // query is still resolved against vanilla's. Keying these branches off the
    // direct-view flag alone left exactly the constructor-time and
    // collection-state-transition updateWorldInfo passes falling through.
    static HkTrampoline<bool, const al::LayoutActor*, s32, s32> isOpenShineNameHook = hk::hook::trampoline([](const al::LayoutActor* layout, s32 worldId, s32 index) -> bool {
        // Moon Rock Key rows are always "name open": the native page update
        // then routes every row through tryFindShineMessage (hooked in
        // MoonHooks.cpp), which renders at least the kingdom label instead of
        // the native "??????" closed-name row. Whether the LOCATION shows is
        // decided inside that row text, not by this reveal gate.
        if (isShineListPageContextActive(worldId)
            && (isMoonRockKeyListWorld(worldId) || isSubareaKeyListWorld(worldId)))
            return true;
        // The randomized kingdom pages and the Toadette Medals page list their
        // own rosters, so a row's reveal state comes from that row's own hint
        // record. Neither may be left to the isHintSystemEnabled branch below:
        // the row counts they are sized with do not depend on the hint system
        // being on, so this answer must not either.
        if (shouldExposeFullRandomizedMoonList(worldId)
            || (isShineListPageContextActive(worldId) && isToadetteMedalListWorld(worldId)))
            return shouldRevealShineListName(layout, worldId, index);
        if (Randomizer::isHintSystemEnabled() && isDirectShineListViewActive())
            return shouldRevealShineListName(layout, worldId, index);
        if (isOpenShineNameHook.orig(layout, worldId, index))
            return true;
        return shouldRevealShineListName(layout, worldId, index);
    });

    static HkTrampoline<void, al::LayoutActor*> hackListUpdatePartsDataHook = hk::hook::trampoline([](al::LayoutActor* layout) -> void {
        hackListUpdatePartsDataHook.orig(layout);
        if (sIsDirectHackListView) {
            refreshHackListData(layout);
        }
    });

    static HkTrampoline<void, al::LayoutActor*> hackListControlHook = hk::hook::trampoline([](al::LayoutActor* layout) -> void {
        if (sIsDirectHackListView) {
            setDirectHackListViewActive(true, layout);
            refreshHackListData(layout);
        }
        hackListControlHook.orig(layout);
        if (!sIsDirectHackListView)
            return;

        setDirectHackListViewActive(true, layout);
        refreshHackListData(layout);
        observeHackListLayout(layout);
        pollHackListActionInput();
    });

    static HkTrampoline<void, al::LayoutActor*> souvenirListUpdatePartsDataHook = hk::hook::trampoline([](al::LayoutActor* layout) -> void {
        souvenirListUpdatePartsDataHook.orig(layout);
        if (sIsDirectSouvenirListView)
            refreshSouvenirListData(layout);
    });

    static HkTrampoline<void, al::LayoutActor*> souvenirListControlHook = hk::hook::trampoline([](al::LayoutActor* layout) -> void {
        if (sIsDirectSouvenirListView) {
            setDirectSouvenirListViewActive(true, layout);
            refreshSouvenirListData(layout);
        }
        souvenirListControlHook.orig(layout);
        if (!sIsDirectSouvenirListView)
            return;

        setDirectSouvenirListViewActive(true, layout);
        refreshSouvenirListData(layout);
        observeSouvenirListLayout(layout);
        pollSouvenirListActionInput();
        updateConfirmationDialog();
    });

    static HkTrampoline<void, void*> collectionListViewStageMapHook = hk::hook::trampoline([](void* state) -> void {
        collectionListViewStageMapHook.orig(state);
        setDirectShineListViewActive(false);
        setDirectHackListViewActive(false);
        setDirectSouvenirListViewActive(false);
    });

    static HkTrampoline<void, void*> collectionListViewShineListHook = hk::hook::trampoline([](void* state) -> void {
        setDirectHackListViewActive(false);
        setDirectSouvenirListViewActive(false);
        if (!sIsDirectShineListView) {
            // Force exeAppear's first tick (about to run inside orig() below)
            // to recompute this kingdom's row data the same way paging into
            // it does, instead of showing whatever was last computed for it
            // (see cShineListForceWorldInfoUpdateFlagOffset).
            al::LayoutActor* layout = getShineListLayoutFromCollectionState(state);
            if (layout)
                *(reinterpret_cast<u8*>(layout) + cShineListForceWorldInfoUpdateFlagOffset) = 1;
        }
        setDirectShineListViewActive(true);
        collectionListViewShineListHook.orig(state);
    });

    static HkTrampoline<void, void*> collectionListViewHackListHook = hk::hook::trampoline([](void* state) -> void {
        setDirectShineListViewActive(false);
        setDirectSouvenirListViewActive(false);
        al::LayoutActor* layout = getHackListLayoutFromCollectionState(state);
        setDirectHackListViewActive(true, layout);
        refreshHackListData(layout);
        collectionListViewHackListHook.orig(state);
        if (!layout)
            layout = getHackListLayoutFromCollectionState(state);
        setDirectHackListViewActive(true, layout);
        refreshHackListData(layout);
        observeHackListLayout(layout);
        pollHackListActionInput();
        updateConfirmationDialog();
    });

    static HkTrampoline<void, void*> collectionListViewSouvenirListHook = hk::hook::trampoline([](void* state) -> void {
        setDirectShineListViewActive(false);
        setDirectHackListViewActive(false);
        al::LayoutActor* layout = getSouvenirListLayoutFromCollectionState(state);
        setDirectSouvenirListViewActive(true, layout);
        refreshSouvenirListData(layout);
        collectionListViewSouvenirListHook.orig(state);
        if (!layout)
            layout = getSouvenirListLayoutFromCollectionState(state);
        setDirectSouvenirListViewActive(true, layout);
        refreshSouvenirListData(layout);
        observeSouvenirListLayout(layout);
        pollSouvenirListActionInput();
        updateConfirmationDialog();
    });

    static HkTrampoline<void, void*> collectionListViewBgmListHook = hk::hook::trampoline([](void* state) -> void {
        setDirectShineListViewActive(false);
        setDirectHackListViewActive(false);
        setDirectSouvenirListViewActive(false);
        collectionListViewBgmListHook.orig(state);
    });

    static HkTrampoline<void, StageScene*> stageSceneExeCollectionListHook = hk::hook::trampoline([](StageScene* scene) -> void {
        stageSceneExeCollectionListHook.orig(scene);
        updatePendingCollectionListClose(scene);
        updatePendingShineCountAnim(scene);
    });

    static void updateToadetteMedalHuntIntro(StageScene* scene);
    static void updatePendingToadetteMedalMessage(StageScene* scene);

    static HkTrampoline<void, StageScene*> stageSceneControlHook = hk::hook::trampoline([](StageScene* scene) -> void {
        stageSceneControlHook.orig(scene);
        EnemySwapHooks::update(scene);
        if (sSuppressNewConfirmationFrames > 0)
            sSuppressNewConfirmationFrames--;
        if (sRouteViewInputCooldown > 0)
            sRouteViewInputCooldown--;
        updateRouteHintView();
        updateToadetteMedalHuntIntro(scene);
        updatePendingToadetteMedalMessage(scene);
        if (sPendingShineCountAnimFrames <= 0 || !scene)
            return;
        if (al::isNerve(scene, mainOffsetNerve(cStageSceneNrvCollectionListOffset)))
            return;
        updatePendingShineCountAnim(scene);
    });

    // Firing native stage-change/UI-trigger calls immediately from
    // Shine::onSwitchGet crashed (null player actor in
    // al::LiveActor::getSceneInfo(), called from rs::isActiveDemo() during
    // StageScene::exeStartStageBgm()) - the medal's own "got moon" demo is
    // still starting up at that point. This mirrors a guard this codebase
    // already established for exactly this hazard: tryChangeNextStageHook
    // (above) rejects stage-change requests while
    // isBoughtMoonDemoSceneActive(), because changing stage mid-demo corrupts
    // player/scene state. That guard only covers the shop-bought-moon demo
    // path though, so the medal-progress message is deferred here until any
    // moon-get demo nerve (bought or normally collected) has cleared, polled
    // from the already-existing stageSceneControlHook.
    static bool isAnyMoonGetDemoActive(StageScene* scene) {
        return scene
            && (al::isNerve(scene, mainOffsetNerve(cStageSceneNrvDemoShineGetOffset))
                || al::isNerve(scene, mainOffsetNerve(cStageSceneNrvDemoShineGrandGetOffset)));
    }

    // Unconditional grace period before polling the demo nerve: onSwitchGet fires
    // right as the get-demo is starting, before the scene's nerve has actually
    // transitioned into it, so checking the nerve immediately would see "inactive"
    // and fire too early.
    static constexpr s32 cPendingToadetteMedalDeferMinFrames = 30;

    // Cappy Medal Hunt messages use the same rs::tryShowCapMessage/CapMessage
    // plumbing as chapel-blocked messages and Moon Rock Key messages. The text
    // is resolved by tryGetSystemMessageOverride and the label-existence hook
    // in MoonHooks.cpp.
    constexpr const char* cToadetteMedalCollectedMessageLabel = "ToadetteMedalCollected";
    constexpr const char* cToadetteMedalHuntIntroMessageLabel = "ToadetteMedalHuntIntro";
    constexpr const char* cToadetteMedalTombstoneBlockedMessageLabel = "ToadetteMedalTombstoneBlocked";
    constexpr const char* cToadetteMedalHuntIntroShownFlag = "SMORandoToadetteMedalHuntIntroShown";
    static constexpr s32 cToadetteMedalHuntIntroDelayFrames = 90;
    static bool sPendingToadetteMedalMessage = false;
    static s32 sPendingToadetteMedalMessageMinFrames = 0;
    static s32 sPendingToadetteMedalMessageRemaining = 0;
    static s32 sLastToadetteMedalsRemainingForMessage = 0;
    static s32 sToadetteMedalHuntIntroDelay = 0;

    bool isToadetteMedalHuntMessage(const char* fileName, const char* key) {
        if (!fileName || !key || std::strcmp(fileName, "CapMessage") != 0)
            return false;

        return std::strcmp(key, cToadetteMedalCollectedMessageLabel) == 0
            || std::strcmp(key, cToadetteMedalHuntIntroMessageLabel) == 0
            || std::strcmp(key, cToadetteMedalTombstoneBlockedMessageLabel) == 0;
    }

    bool showToadetteMedalTombstoneNotReadyMessage(const al::IUseSceneObjHolder* holder) {
        if (!holder || Randomizer::getBowserWinCondition() != Randomizer::BowserWinCondition_ToadetteMedals)
            return false;
        return rs::tryShowCapMessage(holder, cToadetteMedalTombstoneBlockedMessageLabel, 90, 0);
    }

    static void updateToadetteMedalHuntIntro(StageScene* scene) {
        if (!scene || Randomizer::getBowserWinCondition() != Randomizer::BowserWinCondition_ToadetteMedals) {
            sToadetteMedalHuntIntroDelay = 0;
            return;
        }

        GameDataHolderWriter writer(scene);
        GameDataFile* file = getFile(writer);
        if (!file || file->customIsExistInHackDictionary(cToadetteMedalHuntIntroShownFlag)) {
            sToadetteMedalHuntIntroDelay = 0;
            return;
        }

        if (sToadetteMedalHuntIntroDelay < cToadetteMedalHuntIntroDelayFrames) {
            sToadetteMedalHuntIntroDelay++;
            return;
        }

        if (rs::tryShowCapMessage(scene, cToadetteMedalHuntIntroMessageLabel, 120, 0)) {
            file->customAddHackDictionary(cToadetteMedalHuntIntroShownFlag);
            GameDataFunction::setRequireSave(writer);
            sToadetteMedalHuntIntroDelay = 0;
        }
    }

    // medalsBefore is the aggregate medal-collected count taken right before
    // Shine::onSwitchGet's real native processing ran (captured by the caller)
    // - identifying "was THIS pickup a medal" via shine->curShineInfo was
    // unreliable (no message ever fired in testing), likely because Shine
    // actors are pooled/reused and onSwitchGet's own native cleanup may reset
    // curShineInfo before this runs. Comparing the aggregate count before/after
    // instead avoids relying on a pooled Shine actor's transient info.
    static void checkToadetteMedalCollectedMessage(Shine* shine, s32 medalsBefore) {
        // Diagnostic: no message ever appeared in testing (even after the
        // curShineInfo -> before/after-count rewrite), so log every call,
        // including early returns, to see exactly which gate is blocking it.
        const bool isMedalsOnly = Randomizer::getBowserWinCondition()
            == Randomizer::BowserWinCondition_ToadetteMedals;
        if (cLogToadetteMedalDiagnostics) {
            Randomizer::logPush(
                "hint system checkToadetteMedalCollectedMessage shine=%p medalsOnly=%d pending=%d medalsBefore=%d",
                shine, isMedalsOnly, sPendingToadetteMedalMessage, medalsBefore);
        }

        if (!shine || !isMedalsOnly)
            return;
        if (sPendingToadetteMedalMessage || medalsBefore < 0)
            return;

        GameDataHolderWriter writer(static_cast<const al::IUseSceneObjHolder*>(shine));
        GameDataFile* file = getFile(writer);
        if (!file)
            return;

        const s32 collected = MoonRando::getToadetteMedalCollectedCount(file);
        if (cLogToadetteMedalDiagnostics) {
            Randomizer::logPush(
                "hint system checkToadetteMedalCollectedMessage collected=%d medalsBefore=%d",
                collected, medalsBefore);
        }
        if (collected <= medalsBefore)
            return;

        const s32 required = Randomizer::getToadetteMedalRatio().required;
        s32 remaining = required - collected;
        if (remaining < 0)
            remaining = 0;

        sPendingToadetteMedalMessageRemaining = remaining;
        sPendingToadetteMedalMessage = true;
        sPendingToadetteMedalMessageMinFrames = cPendingToadetteMedalDeferMinFrames;
        if (cLogToadetteMedalDiagnostics) {
            Randomizer::logPush(
                "hint system Toadette Medal collected - deferring progress message remaining=%d",
                remaining);
        }
    }

    static void updatePendingToadetteMedalMessage(StageScene* scene) {
        if (!sPendingToadetteMedalMessage)
            return;

        if (sPendingToadetteMedalMessageMinFrames > 0) {
            sPendingToadetteMedalMessageMinFrames--;
            return;
        }

        if (isAnyMoonGetDemoActive(scene))
            return;

        sPendingToadetteMedalMessage = false;
        if (!scene)
            return;

        sLastToadetteMedalsRemainingForMessage = sPendingToadetteMedalMessageRemaining;
        const bool shown = rs::tryShowCapMessage(scene, cToadetteMedalCollectedMessageLabel, 90, 0);
        if (cLogToadetteMedalDiagnostics) {
            Randomizer::logPush(
                "hint system Toadette Medal progress message tryShowCapMessage result=%d remaining=%d",
                shown, sLastToadetteMedalsRemainingForMessage);
        }
    }

    const char16_t* tryMakeToadetteMedalHuntMessage(const char* fileName, const char* key) {
        if (!isToadetteMedalHuntMessage(fileName, key))
            return nullptr;

        if (std::strcmp(key, cToadetteMedalHuntIntroMessageLabel) == 0) {
            static sead::WFixedSafeString<0x100> sIntroMessage;
            sIntroMessage.format(u"We need to collect %d Toadette Medals and\npay our respects to fight Bowser",
                Randomizer::getToadetteMedalRatio().required);
            return sIntroMessage.cstr();
        }

        if (std::strcmp(key, cToadetteMedalTombstoneBlockedMessageLabel) == 0)
            return u"We don't have enough Toadette Medals to\npay our respects and fight Bowser";

        if (sLastToadetteMedalsRemainingForMessage <= 0)
            return u"We should pay our respects.";

        static sead::WFixedSafeString<0x100> sMessage;
        if (sLastToadetteMedalsRemainingForMessage == 1)
            sMessage = u"1 Toadette Medal to go!";
        else
            sMessage.format(u"%d Toadette Medals to go!", sLastToadetteMedalsRemainingForMessage);
        return sMessage.cstr();
    }

    static void handleToadetteMedalPickup(Shine* shine, s32 medalsBefore) {
        checkToadetteMedalCollectedMessage(shine, medalsBefore);
    }

    static s32 getToadetteMedalCollectedCountForShine(Shine* shine) {
        GameDataFile* file = getFile(GameDataHolderWriter(static_cast<const al::IUseSceneObjHolder*>(shine)));
        return file ? MoonRando::getToadetteMedalCollectedCount(file) : -1;
    }

    static HkTrampoline<void, Shine*> shineOnSwitchGetHook = hk::hook::trampoline([](Shine* shine) -> void {
        if (!shine) {
            shineOnSwitchGetHook.orig(shine);
            return;
        }

        const s32 medalsBefore = getToadetteMedalCollectedCountForShine(shine);

        ChangeStageInfo* originalInfo = shine->mChangeStageInfo;
        if (originalInfo && isChangeStageInfoStorageSane(originalInfo)) {
            shineOnSwitchGetHook.orig(shine);
            handleToadetteMedalPickup(shine, medalsBefore);
            return;
        }

        GameDataHolderAccessor accessor(static_cast<const al::IUseSceneObjHolder*>(shine));
        ChangeStageInfo emptyInfo(accessor.mData, "", "", false, -1,
            ChangeStageInfo::NO_SUB_SCENARIO);
        Randomizer::logPush(
            "hint system patched unsafe Shine change-stage info shine=%p info=%p",
            shine, originalInfo);
        shine->mChangeStageInfo = &emptyInfo;
        shineOnSwitchGetHook.orig(shine);
        shine->mChangeStageInfo = originalInfo;
        handleToadetteMedalPickup(shine, medalsBefore);
    });

    static HkTrampoline<void, StageScene*> stageSceneExeDemoShineGetHook = hk::hook::trampoline([](StageScene* scene) -> void {
        stageSceneExeDemoShineGetHook.orig(scene);
        if (sBoughtMoonDemoShine && !isBoughtMoonDemoSceneActive(scene)) {
            clearBoughtMoonDemoState(true);
            Randomizer::logPush("hint system cleared bought shine demo guard");
        }
    });

    static HkTrampoline<void, void*> stageSceneStateGetShineExeDemoGetFirstHook = hk::hook::trampoline([](void* state) -> void {
        stageSceneStateGetShineExeDemoGetFirstHook.orig(state);
        if (!isBoughtPowerStarDemoActive())
            return;
        if (!isBoughtMoonDemoSceneActive())
            return;

        sBoughtPowerStarDemoGetFirstFrames++;
        if (sBoughtPowerStarDemoGetFirstFrames < cBoughtPowerStarDemoGetFirstFrameMax)
            return;

        sBoughtPowerStarDemoGetFirstFrames = 0;
        al::setNerve(reinterpret_cast<al::IUseNerve*>(state),
            mainOffsetNerve(cStageSceneStateGetShineNrvDemoGetOffset));
        Randomizer::logPush(
            "hint system advanced bought Power Star demo past GetFirst timeout");
    });

    static HkTrampoline<void, al::ActionPadAndCameraCtrl*, const al::ActionPadAndCameraCtrlInfo*>
        actionPadAndCameraStartPadRumbleHook = hk::hook::trampoline([](al::ActionPadAndCameraCtrl* ctrl,
                                                                        const al::ActionPadAndCameraCtrlInfo* info) -> void {
            if (isBoughtPowerStarDemoActive() && isBoughtMoonDemoSceneActive()) {
                if (!sSkippedBoughtPowerStarDemoRumble) {
                    sSkippedBoughtPowerStarDemoRumble = true;
                    Randomizer::logPush(
                        "hint system skipped bought Power Star demo pad rumble");
                }
                return;
            }
            actionPadAndCameraStartPadRumbleHook.orig(ctrl, info);
        });

    // GameDataFunction::tryChangeNextStage starts with a PC-relative `ADRP X9, &cNullChar`.
    // hk's TrampolineHook copies the first instruction verbatim without relocating it (see
    // Trampoline.h), so calling .orig() would execute that ADRP from the trampoline's address
    // and compute a bogus &cNullChar pointer -> crash inside the original function. Replace the
    // function outright instead and perform the stage change through GameDataHolder::changeNextStage
    // directly (hooked elsewhere, safe to call), reproducing the original's trivial body:
    //   if (info->mChangeStageName is empty) return false; else { holder->changeNextStage(info, 0); return true; }
    static HkReplace<bool, GameDataHolderWriter, const ChangeStageInfo*> tryChangeNextStageHook = hk::hook::replace([](GameDataHolderWriter writer, const ChangeStageInfo* info) -> bool {
        if (sBoughtMoonDemoShine && isBoughtMoonDemoSceneActive()) {
            Randomizer::logPush("hint system skipped bought shine demo change-stage request");
            return false;
        }

        if (!info) {
            Randomizer::logPush("hint system skipped null change-stage request");
            return false;
        }

        if (!isChangeStageInfoStorageSane(info)) {
            Randomizer::logPush("hint system skipped corrupt change-stage request info=%p", info);
            return false;
        }

        GameDataHolder* holder = writer.mData;
        if (!holder || info->isEmpty())
            return false;

        holder->changeNextStage(info, 0);
        return true;
    });

    static HkTrampoline<void, al::LiveActor*> liveActorMakeActorAliveHook = hk::hook::trampoline([](al::LiveActor* actor) -> void {
        if (!isBoughtMoonDemoActor(actor)) {
            liveActorMakeActorAliveHook.orig(actor);
            return;
        }

        al::ShadowKeeper* shadowKeeper = actor->mShadowKeeper;
        actor->mShadowKeeper = nullptr;
        liveActorMakeActorAliveHook.orig(actor);
        actor->mShadowKeeper = shadowKeeper;
        Randomizer::logPush("hint system skipped demo shine shadow during makeActorAlive actor=%p",
            actor);
    });

    static HkTrampoline<void, al::LiveActor*> liveActorMovementHook = hk::hook::trampoline([](al::LiveActor* actor) -> void {
        if (!isBoughtMoonDemoActor(actor)) {
            liveActorMovementHook.orig(actor);
            return;
        }

        al::ShadowKeeper* shadowKeeper = actor->mShadowKeeper;
        actor->mShadowKeeper = nullptr;
        liveActorMovementHook.orig(actor);
        actor->mShadowKeeper = shadowKeeper;
    });

    static HkTrampoline<void, al::LiveActor*> liveActorMakeActorDeadHook = hk::hook::trampoline([](al::LiveActor* actor) -> void {
        if (!isBoughtMoonDemoActor(actor)) {
            liveActorMakeActorDeadHook.orig(actor);
            return;
        }

        al::ShadowKeeper* shadowKeeper = actor->mShadowKeeper;
        actor->mShadowKeeper = nullptr;
        liveActorMakeActorDeadHook.orig(actor);
        actor->mShadowKeeper = shadowKeeper;
        Randomizer::logPush("hint system skipped demo shine shadow during makeActorDead actor=%p",
            actor);
    });

    static HkTrampoline<bool, FooterParts*, const char16_t*> footerTryChangeTextFadeHook = hk::hook::trampoline([](FooterParts* footer, const char16_t* text) -> bool {
        sLatestFooter = footer;
        if (const char16_t* prompt = tryMakeFooterPrompt())
            return footerTryChangeTextFadeHook.orig(footer, prompt);
        return footerTryChangeTextFadeHook.orig(footer, text);
    });

    static void applyShineListFooterText(const al::LayoutActor* layout) {
        al::LayoutActor* footer = getShineListFooterLayout(layout);
        if (!footer)
            return;

        const char16_t* prompt = tryMakeFooterPrompt();
        if (!prompt)
            prompt = al::getSystemMessageString(layout, "Footer", "CollectList_ListGuide");
        if (prompt)
            al::setPaneString(footer, "TxtGuide", prompt, 0);
    }

    static void applyHackListFooterText(const al::LayoutActor* layout) {
        al::LayoutActor* footer = getHackListFooterLayout(layout);
        if (!footer)
            return;

        const char16_t* prompt = tryMakeFooterPrompt();
        if (!prompt)
            prompt = al::getSystemMessageString(layout, "Footer", "CollectList_ListGuide");
        if (prompt)
            al::setPaneString(footer, "TxtGuide", prompt, 0);
    }

    static void applySouvenirListFooterText(const al::LayoutActor* layout) {
        al::LayoutActor* footer = getSouvenirListFooterLayout(layout);
        if (!footer)
            return;

        const char16_t* prompt = tryMakeFooterPrompt();
        if (!prompt)
            prompt = al::getSystemMessageString(layout, "Footer", "CollectList_ListGuide");
        if (prompt)
            al::setPaneString(footer, "TxtGuide", prompt, 0);
    }

    static void refreshFooterPrompt() {
        if (sIsDirectShineListView && sActiveDirectShineListLayout)
            applyShineListFooterText(sActiveDirectShineListLayout);
        if (sIsDirectHackListView && sActiveDirectHackListLayout)
            applyHackListFooterText(sActiveDirectHackListLayout);
        if (sIsDirectSouvenirListView && sActiveDirectSouvenirListLayout)
            applySouvenirListFooterText(sActiveDirectSouvenirListLayout);
        if (!sLatestFooter)
            return;
        if (const char16_t* prompt = tryMakeFooterPrompt())
            footerTryChangeTextFadeHook.orig(sLatestFooter, prompt);
    }

    static HkTrampoline<bool, s32> isPadTriggerLHook = hk::hook::trampoline([](s32 port) -> bool {
        if (hasPendingConfirmation())
            return false;
        return isPadTriggerLHook.orig(port);
    });

    static HkTrampoline<bool, s32> isPadTriggerRHook = hk::hook::trampoline([](s32 port) -> bool {
        if (hasPendingConfirmation())
            return false;
        return isPadTriggerRHook.orig(port);
    });

    // Vanilla page count for the ShineList's per-world tabs. The mod always shows
    // all 14 real kingdoms (Cap..Moon) regardless of story progress, then appends
    // only the enabled special-information pages. getWorldIdForShineList is
    // remapped alongside the count so those pages remain contiguous even when
    // Toadette Medals is off but Paintings or Moon Rock Keys is on.
    // Diagnostic: the ShineList title still shows "Mushroom Kingdom" for the
    // Toadette Medals page despite the worldId/layout-name gate in
    // ProgressionHooks.cpp's tryGetWorldNameForWorldMapHook looking correct -
    // and that hook's own diagnostic log never fires at all with worldId==
    // Mushroom. Root cause found via ida_context/Layout/ShineListLayout.md's
    // constructor: it calls THIS function once, clamps the result, stores it,
    // and allocates its per-world title-name cache array to EXACTLY that size
    // (ShineListLayout.md ctor, "operator new[](8 * v82)" sized off the same
    // stored count) - if this returns 14 (not 15) at THAT ONE construction
    // moment, the cache physically has no 15th slot and can never show a
    // Toadette Medals title, regardless of what this hook returns on later
    // calls. Logging every call (capped) to see whether/when it's ever asked
    // for 15, and what isToadetteMedalModeEnabled() reports each time.
    static HkTrampoline<s32, const al::LayoutActor*, s32> getWorldIdForShineListHook = hk::hook::trampoline(
        [](const al::LayoutActor* layout, s32 pageIndex) -> s32 {
            const s32 specialWorldId = getSpecialShineListWorldId(pageIndex);
            if (specialWorldId >= 0)
                return specialWorldId;
            return getWorldIdForShineListHook.orig(layout, pageIndex);
        });

    // The stray Japanese text that used to sit ahead of both counters' glyphs
    // was traced to the recursive show/hide pair above, not to the glyph
    // strings: probes on rs::getWorldCoinCollectPictureFont and
    // rs::getWorldShinePictureFont confirmed every kingdom returns a
    // well-formed picture-font tag (0x000E marker, 6 code units) with the right
    // develop name, including Cloud/Ruined/Dark/Darker correctly falling back
    // to Cap's icon. Both probes have been removed now that they've answered.

    static s32 sCalcWorldNumForShineListLogCount = 0;
    static HkReplace<s32, const al::LayoutActor*> calcWorldNumForShineListHook = hk::hook::replace(
        [](const al::LayoutActor* layout) -> s32 {
            // One line per stage load, always saying the same thing. Three is
            // enough to confirm the page count is what the settings imply.
            if (sCalcWorldNumForShineListLogCount < 3) {
                sCalcWorldNumForShineListLogCount++;
                Randomizer::logPush(
                    "hint system calcWorldNumForShineList call#%d pages=%d medals=%d paintings=%d moonRockKeys=%d subareaKeys=%d/%d",
                    sCalcWorldNumForShineListLogCount, getShineListWorldCount(),
                    Randomizer::isToadetteMedalModeEnabled(),
                    Randomizer::isCloudKingdomMoonsUnlockPaintingsEnabled(),
                    Randomizer::isMoonRockKeyEnabled(),
                    getSubareaKeyCount(), subareaKeyListWorldId());
            }
            return getShineListWorldCount();
        });

    // ── Soft Nuzlocke: the moon list page's own moon counter ────────────────
    //
    // ShineListLayout::updateWorldInfo works out the page's "collected" figure by
    // tallying the rows it has just built, not by asking getShineNum, so the
    // count a Soft reroll carried forward never reached it (confirmed in
    // ida_context/Layout/ShineListLayout.md: the numerator is a local counter
    // incremented per collected row, the denominator is
    // GameDataFunction::getWorldTotalShineNum).
    //
    // The row tally is exactly right for the *entries*, and they stay untouched:
    // nothing is pre-collected any more, so every unticked row on the page is a
    // moon still out there in this seed. Only the header figure is corrected,
    // and only by intercepting the single rs::setCounterAndDenominator call
    // updateWorldInfo makes for that pane.
    //
    // The denominator stays the kingdom's real moon population, so a kingdom
    // that carried in more than it can hold reads as an overflow - 12/8 - rather
    // than being clamped. The numerator is what the player has; clamping it
    // would just move the disagreement somewhere the player cannot see.
    static const void* sShineListCounterTarget = nullptr;
    static s32 sShineListCounterCarry = 0;

    static HkTrampoline<void, al::LayoutActor*> shineListUpdateWorldInfoHook = hk::hook::trampoline(
        [](al::LayoutActor* layout) -> void {
            // Saved and restored rather than cleared: nothing promises this is
            // the outermost call, and a nested rebuild must not leave the window
            // open over whatever draws next.
            const void* previousTarget = sShineListCounterTarget;
            const s32 previousCarry = sShineListCounterCarry;
            const s32 previousUpdatingWorld = sUpdatingShineListWorld;
            sShineListCounterTarget = nullptr;
            sShineListCounterCarry = 0;
            sUpdatingShineListWorld = -1;

            const s32 worldId = layout ? getSelectedShineListWorldId(layout) : -1;
            // The painting and moon-rock-key pages borrow Dark Side's and Darker
            // Side's world ids for a list of something that is not moons. Those
            // two kingdoms carry nothing today, so this changes no behaviour, but
            // the borrowed ids must not start reading a moon count if that ever
            // changes.
            // The Subarea Keys page DOES borrow a real kingdom's id (Cap or
            // Moon), so it must be excluded here or the Nuzlocke moon-count
            // carry would count keys as that kingdom's moons.
            const bool isSyntheticPage = isPaintingListWorld(worldId) || isMoonRockKeyListWorld(worldId)
                || isSubareaKeyListWorld(worldId);
            // Scope every valid page, including the synthetic pages above. The
            // ordinary randomized roster and the borrowed-world custom rosters
            // then use the same transition-safe context throughout orig().
            if (worldId >= 0)
                sUpdatingShineListWorld = worldId;
            if (layout && !isSyntheticPage) {
                GameDataHolderAccessor accessor(layout);
                GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
                const s32 carry = Nuzlocke::getCarriedMoons(file, worldId);
                if (carry > 0) {
                    sShineListCounterTarget = *reinterpret_cast<void* const*>(
                        reinterpret_cast<const u8*>(layout) + cShineListShineCounterLayoutOffset);
                    sShineListCounterCarry = carry;
                }
            }

            shineListUpdateWorldInfoHook.orig(layout);

            sUpdatingShineListWorld = previousUpdatingWorld;
            sShineListCounterTarget = previousTarget;
            sShineListCounterCarry = previousCarry;
        });

    static HkTrampoline<void, void*, s32, s32> setCounterAndDenominatorHook = hk::hook::trampoline(
        [](void* layout, s32 current, s32 denominator) -> void {
            // A shared counter utility - the same call draws the page's regional
            // -coin total right after the moon one, and other layouts use it too
            // - so the window above names the exact pane that may be adjusted
            // rather than trusting call order.
            if (layout && layout == sShineListCounterTarget)
                current += sShineListCounterCarry;
            setCounterAndDenominatorHook.orig(layout, current, denominator);
        });

    // ShineListLayout::updateWorldInfo (0x203ffc) sources the page title from
    // GameDataFunction::tryGetWorldName(layout, worldId), not a plain pane
    // string (confirmed via ida_context/Layout/ShineListLayout.md - the result
    // feeds a RollParts world-name array, not a static "TxtOption"-style pane).
    // That function is already hooked in ProgressionHooks.cpp
    // (tryGetWorldNameForWorldMapHook) - the Toadette Medals title override lives
    // there, merged into the existing hook, rather than a second hook on the
    // same symbol here (double-hooking one function corrupts the trampoline
    // backup and crashes mod init - see that hook's comment).

    void installHooks() {
        findShineWorldIndexHook.installAtMainOffset(cGameDataFileFindShineOffset);
        isGotShineWorldIndexHook.installAtMainOffset(cGameDataFileIsGotShineWorldIndexOffset);
        getWorldTotalShineNumHook.installAtMainOffset(cGetWorldTotalShineNumOffset);
        findShineGetTimeHook.installAtMainOffset(cFindShineGetTimeOffset);
        checkMoonRockShineForShineListHook.installAtMainOffset(cCheckMoonRockShineForShineListOffset);
        checkUnlockHintByHintNpcForShineListHook.installAtMainOffset(cCheckUnlockHintByHintNpcForShineListOffset);
        checkUnlockHintByAmiiboForShineListHook.installAtMainOffset(cCheckUnlockHintByAmiiboForShineListOffset);
        checkShineSeaOfTreeForShineListHook.installAtMainOffset(cCheckShineSeaOfTreeForShineListOffset);
        calcShineNumInOneShineHook.installAtMainOffset(cCalcShineNumInOneShineOffset);
        getWorldIdForShineListHook.installAtMainOffset(cGetWorldIdForShineListOffset);
        shineListControlHook.installAtMainOffset(cShineListControlOffset);
        shineListDecideHook.installAtMainOffset(cShineListDecideOffset);
        // Switched to origin/main's comprehensive fix (see the long comment
        // on isOpenShineNameHook's own definition above) - this hook was
        // previously disabled on this fork after reproducing a null-pointer
        // Data Abort during StageScene::init with Randomize Kingdom Order on,
        // and a narrower bounds-only guard hook ran in its place instead.
        // NEEDS REAL ON-HARDWARE BOOT TESTING (specifically with Randomize
        // Kingdom Order on) before trusting this over that history.
        isOpenShineNameHook.installAtMainOffset(cIsOpenShineNameOffset);
        calcWorldNumForShineListHook.installAtMainOffset(cCalcWorldNumForShineListOffset);
        // shineListUpdateWorldInfoHook.installAtMainOffset(cShineListUpdateWorldInfoOffset);
        setCounterAndDenominatorHook.installAtMainOffset(cRsSetCounterAndDenominatorOffset);
        hk::hook::a64::assemble<"ret">().installAtMainOffset(cUnlockHintAddByMoonRockOffset);
        hackListUpdatePartsDataHook.installAtMainOffset(cHackListUpdatePartsDataOffset);
        hackListControlHook.installAtMainOffset(cHackListControlOffset);
        souvenirListUpdatePartsDataHook.installAtMainOffset(cSouvenirListUpdatePartsDataOffset);
        souvenirListControlHook.installAtMainOffset(cSouvenirListControlOffset);
        collectionListViewStageMapHook.installAtMainOffset(cStageSceneStateCollectionListExeViewStageMapOffset);
        collectionListViewShineListHook.installAtMainOffset(cStageSceneStateCollectionListExeViewShineListOffset);
        collectionListViewHackListHook.installAtMainOffset(cStageSceneStateCollectionListExeViewHackListOffset);
        collectionListViewSouvenirListHook.installAtMainOffset(cStageSceneStateCollectionListExeViewSouvenirListOffset);
        collectionListViewBgmListHook.installAtMainOffset(cStageSceneStateCollectionListExeViewBgmListOffset);
        stageSceneExeCollectionListHook.installAtMainOffset(cStageSceneExeCollectionListOffset);
        stageSceneControlHook.installAtMainOffset(cStageSceneControlOffset);
        shineOnSwitchGetHook.installAtSym<"_ZN5Shine11onSwitchGetEv">();
        stageSceneExeDemoShineGetHook.installAtMainOffset(cStageSceneExeDemoShineGetOffset);
        stageSceneStateGetShineExeDemoGetFirstHook.installAtMainOffset(
            cStageSceneStateGetShineExeDemoGetFirstOffset);
        actionPadAndCameraStartPadRumbleHook.installAtMainOffset(
            cActionPadAndCameraStartPadRumbleOffset);
        tryChangeNextStageHook.installAtSym<"_ZN16GameDataFunction18tryChangeNextStageE20GameDataHolderWriterPK15ChangeStageInfo">();
        liveActorMakeActorAliveHook.installAtSym<"_ZN2al9LiveActor14makeActorAliveEv">();
        liveActorMovementHook.installAtSym<"_ZN2al9LiveActor8movementEv">();
        liveActorMakeActorDeadHook.installAtSym<"_ZN2al9LiveActor13makeActorDeadEv">();
        footerTryChangeTextFadeHook.installAtSym<"_ZN11FooterParts17tryChangeTextFadeEPKDs">();

        isPadTriggerLHook.installAtSym<"_ZN2al13isPadTriggerLEi">();
        isPadTriggerRHook.installAtSym<"_ZN2al13isPadTriggerREi">();
    }

} // namespace HintSystem
