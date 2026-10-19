// Host test for the software PDM decimator used on ESP32-C3/C5/C6.
// Build: g++ -std=c++11 -O2 -Wall -Wextra -Werror -Iesp32-birdnet-mic tests/pdm_filter_test.cpp
//
// A 2nd-order sigma-delta modulator stands in for the microphone (real PDM
// mics use higher-order modulators with a lower noise floor, so the SINAD
// measured here is a lower bound set by the test modulator, not the filter).

#include "PdmFilter.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

static const double kPdmRate = 3072000.0;   // 48 kHz * 64
static const double kPcmRate = 48000.0;
static const double kFs = 2147483648.0;     // PDM full scale -> int16 FS << 16
static int failures = 0;

static void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) failures++;
}

// Tone(s) through a 2nd-order sigma-delta modulator, packed 16 bits/word.
static std::vector<uint16_t> makePdm(const double* freq, const double* amp, int tones,
                                     size_t samples, bool lsbFirst, double noise = 0.0,
                                     double dc = 0.0) {
    const size_t bits = samples * pdmsw::kDecimation;
    std::vector<uint16_t> words(bits / 16, 0);
    double i1 = 0, i2 = 0, y = 0;
    uint32_t lcg = 12345;   // deterministic noise (stands in for mic self-noise)
    for (size_t n = 0; n < bits; n++) {
        double x = dc;
        if (noise > 0) {
            lcg = lcg * 1664525u + 1013904223u;
            x += noise * ((double)(lcg >> 8) / 16777216.0 - 0.5);
        }
        for (int k = 0; k < tones; k++) x += amp[k] * std::sin(2 * M_PI * freq[k] * n / kPdmRate);
        i1 += x - y;
        i2 += i1 - y;
        int bit = i2 >= 0 ? 1 : 0;
        y = bit ? 1.0 : -1.0;
        int t = (int)(n % 16);
        if (bit) words[n / 16] |= (uint16_t)(1u << (lsbFirst ? t : 15 - t));
    }
    return words;
}

static std::vector<int32_t> run(const std::vector<uint16_t>& words, bool lsbFirst) {
    pdmsw::Decimator* d = new pdmsw::Decimator;
    pdmsw::init(d, lsbFirst);
    std::vector<int32_t> out(words.size() / 4 + 4);
    // Feed in uneven chunks to exercise state carried across calls.
    size_t pos = 0, produced = 0, chunk = 37;
    while (pos < words.size()) {
        size_t n = std::min(chunk, words.size() - pos);
        produced += pdmsw::process(d, &words[pos], n, &out[produced], out.size() - produced);
        pos += n;
        chunk = chunk == 37 ? 256 : 37;
    }
    out.resize(produced);
    delete d;
    return out;
}

// Least-squares fit of DC + sin/cos at f; returns amplitude, fills residual RMS.
static double fit(const std::vector<int32_t>& y, size_t skip, double f, double* residRms) {
    const size_t n = y.size() - skip;
    double s[3][3] = {{0}}, r[3] = {0};
    for (size_t i = 0; i < n; i++) {
        double t = 2 * M_PI * f * (double)(i + skip) / kPcmRate;
        double b[3] = {1.0, std::sin(t), std::cos(t)};
        double v = y[i + skip] / kFs;
        for (int a = 0; a < 3; a++) {
            r[a] += b[a] * v;
            for (int c = 0; c < 3; c++) s[a][c] += b[a] * b[c];
        }
    }
    // Solve the 3x3 normal equations (Gaussian elimination).
    for (int c = 0; c < 3; c++) {
        for (int a = c + 1; a < 3; a++) {
            double m = s[a][c] / s[c][c];
            for (int k = c; k < 3; k++) s[a][k] -= m * s[c][k];
            r[a] -= m * r[c];
        }
    }
    double x[3];
    for (int a = 2; a >= 0; a--) {
        double v = r[a];
        for (int k = a + 1; k < 3; k++) v -= s[a][k] * x[k];
        x[a] = v / s[a][a];
    }
    double e2 = 0;
    for (size_t i = 0; i < n; i++) {
        double t = 2 * M_PI * f * (double)(i + skip) / kPcmRate;
        double e = y[i + skip] / kFs - (x[0] + x[1] * std::sin(t) + x[2] * std::cos(t));
        e2 += e * e;
    }
    if (residRms) *residRms = std::sqrt(e2 / n);
    return std::sqrt(x[1] * x[1] + x[2] * x[2]);
}

static double db(double v) { return 20.0 * std::log10(v); }

int main() {
    const size_t N = 19200;    // 0.4 s of output
    const size_t skip = 4800;  // 100 ms: DC blocker time constant is ~21 ms

    // Exactly one output per 4 raw words.
    {
        std::vector<uint16_t> idle(4 * 1000, 0xAAAA);
        std::vector<int32_t> out = run(idle, false);
        check(out.size() == 1000, "4N raw words -> N samples across uneven chunks");
        double peak = 0;
        for (size_t i = 100; i < out.size(); i++) peak = std::max(peak, std::fabs(out[i] / kFs));
        check(db(peak + 1e-12) < -90.0, "idle PDM (50% density) decodes to silence");
    }

    // Level and SINAD at 1 kHz, -6 dBFS and -40 dBFS.
    const double levels[2] = {0.5, 0.01};
    for (int li = 0; li < 2; li++) {
        double f[1] = {1000.0}, a[1] = {levels[li]};
        std::vector<int32_t> out = run(makePdm(f, a, 1, N, false), false);
        double resid = 0, amp = fit(out, skip, 1000.0, &resid);
        double gainDb = db(amp / levels[li]);
        double sinad = db(amp / std::sqrt(2.0) / resid);
        std::printf("  1 kHz @ %.0f dBFS: gain %.2f dB, SINAD %.1f dB\n", db(levels[li]), gainDb, sinad);
        check(std::fabs(gainDb) < 0.2, "1 kHz passband gain within 0.2 dB");
        // Thresholds sit just under a double-precision reference decoder fed the
        // same bits (73.7 dB / ~39.7 dB): the limit is this 2nd-order test
        // modulator's noise, not the filter arithmetic.
        check(sinad > (li == 0 ? 72.0 : 38.0), "SINAD matches reference decoder");
    }

    // Passband flatness at bird-song frequencies.
    const double pass[4] = {200.0, 5000.0, 10000.0, 15000.0};
    for (int i = 0; i < 4; i++) {
        double f[1] = {pass[i]}, a[1] = {0.25};
        std::vector<int32_t> out = run(makePdm(f, a, 1, N, false), false);
        double amp = fit(out, skip, pass[i], nullptr);
        double g = db(amp / 0.25);
        std::printf("  %5.0f Hz: %.2f dB\n", pass[i], g);
        check(g > -0.6 && g < 0.2, "passband response within +0.2/-0.6 dB");
    }

    // Alias rejection: a 40 kHz tone would fold to 8 kHz at 48 kHz output.
    {
        double f[1] = {40000.0}, a[1] = {0.5};
        std::vector<int32_t> out = run(makePdm(f, a, 1, N, false), false);
        double amp = fit(out, skip, 8000.0, nullptr);
        double rej = db(amp / 0.5 + 1e-12);
        std::printf("  40 kHz -> 8 kHz alias: %.1f dB\n", rej);
        check(rej < -60.0, "alias of 40 kHz into 8 kHz below -60 dB");
    }

    // Bit-order handling: LSB-first stream with lsbFirst=true decodes the same;
    // a mismatched setting degrades audio (reported, not asserted).
    {
        double f[1] = {3000.0}, a[1] = {0.5};
        double r1 = 0, r2 = 0, r3 = 0;
        double a1 = fit(run(makePdm(f, a, 1, N, true), true), skip, 3000.0, &r1);
        double a2 = fit(run(makePdm(f, a, 1, N, false), false), skip, 3000.0, &r2);
        double a3 = fit(run(makePdm(f, a, 1, N, false), true), skip, 3000.0, &r3);
        double s1 = db(a1 / std::sqrt(2.0) / r1), s2 = db(a2 / std::sqrt(2.0) / r2);
        std::printf("  3 kHz SINAD: MSB-first %.1f dB, LSB-first %.1f dB, mismatched %.1f dB\n",
                    s2, s1, db(a3 / std::sqrt(2.0) / r3));
        check(std::fabs(s1 - s2) < 1.0, "LSB-first option decodes identically");
    }

    // Automatic bit-order detection from one firmware read chunk (2048 words, ~11 ms).
    // Safety property: never pick the wrong order. "Undecided" is allowed
    // only for a perfectly silent, zero-offset input; the firmware then keeps
    // the hardware-default order and re-checks on later blocks.
    {
        const double amps[3] = {0.0, 0.05, 0.16};          // silence, -26, -16 dBFS
        const double freqs[4] = {300.0, 3000.0, 9000.0, 15000.0};
        const double dcs[2] = {0.0, 0.02};                 // mic DC offset
        int wrong = 0, undecided = 0, total = 0;
        pdmsw::Decimator* d = new pdmsw::Decimator;
        for (int ai = 0; ai < 3; ai++) {
            for (int fi = 0; fi < 4; fi++) {
                for (int di = 0; di < 2; di++) {
                    for (int order = 0; order < 2; order++) {
                        double f[1] = {freqs[fi]}, a[1] = {amps[ai]};
                        std::vector<uint16_t> w = makePdm(f, a, 1, 512, order == 1, 1e-3, dcs[di]);  // 2048 words, the firmware chunk
                        pdmsw::BitOrderResult r = pdmsw::detectBitOrder(d, w.data(), w.size());
                        total++;
                        pdmsw::BitOrderResult want = order == 1 ? pdmsw::ORDER_LSB_FIRST : pdmsw::ORDER_MSB_FIRST;
                        if (r == pdmsw::ORDER_UNDECIDED) {
                            undecided++;
                            if (amps[ai] != 0.0 || dcs[di] != 0.0) wrong++;   // must decide on real signal
                        } else if (r != want) {
                            wrong++;
                        }
                    }
                }
            }
        }
        std::printf("  bit-order detection: %d cases, %d wrong, %d undecided (silent zero-offset only)\n",
                    total, wrong, undecided);
        check(wrong == 0, "bit order never wrong; decided whenever there is signal or offset");
        std::vector<uint16_t> idle(4096, 0xAAAA);
        check(pdmsw::detectBitOrder(d, idle.data(), idle.size()) == pdmsw::ORDER_UNDECIDED,
              "noiseless idle pattern reported as undecided");
        delete d;
    }

    // Detection must not disturb the live decoder: run it on every firmware
    // chunk, as the firmware does while undecided, and compare the output with
    // an undisturbed decode, sample for sample.
    {
        double f[1] = {1000.0}, a[1] = {0.1};
        std::vector<uint16_t> w = makePdm(f, a, 1, 8192, false, 1e-3, 0.02);
        std::vector<int32_t> ref = run(w, false);
        pdmsw::Decimator* d = new pdmsw::Decimator;
        pdmsw::init(d, false);
        std::vector<int32_t> out(w.size() / 4 + 4);
        size_t produced = 0;
        std::vector<uint16_t> idle(2048, 0xAAAA);
        for (size_t pos = 0; pos < w.size(); pos += 2048) {
            size_t n = std::min((size_t)2048, w.size() - pos);
            pdmsw::detectBitOrder(d, &w[pos], n);           // decides MSB-first (= current)
            pdmsw::detectBitOrder(d, idle.data(), idle.size());  // undecided
            produced += pdmsw::process(d, &w[pos], n, &out[produced], out.size() - produced);
        }
        out.resize(produced);
        bool same = out.size() >= ref.size() - 1;
        for (size_t i = 0; same && i < std::min(out.size(), ref.size()); i++) same = out[i] == ref[i];
        check(same, "bit-order detection leaves the live decoder untouched (no clicks)");
        delete d;
    }

    // Robustness: strong ultrasonic content (as from a higher-order mic
    // modulator's shaped noise) must not confuse the detector.
    {
        int wrong = 0, total = 0;
        pdmsw::Decimator* d = new pdmsw::Decimator;
        const double ultra[3] = {60000.0, 80000.0, 95000.0};
        for (int ui = 0; ui < 3; ui++) {
            for (int order = 0; order < 2; order++) {
                double f[2] = {2000.0, ultra[ui]}, a[2] = {0.02, 0.3};
                std::vector<uint16_t> w = makePdm(f, a, 2, 512, order == 1, 1e-3, 0.01);
                pdmsw::init(d, order != 1);                 // start on the wrong order
                pdmsw::BitOrderResult r = pdmsw::detectBitOrder(d, w.data(), w.size());
                pdmsw::BitOrderResult want = order == 1 ? pdmsw::ORDER_LSB_FIRST : pdmsw::ORDER_MSB_FIRST;
                total++;
                if (r != want || d->lsbFirst != (order == 1)) wrong++;
            }
        }
        std::printf("  bit-order detection with strong ultrasonic content: %d cases, %d wrong\n", total, wrong);
        check(wrong == 0, "detector ignores ultrasonic content and switches the live order");
        delete d;
    }

    // Word pairs (the ESP32-C6 returns every word twice for a mono mic):
    // detect it, compact, and decode as if the stream had been clean.
    {
        double f[1] = {1000.0}, a[1] = {0.1};
        std::vector<uint16_t> clean = makePdm(f, a, 1, N, false, 1e-3, 0.01);
        std::vector<uint16_t> doubled;
        for (uint16_t w : clean) { doubled.push_back(w); doubled.push_back(w); }
        check(pdmsw::strideFromRate(153600, 400000, 192000) == 2, "384k words/s decided as pairs");
        check(pdmsw::strideFromRate(76800, 400000, 192000) == 1, "192k words/s decided as plain");
        check(pdmsw::strideFromRate(150000, 400000, 192000) == 2, "pairs with 2% timing jitter");
        check(pdmsw::strideFromRate(78000, 400000, 192000) == 1, "plain with 2% timing jitter");
        check(pdmsw::strideFromRate(115200, 400000, 192000) == 0, "1.5x rate left undecided");
        check(pdmsw::strideFromRate(1000, 100000, 192000) == 0, "too short a window left undecided");
        check(pdmsw::strideFromRate(51200, 400000, 64000) == 2, "16 kHz pairs (128k words/s)");
        check(pdmsw::detectWordStride(doubled.data(), 2048) == 2, "duplicated word pairs detected (stride 2)");
        check(pdmsw::detectWordStride(clean.data(), 2048) == 1, "plain stream detected (stride 1)");
        std::vector<uint16_t> idle(2048, 0xAAAA);
        check(pdmsw::detectWordStride(idle.data(), idle.size()) == 0, "constant pattern left undecided");
        std::vector<uint16_t> c = doubled;
        size_t n = pdmsw::compactWordPairs(c.data(), c.size());
        c.resize(n);
        check(c == clean, "compacting restores the original stream exactly");
        double r1 = 0, r2 = 0;
        double a1 = fit(run(c, false), skip, 1000.0, &r1);
        double a2 = fit(run(clean, false), skip, 1000.0, &r2);
        check(std::fabs(a1 - a2) < 1e-9 && std::fabs(r1 - r2) < 1e-9, "compacted stream decodes identically");
    }

    // Full-scale clipping saturates instead of wrapping.
    {
        std::vector<uint16_t> ones(4 * 2000, 0xFFFF);
        std::vector<int32_t> out = run(ones, false);
        bool wrapped = false;
        for (size_t i = 60; i < 200; i++) if (out[i] < 0) wrapped = true;
        check(!wrapped, "all-ones input does not wrap negative");
    }

    std::printf(failures ? "%d FAILURE(S)\n" : "All PDM filter checks passed\n", failures);
    return failures ? 1 : 0;
}
