#include "joker/mac_address.hpp"
#include <cstdio>
#include <functional>

namespace joker {

namespace {
int ParseHexChar(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}
} // namespace

std::optional<MacAddress> MacAddress::Parse(std::string_view text) {
    if (text.size() != 17) return std::nullopt;

    std::array<uint8_t, kLength> bytes{};
    char sep = text[2];
    if (sep != ':' && sep != '-') return std::nullopt;

    for (size_t i = 0; i < kLength; ++i) {
        size_t offset = i * 3;
        if (i < kLength - 1 && text[offset + 2] != sep) return std::nullopt;

        int h1 = ParseHexChar(text[offset]);
        int h2 = ParseHexChar(text[offset + 1]);
        if (h1 == -1 || h2 == -1) return std::nullopt;

        bytes[i] = static_cast<uint8_t>((h1 << 4) | h2);
    }

    return MacAddress(bytes);
}

std::string MacAddress::ToString() const {
    char buf[18];
    std::snprintf(buf, sizeof(buf), "%02x:%02x:%02x:%02x:%02x:%02x",
                  bytes_[0], bytes_[1], bytes_[2], bytes_[3], bytes_[4], bytes_[5]);
    return std::string(buf);
}

bool MacAddress::IsBroadcast() const {
    for (uint8_t b : bytes_) {
        if (b != 0xFF) return false;
    }
    return true;
}

bool MacAddress::IsZero() const {
    for (uint8_t b : bytes_) {
        if (b != 0x00) return false;
    }
    return true;
}

size_t MacAddressHash::operator()(const MacAddress& m) const noexcept {
    const auto& b = m.bytes();
    uint64_t val = 0;
    for (int i = 0; i < 6; ++i) {
        val = (val << 8) | b[i];
    }
    return std::hash<uint64_t>{}(val);
}

}  // namespace joker
