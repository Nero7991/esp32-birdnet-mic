#pragma once
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <openssl/md5.h>
struct String : std::string {
    using std::string::string;
    String(std::string value) : std::string(value) {}
    String(uint32_t value) : std::string(std::to_string(value)) {}
    size_t length() const { return size(); }
};
inline String operator+(const String& left, size_t number) {
    return std::string(left) + std::to_string(number);
}
inline String operator+(const String& left, const char* right) { return std::string(left) + right; }
inline String operator+(const char* left, const String& right) { return left + std::string(right); }
inline String operator+(const String& left, const String& right) { return std::string(left) + std::string(right); }
#define F(value) value
using esp_err_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_ARG = -1, ESP_ERR_NOT_FOUND = -2;
constexpr int ESP_PARTITION_TYPE_APP = 0, ESP_PARTITION_SUBTYPE_APP_OTA_0 = 0x10, ESP_PARTITION_SUBTYPE_APP_OTA_1 = 0x11;
using esp_ota_img_states_t = unsigned;
constexpr unsigned ESP_OTA_IMG_VALID = 2, ESP_OTA_IMG_INVALID = 3, ESP_OTA_IMG_ABORTED = 4;
constexpr size_t OTA_WITH_SEQUENTIAL_WRITES = 0xFFFFFFFE;
constexpr int ESP_IMAGE_VERIFY = 0;
struct esp_partition_t { uint32_t address, size; };
struct esp_partition_pos_t { uint32_t offset, size; };
struct esp_image_metadata_t { uint32_t image_len; };
using esp_ota_handle_t = unsigned;
using md5_context_t = MD5_CTX;
inline void esp_rom_md5_init(md5_context_t* c) { MD5_Init(c); }
inline void esp_rom_md5_update(md5_context_t* c, const uint8_t* data, uint32_t len) { MD5_Update(c, data, len); }
inline void esp_rom_md5_final(uint8_t* digest, md5_context_t* c) { MD5_Final(digest, c); }
namespace fake {
inline std::vector<uint8_t> flash(4 * 1024 * 1024, 0xFF);
inline uint32_t clock = 0, size = 4 * 1024 * 1024;
inline esp_partition_t app0{0x10000, 0x140000}, app1{0x150000, 0x140000};
inline esp_partition_t* run = &app0;
inline esp_partition_t* boot = &app0;
inline std::map<std::string, bool> prefs;
inline bool failNvs = false, encrypted = false, secure = false, failConfirm = false;
inline bool failRead = false, failCopy = false, failBootSelect = false, failSize = false, failAlloc = false;
inline bool otherRecord = true, failInvalidate = false, failState = false;
inline unsigned tableWrites = 0, pauses = 0, resumes = 0, reboots = 0;
inline int failedWrites = 0, failedErases = 0;
inline int failedVerifyReads = 0, corruptWrites = 0;
inline bool reportWriteError = false;
inline unsigned copyBytes = 0;
inline std::vector<std::string> events;
inline std::vector<uint8_t> copied;
}
inline uint32_t millis() { return fake::clock; }
inline void delay(unsigned ms) { fake::clock += ms; }
struct MockESP {
    void restart() { ++fake::reboots; }
    uint32_t getFreeSketchSpace() { return fake::run == &fake::app0 ? fake::app1.size : fake::app0.size; }
};
inline MockESP ESP;
class Preferences {
public:
    bool begin(const char*, bool readOnly) { return !fake::failNvs && (!readOnly || !fake::prefs.empty()); }
    bool getBool(const char* key, bool fallback) { auto it = fake::prefs.find(key); return it == fake::prefs.end() ? fallback : it->second; }
    size_t putBool(const char* key, bool value) { if (fake::failNvs) return 0; fake::prefs[key] = value; return 1; }
    void end() {}
};
struct esp_flash_os_functions_t { esp_err_t (*region_protected)(void*, size_t, size_t); };
inline int guard(void*, size_t address, size_t size) {
    return address < 0x9000 && address + size > 0x8000 ? ESP_ERR_INVALID_ARG : ESP_OK;
}
inline esp_flash_os_functions_t normalFunctions{guard};
struct esp_flash_t { const esp_flash_os_functions_t* os_func = &normalFunctions; };
inline esp_flash_t chip;
inline esp_flash_t* esp_flash_default_chip = &chip;
inline esp_err_t esp_flash_get_size(esp_flash_t*, uint32_t* size) { *size = fake::size; return fake::failSize ? -1 : ESP_OK; }
inline bool esp_flash_encryption_enabled() { return fake::encrypted; }
inline bool esp_secure_boot_enabled() { return fake::secure; }
inline esp_err_t esp_flash_read(esp_flash_t*, uint8_t* out, uint32_t address, uint32_t length) {
    if (fake::failRead) return -1;
    if (fake::tableWrites && fake::failedVerifyReads > 0) { --fake::failedVerifyReads; return -1; }
    memcpy(out, fake::flash.data() + address, length); return ESP_OK;
}
inline esp_err_t esp_flash_erase_region(esp_flash_t* c, uint32_t address, uint32_t length) {
    assert(address == 0x8000 && length == 4096);
    assert(c->os_func->region_protected(nullptr, address, length) == ESP_OK);
    // The scoped exception must not permit erasing the bootloader.
    assert(c->os_func->region_protected(nullptr, 0x7000, 8192) != ESP_OK);
    if (fake::failedErases > 0) { --fake::failedErases; return -1; }
    std::fill_n(fake::flash.begin() + address, length, 0xFF);
    fake::events.push_back("erase");
    return ESP_OK;
}
inline esp_err_t esp_flash_write(esp_flash_t* c, const uint8_t* bytes, uint32_t address, uint32_t length) {
    assert(c->os_func->region_protected(nullptr, address, length) == ESP_OK);
    ++fake::tableWrites;
    if (fake::failedWrites > 0) { --fake::failedWrites; return -1; }
    memcpy(fake::flash.data() + address, bytes, length);
    if (fake::corruptWrites > 0) { --fake::corruptWrites; fake::flash[address] ^= 1; }
    return fake::reportWriteError ? -1 : ESP_OK;
}
inline const esp_partition_t* esp_ota_get_running_partition() { return fake::run; }
inline const esp_partition_t* esp_ota_get_next_update_partition(const esp_partition_t*) { return fake::run == &fake::app0 ? &fake::app1 : &fake::app0; }
inline const esp_partition_t* esp_partition_find_first(int, int subtype, const char*) { return subtype == 0x10 ? &fake::app0 : &fake::app1; }
inline const esp_partition_t* esp_ota_get_boot_partition() { return fake::boot; }
inline esp_err_t esp_ota_get_state_partition(const esp_partition_t* app, esp_ota_img_states_t* state) {
    if (fake::failState) return -1;
    if (app == &fake::app1 && !fake::otherRecord) return ESP_ERR_NOT_FOUND;
    *state = ESP_OTA_IMG_VALID; return ESP_OK;
}
inline esp_err_t esp_ota_set_boot_partition(const esp_partition_t* p) {
    if (fake::failBootSelect) return -1;
    fake::boot = p == &fake::app0 ? &fake::app0 : &fake::app1;
    fake::events.push_back("select"); return ESP_OK;
}
inline esp_err_t esp_ota_mark_app_valid_cancel_rollback() { fake::events.push_back("confirm"); return fake::failConfirm ? -1 : ESP_OK; }
inline esp_err_t esp_ota_invalidate_inactive_ota_data_slot() {
    fake::events.push_back("invalidate");
    if (!fake::otherRecord || fake::failInvalidate) return -1;
    fake::otherRecord = false; return ESP_OK;
}
inline esp_err_t esp_image_verify(int, const esp_partition_pos_t*, esp_image_metadata_t* image) { image->image_len = 8193; return ESP_OK; }
inline esp_err_t esp_ota_begin(const esp_partition_t* app, size_t, esp_ota_handle_t* handle) {
    assert(app != fake::run); *handle = 1; fake::copied.clear(); return ESP_OK;
}
inline esp_err_t esp_partition_read(const esp_partition_t* app, uint32_t offset, uint8_t* bytes, size_t size) {
    memcpy(bytes, fake::flash.data() + app->address + offset, size); return ESP_OK;
}
inline esp_err_t esp_ota_write(esp_ota_handle_t, const uint8_t* bytes, size_t length) {
    if (fake::failCopy) return -1;
    fake::copied.insert(fake::copied.end(), bytes, bytes + length); return ESP_OK;
}
inline esp_err_t esp_ota_end(esp_ota_handle_t) {
    fake::copyBytes = fake::copied.size();
    memcpy(fake::flash.data() + fake::app0.address, fake::copied.data(), fake::copied.size()); return ESP_OK;
}
inline void esp_ota_abort(esp_ota_handle_t) {}
