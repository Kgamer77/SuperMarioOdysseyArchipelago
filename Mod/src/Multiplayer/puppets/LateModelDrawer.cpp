#include "Multiplayer/puppets/LateModelDrawer.h"

#include "sead/heap/seadHeapMgr.h"

#include "hk/hook/Trampoline.h"
#include "hk/ro/RoUtil.h"

#include "al/Library/Draw/GraphicsSystemInfo.h"
#include "al/Library/Execute/ActorExecuteInfo.h"
#include "al/Library/Execute/ExecuteRequestKeeper.h"
#include "al/Library/Execute/ExecutorActorExecuteBase.h"
#include "al/Library/LiveActor/ActorFlagFunction.h"
#include "al/Library/LiveActor/ActorInitInfo.h"
#include "al/Library/LiveActor/ActorModelFunction.h"
#include "al/Library/LiveActor/ActorPoseUtil.h"
#include "al/Library/LiveActor/ActorSceneInfo.h"
#include "al/Library/LiveActor/LiveActor.h"
#include "al/Library/LiveActor/LiveActorFlag.h"
#include "al/Library/LiveActor/LiveActorFunction.h"
#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Model/ModelDrawerBase.h"
#include "al/Library/Model/ModelKeeper.h"
#include "al/Library/Shadow/DepthShadowMapDirector.h"
#include "al/Library/Shadow/ShadowDirector.h"

#include "Multiplayer/MpDebugLog.h"

#include <cstdint>
#include <cstring>

#include "al/Library/Execute/ExecuteDirector.h"
#include "al/Library/Execute/ExecuteTableHolderDraw.h"
#include "al/Library/Execute/ExecuteTableHolderUpdate.h"
#include "al/Library/Execute/ExecutorListActorExecute.h"
#include "al/Library/Execute/ExecutorListActorModelDraw.h"

namespace {

    s32 sLateBuildDepth = 0;
    s32 sPuppetCostumeBuildDepth = 0;

    constexpr s32 cExecutorHeadroom = 64;
    constexpr s32 cExecutorListHeadroom = 32;
    constexpr s32 cModelHeadroom = 32;
    constexpr s32 cTrackedBufferCount = 256;

    struct ActorBufferInfo {
        al::ExecutorActorExecuteBase* executor = nullptr;
        void** buffer = nullptr;
        s32 capacity = 0;
    };

    struct ModelBufferInfo {
        al::ModelDrawerBase* drawer = nullptr;
        al::ModelCtrl** buffer = nullptr;
        s32 capacity = 0;
    };

    ActorBufferInfo sActorBuffers[cTrackedBufferCount];
    ModelBufferInfo sModelBuffers[cTrackedBufferCount];

    s32 nextCapacity(s32 current, s32 minimum) {
        s32 next = current > 0 ? current * 2 : 8;
        if (next < minimum)
            next = minimum;
        return next;
    }

    template <typename T>
    void growRawArray(T**& buffer, s32& capacity, s32 size, s32 minimum) {
        if (minimum <= capacity && buffer)
            return;

        const s32 oldSize = size > 0 ? size : 0;
        const s32 newCapacity = nextCapacity(capacity, minimum);
        T** replacement = new T*[newCapacity];
        for (s32 i = 0; i < newCapacity; i++)
            replacement[i] = nullptr;

        if (buffer && oldSize > 0) {
            const s32 copyCount = oldSize < newCapacity ? oldSize : newCapacity;
            std::memcpy(replacement, buffer, sizeof(T*) * copyCount);
        }

        // These arrays live on the scene heap and are owned by the engine. We
        // intentionally leave the old allocation alone: freeing it here would
        // race the engine's teardown bookkeeping and is unnecessary for the
        // small, bounded amount of late registration this feature performs.
        buffer = replacement;
        capacity = newCapacity;
    }

    void growUpdateActorListArray(al::ExecuteTableHolderUpdate* table) {
        if (!table)
            return;

        // The draw holder decompiles as bare (capacity, size, pointer)
        // triples; the update holder models the same three words as a
        // sead::PtrArray, so reach them through the container instead of
        // requiring a flattened copy of the upstream header. Nothing in
        // lib/OdysseyHeaders has to change for this.
        //
        // CAUTION: the two count words carry the OPPOSITE meanings to sead's
        // own, exactly as they do for ExecutorActorExecuteBase::mActors below.
        // From IDA on ExecuteTableHolderUpdate (member offsets in brackets):
        //
        //   init()                     [0x30] = allocation size,
        //                              [0x38] = new[](allocation size)
        //   registerExecutorListActor  appends at [0x38][ [0x34]++ ]
        //   tryRegisterActor           loops i < [0x34], calls
        //                              isEqualString([0x38][i]->mName, ...)
        //   createExecutorListTable    loops i < [0x34]
        //
        // [0x30] is mPtrNum, [0x34] is mPtrNumMax, [0x38] is mPtrs. So
        // mPtrNumMax is the LIVE COUNT and the native loop bound, and mPtrNum
        // is the allocation size. Writing a grown capacity into mPtrNumMax
        // makes tryRegisterActor walk the zeroed tail of the new buffer and
        // dereference a null list - a crash inside al::isEqualString with
        // x0 = 0, reached from ExecuteDirector::registerActorUpdate.
        sead::PtrArray<al::ExecutorListActorExecuteBase>& lists = table->mExecutorsActor;

        const s32 listCount = lists.mPtrNumMax;
        const s32 minimum = listCount + cExecutorListHeadroom;
        if (lists.mPtrs && lists.mPtrNum >= minimum)
            return;

        // Only the allocation size and the buffer are replaced. The live count
        // is left strictly alone; the copied entries keep their indices, so
        // every native loop bound stays valid across the swap.
        auto* buffer = reinterpret_cast<al::ExecutorListActorExecuteBase**>(lists.mPtrs);
        s32 capacity = lists.mPtrNum;
        growRawArray(buffer, capacity, listCount, minimum);
        lists.mPtrs = reinterpret_cast<void**>(buffer);
        lists.mPtrNum = capacity;
    }

    ActorBufferInfo* findActorBufferInfo(al::ExecutorActorExecuteBase* executor) {
        for (s32 i = 0; i < cTrackedBufferCount; i++) {
            if (sActorBuffers[i].executor == executor)
                return &sActorBuffers[i];
        }

        for (s32 i = 0; i < cTrackedBufferCount; i++) {
            if (!sActorBuffers[i].executor) {
                sActorBuffers[i].executor = executor;
                sActorBuffers[i].buffer = executor->mActors.mPtrs;
                sActorBuffers[i].capacity = executor->mActors.mPtrs ? executor->mActors.mPtrNum : 0;
                return &sActorBuffers[i];
            }
        }

        return nullptr;
    }

    ModelBufferInfo* findModelBufferInfo(al::ModelDrawerBase* drawer) {
        for (s32 i = 0; i < cTrackedBufferCount; i++) {
            if (sModelBuffers[i].drawer == drawer)
                return &sModelBuffers[i];
        }

        for (s32 i = 0; i < cTrackedBufferCount; i++) {
            if (!sModelBuffers[i].drawer) {
                sModelBuffers[i].drawer = drawer;
                sModelBuffers[i].buffer = drawer->mModels;
                sModelBuffers[i].capacity = drawer->mModels ? drawer->mModelMaxNum : 0;
                return &sModelBuffers[i];
            }
        }

        return nullptr;
    }

    // The native ExecutorActorExecuteBase uses its PtrArray storage with the
    // two count fields in the opposite semantic roles used by sead's public
    // helpers: mPtrNum is the number of actors registered with the executor
    // and mPtrNumMax is the number of currently active actors. The pointer
    // array allocated by createExecutorTable has no separately stored
    // capacity. This is confirmed by IDA for
    // registerActor/createExecutorTable/addActor/removeActor.
    //
    // Keep the extra allocation size in our side table and leave mPtrNum
    // alone when growing. Late registerActor() calls increment mPtrNum; using
    // our side capacity prevents that count from being mistaken for the
    // allocation size on the next late build.
    void growActorExecutor(al::ExecutorActorExecuteBase* executor) {
        if (!executor)
            return;

        ActorBufferInfo* info = findActorBufferInfo(executor);
        if (!info)
            return;

        if (info->buffer != executor->mActors.mPtrs) {
            info->buffer = executor->mActors.mPtrs;
            info->capacity = executor->mActors.mPtrs ? executor->mActors.mPtrNum : 0;
        }

        const s32 registeredCount = executor->mActors.mPtrNum;
        const s32 activeCount = executor->mActors.mPtrNumMax;
        const s32 requiredCapacity = registeredCount + 1;

        if (executor->mActors.mPtrs && info->capacity >= requiredCapacity)
            return;

        const s32 newCapacity = nextCapacity(info->capacity, registeredCount + cExecutorHeadroom);
        al::LiveActor** replacement = new al::LiveActor*[newCapacity];
        for (s32 i = 0; i < newCapacity; i++)
            replacement[i] = nullptr;

        if (executor->mActors.mPtrs && activeCount > 0) {
            const s32 copyCount = activeCount < newCapacity ? activeCount : newCapacity;
            std::memcpy(replacement, executor->mActors.mPtrs, sizeof(al::LiveActor*) * copyCount);
        }

        executor->mActors.mPtrs = reinterpret_cast<void**>(replacement);
        info->buffer = executor->mActors.mPtrs;
        info->capacity = newCapacity;
    }

    void growModelDrawer(al::ModelDrawerBase* drawer) {
        if (!drawer)
            return;

        // A drawer created by a late actor has already received its native
        // registerModel() count, but createTable() has not run yet. Do not
        // install a plain mModels array here: the concrete drawer's
        // createTable() also creates its mesh/display resources, and
        // fixupActor() must be allowed to perform that complete initialization.
        // Installing only the array makes the drawer look initialized and it
        // is subsequently removed as an unusable draw path.
        if (!drawer->mModels)
            return;

        ModelBufferInfo* info = findModelBufferInfo(drawer);
        if (!info)
            return;

        if (info->buffer != drawer->mModels) {
            info->buffer = drawer->mModels;
            info->capacity = drawer->mModels ? drawer->mModelMaxNum : 0;
        }

        const s32 registeredCount = drawer->mModelMaxNum;
        const s32 oldCapacity = info->capacity > 0 ? info->capacity : 0;
        if (drawer->mModels && info->capacity >= registeredCount + 1)
            return;

        const s32 newCapacity = nextCapacity(info->capacity, registeredCount + cModelHeadroom);
        al::ModelCtrl** replacement = new al::ModelCtrl*[newCapacity];
        for (s32 i = 0; i < newCapacity; i++)
            replacement[i] = nullptr;

        if (drawer->mModels && oldCapacity > 0) {
            // removeModel() retains registered-but-inactive models in the
            // tail [mModelNum, mModelMaxNum), so copying only the active prefix
            // makes later wake requests forget those registrations.  The side
            // table records the actual allocation span and is the safe bound.
            const s32 copyCount = oldCapacity < newCapacity ? oldCapacity : newCapacity;
            std::memcpy(replacement, drawer->mModels, sizeof(al::ModelCtrl*) * copyCount);
        }

        // ModelDrawerBase::registerModel increments mModelMaxNum for every
        // newly registered model. Keep that native count unchanged here and
        // track the real allocation in the side table, just as for movement
        // executors above.
        drawer->mModels = replacement;
        info->buffer = replacement;
        info->capacity = newCapacity;
    }

    void prepareUpdateTable(al::ExecuteTableHolderUpdate* table, const char* tag) {
        if (!table)
            return;

        growUpdateActorListArray(table);

        // mPtrNumMax is the live list count here - see growUpdateActorListArray.
        for (s32 i = 0; i < table->mExecutorsActor.mPtrNumMax; i++) {
            al::ExecutorListActorExecuteBase* list = table->mExecutorsActor.unsafeAt(i);
            if (!list)
                continue;

            growRawArray(list->mList, list->mCapacity, list->mSize,
                list->mSize + cExecutorListHeadroom);

            for (s32 j = 0; j < list->mSize; j++)
                growActorExecutor(list->mList[j]);
        }

        (void)tag;
    }

    void prepareDrawTable(al::ExecuteTableHolderDraw* table, const char* tag) {
        if (!table)
            return;

        growRawArray(table->mExecutorsAll, table->mExecutorsAllCapacity,
            table->mExecutorsAllSize, table->mExecutorsAllSize + cExecutorListHeadroom);
        growRawArray(table->mExecutorsActor, table->mExecutorsActorCapacity,
            table->mExecutorsActorSize, table->mExecutorsActorSize + cExecutorListHeadroom);
        growRawArray(table->mExecutorsActorModel, table->mExecutorsActorModelCapacity,
            table->mExecutorsActorModelSize,
            table->mExecutorsActorModelSize + cExecutorListHeadroom);

        for (s32 i = 0; i < table->mExecutorsActorModelSize; i++) {
            al::ExecutorListActorModelDrawBase* list = table->mExecutorsActorModel[i];
            if (!list)
                continue;

            growRawArray(list->mList, list->mCapacity, list->mSize,
                list->mSize + cExecutorListHeadroom);

            for (s32 j = 0; j < list->mSize; j++) {
                al::ModelDrawerBase* drawer = list->mList[j];
                if (!drawer || !drawer->mModels)
                    continue;

                // Only the drawers selected by the actor currently being
                // built can receive a new registered model. Tracking and
                // reallocating every unrelated drawer filled the bounded
                // side table before common capture drawers such as Frog were
                // reached. The post-registration pass then first saw Frog at
                // max=3 even though its physical table still had two slots,
                // copied slot[2] out of bounds, and preserved allocator words
                // such as 0x0000000500000005 as a fake inactive ModelCtrl.
                //
                // Executor/list arrays above still grow globally because the
                // initializer may create a new named list. Existing model
                // drawers are archive/executor-named (the live logs show all
                // three required Frog drawers named "Frog"), so the tag is the
                // precise model-table scope.
                if (tag && (!drawer->getName() || std::strcmp(drawer->getName(), tag) != 0))
                    continue;

                growModelDrawer(drawer);
            }
        }
    }

    // Sub-actor trees here are shallow (body -> cap/head/parts), so this only
    // guards against a cycle in a tree we did not build ourselves.
    constexpr s32 cMaxDepth = 4;

    constexpr uintptr_t cModelDrawerBufferUpdateDrawOffset = 0x009425a4;
    constexpr uintptr_t cModelDrawerDeferredDrawOffset = 0x00942ca0;
    constexpr uintptr_t cModelDrawerDepthOnlyDrawOffset = 0x00944b5c;
    constexpr uintptr_t cModelDrawerDepthShadowDrawOffset = 0x009458e8;
    constexpr uintptr_t cModelDrawerForwardDrawOffset = 0x00945e04;
    constexpr uintptr_t cDepthShadowRegisterOffset = 0x00a18338;
    constexpr uintptr_t cDepthShadowUpdateViewOffset = 0x00a19c80;
    constexpr uintptr_t cDepthShadowUpdateOffset = 0x00a1a260;

    // DepthShadowMapInfo is intentionally kept opaque here. IDA shows its
    // selected ShadowInfo at +0x98. A late capture actor can have a perfectly
    // valid DepthShadowMapInfo (created by the archive initializer) without a
    // map: initAfterPlacement is the pass that normally fills this field.
    // The director's update list assumes the field is non-null, so do not put
    // an unmaterialized entry on that list and make the two update boundaries
    // defensive for entries registered before this hook is installed.
    bool hasDepthShadowInfo(void* mapInfo) {
        if (!mapInfo)
            return false;

        return *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(mapInfo) + 0x98) != nullptr;
    }

    static HkTrampoline<al::DepthShadowMapDirector*, al::DepthShadowMapDirector*, void*>
        depthShadowRegisterHook = hk::hook::trampoline(
            [](al::DepthShadowMapDirector* director, void* mapInfo)
                -> al::DepthShadowMapDirector* {
                if (LateModelDrawer::isLateBuildActive() || !hasDepthShadowInfo(mapInfo))
                    return director;

                return depthShadowRegisterHook.orig(director, mapInfo);
            });

    static HkTrampoline<void, al::DepthShadowMapDirector*, void*> depthShadowUpdateHook
        = hk::hook::trampoline([](al::DepthShadowMapDirector* director, void* shadowInfo) -> void {
              if (!shadowInfo)
                  return;

              depthShadowUpdateHook.orig(director, shadowInfo);
          });

    static HkTrampoline<void, al::DepthShadowMapDirector*, void*, const void*, const void*>
        depthShadowUpdateViewHook = hk::hook::trampoline(
            [](al::DepthShadowMapDirector* director, void* shadowInfo, const void* camera,
                const void* projection) -> void {
                if (!shadowInfo)
                    return;

                depthShadowUpdateViewHook.orig(director, shadowInfo, camera, projection);
            });

    // ModelDrawerForward::draw() checks only mModelNum before passing all
    // three draw resources to ModelDrawDisplayList::prepDraw(). A
    // ForwardOnly drawer with no forward/indirect shader assignment is a
    // valid native no-op and has mMeshDrawerTable/mDrawDisplayList == nullptr;
    // late registration must never make that drawer drawable. Keep this
    // guard at the native draw boundary because a drawer can be shared by
    // several actors and a per-actor repair cannot safely quiesce it by
    // changing its shared mModelNum.
    static HkTrampoline<void, const al::ModelDrawerBase*> modelDrawerForwardDrawHook
        = hk::hook::trampoline([](const al::ModelDrawerBase* drawer) -> void {
              if (!drawer)
                  return;

              if (drawer->mModelNum > 0
                  && (!drawer->mModels || !drawer->mMeshDrawerTable || !drawer->mDrawDisplayList)) {
                  return;
              }

              modelDrawerForwardDrawHook.orig(drawer);
          });

    uintptr_t getDrawerDrawFunction(al::ModelDrawerBase* drawer) {
        if (!drawer)
            return 0;

        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(drawer);
        return vtable ? vtable[3] : 0;
    }

    uintptr_t getDrawerAddFunction(al::ModelDrawerBase* drawer) {
        if (!drawer)
            return 0;

        uintptr_t* vtable = *reinterpret_cast<uintptr_t**>(drawer);
        return vtable ? vtable[5] : 0;
    }

    bool drawerNeedsDisplayList(uintptr_t drawFunction, uintptr_t mainStart) {
        return drawFunction == mainStart + cModelDrawerDeferredDrawOffset
            || drawFunction == mainStart + cModelDrawerDepthOnlyDrawOffset
            || drawFunction == mainStart + cModelDrawerDepthShadowDrawOffset
            || drawFunction == mainStart + cModelDrawerForwardDrawOffset;
    }

    bool hasUsableDrawResources(al::ModelDrawerBase* drawer) {
        if (!drawer)
            return false;

        auto* mainModule = hk::ro::getMainModule();
        if (!mainModule)
            return true;

        const uintptr_t drawFunction = getDrawerDrawFunction(drawer);
        // BufferUpdate only advances model draw buffers and intentionally has
        // neither a mesh table nor a display list.
        if (drawFunction == mainModule->range().start() + cModelDrawerBufferUpdateDrawOffset)
            return true;

        // Every other ModelDrawer used by the game consumes a MeshDrawer. The
        // four display-list based drawers additionally pass mDrawDisplayList
        // to ModelDrawDisplayList::prepDraw/callDraw; Chromakey, silhouette,
        // footprint, sky, and the other manual mesh passes do not.
        if (!drawer->mMeshDrawerTable)
            return false;
        return !drawerNeedsDisplayList(drawFunction, mainModule->range().start())
            || drawer->mDrawDisplayList;
    }

    bool removeModelFromDrawer(al::ModelDrawerBase* drawer, al::ModelCtrl* model) {
        if (!drawer || !drawer->mModels || !model)
            return false;

        for (s32 i = 0; i < drawer->mModelNum; ++i) {
            if (drawer->mModels[i] != model)
                continue;

            --drawer->mModelNum;
            drawer->mModels[i] = drawer->mModels[drawer->mModelNum];
            drawer->mModels[drawer->mModelNum] = nullptr;
            return true;
        }

        return false;
    }

    void fixupActor(const al::ActorInitInfo& initInfo, al::LiveActor* actor, const char* tag,
        s32 depth) {
        if (!actor || depth > cMaxDepth)
            return;

        al::ActorExecuteInfo* executeInfo = actor->getExecuteInfo();
        if (executeInfo) {
            // The movement pipeline has the exact same shape as the draw one:
            // al::initExecutorUpdate registers the actor with an
            // al::ExecutorActorExecuteBase whose mActors buffer is only
            // allocated by createExecutorTable(), which
            // al::ExecutorListActorExecuteBase::createList() calls once at
            // scene init. A late actor is added to a buffer-less executor the
            // frame it goes live and addActor() stores to 0x0 - which is
            // exactly what took the guest down the first time a puppet
            // actually leased a capture model (Ryujinx log, invalid access at
            // 0x0 in al::ExecutorActorExecuteBase::addActor via
            // executeRequestActorMovementAllOn).
            //
            // The capture models had survived until then only because nothing
            // had ever woken one.
            const s32 updaterNum = executeInfo->mUpdaterCount;
            for (s32 i = 0; i < updaterNum; i++) {
                al::ExecutorActorExecuteBase* updater = executeInfo->mUpdaters[i];
                if (!updater || updater->mActors.isBufferReady())
                    continue;

                MpDebugLog::write(
                    "[LateDrawer] %s: actor=%p depth=%d updater[%d]=%p (%s) NO TABLE cap=%d num=%d",
                    tag, actor, depth, i, updater, updater->getName(), updater->mActors.capacity(),
                    updater->mActors.size());

                // createExecutorTable() would be the natural call here, but it
                // is non-virtual and not exported - "ld.lld: undefined symbol"
                // is the fifth al function this session that is declared in
                // the decomp headers yet unreachable from mod code. So set the
                // buffer up directly instead: PtrArrayImpl's three fields are
                // public and PtrArray derives from it publicly, which needs no
                // symbol at all.
                //
                // al::ExecutorActorMovementCalcAnim::execute() iterates
                // mPtrNumMax, not mPtrNum - measured, not assumed. Two crashes
                // in a row faulted on a null slot with the loop bound in X[21]
                // equal to whatever this code had put in mPtrNumMax: 1 when
                // capacity was 1 (01787923437), 5 when capacity was 5
                // (01787924177). So any slot the executor can reach must
                // already hold a live actor, and a buffer sized for actors that
                // are not in it yet is guaranteed to fault.
                //
                // A real, zeroed buffer is still installed so
                // isBufferReady() is true and an unguarded addActor() has
                // somewhere valid to write. Keep the native registration
                // count (mPtrNum) intact; addActor() uses mPtrNumMax as its
                // active count, and makeActorAlive()/makeActorDead() will
                // maintain that count normally.
                constexpr s32 cSlackSlots = 4;
                const s32 registeredCount = updater->mActors.mPtrNum > 0
                    ? updater->mActors.mPtrNum
                    : 0;
                const s32 bufferSlots = registeredCount + cSlackSlots;

                void** buffer = new void*[bufferSlots];
                for (s32 slot = 0; slot < bufferSlots; slot++)
                    buffer[slot] = nullptr;

                updater->mActors.mPtrs = buffer;
                updater->mActors.mPtrNumMax = 0;

                MpDebugLog::write(
                    "[LateDrawer] %s: updater=%p buffer=%p left inert (registered=%d slots=%d active=0)",
                    tag, updater, updater->mActors.mPtrs, registeredCount, bufferSlots);
            }

            s32 kept = 0;
            const s32 drawerNum = executeInfo->mDrawerCount;
            for (s32 i = 0; i < drawerNum; i++) {
                al::ModelDrawerBase* drawer = executeInfo->mDrawers[i];

                if (drawer && !drawer->mModels) {
                    // mModelMaxNum is normally set by the registerModel() pass
                    // that runs at initExecutorDraw() time, so it should
                    // already be right. Log it rather than trust it: a zero
                    // here would make createTable() allocate nothing and the
                    // store would still land on a bad address, and that is
                    // exactly the kind of thing the log needs to say out loud
                    // rather than leave to guesswork.
                    MpDebugLog::write(
                        "[LateDrawer] %s: actor=%p depth=%d drawer[%d]=%p (%s) vt=%p NO TABLE max=%d num=%d mesh=%p",
                        tag, actor, depth, i, drawer, drawer->getName(), *(void**)drawer,
                        drawer->mModelMaxNum, drawer->mModelNum, drawer->mMeshDrawerTable);

                    if (drawer->mModelMaxNum < 1)
                        drawer->mModelMaxNum = 1;

                    drawer->createTable();

                    MpDebugLog::write(
                        "[LateDrawer] %s: drawer=%p after createTable models=%p max=%d num=%d mesh=%p",
                        tag, drawer, drawer->mModels, drawer->mModelMaxNum, drawer->mModelNum,
                        drawer->mMeshDrawerTable);
                }

                if (!drawer)
                    continue;

                if (!drawer->mModels) {
                    if (drawer->mModelMaxNum < 1)
                        drawer->mModelMaxNum = 1;
                    drawer->createTable();
                }

                if (!drawer->mModels) {
                    // The global list is authoritative and may be shared.
                    // Keep the entry there, but do not let this actor wake
                    // through an unusable drawer.
                    MpDebugLog::write(
                        "[LateDrawer] %s: detaching drawer without model table actor=%p drawer=%p",
                        tag, actor, drawer);
                    continue;
                }

                // Several executor passes have a valid no-op form: createTable()
                // allocates mModels but leaves the resources that pass does not
                // use null when the model has no matching shader assignment.
                // The native late-build path never attaches a model to such a
                // drawer. Detach only this actor's private reference and leave
                // the shared/global list intact.
                if (!hasUsableDrawResources(drawer)) {
                    auto* modelKeeper = actor->getModelKeeper();
                    auto* model = modelKeeper ? modelKeeper->getModelCtrl() : nullptr;
                    const bool removed = removeModelFromDrawer(drawer, model);

                    MpDebugLog::write(
                        "[LateDrawer] %s: detaching incomplete draw actor=%p drawer=%p model=%p removed=%d drawFn=%p mesh=%p display=%p",
                        tag, actor, drawer, model, removed ? 1 : 0,
                        reinterpret_cast<void*>(getDrawerDrawFunction(drawer)),
                        drawer->mMeshDrawerTable, drawer->mDrawDisplayList);
                    continue;
                }

                executeInfo->mDrawers[kept++] = drawer;
            }

            for (s32 i = kept; i < drawerNum; i++)
                executeInfo->mDrawers[i] = nullptr;
            executeInfo->mDrawerCount = kept;
        }

        // getSubActorNum() goes through the keeper unconditionally, so the
        // null check has to happen here - a capture model or the 2D body has
        // no sub-actors at all.
        if (!actor->getSubActorKeeper())
            return;

        const s32 subNum = al::getSubActorNum(actor);
        for (s32 i = 0; i < subNum; i++)
            fixupActor(initInfo, al::getSubActor(actor, i), tag, depth + 1);
    }

    struct ActivationSummary {
        s32 aliveActors = 0;
        s32 missingMovement = 0;
        s32 missingDraw = 0;
        s32 unsafeMovement = 0;
        s32 unsafeDraw = 0;
        bool repairQueued = false;
    };

    bool isActorActive(const al::ExecutorActorExecuteBase* updater,
        const al::LiveActor* actor) {
        if (!updater || !updater->mActors.mPtrs)
            return false;

        s32 activeCount = updater->mActors.mPtrNumMax;
        if (activeCount < 0)
            return false;
        if (activeCount > updater->mActors.mPtrNum)
            activeCount = updater->mActors.mPtrNum;

        for (s32 i = 0; i < activeCount; ++i) {
            if (updater->mActors.mPtrs[i] == actor)
                return true;
        }
        return false;
    }

    bool isModelActive(const al::ModelDrawerBase* drawer, const al::ModelCtrl* model) {
        if (!drawer || !drawer->mModels || !model)
            return false;

        s32 activeCount = drawer->mModelNum;
        if (activeCount < 0)
            return false;
        if (activeCount > drawer->mModelMaxNum)
            activeCount = drawer->mModelMaxNum;

        for (s32 i = 0; i < activeCount; ++i) {
            if (drawer->mModels[i] == model)
                return true;
        }
        return false;
    }

    // registerModel() reserves a slot by increasing mModelMaxNum, while
    // addModelToTable() expects the corresponding ModelCtrl either in the
    // inactive tail or in a null tail slot.  Late registration happens after
    // createTable(), so there is no scene-init population pass to establish
    // that registered-but-inactive state for the new model.  Seed only a null
    // slot inside the native registered span; never replace another actor's
    // sleeping model.
    bool ensureInactiveModelSlot(al::ModelDrawerBase* drawer, al::ModelCtrl* model,
        s32* slot, bool* seeded) {
        if (slot)
            *slot = -1;
        if (seeded)
            *seeded = false;

        if (!drawer || !drawer->mModels || !model || drawer->mModelNum < 0
            || drawer->mModelNum >= drawer->mModelMaxNum) {
            return false;
        }

        for (s32 i = drawer->mModelNum; i < drawer->mModelMaxNum; ++i) {
            if (drawer->mModels[i] != model)
                continue;

            if (slot)
                *slot = i;
            return true;
        }

        for (s32 i = drawer->mModelNum; i < drawer->mModelMaxNum; ++i) {
            if (drawer->mModels[i])
                continue;

            drawer->mModels[i] = model;
            if (slot)
                *slot = i;
            if (seeded)
                *seeded = true;
            return true;
        }

        return false;
    }

    bool queueExecuteRequest(al::ExecuteRequestKeeper* keeper, al::LiveActor* actor,
        al::ExecuteRequestKeeper::Request addRequest,
        al::ExecuteRequestKeeper::Request removeRequest) {
        if (!keeper || !actor)
            return false;

        al::ExecuteRequestTable* addTable = keeper->mRequestTables[addRequest];
        al::ExecuteRequestTable* removeTable = keeper->mRequestTables[removeRequest];
        if (!addTable || !removeTable || !addTable->mRequests || !removeTable->mRequests)
            return false;

        // This is exactly ExecuteRequestKeeper::request():
        // cancel the opposite request, then append only if this actor is not
        // already queued.  That method is present in main but not exported to
        // subsdk4, so spelling its public table operation out here avoids both
        // an unresolved symbol and the native method's unbounded final store.
        removeTable->removeRequest(actor);
        for (s32 i = 0; i < addTable->mSize; ++i) {
            if (addTable->mRequests[i] == actor)
                return true;
        }
        if (addTable->mSize >= addTable->mMaxSize)
            return false;

        addTable->addRequest(actor);
        return true;
    }

    void verifyActivationRecursive(al::LiveActor* actor, const char* tag, s32 depth,
        ActivationSummary* summary, bool allowDrawRepair) {
        if (!actor || !summary || depth > cMaxDepth)
            return;

        al::LiveActorFlag* flags = actor->getFlags();
        const bool isDead = flags ? flags->isDead : al::isDead(actor);
        if (isDead)
            return;

        ++summary->aliveActors;

        al::ActorExecuteInfo* executeInfo = actor->getExecuteInfo();
        al::ModelKeeper* modelKeeper = actor->getModelKeeper();
        al::ModelCtrl* model = modelKeeper ? modelKeeper->getModelCtrl() : nullptr;

        const s32 updaterCount = executeInfo ? executeInfo->mUpdaterCount : 0;
        s32 activeUpdaters = 0;
        bool movementTablesSafe = true;
        for (s32 i = 0; i < updaterCount; ++i) {
            al::ExecutorActorExecuteBase* updater = executeInfo->mUpdaters[i];
            if (isActorActive(updater, actor)) {
                ++activeUpdaters;
                continue;
            }

            ++summary->missingMovement;
            if (!updater || !updater->mActors.mPtrs) {
                movementTablesSafe = false;
                ++summary->unsafeMovement;
            }

            MpDebugLog::write(
                "[Activation] %s: actor=%p depth=%d missing updater[%d]=%p name=%s registered=%d active=%d table=%p",
                tag, actor, depth, i, updater, updater ? updater->getName() : "(null)",
                updater ? updater->mActors.mPtrNum : -1,
                updater ? updater->mActors.mPtrNumMax : -1,
                updater ? updater->mActors.mPtrs : nullptr);
        }

        const bool hidden = flags && flags->isModelHidden;
        const bool clipped = flags && flags->isClipped;
        const bool expectsDraw = model && !hidden && !clipped;
        const s32 drawerCount = executeInfo ? executeInfo->mDrawerCount : 0;
        s32 activeDrawers = 0;
        bool drawTablesSafe = true;

        if (expectsDraw && drawerCount == 0) {
            ++summary->missingDraw;
            ++summary->unsafeDraw;
            drawTablesSafe = false;
            MpDebugLog::write(
                "[Activation] %s: actor=%p depth=%d has model=%p but no draw executors",
                tag, actor, depth, model);
        }

        if (expectsDraw) {
            for (s32 i = 0; i < drawerCount; ++i) {
                al::ModelDrawerBase* drawer = executeInfo->mDrawers[i];
                if (isModelActive(drawer, model)) {
                    ++activeDrawers;
                    continue;
                }

                const bool canRepair = drawer && drawer->mModels
                    && drawer->mModelNum >= 0
                    && drawer->mModelNum < drawer->mModelMaxNum
                    && hasUsableDrawResources(drawer);

                // The normal Request_Draw reaches the request table for these
                // late actors but, as measured on both emulators, never adds
                // their model to the shared Frog drawers.  Recreate the
                // registered-but-inactive table state that scene init normally
                // supplies, then invoke the concrete drawer's own virtual
                // path. This preserves specialized Depth, Prepass, Forward,
                // and Deferred setup; calling the base implementation directly
                // would not.
                //
                // This can run on each of the three bounded verification
                // passes. addModelToTable() deduplicates an active model and
                // every concrete drawer performs its extra work only when that
                // call succeeds, so retrying is safe if a native remove request
                // crosses the first verification frame.
                if (allowDrawRepair && canRepair) {
                    s32 inactiveSlot = -1;
                    bool seeded = false;
                    const bool hasInactiveSlot
                        = ensureInactiveModelSlot(drawer, model, &inactiveSlot, &seeded);

                    MpDebugLog::write(
                        "[Activation] %s: repair drawer[%d]=%p name=%s actor=%p model=%p inactiveSlot=%d seeded=%d addFn=%p drawFn=%p",
                        tag, i, drawer, drawer->getName(), actor, model, inactiveSlot,
                        seeded ? 1 : 0,
                        reinterpret_cast<void*>(getDrawerAddFunction(drawer)),
                        reinterpret_cast<void*>(getDrawerDrawFunction(drawer)));

                    if (hasInactiveSlot)
                        drawer->addModel(model);

                    if (isModelActive(drawer, model)) {
                        ++activeDrawers;
                        summary->repairQueued = true;
                        MpDebugLog::write(
                            "[Activation] %s: directly repaired drawer[%d]=%p name=%s actor=%p model=%p active=%d/%d",
                            tag, i, drawer, drawer->getName(), actor, model,
                            drawer->mModelNum, drawer->mModelMaxNum);
                        continue;
                    }

                    if (!hasInactiveSlot) {
                        MpDebugLog::write(
                            "[Activation] %s: drawer[%d] inactive tail full; slot[%d]=%p registered=%d active=%d",
                            tag, i, drawer->mModelNum,
                            drawer->mModelNum < drawer->mModelMaxNum
                                ? drawer->mModels[drawer->mModelNum]
                                : nullptr,
                            drawer->mModelMaxNum, drawer->mModelNum);
                    }
                }

                ++summary->missingDraw;
                if (!canRepair) {
                    drawTablesSafe = false;
                    ++summary->unsafeDraw;
                }

                MpDebugLog::write(
                    "[Activation] %s: actor=%p depth=%d missing drawer[%d]=%p name=%s registered=%d active=%d table=%p model=%p",
                    tag, actor, depth, i, drawer, drawer ? drawer->getName() : "(null)",
                    drawer ? drawer->mModelMaxNum : -1, drawer ? drawer->mModelNum : -1,
                    drawer ? drawer->mModels : nullptr, model);
            }
        }

        const bool movementMissing = updaterCount > 0 && activeUpdaters != updaterCount;
        const bool drawMissing = expectsDraw && (drawerCount == 0 || activeDrawers != drawerCount);

        if (depth == 0 || movementMissing || drawMissing) {
            float px = 0.0f;
            float py = 0.0f;
            float pz = 0.0f;
            float sx = 0.0f;
            float sy = 0.0f;
            float sz = 0.0f;
            if (actor->getPoseKeeper()) {
                const sead::Vector3f& pos = al::getTrans(actor);
                const sead::Vector3f& scale = al::getScale(actor);
                px = pos.x;
                py = pos.y;
                pz = pos.z;
                sx = scale.x;
                sy = scale.y;
                sz = scale.z;
            }

            MpDebugLog::write(
                "[Activation] %s: actor=%p depth=%d name=%s pos=(%.2f,%.2f,%.2f) scale=(%.3f,%.3f,%.3f) hidden=%d clipped=%d invalidClip=%d model=%p update=%d/%d draw=%d/%d",
                tag, actor, depth, actor->getName() ? actor->getName() : "(null)",
                px, py, pz, sx, sy, sz,
                hidden ? 1 : 0, clipped ? 1 : 0,
                flags && flags->isClippingInvalid ? 1 : 0, model, activeUpdaters,
                updaterCount, activeDrawers, expectsDraw ? drawerCount : 0);
        }

        // Use the same deferred request mechanism as LiveActor::makeActorAlive
        // and al::showModel.  Both target add routines deduplicate, so a model
        // present in some but not all paths can safely re-request all of them.
        if (movementMissing && movementTablesSafe && executeInfo
            && executeInfo->mRequestKeeper) {
            if (queueExecuteRequest(executeInfo->mRequestKeeper, actor,
                    al::ExecuteRequestKeeper::Request_Movement,
                    al::ExecuteRequestKeeper::Request_RemoveFromMovement)) {
                summary->repairQueued = true;
            } else {
                ++summary->unsafeMovement;
                MpDebugLog::write(
                    "[Activation] %s: could not queue bounded movement repair actor=%p",
                    tag, actor);
            }
        }
        (void)drawTablesSafe;

        if (!actor->getSubActorKeeper())
            return;

        const s32 subNum = al::getSubActorNum(actor);
        for (s32 i = 0; i < subNum; ++i)
            verifyActivationRecursive(al::getSubActor(actor, i), tag, depth + 1, summary,
                allowDrawRepair);
    }

    al::DepthShadowMapDirector* tryGetDepthShadowMapDirector(const al::ActorInitInfo& initInfo) {
        al::GraphicsSystemInfo* graphics = initInfo.actorSceneInfo.graphicsSystemInfo;
        if (!graphics)
            return nullptr;

        al::ShadowDirector* shadow = graphics->getShadowDirector();
        if (!shadow)
            return nullptr;

        return shadow->getDepthShadowMapDirector();
    }

} // namespace

namespace LateModelDrawer {

    void installHooks() {
        modelDrawerForwardDrawHook.installAtMainOffset(cModelDrawerForwardDrawOffset);
        depthShadowRegisterHook.installAtMainOffset(cDepthShadowRegisterOffset);
        depthShadowUpdateViewHook.installAtMainOffset(cDepthShadowUpdateViewOffset);
        depthShadowUpdateHook.installAtMainOffset(cDepthShadowUpdateOffset);
    }

    ScopedLateBuild::ScopedLateBuild(bool active)
        : mActive(active) {
        if (mActive)
            sLateBuildDepth++;
    }

    ScopedLateBuild::~ScopedLateBuild() {
        if (mActive && sLateBuildDepth > 0)
            sLateBuildDepth--;
    }

    bool isLateBuildActive() {
        return sLateBuildDepth > 0;
    }

    ScopedPuppetCostumeBuild::ScopedPuppetCostumeBuild() {
        sPuppetCostumeBuildDepth++;
    }

    ScopedPuppetCostumeBuild::~ScopedPuppetCostumeBuild() {
        if (sPuppetCostumeBuildDepth > 0)
            sPuppetCostumeBuildDepth--;
    }

    bool isPuppetCostumeBuildActive() {
        return sPuppetCostumeBuildDepth > 0;
    }

    void prepareForLateRegistration(const al::ActorInitInfo& initInfo, const char* tag) {
        if (!initInfo.executeDirector)
            return;

        sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());

        for (s32 i = 0; i < initInfo.executeDirector->mUpdateTableCount; i++)
            prepareUpdateTable(initInfo.executeDirector->mUpdateTables[i], tag);

        for (s32 i = 0; i < initInfo.executeDirector->mDrawTableCount; i++)
            prepareDrawTable(initInfo.executeDirector->mDrawTables[i], tag);
    }

    void resetTrackedBuffers() {
        for (s32 i = 0; i < cTrackedBufferCount; ++i) {
            sActorBuffers[i] = {};
            sModelBuffers[i] = {};
        }
    }

    void createMissingTables(const al::ActorInitInfo& initInfo, al::LiveActor* actor,
        const char* tag) {
        // createTable() allocates the table itself; it has to land on the
        // scene heap alongside the model it belongs to, not on whatever heap
        // happened to be current when the puppet noticed a costume change.
        sead::ScopedCurrentHeapSetter setter(al::getSceneHeap());
        fixupActor(initInfo, actor, tag, 0);
    }

    void logExecutorState(al::LiveActor* actor, const char* tag) {
        if (!actor)
            return;

        al::ActorExecuteInfo* executeInfo = actor->getExecuteInfo();
        if (!executeInfo)
            return;

        for (s32 i = 0; i < executeInfo->mUpdaterCount; i++) {
            al::ExecutorActorExecuteBase* updater = executeInfo->mUpdaters[i];
            if (!updater)
                continue;

            MpDebugLog::write("[LateDrawer] %s: WAKE updater[%d]=%p (%s) num=%d cap=%d buf=%p", tag, i,
                updater, updater->getName(), updater->mActors.mPtrNum, updater->mActors.mPtrNumMax,
                updater->mActors.mPtrs);

            if (!updater->mActors.mPtrs)
                continue;

            for (s32 slot = 0; slot < updater->mActors.mPtrNumMax; slot++)
                MpDebugLog::write("[LateDrawer] %s:   slot[%d]=%p%s", tag, slot,
                    updater->mActors.mPtrs[slot],
                    updater->mActors.mPtrs[slot] == actor ? " <- this actor" : "");
        }
    }

    bool isDrivenByMovementExecutor(al::LiveActor* actor) {
        if (!actor)
            return false;

        al::ActorExecuteInfo* executeInfo = actor->getExecuteInfo();
        if (!executeInfo)
            return false;

        for (s32 i = 0; i < executeInfo->mUpdaterCount; ++i) {
            if (isActorActive(executeInfo->mUpdaters[i], actor))
                return true;
        }

        return false;
    }

    bool verifyActivationAndRequestRepair(al::LiveActor* actor, const char* tag,
        bool allowDrawRepair) {
        if (!actor)
            return false;

        ActivationSummary summary;
        verifyActivationRecursive(
            actor, tag ? tag : "(unnamed)", 0, &summary, allowDrawRepair);

        const bool ready = summary.aliveActors > 0 && summary.missingMovement == 0
            && summary.missingDraw == 0 && summary.unsafeMovement == 0
            && summary.unsafeDraw == 0;

        MpDebugLog::write(
            "[Activation] %s: ready=%d aliveActors=%d missingUpdate=%d missingDraw=%d unsafeUpdate=%d unsafeDraw=%d repairQueued=%d",
            tag ? tag : "(unnamed)", ready ? 1 : 0, summary.aliveActors,
            summary.missingMovement, summary.missingDraw, summary.unsafeMovement,
            summary.unsafeDraw, summary.repairQueued ? 1 : 0);
        return ready;
    }

    DepthShadowCounts snapshotDepthShadow(const al::ActorInitInfo& initInfo) {
        DepthShadowCounts counts;

        al::DepthShadowMapDirector* director = tryGetDepthShadowMapDirector(initInfo);
        if (!director)
            return counts;

        counts.declared = director->_68;
        counts.valid = true;
        return counts;
    }

    void restoreDepthShadow(const al::ActorInitInfo& initInfo, const DepthShadowCounts& saved,
        const char* tag) {
        if (!saved.valid)
            return;

        al::DepthShadowMapDirector* director = tryGetDepthShadowMapDirector(initInfo);
        if (!director)
            return;

        MpDebugLog::write("[LateDrawer] %s: depth shadow director=%p declared %d -> %d",
            tag, director, saved.declared, director->_68);

        director->_68 = saved.declared;
    }

} // namespace LateModelDrawer
