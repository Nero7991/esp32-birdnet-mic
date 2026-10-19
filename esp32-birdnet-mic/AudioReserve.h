#pragma once
#include <stddef.h>

// Preserve RAM for Wi-Fi/TCP after the ring and producer task are allocated.
// Based on Dan Logue's v15.6 hardware findings on XIAO C3/C5.
namespace audioreserve {
static constexpr size_t kHeapMargin = 80 * 1024;
static constexpr size_t kProducerStackBytes = 6144;
static constexpr size_t kAllocationOverhead = 1024;

inline size_t fit(size_t requested, size_t chunkBytes, size_t freeHeap, size_t largestBlock) {
    size_t minimum = chunkBytes * 4;
    if (minimum < 16384) minimum = 16384;
    if (minimum > requested) minimum = requested;
    for (;;) {
        if (freeHeap >= requested + kHeapMargin + kProducerStackBytes + kAllocationOverhead &&
            largestBlock >= requested + kAllocationOverhead) return requested;
        if (requested == minimum) return 0;
        requested -= requested - minimum > 4096 ? 4096 : requested - minimum;
    }
}
} // namespace audioreserve
