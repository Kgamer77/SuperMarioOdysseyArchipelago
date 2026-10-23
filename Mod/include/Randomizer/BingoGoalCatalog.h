#pragma once

#include <basis/seadTypes.h>

namespace Bingo {

    enum class GoalDifficulty : u8
    {
        Easy,
        Hard
    };

    enum class GoalType : u8
    {
        PurpleCoins,
        SpecificStoryMoon,
        StoryMoons,
        SpecificTripleMoon,
        TripleMoons,
        ChestMoons,
        TimerMoons,
        KoopaRaceMoons,
        CoinsEarned,
        LifeUpHearts,
        CrateMoons,
        ShardMoons,
        NoteMoons,
        NutMoons,
        TwoDMoons,
        MoonRockMoons,
        CaptainToadMoons,
        GroundPoundMoons,
        SeedMoons,
        BossEncounters,
        SpecificBoss,
        SphinxQuestions,
        Checkpoints,
        Paintings,
        WorldPeace,
        KingdomMoonLocations,
        ShopMoons,
        NpcConversations,
        KingdomMoonItems,
        SheepHerdingMoons,
        GoombetteMoons,
        LakituMoons,
        RabbitMoons,
        BirdMoons,
        HideAndSeekMoons,
        HintArtMoons,
        SlotsMoons,
        KoopaTraceWalkingMoons,
        Count,
    };

    struct GoalDefinition {
        u16 id;
        GoalType type;
        GoalDifficulty difficulty;
        s16 target;
        s16 subject;
        s32 subjectUid;
        const char* shortLabel;
        const char* fullLabel;
    };

    // The immutable, persisted portion of a generated board cell. Labels remain
    // catalog-owned, while evaluation never changes if the catalog is rebalanced.
    struct GoalInstance {
        u16 definitionId;
        GoalType type;
        GoalDifficulty difficulty;
        s16 target;
        s16 subject;
        s32 subjectUid;
    };

    const GoalDefinition* getGoals();
    s32 getGoalCount();
    const GoalDefinition* findGoal(u16 id);
    bool isEligible(const GoalDefinition& goal);

} // namespace Bingo
