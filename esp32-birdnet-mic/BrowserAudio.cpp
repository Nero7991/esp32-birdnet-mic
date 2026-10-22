#include <Arduino.h>
#include <WiFi.h>
#include <errno.h>
#include <sys/socket.h>
#include "BrowserAudio.h"
#include "PreviewBuffer.h"
#if defined(CONFIG_IDF_TARGET_ESP32C5) && CONFIG_SPIRAM
#include <esp_heap_caps.h>
#endif

namespace {
WiFiServer listener(81);
WiFiClient client;
PreviewBuffer queue;
char request[1025];
size_t requestSize = 0, pendingSize = 0, pendingOffset = 0;
uint8_t pending[2048];
uint32_t acceptedAt = 0, progressedAt = 0, lastAudioAt = 0;
bool sending = false;

void reject(WiFiClient& target, const char* status) {
    char reply[128];
    int size = snprintf(reply, sizeof(reply), "HTTP/1.1 %s\r\nConnection: close\r\nContent-Length: 0\r\n\r\n", status);
    // A rejection must also never block the RTSP loop.
    ::send(target.fd(), reply, size, MSG_DONTWAIT);
    target.stop();
}

bool prepareHeader(uint32_t rate) {
    // The browser may read audio only from the device page on port 80. Command
    // line players can omit Origin. Never reflect an arbitrary cross-site Origin.
    String origin, host;
    char* line = strstr(request, "\r\n");
    while (line && line[2]) {
        line += 2;
        char* end = strstr(line, "\r\n");
        if (!end || end == line) break;
        String header = String(line).substring(0, end - line);
        int colon = header.indexOf(':');
        if (colon > 0) {
            String name = header.substring(0, colon); name.toLowerCase();
            String value = header.substring(colon + 1); value.trim();
            if (name == "origin") origin = value;
            if (name == "host") host = value;
        }
        line = end;
    }
    if (!host.endsWith(":81")) return false;
    host.remove(host.length() - 3);
    if (origin.length() && origin != "http://" + host) return false;
    int size = snprintf(reinterpret_cast<char*>(pending), sizeof(pending),
        "HTTP/1.1 200 OK\r\nContent-Type: audio/wav\r\nCache-Control: no-store\r\nConnection: close\r\n%s%s%s\r\n",
        origin.length() ? "Access-Control-Allow-Origin: " : "",
        origin.length() ? origin.c_str() : "",
        origin.length() ? "\r\nVary: Origin\r\n" : "");
    if (size <= 0 || size + 44 > int(sizeof(pending))) return false;
    uint8_t* wav = pending + size;
    memset(wav, 0, 44);
    memcpy(wav, "RIFF", 4); memcpy(wav + 8, "WAVEfmt ", 8); memcpy(wav + 36, "data", 4);
    auto put32 = [wav](size_t at, uint32_t value) {
        for (uint8_t i = 0; i < 4; ++i) wav[at + i] = uint8_t(value >> (8 * i));
    };
    put32(4, 0x7fffffff); put32(16, 16); wav[20] = 1; wav[22] = 1;
    put32(24, rate); put32(28, rate * 2); wav[32] = 2; wav[34] = 16; put32(40, 0x7fffffff);
    pendingSize = size + 44;
    pendingOffset = 0;
    return queue.begin();
}
}

void browserAudioBegin() {
#if defined(CONFIG_IDF_TARGET_ESP32C5) && CONFIG_SPIRAM
    // C5 with PSRAM: 32 KiB (about 340 ms) in PSRAM instead of the 4 KiB
    // internal queue; the internal queue remains the fallback.
    static bool storagePrepared = false;
    static constexpr size_t kPsramQueueBytes = 32768;
    if (!storagePrepared && heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM) >= kPsramQueueBytes + 64 * 1024) {
        uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(kPsramQueueBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (buffer && !queue.useStorage(buffer, kPsramQueueBytes)) heap_caps_free(buffer);
    }
    storagePrepared = true;
#endif
    listener.begin(); listener.setNoDelay(true);
}
size_t browserAudioQueueBytes() { return queue.queueBytes(); }
bool browserAudioActive() { return queue.active(); }
void browserAudioPush(const int16_t* samples, size_t count) { queue.push(samples, count * sizeof(int16_t)); }
void browserAudioStop() {
    queue.end();
    client.stop();
    requestSize = pendingSize = pendingOffset = 0;
    sending = false;
    request[0] = '\0';
}

void browserAudioLoop(bool allowed, uint32_t rate) {
    uint32_t now = millis();
    if (!allowed) browserAudioStop();
    if (!client.connected()) browserAudioStop();
    WiFiClient incoming = listener.accept();
    if (incoming.connected()) {
        if (!allowed || client.connected()) reject(incoming, "503 Service Unavailable");
        else {
            client = incoming; client.setNoDelay(true);
            acceptedAt = progressedAt = lastAudioAt = now;
        }
    }
    if (!client.connected()) return;
    if (!sending) {
        int available = client.available();
        size_t room = sizeof(request) - 1 - requestSize;
        if (available > 0 && room) {
            size_t bytes = size_t(available) < room ? size_t(available) : room;
            int got = client.read(reinterpret_cast<uint8_t*>(request + requestSize), bytes);
            if (got > 0) requestSize += got;
            request[requestSize] = '\0';
        }
        if (!strstr(request, "\r\n\r\n")) {
            if (!room || uint32_t(now - acceptedAt) >= 2000) browserAudioStop();
            return;
        }
        if (strncmp(request, "GET /listen.wav HTTP/1.", 22) || !prepareHeader(rate)) {
            reject(client, "400 Bad Request"); browserAudioStop(); return;
        }
        sending = true;
    }
    if (queue.overflowed() || uint32_t(now - progressedAt) >= 500 || uint32_t(now - lastAudioAt) >= 2000) {
        browserAudioStop(); return;
    }
    // At most one 2 KiB nonblocking write per pass, after RTSP has been serviced.
    if (pendingOffset == pendingSize) {
        pendingSize = queue.pop(pending, sizeof(pending)); pendingOffset = 0;
        if (pendingSize) lastAudioAt = now;
    }
    if (!pendingSize) { progressedAt = now; return; }
    int sent = ::send(client.fd(), pending + pendingOffset, pendingSize - pendingOffset, MSG_DONTWAIT);
    if (sent > 0) { pendingOffset += sent; progressedAt = now; }
    else if (!sent || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) browserAudioStop();
}
