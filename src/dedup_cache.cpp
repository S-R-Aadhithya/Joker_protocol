#include "joker/dedup_cache.hpp"

namespace joker {

bool DedupCache::Contains(uint32_t packet_id) const {
    return index_.find(packet_id) != index_.end();
}

void DedupCache::Insert(uint32_t packet_id) {
    if (Contains(packet_id)) {
        // Move to back (most recently used)
        auto it = index_[packet_id];
        lru_order_.splice(lru_order_.end(), lru_order_, it);
        it->inserted_at = std::chrono::steady_clock::now();
        return;
    }

    if (index_.size() >= capacity_) {
        // Evict oldest (front of list)
        uint32_t oldest_id = lru_order_.front().packet_id;
        index_.erase(oldest_id);
        lru_order_.pop_front();
    }

    // Insert new at the back
    lru_order_.push_back({packet_id, std::chrono::steady_clock::now()});
    index_[packet_id] = std::prev(lru_order_.end());
}

void DedupCache::EvictExpired(std::chrono::steady_clock::time_point now) {
    while (!lru_order_.empty()) {
        auto& oldest = lru_order_.front();
        if (now - oldest.inserted_at > ttl_) {
            index_.erase(oldest.packet_id);
            lru_order_.pop_front();
        } else {
            // Since the list is ordered by insertion time (LRU),
            // once we hit a non-expired entry, the rest are also not expired.
            break;
        }
    }
}

}  // namespace joker
