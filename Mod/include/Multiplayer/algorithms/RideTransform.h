#pragma once

#include "sead/math/seadQuat.h"
#include "sead/math/seadVector.h"

// Quaternion arithmetic for the ride channel.
//
// A rider's vehicle is NOT at the rider's transform. Mario is bound to a point
// on it, so the vehicle sits at a fixed offset and rotation in the rider's
// local frame - which is why placing the puppet's copy at the packet transform
// put the scooter beside the puppet instead of under it.
//
// The relationship is measured on the sender, where both actors exist, and sent
// as three floats of local offset plus the yaw part of the relative rotation.
// Written out by hand because al has no rotate-vector-by-quat in the link map,
// and because these are four lines each.
namespace RideTransform {

    inline sead::Quatf conjugate(const sead::Quatf& q) {
        sead::Quatf out;
        out.x = -q.x;
        out.y = -q.y;
        out.z = -q.z;
        out.w = q.w;
        return out;
    }

    inline sead::Quatf mul(const sead::Quatf& a, const sead::Quatf& b) {
        sead::Quatf out;
        out.w = a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z;
        out.x = a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y;
        out.y = a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x;
        out.z = a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w;
        return out;
    }

    // v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v)
    inline sead::Vector3f rotate(const sead::Quatf& q, const sead::Vector3f& v) {
        const f32 tx = 2.0f * (q.y * v.z - q.z * v.y);
        const f32 ty = 2.0f * (q.z * v.x - q.x * v.z);
        const f32 tz = 2.0f * (q.x * v.y - q.y * v.x);

        sead::Vector3f out;
        out.x = v.x + q.w * tx + (q.y * tz - q.z * ty);
        out.y = v.y + q.w * ty + (q.z * tx - q.x * tz);
        out.z = v.z + q.w * tz + (q.x * ty - q.y * tx);
        return out;
    }

    inline sead::Vector3f unrotate(const sead::Quatf& q, const sead::Vector3f& v) {
        return rotate(conjugate(q), v);
    }

    // Only the yaw part of the relative rotation survives the trip: a pure-yaw
    // quaternion is (0, y, 0, w), so two floats carry it. Lean and pitch are
    // dropped, which costs a motorcycle its tilt into corners and nothing else.
    // If that turns out to matter, the sender's one-shot log prints the whole
    // relative quaternion so the missing components are visible before anyone
    // spends wire on them.
    inline sead::Quatf yawOnly(f32 y, f32 w) {
        const f32 lengthSq = y * y + w * w;
        if (lengthSq < 0.000001f)
            return sead::Quatf::unit;

        // No sead sqrt dependency: one Newton step from a decent seed is far
        // more precision than a facing angle needs, but std::sqrt is available
        // here and clearer.
        const f32 inv = 1.0f / __builtin_sqrtf(lengthSq);

        sead::Quatf out;
        out.x = 0.0f;
        out.y = y * inv;
        out.z = 0.0f;
        out.w = w * inv;
        return out;
    }

} // namespace RideTransform
