#pragma once

#include <cstdint>

namespace VCX::Labs::labf {

// UTF-8 decoding helper
// Returns the decoded Unicode codepoint and advances ptr
// Returns 0xFFFD (replacement character) for invalid sequences
inline uint32_t DecodeUTF8(const char*& ptr, const char* end) {
    if (ptr >= end) return 0;

    uint8_t c = static_cast<uint8_t>(*ptr++);

    if ((c & 0x80) == 0) {
        return c;
    }

    uint32_t codepoint = 0;
    int extraBytes = 0;

    if ((c & 0xE0) == 0xC0) {
        codepoint = c & 0x1F;
        extraBytes = 1;
    } else if ((c & 0xF0) == 0xE0) {
        codepoint = c & 0x0F;
        extraBytes = 2;
    } else if ((c & 0xF8) == 0xF0) {
        codepoint = c & 0x07;
        extraBytes = 3;
    } else {
        return 0xFFFD;
    }

    for (int i = 0; i < extraBytes && ptr < end; ++i) {
        uint8_t b = static_cast<uint8_t>(*ptr++);
        if ((b & 0xC0) != 0x80) {
            return 0xFFFD;
        }
        codepoint = (codepoint << 6) | (b & 0x3F);
    }

    return codepoint;
}

} // namespace VCX::Labs::labf
