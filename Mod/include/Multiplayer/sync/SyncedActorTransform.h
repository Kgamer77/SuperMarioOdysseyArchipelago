#pragma once

#include "Multiplayer/sync/SyncedActor.h"

// The simplest useful synced actor: position and rotation only.
//
// Phase 1's proving ground. It carries no gameplay state and takes no
// authority of its own, so a mistake here shows up as an actor in the wrong
// place rather than as desynced gameplay.
class SyncedActorTransform : public SyncedActor {
public:
    Kind getKind() const override { return Kind::Transform; }

    void calc(al::Scene* scene) override;

    void writeCalc(SyncPayload& payload) override;
    void readCalc(SyncPayload& payload) override;

private:
    // Set when a remote update has arrived and not yet been applied. The
    // transform is applied in calc(), on the main thread, rather than at
    // packet-drain time - packets are drained from a different context, and
    // moving an actor from there is the reentrancy hazard that produced the
    // scene-init-thread crashes in the capture work.
    bool mHasPendingRemote = false;
    sead::Vector3f mRemotePosition = sead::Vector3f::zero;
    sead::Quatf mRemoteRotation = sead::Quatf::unit;
};
