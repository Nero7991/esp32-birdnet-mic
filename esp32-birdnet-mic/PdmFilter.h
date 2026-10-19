// Adapted from Dan Logue's PDM v15 contribution (October 2026).
#pragma once
// Software PDM-to-PCM decimator for chips whose I2S peripheral can capture raw
// PDM but has no hardware PDM-to-PCM converter (ESP32-C3/C5/C6).
//
// Input : raw 1-bit PDM, packed 16 bits per uint16_t word as the I2S DMA
//         delivers it, at 64 x the output sample rate (3.072 MHz for 48 kHz).
// Output: int32 PCM, left-aligned like an I2S mic sample, so that PDM full
//         scale (100% ones density) maps to int16 full scale << 16. The
//         firmware's I2S Shift / Gain then behave the same as for I2S mics.
//
// Integer-only (these chips have no FPU). Plain C++ with no Arduino
// dependencies, so tests/pdm_filter_test.cpp builds it on the host.
//
// Stage 1: 4th-order sinc (CIC-equivalent), decimate by 16 -> 192 kHz.
//          A 64-bit window (4 raw words) weighted by byte lookup tables
//          (4 KB, generated at init). Each new word is looked up once per
//          window position and folded into running partial sums.
// Stage 2: 44-tap symmetric equiripple FIR, Q14, decimate by 4 -> 48 kHz,
//          evaluated as 22 multiplies on paired taps. Passband 0-16 kHz
//          (+0.0/-0.4 dB incl. stage-1 droop); stopband >= 32 kHz at about
//          -70 dB, so 0-16 kHz is alias-free.
// Stage 3: one-pole DC blocker (about 7.5 Hz at 48 kHz).
//
// All corner frequencies scale with the sample rate (fixed 64x decimation).
// Arithmetic is exact integer; output matches a double-precision reference
// decoder to within 0.1 dB SINAD (see tests/pdm_filter_test.cpp).
//
// Cost: 344 instructions per output sample (about 16.5 M instructions/s at
// 48 kHz), measured by running the C6 -Os build in a RISC-V emulator;
// roughly 10-15% of the 160 MHz core. RAM: 4.4 KB for this struct plus the
// caller's raw read buffer. Not yet measured on hardware.

#include <stdint.h>
#include <stddef.h>
#include <string.h>

// The firmware builds with -Os, which would otherwise leave these per-word
// helpers as calls (about 2x the per-word cost).
#if defined(__GNUC__)
#define PDMSW_INLINE inline __attribute__((always_inline))
#else
#define PDMSW_INLINE inline
#endif

// Optional placement for process(), the per-sample hot path. The firmware
// defines it as IRAM_ATTR + noinline so the filter runs from internal RAM
// without flash-cache misses; it is empty for host builds and tests.
#ifndef PDMSW_HOT
#define PDMSW_HOT
#endif

namespace pdmsw {

static const int kDecimation = 64;       // PDM bits per output sample
static const int kWordsPerSample = 4;    // 64 bits / 16 bits per word
static const int kFirTaps = 44;
static const int kFirHalf = kFirTaps / 2;
static const int kDcShift = 10;          // DC blocker pole: 1 - 2^-10

// First half of the symmetric 44-tap low-pass (h[i] == h[43 - i]); design:
// equiripple at a 192 kHz input rate, pass 16 kHz, stop 32 kHz, Q14.
// Full-filter sum 16328 (DC gain 0.9966); sum|h| 24892, so with stage-1
// outputs bounded by +/-65536 the accumulator stays below 1.64e9 < 2^31.
static const int16_t kFirQ14Half[kFirHalf] = {
         5,     13,     22,     22,      3,    -37,    -85,   -106,    -68,     40,    181,
       273,    224,     -5,   -353,   -643,   -649,   -195,    733,   1942,   3079,   3768,
};

struct Decimator {
    // lut[2j][hi byte] + lut[2j+1][lo byte] = contribution of a raw word in
    // window slot j (0 = oldest). The tables already account for bit order.
    int16_t lut[8][256];              // 4 KB
    int32_t p1, p2, p3;               // stage-1 partial sums for the next outputs
    int32_t hist[2 * kFirTaps];       // stage-2 delay line, doubled to avoid wrap
    int histPos;
    int phase;                        // stage-1 outputs since last stage-2 output
    int32_t dcX1;                     // DC blocker state
    int64_t dcY1;
    bool lsbFirst;
};

// Contribution of word w in window slot j.
static PDMSW_INLINE int32_t slot(const Decimator* d, int j, uint16_t w) {
    return (int32_t)d->lut[2 * j][w >> 8] + (int32_t)d->lut[2 * j + 1][w & 0xFF];
}

// One stage-1 (192 kHz) output for the newest word:
//   out = L0(w[n-3]) + L1(w[n-2]) + L2(w[n-1]) + L3(w[n])
static PDMSW_INLINE int32_t stage1(Decimator* d, uint16_t w) {
    int32_t out = d->p3 + slot(d, 3, w);
    d->p3 = d->p2 + slot(d, 2, w);
    d->p2 = d->p1 + slot(d, 1, w);
    d->p1 = slot(d, 0, w);
    return out;
}

// Build tables and clear state. lsbFirst=false means bit 15 of each word was
// received first (the ESP32 I2S receive default); true means bit 0 first.
inline void init(Decimator* d, bool lsbFirst) {
    // Stage-1 kernel: boxcar(16) convolved with itself 4 times -> 61 taps,
    // integer, sum 16^4 = 65536. Placed at window positions 1..61 of 64.
    int32_t k[64];
    int32_t a[64], b[64];
    memset(a, 0, sizeof(a));
    for (int i = 0; i < 16; i++) a[i] = 1;
    int len = 16;
    for (int pass = 1; pass < 4; pass++) {
        memset(b, 0, sizeof(b));
        for (int i = 0; i < len; i++) {
            for (int j = 0; j < 16; j++) b[i + j] += a[i];
        }
        len += 15;
        memcpy(a, b, sizeof(a));
    }
    memset(k, 0, sizeof(k));
    for (int i = 0; i < len && i + 1 < 64; i++) k[i + 1] = a[i];

    // Time offset (0..15 within the word) of byte bit b:
    //   MSB-first: hi byte bit b -> 7-b,  lo byte bit b -> 15-b
    //   LSB-first: hi byte bit b -> 8+b,  lo byte bit b -> b
    // Bit 1 counts +1, bit 0 counts -1.
    for (int j = 0; j < kWordsPerSample; j++) {
        for (int v = 0; v < 256; v++) {
            int32_t hi = 0, lo = 0;
            for (int bit = 0; bit < 8; bit++) {
                int tHi = lsbFirst ? 8 + bit : 7 - bit;
                int tLo = lsbFirst ? bit : 15 - bit;
                int32_t kh = k[16 * j + tHi], kl = k[16 * j + tLo];
                bool set = ((v >> bit) & 1) != 0;
                hi += set ? kh : -kh;
                lo += set ? kl : -kl;
            }
            d->lut[2 * j][v] = (int16_t)hi;       // |x| <= 21848 for this kernel
            d->lut[2 * j + 1][v] = (int16_t)lo;
        }
    }
    // Start as if the window held an idle line (alternating bits), which
    // keeps the startup transient small.
    const uint16_t idle = 0xAAAA;
    d->p1 = slot(d, 0, idle);
    d->p2 = d->p1 + slot(d, 1, idle);
    d->p3 = d->p2 + slot(d, 2, idle);
    memset(d->hist, 0, sizeof(d->hist));
    d->histPos = 0;
    d->phase = 0;
    d->dcX1 = 0;
    d->dcY1 = 0;
    d->lsbFirst = lsbFirst;
}

// Consume nWords raw words, write at most maxOut PCM samples to out.
// Returns the number of samples written. Feeding 4*N words yields exactly N
// samples (phase stays aligned); any partial read is carried in the state.
static PDMSW_HOT size_t process(Decimator* d, const uint16_t* raw, size_t nWords,
                                int32_t* out, size_t maxOut) {
    size_t produced = 0;
    // Work on locals (kept in registers) and write the state back once.
    const int16_t (*lut)[256] = d->lut;
    int32_t p1 = d->p1, p2 = d->p2, p3 = d->p3;
    int32_t* hist = d->hist;
    int histPos = d->histPos;
    int phase = d->phase;
    for (size_t n = 0; n < nWords; n++) {
        // Stage 1 (same as stage1(), expanded so p1..p3 stay in registers).
        uint16_t w = raw[n];
        unsigned hi = w >> 8, lo = w & 0xFF;
        int32_t s1 = p3 + lut[6][hi] + lut[7][lo];
        p3 = p2 + lut[4][hi] + lut[5][lo];
        p2 = p1 + lut[2][hi] + lut[3][lo];
        p1 = lut[0][hi] + lut[1][lo];

        // Stage 2 delay line.
        hist[histPos] = s1;
        hist[histPos + kFirTaps] = s1;
        if (++histPos == kFirTaps) histPos = 0;
        if (++phase < 4) continue;
        phase = 0;
        if (produced >= maxOut) continue;   // caller asked for fewer; drop

        const int32_t* x = &hist[histPos];   // oldest first
        int32_t acc = 0;
#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC unroll 22
#endif
        for (int i = 0; i < kFirHalf; i++) {
            acc += (int32_t)kFirQ14Half[i] * (x[i] + x[kFirTaps - 1 - i]);
        }

        // Stage 3: DC blocker. acc full scale is 2^30.
        int64_t y = (int64_t)acc - d->dcX1 + d->dcY1 - (d->dcY1 >> kDcShift);
        d->dcX1 = acc;
        d->dcY1 = y;

        int64_t o = y * 2;                  // 2^30 -> 2^31 (int16 FS << 16)
        if (o > INT32_MAX) o = INT32_MAX;
        if (o < INT32_MIN) o = INT32_MIN;
        out[produced++] = (int32_t)o;
    }
    d->p1 = p1; d->p2 = p2; d->p3 = p3;
    d->histPos = histPos;
    d->phase = phase;
    return produced;
}

// Raw word stride. On the ESP32-C3/C5/C6 (ESP-IDF 5.5, raw PDM, mono mic) the
// I2S driver currently returns two 16-bit words per 16 PDM clocks; only every
// second word is used. The firmware starts in pair mode and confirms it from
// the measured word rate (strideFromRate), switching to plain if needed.
//
// Stride from the measured word rate. expectedWordsPerSec is the plain rate
// (sample rate * kWordsPerSample). Returns 2 if words arrive at about twice
// that rate, 1 at about the plain rate, 0 if undecided (too short a window,
// or a rate that matches neither).
inline int strideFromRate(uint64_t words, uint64_t elapsedUs, uint32_t expectedWordsPerSec) {
    if (elapsedUs < 200000 || expectedWordsPerSec == 0) return 0;
    // ratio * 100 = words * 1e6 * 100 / (elapsedUs * expected)
    uint64_t ratio100 = (words * 100000000ULL) / (elapsedUs * (uint64_t)expectedWordsPerSec);
    if (ratio100 >= 160 && ratio100 <= 240) return 2;
    if (ratio100 >= 75 && ratio100 <= 125) return 1;
    return 0;
}

// Data-based check (kept for tests and reference; the firmware uses the rate).
// Returns 2 if the block is made of identical pairs, 1 if it is a plain
// stream, 0 if undecided (too short, or no variation to judge by).
inline int detectWordStride(const uint16_t* raw, size_t nWords) {
    if (nWords < 256) return 0;
    size_t pairs = nWords / 2, evenSame = 0, oddSame = 0;
    for (size_t i = 0; i + 1 < nWords; i += 2) if (raw[i] == raw[i + 1]) evenSame++;
    for (size_t i = 1; i + 1 < nWords; i += 2) if (raw[i] == raw[i + 1]) oddSame++;
    if (oddSame * 10 > pairs * 9) return 0;          // constant pattern: cannot tell
    if (evenSame * 10 >= pairs * 9) return 2;        // >= 90% identical pairs
    return 1;
}

// Keep every second word in place (stride 2). Returns the new word count.
inline size_t compactWordPairs(uint16_t* raw, size_t nWords) {
    size_t n = nWords / 2;
    for (size_t i = 0; i < n; i++) raw[i] = raw[2 * i];
    return n;
}

enum BitOrderResult { ORDER_UNDECIDED = 0, ORDER_MSB_FIRST = 1, ORDER_LSB_FIRST = 2 };

// Bit-order test energy for one order: the stage-1 (192 kHz) signal through a
// (1 - z^-2)^3 band filter, which peaks at 48 kHz, has a triple zero at DC and
// at 96 kHz, and attenuates audio below ~16 kHz by 20 dB or more. With the
// right order this band holds almost nothing (no acoustic content, and the
// mic's shaped noise rises only higher up, toward 96 kHz and beyond); with the
// wrong order the scrambled bits put broadband noise there. Uses `scratch`.
inline uint64_t orderTestEnergy(Decimator* scratch, bool lsbFirst, const uint16_t* raw, size_t nWords) {
    init(scratch, lsbFirst);
    int32_t h[7] = {0, 0, 0, 0, 0, 0, 0};
    uint64_t energy = 0;
    for (size_t n = 0; n < nWords; n++) {
        int32_t s1 = stage1(scratch, raw[n]);               // |s1| <= 65536
        for (int k = 0; k < 6; k++) h[k] = h[k + 1];
        h[6] = s1;
        if (n < 12) continue;                               // window and filter history filling
        int32_t b = h[6] - 3 * h[4] + 3 * h[2] - h[0];      // |b| <= 8 * 65536
        int32_t v = b >> 4;
        energy += (uint64_t)((int64_t)v * v);
    }
    return energy;
}

// Second test, robust to ultrasonic content: energy of the first difference of
// the fully decoded 48 kHz output (removes DC, weights hiss). The wrong order
// scrambles the bits and adds broadband hiss in the audio band; loud audio
// near 10-16 kHz can mask it, which the band test above covers. Uses `scratch`.
inline uint64_t decodedHissEnergy(Decimator* scratch, bool lsbFirst, const uint16_t* raw, size_t nWords) {
    init(scratch, lsbFirst);
    const size_t kSkip = 24;   // FIR fill and DC-blocker start-up transient
    int32_t buf[64];
    uint64_t energy = 0;
    int32_t prev = 0;
    size_t index = 0, pos = 0;
    while (pos < nWords) {
        size_t take = nWords - pos;
        if (take > 64 * (size_t)kWordsPerSample) take = 64 * (size_t)kWordsPerSample;
        size_t got = process(scratch, raw + pos, take, buf, 64);
        pos += take;
        for (size_t i = 0; i < got; i++, index++) {
            int32_t v = buf[i] >> 8;                       // |v| < 2^23
            if (index >= kSkip) {
                int64_t dv = (int64_t)v - prev;            // |dv| < 2^24
                energy += (uint64_t)(dv * dv);             // < 2^48 per sample
            }
            prev = v;
        }
    }
    return energy;
}

// Judge the bit order from a block of raw words (one firmware read chunk,
// 2048 words / ~11 ms at 48 kHz, is enough). Combines both tests: the product
// of their wrong/right energy ratios must reach 4x, so each test can carry the
// decision where the other is blind (loud high audio tones; strong ultrasonic
// content) unless the other strongly contradicts it. A block with no noise at
// all (an idle 1010... pattern reads the same both ways) is undecided, and the
// caller tries a later block.
// The live decimator `d` is left untouched unless the decided order differs
// from its current one (then it is re-initialized once), so calling this
// while streaming does not disturb the audio. Not reentrant: one shared
// scratch decimator (~4.4 KB) is used for the trial decodes.
inline BitOrderResult detectBitOrder(Decimator* d, const uint16_t* raw, size_t nWords) {
    static Decimator scratch;
    double msb = (double)(orderTestEnergy(&scratch, false, raw, nWords) + 1) *
                 (double)(decodedHissEnergy(&scratch, false, raw, nWords) + 1);
    double lsb = (double)(orderTestEnergy(&scratch, true, raw, nWords) + 1) *
                 (double)(decodedHissEnergy(&scratch, true, raw, nWords) + 1);
    BitOrderResult r = ORDER_UNDECIDED;
    if (lsb > 4.0 * msb) r = ORDER_MSB_FIRST;
    else if (msb > 4.0 * lsb) r = ORDER_LSB_FIRST;
    if (r != ORDER_UNDECIDED && d->lsbFirst != (r == ORDER_LSB_FIRST)) init(d, r == ORDER_LSB_FIRST);
    return r;
}

}  // namespace pdmsw
