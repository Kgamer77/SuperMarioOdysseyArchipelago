#pragma once

#include "Multiplayer/algorithms/PlayerAnims.h"
#include "Multiplayer/packets/Packet.h"

// While the sender is riding a capture, two of the six animation blend slots
// carry the capture's animation PHASE instead of Mario's blend weights.
//
// Not a hack for its own sake: those six floats are Mario's, the receiver only
// ever applies them to a body model, and a captured player is not showing one.
// The alternative was widening PlayerInf, which is the highest-frequency packet
// on the wire and whose byte layout the relay server and every other client
// agree on.
//
// Slot 0 is the frame; slot 1 is a marker, because "0.4" is a perfectly good
// blend weight and a perfectly good animation frame. -1 is not a legal blend
// weight (the receiver's own 0..1 filter rejects it, so it cannot be mistaken
// for one even by code that knows nothing about this), which makes the marker
// unambiguous and safe against a peer that predates it.
static constexpr s32 cCaptureAnimFrameSlot = 0;
static constexpr s32 cCaptureAnimMarkerSlot = 1;
static constexpr float cCaptureAnimFrameMarker = -1.0f;

// A RIDE - Jaxi or the Scooter - uses the same trick for a different payload,
// and the two can never overlap: riding is not capturing, and the sender only
// ever writes one of them.
//
// A rider is bound to a point ON the vehicle, so the vehicle is not at the
// rider's transform; it sits at a fixed offset and rotation in the rider's
// local frame. That relationship only exists on the sender, where both actors
// are real, so it is measured there and sent: three floats of local offset, and
// the yaw half of the relative rotation as a (y, w) pair.
//
// This one takes all six slots rather than four, so the marker moves to the
// last. Mario's blend weights are lost for the duration, which costs nothing -
// a rider plays one animation, and blending three or more at once is not a
// thing the riding poses do.
static constexpr s32 cRideOffsetXSlot = 0;
static constexpr s32 cRideOffsetYSlot = 1;
static constexpr s32 cRideOffsetZSlot = 2;
static constexpr s32 cRideQuatYSlot = 3;
static constexpr s32 cRideQuatWSlot = 4;
static constexpr s32 cRideMarkerSlot = 5;
static constexpr float cRideMarker = -2.0f;

struct PlayerInf : public Packet {
    PacketType getType() override { return PacketType::PLAYERINF; }

    PacketVector serialize() override {
        PacketWriter writer(this);

        writer.write(playerPos.x);
        writer.write(playerPos.y);
        writer.write(playerPos.z);
        writer.write(playerRot.x);
        writer.write(playerRot.y);
        writer.write(playerRot.z);
        writer.write(playerRot.w);
        writer.write(animBlendWeights[0]);
        writer.write(animBlendWeights[1]);
        writer.write(animBlendWeights[2]);
        writer.write(animBlendWeights[3]);
        writer.write(animBlendWeights[4]);
        writer.write(animBlendWeights[5]);
        writer.write(actName);
        writer.write(subActName);

        return writer.finalize();
    }

    void deserialize(const PacketVector& data) override {
        PacketReader reader(this, data.data(), data.size());

        reader.read(playerPos.x);
        reader.read(playerPos.y);
        reader.read(playerPos.z);
        reader.read(playerRot.x);
        reader.read(playerRot.y);
        reader.read(playerRot.z);
        reader.read(playerRot.w);
        reader.read(animBlendWeights[0]);
        reader.read(animBlendWeights[1]);
        reader.read(animBlendWeights[2]);
        reader.read(animBlendWeights[3]);
        reader.read(animBlendWeights[4]);
        reader.read(animBlendWeights[5]);
        reader.read(actName);
        reader.read(subActName);

        reader.finalize();
    }

    sead::Vector3f playerPos;
    sead::Quatf playerRot;
    float animBlendWeights[6];
    PlayerAnims::Type actName;
    PlayerAnims::Type subActName;

    bool operator==(const PlayerInf& rhs) const {
        bool isWeightsEqual = true;
        for (size_t i = 0; i < 6; i++) {
            if (animBlendWeights[i] != rhs.animBlendWeights[i]) {
                isWeightsEqual = false;
                break;
            }
        }
        return (playerPos == rhs.playerPos && playerRot == rhs.playerRot && isWeightsEqual && actName == rhs.actName && subActName == rhs.subActName);
    }

    bool operator!=(const PlayerInf& rhs) const { return !operator==(rhs); }
};