#pragma once
#include <Arduino.h>

// Limit runtime repartitioning to the supported 4 MB XIAO targets, including
// builds in the Arduino IDE. S3/C5 and generic ESP32 builds never offer it.
#if defined(ARDUINO_XIAO_ESP32C3) || defined(ARDUINO_XIAO_ESP32C6)
#define FLASH_LAYOUT_TOOL 1
#else
#define FLASH_LAYOUT_TOOL 0
#endif

namespace flashlayout {
// Called before settings/audio/network startup. Finishes only an explicitly
// requested, persisted operation; an ordinary firmware OTA never repartitions.
void begin();
void loop();
bool busy();
// Only firmware containing this tool (v1.35+) enforces this policy.
// Older firmware can still install the bridge release normally.
bool otaAllowed(String& error);
bool start(String& error);
String statusJson();
} // namespace flashlayout
