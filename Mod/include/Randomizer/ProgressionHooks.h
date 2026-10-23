#pragma once

#include <basis/seadTypes.h>

class GameDataHolderWriter;
class GameDataHolderAccessor;

class GameDataFile;

namespace ProgressionHooks {

    void installHooks();
    void resetTravelFacadeForStageChange();
    void requestOdysseyArrivalWithoutExitDemo(const char* stageName);
    bool getForcedStartStage(GameDataHolderAccessor accessor, const char** outStageName, s32* outScenario, s32* outWorldId);
    bool prepareStartStage(GameDataHolderWriter writer, const char** outStageName, s32* outScenario);
    bool prepareNewSaveStartStage(GameDataHolderWriter writer, const char** outStageName, s32* outScenario);

    // While any chaos category is active, loading a save opens in the first
    // kingdom of the kingdom order rather than wherever the save left off, so a
    // stage or scenario that crashes on entry cannot lock the player out of the
    // menu that disables those categories. Returns whether it redirected.
    bool redirectLoadForChaosRandomization(GameDataHolderWriter writer);
    // Randomized Starting Position: pin mCurrentWorldId to the starting kingdom
    // when a start position is active and the save still has an invalid world.
    // GameDataFile::startStage only derives a world from a MAIN stage name and
    // leaves the field alone otherwise, so a sub-area start reaches actor init
    // with -1 and crashes Shine::initAfterPlacement inside
    // WorldList::getMainStageName(-1). Safe to call unconditionally at scene
    // init: it only ever fires on an already-invalid world id.
    void repairStartPositionWorldId(GameDataHolderWriter writer);
    void persistSeed(GameDataHolderWriter writer, u32 seed);
    void requestBowserEscapeSequenceWarp(GameDataHolderWriter writer);
    void clearGravestoneState();

} // namespace ProgressionHooks
