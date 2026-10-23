#pragma once

#include "sead/basis/seadTypes.h"
#include <cstdint>

class PuppetHackActor;

namespace al {
    class LiveActor;
    struct ActorInitInfo;
}

namespace CaptureSync {
    enum class Build : u8
    {
        Generic, // al::initActorWithArchiveName - today's path
        YoshiModel, // PlayerFunction::initYoshiModelActor
        Unsupported, // never build; puppet keeps its body

        // A vehicle the player rides rather than captures: Jaxi (SphinxRide)
        // and the Scooter (Motorcycle). Built from its archive exactly like a
        // capture, but SHOWN differently - a rider keeps their own body and the
        // vehicle is drawn under them, so the puppet leases a second model
        // instead of swapping the one it has.
        //
        // It travels on the capture channel because that channel is already a
        // free-form name and the relay server parses CapturePacket's layout;
        // adding a field would mean a server change for something the receiver
        // can decide from this table.
        Ride,
    };

    struct CaptureAdapter {
        // Sender: read up to 4 floats of extra state off the real hack actor.
        void (*sample)(const al::LiveActor* hackActor, float* out /*[4]*/);
        // Receiver, once at build: create the joint controllers / keepers the
        // capture's own init() would have made, bound to storage the actor owns.
        void (*setupModel)(PuppetHackActor* actor, const al::ActorInitInfo& initInfo);
        // Receiver, per packet: write the sampled floats into that storage.
        void (*apply)(PuppetHackActor* actor, const float* in /*[4]*/);
    };

    struct CaptureSubActor {
        const char* archive; // ObjectData/<archive>
        const char* suffix; // usually nullptr
        const char* jointName; // parent joint to follow, nullptr = follow the actor
    };

    struct Entry {
        const char* hackName; // wire key == getCurrentHackName()
        const char* placementClass; // al::tryGetClassName of the placement
        const char* modelName; // nullptr, or the ModelName that disambiguates
                               // a class shared by several captures
        const char* archive; // override only (if actor->getModelName() is wrong)
        const char* suffix; // e.g. "TQSV", passed to initActorWithArchiveName
        Build build;
        const CaptureSubActor* subActors;
        s32 subActorCount;
        const CaptureAdapter* adapter; // nullptr for most captures

        // Every archive this one wire name can stand for, most-likely first.
        //
        // Some captures are one hack name over a FAMILY of models: the anagram
        // letters ship as AnagramAlphabetA / I / M / O / R, the Fukuwarai face
        // parts are a nose, a mouth, four eyes and a moustache, and New Donk
        // City places a small manhole and a big one - and in every case all of
        // them announce themselves with the same HackName, because the name
        // comes from a shared InitHackCap.byml.
        //
        // The list does two jobs, and it has to be the same list for both:
        //
        //  - Residency fallback. Only some of a family are loaded in any one
        //    stage (which word, which face), so the pool walks this in order
        //    and builds the first one the stage actually has.
        //  - The wire variant. The index into THIS array is what the sender
        //    puts on the capture packet (see encodeWireName), so the receiver
        //    can lease the piece the remote player is really riding rather
        //    than whichever one the pool happened to build first. That is why
        //    every row sharing a hackName must point at the same array: the
        //    index has to mean the same archive on both machines.
        //
        // variants[0] is the preferred archive and is normally the same string
        // as `archive`; `archive` alone is what an ordinary capture uses.
        //
        // Left empty (zero-initialised) for every ordinary capture.
        const char* const* variants;
        s32 variantCount;
    };

    // Longest wire name this module will produce, including the "#N" variant
    // suffix and the terminator. CaptureInf::hackName is 0x20, so this must
    // stay inside it.
    inline constexpr s32 cMaxWireNameLen = 0x20;

    // Installs the adapters' own hooks. Call once at boot, next to
    // PuppetCapturePool::installHooks().
    void installHooks();

    // The Bound Bowl Grand Prix racer's snowball rotation, sampled off a
    // YukimaruRacePlayer rather than a captured Yukimaru.
    //
    // It is not an adapter's sample(): during the race the local player IS the
    // racer, so there is no hack actor and no PlayerHackKeeper to route
    // through - Client::sendPlayerInfPacket's yukimaru branch calls this
    // directly. The far side decodes it with the ordinary Yukimaru adapter,
    // because the two classes keep the same quaternion at different offsets
    // and everything after the read is identical.
    void sampleYukimaruRacePlayer(const al::LiveActor* raceActor, float* out /*[4]*/);

    // Rides are matched back from a live actor, not from a name: the sender
    // learns what its player is on by following a bind sensor to its host, and
    // an actor knows nothing about the registry key its placement carried. The
    // pairing is recorded as each ride is placed.
    // What a ride should be playing right now. Both vehicles animate through
    // ordinary actions - Motorcycle has Wait/RunStart/Run/RunLand/Clash,
    // SphinxRide has Wait/Standby/Run/RunSlow/Turn/Jump - driven by their own
    // state machines, which a pooled model does not have (its nerve keeper is
    // nulled like every capture model's).
    //
    // Rather than spend wire on the real action name - there is no spare field
    // while riding, since actName and subActName are carrying the rider's own
    // animation and all six blend weights are Mario's - the puppet picks
    // between idle and moving from the motion it is already being told about.
    // The vehicle's position is synced, so the choice matches what is on screen.
    const char* findRideAction(const char* hackName, bool moving);

    void noteRidePlacement(al::LiveActor* actor, const Entry* entry);
    void resetRidePlacements();
    const char* findRideName(const al::LiveActor* actor);

    const Entry* findByHackName(const char* hackName);
    const Entry* findByPlacement(const char* className, const char* modelName);

    // ---- capture variants (see Entry::variants) ----

    // Which member of the family `archiveName` is, or -1 when the row has no
    // family or the name is not in it. `archiveName` is the model name read off
    // the real hack actor, which for every family in the table is also its
    // archive name (both come from the one initActor call).
    s32 findVariantIndex(const Entry* entry, const char* archiveName);

    // The archive a variant index names, or nullptr when the index is out of
    // range - which is the ordinary case for a capture that has no family and
    // for a peer that does not send one.
    const char* getVariantArchive(const Entry* entry, s32 variant);

    // "Manhole" + variant 1 -> "Manhole#1"; variant < 0 -> "Manhole".
    //
    // The variant rides inside the existing name string rather than in a new
    // packet field on purpose: the relay server parses CapturePacket's layout,
    // so a field would mean a server change for something both ends can already
    // agree on from the table. Writes at most cMaxWireNameLen bytes and drops
    // the suffix rather than truncating the name if it would not fit.
    //
    // MIXED BUILDS: a receiver from before this existed reads "Manhole#1" as an
    // unknown hack name and shows the puppet's own body, i.e. the four families
    // with variants stop syncing entirely rather than syncing wrongly. The
    // suffix is deliberately still sent for variant 0 - dropping it there would
    // buy back the default piece for old receivers, but it would also stop a
    // new receiver from being able to ask for that exact piece, which is the
    // whole point. Both players run the same randomizer build.
    void encodeWireName(char* out, s32 outLen, const char* hackName, s32 variant);

    // The inverse. Copies the bare hack name into `nameOut` and returns the
    // variant index, or -1 when the peer did not send one.
    s32 decodeWireName(char* nameOut, s32 nameOutLen, const char* wire);

    // Every row a placement matches, not just the best one.
    //
    // One placed class can prove the residency of more than one capture, and
    // the two cases that need it cannot be told apart from the placement at
    // all: a Gotogoton placement is the lake puzzle in one stage and the city
    // puzzle in another, and a FukuwaraiWatcher owns the Goomba face in one
    // stage and the Mario face in another - and neither watcher has a model
    // name to distinguish them by, because neither actor has a model. Both
    // rows are registered and PuppetCapturePool's residency check, which
    // already runs before anything is built, decides which one this stage
    // actually has.
    //
    // Qualified rows still win: if any row matches class AND model name, only
    // those are returned, which is what keeps a plain Pukupuku placement from
    // also registering PukupukuSnow. Writes at most `maxOut` entries and
    // returns how many.
    s32 findByPlacementAll(const char* className, const char* modelName, const Entry** out,
        s32 maxOut);
}
