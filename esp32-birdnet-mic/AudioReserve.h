#pragma once
#include <stddef.h>

// Preserve RAM for Wi-Fi/TCP after the ring and producer task are allocated.
// Based on Dan Logue's v15.6 hardware findings on XIAO C3/C5.
namespace audioreserve {
static constexpr size_t kHeapMargin = 80 * 1024;
static constexpr size_t kProducerStackBytes = 6144;
static constexpr size_t kAllocationOverhead = 1024;
// C5 may restart capture while a connected client still owns network buffers.
// Only the minimum audio queue may use this lower, bounded Wi-Fi allowance.
static constexpr size_t kC5MinimumHeapMargin = 64 * 1024;

inline size_t minimumCapacity(size_t requested, size_t chunkBytes) {
    size_t minimum = chunkBytes * 4;
    if (minimum < 16384) minimum = 16384;
    if (minimum > requested) minimum = requested;
    return minimum;
}

inline size_t fit(size_t requested, size_t chunkBytes, size_t freeHeap, size_t largestBlock,
                  size_t heapMargin = kHeapMargin) {
    const size_t minimum = minimumCapacity(requested, chunkBytes);
    for (;;) {
        if (freeHeap >= requested + heapMargin + kProducerStackBytes + kAllocationOverhead &&
            largestBlock >= requested + kAllocationOverhead) return requested;
        if (requested == minimum) return 0;
        requested -= requested - minimum > 4096 ? 4096 : requested - minimum;
    }
}
} // namespace audioreserve
