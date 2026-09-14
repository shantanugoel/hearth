#ifndef HEARTH_UTIL_H_
#define HEARTH_UTIL_H_

#include <cstddef>
#include <cstdio>
#include <cstring>

inline void HearthCopy(char* dst, size_t cap, const char* src) {
    if (dst == nullptr || cap == 0) {
        return;
    }
    if (src == nullptr) {
        dst[0] = '\0';
        return;
    }
    std::snprintf(dst, cap, "%s", src);
}

inline bool HearthJsonString(const char* json, const char* key, char* out,
                             size_t cap) {
    if (json == nullptr || key == nullptr || out == nullptr || cap == 0) {
        return false;
    }
    char needle[48];
    std::snprintf(needle, sizeof(needle), "\"%s\"", key);
    const char* cursor = std::strstr(json, needle);
    if (cursor == nullptr) {
        return false;
    }
    cursor += std::strlen(needle);
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor != ':') {
        return false;
    }
    cursor++;
    while (*cursor == ' ' || *cursor == '\t') {
        cursor++;
    }
    if (*cursor == '"') {
        cursor++;
        size_t n = 0;
        while (*cursor != '\0' && *cursor != '"' && n + 1 < cap) {
            if (*cursor == '\\' && cursor[1] != '\0') {
                const char esc = cursor[1];
                char ch = esc;
                if (esc == 'n' || esc == 'r' || esc == 't') {
                    ch = ' ';
                }
                out[n++] = ch;
                cursor += 2;
                continue;
            }
            out[n++] = *cursor++;
        }
        out[n] = '\0';
        return true;
    }
    // Bare true/false/null/number — enough for hub "ms": 123.
    size_t n = 0;
    while (*cursor != '\0' && *cursor != ',' && *cursor != '}' &&
           *cursor != ' ' && *cursor != '\n' && n + 1 < cap) {
        out[n++] = *cursor++;
    }
    out[n] = '\0';
    return n > 0;
}

#endif  // HEARTH_UTIL_H_
