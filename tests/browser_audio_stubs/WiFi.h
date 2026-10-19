#pragma once
#include <deque>
#include <memory>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>

class WiFiClient {
    struct Socket {
        int descriptor;
        explicit Socket(int fd) : descriptor(fd) {}
        ~Socket() { if (descriptor >= 0) close(descriptor); }
    };
    std::shared_ptr<Socket> socket;
public:
    WiFiClient() = default;
    explicit WiFiClient(int fd) : socket(std::make_shared<Socket>(fd)) {}
    int fd() { return socket ? socket->descriptor : -1; }
    bool connected() {
        if (fd() < 0) return false;
        char byte;
        return recv(fd(), &byte, 1, MSG_PEEK | MSG_DONTWAIT) != 0;
    }
    void stop() { if (fd() >= 0) { close(fd()); socket->descriptor = -1; } }
    void setNoDelay(bool) {}
    int available() { int bytes = 0; if (fd() >= 0) ioctl(fd(), FIONREAD, &bytes); return bytes; }
    int read(uint8_t* data, size_t bytes) { return recv(fd(), data, bytes, MSG_DONTWAIT); }
};
inline std::deque<WiFiClient> testConnections;
struct WiFiServer {
    explicit WiFiServer(int) {}
    void begin() {}
    void setNoDelay(bool) {}
    WiFiClient accept() {
        if (testConnections.empty()) return {};
        WiFiClient client = testConnections.front(); testConnections.pop_front(); return client;
    }
};
