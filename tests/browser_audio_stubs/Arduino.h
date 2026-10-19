#pragma once
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
extern uint32_t testNow;
inline uint32_t millis() { return testNow; }
struct String : std::string {
    using std::string::string;
    String(std::string s) : std::string(std::move(s)) {}
    String substring(size_t start, size_t end = npos) const {
        return substr(start, end == npos ? npos : end - start);
    }
    int indexOf(char c) const { auto at = find(c); return at == npos ? -1 : int(at); }
    void toLowerCase() { std::transform(begin(), end(), begin(), [](unsigned char c) { return std::tolower(c); }); }
    void trim() {
        size_t first = find_first_not_of(" \t\r\n"), last = find_last_not_of(" \t\r\n");
        *this = first == npos ? String() : String(substr(first, last - first + 1));
    }
    bool endsWith(const char* text) const { size_t n = strlen(text); return size() >= n && compare(size() - n, n, text) == 0; }
    void remove(size_t at) { erase(at); }
};
