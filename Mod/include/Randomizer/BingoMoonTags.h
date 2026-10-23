#pragma once

#include <basis/seadTypes.h>

namespace ShineDB {
    struct ShineEntry;
}

namespace Bingo {

    enum PhysicalMoonTag : u32
    {
        PhysicalMoonTag_None = 0,
        PhysicalMoonTag_MoonRock = 1 << 0,
        PhysicalMoonTag_Chest = 1 << 1,
        PhysicalMoonTag_Timer = 1 << 2,
        PhysicalMoonTag_Race = 1 << 3,
        PhysicalMoonTag_Note = 1 << 4,
        PhysicalMoonTag_CaptainToad = 1 << 5,
        PhysicalMoonTag_TwoD = 1 << 6,
        PhysicalMoonTag_GroundPound = 1 << 7,
        PhysicalMoonTag_Crate = 1 << 8,
        PhysicalMoonTag_Shards = 1 << 9,
        PhysicalMoonTag_Nut = 1 << 10,
        PhysicalMoonTag_Seed = 1 << 11,
        PhysicalMoonTag_SheepHerding = 1 << 12,
        PhysicalMoonTag_Goombette = 1 << 13,
        PhysicalMoonTag_Lakitu = 1 << 14,
        PhysicalMoonTag_Rabbit = 1 << 15,
        PhysicalMoonTag_Bird = 1 << 16,
        PhysicalMoonTag_HideAndSeek = 1 << 17,
        PhysicalMoonTag_Slots = 1 << 18,
        PhysicalMoonTag_KoopaTraceWalking = 1 << 19,
    };

    u32 getPhysicalMoonTags(const ShineDB::ShineEntry& shine);
    s32 getBossForPhysicalMoon(const ShineDB::ShineEntry& shine, u32* encounterKey);

} // namespace Bingo
