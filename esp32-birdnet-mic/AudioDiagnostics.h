#pragma once
#include <stdint.h>

// These helpers have one owning task. The firmware publishes load as one
// atomic 32-bit snapshot; the ISR event total has a separate critical section.
class AudioProcessingWindow {
public:
    static constexpr uint64_t intervalUs = 2000000;
    static constexpr uint32_t validBit = 1UL << 20;

    void reset(uint64_t nowUs) {
        startedAtUs = nowUs;
        processingUs = 0;
        current = peak = 0;
        valid = false;
    }

    // busyUs includes processing after the I2S read, never its blocking wait.
    // Wall-clock timing can include preemption: this is an estimate, not CPU
    // scheduler accounting or a measurement of the whole chip.
    bool add(uint64_t nowUs, uint64_t busyUs) {
        if (nowUs < startedAtUs) { reset(nowUs); return false; }
        processingUs += busyUs;
        uint64_t elapsedUs = nowUs - startedAtUs;
        if (elapsedUs < intervalUs) return false;
        uint64_t load = processingUs * 1000 / elapsedUs;
        current = load > 1000 ? 1000 : static_cast<uint16_t>(load);
        if (current > peak) peak = current;
        valid = true;
        processingUs = 0;
        startedAtUs = nowUs;
        return true;
    }

    uint32_t snapshot() const { return valid ? validBit | current | (uint32_t(peak) << 10) : 0; }
    static uint16_t currentX10(uint32_t value) { return value & 1023; }
    static uint16_t peakX10(uint32_t value) { return (value >> 10) & 1023; }

private:
    uint64_t startedAtUs = 0, processingUs = 0;
    uint16_t current = 0, peak = 0;
    bool valid = false;
};

// Last-minute event increments, sampled by loop() about once a second.
// A delayed poll attributes its accumulated increment to that poll, so aging
// has one-poll precision; it is not an exact timestamp for each ISR event.
class RecentDmaOverruns {
public:
    void reset(uint32_t nowMs, uint32_t baseline = 0) {
        for (auto& slot : slots) slot = 0;
        totalInWindow = 0;
        previousTotal = baseline;
        lastTickMs = nowMs;
        next = 0;
        seen = false;
        lastObservedMs = 0;
    }

    uint32_t update(uint32_t nowMs, uint32_t total) {
        uint32_t seconds = uint32_t(nowMs - lastTickMs) / 1000;
        if (!seconds) return 0;
        lastTickMs += seconds * 1000;
        uint32_t added = total - previousTotal; // Handles counter wrap.
        previousTotal = total;
        if (seconds >= 60) {
            for (auto& slot : slots) slot = 0;
            totalInWindow = 0;
            next = 0;
        } else {
            for (uint32_t i = 1; i < seconds; ++i) push(0);
        }
        push(added);
        if (added) { seen = true; lastObservedMs = nowMs; }
        return added;
    }

    uint32_t count() const { return totalInWindow; }
    bool hasEvent() const { return seen; }
    uint32_t lastEventMs() const { return lastObservedMs; }

private:
    void push(uint32_t added) {
        totalInWindow -= slots[next];
        slots[next] = added;
        totalInWindow += added;
        next = (next + 1) % 60;
    }
    uint32_t slots[60] = {};
    uint32_t totalInWindow = 0, previousTotal = 0, lastTickMs = 0, lastObservedMs = 0;
    uint8_t next = 0;
    bool seen = false;
};

struct DmaOverrunStatus {
    bool available;
    uint32_t total;
    uint32_t recent;
    bool hasEvent;
    uint32_t lastObservedMs;
};
