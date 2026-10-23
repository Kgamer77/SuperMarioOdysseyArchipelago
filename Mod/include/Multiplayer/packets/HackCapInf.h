#pragma once

#include "Multiplayer/packets/Packet.h"

// Values for HackCapInf::isCapVisible. It has always been a u8 carrying a bool,
// so widening it to three states costs no bytes and keeps the packet layout
// (and every existing client's parsing of it) identical - an older client reads
// anything non-zero as "the cap is visible at capPos", which stays true.
static constexpr u8 cCapVisibleNone = 0;
// Thrown: Cappy is flying/spinning on its own, posed by HackCap itself.
static constexpr u8 cCapVisibleThrown = 1;
// Worn on a capture: HackCap has hidden its own model and shows its "Display"
// cap actor at the same pose, sitting on the captured enemy's head.
static constexpr u8 cCapVisibleOnHack = 2;

// Master switch for the worn-on-a-capture hat, checked by the sender and the
// receiver so one flip moves both.
//
// The hat was off for a while because it swam around the head: its transform
// is sampled from the SENDER's animation, where it rides the enemy's head
// joint, while the remote capture used to play that animation on its own phase
// - a Frog restarts its hop when ITS copy of the action ends, not when the
// sender's did. A hat from one instant on a head from another.
//
// It is back on now that PlayerInf carries the capture's animation frame
// (PlayerInfPacket.h): the puppet puts its capture on the sender's phase as
// each packet lands, so the head under the hat is the head the hat was
// measured against. PuppetActor additionally refuses to show the hat unless
// that phase is actually arriving, so a peer that does not send one gets the
// old, hatless behaviour rather than the swimming one.
static constexpr bool cSyncCapOnHack = true;

struct HackCapInf : public Packet {
    PacketType getType() override { return PacketType::HACKCAPINF; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(capPos.x);
        writer.write(capPos.y);
        writer.write(capPos.z);
        writer.write(capJointRot.x);
        writer.write(capJointRot.y);
        writer.write(capJointRot.z);
        writer.write(capJointRot.w);
        writer.write(isCapVisible);
        writer.writeString(capAnim);
        writer.write(capRot.x);
        writer.write(capRot.y);
        writer.write(capRot.z);
        writer.write(capRot.w);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(capPos.x);
        reader.read(capPos.y);
        reader.read(capPos.z);
        reader.read(capJointRot.x);
        reader.read(capJointRot.y);
        reader.read(capJointRot.z);
        reader.read(capJointRot.w);
        reader.read(isCapVisible);
        reader.readString(capAnim);
        reader.read(capRot.x);
        reader.read(capRot.y);
        reader.read(capRot.z);
        reader.read(capRot.w);

        reader.finalize();
    }
    sead::Vector3f capPos;
    sead::Quatf capJointRot;
    u8 isCapVisible = false; // fake bool
    sead::FixedSafeString<PACKBUFSIZE> capAnim;
    sead::Quatf capRot;
};