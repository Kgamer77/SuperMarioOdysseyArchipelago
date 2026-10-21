#pragma once

#include "hk/diag/diag.h"

#include <experimental/memory>

#include "Archipelago/ArchipelagoSocket.h"
#include "Archipelago/WebSocketDataFrame.h"

namespace DataFrameFactory {
    inline WebSocketDataFrame* create(DataFrameVector header) {
        return new (ArchipelagoSocket::sArchipelagoSocketHeap) WebSocketDataFrame(header);
    }
} // namespace DataFrameFactory