#pragma once

#ifndef SMOR_FEATURE_BINGO_ASCEND
#define SMOR_FEATURE_BINGO_ASCEND 0
#endif

#ifndef SMOR_FEATURE_TOADETTE_MEDALS
#define SMOR_FEATURE_TOADETTE_MEDALS 0
#endif

#ifndef SMOR_FEATURE_SUBAREA_KEYS
#define SMOR_FEATURE_SUBAREA_KEYS 0
#endif

#ifndef SMOR_FEATURE_NUZLOCKE_MODES
#define SMOR_FEATURE_NUZLOCKE_MODES 0
#endif

#ifndef SMOR_FEATURE_TIMER_MODE
#define SMOR_FEATURE_TIMER_MODE 0
#endif

// Chaos randomization is two independently buildable halves. Enemy covers the
// combat categories - enemies/captures, NPCs and the 2D sections; Prop covers
// incidental scenery only. They share one placement engine, one generated
// database and one set of hooks, so anything below the option surface is gated
// on SMOR_FEATURE_CHAOS_ANY instead of either half.
#ifndef SMOR_FEATURE_ENEMY_RANDOMIZATION
#define SMOR_FEATURE_ENEMY_RANDOMIZATION 0
#endif

#ifndef SMOR_FEATURE_PROP_RANDOMIZATION
#define SMOR_FEATURE_PROP_RANDOMIZATION 0
#endif

// The build normally defines this alongside the two halves so that translation
// units which never include this header still agree with it; the fallback keeps
// the definition correct anywhere else.
#ifndef SMOR_FEATURE_CHAOS_ANY
#define SMOR_FEATURE_CHAOS_ANY \
    (SMOR_FEATURE_ENEMY_RANDOMIZATION || SMOR_FEATURE_PROP_RANDOMIZATION)
#endif

#ifndef SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION
#define SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION 0
#endif

// Seed-generation debugging aid, OFF by default. When a placement is rejected
// the logic can say WHICH rule rejected it rather than only that it did, and it
// keeps a per-kingdom record of the locations that supplied each moon
// requirement so a construction-vs-replay disagreement can be diffed directly.
// Both cost time on every placement and ~2 KB of static memory, and neither is
// of any use to a player, so a release build leaves them out entirely.
#ifndef SMOR_FEATURE_LOGIC_DIAGNOSTICS
#define SMOR_FEATURE_LOGIC_DIAGNOSTICS 0
#endif

// Multiplayer only ever becomes live by joining a room from the title menu, so
// this gates that entry point (and the Multiplayer settings page) rather than
// the subsystem underneath it: every sync path already no-ops behind
// Client::isSocketActive(), and Multiplayer/hooks/initHooks.cpp is what creates
// gHeap and TrackerBridge's heap for the whole mod, so it must keep running.
#ifndef SMOR_FEATURE_MULTIPLAYER
#define SMOR_FEATURE_MULTIPLAYER 1
#endif

// Archipelago is a title-menu row only - the row is deliberately inert (see
// PauseMenuHooks' title decide handling), so this just decides whether it is
// shown at all.
#ifndef SMOR_FEATURE_ARCHIPELAGO
#define SMOR_FEATURE_ARCHIPELAGO 1
#endif

namespace Randomizer::FeatureFlags {

    static constexpr bool BingoAscend = SMOR_FEATURE_BINGO_ASCEND != 0;
    static constexpr bool ToadetteMedals = SMOR_FEATURE_TOADETTE_MEDALS != 0;
    // Nuzlocke and Timer are Run Mode selector entries, and so was the old
    // combined GameModes flag - which is why they used to depend on it. The
    // selector is now built from whichever modes are compiled in and always has
    // Standard, so each mode stands on its own flag.
    static constexpr bool NuzlockeModes = SMOR_FEATURE_NUZLOCKE_MODES != 0;
    static constexpr bool TimerMode = SMOR_FEATURE_TIMER_MODE != 0;
    static constexpr bool SubareaKeys = SMOR_FEATURE_SUBAREA_KEYS != 0;
    static constexpr bool EnemyRandomization = SMOR_FEATURE_ENEMY_RANDOMIZATION != 0;
    static constexpr bool PropRandomization = SMOR_FEATURE_PROP_RANDOMIZATION != 0;
    static constexpr bool ChaosRandomization = SMOR_FEATURE_CHAOS_ANY != 0;
    static constexpr bool TripleMoonRandomization = SMOR_FEATURE_TRIPLE_MOON_RANDOMIZATION != 0;
    static constexpr bool Multiplayer = SMOR_FEATURE_MULTIPLAYER != 0;
    static constexpr bool Archipelago = SMOR_FEATURE_ARCHIPELAGO != 0;

} // namespace Randomizer::FeatureFlags
