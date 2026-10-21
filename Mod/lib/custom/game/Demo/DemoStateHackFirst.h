#pragma once

class IUsePlayerHack;

namespace al {
    class SensorMsg;
    class HitSensor;
} // namespace al

class DemoStateHackFirst {
public:
    bool tryHackFirst(IUsePlayerHack**, const al::SensorMsg*, al::HitSensor*, al::HitSensor*);
};
