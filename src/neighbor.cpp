#include "joker/neighbor.hpp"

namespace joker {

void NeighborTable::Update(const NeighborEntry& fresh) {
    table_[fresh.mac] = fresh;
}

void NeighborTable::ExpireStale(std::chrono::steady_clock::time_point now,
                                 std::chrono::milliseconds expiry) {
    for (auto it = table_.begin(); it != table_.end(); ) {
        if (it->second.IsStale(now, expiry)) {
            it = table_.erase(it);
        } else {
            ++it;
        }
    }
}

std::optional<NeighborEntry> NeighborTable::Lookup(const MacAddress& mac) const {
    auto it = table_.find(mac);
    if (it != table_.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<NeighborEntry> NeighborTable::AllFresh(
    std::chrono::steady_clock::time_point now,
    std::chrono::milliseconds expiry) const {
    
    std::vector<NeighborEntry> fresh;
    for (const auto& [mac, entry] : table_) {
        if (!entry.IsStale(now, expiry)) {
            fresh.push_back(entry);
        }
    }
    return fresh;
}

}  // namespace joker
