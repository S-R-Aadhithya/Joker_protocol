#pragma once
#include <cstdint>
#include <vector>
#include <optional>
#include "joker/mac_address.hpp"

namespace joker {

enum class PacketType : uint8_t {
    kUnicast    = 0,  // [PAPER] data packet
    kAck        = 1,  // [PAPER]
    kForwarding = 2,  // [PAPER]
};

// [PAPER] Protocol invariant: fixed_overhead = 12 bytes; each additional
// candidate beyond the first adds 6 bytes.
// header_size(Ncandidates) = 12 + 6 * (Ncandidates - 1)
struct JokerHeader {
    PacketType   type;                 // 1 byte
    uint8_t      ttl;                  // 1 byte  [PAPER] default 32
    uint8_t      num_candidates;       // 1 byte  [ENGINEERING DEVIATION: Required to parse variable length header]
    uint32_t     packet_id;            // 4 bytes [PAPER] CRC-32 of payload
    MacAddress   final_destination;    // 6 bytes [PAPER]
    
    // Candidates EXCLUDING the highest-priority one (that MAC travels in the
    // link-layer header instead — [PAPER]).
    std::vector<MacAddress> other_candidates;  // 6 bytes each [PAPER]

    [[nodiscard]] size_t WireSize() const {
        return 13 + 6 * other_candidates.size(); // 13 because we added 1 byte num_candidates
    }
};

// Serializes `header` into `out`, appending. Returns false if
// other_candidates.size() would overflow the configured max candidate count.
bool serialize_header(const JokerHeader& header, std::vector<uint8_t>& out);

// Parses a JokerHeader from `data` (starting at offset 0). On success,
// advances `consumed` to the number of bytes read and returns the header.
// On any malformed input (truncated buffer, invalid packet-type byte,
// candidate list larger than configured max) returns std::nullopt and
// consumes nothing.
std::optional<JokerHeader> deserialize_header(const std::vector<uint8_t>& data,
                                               size_t& consumed);

// Structural validation beyond parse success: rejects headers with TTL
// already 0, duplicate candidate MACs, final_destination == broadcast/zero,
// or candidate count outside [0, kMaxCandidates - 1].
bool validate_header(const JokerHeader& header, uint8_t max_candidates);

}  // namespace joker
