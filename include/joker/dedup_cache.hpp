#pragma once
#include <cstdint>
#include <chrono>
#include <unordered_map>
#include <list>

namespace joker {

// [ENGINEERING] Bounded, TTL-evicting cache of recently-seen packet IDs.
// An unbounded cache is unacceptable: under sustained traffic or a
// duplicate-flooding attack (see ARCHITECTURE.md §17 Security), memory
// would grow without limit. This uses an LRU list + hash map for O(1)
// insert/lookup/evict.
class DedupCache {
public:
    explicit DedupCache(size_t capacity, std::chrono::milliseconds ttl)
        : capacity_(capacity), ttl_(ttl) {}

    [[nodiscard]] bool Contains(uint32_t packet_id) const;
    void Insert(uint32_t packet_id);
    void EvictExpired(std::chrono::steady_clock::time_point now);   // periodic sweep

private:
    struct Entry {
        uint32_t packet_id;
        std::chrono::steady_clock::time_point inserted_at;
    };
    size_t capacity_;
    std::chrono::milliseconds ttl_;
    std::list<Entry> lru_order_;
    std::unordered_map<uint32_t, std::list<Entry>::iterator> index_;
};

}  // namespace joker
