#include "Multiplayer/server/DeltaTime.hpp"

#include <cstdlib>

sead::TickTime Time::prevTime;
sead::TickSpan Time::deltaSpan;
float Time::deltaTime = 1 / 60.f;

void Time::calcTime() {
    Time::deltaSpan = Time::prevTime.diffToNow();
    Time::prevTime.setNow();
    Time::deltaTime = std::abs((double)Time::deltaSpan.toNanoSeconds() / 1000000000.0);
}