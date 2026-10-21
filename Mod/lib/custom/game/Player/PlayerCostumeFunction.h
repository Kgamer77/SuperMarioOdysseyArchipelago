#pragma once

#include "al/Library/Resource/Resource.h"

#include "game/Player/PlayerCostumeInfo.h"

namespace PlayerCostumeFunction {
    PlayerBodyCostumeInfo* createBodyCostumeInfo(al::Resource*, const char*);
    PlayerHeadCostumeInfo* createHeadCostumeInfo(al::Resource*, const char*, bool);
} // namespace PlayerCostumeFunction
