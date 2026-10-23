#include "Multiplayer/sync/SyncedActor.h"

#include "hk/diag/diag.h"

#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/LiveActor.h"

#include "Multiplayer/MpDebugLog.h"
#include "Multiplayer/algorithms/CaptureTypes.h"
#include "Multiplayer/server/Client.hpp"

#include "Multiplayer/packets/ActorSync.h"
#include "Multiplayer/sync/SyncPayload.h"
#include "Multiplayer/sync/SyncedActorTransform.h"

namespace {

    // Everything here is trivially initialised on purpose: this file has no
    // static constructors to run, since nothing guarantees .init_array is
    // executed for the mod. Same reasoning as PuppetCapturePool.
    SyncedActor* sActors[SyncedActor::cMaxActors];
    s32 sActorCount = 0;
    // Latched so an overflowing stage logs once rather than every actor.
    bool sReportedFull = false;

} // namespace

SyncedActor* SyncedActor::entry(al::LiveActor* actor, const char* className) {
    if (!cEnableActorSync || !actor || !className)
        return nullptr;

    const char* name = className;

    // Cheap rejection first: this runs for every placed object in the stage,
    // and FindType is a compile-time crc32 table lookup. Phase 1 syncs only
    // capturable enemies - a small, well-defined set we already classify
    // elsewhere, and the actors this design was wanted for.
    if (CaptureTypes::FindType(name) == CaptureTypes::Type::Unknown)
        return nullptr;

    if (get(actor))
        return nullptr; // already registered

    if (sActorCount >= cMaxActors) {
        if (!sReportedFull) {
            MpDebugLog::write("[SyncedActor] registry full at %d - further actors this stage are not synced",
                cMaxActors);
            sReportedFull = true;
        }
        return nullptr;
    }

    SyncedActor* synced = new (gHeap) SyncedActorTransform();
    if (!synced) {
        hk::diag::logLine("[SyncedActor] allocation failed for %s", name);
        return nullptr;
    }

    synced->mLiveActor = actor;
    synced->mUid = sActorCount;
    // Left unowned. Phase 1 has no authority negotiation - whoever is going to
    // drive this actor claims it later. Writing a real owner here would be
    // guessing, and an unowned actor simply is not sent.
    synced->mOwner = cNoOwner;

    sActors[sActorCount++] = synced;

    // Logged because the absence of any [SyncedActor] line was previously
    // indistinguishable from the feature not running at all - which is exactly
    // what was happening.
    MpDebugLog::write("[SyncedActor] registered %s uid=%d (%d/%d)", className, synced->mUid,
        sActorCount, cMaxActors);

    return synced;
}

SyncedActor* SyncedActor::get(const al::LiveActor* actor) {
    if (!actor)
        return nullptr;

    for (s32 i = 0; i < sActorCount; i++) {
        if (sActors[i] && sActors[i]->mLiveActor == actor)
            return sActors[i];
    }
    return nullptr;
}

SyncedActor* SyncedActor::get(s32 uid) {
    if (uid < 0 || uid >= sActorCount)
        return nullptr;

    return sActors[uid];
}

void SyncedActor::reset() {
    // The SyncedActor objects are ours (gHeap) and are deleted; the LiveActors
    // they point at belong to the scene heap that is being torn down and must
    // only be forgotten. Deleting through mLiveActor here would be a
    // use-after-teardown - the same rule PuppetCapturePool::onStageKill
    // follows.
    for (s32 i = 0; i < sActorCount; i++) {
        delete sActors[i];
        sActors[i] = nullptr;
    }

    if (sActorCount > 0)
        MpDebugLog::write("[SyncedActor] reset, dropped %d actors", sActorCount);

    sActorCount = 0;
    sReportedFull = false;
}

void SyncedActor::calcAll(al::Scene* scene) {
    if (!cEnableActorSync)
        return;

    for (s32 i = 0; i < sActorCount; i++) {
        if (sActors[i])
            sActors[i]->calc(scene);
    }

    sendOwned();
}

void SyncedActor::sendOwned() {
    // AUTHORITY: the room's setup owner transmits, everyone else applies.
    //
    // Borrowed, not designed for this - RoomRole::SetupOwner marks who uploads
    // the seed. It is used here because it is the one existing designation that
    // names exactly one client per room, which is what generic actors need and
    // what neither our protocol nor the reference design otherwise provides
    // (that design assigns an owner only to the local Mario and leaves every
    // other actor at 0xFF). Both clients load the same stage and register the
    // same enemies, so without a single authority both would broadcast and
    // fight over the same actor.
    //
    // Deliberately easy to replace: this predicate is the whole rule, so
    // swapping it for per-actor claims later is a local change.
    if (!Client::isSetupOwner() || sActorCount <= 0)
        return;

    // Round-robin a slice per frame rather than everything every frame. A
    // stage can register a couple of hundred actors and this runs at 60Hz;
    // sending all of them would dwarf the rest of the protocol combined.
    static s32 sCursor = 0;

    for (s32 sent = 0; sent < cSendPerFrame && sent < sActorCount; sent++) {
        if (sCursor >= sActorCount)
            sCursor = 0;

        SyncedActor* actor = sActors[sCursor++];
        if (!actor || !actor->mLiveActor)
            continue;

        ActorSync* packet = new (gHeap) ActorSync();
        if (!packet)
            return;

        SyncPayload payload(packet->mPayload, ActorSync::cPayloadSize);
        actor->writeCalc(payload);

        if (payload.isFailed()) {
            MpDebugLog::write("[SyncedActor] payload overflow writing uid %d - not sent", actor->mUid);
            delete packet;
            continue;
        }

        packet->mKind = static_cast<u8>(actor->getKind());
        packet->mOwner = actor->mOwner;
        packet->mActorUid = static_cast<u16>(actor->mUid);
        packet->mUsedSize = payload.getUsed();

        Client::queueActorSync(packet);
    }
}

void SyncedActor::onReceive(u32 senderUid, u16 actorUid, u8 kind, const u8* payload, u16 size) {
    if (!cEnableActorSync || !payload)
        return;

    SyncedActor* actor = get(static_cast<s32>(actorUid));
    if (!actor)
        return;

    // ASSUMPTION, and the weakest part of Phase 1: uids match across clients.
    // They are assigned in registration order, which is placement-sweep order,
    // which is deterministic for the same stage - so they should agree. The
    // kind check below is a cheap guard that catches a disagreement rather
    // than silently applying one actor's transform to another. If this ever
    // trips in practice the fix is a stable key (placement id) instead of an
    // index, not a bigger guard.
    if (static_cast<u8>(actor->getKind()) != kind) {
        MpDebugLog::write("[SyncedActor] uid %u kind mismatch (local %u, remote %u) - ignoring",
            actorUid, static_cast<u8>(actor->getKind()), kind);
        return;
    }

    // const_cast is safe: SyncPayload only writes through its buffer in write(),
    // and this path is read-only.
    SyncPayload reader(const_cast<u8*>(payload), size);
    actor->readCalc(reader);
}

s32 SyncedActor::getCount() {
    return sActorCount;
}

void SyncedActor::calc(al::Scene* scene) {
    if (!mLiveActor)
        return;

    // Cache the transform every frame regardless of ownership: the owner sends
    // it, and a remote entry needs somewhere to hold what arrived until the
    // actor is safe to move. Actually applying a received transform is the
    // subclass's job.
    mPosition = al::getTrans(mLiveActor);
    mRotation = al::getQuat(mLiveActor);
}
