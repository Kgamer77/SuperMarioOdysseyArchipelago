#pragma once

#include "hk/diag/diag.h"

#include "al/Library/Memory/HeapUtil.h"
#include "al/Library/Thread/AsyncFunctorThread.h"
#include "sead/heap/seadHeap.h"
#include <basis/seadTypes.h>
#include <set>
#include <vector>

#include "SD/SDHooks.h"

namespace ArchipelagoSocket {
    inline sead::Heap* sArchipelagoSocketHeap = nullptr;
    // Change this size depedning on observations. Currently based on
    // space for 200 4kb packets which is the max Archipelago can seemingly handle
    // with an additional 64kb for misc data.
    constexpr size_t kArchipelagoSocketHeapSize = 0xD8000; // 864KB

    // See the header: off unless a card opts in.
    inline bool sNonBlockingRecv = false;

    // Creates own small dedicated heap, as a child of al::getStationedHeap(). Must be called exactly
    // once, early, from a context that's actually guaranteed to have a valid
    // heap - ridden along on Multiplayer's existing al::SystemKit::
    // createMemorySystem hook in Multiplayer/hooks/initHooks.cpp rather than
    // installing a second independent hook on that same symbol.
    // Safe to call again (no-op) if it already succeeded.
    void initHeap();

    void init();

    // Whether receives are issued non-blocking (server.ini: nonblocking_recv).
    //
    // DEFAULT OFF, and that default is deliberate. Real hardware does not need
    // it: concurrent send-while-recv-is-parked works there, which is why
    // multiplayer has always worked on a Switch. Ryujinx is where a blocking
    // recv also blocks a concurrent socket::Send on the same fd, deadlocking
    // every outgoing packet the moment the recv thread starts.
    //
    // Keeping it off by default means a hardware SD card gets byte-for-byte the
    // socket behaviour that already works, and only a card that explicitly asks
    // for it (an emulator's) takes the different path. server.ini lives per-SD
    // card, so the same binary can be correct on both.
    bool useNonBlockingRecv();

    // implement a version of MpDebugLog here for use in socket and other places for ap specific debug logging

} // namespace ArchipelagoSocket

template <typename T>
class DataFrameAllocator {
public:
    using value_type = T;

    DataFrameAllocator() noexcept = default;

    template <typename U>
    DataFrameAllocator(const DataFrameAllocator<U>& other) noexcept { }

    T* allocate(std::size_t n) {
        if (n == 0)
            return nullptr;
        void* ptr = ArchipelagoSocket::sArchipelagoSocketHeap->alloc(n * sizeof(T));
        if (!ptr)
            HK_ABORT("Crashed while allocating for a vector!");
        return static_cast<T*>(ptr);
    }

    void deallocate(T* p, std::size_t n) noexcept {
        if (p)
            ArchipelagoSocket::sArchipelagoSocketHeap->free(p);
    }

    bool operator==(const DataFrameAllocator& other) const noexcept = default;
};

using DataFrameVector = std::vector<u8, DataFrameAllocator<u8>>;
template <typename T>
using vector = std::vector<T, DataFrameAllocator<T>>;
template <typename T>
using set = std::set<T, std::less<T>, DataFrameAllocator<T>>;