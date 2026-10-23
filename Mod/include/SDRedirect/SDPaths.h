#pragma once

#include "nn/fs.h"

// Everything the mod writes lives under sd:/SMO-Randomizer/. That path has an
// intermediate component - sd:/SMO-Randomizer - and nn::fs::CreateDirectory is NOT
// recursive: it is mkdir, not mkdir -p, and fails outright if the parent is
// missing.
//
// This only became a problem when the tree moved. The old layout was a single
// top-level sd:/SMO-Randomizer/, so one CreateDirectory call was genuinely
// enough. Moving to sd:/SMO-Randomizer/ (commits 4a92173 then 281f060) added
// the sd:/SMO-Randomizer level, and every writer kept doing the same lone
// CreateDirectory("sd:/SMO-Randomizer") it always had. On any SD card that did
// not already happen to have an sd:/SMO-Randomizer directory, all of them failed - and
// since not one of those call sites checks the result, they failed silently and
// took their file write down with them.
//
// Confirmed live rather than theorised: on a card with no sd:/SMO-Randomizer, the
// multiplayer client was connecting to the compiled-in default relay address
// instead of the operator's, because ServerConfig could neither read
// server.ini (not there) nor write it back (CreateDirectory failed), so it kept
// the built-in fallback. mp_debug logs had gone quiet for the same reason.
//
// Call this instead of creating the Randomizer directory directly. It is cheap
// and idempotent - CreateDirectory on an existing directory just fails
// harmlessly, which is exactly how every existing call site already treats it.
namespace SDPaths {

    inline void ensureRandomizerRoot() {
        nn::fs::CreateDirectory("sd:/SMO-Randomizer");
    }

    inline void ensureArchipelagoRoot() {
        nn::fs::CreateDirectory("sd:/SMO-Archipelago");
    }

} // namespace SDPaths
