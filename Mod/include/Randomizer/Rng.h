#pragma once

#include <basis/seadTypes.h>

namespace Randomizer {

    enum class RngStream : u32
    {
        Moon,
        Entrance,
        Requirement,
        Skybox,
        Music,
        Logic,
        Capture,
        Ability,
        Painting,
        Sphinx,
        Kingdom,
        Chest,
        StartPosition,
        MoonAllocation,
        ShopPrice,
    };

    class Rng {
    public:
        explicit Rng(u32 seed)
            : mState(seed) { }

        u32 next() {
            mState = mState * 1664525u + 1013904223u;
            return mState;
        }

        u32 nextModulo(u32 range) {
            if (range == 0)
                return 0;
            return next() % range;
        }

        float nextFloat() {
            return (float)(next() >> 8) / (float)(1 << 24);
        }

        u32 state() const { return mState; }

    private:
        u32 mState;
    };

    inline u32 getRngStreamSalt(RngStream stream) {
        switch (stream) {
        case RngStream::Skybox:
            return 0xB0B0CAFEu;
        case RngStream::Music:
            return 0xBEE7BEE7u;
        case RngStream::Logic:
            return 0x10C1C10Cu;
        case RngStream::Capture:
            return 0xCA97A11u;
        case RngStream::Ability:
            return 0xAB1117EDu;
        case RngStream::Painting:
            return 0xFA1171A6u;
        case RngStream::Sphinx:
            return 0x5F1A11A5u;
        case RngStream::Kingdom:
            return 0xD14C0D3Du;
        case RngStream::Chest:
            return 0x43686573u; // "Ches"
        case RngStream::StartPosition:
            return 0x53746172u; // "Star"
        case RngStream::MoonAllocation:
            return 0x4D416C6Cu; // "MAll"
        case RngStream::ShopPrice:
            return 0x53686F70u; // "Shop"
        // Moon/Entrance/Requirement previously all returned 0, which made their
        // per-stream sequences identical for a given seed (correlated, though not
        // interfering since each makeRng builds an independent Rng). Give each a
        // distinct salt so the streams are uncorrelated. ASCII-derived constants.
        case RngStream::Moon:
            return 0x4D6F6F6Eu; // "Moon"
        case RngStream::Entrance:
            return 0x456E7472u; // "Entr"
        case RngStream::Requirement:
            return 0x52657175u; // "Requ"
        default:
            return 0;
        }
    }

    inline Rng makeRng(u32 seed, RngStream stream) {
        return Rng(seed ^ getRngStreamSalt(stream));
    }

    // Moon allocation keeps its independent decisions on one named stream while
    // separating target/tie selection, carrier reservations, and the final fill.
    static constexpr u32 kMoonAllocationTargetSeedSalt = 0x54415247u; // "TARG"
    static constexpr u32 kMoonAllocationCarrierSeedSalt = 0x43415252u; // "CARR"
    static constexpr u32 kMoonAllocationAssignmentSeedSalt = 0x4153534Eu; // "ASSN"
    static constexpr u32 kMoonAllocationProfileSeedSalt = 0x50524F46u; // "PROF"

    inline Rng makeMoonAllocationRng(u32 seed, u32 substreamSalt) {
        return makeRng(seed ^ substreamSalt, RngStream::MoonAllocation);
    }

} // namespace Randomizer
