#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/BingoGoalCatalog.h"
#include "SD/SDHooks.h"

class GameDataFile;
class GameDataHolder;

namespace ShineDB {
    struct ShineEntry;
}

namespace Bingo {

    enum class BoardState : u8
    {
        Ready,
        Active,
        Won
    };
    enum class EventOrigin : u8
    {
        Local,
        Remote
    };

    struct GoalEvent {
        GoalType type;
        s32 amount;
        s32 subject;
        u32 uniqueKey;
        EventOrigin origin;
    };

    void reset();
    bool generate(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool load(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool save();
    bool flushIfDirty();
    bool remove(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool validate(s32 slot, u32 seed, u8 runMode,
        SaveRedirect::Category category = SaveRedirect::getActiveCategory());

    void update(GameDataFile* file);
    // Arms the board's opening input lock after the run first hands Mario over to
    // the player. This notification is idempotent.
    void notifyMarioControlAvailable();
    void draw();
    void handleInput();
    bool shouldSuppressTracker();
    bool shouldBlockGameplayInput();
    bool consumeHorizontalTrigger();
    bool consumeStartTrigger();

    void recordEvent(const GoalEvent& event);
    void recordMoon(const ShineDB::ShineEntry* physical, const ShineDB::ShineEntry* item,
        bool alreadyCollected,
        EventOrigin origin = EventOrigin::Local);
    void recordWorldPeace(s32 worldId, EventOrigin origin = EventOrigin::Local);

    // Multiplayer state is applied through the same game-data APIs as local state.
    // Bracket those writes so aggregate counters and scenario transitions can move
    // their baselines without awarding board progress.
    void beginRemoteApply(GameDataFile* file);
    void endRemoteApply(GameDataFile* file);

    BoardState getState();
    bool isBoardVisible();

} // namespace Bingo
