#pragma once

#include "EntranceDB.h"
#include "Randomizer/Randomizer.h"

// Subarea Keys lock a kingdom's optional sub-areas behind a found item, cut by
// the kind of door the player walks into. The kinds themselves are generated
// into EntranceDB2 (see tools/generate_entrance_db2.py); this is the runtime
// view over that table: which (kingdom, kind) pairs actually exist, which links
// the feature is allowed to gate, and how the current mode slices those pairs
// into key slots.
//
// Everything here keys on a link's HOME side - the door the player physically
// touches - so the answer stays the same no matter where entrance rando has
// routed that door.
namespace SubareaKeyDB {

    using SubareaType = EntranceDB2::SubareaType;

    // Kinds a key can gate. Spark Pylons are left free because they are one-way
    // launches with no matching door to bounce back out of, and Paintings are
    // owned by PaintingRando, which already gates them.
    static constexpr SubareaType sGatedTypes[] = {
        EntranceDB2::SubareaType_WarpPipe,
        EntranceDB2::SubareaType_MoonPipe,
        EntranceDB2::SubareaType_OpenPassage,
        EntranceDB2::SubareaType_Doorway,
        EntranceDB2::SubareaType_CappyDoor,
        EntranceDB2::SubareaType_Beanstalk,
    };
    static constexpr s32 sGatedTypeCount = sizeof(sGatedTypes) / sizeof(sGatedTypes[0]);

    // Moons available to carry keys, per host. Fixed by the shine table.
    static constexpr s32 cCapMoonCapacity = 31;
    static constexpr s32 cMoonMoonCapacity = 38;

    bool isGatedType(SubareaType type);

    // Kind of door this link presents on its home side, straight from the
    // generated table.
    SubareaType typeForLink(s32 linkIndex);

    // Kingdom that owns the link's home side, or -1 when the home side belongs
    // to no kingdom for key purposes. That is normally "the home stage is a
    // kingdom overworld", which is also how nested sub-area doors are
    // recognised, plus the hub sub-areas named in the .cpp's
    // sKeyedSubareaHomeStages (Shiveria Town, whose own doors are still Snow
    // Kingdom's optional content but which has to stay scoped as a sub-area for
    // entrance routing).
    s32 kingdomForLink(s32 linkIndex);

    // Whether this link is subject to Subarea Keys at all. Story routes
    // (RandoStatus_Vanilla), nested sub-area doors and the Deep Woods loading
    // zones are never gated.
    bool isGatedLink(s32 linkIndex);

    // Which kinds a kingdom actually has gateable doors for, as a bit per
    // SubareaType. A kingdom only gets keys for kinds it really has - Cap has no
    // plain pipes, Ruined has only a Moon Pipe.
    u32 typeMaskForKingdom(s32 kingdom);
    bool kingdomHasType(s32 kingdom, SubareaType type);

    // --- Key slots -------------------------------------------------------
    // A slot is one key. How (kingdom, kind) pairs collapse into slots depends
    // on the mode: per-kind keys span every kingdom (kingdom == -1), per-kingdom
    // keys span every kind (type == SubareaType_Count).
    s32 keySlotCount();
    bool keySlotAt(s32 slot, s32* outKingdom, SubareaType* outType);

    // Slot governing this pair under the current mode, or -1 when the pair has
    // no gateable doors (or the feature is off).
    s32 findKeySlot(s32 kingdom, SubareaType type);

    // Keys the mode needs, and moons the host can spare for them. The file
    // select, the mod config readout and the seed builder all compare these
    // through Randomizer::subareaHostFits so they cannot disagree.
    s32 keyCountForMode(Randomizer::SubareaUnlockMode mode);
    s32 hostMoonCapacity(Randomizer::SubareaUnlockHost host);

    // True when this kingdom's moons carry keys under the current host setting.
    bool isHostKingdom(s32 kingdom);

    const char16_t* typeName(SubareaType type); // u"Warp Pipe"
    const char16_t* typeNamePlural(SubareaType type); // u"Warp Pipes"
    const char* typeNameAscii(SubareaType type); // "Warp Pipe", for logs

} // namespace SubareaKeyDB
