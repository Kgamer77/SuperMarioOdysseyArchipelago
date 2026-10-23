#include "Randomizer/SeedGenerator.h"

#include "Randomizer/BingoBoard.h"
#include "Randomizer/EntranceRando.h"
#include "Randomizer/MoonRando.h"
#include "Randomizer/Nuzlocke.h"
#include "Randomizer/RandoSaveFile.h"
#include "Randomizer/Randomizer.h"
#include "Randomizer/TrackerBridge.h"

#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Thread/AsyncFunctorThread.h"

#include "nn/os.h"
#include "nn/util.h"
#include "sead/heap/seadExpHeap.h"
#include "sead/heap/seadHeapMgr.h"

namespace SeedGenerator {
    namespace {
        class GeneratorWorker {
        public:
            void run();
        };

        bool isValidSlot(s32 slot) {
            return slot >= 1 && slot <= 10;
        }

        GeneratorWorker sWorker;
        al::AsyncFunctorThread* sThread = nullptr;
        sead::Heap* sGeneratorHeap = nullptr;
        volatile bool sRunning = false;
        volatile bool sDone = true;
        volatile bool sSucceeded = false;
        volatile s32 sProgressPercent = 0;
        const char* volatile sStatus = "Idle";
        s32 sSlot = -1;
        SaveRedirect::Category sCategory = SaveRedirect::Category::SinglePlayer;
        enum class Job : u8
        {
            NewSlot,
            SoftRespawn
        };
        Job sJob = Job::NewSlot;
        SoftSeedRequest sSoftRequest = {};

        // 0x80000 stack + thread/delegate/message-queue overhead.
        constexpr size_t kGeneratorHeapSize = 0xC0000; // 768 KB
        // nn::os thread priorities are valid only in [0, 31] (0 = highest,
        // 31 = lowest); CreateThread asserts otherwise. Use a low (background)
        // priority below the main thread so the progress UI keeps animating
        // while the worker runs. (Was 45, which is out of range and asserted
        // at boot during SeedGenerator::prepareThread().)
        constexpr s32 kGeneratorThreadPriority = 20;

        // Status text is COPIED into double-buffered static storage before the
        // published pointer flips. Callers may pass stack-local strings; storing
        // the caller's pointer directly dangles once their frame unwinds, and the
        // progress UI then renders reused stack memory as garbage text. The
        // buffer not being written stays intact, so a reader holding last
        // frame's pointer is always looking at complete, immutable text.
        char sStatusBufs[2][96];
        s32 sStatusBufNext = 0;

        void setProgress(s32 percent, const char* status) {
            if (percent < 0)
                percent = 0;
            if (percent > 100)
                percent = 100;
            sProgressPercent = percent;
            char* buf = sStatusBufs[sStatusBufNext];
            nn::util::SNPrintf(buf, sizeof(sStatusBufs[0]), "%s", status ? status : "");
            sStatus = buf;
            sStatusBufNext ^= 1;
            nn::os::YieldThread();
        }

        void ensureThread() {
            if (sThread)
                return;

            // The worker needs a large (0x80000) stack for the fill's LogicState.
            // The stack is allocated from the *current* sead heap inside the ctor,
            // and no live heap has 0.5 MB free by title time — so allocate it from
            // the dedicated heap carved early by createHeap(). Fall back to the
            // stationed heap only if that heap was never made.
            sead::Heap* heap = sGeneratorHeap ? sGeneratorHeap : al::getStationedHeap();
            Randomizer::logPush("SeedGenerator: create thread heap=%p dedicated=%d",
                heap, (int)(heap == sGeneratorHeap));
            sead::ScopedCurrentHeapSetter heapSetter(heap);
            sThread = new al::AsyncFunctorThread(
                "SeedGenerator",
                al::FunctorV0M<GeneratorWorker*, void (GeneratorWorker::*)()>(
                    &sWorker, &GeneratorWorker::run),
                kGeneratorThreadPriority, 0x80000, { 0 });
            Randomizer::logPush("SeedGenerator: thread created ptr=%p", sThread);
        }
    } // namespace

    void GeneratorWorker::run() {
        Randomizer::logPush("SeedGenerator: worker entered slot=%d job=%d", sSlot,
            static_cast<s32>(sJob));
        sDone = false;
        sSucceeded = false;

        if (sJob == Job::SoftRespawn) {
            setProgress(8, "Reshuffling seed");
            // This intentionally performs only table generation.  The active
            // GameDataFile and every save write remain on the sequence thread,
            // after the old scene and world resources are gone.
            sSucceeded = Nuzlocke::runSoftSeedJob(sSoftRequest);
            setProgress(sSucceeded ? 100 : 0,
                sSucceeded ? "Reshuffle complete" : "Reshuffle failed");
            Randomizer::logPush("SeedGenerator: soft worker finished success=%d", (int)sSucceeded);
            sRunning = false;
            sDone = true;
            return;
        }

        setProgress(2, "Clearing previous seed data");
        // These are active-save progression fields, not generation settings.
        // Generation can begin while the previous slot is still resident in the
        // process, so clear them before RandoSaveFile::write builds the new slot's
        // initial Meta block.
        Randomizer::setVisitedKingdoms(0);
        Randomizer::resetKingdomVisitOrder();
        Randomizer::setForcedPeaceKingdoms(0);
        RandoSaveFile::resetAutoGrantState();
        MoonRando::clear();
        EntranceRando::reset();
        setProgress(8, "Building randomizer tables");
        const bool generated = Randomizer::reinit();
        if (generated) {
            setProgress(92, "Writing save file");
            sSucceeded = RandoSaveFile::write(sSlot, sCategory);
            if (sSucceeded) {
                TrackerBridge::saveRoomCodeToSlot(sSlot, sCategory);
                sSucceeded = Bingo::generate(sSlot, sCategory);
            }
        } else {
            sSucceeded = false;
            setProgress(0, MoonRando::getFailureMessage());
        }
        char completionStatus[96];
        const s32 autoGrantCount = sSucceeded ? RandoSaveFile::getAutoGrantCount() : 0;
        if (sSucceeded && autoGrantCount > 0) {
            nn::util::SNPrintf(completionStatus, sizeof(completionStatus),
                "Done (%d items granted at start)", autoGrantCount);
        } else if (sSucceeded) {
            nn::util::SNPrintf(completionStatus, sizeof(completionStatus), "Done");
        } else {
            nn::util::SNPrintf(completionStatus, sizeof(completionStatus), "%s",
                generated ? "Save write failed" : MoonRando::getFailureMessage());
        }
        setProgress(sSucceeded ? 100 : 0, completionStatus);
        Randomizer::logPush("SeedGenerator: worker finished success=%d progress=%d",
            (int)sSucceeded, (int)sProgressPercent);

        sRunning = false;
        sDone = true;
    }

    void createHeap() {
        if (sGeneratorHeap)
            return;
        sead::Heap* parent = al::getStationedHeap();
        if (!parent) {
            Randomizer::logPush("SeedGenerator: no stationed heap for worker");
            return;
        }
        const size_t maxAlloc = parent->getMaxAllocatableSize(8);
        if (maxAlloc < kGeneratorHeapSize + 0x4000) {
            Randomizer::logPush("SeedGenerator: worker heap skipped max=%u need=%u",
                (u32)maxAlloc, (u32)(kGeneratorHeapSize + 0x4000));
            return; // leave the fallback (stationed) path; better than asserting
        }
        sGeneratorHeap = sead::ExpHeap::create(kGeneratorHeapSize, "SeedGeneratorHeap",
            parent, 8, sead::Heap::cHeapDirection_Forward, false);
        Randomizer::logPush("SeedGenerator: worker heap=%p", sGeneratorHeap);
    }

    bool prepareThread() {
        ensureThread();
        return sThread != nullptr;
    }

    bool isPrepared() {
        return sThread != nullptr;
    }

    bool start(s32 slot, SaveRedirect::Category category) {
        if (!isValidSlot(slot) || sRunning)
            return false;

        Randomizer::logPush(
            "SeedGenerator: start requested slot=%d category=%d", slot, (int)category);
        ensureThread();
        if (!sThread) {
            sRunning = false;
            sDone = true;
            sSucceeded = false;
            setProgress(0, "Generator thread unavailable");
            Randomizer::logPush("SeedGenerator: start failed, no thread");
            return false;
        }

        sSlot = slot;
        sCategory = category;
        sJob = Job::NewSlot;
        sSucceeded = false;
        sDone = false;
        sRunning = true;
        setProgress(1, "Queued");
        Randomizer::logPush("SeedGenerator: calling thread start");
        sThread->start();
        setProgress(3, "Worker starting");
        Randomizer::logPush("SeedGenerator: thread start returned");
        return true;
    }

    bool startSoft(const SoftSeedRequest& request) {
        if (!isValidSlot(request.slot) || !request.requestedSeed || sRunning)
            return false;

        ensureThread();
        if (!sThread) {
            sRunning = false;
            sDone = true;
            sSucceeded = false;
            setProgress(0, "Generator thread unavailable");
            return false;
        }

        sSlot = request.slot;
        // SoftRespawn never calls RandoSaveFile::write/Bingo::generate/
        // TrackerBridge::saveRoomCodeToSlot (see run()'s SoftRespawn branch) -
        // this just keeps sCategory from holding a stale value from a
        // previous NewSlot generation in case something reads getCategory().
        sCategory = SaveRedirect::getActiveCategory();
        sSoftRequest = request;
        sJob = Job::SoftRespawn;
        sSucceeded = false;
        sDone = false;
        sRunning = true;
        setProgress(1, "Queued soft respawn");
        sThread->start();
        setProgress(3, "Worker starting");
        return true;
    }

    bool isRunning() {
        return sRunning;
    }

    bool isDone() {
        return sDone;
    }

    bool succeeded() {
        return sDone && sSucceeded;
    }

    s32 progressPercent() {
        return sProgressPercent;
    }

    const char* status() {
        return sStatus;
    }

    s32 getSlot() {
        return sSlot;
    }

    SaveRedirect::Category getCategory() {
        return sCategory;
    }

    void reportProgress(s32 percent, const char* status) {
        if (sRunning)
            setProgress(percent, status);
    }
} // namespace SeedGenerator
