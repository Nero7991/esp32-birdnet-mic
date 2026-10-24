#pragma once
// Based on Dan Logue's proposed C3/C6 layout. Keep this logic independent of
// ESP-IDF so the exact bytes and failure recovery can also be tested on a host.
#include <stdint.h>
#include <string.h>
#include <initializer_list>

namespace flashtable {
constexpr uint32_t kTableAddress = 0x8000;
constexpr uint32_t kSectorSize = 0x1000;
constexpr uint32_t kTableSize = 0xC00;
constexpr uint32_t kDefaultCapacity = 0x140000;
constexpr uint32_t kEnlargedCapacity = 0x1F0000;
using Md5Fn = void (*)(const uint8_t*, uint32_t, uint8_t[16]);
enum Layout { Unknown, Default, Enlarged };

struct Entry {
    uint16_t magic;
    uint8_t type, subtype;
    uint32_t offset, size;
    char label[16];
    uint32_t flags;
};
static_assert(sizeof(Entry) == 32, "ESP partition entry size");

inline void build(uint8_t* out, Layout layout, Md5Fn md5) {
    const bool enlarged = layout == Enlarged;
    const Entry entries[] = {
        {0x50AA, 1, 2, 0x9000, 0x5000, "nvs", 0},
        {0x50AA, 1, 0, 0xE000, 0x2000, "otadata", 0},
        {0x50AA, 0, 0x10, 0x10000, enlarged ? kEnlargedCapacity : kDefaultCapacity, "app0", 0},
        {0x50AA, 0, 0x11, enlarged ? 0x200000u : 0x150000u,
         enlarged ? kEnlargedCapacity : kDefaultCapacity, "app1", 0},
        {0x50AA, 1, 0x82, 0x290000, 0x160000, "spiffs", 0},
        {0x50AA, 1, 3, 0x3F0000, 0x10000, "coredump", 0}
    };
    memset(out, 0xFF, kSectorSize);
    memcpy(out, entries, 4 * sizeof(Entry));
    uint32_t length = 4 * sizeof(Entry);
    if (!enlarged) { memcpy(out + length, &entries[4], sizeof(Entry)); length += sizeof(Entry); }
    memcpy(out + length, &entries[5], sizeof(Entry));
    length += sizeof(Entry);
    out[length] = out[length + 1] = 0xEB;
    md5(out, length, out + length + 16);
}

// Exact comparison includes labels, flags, MD5 and all unused sector bytes.
// Custom/encrypted/corrupt tables must never be silently converted.
inline Layout classify(const uint8_t* table, uint8_t* scratch, Md5Fn md5) {
    for (Layout layout : {Default, Enlarged}) {
        build(scratch, layout, md5);
        if (memcmp(table, scratch, kSectorSize) == 0) return layout;
    }
    return Unknown;
}

enum RewriteResult { Prepared, Restored, RecoveryRequired };

// Driver supplies erase(), write(bytes), read(bytes). A reported driver error
// can still leave valid data: always read back before deciding what to do.
template<class Driver>
RewriteResult rewrite(Driver& driver, const uint8_t* original,
                      const uint8_t* enlarged, uint8_t* scratch) {
    for (const uint8_t* target : {enlarged, original}) {
        for (unsigned attempt = 0; attempt < 2; ++attempt) {
            if (driver.erase()) driver.write(target);
            if (driver.read(scratch) && memcmp(scratch, target, kSectorSize) == 0)
                return target == enlarged ? Prepared : Restored;
        }
    }
    return RecoveryRequired;
}
} // namespace flashtable
