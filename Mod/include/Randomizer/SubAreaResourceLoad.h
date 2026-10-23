#pragma once

#include <basis/seadTypes.h>

class WorldResourceLoader;

namespace SubAreaResourceLoad {

    // Decides whether a cross-kingdom sub-area load may reuse the resident
    // kingdom's world resources instead of swapping them out. Call it from the
    // WorldResourceLoader::requestLoadWorldHomeStageResource hook: a true result
    // means "return false to the sequence and do not call the original", which
    // keeps HakoniwaSequence::exeLoadStage out of the exeLoadWorldResource wait
    // entirely. Returns false for every case it is not certain about, so the
    // fallback is always vanilla behaviour.
    bool shouldKeepResidentWorld(WorldResourceLoader* loader, s32 requestedWorldId,
        const char* destinationStage, s32 destinationWorldId,
        bool isStartRedirect);

} // namespace SubAreaResourceLoad
