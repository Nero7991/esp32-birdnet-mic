#pragma once
// Tailscale on the device (fork build only). The auth key lives in NVS and in one
// static buffer that MicroLink borrows; it is never logged, printed or returned.
#ifdef BIRDNET_TS_FORK
#include <Arduino.h>
#include "TailscaleConfig.h"

void tailscaleLoad();
bool tailscaleSaveKey(const String &key);
void tailscaleClearKey();
bool tailscaleHasKey();
bool tailscaleSaveHostname(const String &hostname);
TailscaleStatus tailscaleStatus();
#endif
