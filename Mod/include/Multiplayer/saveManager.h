#pragma once

#include "sead/heap/seadDisposer.h"

#include "al/Library/Thread/AsyncFunctorThread.h"

#include "game/System/GameConfigData.h"

namespace sead {
    class FrameHeap;
}
class GameConfigData;

constexpr const char* sModFolder = "sd:/SMOO-Plus";
constexpr const char* sSettingsPath = "sd:/SMOO-Plus/settings.byml";

class SaveManager {
    SEAD_SINGLETON_DISPOSER(SaveManager)

public:
    SaveManager();

    void startThread(GameConfigData* config);
    void write();
    void read(GameConfigData* config);

public:
    al::AsyncFunctorThread mThread;
    sead::FrameHeap* mHeap = nullptr;
    GameConfigData mConfig;
    u8* mBuffer = nullptr;
};