#pragma once

#include "nn/account.h"

#include "sead/math/seadQuat.h"
#include "sead/prim/seadSafeString.h"

#include "Multiplayer/algorithms/PlayerAnims.h"
#include "Multiplayer/packets/Packet.h"
struct PuppetInfo {
    // General Puppet Info
    sead::FixedSafeString<0x10>
        puppetName; // max user account name size is 10 chars, so this could go down to 0xB
    bool isConnected = false;
    nn::account::Uid playerID;
    // Puppet Translation Info
    sead::Vector3f playerPos = sead::Vector3f(0.f, 0.f, 0.f);
    // Identity is a safe inert pose while a peer is joining.  The previous
    // all-zero quaternion is not a rotation and could be consumed after the
    // stage packet arrived but before the first valid PlayerInf, collapsing
    // the remote model's transform matrix.
    sead::Quatf playerRot = sead::Quatf(0.f, 0.f, 0.f, 1.f);
    // Stage metadata and pose packets are independent.  Do not wake a puppet
    // until one complete, validated pose has arrived.
    bool hasPlayerPose = false;
    // Puppet Stage Info
    u8 scenarioNo = -1;
    sead::FixedSafeString<0x40> stageName;
    bool isInSameStage = false;
    // Puppet Costume Info
    sead::FixedSafeString<0x20> costumeBody;
    sead::FixedSafeString<0x20> costumeHead;
    // Puppet Capture Info
    sead::FixedSafeString<0x40> curHack;
    bool isCaptured = false;

    // Which member of curHack's archive family the sender is actually riding,
    // or -1 for the captures that are one name over one archive.
    //
    // Several captures share a HackName across a whole family of models - the
    // five anagram letters, the seven pieces of each Fukuwarai face, the two
    // New Donk manholes - because the name comes from a shared InitHackCap
    // byml. The pool keys on the name, so without this every letter came out an
    // M, every Goomba piece the angry mouth, every Mario piece the nose and the
    // big manhole small. It rides inside the capture packet's existing name
    // string as a "#N" suffix (CaptureSync::encodeWireName) rather than in a new
    // field, because the relay server parses that packet's layout.
    s8 captureVariant = -1;

    // A ride, not a capture: Jaxi or the Scooter. It arrives on the same
    // CaptureInf string as a capture (see CaptureSync::Build::Ride) but is
    // shown differently - the rider keeps their own body and the vehicle is
    // a second model underneath, so the two must never both be set.
    bool isRiding = false;

    // Where the vehicle sits relative to the rider, measured on the sender.
    // hasRideTransform stays false until a packet carrying it arrives, so a
    // freshly-acquired vehicle is not placed from an all-zero offset for a
    // frame - it simply is not moved until the real one lands.
    bool hasRideTransform = false;
    sead::Vector3f rideOffset = sead::Vector3f::zero;
    float rideQuatY = 0.0f;
    float rideQuatW = 1.0f;
    bool isStartCapture = false;
    // Puppet Model Info
    PlayerAnims::Type curAnim = PlayerAnims::Type::Unknown;
    PlayerAnims::Type curSubAnim = PlayerAnims::Type::Unknown;
    // The action a captured player's ENEMY is playing, kept separate from
    // curAnim so a PlayerInf and a CaptureInf arriving on adjacent TCP reads
    // cannot make a capture model play a Mario action for a frame (or make a
    // late Mario frame erase the last valid capture action).
    //
    // Encoded with PlayerAnims, NOT CaptureAnims. CaptureAnims looked like the
    // right table by its name and is in fact a strict SUBSET of PlayerAnims -
    // all 385 of its names appear in PlayerAnims' 922, and it is missing the
    // plainest ones a capture actually plays. A hacked Frog runs
    // FrogStateHack, whose actions are "Run", "Wait", "Fall", "Brake",
    // "RunStay", "HackLand"... and of those, CaptureAnims has only HackLand and
    // RunStay. Every other frame encoded to Unknown, decoded to the empty
    // string, and startAction ignored it - so the capture kept whatever
    // "HackOn" left on it and slid around frozen. One vocabulary for both ends
    // removes the whole failure mode: the same index means the same string
    // whichever actor it is aimed at.
    PlayerAnims::Type curCaptureAnim = PlayerAnims::Type::Unknown;
    PlayerAnims::Type curCaptureSubAnim = PlayerAnims::Type::Unknown;
    // The frame that capture's animation is on, sampled from the sender in the
    // same packet as the action name, the position and the cap pose. Negative
    // when the sender is not reporting one.
    //
    // The name alone is not enough to make two copies of an animation agree.
    // Each side starts an action when IT decides to (a Frog restarts its hop
    // when its own copy runs out), so the two run at the same rate on
    // different phases - which is invisible on the capture itself and glaring
    // on anything that has to follow the capture's skeleton, like Cappy
    // sitting on its head.
    f32 captureAnimFrame = -1.f;
    sead::FixedSafeString<PACKBUFSIZE> curAnimStr;
    sead::FixedSafeString<PACKBUFSIZE> curSubAnimStr;
    float blendWeights[6] = {};
    float animRate = 0.f;
    bool is2D = false;
    s8 gameMode = -1;
    // Puppet Hack Cap Info
    sead::Vector3f capPos = sead::Vector3f(0.f, 0.f, 0.f);
    sead::Quatf capRot = sead::Quatf(0.f, 0.f, 0.f, 0.f);
    sead::Quatf capQuat = sead::Quatf(0.f, 0.f, 0.f, 1.f);
    sead::FixedSafeString<PACKBUFSIZE> capAnim;
    // The cap worn on a capture is scaled to the enemy wearing it
    // (CapTargetInfo::calcLockOnFollowTargetScale -> HackCap's own scale ->
    // copyPose onto the display cap), which is why an unscaled remote hat looks
    // enormous on a Frog. Only meaningful while isCapOnHack; identity otherwise.
    sead::Vector3f capScale = sead::Vector3f(1.f, 1.f, 1.f);
    bool isCapThrow = false;
    // Cappy is not only visible while it is in flight.  A captured player wears
    // it on the enemy's head, and that hat is NOT part of the capture archive:
    // HackCap hides its own model on the frame the hack starts and shows a
    // second "MarioCap"/"Display" actor posed at HackCap's own transform
    // (HackCap::exeHack -> hideModelIfShow + updateCapPose -> copyPose).  The
    // capture archive only owns the moustache, which is exactly what a synced
    // capture showed: moustache yes, hat no.  The sender now reports the
    // on-a-capture state as well as the in-flight one, so the puppet can show
    // its own cap actor at the transmitted pose in both.
    bool isCapOnHack = false;
    bool isHoldThrow = false;
    // Hide and Seek Gamemode Info
    bool isIt = false;
    u8 seconds = 0;
    u16 minutes = 0;
    // Freeze Tag Gamemode Info
    u16 freezeTagScore = 0;
    bool isFreezeTagRunner = true;
    bool isFreezeTagFreeze = false;
    bool isFreezeTagFallenOff = false; // When runenr falls off and is automatically frozen, this flag is set
    float freezeIconSize = 0.f;
    // Shine Thief Gamemode Info
    u16 shineThiefScore = 0;
    bool isShineThiefHolder = false; // TRUE = has shine, FALSE = thief (CHANGED FROM isShineThiefRunner)
    bool isShineThiefFallenOff = false;
    float shineThiefIconSize = 0.f;
    u8 shineThiefTeam = 0; // 0 = NONE, 1 = TEAM_1, 2 = TEAM_2
};
