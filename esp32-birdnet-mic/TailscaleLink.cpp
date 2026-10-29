#ifdef BIRDNET_TS_FORK
#include "TailscaleLink.h"
#include <Preferences.h>

static const char *TS_NVS_NAMESPACE = "tsnet";
static char tsAuthKey[201] = {0};
static String tsHostname;
static int tsState = TS_STATE_NO_KEY;
static String tsIp;
static String tsPeerPath;
static String tsLastError;

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
