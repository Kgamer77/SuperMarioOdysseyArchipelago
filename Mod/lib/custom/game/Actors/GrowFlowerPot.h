#pragma once

#include "game/Util/ActorDimensionKeeper.h"
#include "game/Util/IUseDimension.h"

#include "Library/LiveActor/LiveActor.h"
#include "types.h"

class GrowFlowerPot : public al::LiveActor, public IUseDimension {
public:
    void tryMaxGrowLevel();

    void* qword110;
    al::PlacementId* mPlacementId;
};