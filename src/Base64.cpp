#include "Base64.h"

#include <array>

namespace vwave {

namespace {

constexpr char kAlphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

int decodeChar(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

} // namespace

std::string base64Encode(const uint8_t* data, size_t size) {
    std::string out;
    out.reserve(((size + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < size) {
        const uint32_t v = (static_cast<uint32_t>(data[i]) << 16) |
                           (static_cast<uint32_t>(data[i + 1]) << 8) |
                           static_cast<uint32_t>(data[i + 2]);
        out.push_back(kAlphabet[(v >> 18) & 0x3f]);
        out.push_back(kAlphabet[(v >> 12) & 0x3f]);
        out.push_back(kAlphabet[(v >> 6) & 0x3f]);
        out.push_back(kAlphabet[v & 0x3f]);
        i += 3;
    }
    if (i < size) {
        uint32_t v = static_cast<uint32_t>(data[i]) << 16;
        if (i + 1 < size) v |= static_cast<uint32_t>(data[i + 1]) << 8;
        out.push_back(kAlphabet[(v >> 18) & 0x3f]);
        out.push_back(kAlphabet[(v >> 12) & 0x3f]);
        out.push_back(i + 1 < size ? kAlphabet[(v >> 6) & 0x3f] : '=');
        out.push_back('=');
    }
    return out;
}

bool base64Decode(const std::string& text, std::vector<uint8_t>& out) {
    out.clear();
    if (text.size() % 4 != 0) return false;
    out.reserve((text.size() / 4) * 3);
    for (size_t i = 0; i < text.size(); i += 4) {
        const int a = decodeChar(text[i]);
        const int b = decodeChar(text[i + 1]);
        const int c = text[i + 2] == '=' ? -2 : decodeChar(text[i + 2]);
        const int d = text[i + 3] == '=' ? -2 : decodeChar(text[i + 3]);
        if (a < 0 || b < 0 || c == -1 || d == -1) return false;
        if (c == -2 && d != -2) return false;
        const uint32_t value = (static_cast<uint32_t>(a) << 18) |
                               (static_cast<uint32_t>(b) << 12) |
                               ((c == -2 ? 0 : static_cast<uint32_t>(c)) << 6) |
                               (d == -2 ? 0 : static_cast<uint32_t>(d));
        out.push_back(static_cast<uint8_t>((value >> 16) & 0xff));
        if (c != -2) out.push_back(static_cast<uint8_t>((value >> 8) & 0xff));
        if (d != -2) out.push_back(static_cast<uint8_t>(value & 0xff));
    }
    return true;
}

} // namespace vwave
