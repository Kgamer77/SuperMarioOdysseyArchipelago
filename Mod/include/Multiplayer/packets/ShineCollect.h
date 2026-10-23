#pragma once

#include "sead/prim/seadSafeString.h"

#include "Multiplayer/packets/Packet.h"

struct ShineCollect : public Packet {
    PacketType getType() override { return PacketType::SHINECOLL; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(shineId);
        writer.write(isGrand);
        writer.write(abilityId);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(shineId);
        reader.read(isGrand);
        // Added after isGrand - older peers sending a packet without this
        // field would fail this read and drop the whole packet via
        // finalize()'s size check, so this field must never be added to a
        // build where the other side might still be on an older one.
        reader.read(abilityId);

        reader.finalize();
    }

    int shineId = -1;
    u8 isGrand = false; // fake bool
    // Which ability AbilityRando::unlockForMoon actually granted the sender
    // for this moon (an AbilityId, or -1 for none) - the receiving player
    // has no way to derive this locally (it depends on the SENDER's own
    // save/progression state, not anything knowable from shineId alone), so
    // it has to ride along on the wire. See PlayerEventLog::
    // fillLastUnlockedAbility/fillAbilityById for why a static per-moon
    // lookup doesn't work here.
    int abilityId = -1;
    // The SECOND and THIRD abilities a Multi (triple) moon granted, comma
    // separated ("12,34"), empty otherwise.
    //
    // NOT SERIALISED, deliberately - it is never on this packet's wire. Moons
    // travel as SharedEvent now, where this rides in the event's auxiliary
    // field; this struct is only the internal shape updateShineInfo is called
    // with. Adding it to serialize()/deserialize() would change the frame that
    // the direct PacketType::SHINECOLL path and the C# ShinePacket still agree
    // on.
    sead::FixedSafeString<32> extraAbilityIds;
};