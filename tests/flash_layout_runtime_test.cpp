// Execute the actual firmware coordinator with flash/NVS/boot fault injection.
#include "flash_layout_stubs/Platform.h"
bool otaInProgress = false;
volatile unsigned long scheduledRebootAt = 0;
void beginOtaUpdate() { if (!otaInProgress) ++fake::pauses; otaInProgress = true; }
void endOtaUpdate(bool installed) { if (!installed) { otaInProgress = false; ++fake::resumes; } }
void scheduleReboot(bool reset, uint32_t ms) { assert(!reset); scheduledRebootAt = millis() + ms; }
void webui_pushLog(const String&) {}
static void* controlledMalloc(size_t size) { return fake::failAlloc ? nullptr : std::malloc(size); }
#define malloc controlledMalloc
#include "FlashLayout.cpp"
#undef malloc
using namespace flashtable;

static void reset(bool app1 = false) {
    free(flashlayout::recoveryTable); flashlayout::recoveryTable = nullptr;
    flashlayout::queued = flashlayout::working = flashlayout::recovery = false;
    flashlayout::lastError = "";
    otaInProgress = false; scheduledRebootAt = 0;
    fake::clock = 0; fake::size = 4 * 1024 * 1024;
    fake::failNvs = fake::encrypted = fake::secure = fake::failConfirm = false;
    fake::failRead = fake::failCopy = fake::failBootSelect = fake::failSize = fake::failAlloc = false;
    fake::otherRecord = true; fake::failInvalidate = fake::failState = false;
    fake::pauses = fake::resumes = fake::tableWrites = fake::reboots = fake::copyBytes = 0;
    fake::failedWrites = fake::failedErases = 0;
    fake::failedVerifyReads = fake::corruptWrites = 0; fake::reportWriteError = false;
    fake::prefs.clear(); fake::events.clear();
    fake::app0 = {0x10000, kDefaultCapacity}; fake::app1 = {0x150000, kDefaultCapacity};
    fake::run = app1 ? &fake::app1 : &fake::app0; fake::boot = fake::run;
    std::fill(fake::flash.begin(), fake::flash.end(), 0xFF);
    build(fake::flash.data() + kTableAddress, Default, flashlayout::md5);
    std::fill_n(fake::flash.begin() + fake::run->address, 8193, 0xE9);
    // Sentinel settings bytes may never change.
    std::fill_n(fake::flash.begin() + 0x9000, 0x5000, 0x3A);
}
static void settingsKept() {
    for (size_t i = 0x9000; i < 0xE000; ++i) assert(fake::flash[i] == 0x3A);
    assert(chip.os_func == &normalFunctions);
}
static void request() {
    String error;
    assert(flashlayout::start(error));
    assert(flashlayout::busy());
    assert(flashlayout::start(error)); // Acknowledge a retry without a double start.
    assert(fake::tableWrites == 0); // HTTP response before the operation.
    flashlayout::loop(); assert(fake::pauses == 0);
    fake::clock += 300; flashlayout::loop();
}
static void reboot() {
    flashlayout::working = flashlayout::queued = false;
    otaInProgress = false; scheduledRebootAt = 0;
    fake::run = fake::boot;
    if (flashlayout::detect() == Enlarged) {
        fake::app0.size = kEnlargedCapacity;
        fake::app1.address = 0x200000; fake::app1.size = kEnlargedCapacity;
    }
    flashlayout::begin();
}
int main() {
    reset();
    flashlayout::begin(); assert(!flashlayout::busy() && fake::tableWrites == 0);
    assert(flashlayout::statusJson().find("available") != std::string::npos);
    String blocked;
    assert(!flashlayout::otaAllowed(blocked) && blocked.find("Prepare device") != std::string::npos);
    assert(!fake::pauses && !fake::tableWrites); // Gate itself is read-only.
    request();
    assert(!flashlayout::otaAllowed(blocked));
    assert(fake::pauses == 1 && fake::tableWrites == 1 && scheduledRebootAt);
    assert(flashlayout::statusJson().find("working") != std::string::npos); // Not ready with cached descriptors.
    assert(fake::events[0] == "select" && fake::events[1] == "confirm" && fake::events[2] == "invalidate");
    // Simulate flags cleared without the actual reboot: raw table is larger,
    // but cached descriptors still old. This cannot enable an OTA.
    flashlayout::working=false;
    assert(!flashlayout::otaAllowed(blocked));
    flashlayout::working=true;
    reboot(); assert(flashlayout::statusJson().find("ready") != std::string::npos);
    assert(flashlayout::otaAllowed(blocked));
    fake::failRead=true; assert(!flashlayout::otaAllowed(blocked));
    fake::failRead=false; assert(flashlayout::otaAllowed(blocked));
    fake::failAlloc=true;
    assert(!flashlayout::otaAllowed(blocked));
    assert(flashlayout::statusJson().find("check_failed")!=std::string::npos);
    fake::failAlloc=false; assert(flashlayout::otaAllowed(blocked));
    assert(!fake::prefs["pending"] && !flashlayout::busy()); settingsKept();
    String error; assert(flashlayout::start(error) && !flashlayout::busy());

    reset(true); request();
    assert(fake::copyBytes == 8193 && fake::tableWrites == 0 && fake::boot == &fake::app0);
    assert(fake::prefs["pending"] && scheduledRebootAt);
    reboot(); assert(fake::tableWrites == 1 && fake::reboots == 1); // Early boot: no audio/network needed.
    reboot(); assert(flashlayout::statusJson().find("ready") != std::string::npos); settingsKept();

    reset(); fake::failedWrites = 1; request(); // Retry succeeds.
    assert(fake::tableWrites == 2 && scheduledRebootAt); settingsKept();
    reset(); fake::failedVerifyReads = 1; request(); // A transient verification-read failure.
    assert(fake::tableWrites == 2 && scheduledRebootAt); settingsKept();
    reset(); fake::reportWriteError = true; request(); // Read-back proves the write completed despite its error.
    assert(fake::tableWrites == 1 && scheduledRebootAt); settingsKept();
    reset(); fake::corruptWrites = 2; request(); // Successful return with corrupt bytes must be detected.
    assert(fake::tableWrites == 3 && flashlayout::detect() == Default && !scheduledRebootAt); settingsKept();
    reset(); fake::failedWrites = 2; request(); // Both new writes fail: restore original.
    assert(fake::tableWrites == 3 && flashlayout::detect() == Default);
    assert(!scheduledRebootAt && !otaInProgress && !flashlayout::busy());
    assert(flashlayout::statusJson().find("error") != std::string::npos); settingsKept();

    reset(); fake::failedWrites = 4; request(); // No verified table: stay powered and paused.
    assert(!scheduledRebootAt && otaInProgress && flashlayout::busy());
    assert(flashlayout::statusJson().find("recovery_required") != std::string::npos); settingsKept();
    assert(flashlayout::recoveryTable);
    assert(flashlayout::start(error)); fake::clock += 300; flashlayout::loop();
    assert(scheduledRebootAt && !flashlayout::recoveryTable); settingsKept();
    reset(); fake::failedVerifyReads = 4; request(); // Never reboot without a successful read-back.
    assert(!scheduledRebootAt && flashlayout::recoveryTable && otaInProgress); settingsKept();

    for (int fault = 0; fault < 6; ++fault) {
        reset();
        if (fault == 0) fake::size *= 2;
        if (fault == 1) fake::encrypted = true;
        if (fault == 2) fake::secure = true;
        if (fault == 3) fake::flash[0x8000 + 28] = 1; // Encrypted/custom flags.
        if (fault == 4) fake::flash[0x8000 + 6 * 32 + 16] ^= 1; // Bad MD5.
        if (fault == 5) fake::failRead = true;
        assert(!flashlayout::start(error)); assert(fake::tableWrites == 0); settingsKept();
    }
    reset(); fake::failNvs = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(); fake::failConfirm = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(); fake::failInvalidate = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(); fake::failState = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(); fake::otherRecord = false; request(); assert(fake::tableWrites == 1 && scheduledRebootAt); settingsKept();
    reset(true); fake::failCopy = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(true); fake::failBootSelect = true; request(); assert(fake::tableWrites == 0 && !scheduledRebootAt); settingsKept();
    reset(true); fake::prefs["pending"] = true; flashlayout::begin();
    assert(!fake::prefs["pending"] && fake::tableWrites == 0); // Interrupted before boot selection.
    reset(); fake::failRead=true; assert(!flashlayout::otaAllowed(error));
    fake::failRead=false; assert(!flashlayout::otaAllowed(error)); // No failed-read bypass.
    reset(); fake::failSize=true; assert(!flashlayout::otaAllowed(error));
    reset(); fake::flash[0x8000 + 6*32 + 16]^=1; assert(!flashlayout::otaAllowed(error));
    // A valid custom layout cannot use Prepare; preserve its ordinary OTA.
    reset(); fake::flash[0x8000+12]='x';
    flashlayout::md5(fake::flash.data()+0x8000, 6*32, fake::flash.data()+0x8000+6*32+16);
    assert(flashlayout::detect()==Unknown && flashlayout::otaAllowed(error));
    reset(); fake::size=8*1024*1024; assert(flashlayout::otaAllowed(error));
    reset(); fake::encrypted=true; assert(flashlayout::otaAllowed(error));
    reset(); fake::secure=true; assert(flashlayout::otaAllowed(error));
    reset(); otaInProgress = true; assert(!flashlayout::start(error));
    reset(); scheduledRebootAt = 10; assert(!flashlayout::start(error));
}
