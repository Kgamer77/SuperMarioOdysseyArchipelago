#pragma once

#include "al/Library/Layout/LayoutInitInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/Nerve/Nerve.h"
#include "al/Library/Nerve/NerveSetupUtil.h"
#include "al/Library/Nerve/NerveStateBase.h"
#include "al/Library/Play/Layout/SimpleLayoutAppearWaitEnd.h"
#include "al/Library/Scene/Scene.h"

#include "game/Layout/MenuSelectParts.h"
#include "game/Layout/StageSceneLayout.h"
#include "game/Scene/StageSceneStateOption.h"

class FooterParts;
class StageSceneStateStartSeparatePlay;
class StageSceneStateEndSeparatePlay;
class SceneAudioSystemPauseController;

namespace al {
    class WindowConfirm;
    class KeyRepeatCtrl;
    class PauseCameraCtrl;
    class WipeSimple;
} // namespace al

class StageSceneStatePauseMenu : public al::HostStateBase<al::Scene> {
public:
    StageSceneStatePauseMenu(const char*, al::Scene*, al::SimpleLayoutAppearWaitEnd*, GameDataHolder*, const al::SceneInitInfo&, const al::ActorInitInfo&,
        const al::LayoutInitInfo&, al::WindowConfirm*, StageSceneLayout*, bool, SceneAudioSystemPauseController*);
    ~StageSceneStatePauseMenu();

    void appear(void);
    void kill(void);
    void killPauseMenu(void);
    void killMarioModel(void);
    void isNeedKillHost(void);
    void startNormal(void);
    void startAfterTitle(void);
    void killAllOptionLayout(void);
    void isEndToCancel(void);
    void isEndToHelp(void);
    void isLoadData(void);
    void getSelectedFileId(void);
    void isChangeLanguage(void);
    void getLanguage(void);
    void isNewGame(void);
    void isModeSelectEnd(void);
    void checkNeedKillByHostAndEnd(void);
    void startActionMario(const char*);
    void getMarioActor(void);
    bool isDrawLayout(void) const;
    void isDrawLayoutMain(void);
    void isDrawViewRenderer(void);
    void isDrawChromakey(void);
    void setNormal(void);
    void appearMarioModel(void);
    void updatePlayerPose(void);
    void changeNerveAndReturn(const al::Nerve*);
    void startPauseCamera(void);
    void setAfterTitle(void);

    void exeAppear(void);
    void exeWait(void);
    void exeFadeBeforeHelp(void);
    void exeStartHelp(void);
    void exeWaitDraw(void);
    void exeEnd(void);
    void exeStartSeparatePlay(void);
    void exeEndSeparatePlay(void);
    void exeOption(void);
    void exeSave(void);
    void exeConfirmNewGame(void);
    void exeNotExistEmptyFile(void);

    void exeModConfig(void);
    void exeFileSelect(void);

    al::SimpleLayoutAppearWaitEnd* field_0x20; // 0x20
    al::SimpleLayoutAppearWaitEnd* mMenuGuide; // 0x28
    al::SimpleLayoutAppearWaitEnd* mMenuRight; // 0x30
    FooterParts* mFooterParts; // 0x38
    MenuSelectParts* mSelectParts; // 0x40
    al::WipeSimple* mWipeSimple; // 0x48
    void* field_0x50; // 0x50
    int field_0x58;
    int field_0x5c;
    StageSceneStateStartSeparatePlay* mStateStartSeperatePlay;
    StageSceneStateEndSeparatePlay* mStateEndSeperatePlay;
    StageSceneStateOption* mStateOption;
    al::LiveActor* mMarioHigh;
    unsigned char field_0x80[39];
    GameDataHolder* mDataHolder;
    al::PauseCameraCtrl* mPauseCameraCtrl;
    al::WindowConfirm* field_0xb8;
    bool mIsNewGame;
    unsigned char _pad_c1[7];
    al::KeyRepeatCtrl* field_0xc8;
    StageSceneLayout* mStageSceneLyt;
    int field_0xd8;
    int field_0xdc;
    unsigned long field_0xe0;
    void* field_0xe8;
};

namespace {
    NERVE_IMPL(StageSceneStatePauseMenu, Appear)
    NERVE_IMPL(StageSceneStatePauseMenu, Wait)
    NERVE_IMPL(StageSceneStatePauseMenu, FadeBeforeHelp)
    NERVE_IMPL(StageSceneStatePauseMenu, StartHelp)
    NERVE_IMPL(StageSceneStatePauseMenu, WaitDraw)
    NERVE_IMPL(StageSceneStatePauseMenu, End)
    NERVE_IMPL(StageSceneStatePauseMenu, StartSeparatePlay)
    NERVE_IMPL(StageSceneStatePauseMenu, EndSeparatePlay)
    NERVE_IMPL(StageSceneStatePauseMenu, Option)
    NERVE_IMPL(StageSceneStatePauseMenu, Save)
    NERVE_IMPL(StageSceneStatePauseMenu, ConfirmNewGame)
    NERVE_IMPL(StageSceneStatePauseMenu, NotExistEmptyFile)
    // custom nerves
    NERVE_IMPL(StageSceneStatePauseMenu, ModConfig)
    NERVE_IMPL(StageSceneStatePauseMenu, FileSelect)

    NERVES_MAKE_STRUCT(StageSceneStatePauseMenu, Appear, Wait, FadeBeforeHelp, StartHelp, WaitDraw, End, StartSeparatePlay, EndSeparatePlay, Option, Save,
        ConfirmNewGame, NotExistEmptyFile, ModConfig, FileSelect)
} // namespace
