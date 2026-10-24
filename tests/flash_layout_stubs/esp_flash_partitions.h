#pragma once
#include "Platform.h"
struct esp_partition_info_t {
    uint16_t magic;
    uint8_t type, subtype;
    uint32_t offset, size;
    char label[16];
    uint32_t flags;
};
inline esp_err_t esp_partition_table_verify(const esp_partition_info_t* table, bool, int* count) {
    const auto* bytes = reinterpret_cast<const uint8_t*>(table);
    for (unsigned i=0; i<96; ++i) {
        uint16_t magic; memcpy(&magic, bytes+i*32, 2);
        if (magic==0x50AA) continue;
        if (magic!=0xEBEB || !i) return -1;
        uint8_t digest[16]; MD5(bytes,i*32,digest);
        if (memcmp(bytes+i*32+16,digest,16)) return -1;
        *count=i; return ESP_OK;
    }
    return -1;
}
