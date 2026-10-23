#include "Archipelago/ArchipelagoSocket.h"

#include "Library/Base/StringUtil.h"
#include "Scene/StageScene.h"
#include "Sequence/HakoniwaSequence.h"
#include "System/GameSystem.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Thread/AsyncFunctorThread.h"
#include "game/System/GameDataFile.h"
#include "game/System/GameDataHolder.h"
#include "game/System/GameDataHolderAccessor.h"
#include "helpers.hpp"
#include "sead/heap/seadExpHeap.h"
#include "sead/heap/seadHeapMgr.h"
#include "sead/thread/seadAtomic.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <new>
#include <nn/fs.h>
#include <nn/util.h>

namespace ArchipelagoSocket {

    void initHeap() {
        if (sArchipelagoSocketHeap)
            return;
        sArchipelagoSocketHeap = sead::ExpHeap::create(
            kArchipelagoSocketHeapSize, "ArchipelagoSocketHeap", al::getStationedHeap(), sizeof(void*),
            sead::Heap::cHeapDirection_Forward, true);
        if (sArchipelagoSocketHeap)
            al::addNamedHeap(sArchipelagoSocketHeap, "ArchipelagoSocketHeap");
    }

    void init() {
        // sead::ScopedCurrentHeapSetter setter(sArchipelagoSocketHeap);
    }

    bool useNonBlockingRecv() { return sNonBlockingRecv; }

} // namespace ArchipelagoSocket