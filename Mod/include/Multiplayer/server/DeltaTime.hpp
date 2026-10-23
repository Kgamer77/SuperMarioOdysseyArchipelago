#pragma once

#include "sead/math/seadQuatCalcCommon.hpp"
#include "sead/time/seadDateTime.h"
#include "sead/time/seadTickTime.h"

struct Time {
    static void calcTime();

    static sead::TickTime prevTime;
    static sead::TickSpan deltaSpan;
    static float deltaTime;
};