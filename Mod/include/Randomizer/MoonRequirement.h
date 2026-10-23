#pragma once

#include <basis/seadTypes.h>

namespace al {
    class IUseSceneObjHolder;
}

class GameDataHolderAccessor;

namespace MoonRequirement {

    const char* getBlockedMessageLabel();

    bool isMoonHomeStage(const char* stageName);
    bool isMoonChapelStage(const char* stageName);
    bool isMoonWorld(GameDataHolderAccessor accessor);

    bool isBlockedMessage(const char* fileName, const char* key);
    s32 getRequirement();
    s32 getCollected(GameDataHolderAccessor accessor);
    s32 getRemaining(GameDataHolderAccessor accessor);
    bool isSatisfied(GameDataHolderAccessor accessor);
    bool isToadetteMedalHuntChapelLocked();

    bool showBlockedMessage(const al::IUseSceneObjHolder* holder);
    const char16_t* tryMakeCapMessage(const char* fileName, const char* key);

} // namespace MoonRequirement
