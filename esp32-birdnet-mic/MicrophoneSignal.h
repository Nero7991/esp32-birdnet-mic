#pragma once
#include <atomic>
#include <stdint.h>

// One capture-task writer. Consumers only read published atomics. Evaluate raw
// I2S or decoded PDM PCM before user gain/shift/HPF; quiet audio is not a fault.
class MicrophoneSignal {
public:
    enum State { Unavailable, Checking, Healthy, Flatline };
    static constexpr uint32_t timeoutMs = 20000;
    void reset() {
        started = moved = havePrevious = false;
        published.store(Unavailable, std::memory_order_release);
    }
    void observe(int32_t minimum, int32_t maximum, uint32_t now) {
        if (!started) { lastMovement = now; started = true; }
        if (minimum != maximum || (havePrevious && minimum != previous)) {
            moved = true;
            lastMovement = now;
        }
        previous = maximum;
        havePrevious = true;
        readAt.store(now, std::memory_order_relaxed);
        uint32_t age = now - lastMovement;
        State value = age >= timeoutMs ? Flatline : moved ? Healthy : Checking;
        published.store(uint32_t(value) | (value == Flatline ? (age / 1000) << 2 : 0),
                        std::memory_order_release);
    }
    State state(uint32_t now, bool capturing = true) const {
        auto value = published.load(std::memory_order_acquire);
        uint32_t age = now - readAt.load(std::memory_order_relaxed);
        // A concurrently published read can be a few ms newer than caller's now.
        if (!capturing || (value & 3) == Unavailable || (age >= 2000 && age < UINT32_MAX - 1000))
            return Unavailable;
        return static_cast<State>(value & 3);
    }
    uint32_t flatSeconds(uint32_t now, bool capturing = true) const {
        return state(now, capturing) == Flatline ? published.load(std::memory_order_acquire) >> 2 : 0;
    }
    static const char* name(State state) {
        switch (state) {
        case Checking: return "checking";
        case Healthy: return "ok";
        case Flatline: return "flatline";
        default: return "unavailable";
        }
    }
private:
    bool started = false, moved = false, havePrevious = false;
    int32_t previous = 0;
    uint32_t lastMovement = 0;
    std::atomic<uint32_t> published{Unavailable}, readAt{0};
};
