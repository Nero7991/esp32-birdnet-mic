// Adapted from Dan Logue's PDM v15.6 contribution (October 2026).
#pragma once
// Integer-only per-sample audio stage: I2S shift -> high-pass biquad -> gain
// -> clamp to int16, with peak/clip metering. Replaces the float version,
// which the ESP32-C3/C5/C6 (no FPU) had to emulate in software for every
// sample. Used for every microphone type on every board.
//
// Plain C++ with no Arduino dependencies; tests/fixed_audio_test.cpp checks
// it against the previous float implementation on the host.

#include <stdint.h>
#include <stddef.h>
#include <math.h>

// Optional placement for processBlock(); the firmware defines it as
// IRAM_ATTR + noinline. Empty for host builds.
#ifndef FXAUDIO_HOT
#define FXAUDIO_HOT
#endif

namespace fxaudio {

static const int kCoefShift = 28;   // biquad coefficients in Q28 (range +/-8)
static const int kGainShift = 20;   // gain in Q20 (0.1 .. 100 -> 104858 .. 104857600)

// 2nd-order Butterworth high-pass with the gain folded in, integer-only.
//
//   y = g*b0*(x - 2x1 + x2) - a1*y1 - a2*y2        (RBJ cookbook, Q = 0.7071)
//
// Fixed-point measures (each found necessary by tests/fixed_audio_test.cpp):
// - The numerator is the exact second difference d = x - 2x1 + x2: any mic
//   DC offset cancels before the multiply and the zero at DC is exact.
// - Gain is folded into the numerator coefficient once per block, so the
//   numerator enters the accumulator at full precision (truncating it would
//   leave a bias that the filter's large low-frequency gain amplifies).
// - The feedback coefficients are stored as a1 + 2 and a2 - 1 in Q29 (range
//   +/-4: a1 + 2 reaches ~3.6 at the 0.45*fs maximum cutoff). Near
//   DC the filter depends on 1 + a1 + a2 (about 1.7e-6 at a 10 Hz cutoff),
//   which these small offsets represent precisely.
// - The output state is kept in output units with kFrac fractional bits, and
//   its rounding uses second-order error feedback, shaping it by (1 - z^-1)^2
//   so it is not amplified near DC.
// - The input history is primed with the first sample, so start-up does not
//   see a large DC step.
static const int kFbShift = 29;                     // feedback coefficients, Q29
static const int kFrac = 10;                        // fractional bits of y
static const int kAccShift = kFbShift;              // accumulator = y in Q(kFrac + 29)
static const int64_t kYLimit = (int64_t)1 << 28;    // |y| <= 2^18 output LSB (far past clipping)

struct BiquadQ28 {
    int32_t b0;          // numerator coefficient, Q28 (b1 = -2 b0, b2 = b0)
    int32_t a1p, a2p;    // a1 + 2 and a2 - 1, Q29
    int32_t x1, x2;      // input history (shifted input units)
    int32_t y1, y2;      // output history, Q(kFrac) output units
    int64_t e1, e2;      // rounding remainders for error feedback
    bool primed;
};

inline void biquadReset(BiquadQ28* f) {
    f->x1 = f->x2 = 0;
    f->y1 = f->y2 = 0;
    f->e1 = f->e2 = 0;
    f->primed = false;
}

// Round half away from zero (same as llround) without pulling in libm.
static inline int64_t roundToInt(double v) {
    return (int64_t)(v < 0.0 ? v - 0.5 : v + 0.5);
}

// sin and cos of x for 0 <= x <= pi/2, by Taylor series in double: accurate to
// ~1e-16 relative over that range. Used instead of libm sin()/cos(), whose
// double-precision argument reduction adds ~6 KB of flash on the C6.
static inline void sinCosHalfQuadrant(double x, double* s, double* c) {
    double x2 = x * x;
    double ts = x, tc = 1.0, sumS = x, sumC = 1.0;
    for (int k = 1; k <= 12; k++) {
        ts *= -x2 / (double)((2 * k) * (2 * k + 1));
        tc *= -x2 / (double)((2 * k - 1) * (2 * k));
        sumS += ts;
        sumC += tc;
    }
    *s = sumS;
    *c = sumC;
}

// Computed in double (only when settings change). Resets the filter state.
// Written in terms of the half angle, s = sin(w0/2), so 1 - cos(w0) = 2 s^2 is
// exact even at low cutoffs (no cancellation), using RBJ with Q = 1/sqrt(2):
//   alpha = sin(w0)/(2Q) = sqrt(2) s ch,   a0 = 1 + alpha,
//   b0 = (1 + cos w0)/2/a0 = ch^2/a0,
//   a1 + 2 = 2(a0 - cos w0)/a0 = 2(alpha + 2 s^2)/a0,   a2 - 1 = -2 alpha/a0.
inline void biquadSetHighpass(BiquadQ28* f, double sampleRate, double cutoffHz) {
    const double pi = 3.14159265358979323846;
    double h = pi * cutoffHz / sampleRate;    // w0 / 2
    if (h < 0.0) h = 0.0;
    if (h > pi / 2) h = pi / 2;
    double s, ch;
    sinCosHalfQuadrant(h, &s, &ch);
    double alpha = 1.41421356237309505 * s * ch;
    double a0 = 1.0 + alpha;
    double b0 = ch * ch / a0;
    double a1p = 2.0 * (alpha + 2.0 * s * s) / a0;
    double a2p = -2.0 * alpha / a0;
    f->b0 = (int32_t)roundToInt(b0 * (double)(1L << kCoefShift));
    f->a1p = (int32_t)roundToInt(a1p * (double)(1L << kFbShift));
    f->a2p = (int32_t)roundToInt(a2p * (double)(1L << kFbShift));
    biquadReset(f);
}

inline int32_t gainToQ(float gain) {
    return (int32_t)roundToInt((double)gain * (double)(1L << kGainShift));
}

static inline int64_t clamp64(int64_t v, int64_t lim) {
    return v > lim ? lim : (v < -lim ? -lim : v);
}

// Clamp to int16 and update peak/clip. a is in output LSB.
static inline int16_t finish(int64_t a, uint32_t* peak, bool* clip) {
    uint64_t mag = a < 0 ? (uint64_t)(-a) : (uint64_t)a;
    if (mag > *peak) *peak = mag > 0xFFFFFFFFu ? 0xFFFFFFFFu : (uint32_t)mag;
    if (a > 32767) { *clip = true; return 32767; }
    if (a <= -32768) { *clip = true; return -32768; }   // |a| > 32767
    return (int16_t)a;
}

// Process n samples. hpf may be null (high-pass off). Writes int16 output
// and returns the block peak |sample| before clamping (capped at 32767);
// *clipped is set if any sample exceeded the int16 range.
//
// Bounds: Bq * d <= 2^60 (d clamped to dLim); |a1p*y1| <= 2^59 (a1p < 2^31,
// |y| <= 2^28); |a2p*y2|, |2*y1*2^29| and |y2*2^29| are each <= 2^58, so
// the accumulator stays below 2^61.
static FXAUDIO_HOT uint16_t processBlock(const int32_t* in, int16_t* out, int n,
                                         uint8_t shift, BiquadQ28* hpf,
                                         int32_t gainQ, bool* clipped) {
    uint32_t peak = 0;
    bool clip = false;
    if (hpf && n > 0) {
        // Numerator coefficient g*b0 in Q(kFrac + 29): Q28 * Q20 = Q48 -> Q39.
        int64_t bq = ((int64_t)hpf->b0 * gainQ) >> (kCoefShift + kGainShift - kFrac - kAccShift);
        if (bq < 1) bq = 1;
        int64_t dLim = (((int64_t)1 << 60) / bq);
        if (dLim > INT32_MAX) dLim = INT32_MAX;

        const int32_t a1p = hpf->a1p, a2p = hpf->a2p;
        int32_t x1 = hpf->x1, x2 = hpf->x2, y1 = hpf->y1, y2 = hpf->y2;
        int64_t e1 = hpf->e1, e2 = hpf->e2;
        if (!hpf->primed) {
            x1 = x2 = in[0] >> shift;
            hpf->primed = true;
        }
        const int64_t one = (int64_t)1 << kAccShift;
        const int64_t half = (int64_t)1 << (kFrac - 1);
        for (int i = 0; i < n; i++) {
            int32_t x = in[i] >> shift;
            int64_t d = clamp64((int64_t)x - 2 * (int64_t)x1 + (int64_t)x2, dLim);
            int64_t acc = bq * d
                        - (int64_t)a1p * y1 - (int64_t)a2p * y2
                        + (2 * (int64_t)y1 - (int64_t)y2) * one
                        + 2 * e1 - e2;
            int64_t y = acc >> kAccShift;
            int64_t e0 = acc - y * one;                       // 0 .. 2^29-1
            if (y > kYLimit || y < -kYLimit) { y = clamp64(y, kYLimit); e0 = 0; }
            x2 = x1; x1 = x;
            y2 = y1; y1 = (int32_t)y;
            e2 = e1; e1 = e0;
            out[i] = finish((y + half) >> kFrac, &peak, &clip);
        }
        hpf->x1 = x1; hpf->x2 = x2; hpf->y1 = y1; hpf->y2 = y2;
        hpf->e1 = e1; hpf->e2 = e2;
    } else if (!hpf) {
        const int64_t round = (int64_t)1 << (kGainShift - 1);
        for (int i = 0; i < n; i++) {
            int32_t x = in[i] >> shift;
            out[i] = finish(((int64_t)x * gainQ + round) >> kGainShift, &peak, &clip);
        }
    }
    *clipped = clip;
    return (uint16_t)(peak > 32767u ? 32767u : peak);
}

}  // namespace fxaudio
