#include "Scene/StageSceneStateModConfig.hpp"

#include "sead/container/seadSafeArray.h"
#include "sead/prim/seadSafeString.h"

#include "al/Library/Controller/InputFunction.h"
#include "al/Library/Layout/LayoutActionFunction.h"
#include "al/Library/Layout/LayoutActorUtil.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Nerve/NerveUtil.h"
#include "al/Library/Play/Layout/RollParts.h"

#include "Sequence/HakoniwaSequence.h"
#include "System/GameDataFunction.h"
#include "System/GameSystem.h"
#include "game/Layout/CommonVerticalList.h"
#include "game/Layout/FooterParts.h"
#include "game/Layout/SimpleLayoutMenu.h"
#include "game/MapObj/ChangeStageInfo.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataFunction.h"
#include "game/System/GameDataHolderAccessor.h"
#include "game/System/GameDataHolderWriter.h"
#include "game/System/SaveDataAccessFunction.h"
#include "game/Util/StageInputFunction.h"

#include "../audio/CustomBgmManager.h"
#include "Randomizer/AbilityRando.h"
#include "Randomizer/CaptureRando.h"
#include "Randomizer/EntranceHooks.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/PauseMenuHooks.h"
#if SMOR_FEATURE_CHAOS_ANY
#include "Randomizer/EnemySwapDB.h"
#include "Randomizer/EnemySwapDebug.h"
#include "Randomizer/EnemySwapRando.h"
#endif
#include "Multiplayer/ServerConfig.h"
#if SMOR_FEATURE_MULTIPLAYER
#include "Multiplayer/server/Client.hpp"
#endif
#include "Randomizer/GlobalConfig.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/MusicRando.h"
#include "Randomizer/PathHintOverlay.h"
#include "Randomizer/ProgressionHooks.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/ScenarioNames.h"
#include "Randomizer/ScenarioPatcher.h"
#include "Randomizer/TrackerBridge.h"
#include "Randomizer/WorldMeta.h"

#include "SD/SDHooks.h"

#include "nn/err.h"
#include "nn/util.h"

#include "../stage_warp.h"
#include "EntranceDB.h"
#include "ShineDB.h"
#include "heap/seadHeapMgr.h"

#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static const char* getTogglePrefix(bool enabled) {
    return enabled ? "[ X ] " : "[    ] ";
}

namespace {

    constexpr s32 kFooterBufferSize = 0x200;
    constexpr u32 kMaxSeedValue = 99999999u;

    // Preserve the persisted enum numbering while presenting the mode in the
    // user-facing Off/Standard/Extended/Max order.
    const Randomizer::MusicRandoMode sMusicRandoUiOrder[] = {
        Randomizer::MusicRandoMode::Off,
        Randomizer::MusicRandoMode::On,
        Randomizer::MusicRandoMode::Extended,
        Randomizer::MusicRandoMode::Max,
    };
    constexpr s32 sMusicRandoUiCount = sizeof(sMusicRandoUiOrder) / sizeof(sMusicRandoUiOrder[0]);

    s32 musicRandoModeToUiIndex(Randomizer::MusicRandoMode mode) {
        for (s32 i = 0; i < sMusicRandoUiCount; i++)
            if (sMusicRandoUiOrder[i] == mode)
                return i;
        return 0;
    }

    Randomizer::MusicRandoMode musicRandoModeFromUiIndex(s32 index) {
        if (index < 0 || index >= sMusicRandoUiCount)
            return Randomizer::MusicRandoMode::Off;
        return sMusicRandoUiOrder[index];
    }

    constexpr char16_t kMessageTagMark = 0x000e;
    constexpr char16_t kPadStyleGroup = 0x0006;
    constexpr char16_t kAButtonPadStyle = 0x0002;
    constexpr char16_t kBButtonPadStyle = 0x0003;
    constexpr char16_t kXButtonPadStyle = 0x0004; // sequential with A/B (verify glyph in-game)
    constexpr char16_t kYButtonPadStyle = 0x0005;
    constexpr char16_t kLButtonPadStyle = 0x0006;
    constexpr char16_t kRButtonPadStyle = 0x0007;
    constexpr char16_t kLStickPadStyle = 0x0010;
    constexpr char16_t kRStickPadStyle = 0x0011;

    static char16_t sMainFooterText[kFooterBufferSize] = {};
    static char16_t sRandomizerFooterText[kFooterBufferSize] = {};
    static char16_t sReadOnlyFooterText[kFooterBufferSize] = {};
#if SMOR_FEATURE_CHAOS_ANY
    static char16_t sEnemyInspectorFooterText[kFooterBufferSize] = {};
#endif

    constexpr s32 kSingleColumnVisibleRows = 9;
    constexpr s32 kSingleColumnListParts = kSingleColumnVisibleRows + 3;

    static s32 getFooterTextLength(const char16_t* text) {
        s32 length = 0;
        while (text && text[length])
            length++;
        return length;
    }

    static s32 appendFooterText(char16_t* buffer, s32 offset, const char16_t* text) {
        if (!buffer || !text)
            return offset;

        while (*text && offset < kFooterBufferSize - 1)
            buffer[offset++] = *text++;
        buffer[offset] = 0;
        return offset;
    }

    static s32 appendPadStyleLabel(char16_t* buffer, s32 offset, const char16_t* label) {
        const s32 labelLength = getFooterTextLength(label);
        buffer[offset++] = static_cast<char16_t>(labelLength * sizeof(char16_t));

        for (s32 i = 0; i < labelLength; i++)
            buffer[offset++] = label[i];

        return offset;
    }

    static s32 getPadStylePayloadWordCount(
        const char16_t* dualLabel, const char16_t* handheldLabel,
        const char16_t* fullKeyLabel, const char16_t* leftLabel,
        const char16_t* rightLabel) {
        return 5
            + getFooterTextLength(dualLabel)
            + getFooterTextLength(handheldLabel)
            + getFooterTextLength(fullKeyLabel)
            + getFooterTextLength(leftLabel)
            + getFooterTextLength(rightLabel);
    }

    static s32 appendPadStyleTag(
        char16_t* buffer, s32 offset, char16_t padStyleTag,
        const char16_t* dualLabel, const char16_t* handheldLabel,
        const char16_t* fullKeyLabel, const char16_t* leftLabel,
        const char16_t* rightLabel) {
        if (!buffer)
            return offset;

        const s32 payloadWordCount = getPadStylePayloadWordCount(
            dualLabel, handheldLabel, fullKeyLabel, leftLabel, rightLabel);
        const s32 tagWordCount = 4 + payloadWordCount;
        if (offset + tagWordCount >= kFooterBufferSize)
            return offset;

        buffer[offset++] = kMessageTagMark;
        buffer[offset++] = kPadStyleGroup;
        buffer[offset++] = padStyleTag;
        buffer[offset++] = static_cast<char16_t>(payloadWordCount * sizeof(char16_t));
        offset = appendPadStyleLabel(buffer, offset, dualLabel);
        offset = appendPadStyleLabel(buffer, offset, handheldLabel);
        offset = appendPadStyleLabel(buffer, offset, fullKeyLabel);
        offset = appendPadStyleLabel(buffer, offset, leftLabel);
        offset = appendPadStyleLabel(buffer, offset, rightLabel);
        buffer[offset] = 0;
        return offset;
    }

    static s32 appendLStickIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kLStickPadStyle,
            u"DualStickL", u"HandheldStickL", u"FullKeyStickL", u"LeftStickL", u"RightStickL");
    }

    static s32 appendRStickIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kRStickPadStyle,
            u"DualStickR", u"HandheldStickR", u"FullKeyStickR", u"LeftStickR", u"RightStickR");
    }

    static s32 appendBButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kBButtonPadStyle,
            u"DualButtonB", u"HandheldButtonB", u"FullKeyButtonB", u"LeftButtonB", u"RightButtonB");
    }

    static s32 appendXButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kXButtonPadStyle,
            u"DualButtonX", u"HandheldButtonX", u"FullKeyButtonX", u"LeftButtonX", u"RightButtonX");
    }

    static s32 appendYButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kYButtonPadStyle,
            u"DualButtonY", u"HandheldButtonY", u"FullKeyButtonY", u"LeftButtonY", u"RightButtonY");
    }

    static s32 appendAButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kAButtonPadStyle,
            u"DualButtonA", u"HandheldButtonA", u"FullKeyButtonA", u"LeftButtonA", u"RightButtonA");
    }

    static s32 appendLButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kLButtonPadStyle,
            u"DualButtonL", u"HandheldButtonL", u"FullKeyButtonL", u"LeftButtonL", u"RightButtonL");
    }

    static s32 appendRButtonIcon(char16_t* buffer, s32 offset) {
        return appendPadStyleTag(buffer, offset, kRButtonPadStyle,
            u"DualButtonR", u"HandheldButtonR", u"FullKeyButtonR", u"LeftButtonR", u"RightButtonR");
    }

    static const char16_t* makeMainFooterText() {
        s32 offset = 0;
        offset = appendLStickIcon(sMainFooterText, offset);
        offset = appendFooterText(sMainFooterText, offset, u" Select  ");
        offset = appendBButtonIcon(sMainFooterText, offset);
        offset = appendFooterText(sMainFooterText, offset, u" Back  ");
        offset = appendAButtonIcon(sMainFooterText, offset);
        appendFooterText(sMainFooterText, offset, u" Confirm");
        return sMainFooterText;
    }

    static const char16_t* makeRandomizerFooterText() {
        s32 offset = 0;
        offset = appendLStickIcon(sRandomizerFooterText, offset);
        offset = appendFooterText(sRandomizerFooterText, offset, u" Select ");
        offset = appendRStickIcon(sRandomizerFooterText, offset);
        offset = appendFooterText(sRandomizerFooterText, offset, u" Roll ");
        offset = appendBButtonIcon(sRandomizerFooterText, offset);
        offset = appendFooterText(sRandomizerFooterText, offset, u" Back ");
        offset = appendAButtonIcon(sRandomizerFooterText, offset);
        appendFooterText(sRandomizerFooterText, offset, u" Confirm");
        return sRandomizerFooterText;
    }

    static const char16_t* makeReadOnlyFooterText() {
        s32 offset = 0;
        offset = appendLStickIcon(sReadOnlyFooterText, offset);
        offset = appendFooterText(sReadOnlyFooterText, offset, u" Select  ");
        offset = appendBButtonIcon(sReadOnlyFooterText, offset);
        appendFooterText(sReadOnlyFooterText, offset, u" Back");
        return sReadOnlyFooterText;
    }

    // Kingdom Checks stays read-only, but a check sitting inside a sub-area can
    // show how to get in. That route is free here: this list is a tracker aid,
    // and the check's location is already on screen.
    static const char16_t* makeKingdomChecksFooterText(bool showRoute) {
        if (!showRoute)
            return makeReadOnlyFooterText();

        s32 offset = 0;
        offset = appendLStickIcon(sReadOnlyFooterText, offset);
        offset = appendFooterText(sReadOnlyFooterText, offset, u" Select  ");
        offset = appendAButtonIcon(sReadOnlyFooterText, offset);
        offset = appendFooterText(sReadOnlyFooterText, offset, u" View Path  ");
        offset = appendBButtonIcon(sReadOnlyFooterText, offset);
        appendFooterText(sReadOnlyFooterText, offset, u" Back");
        return sReadOnlyFooterText;
    }

#if SMOR_FEATURE_CHAOS_ANY
    static const char16_t* makeEnemyInspectorFooterText() {
        s32 offset = 0;
        offset = appendLStickIcon(
            sEnemyInspectorFooterText, offset);
        offset = appendFooterText(
            sEnemyInspectorFooterText, offset, u" Select  ");
        offset = appendRStickIcon(
            sEnemyInspectorFooterText, offset);
        offset = appendFooterText(
            sEnemyInspectorFooterText, offset, u" Roll  ");
        offset = appendAButtonIcon(
            sEnemyInspectorFooterText, offset);
        offset = appendFooterText(
            sEnemyInspectorFooterText, offset, u" Set Pair  ");
        offset = appendBButtonIcon(
            sEnemyInspectorFooterText, offset);
        appendFooterText(
            sEnemyInspectorFooterText, offset, u" Back");
        return sEnemyInspectorFooterText;
    }
#endif

    static char16_t sSettingsFooterText[kFooterBufferSize] = {};

    // showColumnHint: a two-column FileSelect page spends D-pad left/right on
    // crossing between its columns, so values roll on the right stick alone
    // there. Every other page (and the whole in-game Mod Menu) leaves it off.
    static const char16_t* buildSettingsFooterText(bool showApplyHint, bool showDeleteHint,
        bool showColumnHint = false, bool showAdvancedHint = false, bool advancedOnA = false,
        bool hasDecideAction = true, bool showPlusHint = false) {
        s32 offset = 0;
        offset = appendLButtonIcon(sSettingsFooterText, offset);
        offset = appendFooterText(sSettingsFooterText, offset, u" / ");
        offset = appendRButtonIcon(sSettingsFooterText, offset);
        offset = appendFooterText(sSettingsFooterText, offset, u" Page  ");
        if (showColumnHint) {
            offset = appendFooterText(sSettingsFooterText, offset, u"Left/Right Column  ");
            offset = appendRStickIcon(sSettingsFooterText, offset);
            offset = appendFooterText(sSettingsFooterText, offset, u" Value  ");
        }
        offset = appendLStickIcon(sSettingsFooterText, offset);
        offset = appendFooterText(sSettingsFooterText, offset, u" Move  ");
        if (hasDecideAction || advancedOnA) {
            offset = appendAButtonIcon(sSettingsFooterText, offset);
            offset = appendFooterText(sSettingsFooterText, offset,
                advancedOnA ? u" Advanced  " : (showApplyHint ? u" Apply  " : u" Toggle  "));
        }
        if (showDeleteHint) {
            offset = appendYButtonIcon(sSettingsFooterText, offset);
            offset = appendFooterText(sSettingsFooterText, offset, u" Delete  ");
        }
        if (showAdvancedHint && !advancedOnA) {
            offset = appendYButtonIcon(sSettingsFooterText, offset);
            offset = appendFooterText(sSettingsFooterText, offset, u" Advanced  ");
        }
        if (showPlusHint)
            offset = appendFooterText(sSettingsFooterText, offset, u"+ Confirm  ");
        offset = appendBButtonIcon(sSettingsFooterText, offset);
        appendFooterText(sSettingsFooterText, offset, u" Back");
        return sSettingsFooterText;
    }

    static void setStandardMenuFooter(FooterParts** footers, s32 menuIndex,
        al::LayoutActor* menu, const al::LayoutInitInfo& initInfo) {
        footers[menuIndex] = new FooterParts(menu, initInfo, u"", "TxtGuide", "ParFooter");
        footers[menuIndex]->changeText(makeMainFooterText());
    }

    static void configureSingleColumnScrollableList(CommonVerticalList* list) {
        if (!list || !list->mListPartsArr)
            return;

        const s32 originalPartsNum = list->mListPartsNum;
        const s32 activePartsNum = originalPartsNum > kSingleColumnListParts
            ? kSingleColumnListParts
            : originalPartsNum;

        for (s32 i = 0; i < originalPartsNum; i++) {
            al::LayoutActor* item = list->mListPartsArr[i];
            if (!item)
                continue;

            StageSceneStateModConfig::applyRowStyle(
                item, StageSceneStateModConfig::RowStyle::Base);
            if (i >= activePartsNum)
                al::startAction(item, "Hide", nullptr);
        }

        list->mListPartsNum = activePartsNum;
    }

    static void setMenuRowText(sead::WFixedSafeString<0x200>& row, const char* fmt, ...) {
        char buf[96];
        va_list args;
        va_start(args, fmt);
        nn::util::VSNPrintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        row.convertFromMultiByteString(buf, strlen(buf));
    }

    static void handleTwoColumnNavigationInput(
        InputSeparator* input, CommonVerticalList* list,
        s32 optionCount, s32 colSize, int& activeColumn) {
        if (!input || !list)
            return;

        const s32 selected = list->mCurSelected;
        if (input->isTriggerUiRight() && selected < colSize) {
            const s32 target = selected + colSize;
            if (target < optionCount)
                list->setSelectedIdx(target, 0);
        } else if (input->isTriggerUiLeft() && selected >= colSize) {
            const s32 target = selected - colSize;
            if (target >= 0)
                list->setSelectedIdx(target, 0);
        }

        if (input->isHoldUiUp() && input->isRepeatUiUp()) {
            if (input->isTriggerUiUp() && list->mCurSelected == list->mTopSelectableIdx)
                list->jumpBottom();
            list->up();
        }

        if (input->isHoldUiDown() && input->isRepeatUiDown()) {
            if (input->isTriggerUiDown() && list->mCurSelected == (list->mDataCount - 1))
                list->jumpTop();
            list->down();
        }

        activeColumn = list->mCurSelected >= colSize ? 1 : 0;
    }

    // --- Misc/QoL/Scenario Override description text ---------------------------
    // Rendered by ModConfigInfoOverlay (ImGui, in src/Randomizer/ModConfigInfoOverlay.cpp)
    // rather than a native layout part: the title scene's layout-executor table
    // is already at capacity (see the MENU_RANDOMIZER_MISC page-sharing comment
    // above), and a new SimpleLayoutMenu here overflowed it and crashed at boot.
    // Only the word-wrap (a plain string transform) lives here; ModConfigInfoOverlay
    // owns all actual drawing.

    // Greedy word-wrap into '\n'-separated lines no wider than `cols` chars.
    constexpr s32 kMiscInfoWrapCols = 48;
    void wrapMiscInfoText(const char* src, char* out, size_t outSize, s32 cols) {
        size_t o = 0;
        s32 lineLen = 0;
        size_t i = 0;
        if (!src)
            src = "";
        while (src[i] && o + 1 < outSize) {
            while (src[i] == ' ')
                i++;
            const size_t wordStart = i;
            while (src[i] && src[i] != ' ')
                i++;
            const s32 wordLen = static_cast<s32>(i - wordStart);
            if (wordLen == 0)
                break;

            const bool needsSpace = lineLen > 0;
            const s32 neededLen = lineLen + (needsSpace ? 1 : 0) + wordLen;
            if (lineLen > 0 && neededLen > cols) {
                if (o + 1 < outSize)
                    out[o++] = '\n';
                lineLen = 0;
            } else if (needsSpace && o + 1 < outSize) {
                out[o++] = ' ';
                lineLen++;
            }

            for (s32 k = 0; k < wordLen && o + 1 < outSize; k++)
                out[o++] = src[wordStart + k];
            lineLen += wordLen;
        }
        out[o] = '\0';
    }

} // namespace

const char16_t* StageSceneStateModConfig::makeStandardFooterText() {
    return makeMainFooterText();
}

const char16_t* StageSceneStateModConfig::makeSettingsFooterText(bool showApplyHint, bool showDeleteHint,
    bool showColumnHint, bool showAdvancedHint, bool advancedOnA, bool hasDecideAction,
    bool showPlusHint) {
    return buildSettingsFooterText(showApplyHint, showDeleteHint, showColumnHint, showAdvancedHint,
        advancedOnA, hasDecideAction, showPlusHint);
}

const char16_t* StageSceneStateModConfig::getMotionFreeHintLine(s32 lineIdx) {
    static char16_t buf[kFooterBufferSize];
    s32 o = 0;
    buf[0] = 0;

    switch (lineIdx) {
    case 0:
        o = appendFooterText(buf, o, u"No motion needed:  ");
        o = appendXButtonIcon(buf, o);
        o = appendFooterText(buf, o, u" Up-throw   ");
        break;
    case 1:
        o = appendBButtonIcon(buf, o);
        o = appendFooterText(buf, o, u" Down-throw");
        break;
    case 2:
        o = appendRButtonIcon(buf, o);
        o = appendFooterText(buf, o, u" Spin / Shake (captures & rolling)");
        break;
    case 3:
        o = appendRButtonIcon(buf, o);
        o = appendFooterText(buf, o, u" Home in");
        break;
    default:
        return nullptr;
    }

    buf[o] = 0;
    return buf;
}

void StageSceneStateModConfig::configureScrollableList(CommonVerticalList* list) {
    configureSingleColumnScrollableList(list);
}

void StageSceneStateModConfig::repositionTwoColumnPanes(CommonVerticalList* list,
    s32 optionCount, s32 colSize) {
    if (!list || !list->mListPartsArr)
        return;

    static constexpr float kLeftX = -260.f;
    static constexpr float kRightX = 260.f;

    for (s32 i = 0; i < colSize; i++) {
        const s32 leftIndex = i;
        const s32 rightIndex = i + colSize;
        if (leftIndex >= optionCount)
            continue;

        // The list array is offset by one: parts[0] is not a data row.
        al::LayoutActor* leftItem = list->mListPartsArr[leftIndex + 1];
        if (!leftItem)
            continue;

        const float rowY = al::getLocalTrans(leftItem).y;
        al::setLocalTrans(leftItem, sead::Vector2f(kLeftX, rowY));

        if (rightIndex < optionCount) {
            al::LayoutActor* rightItem = list->mListPartsArr[rightIndex + 1];
            if (rightItem)
                al::setLocalTrans(rightItem, sead::Vector2f(kRightX, rowY));
        }
    }
}

// ============================================================================
// Constructor
// ============================================================================

bool StageSceneStateModConfig::sSuppressLiveData = false;
bool StageSceneStateModConfig::sEnterOnMultiplayerPage = false;
bool StageSceneStateModConfig::sMultiplayerFileSelectRequest = false;
// Zero-initialised once at process start: "Show IP and Port" is off every time
// the mod boots, and nothing writes either of these to disk.
bool StageSceneStateModConfig::sMpShowEndpoint = false;
char StageSceneStateModConfig::sMpRoomName[17] = {};

StageSceneStateModConfig::StageSceneStateModConfig(
    const char* name, al::Scene* scene,
    const al::LayoutInitInfo& initInfo,
    FooterParts* footerParts,
    GameDataHolder* dataHolder,
    bool)
    : al::HostStateBase<al::Scene>(name, scene) {
    mFooterParts = footerParts;
    mGameDataHolder = dataHolder;
    mMsgSystem = initInfo.getMessageSystem();
    mInput = new InputSeparator(mHost, true);

    Randomizer::logPush("ModConfig ctor: begin holder=%p heap=%p", dataHolder,
        sead::HeapMgr::instance()->getCurrentHeap());

    // Each message list contains 64 inline WFixedSafeString<0x200> buffers.
    // Allocating all eight lists from the current Scene FrameHeap consumes
    // roughly half a megabyte before al::Scene::endInit() builds its draw
    // tables.  On WorldMapStage that left less than the 0x3f0 bytes needed by
    // MeshDrawer's ModelShaderAssign and made FrameHeap::tryAlloc assert.
    //
    // The strings are fixed-capacity scratch storage, not scene resources, so
    // keep them in the module's static storage instead.  Two gameplay banks
    // let the initialize thread construct an incoming stage without rewriting
    // buffers that the outgoing stage may still be drawing.  The title gets a
    // separate bank because its pause-menu state can overlap a stage during a
    // scene transition.  Reusing the banks also avoids leaking allocations in
    // a process-lifetime heap on every stage load.
    using MessageList = sead::SafeArray<sead::WFixedSafeString<0x200>, maxMsgCount>;
    static MessageList sTitleMessageLists[menuCount];
    static MessageList sGameplayMessageLists[2][menuCount];
    static u32 sNextGameplayMessageBank = 0;

    MessageList* messageLists = nullptr;
    u32 gameplayBank = 0;
    if (sSuppressLiveData) {
        messageLists = sTitleMessageLists;
    } else {
        gameplayBank = sNextGameplayMessageBank;
        messageLists = sGameplayMessageLists[gameplayBank];
        sNextGameplayMessageBank ^= 1;
    }
    for (int i = 0; i < menuCount; i++) {
        msgList[i] = &messageLists[i];
        for (int row = 0; row < maxMsgCount; row++)
            msgList[i]->mBuffer[row].clear();
    }

#if SMOR_FEATURE_CHAOS_ANY
    // These arrays used to be inline members of StageSceneStateModConfig.  In
    // particular, one 0x40-character label for every generated placement made
    // the state itself well over a megabyte.  Since the state is created with
    // the scene heap as the current heap, that cost was paid before the first
    // constructor log line and compounded the message-list exhaustion above.
    struct EnemyInspectorStorage {
        s16 sortedReplacementTargets[Randomizer::EnemySwapDB::sEntityCount];
        sead::WFixedSafeString<0x40> replacementTargetLabels[Randomizer::EnemySwapDB::sEntityCount];
        const char16_t* replacementChoiceLabels[kEnemyInspectorChoiceCount];
        s16 replacementChoiceEntities[kEnemyInspectorChoiceCount];
        const char16_t* objectChoiceLabels[kEnemyInspectorObjectChoiceCount];
        s16 objectChoicePlacements[kEnemyInspectorObjectChoiceCount];
        sead::WFixedSafeString<0x40> objectIdLabels[Randomizer::EnemySwapDB::sPlacementCount];
    };
    //
    // Moving them here traded a scene-heap problem for a .bss one: three banks
    // at ~1.7 MB each are reserved for the whole run to serve a debug menu that
    // is usually never opened. Together with the multiplayer socket pool that
    // was enough to push the module past what the process could map next to the
    // game's heap -- see the socketPoolSize note in DebugMenuHooks.cpp. The
    // durable fix is to allocate these from a non-scene heap on first use
    // instead; the scene heap remains the one place they must not come from.
    static EnemyInspectorStorage sTitleEnemyInspectorStorage = {};
    static EnemyInspectorStorage sGameplayEnemyInspectorStorage[2] = {};

    EnemyInspectorStorage& inspectorStorage = sSuppressLiveData
        ? sTitleEnemyInspectorStorage
        : sGameplayEnemyInspectorStorage[gameplayBank];
    std::memset(inspectorStorage.sortedReplacementTargets, 0,
        sizeof(inspectorStorage.sortedReplacementTargets));
    std::memset(inspectorStorage.replacementChoiceLabels, 0,
        sizeof(inspectorStorage.replacementChoiceLabels));
    std::memset(inspectorStorage.replacementChoiceEntities, 0,
        sizeof(inspectorStorage.replacementChoiceEntities));
    std::memset(inspectorStorage.objectChoiceLabels, 0,
        sizeof(inspectorStorage.objectChoiceLabels));
    std::memset(inspectorStorage.objectChoicePlacements, 0,
        sizeof(inspectorStorage.objectChoicePlacements));
    for (s32 i = 0; i < Randomizer::EnemySwapDB::sEntityCount; i++)
        inspectorStorage.replacementTargetLabels[i].clear();
    for (s32 i = 0; i < Randomizer::EnemySwapDB::sPlacementCount; i++)
        inspectorStorage.objectIdLabels[i].clear();

    mEnemyInspectorSortedObjectReplacementTargets = inspectorStorage.sortedReplacementTargets;
    mEnemyInspectorObjectReplacementTargetLabels = inspectorStorage.replacementTargetLabels;
    mEnemyInspectorObjectReplacementChoiceLabels = inspectorStorage.replacementChoiceLabels;
    mEnemyInspectorObjectReplacementChoiceEntities = inspectorStorage.replacementChoiceEntities;
    mEnemyInspectorObjectChoiceLabels = inspectorStorage.objectChoiceLabels;
    mEnemyInspectorObjectChoicePlacements = inspectorStorage.objectChoicePlacements;
    mEnemyInspectorObjectIdLabels = inspectorStorage.objectIdLabels;
#endif

    Randomizer::logPush("ModConfig ctor: init Main");
    initMainMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: init Randomizer");
    initRandomizerMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: init Randomizer Misc");
    initRandomizerMiscMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: init CaptureDebug");
    initCaptureDebugMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: init AbilityDebug");
    initAbilityDebugMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: init DebugWarp");
    initDebugWarpKingdomMenu(initInfo);
    initDebugWarpSubareaMenu(initInfo);
    Randomizer::logPush("ModConfig ctor: menus done");

    mCurrentList = optionsList[MENU_MAIN];
    mCurrentMenu = menuList[MENU_MAIN];
    mKeyboard = new Keyboard();
    Randomizer::logPush("ModConfig ctor: end");
}

// ============================================================================
// Lifecycle
// ============================================================================

void StageSceneStateModConfig::init() {
    initNerve(&NrvStageSceneStateModConfig.MainMenu, 0);
}

void StageSceneStateModConfig::appear() {
    mCurrentMenu->startAppear("Appear");
    al::NerveStateBase::appear();
}

void StageSceneStateModConfig::kill() {
#if SMOR_FEATURE_CHAOS_ANY
    EnemySwapDebug::setInspectorOverlayContext(false, nullptr, nullptr);
#endif
    hideKingdomChecksRouteView();
    mCurrentMenu->startEnd("End");
    al::NerveStateBase::kill();
    SaveDataAccessFunction::startSaveDataWrite(mGameDataHolder);
}

// ============================================================================
// Main Menu
// ============================================================================

void StageSceneStateModConfig::initMainMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_MAIN] = new SimpleLayoutMenu(
        "ModConfigMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_MAIN] = new CommonVerticalList(
        menuList[MENU_MAIN], initInfo, true);

    al::setPaneString(menuList[MENU_MAIN], "TxtOption", u"Mod Configuration", 0);
    setStandardMenuFooter(mMenuFooters, MENU_MAIN, menuList[MENU_MAIN], initInfo);
    rebuildMainMenuOptionMap();
    optionsList[MENU_MAIN]->initDataNoResetSelected(mMainMenuOptionCount);
    updateMainMenuOptions();
    optionsList[MENU_MAIN]->addStringData(msgList[MENU_MAIN]->mBuffer, "TxtContent");

    for (int i = 0; i < mMainMenuOptionCount; i++)
        applyRowStyle(optionsList[MENU_MAIN]->mListPartsArr[i + 1], RowStyle::Base);
}

void StageSceneStateModConfig::rebuildMainMenuOptionMap() {
    mMainMenuOptionCount = 0;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_RANDOMIZER_SETTINGS;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_RANDOMIZER_MISC;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_QUALITY_OF_LIFE;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_KINGDOM_CHECKS;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_DEBUG_WARP;
    mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_ADD_COINS;

    // Reads the live GameDataHolder's current world/scenario, so it's meaningless
    // (and, at title re-init, unsafe) outside an active in-game session.
    if (!sSuppressLiveData)
        mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_SCENARIO_OVERRIDE;

#if SMOR_FEATURE_CHAOS_ANY
    if (!sSuppressLiveData)
        mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_ENEMY_SWAP_INSPECTOR;
#endif

    if (Randomizer::isCaptureLockFeatureEnabled())
        mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_CAPTURE_DEBUG;
    if (Randomizer::isAbilityLockFeatureEnabled())
        mMainMenuOptionMap[mMainMenuOptionCount++] = MAIN_ABILITY_DEBUG;
}

void StageSceneStateModConfig::updateMainMenuOptions() {
    for (int i = 0; i < maxMsgCount; i++)
        msgList[MENU_MAIN]->mBuffer[i].copy(u"");

    for (int i = 0; i < mMainMenuOptionCount; i++) {
        switch (mMainMenuOptionMap[i]) {
        case MAIN_RANDOMIZER_SETTINGS:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Randomization Overview");
            break;
        case MAIN_RANDOMIZER_MISC:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Misc Randomization");
            break;
        case MAIN_QUALITY_OF_LIFE:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Quality of Life");
            break;
        case MAIN_KINGDOM_CHECKS:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Kingdom Checks");
            break;
        case MAIN_DEBUG_WARP:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Debug Warp");
            break;
        case MAIN_ADD_COINS:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Add 100000 Coins");
            break;
        case MAIN_SCENARIO_OVERRIDE:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Scenario Override");
            break;
#if SMOR_FEATURE_CHAOS_ANY
        case MAIN_ENEMY_SWAP_INSPECTOR:
            msgList[MENU_MAIN]->mBuffer[i].copy(
                u"Enemy Swap Inspector");
            break;
#endif
        case MAIN_CAPTURE_DEBUG:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Capture Debug");
            break;
        case MAIN_ABILITY_DEBUG:
            msgList[MENU_MAIN]->mBuffer[i].copy(u"Ability Debug");
            break;
        }
    }
}

int StageSceneStateModConfig::getSelectedMainMenuOption() const {
    const int selected = optionsList[MENU_MAIN]->mCurSelected;
    if (selected < 0 || selected >= mMainMenuOptionCount)
        return -1;
    return mMainMenuOptionMap[selected];
}

void StageSceneStateModConfig::exeMainMenu() {
    if (al::isFirstStep(this)) {
        mCurrentList = optionsList[MENU_MAIN];
        mCurrentMenu = menuList[MENU_MAIN];
        activateInput();

        // Title screen's Multiplayer row: skip straight past the main menu onto
        // the Multiplayer page. Deliberately done HERE rather than in appear():
        // this is the exact same nerve transition a Decide on a main-menu row
        // would make, so the init() -> MainMenu flow and everything appear()
        // does stay untouched. The main menu is never drawn - subMenuStart()
        // ends its layout on the very next step.
        if (sEnterOnMultiplayerPage) {
            sEnterOnMultiplayerPage = false;
            mMpStandalonePage = true;
            mRandoMiscPage = RANDO_MISC_PAGE_MULTIPLAYER;
            al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
            return;
        }
        mMpStandalonePage = false;
    }

    handleMenuInput();

    if (rs::isTriggerUiCancel(mHost))
        kill();

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        switch (getSelectedMainMenuOption()) {
        case MAIN_RANDOMIZER_SETTINGS:
            al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerSettings);
            break;
        case MAIN_RANDOMIZER_MISC:
            mRandoMiscPage = RANDO_MISC_PAGE_MISC;
            al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
            break;
        case MAIN_QUALITY_OF_LIFE:
            mRandoMiscPage = RANDO_MISC_PAGE_QOL;
            al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
            break;
        case MAIN_KINGDOM_CHECKS:
            mKingdomPickerMode = KINGDOM_PICKER_CHECKS;
            al::setNerve(this, &NrvStageSceneStateModConfig.DebugWarpKingdom);
            break;
        case MAIN_DEBUG_WARP:
            mKingdomPickerMode = KINGDOM_PICKER_DEBUG_WARP;
            al::setNerve(this, &NrvStageSceneStateModConfig.DebugWarpKingdom);
            break;
        case MAIN_ADD_COINS:
            // 100000 rather than 1000 so the uncapped-coin ceiling is actually
            // reachable for testing - at 1000 a press it takes a thousand
            // presses to find out what the counter does past 999999. Clamped
            // by GameDataFile::addCoin either way: vanilla stops at 9999, and
            // with Uncapped Coins on, CoinUncap::cMaxCoins stops it at 999999.
            GameDataFunction::addCoin(GameDataHolderWriter(mGameDataHolder), 100000);
            activateInput();
            break;
        case MAIN_SCENARIO_OVERRIDE:
            mRandoMiscPage = RANDO_MISC_PAGE_SCENARIO_OVERRIDE;
            al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
            break;
#if SMOR_FEATURE_CHAOS_ANY
        case MAIN_ENEMY_SWAP_INSPECTOR:
            mKingdomPickerMode = KINGDOM_PICKER_ENEMY_INSPECTOR;
            al::setNerve(
                this,
                &NrvStageSceneStateModConfig.EnemySwapInspector);
            break;
#endif
        case MAIN_CAPTURE_DEBUG:
            al::setNerve(this, &NrvStageSceneStateModConfig.CaptureDebug);
            break;
        case MAIN_ABILITY_DEBUG:
            al::setNerve(this, &NrvStageSceneStateModConfig.AbilityDebug);
            break;
        }
    }
}

// ============================================================================
// Keyboard helpers
// ============================================================================

static void stageReload(HakoniwaSequence* seq) {
    GameDataHolderWriter* holder = static_cast<GameDataHolderWriter*>(&seq->mGameDataHolderAccessor);
    GameDataHolderAccessor accessor = seq->mGameDataHolderAccessor;
    // Persist the just-changed randomizer settings before reloading: the reload
    // tears the menu down without running kill(), whose save would otherwise be
    // the only thing writing the config (e.g. Painting Rando reverting on reboot).
    // Must be the sync variant: the async write races the stage change and can
    // be dropped before the savedata commit, so the toggle never reaches disk.
    SaveDataAccessFunction::startSaveDataWriteSync(accessor.mData);
    const char* stageName = GameDataFunction::getCurrentStageName(accessor);
    const char* startId = GameDataFunction::tryGetPlayerStartId(accessor);
    ChangeStageInfo info(*holder, startId ? startId : "", stageName, false, -1,
        ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
    info.setWipeType("FadeBlack");
    GameDataFunction::tryChangeNextStage(*holder, &info);
}

static void showNotAvailableError() {
    nn::err::ApplicationErrorArg obj;
    obj.SetApplicationErrorCodeNumber(01);
    obj.SetDialogMessage("SMO-Randomizer: This feature is not yet available");
    nn::err::ShowApplicationError(obj);
}

// Replaces every character of `src` with '*'. Used for the IP/Port rows while
// "Show IP and Port" is off - the row keeps its shape (so the player can see a
// value IS set) without putting a server address on stream.
static void maskEndpointText(char* out, u32 outSize, const char* src) {
    u32 i = 0;
    for (; src && src[i] && i + 1 < outSize; i++)
        out[i] = '*';
    out[i] = '\0';
}

// Re-renders the Multiplayer rows in place, without going through
// loadRandoMiscPage().
//
// Deliberately does NOT call addStringData(): that function APPENDS - it parks
// the buffer pointer at its internal counter and then increments it - so
// calling it again per refresh would walk off the end of the list's string
// table. It never needs calling again anyway, because the pointer it stored
// the first time is live: rewriting msgList's buffers in place is what the
// list re-reads on updateParts().
//
// applyRandoMiscPageStyles() is included because the masked and unmasked forms
// of the IP/Port rows differ in length and the row styles are what set
// TxtContent's pane size.
void StageSceneStateModConfig::refreshMultiplayerRows() {
    applyRandoMiscPageStyles();
    updateRandomizerMiscOptions();
    if (mCurrentList) {
        // CommonVerticalList CACHES its rendered strings. updateParts() only
        // re-reads the buffers addStringData() pointed it at when this flag is
        // set, so without it the rows keep whatever text they were built with -
        // which is exactly why toggling Show IP and Port left the masked values
        // on screen. updateEnemySwapInspectorOptions() sets the same flag for
        // its own dynamic rows; it is the only reason that page can relabel
        // itself without a full reload.
        mCurrentList->mStringsDirty = true;
        mCurrentList->updateParts();
    }
    repositionRandomizerMiscPanes();
}

void StageSceneStateModConfig::updateMultiplayerOptions() {
    CommonVerticalList* list = optionsList[MENU_RANDOMIZER_MISC];
    if (!list || !list->mListPartsArr)
        return;

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_SHOW_ENDPOINT].copy(u"Show IP and Port");
    al::startAction(list->mListPartsArr[MP_SHOW_ENDPOINT + 1],
        sMpShowEndpoint ? "On" : "Off", "State");

    // ServerConfig::getIp()/getPort() already report a session override in
    // preference to the .ini value, so these rows show what Connect would
    // actually use without needing to know which of the two it came from.
    char portText[16] = {};
    nn::util::SNPrintf(portText, sizeof(portText), "%d", ServerConfig::getPort());

    if (sMpShowEndpoint) {
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_IP],
            "IP Address: %s", ServerConfig::getIp());
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_PORT], "Port: %s", portText);
    } else {
        char masked[80] = {};
        maskEndpointText(masked, sizeof(masked), ServerConfig::getIp());
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_IP], "IP Address: %s", masked);
        maskEndpointText(masked, sizeof(masked), portText);
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_PORT], "Port: %s", masked);
    }

    if (mMpSaveStatusBuf[0])
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_SAVE_SETTINGS],
            "Save Settings - %s", mMpSaveStatusBuf);
    else if (ServerConfig::hasSessionOverride())
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_SAVE_SETTINGS].copy(
            u"Save Settings (unsaved changes)");
    else
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_SAVE_SETTINGS].copy(u"Save Settings");

    // Never masked - the room name is what a player reads out to whoever is
    // joining them, and is the one field they must set themselves.
    if (sMpRoomName[0])
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_ROOM_NAME],
            "Room Name: %s", sMpRoomName);
    else
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_ROOM_NAME].copy(u"Room Name: (not set)");

    if (mMpStatusBuf[0])
        setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_CONNECT],
            "Connect - %s", mMpStatusBuf);
    else if (!sMpRoomName[0])
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_CONNECT].copy(
            u"Connect (set a Room Name first)");
    else
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[MP_CONNECT].copy(u"Connect");
}

// Starts the Connect row's handshake and returns the same frame.
// Client::connectWithRoomCode() only kicks off the socket thread, so nothing
// here blocks; the wait for the server's room assignment is
// updateMultiplayerConnect()'s job, one frame at a time.
void StageSceneStateModConfig::startMultiplayerConnect() {
#if SMOR_FEATURE_MULTIPLAYER
    mMpConnectPending = false;
    if (!sMpRoomName[0]) {
        nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf), "Set a Room Name first.");
        return;
    }

    nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf), "Connecting...");
    Client::connectWithRoomCode(sMpRoomName);
    if (Client::hasRoomError()) {
        // Rejected before the socket was even touched (an unusable room code).
        nn::util::SNPrintf(
            mMpStatusBuf, sizeof(mMpStatusBuf), "%s", Client::getRoomStatusMessage());
        return;
    }

    mMpConnectPending = true;
    mMpConnectFrames = 0;
#else
    nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf), "Multiplayer not compiled in.");
#endif
}

// Polls the in-flight handshake. Runs every frame while the Connect row is
// waiting, so the game keeps rendering and the row keeps reporting instead of
// the whole thread stalling on the answer.
//
// The timeout matches what the old blocking wait allowed (30s), just spent a
// frame at a time - long enough for a slow server, short enough that a room
// assignment which is never coming eventually says so on the row.
void StageSceneStateModConfig::updateMultiplayerConnect() {
#if SMOR_FEATURE_MULTIPLAYER
    if (!mMpConnectPending)
        return;

    mMpConnectFrames++;

    if (Client::isRoomAssigned()) {
        mMpConnectPending = false;
        nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf), "Connected.");
        // The title's Wait handler picks this up and moves on to the
        // multiplayer file select, matching where the old room-code prompt
        // landed.
        sMultiplayerFileSelectRequest = true;
        exitMultiplayerMenu();
        return;
    }

    // getRoomStatusMessage() carries the server's own reason (bad code, room
    // full, protocol mismatch), which is far more useful on the row than a
    // generic failure string.
    if (Client::hasRoomError()) {
        mMpConnectPending = false;
        nn::util::SNPrintf(
            mMpStatusBuf, sizeof(mMpStatusBuf), "%s", Client::getRoomStatusMessage());
        refreshMultiplayerRows();
        activateInput();
        return;
    }

    if (mMpConnectFrames >= cMpConnectTimeoutFrames) {
        mMpConnectPending = false;
        nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf),
            "No room assignment from the server. Is someone else using this profile?");
        Randomizer::logPush("ModConfig: connect timed out with no room assignment (socket=%d)",
            Client::isSocketActive() ? 1 : 0);
        refreshMultiplayerRows();
        activateInput();
        return;
    }

    // Once a second, so the row shows the wait is alive rather than looking
    // like the frozen state this replaced.
    if ((mMpConnectFrames % 60) == 0) {
        nn::util::SNPrintf(mMpStatusBuf, sizeof(mMpStatusBuf), "Connecting... %ds",
            mMpConnectFrames / 60);
        refreshMultiplayerRows();
    }
#endif
}

// Leaves the Multiplayer page and the whole ModConfig state, dropping back to
// whatever host put us here (the title menu, in the only flow that reaches
// this).
//
// This is kill() minus SaveDataAccessFunction::startSaveDataWrite(): nothing on
// this page is saved data. Both IP/Port overrides are session-only by design
// and "Show IP and Port" is never persisted, so there is nothing to write - and
// the only route onto this page is the TITLE host, where there is no loaded
// save sitting behind mGameDataHolder to write in the first place. Also
// deactivates the list, which kill() leaves to its caller and which
// endSubMenu() does for every other page.
void StageSceneStateModConfig::exitMultiplayerMenu() {
    showRandoMiscInfoBox(false);
    hideKingdomChecksRouteView();
    mCurrentList->deactivate();
    mCurrentMenu->startEnd("End");
    al::NerveStateBase::kill();
}

// One nerve for all three keyboard rows; mMpKeyboardField says which. Modelled
// on exeOpenKeyboardSeed() below, including the
// deactivate -> blocking swkbd -> activate/appearCursor dance.
void StageSceneStateModConfig::exeOpenKeyboardMultiplayer() {
    if (!al::isFirstStep(this))
        return;

    mCurrentList->deactivate();

    const s32 field = mMpKeyboardField;
    char seed[80] = {};

    switch (field) {
    case MP_IP:
        mKeyboard->setHeaderText("Server IP Address");
        mKeyboard->setSubText("IPv4 address or hostname. This session only.");
        nn::util::SNPrintf(seed, sizeof(seed), "%s", ServerConfig::getIp());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = 63;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    case MP_PORT:
        mKeyboard->setHeaderText("Server Port");
        mKeyboard->setSubText("Port number. This session only.");
        nn::util::SNPrintf(seed, sizeof(seed), "%d", ServerConfig::getPort());
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 5;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    default:
        mKeyboard->setHeaderText("Room Name");
        mKeyboard->setSubText("1-16 letters or digits. Share this with whoever is joining.");
        nn::util::SNPrintf(seed, sizeof(seed), "%s", sMpRoomName);
        mKeyboard->openKeyboard(seed, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeASCII;
            config.textMaxLength = 16;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });
        break;
    }

    while (!mKeyboard->isDone())
        nn::os::YieldThread();

    const char* result = mKeyboard->isCancelled() ? nullptr : mKeyboard->getResult();
    if (result && result[0]) {
        switch (field) {
        case MP_IP:
            // Session-only: server.ini is deliberately left alone, so pointing
            // at someone else's server for one night never silently becomes the
            // permanent default. See ServerConfig::setSessionOverride().
            ServerConfig::setSessionOverride(result, 0);
            break;
        case MP_PORT: {
            const int port = atoi(result);
            if (port > 0 && port <= 65535)
                ServerConfig::setSessionOverride(nullptr, port);
            break;
        }
        default:
            // Stored raw here; Client::connectWithRoomCode() is what normalises
            // and validates it (uppercases, rejects anything outside [A-Z0-9]),
            // so the menu cannot disagree with what the server is actually sent.
            nn::util::SNPrintf(sMpRoomName, sizeof(sMpRoomName), "%s", result);
            break;
        }
        // A stale "Connected." / error line no longer describes these settings,
        // and neither does a stale "Saved to server.ini." - what was just typed
        // is unsaved again.
        mMpStatusBuf[0] = '\0';
        mMpSaveStatusBuf[0] = '\0';
    }

    mMpKeyboardField = -1;
    mMpRestoreRow = field;
    al::startHitReaction(mCurrentMenu, "\u30ea\u30bb\u30c3\u30c8", 0);
    mCurrentList->activate();
    mCurrentList->appearCursor();
    al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerMiscSettings);
}

void StageSceneStateModConfig::exeOpenKeyboardSeed() {
    if (al::isFirstStep(this)) {
        mCurrentList->deactivate();

        mKeyboard->setHeaderText("Randomizer Seed");
        mKeyboard->setSubText("Enter a seed number (up to 8 digits)");

        char buf[16];
        const u32 seed = Randomizer::getSeed() > kMaxSeedValue
            ? kMaxSeedValue
            : Randomizer::getSeed();
        nn::util::SNPrintf(buf, sizeof(buf), "%u", seed);

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = kMaxKeyboardInputLength;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled()) {
            u32 newSeed = (u32)atoi(mKeyboard->getResult());
            Randomizer::setSeed(newSeed);
            Randomizer::reinit();
            HakoniwaSequence* seq = (HakoniwaSequence*)GameSystemFunction::getGameSystem()->mSequence;
            stageReload(seq);
        }

        al::startHitReaction(mCurrentMenu, "リセット", 0);
        mCurrentList->activate();
        mCurrentList->appearCursor();
        updateRandomizerOptions();
        al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerSettings);
    }
}

void StageSceneStateModConfig::exeOpenKeyboardMoonReq() {
    if (al::isFirstStep(this)) {
        mCurrentList->deactivate();

        mKeyboard->setHeaderText("Total Moon Requirement");
        mKeyboard->setSubText("Enter a total from 0 to 657");

        char buf[16];
        nn::util::SNPrintf(buf, sizeof(buf), "%u", Randomizer::getMoonRequirement());

        mKeyboard->openKeyboard(buf, [](nn::swkbd::KeyboardConfig& config) {
            config.keyboardMode = nn::swkbd::KeyboardMode::ModeNumeric;
            config.textMaxLength = 3;
            config.textMinLength = 1;
            config.isUseUtf8 = true;
            config.inputFormMode = nn::swkbd::InputFormMode::OneLine;
        });

        while (!mKeyboard->isDone())
            nn::os::YieldThread();

        if (!mKeyboard->isCancelled()) {
            u32 val = (u32)atoi(mKeyboard->getResult());
            Randomizer::setMoonRequirement(val);
            Randomizer::reinit();
        }

        al::startHitReaction(mCurrentMenu, "リセット", 0);
        mCurrentList->activate();
        mCurrentList->appearCursor();
        updateRandomizerOptions();
        al::setNerve(this, &NrvStageSceneStateModConfig.RandomizerSettings);
    }
}

// ============================================================================
// Randomizer Menus
// ============================================================================

void StageSceneStateModConfig::initRandomizerMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_RANDOMIZER] = new SimpleLayoutMenu(
        "RandomizerMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_RANDOMIZER] = new CommonVerticalList(
        menuList[MENU_RANDOMIZER], initInfo, true);

    al::setPaneString(menuList[MENU_RANDOMIZER], "TxtOption", u"Randomization Overview", 0);
    mMenuFooters[MENU_RANDOMIZER] = new FooterParts(menuList[MENU_RANDOMIZER], initInfo, u"", "TxtGuide", "ParFooter");
    mMenuFooters[MENU_RANDOMIZER]->changeText(makeReadOnlyFooterText());
    optionsList[MENU_RANDOMIZER]->initDataNoResetSelected(mRandoMenuOptionsCount);

    for (int i = 0; i < mRandoMenuOptionsCount; i++)
        applyRowStyle(optionsList[MENU_RANDOMIZER]->mListPartsArr[i + 1], RowStyle::Base);

    optionsList[MENU_RANDOMIZER]->startLoopActionAll("Loop", "Loop");

    optionsList[MENU_RANDOMIZER]->addStringData(msgList[MENU_RANDOMIZER]->mBuffer, "TxtContent");
    updateRandomizerOptions();
    repositionRandomizerPanes();
}

void StageSceneStateModConfig::initRandomizerMiscMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_RANDOMIZER_MISC] = new SimpleLayoutMenu(
        "RandomizerMiscMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_RANDOMIZER_MISC] = new CommonVerticalList(
        menuList[MENU_RANDOMIZER_MISC], initInfo, true);

    al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Misc Randomization", 0);
    mMenuFooters[MENU_RANDOMIZER_MISC] = new FooterParts(menuList[MENU_RANDOMIZER_MISC], initInfo, u"", "TxtGuide", "ParFooter");
    mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeRandomizerFooterText());
    optionsList[MENU_RANDOMIZER_MISC]->initDataNoResetSelected(mRandoMiscMenuOptionsCount);

    for (int i = 0; i < mRandoMiscMenuOptionsCount; i++)
        applyRowStyle(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[i + 1], RowStyle::Base);

    sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());
    optionsList[MENU_RANDOMIZER_MISC]->startLoopActionAll("Loop", "Loop");

    RollPartsData* dataSkyboxMode = new RollPartsData(5, new const char16_t* [] { u"None", u"Kingdoms", u"Subareas", u"Own Areas", u"Max" }, static_cast<int>(Randomizer::getSkyboxRandoMode()), false);
    RollPartsData* dataMusicMode = new RollPartsData(4, new const char16_t* [] { u"Off", u"Standard", u"Extended", u"Max" }, musicRandoModeToUiIndex(Randomizer::getMusicRandoMode()), false);
    RollPartsData* dataMusicSource = new RollPartsData(4,
        CustomBgmManager::instance().getMusicSourceLabels(),
        static_cast<int>(Randomizer::getMusicSourceMode()), false);
    RollPartsData* dataTrackerMode = new RollPartsData(5, new const char16_t* [] { u"None", u"Basic", u"Extended", u"Hints", u"Guided" }, static_cast<int>(Randomizer::getTrackerOverlayMode()), false);
    RollPartsData* dataBoardScale = new RollPartsData(3, new const char16_t* [] { u"Small", u"Medium", u"Large" }, static_cast<int>(Randomizer::getBoardScale()), false);
    mBoardScaleRollParts = dataBoardScale;
    RollPartsData* dataCaveSkip = new RollPartsData(3, new const char16_t* [] { u"None", u"Easy", u"Hard" }, static_cast<int>(Randomizer::getTrackerCaveSkipMode()), false);
    RollPartsData* dataMoonLimits = new RollPartsData(3, new const char16_t* [] { u"None", u"Upper", u"Full" }, static_cast<int>(Randomizer::getTrackerMoonLimitMode()), false);
    RollPartsData* dataEmpty = new RollPartsData(0, new const char16_t* [] { u"" });

    s32 rollPartCount = mRandoMiscMenuOptionsCount > mQualityOfLifeMenuOptionsCount
        ? mRandoMiscMenuOptionsCount
        : mQualityOfLifeMenuOptionsCount;
    // Every page indexes this array by row, so it has to cover the longest one.
    if (mQualityOfLife2MenuOptionsCount > rollPartCount)
        rollPartCount = mQualityOfLife2MenuOptionsCount;
    mRandoMiscRollPartCount = rollPartCount;
    // Roll data is shared by the Misc/QoL pages. Most QoL rows use checkbox
    // styling, so these roll entries simply go unused there. Cave Skip, Tracker
    // Overlay, Moon Limits, and Board Size are the QoL rows styled as rolls.
    mRandoMiscRollParts = new RollPartsData[rollPartCount];
    for (int i = 0; i < rollPartCount; i++)
        mRandoMiscRollParts[i] = *dataEmpty;

    mRandoMiscRollParts[RANDO_MISC_SKYBOXES] = *dataSkyboxMode;
    mRandoMiscRollParts[RANDO_MISC_MUSIC] = *dataMusicMode;
    mRandoMiscRollParts[RANDO_MISC_USE_CUSTOM_MUSIC] = *dataMusicSource;
    mRandoMiscRollParts[QOL_TRACKER_OVERLAY] = *dataTrackerMode;
    mRandoMiscRollParts[QOL2_BOARD_SCALE] = *dataBoardScale;
    mRandoMiscRollParts[QOL_CAVE_SKIP] = *dataCaveSkip;
    mRandoMiscRollParts[QOL_MOON_LIMITS] = *dataMoonLimits;
    // Controls page indices (0-2) alias RANDO_MISC_SKYBOXES/STABILIZE_SHADERS/
    // MUSIC's slots on the Misc page - safe since the two pages are never
    // active at once, same reasoning as RANDO_MISC_FORCE_DISABLE_CHAOS's
    // comment above. loadRandoMiscPage(RANDO_MISC_PAGE_MISC) right below
    // populates these three slots with Misc's own data immediately after
    // construction; rebuildControlsRoll() (mirroring
    // rebuildScenarioOverrideRoll()) refreshes them with live button
    // assignments whenever the Controls page is actually loaded instead, so
    // no separate initialization is needed here.
    mDefaultRandoMiscRollParts = new RollPartsData[rollPartCount];
    for (s32 i = 0; i < rollPartCount; i++)
        mDefaultRandoMiscRollParts[i] = mRandoMiscRollParts[i];
    optionsList[MENU_RANDOMIZER_MISC]->setRollPartsData(mRandoMiscRollParts);

    optionsList[MENU_RANDOMIZER_MISC]->addStringData(msgList[MENU_RANDOMIZER_MISC]->mBuffer, "TxtContent");
    loadRandoMiscPage(RANDO_MISC_PAGE_MISC);
}

void StageSceneStateModConfig::showRandoMiscInfoBox(bool visible) {
    mMiscInfoBoxVisible = visible;
    if (!visible)
        mMiscInfoTextBuf[0] = '\0';
    mMiscInfoBoxLastPage = -1;
    mMiscInfoBoxLastIndex = -1;
}

void StageSceneStateModConfig::updateRandoMiscInfoBox() {
    if (!mMiscInfoBoxVisible)
        return;

    const s32 index = optionsList[MENU_RANDOMIZER_MISC] ? optionsList[MENU_RANDOMIZER_MISC]->mCurSelected : -1;
    if (index == mMiscInfoBoxLastIndex && mRandoMiscPage == mMiscInfoBoxLastPage)
        return;
    mMiscInfoBoxLastPage = mRandoMiscPage;
    mMiscInfoBoxLastIndex = index;

    wrapMiscInfoText(randoMiscOptionDescription(mRandoMiscPage, index), mMiscInfoTextBuf, sizeof(mMiscInfoTextBuf), kMiscInfoWrapCols);
}

const char* StageSceneStateModConfig::randoMiscOptionDescription(s32 page, s32 index) const {
    switch (page) {
    case RANDO_MISC_PAGE_QOL:
        switch (index) {
        case QOL_FAST_MOON_DEMOS:
            return "Moons are collected almost instantly.";
        case QOL_MOTION_FREE:
            return "No motion needed for cap throws/captures/rolling - each role uses a button instead. Remap which button on the Controls page (L/R to page there).";
        case QOL_SKIP_CUTSCENE_CAMERAS:
            return "Skip the camera pan that plays when a switch reveals something (faster, less interruption).";
        case QOL_CAVE_SKIP:
            switch (Randomizer::getTrackerCaveSkipMode()) {
            case Randomizer::TrackerCaveSkipMode::Easy:
                return "Easy: show Ground Pound Jump-or-Backflip, Wall Jump, and Dive beside Cave Requirements.";
            case Randomizer::TrackerCaveSkipMode::Hard:
                return "Hard: show only Wall Jump beside Cave Requirements.";
            case Randomizer::TrackerCaveSkipMode::None:
            default:
                return "None: do not show the Cave Skip checklist on the tracker.";
            }
        case QOL_TRACKER_OVERLAY:
            switch (Randomizer::getTrackerOverlayMode()) {
            case Randomizer::TrackerOverlayMode::Basic:
                return "Basic: Only kingdoms with requirements are displayed on screen";
            case Randomizer::TrackerOverlayMode::Extended:
                return "Extended: All kingdoms are displayed on screen";
            case Randomizer::TrackerOverlayMode::Hints:
                return "Hints: Show available checks from that kingdom next to each kingdom";
            case Randomizer::TrackerOverlayMode::Guided:
                return "Guided: Show progress related checks from that kingdom next to each kingdom";
            case Randomizer::TrackerOverlayMode::None:
            default:
                return "None: No tracker is displayed on screen";
            }
        case QOL_MOON_LIMITS:
            switch (Randomizer::getTrackerMoonLimitMode()) {
            case Randomizer::TrackerMoonLimitMode::Upper:
                return "Upper: a kingdom's moon count turns green once it reaches the highest requirement that kingdom could have been given.";
            case Randomizer::TrackerMoonLimitMode::Full:
                return "Full: red below the lowest requirement that kingdom could have been given, yellow inside the range, green at or past the highest.";
            case Randomizer::TrackerMoonLimitMode::None:
            default:
                return "None: kingdom moon counts are never color coded on the tracker.";
            }
        case QOL_COMPLETED_KINGDOM_MOON_COLLECTED:
            return "When a visited kingdom reaches its known moon requirement by collecting or depositing moons, treat its remaining ordinary physical pickups as collected for world gameplay without marking them collected in the moon list. Physical NPC/event-awarded locations, story/multi locations, triple rewards, Toadette Medals, captures, abilities, Moon Rock Keys, paintings and coin rewards remain normal.";
        case QOL_TRACKER_RIGHT_ALIGN:
            return "Anchor the progress tracker to the right side of the screen instead of the left.";
        case QOL_TRACKER_TIMER:
            return "Show the elapsed playtime clock at the top of the progress tracker.";
        default:
            return "";
        }
    case RANDO_MISC_PAGE_QOL_2:
        switch (index) {
        case QOL2_EASY_MINIGAMES:
            return "Jump-rope and volleyball award their two moons after 10 and 25 successful hits.";
        case QOL2_AREA_NAME_OVERLAY:
            return "Show a brief banner with the current area's name when entering an overworld kingdom or a subarea.";
        case QOL2_NO_CLIP:
            return "Allow ZL + ZR + D-Pad Up to toggle No Clip. Disabled by default.";
        case QOL2_SHUFFLE_MUSIC:
            return "With Music Rando enabled, create a new random mapping for every music theme without changing the randomizer seed. Triple press L to temporarily revert the current track to vanilla.";
        case QOL2_FREE_COSTUME_MOON:
            return "Costume-check NPCs give their moon without requiring the matching cap and outfit.";
        case QOL2_ONE_SHEEP_HERDING:
            return "Sheep-herding moons spawn after any one sheep reaches the pen.";
        case QOL2_RANDOMIZE_OUTFITS:
            return "Change to a random outfit every time you die.";
        case QOL2_BOARD_SCALE:
            return "Scale the Bingo and Ascend board HUD. Medium preserves the current layout; Small is 80% and Large is 120%.";
        case QOL2_UNLOCK_HINT_ART:
            return "Treat every Hint Art photo as already viewed, so its dig-point moon can be collected immediately.";
        case QOL2_UNCAPPED_COINS:
            return "Removes the 9999 coin limit, letting your purse run up to 999999. Vanilla clamps the running total every time a coin is added; this lifts that clamp. Spending is the same code path in reverse, so shops, Crazy Cap and every other purchase keep working normally.";
        case QOL2_FAST_DOG:
            return "The dig-spot dog leads you straight to the dig point that actually holds the Power Moon, skipping the coin spots on the way and the fake digs it does around each one. It also moves a little faster and cuts its sniffing pause from two seconds to a third of one.";
        default:
            return "";
        }
    case RANDO_MISC_PAGE_SCENARIO_OVERRIDE:
        switch (index) {
        case SCENARIO_OVERRIDE_CURRENT:
            return "The scenario actually loaded right now for this kingdom (read-only).";
        case SCENARIO_OVERRIDE_VALUE:
            return "Debug tool: force this kingdom to load a specific scenario next time you enter it, or Default to leave it alone.";
        default:
            return "";
        }
    case RANDO_MISC_PAGE_MULTIPLAYER:
        switch (index) {
        case MP_SHOW_ENDPOINT:
            return "Reveal the server address below. Off every time the mod starts, so an address is never on screen unless you ask for it.";
        case MP_IP:
            return "Server address to connect to. Starts from server.ini; anything you type here applies to this session only and does not change the file.";
        case MP_PORT:
            return "Server port to connect to. Starts from server.ini; anything you type here applies to this session only and does not change the file.";
        case MP_SAVE_SETTINGS:
            return "Write the IP and Port above into server.ini so they are used on every future boot. Without this they apply to this session only. Does not save the room name.";
        case MP_ROOM_NAME:
            return "The room to join, 1-16 letters or digits. Everyone playing together must enter the same one. Required before you can connect.";
        case MP_CONNECT:
            return "Connect to the server above and join the room, then continue to multiplayer file select.";
        default:
            return "";
        }
    case RANDO_MISC_PAGE_CONTROLS:
        switch (index) {
        case CONTROLS_UP_THROW:
            return "Motion Free Controls: button for the up-throw role.";
        case CONTROLS_DOWN_THROW_JUMP:
            return "Motion Free Controls: button for the down-throw/jump-suppress/cap-return/keep-held role. Avoid A here - it also suppresses this role's own jump-together check.";
        case CONTROLS_SPIN_SHAKE:
            return "Motion Free Controls: button for the spin-throw/shake role (captures, rolling boost, homing cap attack). Avoid L here - it also suppresses this role's own camera-reset check.";
        default:
            return "";
        }
    default:
        switch (index) {
        case RANDO_MISC_SKYBOXES:
            return "Skybox randomization: None, Kingdoms, Subareas, Own Areas (same-type only), or Max (anything goes).";
        case RANDO_MISC_STABILIZE_SHADERS:
            return "Use original kingdom lighting and shaders when swapping skyboxes. Avoids brightness issues, removes some mood from scene.";
        case RANDO_MISC_MUSIC:
            switch (Randomizer::getMusicRandoMode()) {
            case Randomizer::MusicRandoMode::Max:
                return "Each newly requested source theme picks a fresh random track. Triple press L to temporarily revert the current track to vanilla.";
            case Randomizer::MusicRandoMode::Extended:
                return "Each physical kingdom or subarea gets a stable randomized track for each vanilla music cue. Triple press L to temporarily revert the current track to vanilla.";
            case Randomizer::MusicRandoMode::On:
                return "Randomizes most BGM with one fixed shuffle per seed. Triple press L to temporarily revert the current track to vanilla.";
            case Randomizer::MusicRandoMode::Off:
            default:
                return "Use the game's original background music.";
            }
        case RANDO_MISC_STREAMER_MODE:
            return "Exclude Japanese-language theme variants from the music randomizer's track pool.";
        case RANDO_MISC_MUSIC_OVERLAY:
            return "Overlay randomized music name.";
        case RANDO_MISC_USE_CUSTOM_MUSIC:
            return Randomizer::getMusicSourceModeDescription(Randomizer::getMusicSourceMode());
        case RANDO_MISC_SPHINX:
            return "Randomize Sphinx quiz questions.";
#if SMOR_FEATURE_CHAOS_ANY
        case RANDO_MISC_FORCE_DISABLE_CHAOS:
            return "Disable enemy, capture, NPC and prop randomization. Turn it on if a specific area or scenario crashes on entry, then turn it back off once you are past it.";
#endif
        default:
            return "";
        }
    }
}

s32 StageSceneStateModConfig::randoMiscPageCount() const {
    switch (mRandoMiscPage) {
    case RANDO_MISC_PAGE_QOL:
        return mQualityOfLifeMenuOptionsCount;
    case RANDO_MISC_PAGE_QOL_2:
        return mQualityOfLife2MenuOptionsCount;
    case RANDO_MISC_PAGE_SCENARIO_OVERRIDE:
        return mScenarioOverrideMenuOptionsCount;
    case RANDO_MISC_PAGE_CONTROLS:
        return mControlsMenuOptionsCount;
    case RANDO_MISC_PAGE_MULTIPLAYER:
        return mMultiplayerMenuOptionsCount;
    default:
        return mRandoMiscMenuOptionsCount;
    }
}

s32 StageSceneStateModConfig::randoMiscPageColSize() const {
    switch (mRandoMiscPage) {
    case RANDO_MISC_PAGE_QOL:
        return mQualityOfLifeColSize;
    case RANDO_MISC_PAGE_QOL_2:
        return mQualityOfLife2ColSize;
    case RANDO_MISC_PAGE_SCENARIO_OVERRIDE:
        return mScenarioOverrideColSize;
    case RANDO_MISC_PAGE_CONTROLS:
        return mControlsColSize;
    case RANDO_MISC_PAGE_MULTIPLAYER:
        return mMultiplayerColSize;
    default:
        return mRandoMiscColSize;
    }
}

// Per-row visibility/style for the active page: hide rows past the page's count,
// and style each visible row as a roll (Misc skybox/hint rows, QoL's Cave Skip,
// Tracker Overlay, Moon Limits, and Board Size rows, Scenario Override's Override row), a read-only display
// (Scenario Override's Current row), or a checkbox (everything else).
void StageSceneStateModConfig::applyRandoMiscPageStyles() {
    CommonVerticalList* list = optionsList[MENU_RANDOMIZER_MISC];
    if (!list || !list->mListPartsArr)
        return;

    const s32 count = randoMiscPageCount();
    s32 maxCount = mRandoMiscMenuOptionsCount > mQualityOfLifeMenuOptionsCount
        ? mRandoMiscMenuOptionsCount
        : mQualityOfLifeMenuOptionsCount;
    if (mQualityOfLife2MenuOptionsCount > maxCount)
        maxCount = mQualityOfLife2MenuOptionsCount;
    if (mScenarioOverrideMenuOptionsCount > maxCount)
        maxCount = mScenarioOverrideMenuOptionsCount;
    if (mControlsMenuOptionsCount > maxCount)
        maxCount = mControlsMenuOptionsCount;
    if (mMultiplayerMenuOptionsCount > maxCount)
        maxCount = mMultiplayerMenuOptionsCount;
    for (int i = 0; i < maxCount; i++) {
        al::LayoutActor* item = list->mListPartsArr[i + 1];
        if (!item)
            continue;

        if (i >= count) {
            applyRowStyle(item, RowStyle::Base);
            al::startAction(item, "Hide", nullptr);
            continue;
        }

        al::startAction(item, "Loop", nullptr);

        if (mRandoMiscPage == RANDO_MISC_PAGE_SCENARIO_OVERRIDE) {
            if (i == SCENARIO_OVERRIDE_CURRENT)
                applyRowStyle(item, RowStyle::Base);
            else
                applyRowStyle(item, RowStyle::Roll);
            continue;
        }

        if (mRandoMiscPage == RANDO_MISC_PAGE_CONTROLS) {
            // All three rows are rolls.
            applyRowStyle(item, RowStyle::Roll);
            continue;
        }

        if (mRandoMiscPage == RANDO_MISC_PAGE_MULTIPLAYER) {
            // Only the show/hide toggle is a checkbox; IP/Port/Room Name are
            // keyboard rows and Connect is an action, all of which render their
            // value inside the row text itself.
            if (i == MP_SHOW_ENDPOINT)
                applyRowStyle(item, RowStyle::Check);
            else
                applyRowStyle(item, RowStyle::Base);
            continue;
        }

        if (mRandoMiscPage == RANDO_MISC_PAGE_QOL_2 && i == QOL2_SHUFFLE_MUSIC) {
            applyRowStyle(item, RowStyle::Base);
            continue;
        }

        const bool isRoll = (mRandoMiscPage == RANDO_MISC_PAGE_MISC
                                && (i == RANDO_MISC_SKYBOXES || i == RANDO_MISC_MUSIC
                                    || i == RANDO_MISC_USE_CUSTOM_MUSIC))
            || (mRandoMiscPage == RANDO_MISC_PAGE_QOL
                && (i == QOL_CAVE_SKIP || i == QOL_TRACKER_OVERLAY || i == QOL_MOON_LIMITS))
            || (mRandoMiscPage == RANDO_MISC_PAGE_QOL_2 && i == QOL2_BOARD_SCALE);
        if (isRoll)
            applyRowStyle(item, RowStyle::Roll);
        else
            applyRowStyle(item, RowStyle::Check);
    }
}

// Reconfigure the shared MENU_RANDOMIZER_MISC list for `page`, resetting the
// cursor to the top so paging always lands on the first row.
void StageSceneStateModConfig::loadRandoMiscPage(s32 page) {
    if (page < 0)
        page = 0;
    if (page >= RANDO_MISC_PAGE_COUNT)
        page = RANDO_MISC_PAGE_COUNT - 1;
    mRandoMiscPage = page;

    CommonVerticalList* list = optionsList[MENU_RANDOMIZER_MISC];
    if (!list)
        return;

    if (mRandoMiscRollParts && mDefaultRandoMiscRollParts) {
        for (s32 i = 0; i < mRandoMiscRollPartCount; i++)
            mRandoMiscRollParts[i] = mDefaultRandoMiscRollParts[i];
        // The default array owns the immutable label pointers, but selected
        // indices are live settings.  Rehydrate them after every page swap so
        // leaving and returning to Misc/QoL never visually rolls back a
        // setting changed on the shared list.
        mRandoMiscRollParts[RANDO_MISC_SKYBOXES].mSelectedIdx = static_cast<s32>(Randomizer::getSkyboxRandoMode());
        mRandoMiscRollParts[RANDO_MISC_MUSIC].mSelectedIdx = musicRandoModeToUiIndex(Randomizer::getMusicRandoMode());
        mRandoMiscRollParts[RANDO_MISC_USE_CUSTOM_MUSIC].mSelectedIdx = static_cast<s32>(Randomizer::getMusicSourceMode());
        mRandoMiscRollParts[QOL_TRACKER_OVERLAY].mSelectedIdx = static_cast<s32>(Randomizer::getTrackerOverlayMode());
        mRandoMiscRollParts[QOL_CAVE_SKIP].mSelectedIdx = static_cast<s32>(Randomizer::getTrackerCaveSkipMode());
        if (page == RANDO_MISC_PAGE_QOL) {
            mRandoMiscRollParts[QOL_MOON_LIMITS].mSelectedIdx = static_cast<s32>(Randomizer::getTrackerMoonLimitMode());
        } else if (page == RANDO_MISC_PAGE_QOL_2 && mBoardScaleRollParts) {
            mRandoMiscRollParts[QOL2_BOARD_SCALE] = *mBoardScaleRollParts;
            mRandoMiscRollParts[QOL2_BOARD_SCALE].mSelectedIdx = static_cast<s32>(Randomizer::getBoardScale());
        }
    }

    if (page == RANDO_MISC_PAGE_QOL) {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Quality of Life", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeMainFooterText());
    } else if (page == RANDO_MISC_PAGE_QOL_2) {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Quality of Life II", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeMainFooterText());
    } else if (page == RANDO_MISC_PAGE_SCENARIO_OVERRIDE) {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Scenario Override", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeMainFooterText());
        rebuildScenarioOverrideRoll();
    } else if (page == RANDO_MISC_PAGE_CONTROLS) {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Controls", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeMainFooterText());
        rebuildControlsRoll();
    } else if (page == RANDO_MISC_PAGE_MULTIPLAYER) {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Multiplayer", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeMainFooterText());
    } else {
        al::setPaneString(menuList[MENU_RANDOMIZER_MISC], "TxtOption", u"Misc Randomization", 0);
        if (mMenuFooters[MENU_RANDOMIZER_MISC])
            mMenuFooters[MENU_RANDOMIZER_MISC]->changeText(makeRandomizerFooterText());
    }

    list->initDataNoResetSelected(randoMiscPageCount());
    if (mRandoMiscRollParts)
        list->setRollPartsData(mRandoMiscRollParts);
    list->addStringData(msgList[MENU_RANDOMIZER_MISC]->mBuffer, "TxtContent");
    list->startLoopActionAll("Loop", "Loop");
    list->setSelectedIdx(0, 0);
    applyRandoMiscPageStyles();
    updateRandomizerMiscOptions();
    list->updateParts();
    repositionRandomizerMiscPanes();
}

void StageSceneStateModConfig::rebuildScenarioOverrideRoll() {
    GameDataHolderAccessor accessor(mGameDataHolder);
    const s32 worldId = accessor.mData ? GameDataFunction::getCurrentWorldId(accessor) : -1;
    mScenarioOverrideWorldId = worldId;

    s32 optionCount = 0;

    mScenarioOverrideRollLabels[optionCount].copy(u"Default");
    mScenarioOverrideRollLabelPtrs[optionCount] = mScenarioOverrideRollLabels[optionCount].cstr();
    mScenarioOverrideRollScenarioNos[optionCount] = 0;
    optionCount++;

    const s32 namedCount = Randomizer::ScenarioNames::getCount(worldId);
    for (s32 i = 0; i < namedCount && optionCount < mScenarioOverrideRollOptionsMax; i++) {
        s32 scenarioNo = 0;
        const char* name = nullptr;
        if (!Randomizer::ScenarioNames::getByIndex(worldId, i, &scenarioNo, &name))
            break;

        char label[64];
        nn::util::SNPrintf(label, sizeof(label), "%s (%d)", name, scenarioNo);
        mScenarioOverrideRollLabels[optionCount].convertFromMultiByteString(label, strlen(label));
        mScenarioOverrideRollLabelPtrs[optionCount] = mScenarioOverrideRollLabels[optionCount].cstr();
        mScenarioOverrideRollScenarioNos[optionCount] = scenarioNo;
        optionCount++;
    }

    // Preselect whatever override is already active for this kingdom, if any.
    s32 selectedIdx = 0;
    if (Randomizer::getScenarioOverrideWorldId() == worldId) {
        const s32 activeScenario = Randomizer::getScenarioOverrideValue();
        for (s32 i = 0; i < optionCount; i++) {
            if (mScenarioOverrideRollScenarioNos[i] == activeScenario) {
                selectedIdx = i;
                break;
            }
        }
    }

    mRandoMiscRollParts[SCENARIO_OVERRIDE_VALUE] = RollPartsData(optionCount, mScenarioOverrideRollLabelPtrs, selectedIdx, false);
}

// The button-name list is fixed (unlike Scenario Override's per-kingdom
// labels), so this only needs to (re)read each role's current button -
// called every time the Controls page loads, same trigger as
// rebuildScenarioOverrideRoll().
void StageSceneStateModConfig::rebuildControlsRoll() {
    for (int i = 0; i < Randomizer::cControllerButtonCount; i++)
        mControllerButtonRollLabelPtrs[i] = Randomizer::getControllerButtonName(static_cast<Randomizer::ControllerButton>(i));

    mRandoMiscRollParts[CONTROLS_UP_THROW] = RollPartsData(Randomizer::cControllerButtonCount,
        mControllerButtonRollLabelPtrs, static_cast<int>(Randomizer::getUpThrowButton()), false);
    mRandoMiscRollParts[CONTROLS_DOWN_THROW_JUMP] = RollPartsData(Randomizer::cControllerButtonCount,
        mControllerButtonRollLabelPtrs, static_cast<int>(Randomizer::getDownThrowJumpButton()), false);
    mRandoMiscRollParts[CONTROLS_SPIN_SHAKE] = RollPartsData(Randomizer::cControllerButtonCount,
        mControllerButtonRollLabelPtrs, static_cast<int>(Randomizer::getSpinShakeButton()), false);
}

void StageSceneStateModConfig::updateRandomizerOptions() {
    for (int i = 0; i < maxMsgCount; i++)
        msgList[MENU_RANDOMIZER]->mBuffer[i].copy(u"");

    auto setBoolText = [&](int row, const char* label, bool enabled) {
        setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[row],
            "%s: %s", label, enabled ? "On" : "Off");
    };

    // Kept compact (no "Current"/pipe separator) - this row already runs
    // close to the pane's width limit with just the seed, and a long room
    // code appended pushes it into the adjacent column (see
    // TrackerBridge::kRoomCodeMaxLength for the other half of this fix).
    if (TrackerBridge::getRoomCode()[0])
        setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_SEED],
            "Seed: %u  Room: %s", Randomizer::getSeed(),
            TrackerBridge::getRoomCodeDisplayText());
    else
        setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_SEED],
            "Current Seed: %u", Randomizer::getSeed());
    setBoolText(RANDO_MOONS, "Moons", Randomizer::isMoonRandoEnabled());
    // Shops only get their own segment when the setting is actually doing
    // something; Randomized is what the shuffle already reports above.
    const bool shopsMoved = Randomizer::isEntranceRandoEnabled()
        && Randomizer::getShopLocationMode() != Randomizer::ShopLocationMode_Randomized;
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_LOADING_ZONES],
        "Loading Zones: %s%s%s", Randomizer::getCurrentEntranceRandoModeName(),
        shopsMoved ? " | Shops: " : "",
        shopsMoved ? Randomizer::getCurrentShopLocationModeName() : "");
    setBoolText(RANDO_PAINTINGS, "Paintings", Randomizer::isPaintingRandoEnabled());
    setBoolText(RANDO_KINGDOM_ORDER, "Kingdom Order", Randomizer::isKingdomRandoEnabled());
    setBoolText(RANDO_START_KINGDOM_STATE, "Start Kingdom State",
        Randomizer::isStartKingdomStateRandoEnabled());
    setBoolText(RANDO_COIN_RANDO, "Coin Shuffle",
        Randomizer::isCoinRandoEnabled());
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_LOGIC],
        "Placement Logic: %s%s%s", Randomizer::getCurrentLogicModeLabel(),
        Randomizer::isLogicRandoEnabled() ? " / " : "",
        Randomizer::isLogicRandoEnabled()
            ? Randomizer::getSeedDifficultyName(Randomizer::getResolvedSeedDifficulty())
            : "");
    setBoolText(RANDO_LOCKED_CAPTURES, "Locked Captures", Randomizer::isCaptureRandoEnabled());
    setBoolText(RANDO_LOCKED_ABILITIES, "Locked Abilities", Randomizer::isLockedAbilitiesEnabled());

    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_STARTING_EQUIPMENT],
        "Starting Equipment: %s",
        Randomizer::getStartingEquipmentName(Randomizer::getStartingEquipment()));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_CAPTURE_DIFFICULTY],
        "Item Pool: %s",
        Randomizer::getCaptureAbilityDifficultyName(Randomizer::getCaptureAbilityDifficulty()));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_PURPLE_SHOPS],
        "Purple Shops: A%d C%d M%s %s",
        Randomizer::getPurpleShopAbilityCount(),
        Randomizer::getPurpleShopCaptureCount(),
        Randomizer::arePurpleShopMoonsEnabled() ? "On" : "Off",
        Randomizer::getPurpleShopPriceName(Randomizer::getPurpleShopPrice()));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_MOON_REQ],
        "Moon Requirement: %s", Randomizer::getCurrentMoonRequirementLabel());
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_MOON_DIFFICULTY],
        "Req Difficulty: %s",
        Randomizer::getMoonDifficultyName(Randomizer::getMoonDifficulty()));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_CAP_MOON_USE],
        "Cap Moons: %s", Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Cap));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_CLOUD_MOON_USE],
        "Cloud Moons: %s", Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Cloud));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_MOON_MOON_USE],
        "Moon Moons: %s", Randomizer::getKingdomMoonUseName(WorldMeta::WorldId_Moon));
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_WORLD_PEACE_REQ],
        "World Peace Req: %s", Randomizer::getCurrentWorldPeaceRequirementModeName());
    setBoolText(RANDO_MOON_ROCK_KEY, "Moon Rock Key", Randomizer::isMoonRockKeyEnabled());
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_WIN_CONDITION],
        "Win Condition: %s", Randomizer::getBowserWinConditionName(Randomizer::getBowserWinCondition()));
    if (Randomizer::isToadetteMedalModeEnabled()) {
        const Randomizer::ToadetteMedalRatio ratio = Randomizer::getToadetteMedalRatio();
        setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_TOADETTE_MEDAL_RATIO],
            "Toadette Medals: %d / %d", ratio.required, ratio.total);
    }
    char moonAllocationMode[32] = {};
    if (Randomizer::isMoonAllocationActive()) {
        nn::util::SNPrintf(moonAllocationMode, sizeof(moonAllocationMode), "%s/%s",
            Randomizer::getMoonAllocationDifficultyName(
                Randomizer::getMoonAllocationDifficulty()),
            Randomizer::getMoonCoinPolicyName(Randomizer::getMoonCoinPolicy()));
    } else {
        nn::util::SNPrintf(moonAllocationMode, sizeof(moonAllocationMode), "Off");
    }

    const char* tripleMoonsMode = "Vanilla";
    if (Randomizer::isTripleMoonsAnywhereEnabled()) {
        tripleMoonsMode = Randomizer::isRandomizeTripleMoonsEnabled()
            ? "Anywhere + Randomized"
            : "Anywhere";
    } else if (Randomizer::isRandomizeTripleMoonsEnabled()) {
        tripleMoonsMode = "Randomized";
    }
    setMenuRowText(msgList[MENU_RANDOMIZER]->mBuffer[RANDO_TRIPLE_MOONS],
        "Moon Alloc: %s | Triple: %s", moonAllocationMode, tripleMoonsMode);
}

void StageSceneStateModConfig::updateRandomizerMiscOptions() {
    for (int i = 0; i < maxMsgCount; i++)
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[i].copy(u"");

    CommonVerticalList* list = optionsList[MENU_RANDOMIZER_MISC];

    if (mRandoMiscPage == RANDO_MISC_PAGE_QOL) {
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_FAST_MOON_DEMOS].copy(u"Fast Moon Demos");
        al::startAction(list->mListPartsArr[QOL_FAST_MOON_DEMOS + 1],
            Randomizer::isFastMoonDemosEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_MOTION_FREE].copy(u"Motion-Free Controls");
        al::startAction(list->mListPartsArr[QOL_MOTION_FREE + 1],
            Randomizer::isMotionFreeControlsEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_SKIP_CUTSCENE_CAMERAS].copy(u"Skip Cutscene Cameras");
        al::startAction(list->mListPartsArr[QOL_SKIP_CUTSCENE_CAMERAS + 1],
            Randomizer::isSkipCutsceneCamerasEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_TRACKER_TIMER].copy(u"Tracker Timer");
        al::startAction(list->mListPartsArr[QOL_TRACKER_TIMER + 1],
            Randomizer::isTrackerTimerEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_TRACKER_RIGHT_ALIGN].copy(u"Tracker on Right");
        al::startAction(list->mListPartsArr[QOL_TRACKER_RIGHT_ALIGN + 1],
            Randomizer::isTrackerRightAligned() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_CAVE_SKIP].copy(u"Cave Skip:");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_TRACKER_OVERLAY].copy(u"Tracker Overlay:");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_MOON_LIMITS].copy(u"Moon Limits:");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL_COMPLETED_KINGDOM_MOON_COLLECTED].copy(
            u"Treat completed kingdom moons as collected");
        al::startAction(list->mListPartsArr[QOL_COMPLETED_KINGDOM_MOON_COLLECTED + 1],
            Randomizer::isCompletedKingdomMoonCollectedEnabled() ? "On" : "Off", "State");

        return;
    }

    if (mRandoMiscPage == RANDO_MISC_PAGE_QOL_2) {
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_EASY_MINIGAMES].copy(u"Easy Minigames");
        al::startAction(list->mListPartsArr[QOL2_EASY_MINIGAMES + 1],
            Randomizer::isEasyMinigamesEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_AREA_NAME_OVERLAY].copy(u"Area Name Overlay");
        al::startAction(list->mListPartsArr[QOL2_AREA_NAME_OVERLAY + 1],
            Randomizer::isAreaNameOverlayEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_NO_CLIP].copy(u"Enable No Clip");
        al::startAction(list->mListPartsArr[QOL2_NO_CLIP + 1],
            Randomizer::isNoClipEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_SHUFFLE_MUSIC].copy(u"Shuffle Music");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_FREE_COSTUME_MOON].copy(u"Free Costume Moon");
        al::startAction(list->mListPartsArr[QOL2_FREE_COSTUME_MOON + 1],
            Randomizer::isFreeCostumeMoonEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_ONE_SHEEP_HERDING].copy(u"One-Sheep Herding");
        al::startAction(list->mListPartsArr[QOL2_ONE_SHEEP_HERDING + 1],
            Randomizer::isOneSheepHerdingEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_RANDOMIZE_OUTFITS].copy(u"Randomize Outfits");
        al::startAction(list->mListPartsArr[QOL2_RANDOMIZE_OUTFITS + 1],
            Randomizer::isRandomizeOutfitsEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_BOARD_SCALE].copy(u"Board Size:");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_UNLOCK_HINT_ART].copy(u"Unlocked Hint Art");
        al::startAction(list->mListPartsArr[QOL2_UNLOCK_HINT_ART + 1],
            Randomizer::isUnlockHintArtEnabled() ? "On" : "Off", "State");

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_UNCAPPED_COINS].copy(u"Uncapped Coins");
        al::startAction(list->mListPartsArr[QOL2_UNCAPPED_COINS + 1],
            Randomizer::isUncappedCoinsEnabled() ? "On" : "Off", "State");
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[QOL2_FAST_DOG].copy(u"Fast Doggo");
        al::startAction(list->mListPartsArr[QOL2_FAST_DOG + 1],
            Randomizer::isFastDogEnabled() ? "On" : "Off", "State");
        return;
    }

    if (mRandoMiscPage == RANDO_MISC_PAGE_SCENARIO_OVERRIDE) {
        GameDataHolderAccessor accessor(mGameDataHolder);
        GameDataFile* file = accessor.mData ? accessor.mData->getGameDataFile() : nullptr;
        const s32 worldId = accessor.mData ? GameDataFunction::getCurrentWorldId(accessor) : -1;
        const s32 currentScenario = file ? file->getScenarioNo() : -1;
        const char* currentName = Randomizer::ScenarioNames::find(worldId, currentScenario);

        if (currentName)
            setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[SCENARIO_OVERRIDE_CURRENT],
                "Current Scenario: %s (%d)", currentName, currentScenario);
        else
            setMenuRowText(msgList[MENU_RANDOMIZER_MISC]->mBuffer[SCENARIO_OVERRIDE_CURRENT],
                "Current Scenario: (%d)", currentScenario);

        msgList[MENU_RANDOMIZER_MISC]->mBuffer[SCENARIO_OVERRIDE_VALUE].copy(u"Override Scenario:");
        return;
    }

    if (mRandoMiscPage == RANDO_MISC_PAGE_CONTROLS) {
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[CONTROLS_UP_THROW].copy(u"Up Throw:");
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[CONTROLS_DOWN_THROW_JUMP].copy(u"Down Throw/Jump:");
        msgList[MENU_RANDOMIZER_MISC]->mBuffer[CONTROLS_SPIN_SHAKE].copy(u"Spin/Shake:");
        return;
    }

    if (mRandoMiscPage == RANDO_MISC_PAGE_MULTIPLAYER) {
        updateMultiplayerOptions();
        return;
    }

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_SKYBOXES].copy(u"Skyboxes:");

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_STABILIZE_SHADERS].copy(u"Stabilize Shaders");
    al::startAction(list->mListPartsArr[RANDO_MISC_STABILIZE_SHADERS + 1],
        Randomizer::isStabilizeShadersEnabled() ? "On" : "Off", "State");

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_MUSIC].copy(u"Music Rando:");

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_STREAMER_MODE].copy(u"Streamer Mode");
    al::startAction(list->mListPartsArr[RANDO_MISC_STREAMER_MODE + 1],
        Randomizer::isStreamerModeEnabled() ? "On" : "Off", "State");

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_MUSIC_OVERLAY].copy(u"Music Overlay");
    al::startAction(list->mListPartsArr[RANDO_MISC_MUSIC_OVERLAY + 1],
        Randomizer::isMusicOverlayEnabled() ? "On" : "Off", "State");

    // A roll, like Music Rando above it: the row label is the fixed prefix and
    // the roll itself carries Off / Vanilla / N Custom Songs / Vanilla + N.
    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_USE_CUSTOM_MUSIC].copy(u"Music Source:");

    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_SPHINX].copy(u"Randomize Sphinx Questions");
    al::startAction(list->mListPartsArr[RANDO_MISC_SPHINX + 1],
        Randomizer::isSphinxRandoEnabled() ? "On" : "Off", "State");

#if SMOR_FEATURE_CHAOS_ANY
    msgList[MENU_RANDOMIZER_MISC]->mBuffer[RANDO_MISC_FORCE_DISABLE_CHAOS].copy(
        u"Force Disable Chaos Options");
    al::startAction(list->mListPartsArr[RANDO_MISC_FORCE_DISABLE_CHAOS + 1],
        Randomizer::isChaosForceDisabled() ? "On" : "Off", "State");
#endif
}

void StageSceneStateModConfig::repositionRandomizerPanes() {
    repositionTwoColumnPanes(optionsList[MENU_RANDOMIZER], mRandoMenuOptionsCount, mRandoColSize);
}

void StageSceneStateModConfig::repositionRandomizerMiscPanes() {
    repositionTwoColumnPanes(optionsList[MENU_RANDOMIZER_MISC], randoMiscPageCount(), randoMiscPageColSize());
}

void StageSceneStateModConfig::exeRandomizerSettings() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mActiveColumn = 0;
        mCurrentList = optionsList[MENU_RANDOMIZER];
        mCurrentMenu = menuList[MENU_RANDOMIZER];
        updateRandomizerOptions();
        repositionRandomizerPanes();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    mInput->update();
    mCurrentList->update();

    if (!mIsDecideConfig) {
        handleTwoColumnNavigationInput(
            mInput, mCurrentList, mRandoMenuOptionsCount, mRandoColSize, mActiveColumn);

        if (rs::isTriggerUiCancel(mHost))
            endSubMenu();
    }

    updateRandomizerOptions();
    repositionRandomizerPanes();
}

void StageSceneStateModConfig::exeRandomizerMiscSettings() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mActiveColumn = 0;
        mCurrentList = optionsList[MENU_RANDOMIZER_MISC];
        mCurrentMenu = menuList[MENU_RANDOMIZER_MISC];
        // mRandoMiscPage was set by the main-menu dispatch (Misc vs Quality of Life).
        loadRandoMiscPage(mRandoMiscPage);
        // loadRandoMiscPage() always resets the cursor to row 0, but returning
        // from one of the Multiplayer keyboards re-enters this nerve - without
        // this, editing Port would dump the cursor back on row 0 every time.
        if (mMpRestoreRow >= 0) {
            if (mMpRestoreRow < randoMiscPageCount())
                mCurrentList->setSelectedIdx(mMpRestoreRow, 0);
            mMpRestoreRow = -1;
        }
        mCurrentMenu->startAppear("Appear");
        activateInput();
        showRandoMiscInfoBox(true);
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    // Drives an in-flight Connect. Deliberately ahead of the input handling
    // below: while the handshake is pending the row is mid-connect, and letting
    // A through would start a second one on top of it.
    if (mMpConnectPending) {
        updateMultiplayerConnect();
        return;
    }

    mInput->update();
    mCurrentList->update();
    updateRandoMiscInfoBox();

    const bool isQolPage = mRandoMiscPage == RANDO_MISC_PAGE_QOL
        || mRandoMiscPage == RANDO_MISC_PAGE_QOL_2;
    const bool isFirstQolPage = mRandoMiscPage == RANDO_MISC_PAGE_QOL;
    const bool isScenarioOverridePage = mRandoMiscPage == RANDO_MISC_PAGE_SCENARIO_OVERRIDE;
    const bool isControlsPage = mRandoMiscPage == RANDO_MISC_PAGE_CONTROLS;

    if (!mIsDecideConfig) {
        // L / R shoulder buttons page through Misc Randomization, the two Quality
        // of Life pages, and Scenario Override on the shared list; loadRandoMiscPage
        // resets the cursor to the top.
        // Entered from the title's Multiplayer row, L/R must not page off into
        // the randomizer settings - the player asked for Multiplayer, and on the
        // title those other pages are a debug surface with no live save behind
        // them.
        if (!mMpStandalonePage) {
            if (al::isPadTriggerL() && mRandoMiscPage > 0) {
                loadRandoMiscPage(mRandoMiscPage - 1);
                mActiveColumn = 0;
                activateInput();
                return;
            }
            if (al::isPadTriggerR() && mRandoMiscPage < RANDO_MISC_PAGE_COUNT - 1) {
                loadRandoMiscPage(mRandoMiscPage + 1);
                mActiveColumn = 0;
                activateInput();
                return;
            }
        }

        handleTwoColumnNavigationInput(
            mInput, mCurrentList, randoMiscPageCount(), randoMiscPageColSize(), mActiveColumn);

        // Rolls exist on the Misc page (skybox/music), the QoL page
        // (Cave Skip, Tracker Overlay, and Moon Limits rows), the QoL II page
        // (Board Size row), and the Scenario Override page (Override Scenario row).
        const bool onRollRow = (mRandoMiscPage == RANDO_MISC_PAGE_MISC
                                   && (mCurrentList->mCurSelected == RANDO_MISC_SKYBOXES
                                       || mCurrentList->mCurSelected == RANDO_MISC_MUSIC))
            || (isFirstQolPage && (mCurrentList->mCurSelected == QOL_CAVE_SKIP || mCurrentList->mCurSelected == QOL_TRACKER_OVERLAY || mCurrentList->mCurSelected == QOL_MOON_LIMITS))
            || (!isFirstQolPage && mRandoMiscPage == RANDO_MISC_PAGE_QOL_2
                && mCurrentList->mCurSelected == QOL2_BOARD_SCALE)
            || (isScenarioOverridePage && mCurrentList->mCurSelected == SCENARIO_OVERRIDE_VALUE)
            || isControlsPage;
        if (onRollRow) {
            if (al::isPadTriggerLeftRightStick() || al::isPadRepeatLeftRightStick())
                mCurrentList->rollLeft();
            if (al::isPadTriggerRightRightStick() || al::isPadRepeatRightRightStick())
                mCurrentList->rollRight();
        }

        if (rs::isTriggerUiDecide(mHost))
            deactivateInput();

        if (rs::isTriggerUiCancel(mHost)) {
            // On the standalone Multiplayer page, B leaves ModConfig entirely
            // and drops back to the title menu. endSubMenu() would instead
            // strand the player in the ModConfig main menu, which they never
            // asked for and which is mostly empty on the title anyway
            // (sSuppressLiveData).
            if (mMpStandalonePage)
                exitMultiplayerMenu();
            else
                endSubMenu();
        }
    }

    if (mRandoMiscPage == RANDO_MISC_PAGE_MISC) {
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[RANDO_MISC_SKYBOXES + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[RANDO_MISC_MUSIC + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
    } else if (isFirstQolPage) {
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[QOL_CAVE_SKIP + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[QOL_TRACKER_OVERLAY + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[QOL_MOON_LIMITS + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
    } else if (mRandoMiscPage == RANDO_MISC_PAGE_QOL_2) {
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[QOL2_BOARD_SCALE + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
    } else if (isScenarioOverridePage) {
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[SCENARIO_OVERRIDE_VALUE + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
    } else if (isControlsPage) {
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[CONTROLS_UP_THROW + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[CONTROLS_DOWN_THROW_JUMP + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
        al::setPaneLocalTrans(optionsList[MENU_RANDOMIZER_MISC]->mListPartsArr[CONTROLS_SPIN_SHAKE + 1], "Roll", { StageSceneStateModConfig::kRollPaneX, 0.0f, 0.0f });
    }
    repositionRandomizerMiscPanes();

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        if (mRandoMiscPage == RANDO_MISC_PAGE_MULTIPLAYER) {
            switch (mCurrentList->mCurSelected) {
            case MP_SHOW_ENDPOINT:
                sMpShowEndpoint = !sMpShowEndpoint;
                // Session-only by design, so no GlobalConfig::save() here -
                // this must be back off on the next boot.
                //
                // Re-render right now rather than waiting for the shared tail
                // below: this row's whole job is changing what the two rows
                // under it read, so the address has to appear on the same frame
                // the toggle flips.
                refreshMultiplayerRows();
                break;
            case MP_IP:
            case MP_PORT:
            case MP_ROOM_NAME:
                mMpKeyboardField = mCurrentList->mCurSelected;
                al::setNerve(this, &NrvStageSceneStateModConfig.OpenKeyboardMultiplayer);
                return;
            case MP_SAVE_SETTINGS:
                // The one place the typed address is allowed to reach disk.
                // Reported either way rather than assumed: this write silently
                // failed for real when sd:/smo did not exist.
                if (ServerConfig::commitSessionOverride())
                    nn::util::SNPrintf(mMpSaveStatusBuf, sizeof(mMpSaveStatusBuf),
                        "Saved to server.ini.");
                else
                    nn::util::SNPrintf(mMpSaveStatusBuf, sizeof(mMpSaveStatusBuf),
                        "Could not write server.ini!");
                break;
            case MP_CONNECT:
                // Kicks the connection off and returns immediately; the
                // handshake is then polled a frame at a time by
                // updateMultiplayerConnect() at the top of this nerve. It used
                // to be one blocking call that slept up to 30 SECONDS on this
                // thread waiting for the server's room assignment - and since
                // this is the main thread, a server that accepted the socket
                // but never assigned a room presented as the game hanging at
                // 0 fps on the Connect row, with the actual reason only
                // appearing half a minute later if you waited it out.
                startMultiplayerConnect();
                break;
            default:
                break;
            }
            refreshMultiplayerRows();
            activateInput();
            return;
        }

        if (isQolPage) {
            if (isFirstQolPage) {
                switch (mCurrentList->mCurSelected) {
                case QOL_FAST_MOON_DEMOS:
                    // setFastMoonDemosEnabled keeps the cutscene patches in sync.
                    Randomizer::setFastMoonDemosEnabled(!Randomizer::isFastMoonDemosEnabled());
                    break;
                case QOL_MOTION_FREE:
                    Randomizer::setMotionFreeControlsEnabled(!Randomizer::isMotionFreeControlsEnabled());
                    break;
                case QOL_SKIP_CUTSCENE_CAMERAS:
                    Randomizer::setSkipCutsceneCamerasEnabled(!Randomizer::isSkipCutsceneCamerasEnabled());
                    break;
                case QOL_CAVE_SKIP: {
                    const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                         ->mListPartsArr[QOL_CAVE_SKIP + 1])
                                        ->mSelectedIdx;
                    Randomizer::setTrackerCaveSkipMode(static_cast<Randomizer::TrackerCaveSkipMode>(idx));
                    break;
                }
                case QOL_TRACKER_OVERLAY: {
                    const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                         ->mListPartsArr[QOL_TRACKER_OVERLAY + 1])
                                        ->mSelectedIdx;
                    Randomizer::setTrackerOverlayMode(static_cast<Randomizer::TrackerOverlayMode>(idx));
                    break;
                }
                case QOL_MOON_LIMITS: {
                    const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                         ->mListPartsArr[QOL_MOON_LIMITS + 1])
                                        ->mSelectedIdx;
                    Randomizer::setTrackerMoonLimitMode(static_cast<Randomizer::TrackerMoonLimitMode>(idx));
                    break;
                }
                case QOL_TRACKER_RIGHT_ALIGN:
                    Randomizer::setTrackerRightAligned(!Randomizer::isTrackerRightAligned());
                    break;
                case QOL_COMPLETED_KINGDOM_MOON_COLLECTED:
                    Randomizer::setCompletedKingdomMoonCollectedEnabled(
                        !Randomizer::isCompletedKingdomMoonCollectedEnabled());
                    break;
                case QOL_TRACKER_TIMER:
                    Randomizer::setTrackerTimerEnabled(!Randomizer::isTrackerTimerEnabled());
                    break;
                default:
                    break;
                }
            } else {
                switch (mCurrentList->mCurSelected) {
                case QOL2_EASY_MINIGAMES:
                    Randomizer::setEasyMinigamesEnabled(!Randomizer::isEasyMinigamesEnabled());
                    break;
                case QOL2_AREA_NAME_OVERLAY:
                    Randomizer::setAreaNameOverlayEnabled(!Randomizer::isAreaNameOverlayEnabled());
                    break;
                case QOL2_NO_CLIP:
                    Randomizer::setNoClipEnabled(!Randomizer::isNoClipEnabled());
                    break;
                case QOL2_SHUFFLE_MUSIC:
                    if (MusicRando::reshuffle()) {
                        HakoniwaSequence* seq = (HakoniwaSequence*)GameSystemFunction::getGameSystem()->mSequence;
                        stageReload(seq);
                    }
                    break;
                case QOL2_FREE_COSTUME_MOON:
                    Randomizer::setFreeCostumeMoonEnabled(!Randomizer::isFreeCostumeMoonEnabled());
                    break;
                case QOL2_ONE_SHEEP_HERDING:
                    Randomizer::setOneSheepHerdingEnabled(!Randomizer::isOneSheepHerdingEnabled());
                    break;
                case QOL2_RANDOMIZE_OUTFITS:
                    Randomizer::setRandomizeOutfitsEnabled(!Randomizer::isRandomizeOutfitsEnabled());
                    GlobalConfig::save();
                    break;
                case QOL2_BOARD_SCALE: {
                    const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                         ->mListPartsArr[QOL2_BOARD_SCALE + 1])
                                        ->mSelectedIdx;
                    Randomizer::setBoardScale(static_cast<Randomizer::BoardScale>(idx));
                    break;
                }
                case QOL2_UNLOCK_HINT_ART:
                    Randomizer::setUnlockHintArtEnabled(!Randomizer::isUnlockHintArtEnabled());
                    break;
                case QOL2_UNCAPPED_COINS:
                    Randomizer::setUncappedCoinsEnabled(!Randomizer::isUncappedCoinsEnabled());
                    break;
                case QOL2_FAST_DOG:
                    Randomizer::setFastDogEnabled(!Randomizer::isFastDogEnabled());
                    break;
                default:
                    break;
                }
            }
            GlobalConfig::save();
            updateRandomizerMiscOptions();
            activateInput();
            return;
        }

        if (isScenarioOverridePage) {
            // Only the roll row changes anything; Decide on the read-only Current
            // Scenario row is a no-op, same as the info rows on other pages.
            if (mCurrentList->mCurSelected == SCENARIO_OVERRIDE_VALUE) {
                updateDataFromRollParts();
                HakoniwaSequence* seq = (HakoniwaSequence*)GameSystemFunction::getGameSystem()->mSequence;
                stageReload(seq);
            }
            updateRandomizerMiscOptions();
            activateInput();
            return;
        }

        if (isControlsPage) {
            const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                 ->mListPartsArr[mCurrentList->mCurSelected + 1])
                                ->mSelectedIdx;
            const auto button = static_cast<Randomizer::ControllerButton>(idx);
            switch (mCurrentList->mCurSelected) {
            case CONTROLS_UP_THROW:
                Randomizer::setUpThrowButton(button);
                break;
            case CONTROLS_DOWN_THROW_JUMP:
                Randomizer::setDownThrowJumpButton(button);
                break;
            case CONTROLS_SPIN_SHAKE:
                Randomizer::setSpinShakeButton(button);
                break;
            default:
                break;
            }
            GlobalConfig::save();
            updateRandomizerMiscOptions();
            activateInput();
            return;
        }

        HakoniwaSequence* seq = (HakoniwaSequence*)GameSystemFunction::getGameSystem()->mSequence;
        updateDataFromRollParts();

        switch (mCurrentList->mCurSelected) {
        case RANDO_MISC_SKYBOXES:
            // Mode already applied from the roll in updateDataFromRollParts().
            Randomizer::reinit();
            stageReload(seq);
            break;

        case RANDO_MISC_STABILIZE_SHADERS:
            Randomizer::setStabilizeShadersEnabled(!Randomizer::isStabilizeShadersEnabled());
            // Reloads the stage so the new lighting/sky path is applied immediately.
            stageReload(seq);
            break;

        case RANDO_MISC_MUSIC:
            // Mode already applied from the roll in updateDataFromRollParts().
            Randomizer::reinit();
            stageReload(seq);
            break;

        case RANDO_MISC_STREAMER_MODE:
            Randomizer::setStreamerModeEnabled(!Randomizer::isStreamerModeEnabled());
            Randomizer::reinit();
            stageReload(seq);
            break;

        case RANDO_MISC_MUSIC_OVERLAY:
            Randomizer::setMusicOverlayEnabled(!Randomizer::isMusicOverlayEnabled());
            stageReload(seq);
            break;

        case RANDO_MISC_USE_CUSTOM_MUSIC:
            // Source already applied from the roll in updateDataFromRollParts().
            stageReload(seq);
            break;

        case RANDO_MISC_SPHINX:
            Randomizer::setSphinxRandoEnabled(!Randomizer::isSphinxRandoEnabled());
            Randomizer::reinit();
            stageReload(seq);
            break;

#if SMOR_FEATURE_CHAOS_ANY
        case RANDO_MISC_FORCE_DISABLE_CHAOS:
            Randomizer::setChaosForceDisabled(!Randomizer::isChaosForceDisabled());
            GlobalConfig::save();
            // No reinit: the flag is read where a placement asks for its
            // replacement, so the seed's assignment is untouched and only the
            // actors in the stage the player is standing in have to be rebuilt.
            stageReload(seq);
            break;
#endif

        default:
            break;
        }

        updateRandomizerMiscOptions();
        activateInput();
    }
}

// ============================================================================
// Capture Debug Menu
// ============================================================================

static s32 getVisibleListItemCount(const CommonVerticalList* list) {
    if (!list || list->mListPartsNum <= 0)
        return 0;
    return list->mListPartsNum;
}

static bool isSameUtf16String(const char16_t* a, const char16_t* b) {
    if (!a || !b)
        return a == b;
    while (*a && *b) {
        if (*a != *b)
            return false;
        a++;
        b++;
    }
    return *a == *b;
}

static al::LayoutActor* findVisibleListItemByText(CommonVerticalList* list, const char16_t* label) {
    if (!list || !label)
        return nullptr;
    const s32 visibleItemCount = getVisibleListItemCount(list);
    for (s32 i = 0; i < visibleItemCount; i++) {
        al::LayoutActor* item = list->mListPartsArr[i];
        if (!item)
            continue;
        if (isSameUtf16String(al::getPaneStringBuffer(item, "TxtContent"), label))
            return item;
    }
    return nullptr;
}

static void refreshListSelectedText(CommonVerticalList* list,
    const sead::WFixedSafeString<0x200>* labels, const char16_t* oldLabel, s32 selected,
    s32 dataCount) {
    if (!list || !labels || selected < 0 || selected >= dataCount)
        return;

    al::LayoutActor* item = findVisibleListItemByText(list, oldLabel);
    if (!item)
        item = (al::LayoutActor*)list->getParts(selected);
    if (!item)
        return;

    StageSceneStateModConfig::applyRowStyle(
        item, StageSceneStateModConfig::RowStyle::Base);
    al::setPaneString(item, "TxtContent", labels[selected].cstr(), 0);
}

void StageSceneStateModConfig::initCaptureDebugMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_CAPTURE_DEBUG] = new SimpleLayoutMenu(
        "CaptureDebugMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_CAPTURE_DEBUG] = new CommonVerticalList(
        menuList[MENU_CAPTURE_DEBUG], initInfo, true);

    al::setPaneString(menuList[MENU_CAPTURE_DEBUG], "TxtOption", u"Capture Debug", 0);
    setStandardMenuFooter(mMenuFooters, MENU_CAPTURE_DEBUG, menuList[MENU_CAPTURE_DEBUG], initInfo);
    configureSingleColumnScrollableList(optionsList[MENU_CAPTURE_DEBUG]);

    const s32 captureCount = (!sSuppressLiveData && Randomizer::isCaptureLockFeatureEnabled())
        ? CaptureRando::getCaptureCount()
        : 0;
    const s32 dataCount = captureCount < maxMsgCount ? captureCount : maxMsgCount;
    optionsList[MENU_CAPTURE_DEBUG]->initDataNoResetSelected(dataCount);

    optionsList[MENU_CAPTURE_DEBUG]->addStringData(
        msgList[MENU_CAPTURE_DEBUG]->mBuffer, "TxtContent");
    optionsList[MENU_CAPTURE_DEBUG]->setEnableData(mCaptureDebugEnabled);
    // On the title host, skip the live capture-state read entirely (see
    // sSuppressLiveData) — the menu stays empty and is never shown there.
    if (!sSuppressLiveData)
        updateCaptureDebugOptions();
}

static bool isCaptureDebugMenuUnlocked(GameDataHolderAccessor accessor,
    const CaptureRando::CaptureEntry& capture) {
    if (CaptureRando::isCaptureDebugOverrideActive(capture))
        return CaptureRando::isCaptureDebugUnlocked(capture);
    return CaptureRando::isCaptureUnlocked(accessor, capture);
}

void StageSceneStateModConfig::updateCaptureDebugOptions(bool refreshParts) {
    if (!Randomizer::isCaptureLockFeatureEnabled()) {
        for (s32 i = 0; i < maxMsgCount; i++) {
            mCaptureDebugEnabled[i] = false;
            msgList[MENU_CAPTURE_DEBUG]->mBuffer[i].copy(u"");
        }
        optionsList[MENU_CAPTURE_DEBUG]->setEnableData(mCaptureDebugEnabled);
        if (refreshParts) {
            optionsList[MENU_CAPTURE_DEBUG]->updateParts();
            updateCaptureDebugStatePanes();
        }
        return;
    }

    const s32 captureCount = CaptureRando::getCaptureCount();
    const s32 dataCount = captureCount < maxMsgCount ? captureCount : maxMsgCount;

    for (s32 i = 0; i < maxMsgCount; i++) {
        const CaptureRando::CaptureEntry* capture = i < dataCount
            ? CaptureRando::getCapture(i)
            : nullptr;
        mCaptureDebugEnabled[i] = capture != nullptr;

        if (!capture) {
            msgList[MENU_CAPTURE_DEBUG]->mBuffer[i].copy(u"");
            continue;
        }

        char label[160];
        const bool unlocked = isCaptureDebugMenuUnlocked(
            GameDataHolderAccessor(mGameDataHolder), *capture);
        nn::util::SNPrintf(label, sizeof(label), "%s%s",
            getTogglePrefix(unlocked), capture->displayName);
        msgList[MENU_CAPTURE_DEBUG]->mBuffer[i].convertFromMultiByteString(label, strlen(label));
    }

    optionsList[MENU_CAPTURE_DEBUG]->setEnableData(mCaptureDebugEnabled);
    if (refreshParts) {
        optionsList[MENU_CAPTURE_DEBUG]->updateParts();
        updateCaptureDebugStatePanes();
    }
}

void StageSceneStateModConfig::updateCaptureDebugStatePanes() {
    const s32 listPartsNum = getVisibleListItemCount(optionsList[MENU_CAPTURE_DEBUG]);
    for (s32 i = 0; i < listPartsNum; i++) {
        al::LayoutActor* item = optionsList[MENU_CAPTURE_DEBUG]->mListPartsArr[i];
        if (item)
            applyRowStyle(item, RowStyle::Base);
    }
}

void StageSceneStateModConfig::exeCaptureDebug() {
    if (!Randomizer::isCaptureLockFeatureEnabled()) {
        endSubMenu();
        return;
    }

    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_CAPTURE_DEBUG];
        mCurrentMenu = menuList[MENU_CAPTURE_DEBUG];
        updateCaptureDebugOptions();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    subMenuUpdate();
    updateCaptureDebugStatePanes();

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        const s32 selected = mCurrentList->mCurSelected;
        const s32 captureCount = CaptureRando::getCaptureCount();
        const s32 dataCount = captureCount < maxMsgCount ? captureCount : maxMsgCount;
        sead::WFixedSafeString<0x200> oldLabel;
        if (selected >= 0 && selected < dataCount)
            oldLabel.copy(msgList[MENU_CAPTURE_DEBUG]->mBuffer[selected]);

        if (selected >= 0 && selected < dataCount) {
            const CaptureRando::CaptureEntry* capture = CaptureRando::getCapture(selected);
            if (capture) {
                GameDataHolderWriter writer(mGameDataHolder);
                const bool unlocked = isCaptureDebugMenuUnlocked(
                    GameDataHolderAccessor(mGameDataHolder), *capture);
                CaptureRando::setCaptureDebugUnlocked(*capture, !unlocked);
                CaptureRando::setCaptureUnlocked(writer, *capture, !unlocked);
            }
        }

        updateCaptureDebugOptions(false);
        refreshListSelectedText(optionsList[MENU_CAPTURE_DEBUG],
            msgList[MENU_CAPTURE_DEBUG]->mBuffer, oldLabel.cstr(), selected, dataCount);
        activateInput();
    }
}

// ============================================================================
// Ability Debug Menu
// ============================================================================

static bool isAbilityStartingCapThrow(const AbilityRando::AbilityEntry& ability) {
    const AbilityRando::AbilityEntry* startingCapThrow = AbilityRando::getStartingCapThrow();
    return startingCapThrow && startingCapThrow->id == ability.id;
}

static bool isAbilityDebugMenuToggleable(const AbilityRando::AbilityEntry& ability) {
    return !ability.alwaysUnlocked;
}

void StageSceneStateModConfig::initAbilityDebugMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_ABILITY_DEBUG] = new SimpleLayoutMenu(
        "AbilityDebugMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_ABILITY_DEBUG] = new CommonVerticalList(
        menuList[MENU_ABILITY_DEBUG], initInfo, true);

    al::setPaneString(menuList[MENU_ABILITY_DEBUG], "TxtOption", u"Ability Debug", 0);
    setStandardMenuFooter(mMenuFooters, MENU_ABILITY_DEBUG, menuList[MENU_ABILITY_DEBUG], initInfo);
    configureSingleColumnScrollableList(optionsList[MENU_ABILITY_DEBUG]);

    const s32 abilityCount = (!sSuppressLiveData && Randomizer::isAbilityLockFeatureEnabled())
        ? AbilityRando::getDebugAbilityCount()
        : 0;
    const s32 dataCount = abilityCount < maxMsgCount ? abilityCount : maxMsgCount;
    optionsList[MENU_ABILITY_DEBUG]->initDataNoResetSelected(dataCount);

    optionsList[MENU_ABILITY_DEBUG]->addStringData(
        msgList[MENU_ABILITY_DEBUG]->mBuffer, "TxtContent");
    optionsList[MENU_ABILITY_DEBUG]->setEnableData(mAbilityDebugEnabled);
    // On the title host, skip the live ability-state read entirely (see
    // sSuppressLiveData) — the menu stays empty and is never shown there.
    if (!sSuppressLiveData)
        updateAbilityDebugOptions();
}

static bool isAbilityDebugMenuUnlocked(GameDataHolderAccessor accessor,
    const AbilityRando::AbilityEntry& ability) {
    if (ability.alwaysUnlocked)
        return true;
    if (AbilityRando::isAbilityDebugOverrideActive(ability))
        return AbilityRando::isAbilityDebugUnlocked(ability);
    if (isAbilityStartingCapThrow(ability))
        return true;
    return AbilityRando::isAbilityInSave(accessor, ability);
}

void StageSceneStateModConfig::updateAbilityDebugOptions(bool refreshParts) {
    if (!Randomizer::isAbilityLockFeatureEnabled()) {
        for (s32 i = 0; i < maxMsgCount; i++) {
            mAbilityDebugEnabled[i] = false;
            msgList[MENU_ABILITY_DEBUG]->mBuffer[i].copy(u"");
        }
        optionsList[MENU_ABILITY_DEBUG]->setEnableData(mAbilityDebugEnabled);
        if (refreshParts) {
            optionsList[MENU_ABILITY_DEBUG]->updateParts();
            updateAbilityDebugStatePanes();
        }
        return;
    }

    const s32 abilityCount = AbilityRando::getDebugAbilityCount();
    const s32 dataCount = abilityCount < maxMsgCount ? abilityCount : maxMsgCount;

    for (s32 i = 0; i < maxMsgCount; i++) {
        const AbilityRando::AbilityEntry* ability = i < dataCount
            ? AbilityRando::getDebugAbility(i)
            : nullptr;
        mAbilityDebugEnabled[i] = ability && isAbilityDebugMenuToggleable(*ability);

        if (!ability) {
            msgList[MENU_ABILITY_DEBUG]->mBuffer[i].copy(u"");
            continue;
        }

        char label[160];
        const bool unlocked = isAbilityDebugMenuUnlocked(
            GameDataHolderAccessor(mGameDataHolder), *ability);
        nn::util::SNPrintf(label, sizeof(label), "%s%s%s",
            getTogglePrefix(unlocked),
            ability->displayName,
            isAbilityStartingCapThrow(*ability) ? " (Start)" : "");
        msgList[MENU_ABILITY_DEBUG]->mBuffer[i].convertFromMultiByteString(label, strlen(label));
    }

    optionsList[MENU_ABILITY_DEBUG]->setEnableData(mAbilityDebugEnabled);
    if (refreshParts) {
        optionsList[MENU_ABILITY_DEBUG]->updateParts();
        updateAbilityDebugStatePanes();
    }
}

void StageSceneStateModConfig::updateAbilityDebugStatePanes() {
    const s32 listPartsNum = getVisibleListItemCount(optionsList[MENU_ABILITY_DEBUG]);
    for (s32 i = 0; i < listPartsNum; i++) {
        al::LayoutActor* item = optionsList[MENU_ABILITY_DEBUG]->mListPartsArr[i];
        if (item)
            applyRowStyle(item, RowStyle::Base);
    }
}

static void setAbilityDebugUnlockedRecursive(GameDataHolderWriter writer,
    const AbilityRando::AbilityEntry& ability, bool unlocked) {
    if (unlocked && ability.prerequisite >= 0) {
        if (const AbilityRando::AbilityEntry* prerequisite = AbilityRando::getAbility(ability.prerequisite))
            setAbilityDebugUnlockedRecursive(writer, *prerequisite, true);
    }
    AbilityRando::setAbilityDebugUnlocked(ability, unlocked);
    AbilityRando::setAbilityUnlocked(writer, ability, unlocked);
}

void StageSceneStateModConfig::exeAbilityDebug() {
    if (!Randomizer::isAbilityLockFeatureEnabled()) {
        endSubMenu();
        return;
    }

    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_ABILITY_DEBUG];
        mCurrentMenu = menuList[MENU_ABILITY_DEBUG];
        updateAbilityDebugOptions();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    subMenuUpdate();
    updateAbilityDebugStatePanes();

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        const s32 selected = mCurrentList->mCurSelected;
        const s32 abilityCount = AbilityRando::getDebugAbilityCount();
        const s32 dataCount = abilityCount < maxMsgCount ? abilityCount : maxMsgCount;
        sead::WFixedSafeString<0x200> oldLabel;
        if (selected >= 0 && selected < dataCount)
            oldLabel.copy(msgList[MENU_ABILITY_DEBUG]->mBuffer[selected]);

        if (selected >= 0 && selected < dataCount) {
            const AbilityRando::AbilityEntry* ability = AbilityRando::getDebugAbility(selected);
            if (ability && isAbilityDebugMenuToggleable(*ability)) {
                GameDataHolderWriter writer(mGameDataHolder);
                const bool unlocked = isAbilityDebugMenuUnlocked(
                    GameDataHolderAccessor(mGameDataHolder), *ability);
                setAbilityDebugUnlockedRecursive(writer, *ability, !unlocked);
            }
        }

        updateAbilityDebugOptions(false);
        refreshListSelectedText(optionsList[MENU_ABILITY_DEBUG],
            msgList[MENU_ABILITY_DEBUG]->mBuffer, oldLabel.cstr(), selected, dataCount);
        activateInput();
    }
}

// ============================================================================
// Debug Warp
// ============================================================================

static void debugWarpToSubarea(
    GameDataHolder* dataHolder,
    const EntranceDB::EntranceLink& source,
    s32 sourceIdx,
    bool fromFarSide) {
    if (!dataHolder || sourceIdx < 0 || sourceIdx >= EntranceDB::sLinkCount
        || !source.subStage || !source.subStage[0])
        return;

    const char* startId = fromFarSide ? source.subEntranceOut : source.subEntranceIn;
    const char* fallbackId = fromFarSide ? source.subEntranceIn : source.subEntranceOut;
    if (!startId || !startId[0])
        startId = fallbackId;

    EntranceHooks::bypassNextTransition();
    EntranceRando::noteEnteredSub(source.subStage);

    GameDataHolderWriter holder(dataHolder);
    ChangeStageInfo info(
        holder,
        startId ? startId : "start",
        source.subStage,
        false,
        -1,
        ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
    info.mPlacementString.copy(startId ? startId : "start");
    info.setWipeType("FadeBlack");
    ScenarioPatcher::normalizeChangeStageInfo(holder->getGameDataFile(), &info, 0);
    GameDataFunction::tryChangeNextStage(holder, &info);
}

static void debugWarpToOverworld(GameDataHolder* dataHolder, const char* stageName) {
    if (!dataHolder || !stageName || !stageName[0])
        return;

    EntranceHooks::bypassNextTransition();
    EntranceRando::clearSubStack();

    GameDataHolderWriter holder(dataHolder);
    ChangeStageInfo info(
        holder,
        "start",
        stageName,
        false,
        -1,
        ChangeStageInfo::SubScenarioType::NO_SUB_SCENARIO);
    info.mPlacementString.copy("start");
    info.setWipeType("FadeBlack");
    ScenarioPatcher::normalizeChangeStageInfo(holder->getGameDataFile(), &info, 0);
    GameDataFunction::tryChangeNextStage(holder, &info);
}

void StageSceneStateModConfig::initDebugWarpKingdomMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_DEBUG_WARP_KINGDOM] = new SimpleLayoutMenu(
        "DebugWarpKingdomMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_DEBUG_WARP_KINGDOM] = new CommonVerticalList(
        menuList[MENU_DEBUG_WARP_KINGDOM], initInfo, true);

    al::setPaneString(menuList[MENU_DEBUG_WARP_KINGDOM], "TxtOption", u"Debug Warp", 0);
    setStandardMenuFooter(mMenuFooters, MENU_DEBUG_WARP_KINGDOM, menuList[MENU_DEBUG_WARP_KINGDOM], initInfo);
    configureSingleColumnScrollableList(optionsList[MENU_DEBUG_WARP_KINGDOM]);

    const s32 dataCount = WorldMeta::sWorldCount < maxMsgCount
        ? WorldMeta::sWorldCount
        : maxMsgCount;
    optionsList[MENU_DEBUG_WARP_KINGDOM]->initDataNoResetSelected(dataCount);

    optionsList[MENU_DEBUG_WARP_KINGDOM]->addStringData(
        msgList[MENU_DEBUG_WARP_KINGDOM]->mBuffer, "TxtContent");
    updateDebugWarpKingdomOptions();
}

void StageSceneStateModConfig::updateDebugWarpKingdomOptions() {
    for (s32 i = 0; i < maxMsgCount; i++)
        msgList[MENU_DEBUG_WARP_KINGDOM]->mBuffer[i].copy(u"");

    for (s32 i = 0; i < WorldMeta::sWorldCount && i < maxMsgCount; i++)
        msgList[MENU_DEBUG_WARP_KINGDOM]->mBuffer[i].copy(WorldMeta::sWorlds[i].displayName16);
}

void StageSceneStateModConfig::exeDebugWarpKingdom() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_DEBUG_WARP_KINGDOM];
        mCurrentMenu = menuList[MENU_DEBUG_WARP_KINGDOM];
        al::setPaneString(mCurrentMenu, "TxtOption",
            mKingdomPickerMode == KINGDOM_PICKER_CHECKS ? u"Kingdom Checks" : u"Debug Warp", 0);
        updateDebugWarpKingdomOptions();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    subMenuUpdate();

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        const s32 selected = mCurrentList->mCurSelected;
        if (selected >= 0 && selected < WorldMeta::sWorldCount) {
            mDebugWarpSelectedKingdomIndex = selected;
            if (mKingdomPickerMode == KINGDOM_PICKER_CHECKS)
                al::setNerve(this, &NrvStageSceneStateModConfig.KingdomChecksList);
            else
                al::setNerve(this, &NrvStageSceneStateModConfig.DebugWarpSubarea);
            return;
        }
        activateInput();
    }
}

void StageSceneStateModConfig::initDebugWarpSubareaMenu(const al::LayoutInitInfo& initInfo) {
    menuList[MENU_DEBUG_WARP_SUBAREA] = new SimpleLayoutMenu(
        "DebugWarpSubareaMenu", "OptionModCheck", initInfo, 0, false);
    optionsList[MENU_DEBUG_WARP_SUBAREA] = new CommonVerticalList(
        menuList[MENU_DEBUG_WARP_SUBAREA], initInfo, true);

    al::setPaneString(menuList[MENU_DEBUG_WARP_SUBAREA], "TxtOption", u"Debug Warp", 0);
    setStandardMenuFooter(mMenuFooters, MENU_DEBUG_WARP_SUBAREA, menuList[MENU_DEBUG_WARP_SUBAREA], initInfo);
    configureSingleColumnScrollableList(optionsList[MENU_DEBUG_WARP_SUBAREA]);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->initDataNoResetSelected(maxMsgCount);

    optionsList[MENU_DEBUG_WARP_SUBAREA]->addStringData(
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer, "TxtContent");
    optionsList[MENU_DEBUG_WARP_SUBAREA]->setEnableData(mDebugWarpEnabled);
}

void StageSceneStateModConfig::updateDebugWarpSubareaOptions() {
    for (s32 i = 0; i < maxMsgCount; i++) {
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[i].copy(u"");
        mDebugWarpEnabled[i] = false;
        mDebugWarpFromFarSide[i] = false;
    }

    // This list can hold up to maxMsgCount logical rows, but the underlying
    // CommonVerticalList only ever allocates as many physical LayoutActor
    // parts as the "OptionModCheck" layout resource provides (far fewer than
    // maxMsgCount). Bound by the list's real physical row count, not
    // maxMsgCount, or mListPartsArr[i + 1] reads past its allocation and
    // applyRowStyle() crashes inside al::hidePane on the garbage pointer.
    const s32 listPartsNum = getVisibleListItemCount(optionsList[MENU_DEBUG_WARP_SUBAREA]);
    for (s32 i = 0; i < listPartsNum; i++) {
        al::LayoutActor* item = optionsList[MENU_DEBUG_WARP_SUBAREA]->mListPartsArr[i];
        if (item)
            applyRowStyle(item, RowStyle::Base);
    }

    const char* homeStage = WorldMeta::sWorlds[mDebugWarpSelectedKingdomIndex].mainStage;
    s32 count = 1;
    msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[0].copy(u"Overworld");
    mDebugWarpEnabled[0] = true;
    mDebugWarpLinkIndices[0] = -1;

    auto addEntry = [&](const EntranceDB::EntranceLink& link, s32 linkIdx, bool fromFarSide) {
        if (count >= maxMsgCount)
            return;

        const char* entrance = fromFarSide ? link.subEntranceOut : link.subEntranceIn;
        const char* fallbackEntrance = fromFarSide ? link.subEntranceIn : link.subEntranceOut;
        if (!entrance || !entrance[0])
            entrance = fallbackEntrance;
        if (!entrance || !entrance[0])
            return;

        char label[160];
        nn::util::SNPrintf(label, sizeof(label), "%s via %s %s",
            getEnglishName(link.subStage), fromFarSide ? "exit" : "entry", entrance);
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[count].convertFromMultiByteString(label, strlen(label));
        mDebugWarpEnabled[count] = true;
        mDebugWarpLinkIndices[count] = linkIdx;
        mDebugWarpFromFarSide[count] = fromFarSide;
        count++;
    };

    for (s32 i = 0; i < EntranceDB::sLinkCount && count < maxMsgCount; i++) {
        const EntranceDB::EntranceLink& link = EntranceDB::sLinks[i];
        const bool isShiveriaSubarea = mDebugWarpSelectedKingdomIndex == WorldMeta::WorldId_Snow
            && std::strcmp(link.homeStage, "SnowWorldTownStage") == 0
            && (std::strcmp(link.subStage, "SnowWorldShopStage") == 0
                || std::strcmp(link.subStage, "SnowWorldCostumeStage") == 0);
        if (!EntranceDB::isRuntimeUsableLink(i)
            || (std::strcmp(link.homeStage, homeStage) != 0 && !isShiveriaSubarea))
            continue;

        addEntry(link, i, false);
        if (count >= maxMsgCount)
            break;
        if (EntranceDB::hasSubOutDoor(i))
            addEntry(link, i, true);
    }

    mDebugWarpSubareaCount = count;
    optionsList[MENU_DEBUG_WARP_SUBAREA]->initDataNoResetSelected(mDebugWarpSubareaCount);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->setSelectedIdx(0, 0);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->addStringData(
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer, "TxtContent");
    optionsList[MENU_DEBUG_WARP_SUBAREA]->setEnableData(mDebugWarpEnabled);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->updateParts();
}

void StageSceneStateModConfig::exeDebugWarpSubarea() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_DEBUG_WARP_SUBAREA];
        mCurrentMenu = menuList[MENU_DEBUG_WARP_SUBAREA];
        al::setPaneString(mCurrentMenu, "TxtOption",
            WorldMeta::sWorlds[mDebugWarpSelectedKingdomIndex].displayName16, 0);
        mMenuFooters[MENU_DEBUG_WARP_SUBAREA]
            ->changeText(makeMainFooterText());
        updateDebugWarpSubareaOptions();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    handleMenuInput();

    if (rs::isTriggerUiCancel(mHost) && !mIsDecideConfig) {
        al::setNerve(this, &NrvStageSceneStateModConfig.DebugWarpKingdom);
        return;
    }

    if (mIsDecideConfig && mCurrentList->isDecideEnd()) {
        const s32 selected = mCurrentList->mCurSelected;
        if (selected >= 0 && selected < mDebugWarpSubareaCount && mDebugWarpEnabled[selected]) {
            const s32 linkIdx = mDebugWarpLinkIndices[selected];
            kill();
            if (linkIdx < 0) {
                debugWarpToOverworld(
                    mGameDataHolder,
                    WorldMeta::sWorlds[mDebugWarpSelectedKingdomIndex].mainStage);
            } else {
                const EntranceDB::EntranceLink& link = EntranceDB::sLinks[linkIdx];
                debugWarpToSubarea(
                    mGameDataHolder, link, linkIdx, mDebugWarpFromFarSide[selected]);
            }
            return;
        }
        activateInput();
    }
}

#if SMOR_FEATURE_CHAOS_ANY
// ============================================================================
// Enemy Swap Inspector (reuses MENU_DEBUG_WARP_SUBAREA)
// ============================================================================

namespace {
    namespace EnemyInspectorDB = Randomizer::EnemySwapDB;

    static s32 compareInspectorTargets(s16 leftIndex, s16 rightIndex) {
        const EnemyInspectorDB::Entity& left = EnemyInspectorDB::sEntities[leftIndex];
        const EnemyInspectorDB::Entity& right = EnemyInspectorDB::sEntities[rightIndex];
        return std::strcmp(left.friendlyName, right.friendlyName);
    }

    static bool inspectorSourceIsSupported(
        const EnemyInspectorDB::Entity& entity) {
        return !std::strcmp(entity.category, "enemy")
            || !std::strcmp(entity.category, "capture")
            || !std::strcmp(entity.category, "npc");
    }

    static bool inspectorStageMatches(
        const char* tableStage, const char* runtimeStage) {
        if (!tableStage || !runtimeStage)
            return false;
        if (!std::strcmp(tableStage, runtimeStage))
            return true;
        const size_t tableLength = std::strlen(tableStage);
        const size_t runtimeLength = std::strlen(runtimeStage);
        if (tableLength + 3 == runtimeLength
            && !std::strncmp(runtimeStage, tableStage, tableLength)
            && !std::strcmp(runtimeStage + tableLength, "Map"))
            return true;
        if (runtimeLength + 3 == tableLength
            && !std::strncmp(tableStage, runtimeStage, runtimeLength)
            && !std::strcmp(tableStage + runtimeLength, "Map"))
            return true;
        return false;
    }

    static bool inspectorPlacementMatchesCurrentStage(
        const EnemyInspectorDB::Placement& placement,
        const char* physicalStage, const char* logicStage) {
        return (logicStage && placement.logicStage
                   && !std::strcmp(placement.logicStage, logicStage))
            || inspectorStageMatches(placement.stage, physicalStage);
    }

}

void StageSceneStateModConfig::rebuildEnemySwapInspectorTargets() {
    // Build the object-ID slider from the current logical stage and the live
    // physical-stage alias. Embedded-zone records can use a different
    // physical key from the stage returned by GameDataFunction, and NPC rows
    // are especially common in those zones.
    static const char16_t* sNoObjectLabel = u"None";
    mEnemyInspectorObjectChoiceLabels[0] = sNoObjectLabel;
    mEnemyInspectorObjectChoicePlacements[0] = -1;
    s32 objectCount = 0;
    for (s32 i = 0; i < EnemyInspectorDB::sPlacementCount; i++) {
        const EnemyInspectorDB::Placement& placement = EnemyInspectorDB::sPlacements[i];
        if (!inspectorPlacementMatchesCurrentStage(
                placement, mEnemyInspectorPhysicalStage,
                mEnemyInspectorLogicStage)
            || placement.entity < 0
            || placement.entity >= EnemyInspectorDB::sEntityCount
            || !inspectorSourceIsSupported(
                EnemyInspectorDB::sEntities[placement.entity]))
            continue;

        const s32 choice = objectCount + 1;
        const EnemyInspectorDB::Entity& source = EnemyInspectorDB::sEntities[placement.entity];
        char label[128];
        nn::util::SNPrintf(
            label, sizeof(label), "%s (%s) [%d]",
            placement.objId ? placement.objId : "?",
            source.friendlyName ? source.friendlyName : source.name, i);
        mEnemyInspectorObjectIdLabels[objectCount].convertFromMultiByteString(
            label, std::strlen(label));
        mEnemyInspectorObjectChoiceLabels[choice] = mEnemyInspectorObjectIdLabels[objectCount].cstr();
        mEnemyInspectorObjectChoicePlacements[choice] = (s16)i;
        objectCount++;
    }
    mEnemyInspectorActiveObjectChoiceCount = objectCount + 1;

    // Drop the selected object if it no longer belongs to the current stage,
    // and make the slider convenient to use when entering the inspector.
    bool selectedStillExists = false;
    for (s32 choice = 1; choice < mEnemyInspectorActiveObjectChoiceCount;
         choice++) {
        if (mEnemyInspectorObjectChoicePlacements[choice]
            == mEnemyInspectorSelectedPlacement) {
            selectedStillExists = true;
            break;
        }
    }
    if (!selectedStillExists)
        mEnemyInspectorSelectedPlacement = objectCount > 0
            ? mEnemyInspectorObjectChoicePlacements[1]
            : -1;

    // Build the replacement list from the same asymmetric policy enforced by
    // EnemySwapDebug at commit and runtime lookup time.
    static const char16_t* sObjectDefaultLabel = u"Default";
    static const char16_t* sSelectObjectLabel = u"Select Object";
    const EnemyInspectorDB::Entity* source = nullptr;
    if (mEnemyInspectorSelectedPlacement >= 0
        && mEnemyInspectorSelectedPlacement
            < EnemyInspectorDB::sPlacementCount) {
        const s16 sourceEntity = EnemyInspectorDB::sPlacements[mEnemyInspectorSelectedPlacement].entity;
        if (sourceEntity >= 0 && sourceEntity < EnemyInspectorDB::sEntityCount)
            source = &EnemyInspectorDB::sEntities[sourceEntity];
    }

    mEnemyInspectorObjectReplacementChoiceLabels[0] = source ? sObjectDefaultLabel : sSelectObjectLabel;
    mEnemyInspectorObjectReplacementChoiceEntities[0] = -1;
    s32 replacementTargetCount = 0;
    for (s32 entityIndex = 0;
         source && entityIndex < EnemyInspectorDB::sEntityCount;
         entityIndex++) {
        if (!EnemySwapDebug::isObjectTargetSupported(
                mEnemyInspectorSelectedPlacement, (s16)entityIndex))
            continue;
        mEnemyInspectorSortedObjectReplacementTargets[replacementTargetCount++] = (s16)entityIndex;
    }

    for (s32 i = 1; i < replacementTargetCount; i++) {
        const s16 value = mEnemyInspectorSortedObjectReplacementTargets[i];
        s32 insert = i;
        while (insert > 0
            && compareInspectorTargets(
                   value,
                   mEnemyInspectorSortedObjectReplacementTargets[insert - 1])
                < 0) {
            mEnemyInspectorSortedObjectReplacementTargets[insert] = mEnemyInspectorSortedObjectReplacementTargets[insert - 1];
            insert--;
        }
        mEnemyInspectorSortedObjectReplacementTargets[insert] = value;
    }

    for (s32 i = 0; i < replacementTargetCount; i++) {
        const s16 entityIndex = mEnemyInspectorSortedObjectReplacementTargets[i];
        const char* name = EnemyInspectorDB::sEntities[entityIndex].friendlyName;
        mEnemyInspectorObjectReplacementTargetLabels[i]
            .convertFromMultiByteString(name, std::strlen(name));
        mEnemyInspectorObjectReplacementChoiceLabels[i + 1] = mEnemyInspectorObjectReplacementTargetLabels[i].cstr();
        mEnemyInspectorObjectReplacementChoiceEntities[i + 1] = entityIndex;
    }
    mEnemyInspectorActiveObjectReplacementChoiceCount = replacementTargetCount + 1;
}

void StageSceneStateModConfig::updateEnemySwapInspectorOptions() {
    CommonVerticalList* list = optionsList[MENU_DEBUG_WARP_SUBAREA];
    if (!list)
        return;
    const s32 previouslySelectedRow = list->mCurSelected;

    // Non-roll rows get a persistent, non-NULL empty roll (matching the
    // RANDO_MISC menu's dataEmpty). Must be static: setData keeps the pointer.
    static const char16_t* sEmptyRollMsg[] = { u"" };
    for (s32 i = 0; i < maxMsgCount; i++) {
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[i].copy(u"");
        mEnemyInspectorEnabled[i] = false;
        mEnemyInspectorRollParts[i] = { 0, sEmptyRollMsg, 0, false };
    }

    // Row 0: Reset all object overrides.
    msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[kEnemyInspectorResetAllRow].copy(u"Reset All Object Swaps");
    mEnemyInspectorEnabled[kEnemyInspectorResetAllRow] = true;

    // Row 1: Show Object IDs (checkbox)
    msgList[MENU_DEBUG_WARP_SUBAREA]
        ->mBuffer[kEnemyInspectorObjectIdsVisibleRow]
        .copy(
            u"Show Object IDs");
    mEnemyInspectorEnabled[kEnemyInspectorObjectIdsVisibleRow] = true;

    // Rows 2 and 3 are one editable pair. The object-ID roll contains enemy,
    // capture, and NPC placements; its replacement roll is rebuilt from the
    // selected source category.
    msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[kEnemyInspectorObjectIdRow].copy(u"Object ID");
    mEnemyInspectorEnabled[kEnemyInspectorObjectIdRow] = true;
    msgList[MENU_DEBUG_WARP_SUBAREA]
        ->mBuffer[kEnemyInspectorObjectReplacementRow]
        .copy(u"Object Replacement");
    mEnemyInspectorEnabled[kEnemyInspectorObjectReplacementRow] = true;

    // Row 4 is the explicit boundary between editing session state and
    // reloading the stage. This lets A commit several independent pairs in a
    // single visit without applying only the last slider selection.
    msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[kEnemyInspectorApplyRow].copy(u"Apply Swaps / Reload Stage");
    mEnemyInspectorEnabled[kEnemyInspectorApplyRow] = true;

    // Row 5: re-resolve every enemy, NPC and prop in the file and come back
    // into this stage with the new roll. The whole crash-hunting loop in one
    // button; the roll number is shown so a crashing roll can be named.
    {
        char rerollLabel[0x80] = {};
        std::snprintf(rerollLabel, sizeof(rerollLabel),
            "Reroll Swaps / Reload Stage (roll %u)",
            static_cast<unsigned>(EnemySwapRando::getRerollCount()));
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[kEnemyInspectorRerollRow].convertFromMultiByteString(
            rerollLabel, std::strlen(rerollLabel));
    }
    mEnemyInspectorEnabled[kEnemyInspectorRerollRow] = EnemySwapRando::isInitialized();

    const auto selectedObjectOverrideChoice = [this]() {
        const s32 placementIndex = mEnemyInspectorSelectedPlacement;
        if (placementIndex < 0)
            return 0;
        const s16 activeEntity = EnemySwapDebug::getOverrideEntity(
            placementIndex);
        if (activeEntity < 0)
            return 0;
        for (s32 c = 1;
             c < mEnemyInspectorActiveObjectReplacementChoiceCount; c++) {
            if (mEnemyInspectorObjectReplacementChoiceEntities[c]
                == activeEntity)
                return c;
        }
        return 0;
    };

    s32 selectedObjectChoice = 0;
    for (s32 choice = 1;
         choice < mEnemyInspectorActiveObjectChoiceCount; choice++) {
        if (mEnemyInspectorObjectChoicePlacements[choice]
            == mEnemyInspectorSelectedPlacement) {
            selectedObjectChoice = choice;
            break;
        }
    }
    mEnemyInspectorRollParts[kEnemyInspectorObjectIdRow] = {
        mEnemyInspectorActiveObjectChoiceCount,
        mEnemyInspectorObjectChoiceLabels,
        selectedObjectChoice,
        false
    };
    mEnemyInspectorRollParts[kEnemyInspectorObjectReplacementRow] = {
        mEnemyInspectorActiveObjectReplacementChoiceCount,
        mEnemyInspectorObjectReplacementChoiceLabels,
        selectedObjectOverrideChoice(),
        false
    };

    list->initDataNoResetSelected(kEnemyInspectorRowCount);
    list->setSelectedIdx(
        previouslySelectedRow >= 0
                && previouslySelectedRow < kEnemyInspectorRowCount
            ? previouslySelectedRow
            : 0,
        0);
    list->setRollPartsData(mEnemyInspectorRollParts);
    list->addStringData(
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer,
        "TxtContent");
    list->setEnableData(mEnemyInspectorEnabled);

    for (s32 row = 0; row < kEnemyInspectorRowCount; row++) {
        al::LayoutActor* item = list->mListPartsArr[row + 1];
        if (!item)
            continue;
        al::startAction(item, "Loop", nullptr);
        if (row == kEnemyInspectorObjectIdsVisibleRow) {
            applyRowStyle(item, RowStyle::Check);
            al::startAction(
                item,
                EnemySwapDebug::areObjectIdsVisible()
                    ? "On"
                    : "Off",
                "State");
        } else if (row == kEnemyInspectorObjectIdRow
            || row == kEnemyInspectorObjectReplacementRow) {
            applyRowStyle(item, RowStyle::Roll);
        } else {
            applyRowStyle(item, RowStyle::Base);
        }
    }
    list->mStringsDirty = true;
    list->updateParts();
}

void StageSceneStateModConfig::exeEnemySwapInspector() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_DEBUG_WARP_SUBAREA];
        mCurrentMenu = menuList[MENU_DEBUG_WARP_SUBAREA];
        al::setPaneString(
            mCurrentMenu, "TxtOption",
            u"Enemy Swap Inspector", 0);
        mMenuFooters[MENU_DEBUG_WARP_SUBAREA]->changeText(
            makeEnemyInspectorFooterText());

        GameDataHolderAccessor accessor(mGameDataHolder);
        const char* physicalStage = GameDataFunction::getCurrentStageName(accessor);
        mEnemyInspectorPhysicalStage = physicalStage;
        mEnemyInspectorLogicStage = EnemySwapDebug::resolveLogicStage(physicalStage);
        EnemySwapDebug::setInspectorOverlayContext(
            true, mEnemyInspectorPhysicalStage, mEnemyInspectorLogicStage);
        mEnemyInspectorDirty = false;
        rebuildEnemySwapInspectorTargets();
        updateEnemySwapInspectorOptions();
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    handleMenuInput();

    if (!mIsDecideConfig) {
        const s32 selected = mCurrentList->mCurSelected;
        const bool objectIdRow = selected == kEnemyInspectorObjectIdRow;
        const bool objectReplacementRow = selected == kEnemyInspectorObjectReplacementRow;
        if (objectIdRow || objectReplacementRow) {
            // Accept both the standard left/right menu buttons and the
            // right-stick roll controls. The inspector bypasses the generic
            // roll handling in handleMenuInput(), so it must handle both
            // input paths here or the sliders appear stuck on a controller
            // that uses the D-pad.
            if (mInput->isTriggerUiLeft()
                || al::isPadTriggerLeftRightStick()
                || al::isPadRepeatLeftRightStick())
                mCurrentList->rollLeft();
            if (mInput->isTriggerUiRight()
                || al::isPadTriggerRightRightStick()
                || al::isPadRepeatRightRightStick())
                mCurrentList->rollRight();

            if (objectIdRow) {
                const s32 objectChoice = mCurrentList->getRollPartsSelected(
                    selected);
                if (objectChoice >= 0
                    && objectChoice < mEnemyInspectorActiveObjectChoiceCount) {
                    const s32 placementIndex = mEnemyInspectorObjectChoicePlacements[objectChoice];
                    if (placementIndex
                        != mEnemyInspectorSelectedPlacement) {
                        mEnemyInspectorSelectedPlacement = placementIndex;
                        rebuildEnemySwapInspectorTargets();
                        updateEnemySwapInspectorOptions();
                    }
                }
            }
        }

        if (rs::isTriggerUiCancel(mHost)) {
            // B is the normal submenu-back action. Committed pairs remain in
            // the inspector state; the Apply row performs the eventual stage
            // reload after all desired pairs have been committed.
            mMenuFooters[MENU_DEBUG_WARP_SUBAREA]
                ->changeText(makeMainFooterText());
            endSubMenu();
            return;
        }
    }

    if (!mIsDecideConfig || !mCurrentList->isDecideEnd())
        return;

    const s32 selected = mCurrentList->mCurSelected;
    if (selected == kEnemyInspectorResetAllRow) {
        const s32 cleared = EnemySwapDebug::clearAllOverrides();
        if (cleared > 0)
            mEnemyInspectorDirty = true;
        mEnemyInspectorSelectedPlacement = -1;
        rebuildEnemySwapInspectorTargets();
        updateEnemySwapInspectorOptions();
        activateInput();
        return;
    }
    if (selected == kEnemyInspectorObjectIdsVisibleRow) {
        // Toggle Show Object IDs
        EnemySwapDebug::setObjectIdsVisible(
            !EnemySwapDebug::areObjectIdsVisible());
        updateEnemySwapInspectorOptions();
        activateInput();
        return;
    }
    const bool objectIdRow = selected == kEnemyInspectorObjectIdRow;
    const bool objectReplacementRow = selected == kEnemyInspectorObjectReplacementRow;
    if (selected == kEnemyInspectorRerollRow) {
        // The reroll happens before the reload so the new assignment is the
        // one the incoming stage builds from.
        EnemySwapRando::rerollAssignment();
        EnemySwapDebug::setInspectorOverlayContext(false, nullptr, nullptr);
        kill();
        HakoniwaSequence* seq = (HakoniwaSequence*)
                                    GameSystemFunction::getGameSystem()
                                        ->mSequence;
        stageReload(seq);
        return;
    }
    if (selected == kEnemyInspectorApplyRow) {
        if (mEnemyInspectorDirty) {
            EnemySwapDebug::setInspectorOverlayContext(false, nullptr, nullptr);
            kill();
            HakoniwaSequence* seq = (HakoniwaSequence*)
                                        GameSystemFunction::getGameSystem()
                                            ->mSequence;
            stageReload(seq);
        } else {
            mMenuFooters[MENU_DEBUG_WARP_SUBAREA]
                ->changeText(makeMainFooterText());
            endSubMenu();
        }
        return;
    }
    if (objectIdRow || objectReplacementRow) {
        // A commits the complete object/replacement pair from either selector
        // row, then returns to Object ID so another pair can be selected.
        if (applyEnemyInspectorPair()) {
            rebuildEnemySwapInspectorTargets();
            updateEnemySwapInspectorOptions();
        }
        mCurrentList->setSelectedIdx(kEnemyInspectorObjectIdRow, 0);
        activateInput();
        return;
    }

    activateInput();
}

bool StageSceneStateModConfig::applyEnemyInspectorPair() {
    if (!mCurrentList)
        return false;

    const s32 objectChoice = mCurrentList->getRollPartsSelected(
        kEnemyInspectorObjectIdRow);
    if (objectChoice >= 0
        && objectChoice < mEnemyInspectorActiveObjectChoiceCount)
        mEnemyInspectorSelectedPlacement = mEnemyInspectorObjectChoicePlacements[objectChoice];

    const s32 placementIndex = mEnemyInspectorSelectedPlacement;
    const s32 replacementChoice = mCurrentList->getRollPartsSelected(
        kEnemyInspectorObjectReplacementRow);
    if (placementIndex < 0
        || replacementChoice < 0
        || replacementChoice >= mEnemyInspectorActiveObjectReplacementChoiceCount)
        return false;

    const s16 replacementEntity = mEnemyInspectorObjectReplacementChoiceEntities[replacementChoice];
    const bool changed = EnemySwapDebug::getOverrideEntity(
                             placementIndex)
        != replacementEntity;
    if (!EnemySwapDebug::setOverrideEntity(
            placementIndex, replacementEntity))
        return false;

    if (changed) {
        const EnemyInspectorDB::Placement& placement = EnemyInspectorDB::sPlacements[placementIndex];
        Randomizer::logPush(
            "enemy inspector object pair: stage=%s logic=%s object=%s "
            "placement=%d target=%s",
            placement.stage ? placement.stage : "?",
            placement.logicStage ? placement.logicStage : "?",
            placement.objId ? placement.objId : "?", placementIndex,
            replacementEntity >= 0
                    && replacementEntity < EnemyInspectorDB::sEntityCount
                ? EnemyInspectorDB::sEntities[replacementEntity].name
                : "Default");
        mEnemyInspectorDirty = true;
    }
    return changed;
}
#endif

// ============================================================================
// Kingdom Checks (read-only: reuses the MENU_DEBUG_WARP_SUBAREA slot)
// ============================================================================

void StageSceneStateModConfig::updateKingdomChecksOptions() {
    for (s32 i = 0; i < maxMsgCount; i++) {
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[i].copy(u"");
        // A prior Debug Warp visit may have left some rows disabled/dimmed on
        // this shared list; Kingdom Checks rows are all equally viewable.
        mDebugWarpEnabled[i] = true;
    }

    // See updateDebugWarpSubareaOptions() - bound by the list's real physical
    // row count, not maxMsgCount, to avoid reading past mListPartsArr.
    const s32 listPartsNum = getVisibleListItemCount(optionsList[MENU_DEBUG_WARP_SUBAREA]);
    for (s32 i = 0; i < listPartsNum; i++) {
        al::LayoutActor* item = optionsList[MENU_DEBUG_WARP_SUBAREA]->mListPartsArr[i];
        if (item)
            applyRowStyle(item, RowStyle::Base);
    }

    for (s32 i = 0; i < maxMsgCount; i++) {
        mKingdomChecksEntries[i] = nullptr;
        mKingdomChecksAccessTargets[i] = -1;
    }

    // Keep access rows together at the top, then append the currently
    // reachable moons. This leaves enough room for the extra kingdom rows even
    // in a kingdom with a full moon list.
    s16 accessTargets[WorldMeta::sWorldCount] = {};
    const s32 accessCount = MoonRando::getKingdomDirectAccesses(
        GameDataHolderAccessor(mGameDataHolder), mDebugWarpSelectedKingdomIndex,
        accessTargets, WorldMeta::sWorldCount);
    for (s32 i = 0; i < accessCount && i < maxMsgCount; i++) {
        const s32 target = accessTargets[i];
        mKingdomChecksAccessTargets[i] = (s16)target;
        char label[160];
        nn::util::SNPrintf(label, sizeof(label), "Access to %s",
            WorldMeta::getDisplayNameForWorldId(target));
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[i].convertFromMultiByteString(
            label, strlen(label));
    }

    const s32 moonCapacity = maxMsgCount - (accessCount < maxMsgCount ? accessCount : maxMsgCount);
    const ShineDB::ShineEntry* moonEntries[maxMsgCount] = {};
    const s32 moonCount = MoonRando::getKingdomReachableUncollectedMoons(
        GameDataHolderAccessor(mGameDataHolder), mDebugWarpSelectedKingdomIndex,
        moonEntries, moonCapacity);
    const s32 count = (accessCount < maxMsgCount ? accessCount : maxMsgCount) + moonCount;

    for (s32 i = 0; i < moonCount; i++) {
        const s32 row = (accessCount < maxMsgCount ? accessCount : maxMsgCount) + i;
        mKingdomChecksEntries[row] = moonEntries[i];
        const char* name = moonEntries[i]->name;
        if (name && name[0])
            msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[row].convertFromMultiByteString(name, strlen(name));
        else
            msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[row].copy(u"(unknown check)");
    }

    if (count == 0)
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer[0].copy(u"Nothing currently reachable");

    mKingdomChecksCount = count > 0 ? count : 1;
    optionsList[MENU_DEBUG_WARP_SUBAREA]->initDataNoResetSelected(mKingdomChecksCount);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->setSelectedIdx(0, 0);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->addStringData(
        msgList[MENU_DEBUG_WARP_SUBAREA]->mBuffer, "TxtContent");
    optionsList[MENU_DEBUG_WARP_SUBAREA]->setEnableData(mDebugWarpEnabled);
    optionsList[MENU_DEBUG_WARP_SUBAREA]->updateParts();
}

bool StageSceneStateModConfig::kingdomChecksRowHasRoute(s32 row) const {
    if (row < 0 || row >= maxMsgCount)
        return false;
    if (mKingdomChecksAccessTargets[row] >= 0)
        return true;
    return mKingdomChecksEntries[row]
        && MoonRando::isLocationInSubArea(mKingdomChecksEntries[row]);
}

void StageSceneStateModConfig::hideKingdomChecksRouteView() {
    mKingdomChecksRouteRow = -1;
    PathHintOverlay::hide();
}

void StageSceneStateModConfig::showKingdomChecksRouteView(s32 row) {
    if (!kingdomChecksRowHasRoute(row))
        return;

    const s32 targetKingdom = mKingdomChecksAccessTargets[row];
    const ShineDB::ShineEntry* location = mKingdomChecksEntries[row];
    char title[128];
    if (targetKingdom >= 0) {
        nn::util::SNPrintf(title, sizeof(title), "Route to %s",
            WorldMeta::getDisplayNameForWorldId(targetKingdom));
    } else {
        nn::util::SNPrintf(title, sizeof(title), "Route to %s",
            location->name && location->name[0] ? location->name : "this check");
    }

    // Scoped to the kingdom whose list this is, and to the movement the player
    // actually has: the check earned its place here by being walkable from this
    // kingdom's front door right now, so that is the route worth showing.
    static MoonRando::AreaRouteSet sRoutes;
    const bool hasRoute = targetKingdom >= 0
        ? MoonRando::buildReachableRoutesToKingdom(
              GameDataHolderAccessor(mGameDataHolder), mDebugWarpSelectedKingdomIndex,
              targetKingdom, &sRoutes)
        : MoonRando::buildReachableRoutesToLocation(
              GameDataHolderAccessor(mGameDataHolder), location,
              mDebugWarpSelectedKingdomIndex, &sRoutes);
    if (hasRoute)
        PathHintOverlay::show(title, sRoutes);
    else
        PathHintOverlay::showMessage(title, "No route could be worked out from this kingdom.");

    mKingdomChecksRouteRow = row;
}

void StageSceneStateModConfig::updateKingdomChecksRouteView() {
    const s32 row = mCurrentList ? mCurrentList->mCurSelected : -1;
    if (mKingdomChecksRouteRow >= 0 && row != mKingdomChecksRouteRow)
        hideKingdomChecksRouteView();

    if (row == mKingdomChecksFooterRow)
        return;
    mKingdomChecksFooterRow = row;
    mMenuFooters[MENU_DEBUG_WARP_SUBAREA]
        ->changeText(makeKingdomChecksFooterText(kingdomChecksRowHasRoute(row)));
}

void StageSceneStateModConfig::exeKingdomChecksList() {
    if (al::isFirstStep(this)) {
        subMenuStart();
        mCurrentList = optionsList[MENU_DEBUG_WARP_SUBAREA];
        mCurrentMenu = menuList[MENU_DEBUG_WARP_SUBAREA];
        al::setPaneString(mCurrentMenu, "TxtOption",
            WorldMeta::sWorlds[mDebugWarpSelectedKingdomIndex].displayName16, 0);
        mMenuFooters[MENU_DEBUG_WARP_SUBAREA]
            ->changeText(makeReadOnlyFooterText());
        updateKingdomChecksOptions();
        hideKingdomChecksRouteView();
        mKingdomChecksFooterRow = -1;
        mCurrentMenu->startAppear("Appear");
        activateInput();
        return;
    }
    if (al::getNerveStep(this) < 2)
        return;

    handleMenuInput();
    updateKingdomChecksRouteView();

    if (rs::isTriggerUiDecide(mHost) && !mIsDecideConfig) {
        const s32 row = mCurrentList ? mCurrentList->mCurSelected : -1;
        if (mKingdomChecksRouteRow == row)
            hideKingdomChecksRouteView();
        else
            showKingdomChecksRouteView(row);
        return;
    }

    if (rs::isTriggerUiCancel(mHost) && !mIsDecideConfig) {
        // B dismisses the route panel first; a second press leaves the list.
        if (mKingdomChecksRouteRow >= 0) {
            hideKingdomChecksRouteView();
            return;
        }
        al::setNerve(this, &NrvStageSceneStateModConfig.DebugWarpKingdom);
        return;
    }
}

void StageSceneStateModConfig::updateDataFromRollParts() {
    if (mCurrentMenu != menuList[MENU_RANDOMIZER_MISC])
        return;
    // The Quality of Life page shares this list but has no roll rows.
    if (mRandoMiscPage == RANDO_MISC_PAGE_QOL)
        return;

    if (mRandoMiscPage == RANDO_MISC_PAGE_SCENARIO_OVERRIDE) {
        // Row indices on this page alias the shared list's normal Misc rows.
        // (both lists share one CommonVerticalList), so this must be handled here
        // and return before the Misc-page checks below ever see it.
        if (mCurrentList->mCurSelected != SCENARIO_OVERRIDE_VALUE)
            return;

        const s32 idx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                             ->mListPartsArr[SCENARIO_OVERRIDE_VALUE + 1])
                            ->mSelectedIdx;
        const s32 scenarioNo = (idx >= 0 && idx < mScenarioOverrideRollOptionsMax)
            ? mScenarioOverrideRollScenarioNos[idx]
            : 0;
        if (scenarioNo > 0)
            Randomizer::setScenarioOverride(mScenarioOverrideWorldId, scenarioNo);
        else
            Randomizer::clearScenarioOverride();
        return;
    }

    if (mCurrentList->mCurSelected == RANDO_MISC_SKYBOXES) {
        // Skybox mode: 0=None 1=Kingdoms 2=Subareas 3=Own Areas 4=Max
        int skyIdx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                          ->mListPartsArr[RANDO_MISC_SKYBOXES + 1])
                         ->mSelectedIdx;
        Randomizer::setSkyboxRandoMode(
            static_cast<Randomizer::SkyboxRandoMode>(skyIdx));
    } else if (mCurrentList->mCurSelected == RANDO_MISC_USE_CUSTOM_MUSIC) {
        const s32 sourceIdx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                   ->mListPartsArr[RANDO_MISC_USE_CUSTOM_MUSIC + 1])
                                  ->mSelectedIdx;
        Randomizer::setMusicSourceMode(
            static_cast<Randomizer::MusicSourceMode>(sourceIdx < 0 ? 0 : (sourceIdx > 3 ? 3 : sourceIdx)));
        Randomizer::logPush("[CustomBgm] misc menu set source roll=%d -> %s", sourceIdx,
            Randomizer::getMusicSourceModeName(Randomizer::getMusicSourceMode()));
    } else if (mCurrentList->mCurSelected == RANDO_MISC_MUSIC) {
        const s32 musicIdx = ((al::RollParts*)optionsList[MENU_RANDOMIZER_MISC]
                                  ->mListPartsArr[RANDO_MISC_MUSIC + 1])
                                 ->mSelectedIdx;
        Randomizer::setMusicRandoMode(
            musicRandoModeFromUiIndex(musicIdx));
    }
}

// ============================================================================
// Navigation helpers
// ============================================================================

void StageSceneStateModConfig::handleMenuInput() {
    mInput->update();
    mCurrentList->update();

    if (mInput->isHoldUiUp() && mInput->isRepeatUiUp()) {
        if (mInput->isTriggerUiUp() && mCurrentList->mCurSelected == mCurrentList->mTopSelectableIdx)
            mCurrentList->jumpBottom();
        mCurrentList->up();
    }

    if (mInput->isHoldUiDown() && mInput->isRepeatUiDown()) {
        if (mInput->isTriggerUiDown() && mCurrentList->mCurSelected == (mCurrentList->mDataCount - 1))
            mCurrentList->jumpTop();
        mCurrentList->down();
    }

    if (mInput->isTriggerUiLeft() || mInput->isTriggerUiRight()) {
        bool isRollRow = false;

        if (mCurrentMenu == menuList[MENU_RANDOMIZER_MISC]) {
            int sel = mCurrentList->mCurSelected;
            isRollRow = mRandoMiscPage == RANDO_MISC_PAGE_MISC
                && (sel == RANDO_MISC_SKYBOXES || sel == RANDO_MISC_MUSIC
                    || sel == RANDO_MISC_USE_CUSTOM_MUSIC);
        }

        if (isRollRow) {
            if (mInput->isTriggerUiLeft())
                mCurrentList->rollLeft();
            if (mInput->isTriggerUiRight())
                mCurrentList->rollRight();
        }
    }

    bool canDecide = true;
    if (mCurrentMenu == menuList[MENU_RANDOMIZER]) {
        canDecide = false;
    } else if (mCurrentMenu == menuList[MENU_DEBUG_WARP_SUBAREA]
        && mKingdomPickerMode == KINGDOM_PICKER_CHECKS) {
        // Kingdom Checks is a browse-only list.
        canDecide = false;
#if SMOR_FEATURE_CHAOS_ANY
    } else if (mCurrentMenu == menuList[MENU_DEBUG_WARP_SUBAREA]
        && mKingdomPickerMode == KINGDOM_PICKER_ENEMY_INSPECTOR) {
        // A commits either selector row as a complete object/replacement
        // pair; action rows use the normal A-to-decide path as well.
        canDecide = true;
#endif
    } else if (mCurrentMenu == menuList[MENU_RANDOMIZER_MISC]) {
        const int sel = mCurrentList->mCurSelected;
        canDecide = (mRandoMiscPage == RANDO_MISC_PAGE_MISC
            && (sel == RANDO_MISC_SKYBOXES
                || sel == RANDO_MISC_STABILIZE_SHADERS
                || sel == RANDO_MISC_SPHINX
                || sel == RANDO_MISC_MUSIC
                || sel == RANDO_MISC_STREAMER_MODE
                || sel == RANDO_MISC_MUSIC_OVERLAY
                || sel == RANDO_MISC_USE_CUSTOM_MUSIC
#if SMOR_FEATURE_CHAOS_ANY
                || sel == RANDO_MISC_FORCE_DISABLE_CHAOS
#endif
                ));
    }

    if (rs::isTriggerUiDecide(mHost) && canDecide)
        deactivateInput();
}

void StageSceneStateModConfig::subMenuStart() {
    mCurrentList->deactivate();
    mCurrentMenu->startEnd("End");
}

void StageSceneStateModConfig::subMenuUpdate() {
    handleMenuInput();
    updateDataFromRollParts();

    if (rs::isTriggerUiCancel(mHost) && !mIsDecideConfig)
        endSubMenu();
}

void StageSceneStateModConfig::endSubMenu() {
#if SMOR_FEATURE_CHAOS_ANY
    EnemySwapDebug::setInspectorOverlayContext(false, nullptr, nullptr);
#endif
    showRandoMiscInfoBox(false);
    hideKingdomChecksRouteView();
    mCurrentList->deactivate();
    mCurrentMenu->startEnd("End");
    mCurrentList = optionsList[MENU_MAIN];
    mCurrentMenu = menuList[MENU_MAIN];
    mCurrentMenu->startAppear("Appear");
    al::startHitReaction(mCurrentMenu, "リセット", 0);
    al::setNerve(this, &NrvStageSceneStateModConfig.MainMenu);
}

void StageSceneStateModConfig::activateInput() {
    mInput->reset();
    mCurrentList->activate();
    mCurrentList->appearCursor();
    mIsDecideConfig = false;
}

void StageSceneStateModConfig::deactivateInput() {
    al::startHitReaction(mCurrentMenu, "決定", 0);
    mCurrentList->endCursor();
    mCurrentList->decide();
    mIsDecideConfig = true;
}
