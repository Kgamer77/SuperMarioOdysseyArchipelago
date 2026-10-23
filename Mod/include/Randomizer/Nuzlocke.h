#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/SeedGenerator.h"
#include "Randomizer/WorldMeta.h"

class GameDataFile;
class StageScene;
class HakoniwaSequence;

namespace AbilityRando {
    struct AbilityEntry;
}

namespace CaptureRando {
    struct CaptureEntry;
}

namespace ShineDB {
    struct ShineEntry;
}

namespace Nuzlocke {

    // Installs the PlayerHitPointData hooks used by Extreme Nuzlocke.  The
    // hooks are always installed with the mod, but are exact pass-throughs
    // unless an active Nuzlocke has Extreme enabled.
    void installHooks();

    // The miss callback arms a sequence-owned death coordinator, then still
    // lets the game finish its normal scene destruction.  No randomizer work
    // or save I/O is allowed in that callback.
    bool armMarioDeath(GameDataFile* file);

    // Called before HakoniwaSequence::exeDestroy, i.e. before the delete-scene
    // state captures how much of the scene's memory it is allowed to release.
    // A Nuzlocke death never respawns into the stage it came from, so the
    // vanilla Miss economy of keeping that stage's resources loaded has to be
    // opted out of here.
    void prepareSceneTeardown(HakoniwaSequence* sequence);

    // Called after HakoniwaSequence::exeDestroy.  Takes over only after the
    // StageScene is gone, preventing the normal Miss path from allocating the
    // replacement scene while a Soft respawn is being generated.
    bool takeControlAfterSceneDestroy(HakoniwaSequence* sequence);
    void updateDeathSequence(HakoniwaSequence* sequence);
    void updateLoadWorldResource(HakoniwaSequence* sequence);
    // True from the moment a hard death hands the sequence back to the boot
    // state until the coordinator is reset.  main.cpp's HakoniwaStateBootLoadData
    // ::startLoad hook only calls through to the original while this holds.
    bool isReturningToTitle();

    // Called exclusively by SeedGenerator's dedicated worker thread.  It only
    // rebuilds randomizer tables; GameDataFile mutation is deferred until the
    // coordinator has the worker result on the sequence thread.
    bool runSoftSeedJob(const SeedGenerator::SoftSeedRequest& request);

    // A Soft reroll carries a moon *count* forward, never the moons themselves.
    // Nothing is pre-collected: the new seed starts with an empty board, and the
    // kingdom the run kept moons in simply begins that many moons up.  The count
    // is stored per kingdom in the GameDataFile hack dictionary and added on top
    // of the seed's real collection inside MoonHooks' getShineNum hooks, so every
    // reader - HUD counter, tracker, globe deposit limit and the unlock
    // requirement - agrees without any of them knowing this exists.
    //
    // worldId is the logical/WorldList id, the same space getShineNum takes.
    s32 getCarriedMoons(const GameDataFile* file, s32 worldId);
    // Drops the parsed cache.  Needed wherever a different save may arrive in a
    // GameDataFile the cache has already read, which is the same hazard (and the
    // same call sites) as KingdomProgress::reset().
    void dropCarriedMoonCache();

    // Soft Nuzlocke inventory locks are persisted in the normal GameDataFile
    // hack dictionary.  They prevent an already-collected item being remapped
    // to a reward that was deliberately lost on the previous seed.
    bool isCaptureTemporarilyLocked(const GameDataFile* file,
        const CaptureRando::CaptureEntry& capture);
    bool isCaptureExplicitlyRetained(const GameDataFile* file,
        const CaptureRando::CaptureEntry& capture);
    bool isAbilityTemporarilyLocked(const GameDataFile* file,
        const AbilityRando::AbilityEntry& ability);
    bool isAbilityExplicitlyRetained(const GameDataFile* file,
        const AbilityRando::AbilityEntry& ability);
    bool isPaintingTemporarilyLocked(const GameDataFile* file, s32 paintingIndex);
    bool isPaintingExplicitlyRetained(const GameDataFile* file, s32 paintingIndex);

    // Everything the death screen draws after the coin counter lives here.  The
    // coordinator owns the numbers and the pacing (it runs on the sequence nerve,
    // one tick per frame); NuzlockeOverlay only renders this snapshot, so the
    // animation stays frame-locked to the state machine rather than to ImGui.
    enum class OverlayStage : u8
    {
        Hidden,
        MoonLoss,
        Rerolling,
        RerollFailed,
    };

    // Captures and abilities surrendered alongside the moons.  Names point at the
    // static display strings in the capture/ability tables, so they stay valid.
    enum class LostItemKind : u8
    {
        Capture,
        Ability,
    };

    static constexpr s32 cMaxLostItems = 32;
    // The lost-item list is a rolling ticker rather than a full list: entries
    // rise into view at the bottom and fade out at the top, so a long tail of
    // losses costs the panel no extra height.  Shared with NuzlockeOverlay
    // because the coordinator drives the scroll and the overlay lays it out.
    static constexpr s32 cLostVisibleRows = 5;

    struct OverlayState {
        OverlayStage stage = OverlayStage::Hidden;
        bool soft = false;
        // The run ended on a Timer Mode timeout rather than a death.  Only the
        // panel's headline changes; everything else is the hard-death ledger.
        bool timeout = false;
        float alpha = 0.0f;
        s32 lostItemCount = 0;
        const char* lostItemName[cMaxLostItems] = {};
        LostItemKind lostItemKind[cMaxLostItems] = {};
        // Position of the ticker in item units.  Entry i sits `lostScroll - i`
        // rows above the bottom of the viewport; it is fractional so the list
        // glides rather than stepping, and it wraps once every entry has shown.
        float lostScroll = 0.0f;
        s32 rowCount = 0;
        s32 rowWorld[WorldMeta::sWorldCount] = {};
        s32 rowBefore[WorldMeta::sWorldCount] = {};
        s32 rowValue[WorldMeta::sWorldCount] = {};
        s32 rowAfter[WorldMeta::sWorldCount] = {};
        // -1 means "every row is draining at once", which is what a hard
        // Nuzlocke wipe does; soft runs walk one kingdom at a time.
        s32 activeRow = -1;
        float activePulse = 0.0f;
        s32 lostTotal = 0;
    };

    const OverlayState& getOverlayState();

    void unlockCaptureForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon);
    void unlockAbilityForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon);
    void unlockPaintingForMoon(GameDataFile* file, const ShineDB::ShineEntry& moon);

} // namespace Nuzlocke
