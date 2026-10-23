#pragma once

#include <basis/seadTypes.h>

#include "SD/SDHooks.h"

class GameDataHolderAccessor;
class GameDataFile;
class GameDataHolderWriter;
namespace Randomizer {
    enum class RunMode : u8;
}

namespace RandoSaveFile {
    static constexpr u32 cMagic = 0x4F444E52; // 'RNDO'
    static constexpr u32 cVersion = 20;

#pragma pack(push, 1)
    struct Header {
        u32 magic;
        u32 version;
        u32 fileSize;
        u32 payloadCrc32;
        u32 seed;
        u32 settingsFlags;
        u8 moonRequirement;
        u8 moonDifficulty;
        u8 logicDifficulty;
        u8 hintPriceTier;
        // Successful logic retry attempt. Historically named for entrance rando,
        // but it also selects attempt-dependent kingdom, requirement, painting,
        // capture and ability tables.
        u32 entranceAttempt;
        u8 logicSucceeded;
        // Kept in the existing header padding so this does not change the
        // on-disk header size. settingsFormat distinguishes this layout from
        // earlier version-4 saves.
        u8 bowserWinCondition;
        u8 toadetteMedalRatio;
        u8 settingsFormat;
        s32 ldMoonCount;
        s32 shineTableSize;
        s32 entranceLinkCount;
        u8 reserved[0x10];
    };

    struct Meta {
        s32 totalMoons;
        s32 currentWorldId;
        // Per-kingdom (WorldMeta::WorldId) visit-order rank, 0xFF = unranked / not
        // visited. Split across visitOrderLo (WorldId 0-7) and visitOrderHi
        // (WorldId 8-16) so visitedKingdoms keeps its original byte offset for
        // save-format backward compatibility (was reservedProgress0/1 and part of
        // reserved[0xC]). Only meaningful when the matching visitedKingdoms bit is
        // also set; see RandoSaveFile.cpp for the old-save zero-collision guard.
        u8 visitOrderLo[8];
        u32 visitedKingdoms;
        u8 visitOrderHi[9];
        // Low 17 bits retain the forced-peace kingdom mask.  reserved[2] bit 1
        // carries RunMode bit 2 and bits 6-7 carry RunMode bits 0-1; zero
        // remains Standard for old saves.
        u8 reserved[0x3];
    };
#pragma pack(pop)

    static_assert(sizeof(Header) == 0x40, "Rando save header must be 0x40 bytes");
    static_assert(sizeof(Meta) == 0x20, "Rando save meta must be 0x20 bytes");

    // Every payload version this build can still read. The file-select slot list
    // and the loader share it so a format bump never lists a slot as incompatible
    // that loadAndApply would happily have accepted.
    bool isVersionSupported(u32 version);

    // Every function below defaults its category to whatever's currently
    // active (SaveRedirect::getActiveCategory()) - correct for the many
    // in-gameplay callers, which always run within an already-activated
    // category. A file-select screen BROWSING slots before anything is
    // activated (or generating a brand new save on a background thread)
    // needs to pass its own category explicitly instead.
    bool readHeader(s32 slot, Header* header, Meta* meta,
        SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool write(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool loadAndApply(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool updateMeta(s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool updateMeta(s32 slot, const GameDataFile* file,
        SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool updateMeta(s32 slot, GameDataHolderAccessor accessor,
        SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    // The metadata write itself is synchronous. Runtime callers may schedule it
    // outside a moon-get demo, and can suppress the verbose diagnostic trace
    // that otherwise writes several debugger lines.
    bool updateMeta(s32 slot, GameDataHolderAccessor accessor, bool verbose,
        SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    bool updateSettingsFlags(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    // Transactionally updates the version-10 hint price profiles.
    // Loaded older sidecars are upgraded through the same full-file transaction.
    bool updateHintConfiguration(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    // Generation-safety-net items are kept in rando.bin until the freshly
    // initialized native save has received them. Applying is main-thread-only;
    // committing the notification bit happens after the native save write.
    void resetAutoGrantState();
    s32 getAutoGrantCount();
    s32 applyAutoGrants(GameDataHolderWriter writer);
    bool commitAutoGrantsNotified(
        s32 slot, SaveRedirect::Category category = SaveRedirect::getActiveCategory());
    Randomizer::RunMode getRunMode(const Header& header, const Meta* meta = nullptr);
} // namespace RandoSaveFile
