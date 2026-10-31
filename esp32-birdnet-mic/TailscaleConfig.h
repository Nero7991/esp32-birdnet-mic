#pragma once
// Tailscale (fork build): pure helpers shared by TailscaleLink and the host tests.
// Nothing here ever handles the auth key beyond checking its shape.

#include <cstdint>
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

// Text for last_error. Not connected for over 30 s means the tailnet is unreachable;
// state 6 (error) means the control server refused the node.
inline const char *tailscaleProblem(int state, unsigned long notConnectedMs) {
    if (state == 6) return "The Tailscale control server rejected this device. Check the auth key and that the node was not removed.";
    if (state >= 1 && state <= 5 && state != 4 && notConnectedMs > 30000UL)
        return "Cannot reach Tailscale. Check that this network has internet access.";
    return "";
}

// The peer path is only meaningful while connected; MicroLink keeps cached peers otherwise.
inline const char *tailscalePeerPath(int state, const char *path) {
    return state == 4 && path ? path : "";
}

// True if an app image was built from this fork (ESP-IDF project "birdnet-mic-ts"). The
// project name sits in esp_app_desc_t, after the 24-byte image header and the 8-byte
// segment header. Uploading upstream's image would drop a remote mic off the tailnet.
inline bool forkImageIsOurs(const uint8_t *image, size_t len) {
    static const char kProject[] = "birdnet-mic-ts";
    if (!image || len < 112 || image[0] != 0xE9) return false;
    if (image[32] != 0x32 || image[33] != 0x54 || image[34] != 0xCD || image[35] != 0xAB) return false;
    return memcmp(image + 80, kProject, sizeof(kProject)) == 0;
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
