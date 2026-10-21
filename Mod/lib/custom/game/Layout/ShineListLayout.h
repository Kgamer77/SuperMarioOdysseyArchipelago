#pragma once

#include "game/Layout/CommonVerticalList.h"

#include <math/seadVector.h>
#include <prim/seadSafeString.h>

#include "Library/Nerve/NerveExecutor.h"

class ShineListLayout : public CommonVerticalList {
public:
    ShineListLayout(const al::LayoutInitInfo&);
    void updateWorldInfo();
    void ShineListLayout(const al::LayoutInitInfo&);
    void appear();
    int getSelectedWorldId() const;
    void control();
    int getWorldShineNum(const al::LayoutActor*, int) const;
    void setSelectedWorld(int);
    bool isClosing() const;
    bool isEnableInput() const;
    bool isCompleteShine() const;
    bool isCompleteCollectCoin() const;
    void calcCursorPos(sead::Vector2<float>*) const;
    bool isEnableChangePage() const;
    void exeAppear();
    void appearPosLayout();
    void exeList();
    void updatePosLayout(bool);
    void exeWorldRoll();
    void exeDeactive();
    void exeEnd();
    void endPosLayout();
    void exeChangeOut();
    void exeChangeIn();
    void deactivate();
    void upTrigger();
    void up();
    void down();
    void downTrigger();
    void right();
    void rightTrigger();
    void left();
    void leftTrigger();
    void decide();
    void cancel();
    void pageUpTrigger();
    void pageDownTrigger();
    void changeOut(bool);
    void changeIn(bool);
    void jumpAchievement();
};