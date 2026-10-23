#include "Randomizer/SpawnTuning.h"

#if SMOR_FEATURE_CHAOS_ANY

#include <cstring>

namespace SpawnTuning {

    namespace {

        // Same mixer the swap assignment uses, for the same reason: it has to
        // be reproducible from (seed, index) alone, on console and on host.
        constexpr u32 kJitterSalt = 0x6C8E9CF5u;

        u32 mix(u32 value) {
            value ^= value >> 16;
            value *= 0x7FEB352Du;
            value ^= value >> 15;
            value *= 0x846CA68Bu;
            value ^= value >> 16;
            return value;
        }

        // A deterministic value in [-magnitude, +magnitude]. `axis` keeps the
        // three components of one jitter independent; without it an actor
        // would only ever be displaced along the line x == y == z.
        f32 jitterAxis(u32 seed, s32 placementIndex, u32 axis, f32 magnitude) {
            if (magnitude == 0.0f)
                return 0.0f;
            const u32 hashed = mix(
                seed ^ kJitterSalt
                ^ (static_cast<u32>(placementIndex) * 0x9E3779B1u)
                ^ (axis * 0x85EBCA6Bu));
            // 24 bits is ample for a placement offset and keeps the result
            // exactly representable, so the same hash gives the same float.
            const f32 unit = static_cast<f32>(hashed & 0xFFFFFFu)
                / static_cast<f32>(0xFFFFFF);
            return (unit * 2.0f - 1.0f) * magnitude;
        }

        bool textEquals(const char* lhs, const char* rhs) {
            return lhs && rhs && std::strcmp(lhs, rhs) == 0;
        }

    }

    const Adjust* findAdjust(const char* entityName, const char* category) {
        const Adjust* categoryMatch = nullptr;
        for (u32 i = 0; i < sAdjustmentCount; i++) {
            const Adjust& adjust = sAdjustments[i];
            if (adjust.kind == MatchKind::Entity) {
                // An entity row is the most specific statement available, so
                // it returns immediately rather than letting a category row
                // that appears later override it.
                if (textEquals(adjust.match, entityName))
                    return &adjust;
            } else if (!categoryMatch && textEquals(adjust.match, category)) {
                categoryMatch = &adjust;
            }
        }
        return categoryMatch;
    }

    void applyAdjust(
        const Adjust* adjust, u32 seed, s32 placementIndex,
        sead::Vector3f* trans, sead::Vector3f* rotate) {
        if (!adjust)
            return;

        if (trans) {
            trans->x += adjust->translate.x;
            trans->y += adjust->translate.y;
            trans->z += adjust->translate.z;
            if (adjust->translateJitter.isActive()) {
                trans->x += jitterAxis(
                    seed, placementIndex, 0, adjust->translateJitter.x);
                trans->y += jitterAxis(
                    seed, placementIndex, 1, adjust->translateJitter.y);
                trans->z += jitterAxis(
                    seed, placementIndex, 2, adjust->translateJitter.z);
            }
        }

        if (rotate) {
            if ((adjust->flags & SpawnFlag_ReplaceRotation) != 0) {
                rotate->x = 0.0f;
                rotate->y = 0.0f;
                rotate->z = 0.0f;
            }
            rotate->x += adjust->rotate.x;
            rotate->y += adjust->rotate.y;
            rotate->z += adjust->rotate.z;
            if (adjust->rotateJitter.isActive()) {
                // Axes 3-5 rather than 0-2: sharing the axis index with the
                // translate jitter would tie an actor's rotation to its
                // displacement, so everything nudged left would also lean
                // left by a proportional amount.
                rotate->x += jitterAxis(
                    seed, placementIndex, 3, adjust->rotateJitter.x);
                rotate->y += jitterAxis(
                    seed, placementIndex, 4, adjust->rotateJitter.y);
                rotate->z += jitterAxis(
                    seed, placementIndex, 5, adjust->rotateJitter.z);
            }
            if ((adjust->flags & SpawnFlag_YawOnly) != 0) {
                rotate->x = 0.0f;
                rotate->z = 0.0f;
            }
        }
    }

    void applyForEntity(
        const char* entityName, const char* category, u32 seed,
        s32 placementIndex, sead::Vector3f* trans, sead::Vector3f* rotate) {
        applyAdjust(
            findAdjust(entityName, category), seed, placementIndex,
            trans, rotate);
    }

}

#endif
