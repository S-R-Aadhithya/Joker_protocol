#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <optional>
#include <compare>

namespace joker {

class MacAddress {
public:
    static constexpr size_t kLength = 6;

    MacAddress() = default;
    explicit MacAddress(const std::array<uint8_t, kLength>& bytes) : bytes_(bytes) {}

    // Parses "aa:bb:cc:dd:ee:ff" (colon or dash separated). Returns nullopt on
    // any malformed input — never throws, never partially parses.
    static std::optional<MacAddress> Parse(std::string_view text);

    // Formats as lowercase colon-separated hex, always 17 chars.
    [[nodiscard]] std::string ToString() const;

    [[nodiscard]] const std::array<uint8_t, kLength>& bytes() const { return bytes_; }

    [[nodiscard]] bool IsBroadcast() const;   // ff:ff:ff:ff:ff:ff
    [[nodiscard]] bool IsZero() const;        // 00:00:00:00:00:00 — treated as invalid

    auto operator<=>(const MacAddress&) const = default;  // enables deterministic
                                                            // tie-break sort (§9 arch)
    bool operator==(const MacAddress&) const = default;

private:
    std::array<uint8_t, kLength> bytes_{};
};

// Hash support for use as unordered_map/unordered_set key (neighbor table,
// dedup cache keyed partially by MAC, etc.)
struct MacAddressHash {
    size_t operator()(const MacAddress& m) const noexcept;
};

}  // namespace joker
