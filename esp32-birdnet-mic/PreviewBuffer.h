#pragma once
#if defined(__has_include)
#if __has_include(<sdkconfig.h>)
#include <sdkconfig.h>
#endif
#endif
#include <atomic>
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Independent, bounded PCM queue. No capture-side allocation, waiting or socket
// calls. A slow preview is disconnected instead of applying backpressure to RTSP.
class PreviewBuffer {
public:
#if defined(CONFIG_IDF_TARGET_ESP32C5)
    // Free 12 KiB of internal RAM for capture and Wi-Fi/TCP on C5.
    // At 48 kHz mono int16 this holds about 43 ms for one browser listener.
    static constexpr size_t capacity = 4096;
#else
    static constexpr size_t capacity = 16384;
#endif
    bool begin() {
        // Only the consumer discards backlog. Monotonic offsets are never reset
        // while capture could still be publishing a block from the last session.
        tail.store(head.load(std::memory_order_acquire), std::memory_order_release);
        failed.store(false);
        enabled.store(true);
        return true;
    }
    void end() { enabled.store(false); }
    bool active() const { return enabled.load(); }
    bool overflowed() const { return failed.load(); }
    void push(const void* data, size_t bytes) {
        if (!active()) return;
        uint32_t write = head.load(std::memory_order_relaxed);
        uint32_t read = tail.load(std::memory_order_acquire);
        if (bytes > capacity - uint32_t(write - read)) failed.store(true);
        else {
            size_t offset = write % capacity;
            size_t first = bytes < capacity - offset ? bytes : capacity - offset;
            memcpy(storage + offset, data, first);
            memcpy(storage, static_cast<const uint8_t*>(data) + first, bytes - first);
            head.store(write + bytes, std::memory_order_release);
        }
    }
    size_t pop(void* data, size_t bytes) {
        uint32_t read = tail.load(std::memory_order_relaxed);
        uint32_t used = head.load(std::memory_order_acquire) - read;
        if (bytes > used) bytes = used;
        size_t offset = read % capacity;
        size_t first = bytes < capacity - offset ? bytes : capacity - offset;
        memcpy(data, storage + offset, first);
        memcpy(static_cast<uint8_t*>(data) + first, storage, bytes - first);
        tail.store(read + bytes, std::memory_order_release);
        return bytes;
    }
private:
    uint8_t storage[capacity];
    std::atomic<uint32_t> head{0}, tail{0};
    std::atomic<bool> enabled{false}, failed{false};
};
