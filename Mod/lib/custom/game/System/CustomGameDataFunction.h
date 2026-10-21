#include "Library/LiveActor/LiveActor.h"
#include "System/GameDataFile.h"
#include "System/GameDataHolder.h"
#include "System/GameDataHolderAccessor.h"

namespace CustomGameDataFunction {
    static GameDataFile::HintInfo* getHintInfoByUniqueID(GameDataHolderAccessor accessor, int uid) {
        return accessor.mData->getGameDataFile()->findShine(uid);
    }

    static const GameDataFile::HintInfo* getHintInfoByIndex(GameDataHolderAccessor accessor, int index) {
        return &accessor.mData->getGameDataFile()->getHintList()[index];
    }

    static const GameDataFile::HintInfo* getHintInfoByIndex(al::LiveActor* actor, int index) {
        GameDataHolderAccessor accessor(actor);
        return getHintInfoByIndex(accessor, index);
    }
} // namespace CustomGameDataFunction
