#pragma once
#include <chrono>
#include <optional>
#include <unordered_map>
#include <vector>
#include "joker/mac_address.hpp"

namespace joker {

struct NeighborEntry {
    MacAddress mac;
    std::chrono::steady_clock::time_point last_seen;

    uint8_t  tq_local   = 0;
    uint8_t  tq_recv    = 0;
    double   f_asym     = 1.0;
    double   hop_penalty = 1.0;

    double   rx_power_dbm = 0.0;

    bool     reachable_to_dest = false;

    [[nodiscard]] bool IsStale(std::chrono::steady_clock::time_point now,
                                std::chrono::milliseconds expiry) const {
        return (now - last_seen) > expiry;
    }
};

class NeighborTable {
public:
    void Update(const NeighborEntry& fresh);
    void ExpireStale(std::chrono::steady_clock::time_point now,
                      std::chrono::milliseconds expiry);
    [[nodiscard]] std::optional<NeighborEntry> Lookup(const MacAddress& mac) const;
    [[nodiscard]] std::vector<NeighborEntry> AllFresh(
        std::chrono::steady_clock::time_point now,
        std::chrono::milliseconds expiry) const;

private:
    std::unordered_map<MacAddress, NeighborEntry, MacAddressHash> table_;
};

}  // namespace joker
