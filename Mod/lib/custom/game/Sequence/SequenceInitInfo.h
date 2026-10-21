#pragma once

#include "Library/System/GameSystemInfo.h"

namespace al {
    class SequenceInitInfo {
    public:
        SequenceInitInfo(const al::GameSystemInfo* sysInf)
            : mSystemInfo(sysInf) { }

        const GameSystemInfo* mSystemInfo;
    };
} // namespace al
