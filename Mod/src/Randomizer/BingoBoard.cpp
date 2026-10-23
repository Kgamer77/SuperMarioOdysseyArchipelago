#include "Randomizer/BingoBoard.h"

#include "Randomizer/BingoMoonTags.h"
#include "Randomizer/HudTheme.h"
#include "Randomizer/LogicData.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/TrackerOverlay.h"
#include "Randomizer/WorldMeta.h"
#include "SD/SDHooks.h"
#include "ShineDB.h"

#include "Library/Base/StringUtil.h"
#include "System/GameDataFile.h"
#include "al/Library/Controller/InputFunction.h"

#include "imgui.h"
#include "nn/fs.h"
#include "nn/util.h"

#include "Multiplayer/algorithms/crc32.h"
#include <algorithm>
#include <cstddef>
#include <cstring>

namespace Bingo {
    namespace {

        namespace LD = Randomizer::LogicData;

        constexpr u32 cMagic = 0x4F474E42; // BNGO
        constexpr u16 cVersion = 1;
        constexpr s32 cCellCount = 25;
        constexpr s32 cGoalShuffleCapacity = 256;
        constexpr s32 cWorldCount = WorldMeta::sWorldCount;
        constexpr s32 cUniqueKeyCount = 2048;
        constexpr s64 cProgressRefreshFrames = 6;
        constexpr s64 cAutoSaveFrames = 300;
        constexpr s64 cSaveRetryFrames = 60;
        constexpr s64 cProgressPulseFrames = 36;
        constexpr s64 cCompletionPulseFrames = 72;
        // Only aggregate progress types are persisted here. Keep this at the original
        // boundary so adding event-driven goal types does not invalidate saved boards.
        constexpr s32 cSavedBaselineGoalTypeCount = static_cast<s32>(GoalType::WorldPeace) + 1;

#pragma pack(push, 1)
        struct SavedCell {
            GoalInstance goal;
            s16 progress;
            u8 revealed;
            u8 completed;
        };

        struct SavedBoard {
            u32 magic;
            u16 version;
            u16 size;
            u32 crc32;
            u32 seed;
            u8 runMode;
            u8 state;
            u8 winningCell;
            u8 reserved0;
            s64 startPlayTime;
            s64 finishElapsed;
            s32 baseline[cSavedBaselineGoalTypeCount];
            s16 purpleBaseline[cWorldCount];
            s16 peaceBaseline[cWorldCount];
            SavedCell cells[cCellCount];
            u32 uniqueKeys[cUniqueKeyCount];
            u16 uniqueKeyCount;
            s32 shopMoonBaseline;
            u8 reserved[10];
        };
#pragma pack(pop)

        SavedBoard sBoard = {};
        s32 sSlot = -1;
        SaveRedirect::Category sCategory = SaveRedirect::Category::SinglePlayer;
        bool sLoaded = false;
        bool sSessionReady = false;
        bool sInitialMarioControlAvailable = false;
        bool sBoardVisible = true;
        bool sDirty = false;
        bool sConsumedHorizontal = false;
        bool sConsumedStart = false;
        bool sStartChordHeld = false;
        s64 sCurrentPlayTime = 0;
        s64 sNextProgressRefresh = -1;
        s64 sNextAutoSave = -1;
        s64 sProgressPulseUntil[cCellCount] = {};
        s64 sCompletionPulseUntil[cCellCount] = {};
        s32 sLatestCell = -1;
        s32 sRemoteApplyDepth = 0;
        s32 sRemoteBefore[(s32)GoalType::Count] = {};
        s16 sRemotePurpleBefore[cWorldCount] = {};
        s16 sRemotePeaceBefore[cWorldCount] = {};
        s32 sRemoteShopMoonBefore = 0;

        u32 calcCrc(const void* data, size_t size) {
            const u8* bytes = static_cast<const u8*>(data);
            u32 crc = 0xFFFFFFFF;
            for (size_t i = 0; i < size; i++)
                crc = (crc >> 8) ^ crc32::s_Table[(crc ^ bytes[i]) & 0xFF];
            return crc ^ 0xFFFFFFFF;
        }

        u32 boardCrc(const SavedBoard& board) {
            const size_t offset = offsetof(SavedBoard, seed);
            return calcCrc(reinterpret_cast<const u8*>(&board) + offset, sizeof(board) - offset);
        }

        bool isStructurallyValid(const SavedBoard& board) {
            if (board.magic != cMagic || board.version != cVersion
                || board.size != sizeof(SavedBoard) || board.crc32 != boardCrc(board)
                || board.state > static_cast<u8>(BoardState::Won)
                || board.uniqueKeyCount > cUniqueKeyCount)
                return false;
            if (board.runMode == static_cast<u8>(Randomizer::RunMode::Bingo)) {
                if (board.winningCell != 0xFF)
                    return false;
            } else if (board.runMode == static_cast<u8>(Randomizer::RunMode::Ascend)) {
                if (board.winningCell >= 5)
                    return false;
            } else {
                return false;
            }
            for (s32 i = 0; i < cCellCount; i++) {
                const SavedCell& cell = board.cells[i];
                if (!findGoal(cell.goal.definitionId)
                    || static_cast<u8>(cell.goal.type) >= static_cast<u8>(GoalType::Count)
                    || cell.goal.target <= 0 || cell.progress < 0
                    || cell.revealed > 1 || cell.completed > 1)
                    return false;
            }
            return true;
        }

        al::StringTmp<128> makePath(s32 slot, SaveRedirect::Category category, bool temporary = false) {
            const char* root = category == SaveRedirect::Category::Multiplayer
                ? "MPSaves"
                : category == SaveRedirect::Category::MultiplayerGuest ? "MPSaves/_guest"
                                                                       : "Saves";
            return temporary
                ? al::StringTmp<128>("sd:/SMO-Randomizer/%s/Slot%d/board.tmp", root, slot)
                : al::StringTmp<128>("sd:/SMO-Randomizer/%s/Slot%d/board.bin", root, slot);
        }

        bool fileExists(const char* path) {
            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Read).IsFailure())
                return false;
            nn::fs::CloseFile(handle);
            return true;
        }

        bool writeCompleteFile(const char* path, const SavedBoard& board) {
            nn::fs::DeleteFile(path);
            if (nn::fs::CreateFile(path, sizeof(SavedBoard)).IsFailure())
                return false;

            nn::fs::FileHandle handle;
            if (nn::fs::OpenFile(&handle, path, nn::fs::OpenMode_Write).IsFailure())
                return false;

            const bool ok = nn::fs::WriteFile(handle, 0, &board, sizeof(board),
                nn::fs::WriteOption::CreateOption(nn::fs::WriteOptionFlag_Flush))
                                .IsSuccess();
            nn::fs::CloseFile(handle);
            return ok;
        }

        s32 getPurpleCoinCount(const GameDataFile* file, s32 world) {
            if (!file || world < 0 || world >= cWorldCount)
                return 0;
            return file->mCoinCollectGotNum[world];
        }

        s32 getCheckpointCount(GameDataFile* file) {
            if (!file)
                return 0;
            s32 count = 0;
            auto& checkpoints = file->getGotCheckpointTable();
            if (!checkpoints.begin())
                return 0;
            for (s32 i = 0; i < 320; i++)
                if (!checkpoints[i].mStageName.isEmpty()
                    || !checkpoints[i].mObjId.isEmpty())
                    count++;
            return count;
        }

        s32 getShopMoonCount(const GameDataFile* file) {
            if (!file)
                return 0;
            return std::max(0, file->customGetTotalShopShineNum());
        }

        s32 getShopMoonGoalValue(s32 shopMoons) {
            return std::max(0, shopMoons);
        }

        s32 getAssignedMoonKingdom(const ShineDB::ShineEntry* entry) {
            if (!entry)
                return -1;
            const s32 assigned = MoonRando::getAssignedKingdomForEntry(entry);
            return assigned >= 0 ? assigned : entry->worldId;
        }

        u32 nextRandom(u32& state) {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        void shuffle(s16* values, s32 count, u32& rng) {
            for (s32 i = count - 1; i > 0; i--) {
                const s32 j = static_cast<s32>(nextRandom(rng) % static_cast<u32>(i + 1));
                const s16 tmp = values[i];
                values[i] = values[j];
                values[j] = tmp;
            }
        }

        bool isKnownKey(u32 key) {
            if (!key)
                return false;
            for (s32 i = 0; i < sBoard.uniqueKeyCount; i++)
                if (sBoard.uniqueKeys[i] == key)
                    return true;
            return false;
        }

        void setCellGoal(SavedCell* cell, const GoalDefinition& goal) {
            cell->goal.definitionId = goal.id;
            cell->goal.type = goal.type;
            cell->goal.difficulty = goal.difficulty;
            cell->goal.target = goal.target;
            cell->goal.subject = goal.subject;
            cell->goal.subjectUid = goal.subjectUid;
        }

        bool rememberKey(u32 key) {
            if (!key || isKnownKey(key) || sBoard.uniqueKeyCount >= cUniqueKeyCount)
                return false;
            sBoard.uniqueKeys[sBoard.uniqueKeyCount++] = key;
            return true;
        }

        void revealNeighbors(s32 index) {
            const s32 row = index / 5;
            const s32 col = index % 5;
            if (row > 0)
                sBoard.cells[index - 5].revealed = 1;
            if (row < 4)
                sBoard.cells[index + 5].revealed = 1;
            if (col > 0)
                sBoard.cells[index - 1].revealed = 1;
            if (col < 4)
                sBoard.cells[index + 1].revealed = 1;
        }

        bool hasBingo() {
            for (s32 row = 0; row < 5; row++) {
                bool complete = true;
                for (s32 col = 0; col < 5; col++)
                    complete &= sBoard.cells[row * 5 + col].completed != 0;
                if (complete)
                    return true;
            }
            for (s32 col = 0; col < 5; col++) {
                bool complete = true;
                for (s32 row = 0; row < 5; row++)
                    complete &= sBoard.cells[row * 5 + col].completed != 0;
                if (complete)
                    return true;
            }
            bool down = true, up = true;
            for (s32 i = 0; i < 5; i++) {
                down &= sBoard.cells[i * 5 + i].completed != 0;
                up &= sBoard.cells[i * 5 + (4 - i)].completed != 0;
            }
            return down || up;
        }

        bool isWinningBingoCell(s32 index) {
            if (sBoard.state != static_cast<u8>(BoardState::Won)
                || Randomizer::getRunMode() != Randomizer::RunMode::Bingo)
                return false;
            const s32 row = index / 5;
            const s32 col = index % 5;
            bool rowWon = true, colWon = true;
            for (s32 i = 0; i < 5; i++) {
                rowWon &= sBoard.cells[row * 5 + i].completed != 0;
                colWon &= sBoard.cells[i * 5 + col].completed != 0;
            }
            bool diagonal = false;
            if (row == col) {
                diagonal = true;
                for (s32 i = 0; i < 5; i++)
                    diagonal &= sBoard.cells[i * 5 + i].completed != 0;
            }
            bool antiDiagonal = false;
            if (row + col == 4) {
                antiDiagonal = true;
                for (s32 i = 0; i < 5; i++)
                    antiDiagonal &= sBoard.cells[i * 5 + (4 - i)].completed != 0;
            }
            return rowWon || colWon || diagonal || antiDiagonal;
        }

        void noteCellProgress(s32 index, s32 previousProgress) {
            if (index < 0 || index >= cCellCount
                || sBoard.cells[index].progress <= previousProgress)
                return;
            sProgressPulseUntil[index] = sCurrentPlayTime + cProgressPulseFrames;
            if (sBoard.cells[index].revealed)
                sLatestCell = index;
        }

        void noteCellCompletion(s32 index) {
            if (index < 0 || index >= cCellCount)
                return;
            sCompletionPulseUntil[index] = sCurrentPlayTime + cCompletionPulseFrames;
            if (sBoard.cells[index].revealed)
                sLatestCell = index;
        }

        float animationStrength(s64 until, s64 duration) {
            if (until <= sCurrentPlayTime || duration <= 0)
                return 0.0f;
            return std::min(1.0f,
                static_cast<float>(until - sCurrentPlayTime) / static_cast<float>(duration));
        }

        void evaluateCompletion(GameDataFile* file) {
            bool changed;
            do {
                changed = false;
                for (s32 i = 0; i < cCellCount; i++) {
                    SavedCell& cell = sBoard.cells[i];
                    const GoalDefinition* goal = findGoal(cell.goal.definitionId);
                    if (!goal || cell.completed || cell.progress < cell.goal.target)
                        continue;
                    if (Randomizer::getRunMode() == Randomizer::RunMode::Ascend && !cell.revealed)
                        continue;
                    cell.completed = 1;
                    noteCellCompletion(i);
                    changed = true;
                    if (Randomizer::getRunMode() == Randomizer::RunMode::Ascend)
                        revealNeighbors(i);
                }
            } while (changed);

            const bool won = Randomizer::getRunMode() == Randomizer::RunMode::Bingo
                ? hasBingo()
                : sBoard.winningCell < cCellCount && sBoard.cells[sBoard.winningCell].completed;
            if (won && sBoard.state != static_cast<u8>(BoardState::Won)) {
                sBoard.state = static_cast<u8>(BoardState::Won);
                const s64 now = file ? file->getPlayTimeTotal() : sCurrentPlayTime;
                sBoard.finishElapsed = std::max<s64>(0, now - sBoard.startPlayTime);
                sBoardVisible = true;
                sDirty = true;
                save();
            }
        }

        s32 currentValue(GoalType type, GameDataFile* file) {
            if (!file)
                return 0;
            switch (type) {
            case GoalType::CoinsEarned:
                return file->getTotalCoinNum();
            case GoalType::Checkpoints:
                return getCheckpointCount(file);
            default:
                return 0;
            }
        }

        void captureBaselines(GameDataFile* file) {
            for (s32 i = 0; i < cSavedBaselineGoalTypeCount; i++)
                sBoard.baseline[i] = currentValue(static_cast<GoalType>(i), file);
            for (s32 world = 0; world < cWorldCount; world++) {
                sBoard.purpleBaseline[world] = getPurpleCoinCount(file, world);
                sBoard.peaceBaseline[world] = file
                    ? file->getScenarioNo(world)
                    : Randomizer::getInitialKingdomScenario(world);
            }
            sBoard.shopMoonBaseline = getShopMoonCount(file);
        }

        void refreshProgress(GameDataFile* file) {
            for (s32 i = 0; i < cCellCount; i++) {
                SavedCell& cell = sBoard.cells[i];
                const GoalDefinition* goal = findGoal(cell.goal.definitionId);
                if (!goal)
                    continue;
                const s32 previousProgress = cell.progress;
                s32 progress = cell.progress;
                if (cell.goal.type == GoalType::PurpleCoins && cell.goal.subject >= 0 && cell.goal.subject < cWorldCount) {
                    progress = std::max(progress,
                        getPurpleCoinCount(file, cell.goal.subject) - sBoard.purpleBaseline[cell.goal.subject]);
                } else if (cell.goal.type == GoalType::ShopMoons) {
                    const s32 boughtMoons = getShopMoonCount(file) - sBoard.shopMoonBaseline;
                    progress = std::max(progress, getShopMoonGoalValue(boughtMoons));
                } else if (cell.goal.type == GoalType::WorldPeace && cell.goal.subject >= 0 && cell.goal.subject < cWorldCount) {
                    const s32 peace = LD::sPeaceScenarioByKingdom[cell.goal.subject];
                    progress = sBoard.peaceBaseline[cell.goal.subject] < peace && file->getScenarioNo(cell.goal.subject) >= peace ? 1 : progress;
                } else if (cell.goal.type == GoalType::Checkpoints) {
                    // This count is reconstructed from the saved table rather
                    // than the legacy counter, so repair an over-counted cell
                    // from an older board as soon as the board refreshes.
                    const s32 value = currentValue(cell.goal.type, file);
                    const s32 typeIndex = static_cast<s32>(cell.goal.type);
                    if (typeIndex < cSavedBaselineGoalTypeCount)
                        progress = std::max(0, value - sBoard.baseline[typeIndex]);
                } else {
                    const s32 value = currentValue(cell.goal.type, file);
                    const s32 typeIndex = static_cast<s32>(cell.goal.type);
                    if (typeIndex < cSavedBaselineGoalTypeCount
                        && (value || sBoard.baseline[typeIndex]))
                        progress = std::max(progress, value - sBoard.baseline[typeIndex]);
                }
                progress = std::max(0, std::min<s32>(progress, cell.goal.target));
                if (cell.progress != progress) {
                    cell.progress = static_cast<s16>(progress);
                    noteCellProgress(i, previousProgress);
                    sDirty = true;
                }
            }
            evaluateCompletion(file);
        }

        void formatTime(s64 frames, char* out, size_t size) {
            const s64 cs = std::max<s64>(0, frames) * 100 / 60;
            const s32 h = static_cast<s32>(cs / 360000);
            const s32 m = static_cast<s32>((cs / 6000) % 60);
            const s32 s = static_cast<s32>((cs / 100) % 60);
            const s32 c = static_cast<s32>(cs % 100);
            if (h)
                nn::util::SNPrintf(out, size, "%d:%02d:%02d.%02d", h, m, s, c);
            else
                nn::util::SNPrintf(out, size, "%d:%02d.%02d", m, s, c);
        }

        constexpr s32 cMaxBoardLabelLines = 3;

        struct WrappedTextLayout {
            const char* starts[cMaxBoardLabelLines] = {};
            const char* ends[cMaxBoardLabelLines] = {};
            s32 lineCount = 0;
            bool complete = false;
            bool splitWord = false;
        };

        bool isWrapWhitespace(char c) {
            return c == ' ' || c == '\n' || c == '\r' || c == '\t';
        }

        WrappedTextLayout makeWrappedTextLayout(ImFont* font, float fontSize,
            const char* text, float wrapWidth) {
            WrappedTextLayout layout;
            const char* cursor = text;
            const char* textEnd = text + std::strlen(text);
            const float fontScale = font->LegacySize > 0.0f
                ? fontSize / font->LegacySize
                : 1.0f;

            while (cursor < textEnd && layout.lineCount < cMaxBoardLabelLines) {
                const char* wrapEnd = font->CalcWordWrapPositionA(
                    fontScale, cursor, textEnd, wrapWidth);
                if (wrapEnd <= cursor)
                    wrapEnd = cursor + 1;
                if (wrapEnd < textEnd && !isWrapWhitespace(wrapEnd[-1])
                    && !isWrapWhitespace(*wrapEnd))
                    layout.splitWord = true;

                const char* visibleEnd = wrapEnd;
                while (visibleEnd > cursor && isWrapWhitespace(visibleEnd[-1]))
                    visibleEnd--;
                layout.starts[layout.lineCount] = cursor;
                layout.ends[layout.lineCount] = visibleEnd;
                layout.lineCount++;
                cursor = wrapEnd;
                while (cursor < textEnd && isWrapWhitespace(*cursor))
                    cursor++;
            }
            layout.complete = cursor >= textEnd;
            return layout;
        }

        void drawCenteredWrappedText(ImDrawList* dl, ImFont* font, float fontSize,
            ImU32 color, const char* text, const ImVec2& min, const ImVec2& max) {
            if (!dl || !font || !text || !text[0] || max.x <= min.x || max.y <= min.y)
                return;

            const float wrapWidth = max.x - min.x;
            const float availableHeight = max.y - min.y;
            // Keep the usual label size where possible, but make a cell's text smaller
            // when it would otherwise split a word, exceed three lines, or be clipped.
            const float minimumFontSize = fontSize * (8.0f / 15.0f);
            const float fontStep = std::max(0.25f, fontSize * 0.05f);
            WrappedTextLayout layout;
            float fittedFontSize = fontSize;
            for (;;) {
                layout = makeWrappedTextLayout(font, fittedFontSize, text, wrapWidth);
                const float blockHeight = fittedFontSize * 1.05f * layout.lineCount;
                if ((layout.complete && !layout.splitWord && blockHeight <= availableHeight)
                    || fittedFontSize <= minimumFontSize)
                    break;
                fittedFontSize = std::max(minimumFontSize, fittedFontSize - fontStep);
            }

            const float lineHeight = fittedFontSize * 1.05f;
            const float blockHeight = lineHeight * layout.lineCount;
            float textY = min.y + std::max(0.0f, (availableHeight - blockHeight) * 0.5f);
            dl->PushClipRect(min, max, true);
            for (s32 i = 0; i < layout.lineCount; i++) {
                const ImVec2 size = font->CalcTextSizeA(fittedFontSize, 3.402823e38F, 0.0f,
                    layout.starts[i], layout.ends[i]);
                const float textX = min.x + std::max(0.0f, (wrapWidth - size.x) * 0.5f);
                dl->AddText(font, fittedFontSize, ImVec2(textX, textY), color,
                    layout.starts[i], layout.ends[i]);
                textY += lineHeight;
            }
            dl->PopClipRect();
        }

        void drawControlHint(ImDrawList* dl, ImFont* font, float scale,
            float x, float y, const char* prompt) {
            const float fontSize = 14.0f * scale;
            const ImVec2 textSize = font->CalcTextSizeA(
                fontSize, 3.402823e38F, 0.0f, prompt);
            const float padX = 10.0f * scale;
            const float padY = 6.0f * scale;
            const ImVec2 min(x, y);
            const ImVec2 max(x + textSize.x + padX * 2.0f,
                y + textSize.y + padY * 2.0f);
            const HudTheme::Palette theme = HudTheme::get();
            dl->AddRectFilled(min, max, IM_COL32(8, 10, 16, 205), 6.0f * scale);
            dl->AddRect(min, max, theme.border, 6.0f * scale, 0, 1.5f * scale);
            dl->AddText(font, fontSize,
                ImVec2(min.x + padX, min.y + padY),
                theme.heading, prompt);
        }

    } // namespace

    void reset() {
        std::memset(&sBoard, 0, sizeof(sBoard));
        sSlot = -1;
        sCategory = SaveRedirect::Category::SinglePlayer;
        sLoaded = false;
        sSessionReady = false;
        sInitialMarioControlAvailable = false;
        sBoardVisible = true;
        sDirty = false;
        sConsumedHorizontal = false;
        sConsumedStart = false;
        sStartChordHeld = false;
        sCurrentPlayTime = 0;
        sNextProgressRefresh = -1;
        sNextAutoSave = -1;
        std::memset(sProgressPulseUntil, 0, sizeof(sProgressPulseUntil));
        std::memset(sCompletionPulseUntil, 0, sizeof(sCompletionPulseUntil));
        sLatestCell = -1;
        sRemoteApplyDepth = 0;
    }

    bool generate(s32 slot, SaveRedirect::Category category) {
        if (!Randomizer::isBoardRunMode()) {
            remove(slot, category);
            return true;
        }
        reset();
        sSlot = slot;
        sCategory = category;
        sBoard.magic = cMagic;
        sBoard.version = cVersion;
        sBoard.size = sizeof(SavedBoard);
        sBoard.seed = Randomizer::getSeed();
        sBoard.runMode = static_cast<u8>(Randomizer::getRunMode());
        sBoard.state = static_cast<u8>(BoardState::Ready);
        sBoard.winningCell = 0xFF;

        if (getGoalCount() > cGoalShuffleCapacity)
            return false;
        s16 all[cGoalShuffleCapacity];
        s32 allCount = 0;
        for (s32 i = 0; i < getGoalCount(); i++) {
            const GoalDefinition& goal = getGoals()[i];
            if (!isEligible(goal))
                continue;
            all[allCount++] = i;
        }
        u32 rng = sBoard.seed ^ (sBoard.runMode == static_cast<u8>(Randomizer::RunMode::Bingo) ? 0xB16B00B5u : 0xA5CE4D21u);
        if (!rng)
            rng = 0x6D2B79F5u;
        shuffle(all, allCount, rng);

        // Difficulty remains serialized with old boards, but generation now draws
        // every cell from one uniformly shuffled pool.
        if (allCount < cCellCount)
            return false;
        for (s32 i = 0; i < cCellCount; i++) {
            setCellGoal(&sBoard.cells[i], getGoals()[all[i]]);
            sBoard.cells[i].revealed = 0;
        }
        if (Randomizer::getRunMode() == Randomizer::RunMode::Ascend)
            sBoard.winningCell = static_cast<u8>(nextRandom(rng) % 5);
        sLoaded = true;
        sBoardVisible = true;
        return save();
    }

    bool save() {
        if (!sLoaded || sSlot < 1)
            return false;
        sBoard.crc32 = boardCrc(sBoard);
        const auto tmp = makePath(sSlot, sCategory, true);
        const auto path = makePath(sSlot, sCategory);
        if (!writeCompleteFile(tmp.cstr(), sBoard))
            return false;
        if (!writeCompleteFile(path.cstr(), sBoard))
            return false;
        nn::fs::DeleteFile(tmp.cstr());
        sDirty = false;
        return true;
    }

    bool flushIfDirty() {
        return !sDirty || save();
    }

    bool validate(s32 slot, u32 seed, u8 runMode, SaveRedirect::Category category) {
        if (runMode != static_cast<u8>(Randomizer::RunMode::Bingo)
            && runMode != static_cast<u8>(Randomizer::RunMode::Ascend))
            return true;
        const auto path = makePath(slot, category);
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false;
        SavedBoard board = {};
        const bool ok = nn::fs::ReadFile(handle, 0, &board, sizeof(board)).IsSuccess();
        nn::fs::CloseFile(handle);
        return ok && isStructurallyValid(board) && board.seed == seed
            && board.runMode == runMode;
    }

    bool load(s32 slot, SaveRedirect::Category category) {
        reset();
        const auto path = makePath(slot, category);
        nn::fs::FileHandle handle;
        if (nn::fs::OpenFile(&handle, path.cstr(), nn::fs::OpenMode_Read).IsFailure())
            return false;
        const bool ok = nn::fs::ReadFile(handle, 0, &sBoard, sizeof(sBoard)).IsSuccess();
        nn::fs::CloseFile(handle);
        if (!ok || !isStructurallyValid(sBoard)
            || sBoard.seed != Randomizer::getSeed()
            || sBoard.runMode != static_cast<u8>(Randomizer::getRunMode())) {
            reset();
            return false;
        }
        sSlot = slot;
        sCategory = category;
        sLoaded = true;
        sBoardVisible = true;
        return true;
    }

    bool remove(s32 slot, SaveRedirect::Category category) {
        const auto path = makePath(slot, category);
        const auto tmp = makePath(slot, category, true);
        bool ok = true;
        if (fileExists(path.cstr()))
            ok = nn::fs::DeleteFile(path.cstr()).IsSuccess() && ok;
        if (fileExists(tmp.cstr()))
            ok = nn::fs::DeleteFile(tmp.cstr()).IsSuccess() && ok;
        if (sSlot == slot && sCategory == category)
            reset();
        return ok;
    }

    void update(GameDataFile* file) {
        if (!sLoaded || !Randomizer::isBoardRunMode() || !file)
            return;
        sSessionReady = true;
        sCurrentPlayTime = file->getPlayTimeTotal();
        if (sBoard.state != static_cast<u8>(BoardState::Active))
            return;
        if (sBoard.startPlayTime < 0) {
            captureBaselines(file);
            sBoard.startPlayTime = sCurrentPlayTime;
            sDirty = true;
            const bool saved = save();
            sNextProgressRefresh = sCurrentPlayTime;
            sNextAutoSave = sCurrentPlayTime
                + (saved ? cAutoSaveFrames : cSaveRetryFrames);
        }
        // Aggregate fallbacks do not need frame-perfect polling. Event-driven goals
        // still update immediately through recordEvent()/recordMoon().
        if (sNextProgressRefresh < 0 || sCurrentPlayTime >= sNextProgressRefresh) {
            refreshProgress(file);
            sNextProgressRefresh = sCurrentPlayTime + cProgressRefreshFrames;
        }
        if (sNextAutoSave < 0)
            sNextAutoSave = sCurrentPlayTime + cAutoSaveFrames;
        if (sDirty && sCurrentPlayTime >= sNextAutoSave) {
            sNextAutoSave = sCurrentPlayTime
                + (save() ? cAutoSaveFrames : cSaveRetryFrames);
        }
    }

    void notifyMarioControlAvailable() {
        if (sLoaded && Randomizer::isBoardRunMode())
            sInitialMarioControlAvailable = true;
    }

    void handleInput() {
        sConsumedHorizontal = false;
        sConsumedStart = false;
        if (!sLoaded || !sSessionReady || !sInitialMarioControlAvailable
            || !Randomizer::isBoardRunMode())
            return;

        const bool zl = al::isPadHoldZL(-1);
        const bool zr = al::isPadHoldZR(-1);
        const bool startChord = zl && zr;
        const bool startTriggered = startChord && !sStartChordHeld;
        sStartChordHeld = startChord;
        if (!TrackerOverlay::isHudConditionActive())
            return;

        if (sBoard.state == static_cast<u8>(BoardState::Ready)) {
            if (!startTriggered)
                return;
            sConsumedStart = true;
            // Baselines are captured by update() on the first active frame, when the
            // StageScene's GameDataFile is available.
            sBoard.state = static_cast<u8>(BoardState::Active);
            for (s32 i = 0; i < cCellCount; i++)
                sBoard.cells[i].revealed = Randomizer::getRunMode() == Randomizer::RunMode::Bingo || i >= 20;
            sBoard.startPlayTime = -1;
            sBoardVisible = true;
            sDirty = true;
            return;
        }

        if (zl && (al::isPadTriggerLeft(-1) || al::isPadTriggerRight(-1))) {
            sConsumedHorizontal = true;
            sBoardVisible = !sBoardVisible;
        }
    }

    void recordEvent(const GoalEvent& event) {
        if (!sLoaded || sBoard.state != static_cast<u8>(BoardState::Active)
            || event.origin != EventOrigin::Local || sRemoteApplyDepth > 0)
            return;
        bool hasMatch = false;
        for (s32 i = 0; i < cCellCount; i++) {
            const SavedCell& cell = sBoard.cells[i];
            if (findGoal(cell.goal.definitionId) && cell.goal.type == event.type
                && (cell.goal.subject < 0 || cell.goal.subject == event.subject)) {
                hasMatch = true;
                break;
            }
        }
        if (!hasMatch || (event.uniqueKey && !rememberKey(event.uniqueKey)))
            return;
        for (s32 i = 0; i < cCellCount; i++) {
            SavedCell& cell = sBoard.cells[i];
            const GoalDefinition* goal = findGoal(cell.goal.definitionId);
            if (!goal || cell.goal.type != event.type)
                continue;
            if (cell.goal.subject >= 0 && cell.goal.subject != event.subject)
                continue;
            const s32 previousProgress = cell.progress;
            cell.progress = static_cast<s16>(std::min<s32>(cell.goal.target,
                cell.progress + std::max(0, event.amount)));
            noteCellProgress(i, previousProgress);
            sDirty = true;
        }
        evaluateCompletion(nullptr);
    }

    void recordMoon(const ShineDB::ShineEntry* physical, const ShineDB::ShineEntry* item,
        bool alreadyCollected, EventOrigin origin) {
        if (!sLoaded || sBoard.state != static_cast<u8>(BoardState::Active)
            || !physical || alreadyCollected || origin != EventOrigin::Local
            || sRemoteApplyDepth > 0)
            return;
        if (!item)
            item = physical;
        const u32 key = 0x80000000u ^ static_cast<u32>(ShineDB::findIndex(physical));
        if (!rememberKey(key))
            return;
        const bool story = (physical->flags & ShineDB::ShineFlag_Story) != 0;
        const bool multi = (physical->flags & ShineDB::ShineFlag_Multi) != 0;
        // "Collect N triple moons" tracks the seed's actual triples, honoring
        // Randomize Triple Moons (the reward you got, not the vanilla flag). The
        // specific-named grand goals below stay keyed on the physical moon so they
        // still complete by collecting that exact moon.
        const bool rewardMulti = MoonRando::isRewardMultiEntry(item);
        const u32 tags = getPhysicalMoonTags(*physical);
        const s32 itemKingdom = getAssignedMoonKingdom(item);
        u32 bossEncounterKey = 0;
        const s32 boss = getBossForPhysicalMoon(*physical, &bossEncounterKey);
        (void)bossEncounterKey;
        for (s32 i = 0; i < cCellCount; i++) {
            SavedCell& cell = sBoard.cells[i];
            const GoalDefinition* goal = findGoal(cell.goal.definitionId);
            if (!goal)
                continue;
            const GoalInstance& instance = cell.goal;
            bool match = instance.type == GoalType::KingdomMoonLocations
                && instance.subject == physical->worldId;
            match |= instance.type == GoalType::KingdomMoonItems
                && instance.subject == itemKingdom;
            match |= instance.type == GoalType::StoryMoons && story;
            match |= instance.type == GoalType::TripleMoons && rewardMulti;
            match |= instance.type == GoalType::SpecificStoryMoon && story
                && instance.subject == physical->worldId && instance.subjectUid == physical->uid;
            match |= instance.type == GoalType::SpecificTripleMoon && multi
                && instance.subject == physical->worldId && instance.subjectUid == physical->uid;
            match |= instance.type == GoalType::MoonRockMoons
                && (tags & PhysicalMoonTag_MoonRock) != 0;
            match |= instance.type == GoalType::ChestMoons && (tags & PhysicalMoonTag_Chest) != 0;
            match |= instance.type == GoalType::TimerMoons && (tags & PhysicalMoonTag_Timer) != 0;
            match |= instance.type == GoalType::KoopaRaceMoons && (tags & PhysicalMoonTag_Race) != 0;
            match |= instance.type == GoalType::NoteMoons && (tags & PhysicalMoonTag_Note) != 0;
            match |= instance.type == GoalType::CaptainToadMoons && (tags & PhysicalMoonTag_CaptainToad) != 0;
            match |= instance.type == GoalType::TwoDMoons && (tags & PhysicalMoonTag_TwoD) != 0;
            match |= instance.type == GoalType::GroundPoundMoons && (tags & PhysicalMoonTag_GroundPound) != 0;
            match |= instance.type == GoalType::CrateMoons && (tags & PhysicalMoonTag_Crate) != 0;
            match |= instance.type == GoalType::ShardMoons && (tags & PhysicalMoonTag_Shards) != 0;
            match |= instance.type == GoalType::NutMoons && (tags & PhysicalMoonTag_Nut) != 0;
            match |= instance.type == GoalType::SeedMoons && (tags & PhysicalMoonTag_Seed) != 0;
            match |= instance.type == GoalType::SheepHerdingMoons
                && (tags & PhysicalMoonTag_SheepHerding) != 0;
            match |= instance.type == GoalType::GoombetteMoons
                && (tags & PhysicalMoonTag_Goombette) != 0;
            match |= instance.type == GoalType::LakituMoons && (tags & PhysicalMoonTag_Lakitu) != 0;
            match |= instance.type == GoalType::RabbitMoons && (tags & PhysicalMoonTag_Rabbit) != 0;
            match |= instance.type == GoalType::BirdMoons && (tags & PhysicalMoonTag_Bird) != 0;
            match |= instance.type == GoalType::HideAndSeekMoons
                && (tags & PhysicalMoonTag_HideAndSeek) != 0;
            match |= instance.type == GoalType::HintArtMoons
                && (physical->flags & ShineDB::ShineFlag_HintArt) != 0;
            match |= instance.type == GoalType::SlotsMoons && (tags & PhysicalMoonTag_Slots) != 0;
            match |= instance.type == GoalType::KoopaTraceWalkingMoons
                && (tags & PhysicalMoonTag_KoopaTraceWalking) != 0;
            match |= instance.type == GoalType::BossEncounters && boss >= 0;
            match |= instance.type == GoalType::SpecificBoss && boss >= 0
                && instance.subject == boss;
            if (match) {
                const s32 previousProgress = cell.progress;
                cell.progress = static_cast<s16>(std::min<s32>(cell.goal.target,
                    cell.progress + 1));
                noteCellProgress(i, previousProgress);
                sDirty = true;
            }
        }
        evaluateCompletion(nullptr);
    }

    void recordWorldPeace(s32 worldId, EventOrigin origin) {
        recordEvent({ GoalType::WorldPeace, 1, worldId, 0x70000000u | static_cast<u32>(worldId), origin });
    }

    void beginRemoteApply(GameDataFile* file) {
        if (sRemoteApplyDepth++ > 0 || !sLoaded
            || sBoard.state != static_cast<u8>(BoardState::Active) || !file)
            return;

        // Commit any local aggregate changes before taking the remote snapshot.
        refreshProgress(file);
        for (s32 i = 0; i < static_cast<s32>(GoalType::Count); i++)
            sRemoteBefore[i] = currentValue(static_cast<GoalType>(i), file);
        for (s32 world = 0; world < cWorldCount; world++) {
            sRemotePurpleBefore[world] = getPurpleCoinCount(file, world);
            sRemotePeaceBefore[world] = file->getScenarioNo(world);
        }
        sRemoteShopMoonBefore = getShopMoonCount(file);
    }

    void endRemoteApply(GameDataFile* file) {
        if (sRemoteApplyDepth <= 0)
            return;
        if (--sRemoteApplyDepth > 0 || !sLoaded
            || sBoard.state != static_cast<u8>(BoardState::Active) || !file)
            return;

        for (s32 i = 0; i < cSavedBaselineGoalTypeCount; i++) {
            const s32 after = currentValue(static_cast<GoalType>(i), file);
            if (after > sRemoteBefore[i])
                sBoard.baseline[i] += after - sRemoteBefore[i];
        }
        for (s32 world = 0; world < cWorldCount; world++) {
            const s32 purple = getPurpleCoinCount(file, world);
            if (purple > sRemotePurpleBefore[world])
                sBoard.purpleBaseline[world] += purple - sRemotePurpleBefore[world];
            const s32 scenario = file->getScenarioNo(world);
            if (scenario > sRemotePeaceBefore[world])
                sBoard.peaceBaseline[world] = std::max<s16>(
                    sBoard.peaceBaseline[world], static_cast<s16>(scenario));
        }
        const s32 shopMoonCount = getShopMoonCount(file);
        if (shopMoonCount > sRemoteShopMoonBefore)
            sBoard.shopMoonBaseline += shopMoonCount - sRemoteShopMoonBefore;
        sDirty = true;
    }

    bool shouldSuppressTracker() {
        return Randomizer::isBoardRunMode() && sLoaded && sSessionReady
            && sInitialMarioControlAvailable && sBoardVisible;
    }
    bool shouldBlockGameplayInput() {
        return Randomizer::isBoardRunMode() && sLoaded && sSessionReady
            // Input-gate hooks also service pause-menu controls.  Keep the board's
            // ready/start lock scoped to the live stage HUD, where Mario input is
            // actually being handled.
            && sInitialMarioControlAvailable && TrackerOverlay::isHudConditionActive()
            && (sBoard.state == static_cast<u8>(BoardState::Ready) || sConsumedStart);
    }
    bool consumeHorizontalTrigger() { return sConsumedHorizontal; }
    bool consumeStartTrigger() { return sConsumedStart; }
    BoardState getState() { return static_cast<BoardState>(sBoard.state); }
    bool isBoardVisible() { return sBoardVisible; }

    void draw() {
        if (!sLoaded || !sSessionReady || !sInitialMarioControlAvailable
            || !Randomizer::isBoardRunMode())
            return;
        ImGuiIO& io = ImGui::GetIO();
        if (io.DisplaySize.x <= 0 || io.DisplaySize.y <= 0)
            return;
        ImDrawList* dl = ImGui::GetForegroundDrawList();
        ImFont* font = ImGui::GetFont();
        const float hudScale = TrackerOverlay::getHudScale();
        if (!TrackerOverlay::isHudConditionActive())
            return;

        if (!sBoardVisible) {
            const bool trackerEnabled = Randomizer::isTrackerOverlayEnabled();
            const char* prompt = trackerEnabled
                ? "ZL + D-Pad Left/Right: Board"
                : "ZL + D-Pad Left/Right: Show board";
            const float trackerBottom = TrackerOverlay::getLastPanelBottom();
            const float promptY = trackerEnabled && trackerBottom > 0.0f
                ? trackerBottom + 6.0f * hudScale
                : TrackerOverlay::getPanelTop();
            drawControlHint(dl, font, hudScale, 24.0f * hudScale, promptY, prompt);
            return;
        }

        const float boardFactor = Randomizer::getBoardScaleFactor();
        const float scale = hudScale * boardFactor;
        const bool ready = sBoard.state == static_cast<u8>(BoardState::Ready);
        const float x = 16.0f * hudScale;
        const float y = TrackerOverlay::getPanelTop();
        const float width = std::min(io.DisplaySize.x * 0.31f,
                                480.0f * hudScale)
            * boardFactor;
        const float header = 48.0f * scale;
        const float cellWidth = width / 5.0f;
        const float cellHeight = 76.0f * scale;
        const float footer = 8.0f * scale;
        const float height = header + cellHeight * 5.0f + footer;
        dl->AddRectFilled(ImVec2(x, y), ImVec2(x + width, y + height),
            IM_COL32(8, 10, 16, 185), 8.0f * scale);
        dl->AddRectFilled(ImVec2(x + 1.0f * scale, y + 1.0f * scale),
            ImVec2(x + width - 1.0f * scale, y + header),
            IM_COL32(15, 19, 29, 210), 8.0f * scale);
        const HudTheme::Palette theme = HudTheme::get();
        dl->AddRect(ImVec2(x, y), ImVec2(x + width, y + height),
            theme.border, 8.0f * scale, 0, 1.5f * scale);
        dl->AddLine(ImVec2(x + 8.0f * scale, y + header),
            ImVec2(x + width - 8.0f * scale, y + header),
            theme.divider, 1.0f * scale);

        const char* title = Randomizer::getRunMode() == Randomizer::RunMode::Bingo ? "BINGO" : "ASCEND";
        dl->AddText(font, 20.0f * scale,
            ImVec2(x + 10.0f * scale, y + 7.0f * scale),
            theme.heading, title);

        char time[32];
        const s64 elapsed = sBoard.state == static_cast<u8>(BoardState::Won)
            ? sBoard.finishElapsed
            : (sBoard.state == static_cast<u8>(BoardState::Active) && sBoard.startPlayTime >= 0
                      ? std::max<s64>(0, sCurrentPlayTime - sBoard.startPlayTime)
                      : 0);
        if (ready)
            std::strcpy(time, "0:00");
        else
            formatTime(elapsed, time, sizeof(time));
        const float timeFontSize = 19.0f * scale;
        const ImVec2 timeSize = font->CalcTextSizeA(
            timeFontSize, 3.402823e38F, 0.0f, time);
        dl->AddText(font, timeFontSize,
            ImVec2(x + width - timeSize.x - 10.0f * scale, y + 7.0f * scale),
            IM_COL32(130, 205, 255, 255), time);
        if (sBoard.state == static_cast<u8>(BoardState::Won)) {
            dl->AddText(font, 12.5f * scale,
                ImVec2(x + 10.0f * scale, y + 29.0f * scale),
                IM_COL32(100, 235, 125, 255), "RUN COMPLETE");
        } else if (sLatestCell >= 0 && sLatestCell < cCellCount) {
            const SavedCell& latest = sBoard.cells[sLatestCell];
            const GoalDefinition* latestGoal = findGoal(latest.goal.definitionId);
            if (latestGoal && latest.revealed) {
                char latestText[128];
                nn::util::SNPrintf(latestText, sizeof(latestText),
                    "LATEST: %s  %d/%d", latestGoal->shortLabel,
                    latest.progress, latest.goal.target);
                dl->PushClipRect(
                    ImVec2(x + 10.0f * scale, y + 27.0f * scale),
                    ImVec2(x + width - 10.0f * scale, y + header - 2.0f * scale), true);
                dl->AddText(font, 12.5f * scale,
                    ImVec2(x + 10.0f * scale, y + 29.0f * scale),
                    IM_COL32(140, 205, 255, 255), latestText);
                dl->PopClipRect();
            }
        }

        for (s32 i = 0; i < cCellCount; i++) {
            const s32 row = i / 5, col = i % 5;
            const float cx = x + col * cellWidth;
            const float cy = y + header + row * cellHeight;
            const SavedCell& saved = sBoard.cells[i];
            const bool visible = !ready && saved.revealed;
            const bool winningLine = isWinningBingoCell(i);
            const bool winningTarget = i == sBoard.winningCell && visible;
            const float progressPulse = animationStrength(
                sProgressPulseUntil[i], cProgressPulseFrames);
            const float completionPulse = animationStrength(
                sCompletionPulseUntil[i], cCompletionPulseFrames);
            ImU32 fill = ((row + col) & 1)
                ? IM_COL32(18, 22, 32, 215)
                : IM_COL32(13, 17, 26, 215);
            if (!visible)
                fill = IM_COL32(8, 10, 16, 215);
            if (saved.completed)
                fill = IM_COL32(27, 83, 47, 225);
            if (winningLine || winningTarget)
                fill = saved.completed ? IM_COL32(112, 83, 20, 230) : IM_COL32(73, 57, 20, 225);
            const ImVec2 cellMin(cx + 1.5f * scale, cy + 1.5f * scale);
            const ImVec2 cellMax(cx + cellWidth - 1.5f * scale,
                cy + cellHeight - 1.5f * scale);
            ImU32 border = IM_COL32(60, 64, 74, 235);
            if (saved.completed)
                border = IM_COL32(90, 210, 110, 220);
            if (winningLine || winningTarget)
                border = IM_COL32(245, 205, 70, 245);
            dl->AddRectFilled(cellMin, cellMax, fill, 3.5f * scale);
            dl->AddRect(cellMin, cellMax, border,
                3.5f * scale, 0, (winningLine || winningTarget ? 2.0f : 1.0f) * scale);
            if (saved.completed || winningTarget) {
                dl->AddRectFilled(cellMin,
                    ImVec2(cellMax.x, cellMin.y + 2.5f * scale), border,
                    3.5f * scale);
            }
            if (visible && progressPulse > 0.0f && completionPulse <= 0.0f) {
                const s32 pulseAlpha = static_cast<s32>(65.0f * progressPulse);
                dl->AddRectFilled(cellMin, cellMax,
                    IM_COL32(90, 185, 255, pulseAlpha), 3.5f * scale);
                dl->AddRect(cellMin, cellMax, IM_COL32(140, 215, 255, 255),
                    3.5f * scale, 0, (1.0f + 2.0f * progressPulse) * scale);
            }
            if (visible && completionPulse > 0.0f) {
                const float expansion = (1.0f - completionPulse) * 5.0f * scale;
                const s32 flashAlpha = static_cast<s32>(95.0f * completionPulse);
                const s32 ringAlpha = static_cast<s32>(255.0f * completionPulse);
                dl->AddRectFilled(cellMin, cellMax,
                    IM_COL32(180, 255, 190, flashAlpha), 3.5f * scale);
                dl->AddRect(
                    ImVec2(cellMin.x - expansion, cellMin.y - expansion),
                    ImVec2(cellMax.x + expansion, cellMax.y + expansion),
                    IM_COL32(120, 255, 145, ringAlpha), 5.0f * scale, 0,
                    (1.5f + 2.5f * completionPulse) * scale);
            }

            if (!visible) {
                if (!ready) {
                    const float lockedSize = 12.5f * scale;
                    const ImVec2 lockedText = font->CalcTextSizeA(
                        lockedSize, 3.402823e38F, 0.0f, "LOCKED");
                    dl->AddText(font, lockedSize,
                        ImVec2(cx + (cellWidth - lockedText.x) * 0.5f,
                            cy + (cellHeight - lockedText.y) * 0.5f),
                        IM_COL32(140, 145, 155, 255), "LOCKED");
                }
                continue;
            }

            const GoalDefinition* goal = findGoal(saved.goal.definitionId);
            if (!goal)
                continue;
            const float pad = 6.5f * scale;
            const float labelSize = 15.0f * scale;
            const float progressSize = 13.0f * scale;
            drawCenteredWrappedText(dl, font, labelSize, IM_COL32_WHITE,
                goal->shortLabel,
                ImVec2(cx + pad, cy + pad),
                ImVec2(cx + cellWidth - pad, cy + cellHeight - 21.0f * scale));

            char progress[24];
            nn::util::SNPrintf(progress, sizeof(progress), "%d/%d", saved.progress, saved.goal.target);
            const ImVec2 progressText = font->CalcTextSizeA(
                progressSize, 3.402823e38F, 0.0f, progress);
            dl->AddText(font, progressSize,
                ImVec2(cx + (cellWidth - progressText.x) * 0.5f,
                    cy + cellHeight - 18.0f * scale),
                saved.completed ? IM_COL32(150, 235, 165, 255) : IM_COL32(140, 205, 255, 255),
                progress);
        }

        const char* prompt = nullptr;
        if (ready) {
            prompt = Randomizer::isTrackerOverlayEnabled()
                ? "ZL + ZR: Start  |  ZL + D-Pad Left/Right: Tracker"
                : "ZL + ZR: Start  |  ZL + D-Pad Left/Right: Hide";
        } else {
            prompt = Randomizer::isTrackerOverlayEnabled()
                ? "ZL + D-Pad Left/Right: Tracker"
                : "ZL + D-Pad Left/Right: Hide";
        }
        drawControlHint(dl, font, scale, x, y + height + 6.0f * scale, prompt);
    }

} // namespace Bingo
