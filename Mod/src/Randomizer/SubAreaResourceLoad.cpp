#include "Randomizer/SubAreaResourceLoad.h"

#include "EntranceDB.h"
#include "Randomizer/WorldMeta.h"

#include "al/Library/Thread/AsyncFunctorThread.h"
#include "game/Sequence/WorldResourceLoader.h"
#include "sead/heap/seadHeap.h"

namespace SubAreaResourceLoad {
    namespace {

        // HakoniwaSequence::exeLoadStage resolves its destination through
        // WorldList::tryFindWorldIndexByStageName, which maps all 251 sub-stages
        // onto their vanilla home kingdom. Entrance rando crosses those kingdom
        // boundaries constantly, so every cross-kingdom sub-area entry made the
        // sequence destroy the whole world resource set and reload the
        // destination kingdom's - 190-277 MB decompressed for the big kingdoms -
        // and then do it again on the way back, for a sub-area whose own
        // placement needs 20-30 MB.
        //
        // Skipping that swap is safe because the world resource list is a
        // prefetch, not a requirement. al::setCurrentCategoryNameDefault points
        // the default resource category at the world resource category whenever
        // that heap exists, and ResourceSystem::createResource allocates into
        // the category's own heap, so anything the sub-area is missing is
        // demand-loaded into the same 855 MB WorldHeapResource during scene
        // init - behind the same loading screen, since exeLoadStage stays put
        // until tryEndSceneInitThread.
        //
        // Retail already loads sub-areas exactly this way. WorldList's per
        // scenario lists deliberately leave sub-area content out: entering
        // BikeSteelExStage from City has 10 of its 17 archives unlisted in every
        // one of City's nine scenarios, its own ground geometry
        // (BikeSteelExGround000) among them, and the same holds for
        // AnimalChaseExGround000, FogMountainExGround000 and the rest. Those
        // always arrive by demand load. Nor does the game reload world resources
        // for a sub-area of the current kingdom when the scenario has moved on
        // since the resident set was built - requestLoadWorldHomeStageResource
        // compares mLoadWorldId and nothing else. So all this changes is which
        // kingdom's shared set happens to sit alongside the sub-area, and the
        // sub-area's own content was never in that set to begin with.
        //
        // Deliberately subtractive: this never starts the loader thread, never
        // destroys a world resource, and never turns a "no load needed" answer
        // into a "wait for the loader" one. al::AsyncFunctorThread::start()
        // clears mIsDone and only the worker sets it again, so any path that
        // reports a load it did not actually start hangs exeLoadWorldResource
        // forever. Suppressing can only ever remove a wait, never add one.

        // Keeping the resident set means the sub-area's additions are not
        // reclaimed until the next genuine kingdom change. Two ceilings bound
        // that: the 855 MB WorldHeapResource ExpHeap, and the 0x400-entry node
        // pool ResourceSystem::addCategory allocates for the category. Stop
        // suppressing before either is in reach and let one ordinary swap reset
        // both - a rare long load instead of an allocation failure.
        constexpr size_t kHeapReserveBytes = 128 * 1024 * 1024;
        constexpr s32 kMaxKeptTransitions = 12;

        // Mirrors loader->mLoadWorldId as of the last decision, so a genuine
        // (re)load - including the tryDestroyWorldResource() callers that reset
        // it to -1 - is observed as a change and re-bases the budget.
        s32 sResidentWorldId = -1;
        s32 sKeptTransitions = 0;

    } // namespace

    bool shouldKeepResidentWorld(WorldResourceLoader* loader, s32 requestedWorldId,
        const char* destinationStage, s32 destinationWorldId,
        bool isStartRedirect) {
        if (!loader)
            return false;

        if (loader->mLoadWorldId != sResidentWorldId) {
            sResidentWorldId = loader->mLoadWorldId;
            sKeptTransitions = 0;
        }

        // Nothing is resident: boot, a return to title, or any other
        // tryDestroyWorldResource() caller. There is no set worth keeping and no
        // category to demand-load into, so this has to be a real load. This is
        // the guard that keeps the very first load into a kingdom on the vanilla
        // path.
        if (loader->mLoadWorldId < 0 || !loader->mWorldResourceHeap)
            return false;

        // A load is already in flight. Leave the request to the original, which
        // knows how to no-op against its own worker.
        if (!loader->mWorldResourceLoader || !loader->mWorldResourceLoader->mIsDone)
            return false;

        // The original already no-ops when the world matches, and the randomized
        // start redirect is the most load-bearing path in the mod. Leave both
        // exactly as they are.
        if (requestedWorldId < 0 || requestedWorldId == loader->mLoadWorldId || isStartRedirect)
            return false;

        if (!destinationStage || !destinationStage[0])
            return false;

        // Only act on a request the pending stage change itself produced. The
        // other requestLoadWorldHomeStageResource callers derive their world id
        // from getCurrentWorldId instead, so the two views can disagree - and a
        // disagreement means this is not the transition being reasoned about.
        if (destinationWorldId != requestedWorldId)
            return false;

        // Only sub-areas. Arriving at a kingdom's own main stage is a real
        // kingdom change and has to keep the vanilla swap.
        if (WorldMeta::findByMainStage(destinationStage))
            return false;
        if (!EntranceDB::isTrackedSubStage(destinationStage))
            return false;

        if (sKeptTransitions >= kMaxKeptTransitions)
            return false;

        // getMaxAllocatableSize rather than getFreeSize: the world resource heap
        // is an ExpHeap and only the former accounts for fragmentation, which is
        // what an archive allocation actually has to fit into.
        if (loader->mWorldResourceHeap->getMaxAllocatableSize(8) < kHeapReserveBytes)
            return false;

        ++sKeptTransitions;
        return true;
    }

} // namespace SubAreaResourceLoad
