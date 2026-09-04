#include "joker/header.hpp"
#include <algorithm>
#include <unordered_set>

namespace joker {

namespace {
void put_u32_be(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>(v & 0xFF));
}

std::optional<uint32_t> get_u32_be(const std::vector<uint8_t>& data, size_t off) {
    if (off + 4 > data.size()) return std::nullopt;
    return (static_cast<uint32_t>(data[off]) << 24) |
           (static_cast<uint32_t>(data[off + 1]) << 16) |
           (static_cast<uint32_t>(data[off + 2]) << 8) |
            static_cast<uint32_t>(data[off + 3]);
}
}  // namespace

bool serialize_header(const JokerHeader& header, std::vector<uint8_t>& out) {
    out.push_back(static_cast<uint8_t>(header.type));
    out.push_back(header.ttl);
    out.push_back(static_cast<uint8_t>(header.other_candidates.size())); // [ENGINEERING DEVIATION]
    put_u32_be(out, header.packet_id);
    out.insert(out.end(), header.final_destination.bytes().begin(),
                          header.final_destination.bytes().end());
    for (const auto& c : header.other_candidates) {
        out.insert(out.end(), c.bytes().begin(), c.bytes().end());
    }
    return true;
}

std::optional<JokerHeader> deserialize_header(const std::vector<uint8_t>& data,
                                               size_t& consumed) {
    constexpr size_t kFixedSize = 1 + 1 + 1 + 4 + 6;  // type + ttl + num_cands + id + dest MAC
    if (data.size() < kFixedSize) return std::nullopt;

    JokerHeader h{};
    size_t off = 0;

    if (data[off] > static_cast<uint8_t>(PacketType::kForwarding))
        return std::nullopt;   // unknown packet type
    h.type = static_cast<PacketType>(data[off]); off += 1;

    h.ttl = data[off]; off += 1;
    h.num_candidates = data[off]; off += 1; // [ENGINEERING DEVIATION]

    auto id = get_u32_be(data, off);
    if (!id) return std::nullopt;
    h.packet_id = *id; off += 4;

    std::array<uint8_t, 6> dest{};
    std::copy_n(data.begin() + off, 6, dest.begin());
    h.final_destination = MacAddress(dest);
    off += 6;

    // Read exactly num_candidates MAC addresses
    size_t remaining = data.size() - off;
    size_t expected_candidate_bytes = static_cast<size_t>(h.num_candidates) * 6;
    if (remaining < expected_candidate_bytes) return std::nullopt;
    
    h.other_candidates.reserve(h.num_candidates);
    for (size_t i = 0; i < h.num_candidates; ++i) {
        std::array<uint8_t, 6> mac{};
        std::copy_n(data.begin() + off, 6, mac.begin());
        h.other_candidates.emplace_back(mac);
        off += 6;
    }

    consumed = off;
    return h;
}

bool validate_header(const JokerHeader& header, uint8_t max_candidates) {
    if (header.final_destination.IsBroadcast() || header.final_destination.IsZero()) return false;
    
    // total candidates = 1 (top candidate in link layer) + other_candidates.size()
    if (1 + header.other_candidates.size() > max_candidates) return false;
    
    // check for duplicate candidate MACs
    std::unordered_set<MacAddress, MacAddressHash> seen;
    for (const auto& c : header.other_candidates) {
        if (!seen.insert(c).second) {
            return false;
        }
    }

    return true;
}

}  // namespace joker
