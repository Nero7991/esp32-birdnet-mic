#pragma once
#include <stdint.h>
#include <math.h>

// Persist only events/state transitions, never the five-second sensor samples.
struct ThermalRecoveryState {
    uint32_t magic = 0x54485232; // THR2: ten-degree cooling margin, same layout.
    uint32_t tripEpoch[3] = {};
    float coolLimitC = 70.0f;
    uint8_t count = 0;
    uint8_t latched = 0;
    uint8_t manualBlocked = 0;
    uint8_t resumePending = 0;
    uint8_t notice = 0;
    uint8_t resumed = 0;
    uint8_t emergency = 0;
};

class ThermalRecovery {
public:
    static constexpr uint32_t WINDOW_SEC = 86400;
    static constexpr uint32_t COOL_MS = 300000;
    static constexpr uint32_t SAMPLE_MAX_AGE_MS = 15000;
    static constexpr float COOL_MARGIN_C = 10.0f;
    ThermalRecoveryState state;

    void restore(const ThermalRecoveryState& saved, bool legacyLatch, uint64_t uptimeSec, float limitC = 80.0f) {
        const bool legacy = saved.magic == 0x54485231;
        bool valid = (legacy || saved.magic == 0x54485232) && saved.count <= 3 &&
                     isfinite(saved.coolLimitC) && saved.coolLimitC >= 15 && saved.coolLimitC <= 85;
        state = valid ? saved : ThermalRecoveryState{};
        state.magic = 0x54485232;
        // Migrate an existing shutdown without erasing it or its retry history.
        // Do not let a later, lower configured limit weaken the cooling target.
        if (valid) state.coolLimitC = fminf(state.coolLimitC + (legacy ? 5.0f : 0.0f), limitC - COOL_MARGIN_C);
        else state.coolLimitC = limitC - COOL_MARGIN_C;
        if (!valid && legacyLatch) {
            // An old latch has no retry history: require acknowledgement.
            state.latched = state.manualBlocked = state.notice = 1;
        }
        if (state.latched) state.notice = 1;
        if (state.count >= 3 || state.emergency) state.manualBlocked = 1;
        for (uint8_t i = 0; i < 3; ++i) seenAtSec[i] = uptimeSec;
        resetCooling(); // Reboot always requires a fresh five-minute cooldown.
    }

    bool updateHistory(uint32_t epoch, uint64_t uptimeSec) {
        bool changed = false;
        uint8_t keep = 0;
        for (uint8_t i = 0; i < state.count; ++i) {
            uint64_t age = uptimeSec - seenAtSec[i];
            uint32_t stamp = state.tripEpoch[i];
            bool expired = age >= WINDOW_SEC || (epoch && stamp && epoch >= stamp && epoch - stamp >= WINDOW_SEC);
            if (expired) { changed = true; continue; }
            // Unsynced events retain their age in this boot. Across a reboot,
            // unknown age is conservatively counted for another 24 hours.
            if (!stamp && epoch) {
                stamp = epoch > age ? epoch - (uint32_t)age : epoch;
                changed = true;
            }
            state.tripEpoch[keep] = stamp;
            seenAtSec[keep++] = seenAtSec[i];
        }
        state.count = keep;
        // manualBlocked is sticky even when its events age out.
        return changed;
    }

    void trip(uint32_t epoch, uint64_t uptimeSec, float limitC, bool emergency, bool wasRunning) {
        updateHistory(epoch, uptimeSec);
        if (state.count < 3) {
            state.tripEpoch[state.count] = epoch;
            seenAtSec[state.count++] = uptimeSec;
        }
        state.latched = state.notice = 1;
        state.resumed = 0;
        state.emergency = emergency;
        state.manualBlocked = state.manualBlocked || state.count >= 3 || emergency;
        state.resumePending = wasRunning;
        state.coolLimitC = limitC - COOL_MARGIN_C;
        resetCooling();
    }

    void resetCooling() { cooling = false; }

    void sample(uint32_t nowMs, bool valid, float tempC) {
        if (!state.latched || !valid || !isfinite(tempC) || tempC > state.coolLimitC) {
            resetCooling();
            return;
        }
        if (!cooling || uint32_t(nowMs - lastSampleMs) > SAMPLE_MAX_AGE_MS) {
            coolSinceMs = nowMs;
            cooling = true;
        }
        lastSampleMs = nowMs;
    }

    bool cooled(uint32_t nowMs) const {
        return cooling && uint32_t(nowMs - lastSampleMs) <= SAMPLE_MAX_AGE_MS &&
               uint32_t(nowMs - coolSinceMs) >= COOL_MS;
    }

    uint32_t remainingSec(uint32_t nowMs) const {
        if (!cooling || uint32_t(nowMs - lastSampleMs) > SAMPLE_MAX_AGE_MS) return COOL_MS / 1000;
        uint32_t elapsed = uint32_t(nowMs - coolSinceMs);
        return elapsed >= COOL_MS ? 0 : (COOL_MS - elapsed + 999) / 1000;
    }

    bool autoPending(bool enabled, bool protection) const {
        return state.latched && enabled && protection && !state.manualBlocked && state.resumePending;
    }

    bool canResume(uint32_t nowMs, bool enabled, bool protection, bool scheduleAllowed) const {
        return autoPending(enabled, protection) && scheduleAllowed && cooled(nowMs);
    }

    void resume() {
        state.latched = state.resumePending = 0;
        state.resumed = 1;
        resetCooling();
    }

    // Acknowledging an active shutdown resets the retry series. Acknowledging
    // the warning after automatic recovery preserves the rolling trip count.
    bool acknowledge(uint32_t nowMs) {
        if (state.latched && !cooled(nowMs)) return false;
        if (state.latched) {
            state.count = state.latched = state.manualBlocked = state.resumePending = state.emergency = 0;
            for (uint8_t i = 0; i < 3; ++i) state.tripEpoch[i] = 0;
        }
        state.notice = state.resumed = 0;
        resetCooling();
        return true;
    }

private:
    uint64_t seenAtSec[3] = {};
    uint32_t coolSinceMs = 0, lastSampleMs = 0;
    bool cooling = false;
};
