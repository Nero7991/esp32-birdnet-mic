// Host test for FixedAudio.h (integer shift -> high-pass -> gain -> int16).
// Build: g++ -std=c++11 -O2 -Wall -Wextra -Werror -Iesp32-birdnet-mic tests/fixed_audio_test.cpp
//
// Compares the integer stage and the previous float implementation against a
// double-precision reference of the same filter. The integer stage must be
// at least as accurate as the float code it replaces.

#include "FixedAudio.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int failures = 0;
static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

struct Coefs { double b0, b1, b2, a1, a2; };

// Same design as updateHighpassCoeffs() in the firmware (RBJ, Q = 0.7071).
static Coefs designHpf(double fs, double fc) {
    double w0 = 2.0 * M_PI * fc / fs, c = std::cos(w0), al = std::sin(w0) / (2.0 * 0.70710678);
    double a0 = 1.0 + al;
    return {(1.0 + c) * 0.5 / a0, -(1.0 + c) / a0, (1.0 + c) * 0.5 / a0, -2.0 * c / a0, (1.0 - al) / a0};
}

// The firmware's previous float stage, verbatim in behavior.
struct FloatBiquad {
    float b0, b1, b2, a1, a2, x1, x2, y1, y2;
    explicit FloatBiquad(const Coefs& c)
        : b0((float)c.b0), b1((float)c.b1), b2((float)c.b2), a1((float)c.a1), a2((float)c.a2),
          x1(0), x2(0), y1(0), y2(0) {}
    float process(float x) {
        float y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = x; y2 = y1; y1 = y;
        return y;
    }
};
static int16_t floatStage(int32_t raw, int shift, FloatBiquad* f, float gain) {
    float s = (float)(raw >> shift);
    if (f) s = f->process(s);
    float a = s * gain;
    if (a > 32767.0f) a = 32767.0f;
    if (a < -32768.0f) a = -32768.0f;
    return (int16_t)a;
}

int main() {
    const double rates[2] = {48000.0, 16000.0};
    const double cutoffs[5] = {10.0, 100.0, 500.0, 2000.0, 6000.0};
    const float gains[4] = {0.1f, 1.5f, 10.0f, 100.0f};
    const int shifts[3] = {8, 12, 16};
    const int N = 24000;

    double worstFixed = 0, worstFloat = 0;
    long fixedWithin1 = 0, total = 0;
    bool peakOk = true, clipOk = true;
    srand(7);

    for (double fs : rates) {
        for (double fc : cutoffs) {
            if (fc > fs * 0.45) continue;
            Coefs c = designHpf(fs, fc);
            for (float g : gains) {
                for (int sh : shifts) {
                    // Mic-like input: left-aligned 24-bit data with DC offset,
                    // tones and noise; level chosen so most blocks don't clip.
                    std::vector<int32_t> in(N);
                    double amp = 32767.0 / g * std::ldexp(1.0, sh) * 0.6;
                    for (int i = 0; i < N; i++) {
                        double v = amp * (0.5 * std::sin(2 * M_PI * 1000.0 * i / fs) +
                                          0.2 * std::sin(2 * M_PI * 37.0 * i / fs) +
                                          0.15 * ((rand() / (double)RAND_MAX) - 0.5)) +
                                   amp * 0.3;   // DC offset the HPF should remove
                        if (v > 2147483000.0) v = 2147483000.0;
                        if (v < -2147483000.0) v = -2147483000.0;
                        in[i] = (int32_t)v & ~0xFF;   // 24-bit mic in a 32-bit slot
                    }

                    fxaudio::BiquadQ28 fx;
                    fxaudio::biquadSetHighpass(&fx, fs, fc);
                    FloatBiquad fl(c);
                    std::vector<int16_t> outFx(N);
                    bool clipped = false;
                    uint16_t peak = 0;
                    for (int off = 0; off < N; off += 512) {   // firmware-sized blocks
                        int n = std::min(512, N - off);
                        bool cb = false;
                        uint16_t p = fxaudio::processBlock(&in[off], &outFx[off], n, (uint8_t)sh, &fx,
                                                           fxaudio::gainToQ(g), &cb);
                        if (p > peak) peak = p;
                        clipped = clipped || cb;
                    }

                    // Double-precision references: one primed like the integer
                    // stage, one unprimed like the old float stage.
                    double p0 = (double)(in[0] >> sh);
                    double x1 = p0, x2 = p0, y1 = 0, y2 = 0, refPeak = 0;
                    double u1 = 0, u2 = 0, v1 = 0, v2 = 0;
                    bool refClip = false;
                    for (int i = 0; i < N; i++) {
                        int16_t oFl = floatStage(in[i], sh, &fl, g);
                        double x = (double)(in[i] >> sh);
                        double y = c.b0 * x + c.b1 * x1 + c.b2 * x2 - c.a1 * y1 - c.a2 * y2;
                        x2 = x1; x1 = x; y2 = y1; y1 = y;
                        double v = c.b0 * x + c.b1 * u1 + c.b2 * u2 - c.a1 * v1 - c.a2 * v2;
                        u2 = u1; u1 = x; v2 = v1; v1 = v;
                        double a = y * g, af = v * g;
                        if (i > 0 && std::fabs(a) > refPeak) refPeak = std::fabs(a);
                        if (i > 0 && std::fabs(a) > 32767.0) refClip = true;
                        if (i > 2000 && std::fabs(a) < 32760.0) {   // settled, unclipped
                            double eFx = std::fabs(outFx[i] - a);
                            if (eFx > worstFixed) worstFixed = eFx;
                            total++;
                            if (eFx <= 0.5 + 1e-6) fixedWithin1++;
                        }
                        if (i > 2000 && std::fabs(af) < 32760.0) {
                            double eFl = std::fabs(oFl - af);
                            if (eFl > worstFloat) worstFloat = eFl;
                        }
                    }
                    double rp = std::min(refPeak, 32767.0);
                    if (std::fabs((double)peak - rp) > 2.0) peakOk = false;
                    if (clipped != refClip) {
                        // Allow disagreement only when the reference peak is
                        // within 1 LSB of the threshold.
                        if (std::fabs(refPeak - 32767.0) > 1.0) clipOk = false;
                    }
                }
            }
        }
    }

    std::printf("  integer stage: worst error %.2f LSB, %.2f%% of samples correctly rounded (<= 0.5 LSB)\n",
                worstFixed, 100.0 * fixedWithin1 / (double)total);
    std::printf("  previous float stage: worst error %.2f LSB\n", worstFloat);
    check(worstFixed <= 1.0, "integer stage within 1 LSB of double reference");
    check(worstFixed <= worstFloat, "integer stage at least as accurate as the float stage");
    check(peakOk, "peak meter matches reference within 2 LSB");
    check(clipOk, "clip flag matches reference");

    // High-pass off: pure shift + gain must be exact (rounded).
    {
        int32_t in[4] = {123456 * 256, -98765 * 256, 0, 2147483392};
        int16_t out[4];
        bool clip = false;
        fxaudio::processBlock(in, out, 4, 12, nullptr, fxaudio::gainToQ(1.5f), &clip);
        bool ok = out[0] == (int16_t)std::lround((123456 * 256 / 4096) * 1.5) &&
                  out[1] == (int16_t)std::floor((-98765 * 256 / 4096.0) * 1.5 + 0.5) && out[2] == 0 &&
                  out[3] == 32767 && clip;
        check(ok, "high-pass off: exact rounded gain, clamps and flags clipping");
    }

    // Stability at the extremes: 10 Hz cutoff, full-scale input, long run.
    {
        fxaudio::BiquadQ28 fx;
        fxaudio::biquadSetHighpass(&fx, 48000.0, 10.0);
        std::vector<int32_t> in(48000 * 5);
        for (size_t i = 0; i < in.size(); i++) in[i] = (i / 4800) % 2 ? INT32_MAX : INT32_MIN;
        std::vector<int16_t> out(in.size());
        bool clip = false;
        fxaudio::processBlock(in.data(), out.data(), (int)in.size(), 0, &fx, fxaudio::gainToQ(0.1f), &clip);
        // After the last step the filter must decay toward zero, not ring forever.
        std::vector<int32_t> zero(48000, 0);
        std::vector<int16_t> tail(48000);
        fxaudio::processBlock(zero.data(), tail.data(), 48000, 0, &fx, fxaudio::gainToQ(0.1f), &clip);
        check(std::abs(tail[47999]) < 2, "stable at 10 Hz cutoff with full-scale square input");
    }

    std::printf(failures ? "%d FAILURE(S)\n" : "All fixed-point audio checks passed\n", failures);
    return failures ? 1 : 0;
}
