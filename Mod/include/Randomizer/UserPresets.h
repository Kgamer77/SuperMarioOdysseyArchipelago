#pragma once

#include <basis/seadTypes.h>

#include "Randomizer/WorldMeta.h"

// Player-defined presets: named, player-saved bundles of randomizer settings
// captured from the live New Save settings UI (see
// StageSceneStateFileSelect::captureCurrentSettingsSnapshot /
// applySettingsSnapshot) and persisted to the SD card. Unlike
// Randomizer::Presets (a small fixed compiled-in table), values here are
// stored as tagged (optionId, value) pairs rather than a positional blob, so
// a preset saved by an older mod build still loads after settings options are
// added or removed: unknown ids are skipped when applied, missing ids are simply
// left at their current value on apply.
namespace UserPresets {

    static constexpr s32 kMaxUserPresets = 20;
    static constexpr s32 kNameLength = 24; // char16_t, includes null terminator
    static constexpr s32 kDescriptionLength = 192; // char, includes null terminator
    // Must stay >= the number of snapshot-able options across every settings
    // page, or captureCurrentSettingsSnapshot silently drops the tail. Raising
    // it only grows newly written files: entries sit after a fixed prefix and
    // readers clamp to the stored count, so older presets stay readable.
    static constexpr s32 kMaxEntries = 160;

    struct Entry {
        u16 optionId;
        u32 value;
    };

    // Probes legacy slot files and named .bin files in the preset directory,
    // then caches what's there. New saves use a readable name derived from the
    // preset name, while old preset_00.bin-style files remain importable.
    // Call once before the preset UI can be shown (safe to call again to
    // reload; save()/remove() already keep the cache in sync).
    void loadAll();

    s32 count();
    bool isFull();

    // Returns the slot index of the n-th (0-based) valid preset, in slot
    // order, or -1 if n >= count(). Lets callers present a compact,
    // gap-free list (e.g. a roll widget) while storage keeps sparse fixed
    // slots after a delete.
    s32 nthValidIndex(s32 n);

    const char16_t* getName(s32 index);
    const char* getDescription(s32 index);
    const Entry* getEntries(s32 index, s32* outCount);

    // Returns the index of the preset named exactly `name`, or -1.
    s32 findByName(const char16_t* name);

    // Saves a new preset into the first free slot, or overwrites
    // `overwriteIndex` if it is >= 0. Returns false if there is no free slot
    // and overwriteIndex < 0, or if writing to the SD card fails.
    bool save(const char16_t* name, const char* description,
        const Entry* entries, s32 entryCount, s32 overwriteIndex = -1);

    bool remove(s32 index);

    // Complete settings snapshots use the same tagged scalar representation as
    // presets, with the Phase 4 editor arrays appended in a stable payload.
    // `code` is the canonical Crockford Base32 filename stem produced by

} // namespace UserPresets
