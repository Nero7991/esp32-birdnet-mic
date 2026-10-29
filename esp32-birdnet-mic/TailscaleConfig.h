#pragma once
// Tailscale (fork build): pure helpers shared by TailscaleLink and the host tests.
// Nothing here ever handles the auth key beyond checking its shape.

#include <cstring>

static constexpr int TS_STATE_NO_PSRAM = 100;
static constexpr int TS_STATE_NO_KEY = 101;

struct TailscaleStatus {
    bool available = false;  // PSRAM present, so MicroLink can run
    bool hasKey = false;
    const char *state = "off";
    String ip;
    String hostname;
    String peerPath;  // "direct", "derp" or empty
    String lastError;
};

// Accepts only Tailscale auth keys: "tskey-auth-" prefix, 20 to 200 printable characters.
inline bool tailscaleKeyLooksValid(const char *key) {
    if (!key) return false;
    size_t n = strlen(key);
    if (n < 20 || n > 200 || strncmp(key, "tskey-auth-", 11) != 0) return false;
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)key[i];
        if (c <= 0x20 || c >= 0x7f) return false;
    }
    return true;
}

// Maps microlink_state_t values (0..6) and the two fork states to API names.
inline const char *tailscaleStateName(int state) {
    switch (state) {
        case 0: return "off";
        case 1: return "waiting_wifi";
        case 2: return "connecting";
        case 3: return "registering";
        case 4: return "connected";
        case 5: return "reconnecting";
        case TS_STATE_NO_PSRAM: return "no_psram";
        case TS_STATE_NO_KEY: return "no_key";
        default: return "error";
    }
}

inline String tailscaleJsonString(const char *text) {
    String out = "\"";
    for (const char *p = text ? text : ""; *p; p++) {
        char c = *p;
        if (c == '"' || c == '\\') {
            out += '\\';
            out += c;
        } else if ((unsigned char)c < 0x20) {
            char buf[8];
            snprintf(buf, sizeof(buf), "\\u%04x", (unsigned)(unsigned char)c);
            out += buf;
        } else {
            out += c;
        }
    }
    out += "\"";
    return out;
}

inline String tailscaleStatusJson(const TailscaleStatus &s) {
    String json = "{";
    json += "\"available\":";
    json += s.available ? "true" : "false";
    json += ",\"has_key\":";
    json += s.hasKey ? "true" : "false";
    json += ",\"state\":" + tailscaleJsonString(s.state);
    json += ",\"ip\":" + tailscaleJsonString(s.ip.c_str());
    json += ",\"hostname\":" + tailscaleJsonString(s.hostname.c_str());
    json += ",\"peer_path\":" + tailscaleJsonString(s.peerPath.c_str());
    json += ",\"last_error\":" + tailscaleJsonString(s.lastError.c_str());
    json += "}";
    return json;
}
