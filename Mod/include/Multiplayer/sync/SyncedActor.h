#pragma once

#include "sead/basis/seadTypes.h"
#include "sead/math/seadQuat.h"
#include "sead/math/seadVector.h"

namespace al {
    class LiveActor;
    class Scene;
}

class SyncPayload;

// Generic per-actor sync, after the SyncedActor design in
// https://github.com/Fuzzy2319/SMO-Online (LGPL-3.0). The design is theirs;
// this is our own implementation of it against our transport, because their
// serialisation targets a SocketStream protocol that no server implements -
// see the plan file. Credit is owed either way.
//
// The point of the design: a registry keyed on al::LiveActor* where each
// entry serialises ITSELF through virtuals, so any actor can be synced by
// adding a subclass. That is the generalisation this codebase never had -
// PuppetActor and PuppetHackActor are each hand-written special cases, which
// is why capture sync cost as much as it did.
//
// PHASE 1 SCOPE: only actors we do not already sync - enemies and world
// objects. Puppets, captures and costumes are untouched and still run on
// PuppetInfo. Phase 2 migrates them; see the plan file for what must be true
// first. Keep this base class free of "enemy" assumptions so that stays a
// subclass rather than a rewrite.
// Master switch, OFF until this is proven in a two-player session. Checked at
// entry(), calcAll() and the send path, so with it off nothing is registered,
// nothing is stepped and nothing is transmitted - the same shape as
// cEnableCaptureSync in PuppetCapturePool.cpp. Flipping this is the only thing
// that makes the feature observable.
inline constexpr bool cEnableActorSync = true;

class SyncedActor {
public:
    // Identifies which subclass wrote a payload, so the receiver knows how to
    // read it. Wire value - only ever append.
    enum class Kind : u8
    {
        Base = 0,
        Transform = 1,
    };

    virtual ~SyncedActor() = default;

    virtual Kind getKind() const { return Kind::Base; }

    // Per-frame work for this actor, owner side and remote side alike.
    virtual void calc(al::Scene* scene);

    // Serialise this actor's per-frame state / apply a received one. The
    // writeInit / readInit split mirrors the reference design: spawn-time
    // identity that never changes belongs in init, per-frame state in calc.
    // Nothing uses init yet, but the split is kept because costume and
    // capture identity are init-shaped and that is what Phase 2 needs.
    virtual void writeCalc(SyncPayload& payload) { }
    virtual void readCalc(SyncPayload& payload) { }
    virtual void writeInit(SyncPayload& payload) { }
    virtual void readInit(SyncPayload& payload) { }

    al::LiveActor* getLiveActor() const { return mLiveActor; }
    s32 getUid() const { return mUid; }
    u8 getOwner() const { return mOwner; }
    void setOwner(u8 owner) { mOwner = owner; }

    // ===== registry =====

    // Registers `actor` if it is a kind we sync, and returns its entry (or
    // nullptr). Safe to call for every actor the game creates - the common
    // case is a cheap rejection.
    //
    // className is the PLACEMENT class name, not actor->getName(). Those are
    // different strings: CaptureTypes and PuppetCapturePool both key on the
    // placement name, while the LiveActor's own name is its runtime/internal
    // one. Filtering on the latter matched nothing and left this whole feature
    // silently inert.
    static SyncedActor* entry(al::LiveActor* actor, const char* className);

    static SyncedActor* get(const al::LiveActor* actor);
    static SyncedActor* get(s32 uid);

    // Drops every entry. Called on stage teardown: the actors live on the
    // scene heap that is about to go, so this must never delete through
    // mLiveActor, only forget it.
    static void reset();

    // Once per frame from Client::update(), main thread.
    static void calcAll(al::Scene* scene);

    static s32 getCount();

    // Send every owned actor's state. Called once per frame from calcAll();
    // separate so the rate limit and the authority rule live in one place.
    static void sendOwned();

    // Apply a received update. Keyed on {sender, uid} rather than uid alone,
    // because clients index their registries independently - the same uid
    // means different actors on different machines.
    static void onReceive(u32 senderUid, u16 actorUid, u8 kind, const u8* payload, u16 size);

protected:
    al::LiveActor* mLiveActor = nullptr;
    sead::Vector3f mPosition = sead::Vector3f::zero;
    sead::Quatf mRotation = sead::Quatf::unit;
    s32 mUid = -1;
    // 0xFF means "nobody owns this yet". The owner is the client whose
    // writeCalc is authoritative; everyone else applies readCalc. Meaningful
    // from day one because it is what lets Phase 2 drive a puppet from a
    // remote client and Mario from the local one.
    u8 mOwner = cNoOwner;

public:
    static constexpr u8 cNoOwner = 0xFF;

    // Deliberately far smaller than the reference design's 5120. That one
    // linear-scans on every actor creation, and this engine has punished
    // every fixed-capacity structure we have touched (ModelDrawerBase,
    // ExecutorActorExecuteBase, ActorResourceHolder, DepthShadowMapDirector).
    // Overflow degrades to "not synced" with a log line, never a crash.
    // Revisit with measurements, not guesses.
    static constexpr s32 cMaxActors = 256;

    // Actors transmitted per frame, round-robin. At 60Hz this is 240 updates
    // a second - enough for a handful of nearby enemies to look live without
    // swamping a protocol that also carries player, cap and costume traffic.
    static constexpr s32 cSendPerFrame = 4;
};
