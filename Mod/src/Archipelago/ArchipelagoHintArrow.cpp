#include "Archipelago/ArchipelagoHintArrow.h"

#include "al/Library/LiveActor/ActorModelFunction.h"

// #include "server/gamemode/GameModeManager.hpp"
#include "Archipelago/Archipelago.h"

ArchipelagoHintArrow::ArchipelagoHintArrow(const char* name)
    : GameModeHintArrow(name) { }

void ArchipelagoHintArrow::initAfterPlacement(void) {
    GameModeHintArrow::initAfterPlacement();
}

void ArchipelagoHintArrow::setupMaterials() {
    al::hideMaterial(this, "BodyRedMT00");
    al::showMaterial(this, "BodyYellowMT00");
    al::showMaterial(this, "BodyBlueMT00");
}

bool ArchipelagoHintArrow::shouldBeVisible() {
    bool isTargetAlive = false;
    return isTargetAlive;
}