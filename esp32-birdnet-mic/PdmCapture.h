#pragma once

#include "PdmFilter.h"

// Raw DMA framing is independent of packet size. Retain byte/pair phase
// across short reads; never silently discard the tail of a duplicated pair.
namespace pdmsw {
// Unlike word equality, arrival rate also works with IM73D122 data where
// the two slots need not match. Skip startup DMA backlog and retry any
// window that lost data; an overrun must not look like a plain stream.
struct WordRateDetector {
    bool started = false, measuring = false;
    uint64_t startUs = 0, windowUs = 0, words = 0;
    uint32_t overrunTotal = 0;
    uint8_t stride = 0;

    void reset() { *this = WordRateDetector{}; }

    uint8_t observe(size_t count, uint64_t nowUs, uint32_t expectedRate, uint32_t overruns) {
        if (stride) return stride;
        if (!started || overruns != overrunTotal) {
            started = true;
            measuring = false;
            startUs = nowUs;
            words = 0;
            overrunTotal = overruns;
            return 0;
        }
        if (!measuring) {
            if (nowUs - startUs >= 100000) {
                measuring = true;
                windowUs = nowUs;
                words = 0;
            }
            return 0;
        }
        words += count;
        uint64_t elapsed = nowUs - windowUs;
        if (elapsed >= 400000) {
            stride = uint8_t(strideFromRate(words, elapsed, expectedRate));
            windowUs = nowUs;
            words = 0;
        }
        return stride;
    }
};

struct WordFramer {
    uint8_t stride = 2; // current C3/C5/C6 drivers; rate measurement confirms
    bool hasByte = false;
    uint8_t byte = 0;
    bool secondOfPair = false;

    void reset() { *this = WordFramer{}; }

    size_t unpack(uint8_t* bytes, size_t count, uint16_t* out) {
        size_t written = 0;
        for (size_t i = 0; i < count; ++i) {
            if (!hasByte) { byte = bytes[i]; hasByte = true; continue; }
            uint16_t word = uint16_t(byte) | (uint16_t(bytes[i]) << 8);
            hasByte = false;
            out[written++] = word;
        }
        return written;
    }

    size_t compact(uint16_t* words, size_t count) {
        size_t written = 0;
        for (size_t i = 0; i < count; ++i) {
            if (stride != 2 || !secondOfPair) words[written++] = words[i];
            secondOfPair = !secondOfPair;
        }
        return written;
    }
};
} // namespace pdmsw
