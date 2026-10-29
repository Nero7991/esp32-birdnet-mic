#ifdef BIRDNET_TS_FORK
#include "TailscaleLink.h"
#include <Preferences.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include "microlink.h"

extern String mdnsHostname;
extern void simplePrintln(String message);

static const char *TS_NVS_NAMESPACE = "tsnet";
static char tsAuthKey[201] = {0};
static String tsHostname;
static int tsState = TS_STATE_NO_KEY;
static String tsIp;
static String tsPeerPath;
static String tsLastError;
static microlink_t *tsLink = nullptr;
static uint32_t tsIpU32 = 0;
static unsigned long tsLastPollMs = 0;

void tailscaleLoad() {
    Preferences prefs;
    if (!prefs.begin(TS_NVS_NAMESPACE, true)) return;
    memset(tsAuthKey, 0, sizeof(tsAuthKey));
    if (prefs.isKey("key")) prefs.getString("key", tsAuthKey, sizeof(tsAuthKey));
    if (!tailscaleKeyLooksValid(tsAuthKey)) memset(tsAuthKey, 0, sizeof(tsAuthKey));
    tsHostname = prefs.isKey("host") ? prefs.getString("host", "") : String();
    prefs.end();
    tsState = tsAuthKey[0] ? 0 : TS_STATE_NO_KEY;
}

bool tailscaleSaveKey(const String &key) {
    if (!tailscaleKeyLooksValid(key.c_str())) return false;
    Preferences prefs;
    if (!prefs.begin(TS_NVS_NAMESPACE, false)) return false;
    bool ok = prefs.putString("key", key) == key.length();
    prefs.end();
    if (ok) tailscaleLoad();
    return ok;
}

void tailscaleClearKey() {
    Preferences prefs;
    if (prefs.begin(TS_NVS_NAMESPACE, false)) {
        prefs.remove("key");
        prefs.end();
    }
    memset(tsAuthKey, 0, sizeof(tsAuthKey));
    tsState = TS_STATE_NO_KEY;
}

bool tailscaleHasKey() {
    return tsAuthKey[0] != 0;
}

// Same character rules as mDNS hostnames: lowercase letters, digits and '-', 1 to 63 long.
bool tailscaleSaveHostname(const String &hostname) {
    if (hostname.length() > 63) return false;
    for (size_t i = 0; i < hostname.length(); i++) {
        char c = hostname[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || (c == '-' && i > 0 && i + 1 < hostname.length());
        if (!ok) return false;
    }
    Preferences prefs;
    if (!prefs.begin(TS_NVS_NAMESPACE, false)) return false;
    if (hostname.length()) prefs.putString("host", hostname);
    else prefs.remove("host");
    prefs.end();
    tsHostname = hostname;
    return true;
}

// Starts MicroLink once a key is stored and PSRAM is present. Any failure leaves the
// device LAN-only; nothing here restarts the device.
void tailscaleBegin() {
    tsLastError = "";
    if (ESP.getPsramSize() == 0) {
        tsState = TS_STATE_NO_PSRAM;
        simplePrintln("Tailscale: disabled, no PSRAM");
        return;
    }
    if (!tailscaleHasKey()) {
        tsState = TS_STATE_NO_KEY;
        return;
    }
    if (tsLink) return;
    String name = tsHostname.length() ? tsHostname : mdnsHostname;
    static char deviceName[64];
    strlcpy(deviceName, name.c_str(), sizeof(deviceName));
    microlink_config_t config = {};
    config.auth_key = tsAuthKey;
    config.device_name = deviceName;
    config.enable_derp = true;
    config.enable_stun = true;
    config.enable_disco = true;
    config.max_peers = 16;
    tsLink = microlink_init(&config);
    if (!tsLink) {
        tsState = 6;
        tsLastError = "init failed (memory)";
        simplePrintln("Tailscale: init failed");
        return;
    }
    if (microlink_start(tsLink) != ESP_OK) {
        tsState = 6;
        tsLastError = "start failed";
        simplePrintln("Tailscale: start failed");
        return;
    }
    tsState = (int)microlink_get_state(tsLink);
    simplePrintln("Tailscale: starting as " + name);
}

// Polls MicroLink about once a second for state, address and peer path.
void tailscaleLoop() {
    if (!tsLink) return;
    unsigned long now = millis();
    if (now - tsLastPollMs < 1000UL) return;
    tsLastPollMs = now;
    int state = (int)microlink_get_state(tsLink);
    uint32_t ip = microlink_get_vpn_ip(tsLink);
    if (ip != tsIpU32) {
        tsIpU32 = ip;
        if (ip) {
            char buf[16];
            microlink_ip_to_str(ip, buf);
            tsIp = buf;
            simplePrintln("Tailscale: address " + tsIp);
        } else {
            tsIp = "";
        }
    }
    if (state != tsState) {
        tsState = state;
        simplePrintln(String("Tailscale: ") + tailscaleStateName(state));
        if (state == 6) tsLastError = "control server rejected the node or is unreachable";
        else if (state == 4) tsLastError = "";
    }
    String path;
    int peers = microlink_get_peer_count(tsLink);
    for (int i = 0; i < peers; i++) {
        microlink_peer_info_t info;
        if (microlink_get_peer_info(tsLink, i, &info) != ESP_OK || !info.online) continue;
        path = info.direct_path ? "direct" : "derp";
        if (info.direct_path) break;
    }
    tsPeerPath = path;
}

IPAddress tailscaleIp() {
    if (!tsIpU32) return IPAddress((uint32_t)0);
    return IPAddress((uint8_t)(tsIpU32 >> 24), (uint8_t)(tsIpU32 >> 16), (uint8_t)(tsIpU32 >> 8), (uint8_t)tsIpU32);
}

TailscaleStatus tailscaleStatus() {
    TailscaleStatus s;
    s.available = ESP.getPsramSize() > 0;
    s.hasKey = tailscaleHasKey();
    s.state = tailscaleStateName(s.available ? tsState : TS_STATE_NO_PSRAM);
    s.ip = tsIp;
    s.hostname = tsHostname;
    s.peerPath = tsPeerPath;
    s.lastError = tsLastError;
    return s;
}
#endif
